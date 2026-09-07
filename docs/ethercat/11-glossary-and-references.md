# 11 — Glossary, Abbreviations, Standards & References

## Glossary / abbreviations

| Term | Meaning |
|------|---------|
| **MainDevice / Master** | The only node that actively sends frames; our application |
| **SubDevice / Slave** | A node that processes frames on the fly (drives, I/O) |
| **ESC** | EtherCAT Slave Controller — the slave's frame-processing hardware |
| **PDI** | Process Data Interface — ESC ↔ local application interface |
| **FMMU** | Fieldbus Memory Management Unit — maps logical ↔ physical memory, bit-wise |
| **SM / SyncManager** | Manages coherent buffer access (mailbox or buffered/process) |
| **SII** | SubDevice Information Interface — slave EEPROM with identity/default config |
| **ESI** | EtherCAT Slave Information — XML device description file |
| **ENI** | EtherCAT Network Information — XML network boot config for the master |
| **ESM** | EtherCAT State Machine — INIT/PREOP/SAFEOP/OP/BOOTSTRAP |
| **AL** | Application Layer (AL Control / AL Status / AL Status Code registers) |
| **WKC** | Working Counter — per-datagram consistency counter |
| **DC** | Distributed Clocks — network-wide synchronized time base |
| **SYNC0/SYNC1** | DC-generated hardware sync pulses at the slave |
| **PDO** | Process Data Object — cyclic real-time data (mapped into process image) |
| **RxPDO / TxPDO** | Received by slave (outputs) / Transmitted by slave (inputs) |
| **SDO** | Service Data Object — acyclic object-dictionary access (config) |
| **OD** | Object Dictionary — 16-bit index + 8-bit subindex parameter space |
| **CoE / SoE / FoE / EoE / AoE** | CANopen / Servo / File / Ethernet / ADS over EtherCAT |
| **EMCY** | Emergency message (slave fault notification) |
| **Domain** | (IgH) contiguous logical process-image region exchanged each cycle |
| **CSP / CSV / CST** | Cyclic Synchronous Position / Velocity / Torque (CiA 402 modes) |
| **PP / PV / PT / HM / IP** | Profile Position/Velocity/Torque / Homing / Interpolated |
| **Controlword / Statusword** | 0x6040 / 0x6041 — command/observe the CiA 402 FSM |
| **APU / RPU** | Application (A53) / Real-time (R5F) processing unit on ZynqMP |
| **GEM / macb** | Zynq Gigabit Ethernet MAC / its Linux driver |
| **PREEMPT_RT** | Mainline Linux real-time preemption patch |

## Command mnemonics (datagram)

APRD/APWR (auto-inc physical R/W), FPRD/FPWR (configured-address R/W),
BRD/BWR (broadcast R/W), LRD/LWR/**LRW** (logical R/W/read-write),
ARMW/FRMW (auto-inc/configured read-multiple-write — DC time distribution).

## Key numeric constants

| Constant | Value |
|----------|-------|
| EtherType | 0x88A4 |
| DC time base | 64-bit, 1 ns units, epoch 2000-01-01 00:00 |
| Max nodes/segment | 65,535 |
| Logical address space | 4 GiB |
| Jitter target (DC) | < 1 µs |
| Typical cycle rates | 1–30 kHz (≤ 100 µs achievable) |
| Copper node distance | ≤ 100 m (100BASE-TX) |

## CiA 402 quick card

- Enable: statusword-driven `0x06 → 0x07 → 0x0F` (controlword 0x6040).
- Operation Enabled: `statusword & 0x006F == 0x0027`.
- Fault: `statusword & 0x004F == 0x0008`; reset via controlword bit7 rising edge (0x0080).
- Modes (0x6060): CSP=8, CSV=9, CST=10, PP=1, PV=3, PT=4, HM=6, IP=7.
- Cyclic objects: 0x6040 ctrl, 0x6041 status, 0x6060/61 mode/disp, 0x607A tgt pos,
  0x60FF tgt vel, 0x6071 tgt trq, 0x6064 act pos, 0x606C act vel, 0x6077 act trq.
- PDO config: 0x1600.. Rx map, 0x1A00.. Tx map, 0x1C12 Rx assign, 0x1C13 Tx assign.

## Standards

- **IEC 61158 / IEC 61784-2** — EtherCAT (Type 12) physical/data-link/application layer.
- **IEC 61800-7-301 / -304** — drive profile mapping (CANopen/CiA 402 & SERCOS to network).
- **IEC 61784-3-12** — Safety over EtherCAT (FSoE), up to SIL 3.
- **ISO 15745-4** — XML device description (ESI).
- **CiA 301** — CANopen application layer / communication profile.
- **CiA 402 (= IEC 61800-7-201/301)** — motion/drive device profile.
- **SEMI E54.20** — EtherCAT for semiconductor equipment.

## Authoritative references (verified during research)

- ETG technology overview — https://www.ethercat.org/en/technology.html
  (functional principle, DC, communication profiles, MainDevice/SubDevice).
- Wikipedia: EtherCAT — https://en.wikipedia.org/wiki/EtherCAT
  (frame/EtherType, synchronization/DC clock latching, epoch, performance).
- Wikipedia: CANopen — https://en.wikipedia.org/wiki/CANopen
  (object dictionary, SDO/PDO semantics, CiA 301/402 references).
- Beckhoff InfoSys — EtherCAT System Documentation (subscriber config, SM, PDO, Startup,
  CoE-Online, DC tab, ESM buttons):
  https://infosys.beckhoff.com/ (EtherCAT System Manual).
- IgH EtherCAT Master (EtherLab) — https://gitlab.com/etherlab.org/ethercat
  (stable-1.6; native NIC drivers incl. `macb`; passive master; PREEMPT_RT/Xenomai).
  Docs/Doxygen: https://docs.etherlab.org/ethercat/1.6/
- SOEM (Open EtherCAT Society) — https://github.com/OpenEtherCATsociety/SOEM
- CAN in Automation (CiA 402) — https://www.can-cia.org/ (profile specs).
- AMD/Xilinx PetaLinux & meta-xilinx (PREEMPT_RT kernel for ZynqMP); rt-tests
  (`cyclictest`) for latency validation.

## Notes / caveats to verify per device

- **assign_activate** (DC) value is device-specific — read from the drive's ESI.
- **PDO fixed vs configurable** — some drives forbid remapping; use their predefined sets.
- **Scaling/units** — position counts, torque per-mille of rated, velocity units differ
  by drive; always confirm in the ESI/manual before commanding motion.
- **SM watchdog timeout** — set safe relative to the chosen cycle time.

---

This knowledge base is intended to be **self-sufficient** for implementing the master.
When we start a new task, read the relevant chapter(s) here instead of re-searching the
web. Update these files if we discover device-specific details or correct any assumption.
