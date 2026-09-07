# Elmo Platinum — Object Dictionary & Multi-Axis Addressing

Source: `MAN-P-ADMINGUIDE.pdf` §5 (Object Dictionary), cross-checked with the ETH
Zurich `elmo_ethercat_sdk` `ObjectDictionary.hpp`. Indices below are hexadecimal.

## 1. Object dictionary map (single axis / axis 1)

| Range | Contents |
|-------|----------|
| `0x1000–0x1018` | Device type, error register, identity (CoE standard) |
| `0x1400–0x1403` | rPDO **communication** parameter — **CAN only** (COB-IDs) |
| `0x1600–0x1603` | rPDO **mapping** parameter (master→drive). ECAT: multi-axis, default *None* |
| `0x1700 / 0x1701–0x1704` | Safety rPDO (FSoE Control container) / additional ECAT rPDO map |
| `0x1800–0x1803` | tPDO communication parameter — **CAN only** |
| `0x1A00–0x1A03` | tPDO **mapping** parameter (drive→master). ECAT: multi-axis, default *None* |
| `0x1B00` | Safety tPDO (FSoE Status container) |
| `0x1C00` | Sync Manager communication type |
| `0x1C12 / 0x1C13` | SM2 (rPDO) / SM3 (tPDO) **PDO assign** |
| `0x1C32 / 0x1C33` | SM2 / SM3 **synchronization** parameters (DC config) |
| `0x2000–0x2FFF` | Elmo manufacturer-specific status/config (see below) |
| `0x3000–0x35FF` | **Elmo command alias** objects (axis-related, mirror the OS interpreter) |
| `0x3600–0x376F` | Elmo manufacturer objects (e.g. 0x3675 extrapolation timeout) |
| `0x6000–0x65FF` | **DS402 (CiA402) profile** objects — axis 1 |
| `0x6600–0x67FF` | FSoE safety objects |

## 2. Multi-axis addressing — THE critical Platinum rule

A multi-axis Platinum drive (e.g. Quartet, up to **6 axes**) does **not** create new
object indices per axis. It applies **fixed offsets** to the axis-1 ranges:

| Object class | Axis 1 base | Per-axis offset | Example (axis 2) |
|--------------|-------------|-----------------|------------------|
| **DS402 profile** (0x6000–0x65FF) | 0x6040 controlword… | **+0x800** | axis-2 controlword = `0x6040 + 0x800 = 0x6840` |
| **Elmo command alias** (0x3000–0x35FF) | 0x30xx | **+0x800** | axis-2 = `0x3xxx + 0x800` |
| **rPDO mapping** (0x1600–0x1603) | 0x1600… | **+0x10** | axis-2 rPDO map = `0x1610–0x1613` |
| **tPDO mapping** (0x1A00–0x1A03) | 0x1A00… | **+0x10** | axis-2 tPDO map = `0x1A10–0x1A13` |

So for a 4-axis drive, the CiA402 controlword for axis *n* (1-based) is
`0x6040 + (n-1)*0x800`, statusword `0x6041 + (n-1)*0x800`, target position
`0x607A + (n-1)*0x800`, etc. Encode this as a helper in our code:

```c
#define ELMO_AXIS_OFFSET   0x800   /* DS402 objects, per additional axis */
#define ELMO_OD(base, axis) ((uint16)((base) + ((axis) - 1) * ELMO_AXIS_OFFSET))
/* controlword of axis 3: ELMO_OD(0x6040, 3) == 0x7040 */

#define ELMO_PDOMAP_OFFSET 0x10    /* 0x1600/0x1A00 mapping objects, per axis */
```

> Always confirm the axis count and offsets against the live drive's ESI: a
> single-axis Platinum only exposes axis-1 objects.

## 3. CiA 402 profile objects (axis 1 — add +0x800 per extra axis)

| Index | Name | Type | Notes |
|-------|------|------|-------|
| `0x603F` | Error code | U16 | last DS402 error |
| `0x6040` | **Controlword** | U16 | state machine commands (RxPDO) |
| `0x6041` | **Statusword** | U16 | state machine status (TxPDO) |
| `0x605A` | Quick stop option code | I16 | |
| `0x605B` | Shutdown option code | I16 | |
| `0x605C` | Disable operation option code | I16 | |
| `0x605D` | Halt option code | I16 | controlword bit 8 behavior |
| `0x605E` | Fault reaction option code | I16 | |
| `0x6060` | **Modes of operation** | I8 | 8=CSP, 9=CSV, 10=CST (set before OP) |
| `0x6061` | Modes of operation display | I8 | echoes active mode |
| `0x6062` | Position demand value | I32 | |
| `0x6064` | **Position actual value** | I32 | TxPDO |
| `0x606C` | Velocity actual value | I32 | TxPDO |
| `0x6071` | **Target torque** | I16 | CST (RxPDO) |
| `0x6072` | Max torque | U16 | |
| `0x6073` | Max current | U16 | |
| `0x6074` | Torque demand | I16 | |
| `0x6075` | Motor rated current | U32 | |
| `0x6076` | Motor rated torque | U32 | |
| `0x6077` | **Torque actual value** | I16 | TxPDO |
| `0x6078` | Current actual value | I16 | |
| `0x6079` | DC link (bus) voltage | U32 | |
| `0x607A` | **Target position** | I32 | CSP (RxPDO) |
| `0x607D` | Software position limit | I32[2] | |
| `0x607E` | Polarity | U8 | |
| `0x607F` | Max profile velocity | U32 | |
| `0x6080` | Max motor speed | U32 | |
| `0x6081` | Profile velocity | U32 | |
| `0x6083` | Profile acceleration | U32 | |
| `0x6084` | Profile deceleration | U32 | |
| `0x6085` | Quick stop deceleration | U32 | |
| `0x608F` | Position encoder resolution | U32[2] | |
| `0x6091` | Gear ratio | U32[2] | |
| `0x60B0` | Position offset | I32 | CSP feed-forward |
| `0x60B1` | Velocity offset | I32 | CSP/CSV feed-forward |
| `0x60B2` | Torque offset | I16 | CSP/CSV/CST feed-forward |
| `0x60C2` | Interpolation time period | rec | sets the expected cycle |
| `0x60F4` | Following error actual value | I32 | TxPDO (monitor) |
| `0x60FD` | Digital inputs | U32 | |
| `0x60FF` | **Target velocity** | I32 | CSV (RxPDO) |
| `0x6502` | Supported drive modes | U32 | bitmask of modes |

## 4. Elmo manufacturer-specific objects (verified)

| Index | Name / use |
|-------|------------|
| `0x2085` | Extra status |
| `0x2086` | STO (Safe Torque Off) status |
| `0x2046` | ECAT DC Inhibit Time |
| `0x2206` | 5 VDC supply monitor |
| `0x22A3` | Drive temperature |
| `0x2012 / 0x2013` | Binary interpreter command / data |
| `0x3000` | Elmo Commands alias (OS interpreter command map, axis-related) |
| `0x3675` | Extrapolation (cyclic) timeout — used by CSP/CSV/CST watchdog |

The `0x3000–0x35FF` alias objects expose Elmo's **OS interpreter** commands (the
classic two-letter Elmo commands) as CANopen/CoE objects, so they too follow the
**+0x800** per-axis offset. Use DS402 objects for motion; use the alias objects only
for Elmo-specific tuning/diagnostics not covered by DS402.

Continue: [02-elmo-cia402-modes.md](02-elmo-cia402-modes.md) for how these objects
are driven through the state machine and cyclic modes.
