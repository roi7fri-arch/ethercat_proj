# EtherCAT Knowledge Base — Index

Purpose: a self-contained reference so we can implement a **deterministic EtherCAT
master application** on a **Xilinx Zynq UltraScale+ MPSoC running Linux**, talking to
**motor/engine drives** (servo/BLDC drivers) over EtherCAT, without re-reading theory
from the web for every task.

Scope of the target system:

- Platform: Xilinx MPSoC (ARM Cortex-A53 APU) + Linux (PREEMPT_RT).
- Role: **MainDevice (master)** — the only node allowed to actively send frames.
- SubDevices (slaves): EtherCAT **servo/engine drivers** exposing the **CoE / CiA 402**
  drive profile.
- Requirement: **hard real-time / deterministic** cyclic control (typ. 1–8 kHz),
  synchronized with **Distributed Clocks (DC)**, jitter target < 1 µs at the drives.

## How to read this

| File | Topic |
|------|-------|
| [01-fundamentals.md](01-fundamentals.md) | What EtherCAT is, "processing on the fly", topology, performance, terminology |
| [02-frame-and-datagram-structure.md](02-frame-and-datagram-structure.md) | Ethernet frame, EtherCAT header, datagrams, commands, addressing, Working Counter |
| [03-esc-fmmu-syncmanager-sii.md](03-esc-fmmu-syncmanager-sii.md) | EtherCAT Slave Controller, memory, FMMU, SyncManager, SII/EEPROM, PDI |
| [04-state-machine-esm.md](04-state-machine-esm.md) | EtherCAT State Machine (INIT/PREOP/SAFEOP/OP/BOOT), AL control/status |
| [05-distributed-clocks.md](05-distributed-clocks.md) | DC, SYNC0/SYNC1, propagation-delay measurement, drift compensation, sync modes |
| [06-mailbox-coe-soe-foe-eoe.md](06-mailbox-coe-soe-foe-eoe.md) | Mailbox protocols; CoE object dictionary, SDO, PDO mapping/assignment |
| [07-cia402-drive-profile.md](07-cia402-drive-profile.md) | CiA 402 drive state machine, controlword/statusword, CSP/CSV/CST/PP/HM, key objects |
| [08-master-implementation.md](08-master-implementation.md) | Master responsibilities, startup sequence, ENI/ESI, IgH & SOEM stacks + API |
| [09-xilinx-mpsoc-rt-linux.md](09-xilinx-mpsoc-rt-linux.md) | MPSoC specifics, PREEMPT_RT, CPU isolation, NIC, determinism tuning |
| [10-master-app-architecture.md](10-master-app-architecture.md) | Proposed architecture for OUR app: threads, cyclic loop, DC alignment, drive FSM |
| [11-glossary-and-references.md](11-glossary-and-references.md) | Glossary, abbreviations, standards, authoritative links |

## Implementation references (stack + drive)

Concrete, primary-source docs for the **SOEM** master stack and the **Elmo Platinum**
drives we will use. Source code lives in [`../../third_party/SOEM`](../../third_party/SOEM);
the Elmo docs are distilled from the official `MAN-P-ADMINGUIDE.pdf`.

| File | Topic |
|------|-------|
| [../soem/00-soem-overview.md](../soem/00-soem-overview.md) | SOEM v2.0.0: what it is, repo layout, samples, **GPLv3/commercial license caveat** |
| [../soem/01-soem-api-reference.md](../soem/01-soem-api-reference.md) | The `ecx_*` context API we actually call (init → OP → cyclic → SDO → recovery) |
| [../soem/02-soem-linux-rt-and-build.md](../soem/02-soem-linux-rt-and-build.md) | Linux raw-socket NIC, RT cyclic thread, DC↔Linux PI phase-lock, build on MPSoC |
| [../elmo/00-elmo-platinum-overview.md](../elmo/00-elmo-platinum-overview.md) | Platinum as a CoE/CiA402 SubDevice; CAN vs ECAT OD; firmware/ESI caveats |
| [../elmo/01-elmo-object-dictionary.md](../elmo/01-elmo-object-dictionary.md) | Full OD + Elmo objects + **multi-axis offsets (+0x800 DS402, +0x10 PDO map)** |
| [../elmo/02-elmo-cia402-modes.md](../elmo/02-elmo-cia402-modes.md) | State machine, CSP/CSV/CST, control/status bits, **extrapolation-timeout watchdog** |
| [../elmo/03-elmo-pdo-and-syncmanager.md](../elmo/03-elmo-pdo-and-syncmanager.md) | SM assign 0x1C1x/0x1C3x, PDO mapping element format, example layouts, DC objects |
| [../elmo/04-elmo-bringup-with-soem.md](../elmo/04-elmo-bringup-with-soem.md) | End-to-end CSP bring-up: PDO map SDO sequence, PO→SO hook, enable ladder |

## One-paragraph summary (the mental model)

The master builds **one Ethernet frame** carrying **one or more EtherCAT datagrams**.
The frame travels down the line; each slave's **ESC hardware** reads/writes its slice of
the frame **on the fly** (nanosecond delays), increments a **Working Counter**, and
forwards it. The last slave loops the frame back (full-duplex), and the master reads the
now-updated **process image**. Configuration/parameters go through an acyclic **mailbox**
(CoE/SDO); cyclic motor commands go through **PDOs** mapped into the process image.
**Distributed Clocks** give every slave a common < 1 µs time base so all axes act
simultaneously. Drives follow the **CiA 402** state machine and are commanded in modes
like **CSP** (Cyclic Synchronous Position), **CSV** (Velocity), **CST** (Torque).

## Key numeric facts (memorize)

- EtherType: **0x88A4**.
- DC system time: **64-bit nanoseconds**, epoch **2000-01-01 00:00**.
- Jitter with DC: **< 1 µs**; typical cycle times **1–30 kHz** (≤ 100 µs achievable).
- Up to **65535 nodes** per segment; 4 GiB logical address space.
- CiA 402 core objects: controlword **0x6040**, statusword **0x6041**,
  modes of operation **0x6060**, target position **0x607A**, target velocity **0x60FF**,
  target torque **0x6071**, actual position **0x6064**.
