# Elmo Platinum Series — Overview (as an EtherCAT SubDevice)

**Primary source:** Elmo *Platinum Administrative Guide* — `MAN-P-ADMINGUIDE.pdf`
(Ver. 1.002, Nov 2022), located in the workspace root. Where this guide points to a
separate **"EtherCAT (ECAT) Manual"** / **"EtherCAT Application Manual"** for the
low-level ECAT PDO procedure, that is called out explicitly below. Object indices
were also cross-checked against the open-source ETH Zurich `elmo_ethercat_sdk`
(GPLv3), which is known-good against Elmo Gold firmware — noting that **Platinum uses
configurable PDO mapping** whereas the SDK targeted Gold's fixed predefined PDOs
(see [03-elmo-pdo-and-syncmanager.md](03-elmo-pdo-and-syncmanager.md)).

## What the Platinum drive is, from the master's point of view

- An **EtherCAT SubDevice (slave)** with a **CoE** mailbox implementing the
  **CiA 402 (DS402) drive profile**. This is the same profile our theory docs cover
  in [../ethercat/07-cia402-drive-profile.md](../ethercat/07-cia402-drive-profile.md).
- Supports **Distributed Clocks** with SYNC0 (and SYNC0/1); cycle times down to
  ~100 µs on capable models.
- Supports **FSoE (Fail Safe over EtherCAT)** for functional-safety variants (STO
  and more) — carried in dedicated safety PDOs/objects (0x1700/0x1B00, 0x6600–0x67FF).
- Supported motion modes (CiA 402): **CSP** (0x08, cyclic sync position — our primary
  mode), **CSV** (0x09), **CST** (0x0A), plus profile modes PP/PV/PT and homing/IP
  where applicable. See [02-elmo-cia402-modes.md](02-elmo-cia402-modes.md).
- **Multi-axis** models exist (e.g. Quartet = 4 axes). The object dictionary encodes
  extra axes by fixed offsets — this is the single most important Platinum-specific
  rule and is detailed in [01-elmo-object-dictionary.md](01-elmo-object-dictionary.md).

## Dual protocol: CANopen and EtherCAT share one OD

The admin guide documents the object dictionary once and marks each object's
**Protocol** (CAN, ECAT, or both) and whether it is **Multi-axes**. Watch for this:

- Objects like `0x1400–0x1403` (rPDO communication parameter, COB-IDs) are **CAN
  only** — irrelevant on EtherCAT.
- The PDO **mapping** objects `0x1600–0x1603` / `0x1A00–0x1A03` are shared, but their
  **ECAT default is "None"**: on EtherCAT the *master must build the mapping* (in
  PRE-OP), unlike CAN which ships predefined COB-ID mappings. This is a key
  difference and shapes our bring-up (see
  [03-elmo-pdo-and-syncmanager.md](03-elmo-pdo-and-syncmanager.md)).

## Firmware / model caveat (must verify on real hardware)

Exact PDO defaults, per-axis offsets, DC `assign_activate`, and available modes are
ultimately defined by **the specific Platinum model's ESI file** and firmware. Before
writing production config:

1. Run `slaveinfo eth0` (SOEM) against the actual drive to dump its OD, SM/FMMU, DC
   capability, and identity (vendor/product/revision).
2. Load the drive's **ESI (.xml)** and match revision.
3. Consult the separate Elmo **EtherCAT Application Manual** for the exact ECAT PDO
   procedure the admin guide defers to.

Treat everything in these docs as the **verified starting model**; confirm against
the live drive's SII/ESI before committing numbers to production code.

## How these Elmo docs are organized

| File | Contents |
|------|----------|
| [01-elmo-object-dictionary.md](01-elmo-object-dictionary.md) | CiA402 + Elmo-specific objects, and the **multi-axis offset rules** (+0x800 for DS402, +0x10 for PDO map objects). |
| [02-elmo-cia402-modes.md](02-elmo-cia402-modes.md) | State machine + CSP/CSV/CST details, watchdog/extrapolation timeout, control/status word bits. |
| [03-elmo-pdo-and-syncmanager.md](03-elmo-pdo-and-syncmanager.md) | Sync managers 0x1C1x/0x1C3x, PDO mapping element format, exact example PDO layouts, DC objects. |
| [04-elmo-bringup-with-soem.md](04-elmo-bringup-with-soem.md) | End-to-end CSP bring-up with SOEM: SDO sequence, PO→SO hook, cyclic loop, enable sequence. |
