# 02 — Frame and Datagram Structure

This is the wire-level detail the master builds and parses. Byte offsets are what you
need when debugging with Wireshark or writing/validating a stack.

## 1. Standard Ethernet frame carrying EtherCAT

```
+-------------------+---------------------------+-----------+-----------+
| Ethernet header   | EtherCAT frame            | (padding) | FCS/CRC   |
| 14 bytes          | 1..1498 bytes             |           | 4 bytes   |
+-------------------+---------------------------+-----------+-----------+
```

Ethernet header (14 bytes):

| Field | Size | Notes |
|-------|------|-------|
| Destination MAC | 6 | Often FF:FF:FF:FF:FF:FF or master MAC; slaves don't filter on MAC |
| Source MAC | 6 | Master's MAC (bit set so it returns) |
| EtherType | 2 | **0x88A4** = EtherCAT |

- Minimum Ethernet payload is 46 bytes → short EtherCAT frames are padded.
- Max standard payload 1500 → EtherCAT frame ≤ 1498 (2 bytes are the EtherCAT header).
- If IP routing is needed, EtherCAT can be encapsulated in **UDP/IP** (port 0x88A4);
  rarely used for hard RT.

## 2. EtherCAT frame header (2 bytes, 11+1+4 bits)

Little-endian 16-bit word:

| Bits | Field | Meaning |
|------|-------|---------|
| 0–10 | Length | Length of all datagrams following (11 bits) |
| 11 | Reserved | 0 |
| 12–15 | Type | **1 = EtherCAT command datagrams** (Device/DL protocol). Other types e.g. network variables |

## 3. EtherCAT datagram (sub-telegram)

A frame contains **one or more** datagrams, concatenated. Each datagram:

```
+----------------+-------------------------+----------------+
| Datagram hdr   | Data                    | Working Counter|
| 10 bytes       | 0..1486 bytes           | 2 bytes (WKC)  |
+----------------+-------------------------+----------------+
```

### Datagram header (10 bytes)

| Offset | Field | Size | Meaning |
|--------|-------|------|---------|
| 0 | Cmd | 1 | Command / addressing mode (see table below) |
| 1 | Idx | 1 | Index set by master, echoed by slaves (frame/datagram tracking) |
| 2–5 | Address | 4 | Meaning depends on Cmd (position/node/logical address) |
| 6–7 | Len (11 bits) + R(3) + C(1) + M(1) | 2 | Data length, Circulating, More-datagrams-follow bit |
| 8–9 | IRQ | 2 | Interrupt/event request bits from slaves (OR-ed) |

- **M (More)** bit: 1 if another datagram follows in the same frame.
- **Len:** number of process/data bytes in this datagram.

### Working Counter (WKC), 2 bytes, at end of datagram

- Incremented by hardware in **each slave** that was addressed and whose memory access
  succeeded.
- Increment rules: **+1** for a successful read **or** write; a **read-write (LRW)**
  success gives **+1 for read + +2 for write = +3** per participating slave (exact
  scheme depends on command).
- The master computes the **expected WKC** at config time. Every cycle it checks the
  returned WKC:
  - equal → all addressed slaves processed the data → consume it.
  - not equal → something missing (slave dropped, not in expected state) → do **not**
    trust that datagram's data; raise diagnostics.

**This is the master's primary per-cycle health check.** In our app the cyclic loop must
verify WKC before applying drive feedback.

## 4. Commands / addressing modes

`Cmd` byte selects both the operation (read/write/read-write) and the addressing scheme.

### Addressing schemes

| Scheme | How a slave is selected | Used for |
|--------|-------------------------|----------|
| **Position (auto-increment)** | Each slave decrements a position counter; the slave at count 0 is addressed | Boot-up topology scan, before fixed addresses assigned |
| **Node / Configured Station** | Fixed 16-bit address assigned by master at startup | Targeted acyclic access to a specific slave |
| **Logical** | 32-bit logical address in 4 GiB space, translated by the slave's **FMMU** to local memory | **Cyclic process data** (PDOs) — one datagram hits many slaves |
| **Broadcast** | All slaves | State changes, DC init, reading AL status of all |

### Common command set (mnemonics)

| Cmd | Name | Addressing | Direction |
|-----|------|-----------|-----------|
| NOP | No operation | — | — |
| **APRD** | Auto-increment Physical Read | Position | Read |
| **APWR** | Auto-increment Physical Write | Position | Write |
| APRW | Auto-increment Physical Read-Write | Position | R/W |
| **FPRD** | Configured-address Physical Read | Node | Read |
| **FPWR** | Configured-address Physical Write | Node | Write |
| FPRW | Configured Physical Read-Write | Node | R/W |
| **BRD** | Broadcast Read | Broadcast | Read |
| **BWR** | Broadcast Write | Broadcast | Write |
| BRW | Broadcast Read-Write | Broadcast | R/W |
| **LRD** | Logical Read | Logical | Read |
| **LWR** | Logical Write | Logical | Write |
| **LRW** | Logical Read-Write | Logical | R/W |
| **ARMW** | Auto-increment Read Multiple Write | Position | DC time distribution |
| FRMW | Configured Read Multiple Write | Node | DC time distribution |

Notes for our master:

- **Cyclic process data** almost always uses **LRW** (or LRD+LWR) so a single datagram
  exchanges outputs and inputs with all drives at once via their FMMUs.
- **ARMW/FRMW** is the trick used to distribute the reference clock's time to all slaves
  in one pass (see [05-distributed-clocks.md](05-distributed-clocks.md)).
- Boot-up uses **BRD/BWR** (e.g., broadcast AL control) and **APRD/APWR** / **FPRD/FPWR**
  for per-slave register setup.

## 5. Typical cyclic frame the master sends

A single cyclic frame often carries several datagrams, e.g.:

```
[EtherCAT hdr]
  Datagram 1: LRW  logical range -> exchange all drive PDOs (outputs+inputs)   [+WKC]
  Datagram 2: FPRD node=refclk    -> read DC drift for sync monitoring         [+WKC]
  Datagram 3: BRD  AL status      -> monitor slave states                      [+WKC]
[FCS]
```

The master preallocates this frame layout at startup (this is what a "domain" is in the
IgH master — a contiguous logical memory area mapped to slave PDOs). See
[08-master-implementation.md](08-master-implementation.md).

## 6. Endianness

EtherCAT wire fields (headers, addresses, DC time, CoE indices) are **little-endian**.
CANopen/CoE object data is also little-endian. Match this when packing PDOs on the
Cortex-A53 (which runs little-endian by default — convenient).

Continue to [03-esc-fmmu-syncmanager-sii.md](03-esc-fmmu-syncmanager-sii.md).
