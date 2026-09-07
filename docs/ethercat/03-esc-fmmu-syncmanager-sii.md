# 03 — ESC, Memory, FMMU, SyncManager, SII/EEPROM, PDI

The **EtherCAT Slave Controller (ESC)** is the hardware in every slave that makes
"processing on the fly" possible. Even though *we build the master*, we must understand
the ESC because the master configures these registers during startup.

## 1. ESC overview

- Implemented as ASIC (e.g., Beckhoff ET1100), FPGA IP, or integrated in an MCU.
- Contains: DPRAM (application memory), registers, FMMUs, SyncManagers, DC unit, port
  logic, EEPROM (SII) interface, and a **PDI** to the local application/µC.
- ESC address space is 16-bit (64 KiB). Registers occupy the low area; application
  RAM/mailbox/process-data buffers occupy higher area (device dependent, typ. from
  0x1000).

### Key ESC registers (addresses the master writes/reads)

| Addr | Register | Purpose |
|------|----------|---------|
| 0x0000 | Type/Revision/Build | ESC identification |
| 0x0100–0x0103 | DL Control | Data-link control (port config, loop) |
| 0x0110–0x0111 | DL Status | Link/loop status per port |
| 0x0120–0x0121 | **AL Control** | Master requests state (INIT/PREOP/…) |
| 0x0130–0x0131 | **AL Status** | Slave's actual state |
| 0x0134–0x0135 | **AL Status Code** | Error reason if state change fails |
| 0x0200–0x020B | Interrupt registers | Event/IRQ |
| 0x0300+ | RX/TX error counters | Per-port CRC/error counters (diagnostics) |
| 0x0500+ | Watchdog | Process-data watchdog config/status |
| 0x0600–0x06FF | **FMMU** config (8 × 16 bytes) | Logical↔physical mapping |
| 0x0800–0x087F | **SyncManager** config (up to 16 × 8 bytes) | Mailbox & PDO buffers |
| 0x0900+ | **Distributed Clock** registers | Receive-time latches, system time, SYNC0/1 |
| 0x0500? / 0x0980 | SYNC out cycle/config | DC output sync |
| 0x1000+ | Application memory | Mailbox buffers, process data |

(Exact map is in the ETG ESC datasheet / IEC 61158; the master stack knows these.)

## 2. FMMU — Fieldbus Memory Management Unit

The FMMU translates **logical addresses** (the master's 4 GiB process image) into the
slave's **local physical memory** (its DPRAM), **bit-granular**.

- Each slave has several FMMUs (commonly 3–8).
- One FMMU entry maps a range of logical addresses to a physical start address, with
  a start-bit and length in bits, and a direction (read = inputs, write = outputs).
- This is what lets a **single LRW datagram** update the correct bytes/bits in every
  slave: each slave's FMMU picks out "its slice" of the passing frame.

FMMU entry fields (16 bytes): logical start address, length (bytes), logical start-bit,
logical stop-bit, physical start address, physical start-bit, direction, enable.

**In our master:** the stack computes FMMU settings from the PDO mapping so that, e.g.,
Drive #3's controlword lands at a specific offset in the outputs image and its statusword
at an offset in the inputs image.

## 3. SyncManager (SM)

SyncManagers manage coherent access to buffers shared between the EtherCAT side (frame)
and the PDI side (application), preventing the master and slave app from reading a
half-written buffer. Two modes:

### a) Mailbox mode (3-buffer handshake → 1 buffer, with handshake)

- Used for **acyclic** mailbox communication (CoE/SDO/FoE/EoE).
- Ensures a full message is written before the other side reads it.
- Typical layout: **SM0 = mailbox out** (master→slave), **SM1 = mailbox in**
  (slave→master).

### b) Buffered mode (3-buffer)

- Used for **cyclic process data (PDOs)**. Always the newest complete buffer is available;
  writer and reader never block.
- Typical layout: **SM2 = process data outputs** (RxPDO, master→slave),
  **SM3 = process data inputs** (TxPDO, slave→master).

SM config (8 bytes each): physical start address, length, control (mode, direction,
IRQ), status, enable.

**Standard SM assignment for a CoE drive:**

| SM | Direction | Type | Content |
|----|-----------|------|---------|
| SM0 | Master→Slave | Mailbox out | SDO requests |
| SM1 | Slave→Master | Mailbox in | SDO responses |
| SM2 | Master→Slave | Process outputs | **RxPDO**: controlword, target pos/vel/torque, modes |
| SM3 | Slave→Master | Process inputs | **TxPDO**: statusword, actual pos/vel/torque, modes display |

## 4. SII / EEPROM (SubDevice Information Interface)

- Non-volatile memory (EEPROM) on the slave, read by the master over the ESC's SII
  interface at boot.
- Holds device identity and enough config to operate even without the ESI XML:
  vendor ID, product code, revision, serial; default SM/FMMU/DC settings; mailbox
  protocols supported; bootstrap SM config; category strings.
- The full human/tool description is the **ESI XML** file (superset of SII).

Key identity objects the master reads to match a device:

- **Vendor ID**, **Product Code**, **Revision Number**, **Serial Number**.

## 5. PDI — Process Data Interface

The interface between the ESC and the slave's local application (µC or logic):

- 32-bit parallel I/O (simplest slaves, no CPU).
- SPI (small drives, encoders, analog).
- 8/16-bit parallel µC interface (complex nodes, larger data).
- On-chip/synchronous bus for FPGA/SoC integrations.

Not directly our concern as master authors, but it explains why some slaves have
different sync capabilities and latencies.

## 6. Watchdogs

- **Process Data Watchdog (SM watchdog):** if the slave stops receiving valid process
  data within a configured time, it faults to a safe state (e.g., drive disables torque).
  The master configures the WD timeout; our cycle must be well within it.
- Important for safety: if the master or link dies, drives must not keep spinning.

## 7. What the master actually configures per slave (summary)

During INIT→PREOP→SAFEOP→OP the master writes, per slave:

1. **DL/port** control.
2. **SyncManagers** (mailbox SM0/SM1, then process SM2/SM3 with correct sizes).
3. **FMMUs** mapping logical process image ↔ SM2/SM3 buffers.
4. **CoE SDOs** for PDO assignment/mapping and drive parameters (mailbox, in PREOP).
5. **Distributed Clock** registers (SYNC0/SYNC1, cycle time, offset) if DC is used.
6. **Watchdog** timeouts.

Continue to [04-state-machine-esm.md](04-state-machine-esm.md).
