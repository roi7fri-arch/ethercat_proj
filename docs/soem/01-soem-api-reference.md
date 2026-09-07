# SOEM v2.0.0 — API Reference (the calls we actually use)

All functions take a context `ecx_contextt *ctx`. Include with `#include "soem/soem.h"`.
Signatures below are copied from `third_party/SOEM/include/soem/`.

## 1. The context and the process image

```c
static ecx_contextt ctx;      /* one per EtherCAT network/NIC */
static uint8 IOmap[4096];     /* process image; SOEM maps all PDOs into this */
```

The context holds everything: `ctx.slavelist[]` (index 0 = the master/aggregate,
1..N = slaves), `ctx.slavecount`, `ctx.grouplist[]`, `ctx.DCtime` (current DC system
time in ns), port/socket state. After configuration each slave exposes:

```c
ec_slavet *s = &ctx.slavelist[i];
s->outputs;  s->Obytes;   // pointer+size into IOmap for this slave's RxPDO (master→slave)
s->inputs;   s->Ibytes;   // pointer+size into IOmap for this slave's TxPDO (slave→master)
s->state;    s->ALstatuscode;
s->hasdc;    s->configadr; // fixed (station) address
s->CoEdetails;             // != 0 if CoE mailbox present
s->name; s->eep_man; s->eep_id; s->eep_rev;   // SII identity
```

> **WKC / process-image direction:** outputs are what *we* send to the drive
> (targets, controlword); inputs are what the drive returns (actuals, statusword).

## 2. Lifecycle calls (init → OP → shutdown)

```c
/* Bind to NIC via raw socket. ifname e.g. "eth0". Returns >0 on success. */
int  ecx_init(ecx_contextt *ctx, const char *ifname);

/* Scan bus, read SII, set station addresses, fill slavelist[]. Returns slave count. */
int  ecx_config_init(ecx_contextt *ctx);

/* Auto-map all slaves' PDOs of a group into IOmap; sets FMMUs/SMs, Obytes/Ibytes,
   moves slaves to SAFE_OP. Returns the used IOmap size. group 0 = default. */
int  ecx_config_map_group(ecx_contextt *ctx, void *pIOmap, uint8 group);

/* Distributed Clocks: measure propagation delays, pick reference clock. */
boolean ecx_configdc(ecx_contextt *ctx);

/* Per-slave DC SYNC0 (and SYNC0+SYNC1) activation. CyclTime/shift in ns. */
void ecx_dcsync0 (ecx_contextt *ctx, uint16 slave, boolean act, uint32 CyclTime, int32 CyclShift);
void ecx_dcsync01(ecx_contextt *ctx, uint16 slave, boolean act, uint32 CyclTime0, uint32 CyclTime1, int32 CyclShift);

/* State control. Set ctx.slavelist[slave].state then writestate; slave 0 = broadcast. */
int  ecx_writestate(ecx_contextt *ctx, uint16 slave);
uint16 ecx_statecheck(ecx_contextt *ctx, uint16 slave, uint16 reqstate, int timeout);
int  ecx_readstate(ecx_contextt *ctx);
```

Typical bring-up (from `samples/ec_sample/ec_sample.c`):

```c
ecx_init(&ctx, ifname);
ecx_config_init(&ctx);                       // -> ctx.slavecount
ecx_config_map_group(&ctx, IOmap, 0);        // -> SAFE_OP, PDOs mapped
ecx_configdc(&ctx);                          // DC delays + reference
// (optional) per-slave PO->SO config hook runs here; set SDOs for mode/PDO
expectedWKC = grp->outputsWKC * 2 + grp->inputsWKC;
// start cyclic traffic, let DC settle, then:
ctx.slavelist[0].state = EC_STATE_OPERATIONAL;
ecx_writestate(&ctx, 0);
ecx_statecheck(&ctx, 0, EC_STATE_OPERATIONAL, EC_TIMEOUTSTATE);
```

States: `EC_STATE_INIT`, `EC_STATE_PRE_OP`, `EC_STATE_SAFE_OP`,
`EC_STATE_OPERATIONAL`, plus `EC_STATE_ERROR`/`EC_STATE_ACK`.

## 3. The cyclic process-data exchange (the hot path)

```c
/* Queue the current IOmap outputs into a frame and send it. */
int ecx_send_processdata(ecx_contextt *ctx);

/* Receive the returned frame, copy inputs into IOmap, return the Working Counter. */
int ecx_receive_processdata(ecx_contextt *ctx, int timeout);   // EC_TIMEOUTRET
```

Each cycle: write outputs into `IOmap` → `ecx_send_processdata` →
`ecx_receive_processdata` → read inputs from `IOmap` → check `wkc == expectedWKC`.
`ctx.DCtime` is refreshed from the returned frame (used to phase-lock our loop).

## 4. Mailbox / CoE (SDO) — acyclic parameters

```c
/* Read/Write a CoE object dictionary entry (SDO). CA = complete-access. */
int ecx_SDOread (ecx_contextt *ctx, uint16 slave, uint16 index, uint8 subindex,
                 boolean CA, int *psize, void *p, int timeout);          // EC_TIMEOUTRXM
int ecx_SDOwrite(ecx_contextt *ctx, uint16 slave, uint16 index, uint8 subindex,
                 boolean CA, int psize, const void *p, int timeout);
```

PDO-assignment helpers and OD introspection also live in `ec_coe.h`:
`ecx_readPDOmap`, `ecx_readODlist`, `ecx_readODdescription`, `ecx_readOE`.

**v2.0.0 cyclic mailbox handler** — instead of doing blocking SDOs from the RT
thread, register CoE slaves once and let SOEM service their mailboxes inside the
cycle so SDOs can be issued safely from other threads:

```c
ecx_slavembxcyclic(&ctx, slave);   // register slave's mailbox for cyclic handling
// inside the RT loop, after receive_processdata:
ecx_mbxhandler(&ctx, group, limit); // e.g. ecx_mbxhandler(&ctx, 0, 4);
```

## 5. Error recovery (used by the supervisor thread)

```c
int  ecx_reconfig_slave(ecx_contextt *ctx, uint16 slave, int timeout);
int  ecx_recover_slave (ecx_contextt *ctx, uint16 slave, int timeout);
```

`ec_sample` runs these in a separate `ecatcheck` thread when WKC drops or a slave
leaves OP (see [02-soem-linux-rt-and-build.md](02-soem-linux-rt-and-build.md)).

## 6. Adapter discovery + strings

```c
ec_adaptert *ec_find_adapters(void);   // list NICs
void         ec_free_adapters(ec_adaptert *adapter);
const char  *ec_ALstatuscode2string(uint16 ALstatuscode);   // decode AL error
```

## Constants you will reference

| Constant | Meaning |
|----------|---------|
| `EC_TIMEOUTRET` (2000 µs) | tx→rx frame return timeout |
| `EC_TIMEOUTRXM` (700000 µs) | mailbox (SDO) receive timeout |
| `EC_TIMEOUTSTATE` (2000000 µs) | state-change wait |
| `EC_MAXSLAVE` (200), `EC_MAXGROUP` (2) | array sizes (CMake-tunable) |
| `EC_STATE_*` | ESM states |

## Minimal mental model

`ecx_init` (get the NIC) → `ecx_config_init` (find slaves) →
`ecx_config_map_group` (build the process image, reach SAFE_OP) →
`ecx_configdc` + `ecx_dcsync0` (time base) → cyclic
`send`/`receive_processdata` while phase-locking to `ctx.DCtime` → request OP.
Everything Elmo-specific happens as **SDO writes before OP** (PDO map + mode) and
as **struct reads/writes on the IOmap** each cycle — see
[../elmo/04-elmo-bringup-with-soem.md](../elmo/04-elmo-bringup-with-soem.md).
