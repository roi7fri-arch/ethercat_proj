# 01 — EtherCAT Fundamentals

## 1. What EtherCAT is

**EtherCAT** (Ethernet for Control Automation Technology) is a real-time Industrial
Ethernet fieldbus, originally developed by **Beckhoff**, standardized in **IEC 61158 /
IEC 61784** and (for drives) **IEC 61800-7**. Design goals:

- Short cycle times (**≤ 100 µs** achievable).
- Very low jitter for synchronization (**≤ 1 µs**).
- Low hardware cost (master = any standard Ethernet MAC; no special card).

Terminology note: the modern ETG naming is **MainDevice** (master) and **SubDevice**
(slave). Older docs/APIs (and this KB where it matches code) use **master/slave**. They
are synonyms.

## 2. The functional principle — "processing on the fly"

This is THE defining idea and the reason for its performance:

1. The **master** is the *only* node in a segment allowed to actively transmit an
   EtherCAT frame.
2. The frame passes **through** each slave. Each slave's **EtherCAT Slave Controller
   (ESC)** — dedicated hardware (ASIC/FPGA/integrated MCU) — reads the data addressed to
   it and **inserts its own data while the frame is still moving** (it does not stop,
   buffer, and re-emit the whole frame). Delay per node ≈ hardware propagation delay
   (tens of ns).
3. The last node in a line detects an **open port** and returns the frame to the master
   using Ethernet's **full-duplex** feature (logical ring).
4. The master receives the frame back with all inputs already filled in, in a correctly
   sorted **process image** — the CPU does no sorting.

Consequences:

- Effective data rate > 90 % (uses both directions of 100 Mbit/s full duplex).
- One frame typically services the **entire network**.
- The master CPU cost depends only on your application, not on the fieldbus.
- Slaves need no powerful CPU; simple slaves need no CPU at all (I/O wired to the ESC).

```
        +----------------------------------------------------+
Master  |  send  ->  Slave1 -> Slave2 -> ... -> SlaveN        |
(NIC)   |                                         |           |
        |  recv  <--------- (looped back) --------+           |
        +----------------------------------------------------+
  Each slave reads/writes its bytes as the frame streams through.
```

## 3. Topology

- Physically usually a **line/daisy-chain**; logically always a **ring** (full duplex).
- Supported: line, tree, star, ring, drop lines, and combinations.
- Each slave has 2+ ports; an ESC **auto-closes an open port** and loops the frame back
  if no downstream device is present.
- Standard Ethernet PHY: up to **100 m** copper between nodes (100BASE-TX); fiber for
  longer (up to ~20 km single-mode) or isolation.
- Up to **65,535 nodes** per segment.
- Advanced features: **Hot Connect** (connect/disconnect groups during operation, detect
  < 15 µs), **cable redundancy** (ring: connect last node back to a 2nd master NIC port),
  **master redundancy** (hot standby).

## 4. Performance (why it is used for motion)

- 1000 distributed digital I/O in ~30 µs (≈125 bytes over 100 Mbit/s).
- 100 servo axes updated at up to 10 kHz.
- Typical network update rates: **1–30 kHz**.
- Extensions **EtherCAT G / G10** use 1 / 10 Gbit/s for higher bandwidth (same principle).

## 5. Where the "protocol layers" live

- **Physical/Data-Link:** standard Ethernet (IEEE 802.3), EtherType **0x88A4**. No
  TCP/IP for real-time data (can optionally tunnel over UDP/IP for routing).
- **Application layer / device profiles:** carried via **mailbox** protocols:
  - **CoE** — CAN application protocol over EtherCAT (CANopen object dictionary, SDO,
    PDO). **This is what our drives use** (CiA 402).
  - **SoE** — Servo drive profile (SERCOS, IEC 61800-7-204) over EtherCAT.
  - **EoE** — Ethernet over EtherCAT (tunnel arbitrary Ethernet/IP).
  - **FoE** — File access over EtherCAT (firmware upload, TFTP-like).
  - **AoE** — ADS over EtherCAT (routable client/server, diagnostics/gateways).

## 6. Two data paths (crucial distinction)

| Path | Purpose | Timing | Mechanism |
|------|---------|--------|-----------|
| **Process data (PDO)** | Cyclic real-time values (target/actual position, controlword, statusword…) | Every cycle, hard RT | Logical addressing + FMMU → process image |
| **Mailbox (SDO, etc.)** | Acyclic config, parameters, diagnostics | On demand, not RT-critical | CoE/FoE/EoE over SyncManager mailbox |

We configure drives during startup via **SDO (mailbox)**, then run the motion loop with
**PDOs** in the cyclic process image.

## 7. Diagnostics built into the protocol

- **CRC** per frame; each ESC checks it and flags downstream nodes; error counters per
  port allow **exact localization** of a faulty link.
- **Working Counter (WKC):** every slave successfully addressed by a datagram increments
  it. The master compares the returned WKC to the expected value each cycle to confirm
  data consistency (see [02](02-frame-and-datagram-structure.md)).
- Standard Ethernet → **Wireshark** has an EtherCAT dissector.

## 8. Configuration artifacts

- **ESI** (EtherCAT Slave Information): XML file shipped with each device describing its
  PDOs, mailbox support, sync modes, objects. Also stored (subset) in the slave's
  **SII/EEPROM**.
- **ENI** (EtherCAT Network Information): the master's boot-up configuration for the whole
  network, produced by a config tool from the ESI files. IgH/SOEM can also configure
  directly from code instead of a full ENI.

Continue to [02-frame-and-datagram-structure.md](02-frame-and-datagram-structure.md).
