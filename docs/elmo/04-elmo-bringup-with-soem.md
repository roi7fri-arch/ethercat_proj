# Elmo Platinum + SOEM — End-to-End CSP Bring-Up

This ties [../soem/01-soem-api-reference.md](../soem/01-soem-api-reference.md) and
[03-elmo-pdo-and-syncmanager.md](03-elmo-pdo-and-syncmanager.md) into one CSP
bring-up recipe. Pseudocode uses the SOEM v2.0.0 `ecx_*` API. **Verify object
numbers and PDO layout against the drive's ESI + the Elmo ECAT Manual first.**

## 0. PDO image structs (match the mapping from doc 03)

```c
#include "soem/soem.h"
#include <stdint.h>

#pragma pack(push, 1)
typedef struct {          /* RxPDO: master -> drive (SM2 / 0x1600) */
    uint16_t controlword;     /* 0x6040 */
    int32_t  target_position; /* 0x607A */
    int32_t  velocity_offset; /* 0x60B1 */
    int16_t  torque_offset;   /* 0x60B2 */
    int8_t   mode;            /* 0x6060 */
} elmo_rxpdo_t;

typedef struct {          /* TxPDO: drive -> master (SM3 / 0x1A00) */
    uint16_t statusword;      /* 0x6041 */
    int32_t  position_actual; /* 0x6064 */
    int32_t  velocity_actual; /* 0x606C */
    int16_t  torque_actual;   /* 0x6077 */
    int32_t  following_error; /* 0x60F4 */
    int8_t   mode_display;    /* 0x6061 */
} elmo_txpdo_t;
#pragma pack(pop)
```

## 1. PO→SO configuration hook (runs at PRE-OP → SAFE-OP, per slave)

Register this on each Elmo slave *before* `ecx_config_map_group`, by setting
`ctx.slavelist[s].PO2SOconfig = elmo_po2so`. Here we build the PDO mapping and set
the mode. Helpers:

```c
static int sdo_w8 (ecx_contextt *c, uint16 s, uint16 idx, uint8 sub, uint8  v){
    return ecx_SDOwrite(c,s,idx,sub,FALSE,sizeof v,&v,EC_TIMEOUTRXM); }
static int sdo_w32(ecx_contextt *c, uint16 s, uint16 idx, uint8 sub, uint32 v){
    return ecx_SDOwrite(c,s,idx,sub,FALSE,sizeof v,&v,EC_TIMEOUTRXM); }

static int elmo_po2so(ecx_contextt *c, uint16 s) {
    /* --- RxPDO map 0x1600 (see doc 03 §2 order) --- */
    sdo_w8 (c, s, 0x1C12, 0, 0);          /* clear SM2 assign */
    sdo_w8 (c, s, 0x1600, 0, 0);          /* clear mapping    */
    sdo_w32(c, s, 0x1600, 1, 0x60400010); /* controlword  16b */
    sdo_w32(c, s, 0x1600, 2, 0x607A0020); /* target pos   32b */
    sdo_w32(c, s, 0x1600, 3, 0x60B10020); /* vel offset   32b */
    sdo_w32(c, s, 0x1600, 4, 0x60B20010); /* torque off   16b */
    sdo_w32(c, s, 0x1600, 5, 0x60600008); /* mode          8b */
    sdo_w8 (c, s, 0x1600, 0, 5);          /* activate: 5 entries */
    sdo_w8 (c, s, 0x1C12, 1, 0x1600 & 0xFF); /* NB: assign is U16 -> */
    { uint16 m = 0x1600; ecx_SDOwrite(c,s,0x1C12,1,FALSE,sizeof m,&m,EC_TIMEOUTRXM); }
    sdo_w8 (c, s, 0x1C12, 0, 1);          /* one object assigned */

    /* --- TxPDO map 0x1A00 --- */
    sdo_w8 (c, s, 0x1C13, 0, 0);
    sdo_w8 (c, s, 0x1A00, 0, 0);
    sdo_w32(c, s, 0x1A00, 1, 0x60410010); /* statusword   16b */
    sdo_w32(c, s, 0x1A00, 2, 0x60640020); /* pos actual   32b */
    sdo_w32(c, s, 0x1A00, 3, 0x606C0020); /* vel actual   32b */
    sdo_w32(c, s, 0x1A00, 4, 0x60770010); /* torque act   16b */
    sdo_w32(c, s, 0x1A00, 5, 0x60F40020); /* follow err   32b */
    sdo_w32(c, s, 0x1A00, 6, 0x60610008); /* mode disp     8b */
    sdo_w8 (c, s, 0x1A00, 0, 6);
    { uint16 m = 0x1A00; ecx_SDOwrite(c,s,0x1C13,1,FALSE,sizeof m,&m,EC_TIMEOUTRXM); }
    sdo_w8 (c, s, 0x1C13, 0, 1);

    /* --- mode + interpolation period + extrapolation timeout --- */
    sdo_w8 (c, s, 0x6060, 0, 8);          /* CSP */
    /* 0x60C2 interpolation time period: e.g. 1 ms = index1=1, index2=-3 (10^-3) */
    sdo_w8 (c, s, 0x60C2, 1, 1);
    { int8 e = -3; ecx_SDOwrite(c,s,0x60C2,2,FALSE,sizeof e,&e,EC_TIMEOUTRXM); }
    return 1; /* success */
}
```

> For multi-axis: repeat with `0x1610/0x1A10` mapping objects whose entries target
> the axis-2 DS402 objects at **+0x800** (controlword `0x6840` → `0x68400010`, etc.).

## 2. Master init sequence

```c
ecx_init(&ctx, "eth0");
ecx_config_init(&ctx);
for (int s = 1; s <= ctx.slavecount; s++)
    if (is_elmo(&ctx.slavelist[s]))
        ctx.slavelist[s].PO2SOconfig = elmo_po2so;   /* hook runs during map */
ecx_config_map_group(&ctx, IOmap, 0);                /* -> SAFE_OP */
ecx_configdc(&ctx);

elmo_rxpdo_t *rx = (elmo_rxpdo_t *)ctx.slavelist[1].outputs;
elmo_txpdo_t *tx = (elmo_txpdo_t *)ctx.slavelist[1].inputs;
int expectedWKC = ctx.grouplist[0].outputsWKC*2 + ctx.grouplist[0].inputsWKC;
```

## 3. Reach OP with cyclic traffic (DC needs live frames first)

```c
/* prime the outputs so the drive sees valid data */
rx->controlword = 0x0000; rx->target_position = tx->position_actual; rx->mode = 8;
ecx_send_processdata(&ctx); ecx_receive_processdata(&ctx, EC_TIMEOUTRET);

for (int s = 1; s <= ctx.slavecount; s++)
    ecx_dcsync0(&ctx, s, TRUE, CYCLE_NS, CYCLE_NS/2);   /* SYNC0 = cycle, shift */

ctx.slavelist[0].state = EC_STATE_OPERATIONAL;
ecx_writestate(&ctx, 0);
/* keep sending while waiting so watchdog/DC stay alive */
for (int i = 0; i < 200; i++) {
    ecx_send_processdata(&ctx); ecx_receive_processdata(&ctx, EC_TIMEOUTRET);
    if (ecx_statecheck(&ctx,0,EC_STATE_OPERATIONAL,50000) == EC_STATE_OPERATIONAL) break;
}
```

## 4. CiA 402 enable + CSP control (inside the RT loop from SOEM doc 02 §2)

```c
enum { ST_MASK = 0x006F };
for (;;) {
    /* --- read inputs --- */
    uint16 sw = tx->statusword;

    /* --- CiA402 enable ladder (only advance when the prior state is confirmed) --- */
    if      ((sw & 0x004F) == 0x0040) rx->controlword = 0x0006;   /* SwOnDisabled -> Ready */
    else if ((sw & 0x006F) == 0x0021) rx->controlword = 0x0007;   /* Ready -> Switched On  */
    else if ((sw & 0x006F) == 0x0023) rx->controlword = 0x000F;   /* SwitchedOn -> OpEnable*/
    else if ((sw & 0x006F) == 0x0027) {                           /* Operation Enabled     */
        /* --- CSP setpoint: hold, or apply trajectory --- */
        rx->controlword    = 0x000F;
        rx->target_position = next_setpoint();   /* NEW value every cycle! */
        rx->velocity_offset = 0;
        rx->torque_offset   = 0;
    }
    if (sw & 0x0008) rx->controlword = 0x0080;   /* Fault -> pulse reset */

    rx->mode = 8;

    /* --- exchange --- */
    ecx_send_processdata(&ctx);
    int wkc = ecx_receive_processdata(&ctx, EC_TIMEOUTRET);
    if (wkc < expectedWKC) flag_comm_fault();     /* risks 0x3675 quick-stop */
    ecx_mbxhandler(&ctx, 0, 4);

    sleep_until_next_dc_phase();   /* clock_nanosleep ABSTIME + PI offset */
}
```

## 5. Critical correctness notes

- **Deliver a fresh `target_position` every cycle** in CSP. Repeating a stale value
  or missing cycles trips the extrapolation timeout (`0x3675`) → quick-stop
  (`0x605A`). WKC < expected must be treated as a fault.
- **Seed `target_position = position_actual`** before enabling, or the axis jumps.
- **Mapping edits only in PRE-OP** — that's why they live in the PO→SO hook.
- The `0x1C12`/`0x1C13` assign sub-index 1 is a **U16** (the mapping object index),
  not a byte — use a `uint16` SDO write (shown above).
- Confirm struct sizes equal `ctx.slavelist[s].Obytes` / `.Ibytes` after mapping;
  a mismatch means the ESI/mapping disagrees with the struct (fix padding).
- Multi-axis: one `outputs`/`inputs` block per drive may contain **all axes**
  concatenated — index each axis's sub-struct by offset, not a new slave.

## 6. Order of operations (summary)

`ecx_init` → `ecx_config_init` → set `PO2SOconfig` (Elmo PDO map + mode) →
`ecx_config_map_group` (SAFE-OP) → `ecx_configdc` → prime IOmap → `ecx_dcsync0` →
request OP while cycling → RT loop: CiA402 enable ladder → CSP setpoints → monitor
WKC/statusword. Recovery via `ecx_recover_slave`/`ecx_reconfig_slave` off the RT core.
