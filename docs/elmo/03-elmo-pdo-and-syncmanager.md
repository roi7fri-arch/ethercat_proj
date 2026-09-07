# Elmo Platinum — Sync Managers, PDO Mapping & Distributed Clocks

Source: `MAN-P-ADMINGUIDE.pdf` §5.5 (rPDO/tPDO mapping, verbatim rules below) plus
the ETH Zurich SDK for concrete example layouts. For the exact ECAT PDO procedure the
admin guide defers to the separate Elmo **"ECAT Manual" / EtherCAT Application
Manual** — confirm final numbers there and against the drive's ESI.

## 1. Sync Managers and PDO assignment

| Object | Role |
|--------|------|
| `0x1C00` | SM communication type (SM0/1 = mailbox, SM2 = outputs/rPDO, SM3 = inputs/tPDO) |
| `0x1C12` | **SM2 PDO assign** — list of rPDO **mapping** objects active for outputs |
| `0x1C13` | **SM3 PDO assign** — list of tPDO **mapping** objects active for inputs |
| `0x1C32` | SM2 synchronization (output) — DC/SYNC cycle time, shift, sync type |
| `0x1C33` | SM3 synchronization (input) — DC/SYNC cycle time, shift, sync type |

Two-level indirection (standard CoE):
`0x1C12` → points to one or more mapping objects (e.g. `0x1600`) → each mapping object
lists the actual OD entries. Assemble mapping (`0x160x`) first, then assign it
(`0x1C12`).

## 2. PDO mapping element format (verbatim from admin guide §5.5.1)

- Each mapping entry (data type **object 0x21**) is a **32-bit field**:

  ```
  MSB                                                              LSB
  | Index : 16 bits | Sub-index : 8 bits | Object length : 8 bits (in BITS) |
  ```

  Example: controlword `0x6040`, sub 0, 16 bits → `0x60400010`.
- **Maximum 8 objects** mapped to a single PDO → max 8×8 = 64 *bytes* of data per PDO
  (the guide's "8×8=64 bits" refers to 8 entries; watch the ESI for true byte counts).
- **ECAT Sync Manager 2 maximum size is 128 bytes.**
- Write access to mapping objects is permitted **in PRE-OP** on EtherCAT (and in
  PRE-OP/OPERATIONAL on CAN).
- **On EtherCAT the default mapping is "None"** — the master must build it. (On CAN
  there are predefined defaults like `0x1600:1 = 0x60400010`.)

### The remap procedure (must run in PRE-OP)

```
1. 0x1C12:00 = 0                 // deactivate SM2 assignment
2. 0x1600:00 = 0                 // deactivate the mapping object
3. 0x1600:01 = 0x60400010        // controlword,  16 bit
   0x1600:02 = 0x607A0020        // target position, 32 bit
   ...                           // up to 8 entries
4. 0x1600:00 = <count>           // reactivate mapping (drive re-validates integrity)
5. 0x1C12:01 = 0x1600            // assign mapping object to SM2
6. 0x1C12:00 = 1                 // activate assignment (count of assigned objects)
```

Symmetric for inputs: `0x1C13` / `0x1A00`. If a mapping is illegal (too long, or an
unmappable object) the drive aborts the SDO with `0x06020000`, `0x06040041`, or
`0x06040042`. **Order matters**: sub-index 0 must be zeroed before editing entries.

> Multi-axis: axis 2 uses mapping objects `0x1610`/`0x1A10` (+0x10) but the *entries*
> inside them still reference the axis-2 DS402 objects at **+0x800** (e.g. axis-2
> controlword `0x6840` → entry `0x68400010`). See
> [01-elmo-object-dictionary.md](01-elmo-object-dictionary.md).

## 3. Example CSP PDO layout (recommended starting point)

RxPDO (master → drive), assigned via `0x1C12` → `0x1600`:

| Entry | Object | Bits | Field |
|-------|--------|------|-------|
| 1 | `0x60400010` | 16 | controlword |
| 2 | `0x607A0020` | 32 | target position |
| 3 | `0x60B10020` | 32 | velocity offset (feed-forward, optional) |
| 4 | `0x60B20010` | 16 | torque offset (feed-forward, optional) |
| 5 | `0x60600008` | 8 | modes of operation (optional if fixed via SDO) |

TxPDO (drive → master), assigned via `0x1C13` → `0x1A00`:

| Entry | Object | Bits | Field |
|-------|--------|------|-------|
| 1 | `0x60410010` | 16 | statusword |
| 2 | `0x60640020` | 32 | position actual |
| 3 | `0x606C0020` | 32 | velocity actual |
| 4 | `0x60770010` | 16 | torque actual |
| 5 | `0x60F40020` | 32 | following error actual (monitor) |
| 6 | `0x60610008` | 8 | modes of operation display |

Pack these as C structs matching SOEM's `IOmap` (byte order = little-endian on the
wire; mind padding/alignment) — see
[04-elmo-bringup-with-soem.md](04-elmo-bringup-with-soem.md).

> The ETH Zurich SDK (Gold firmware) instead used **fixed predefined** PDO objects
> (e.g. RxPDO assign `{0x1605,0x1618}`, TxPDO `{0x1A03,0x1A1D,0x1A1F,0x1A18}`). Those
> indices are *not* in the Platinum admin guide's documented `0x1600–0x1603` range,
> so **prefer the configurable-mapping approach above for Platinum** and verify the
> drive's ESI for any predefined objects it also exposes.

## 4. FSoE (safety) PDOs

- `0x1700` Safety rPDO = **FSoE Control** container (master → safety slave logic).
- `0x1B00` Safety tPDO = **FSoE Status** container.
- `0x6600–0x67FF` FSoE safety objects.

These ride the same EtherCAT frame but carry the black-channel FSoE payload with its
own CRC/watchdog. Only relevant for functional-safety variants; not part of the
initial motion bring-up.

## 5. Distributed Clocks configuration objects

| Object | Meaning |
|--------|---------|
| `0x1C32` / `0x1C33` | SM2/SM3 sync: **sync type**, **cycle time**, **shift time** — set to match our SYNC0 period |
| `0x2046` | Elmo **ECAT DC Inhibit Time** |
| SYNC0 activation | via SOEM `ecx_dcsync0(ctx, slave, TRUE, CYCLE_NS, shift)` after `ecx_configdc` |

Set `0x60C2` (interpolation time period) consistent with the DC SYNC0 cycle so the
drive's internal interpolator matches our delivery rate. DC phase-locking of the
Linux loop is handled by SOEM's PI servo — see
[../soem/02-soem-linux-rt-and-build.md](../soem/02-soem-linux-rt-and-build.md) §3.

Bring-up glue: [04-elmo-bringup-with-soem.md](04-elmo-bringup-with-soem.md).
