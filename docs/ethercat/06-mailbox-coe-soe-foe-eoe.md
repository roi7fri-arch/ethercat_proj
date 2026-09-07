# 06 — Mailbox Protocols & CoE (Object Dictionary, SDO, PDO)

Acyclic communication uses the **mailbox** (SyncManagers SM0/SM1). Several protocols run
over it; for drives we care almost exclusively about **CoE (CANopen over EtherCAT)**.

## 1. Mailbox protocols overview

| Protocol | Full name | Use for our project |
|----------|-----------|---------------------|
| **CoE** | CAN application protocol over EtherCAT | **Primary** — object dictionary, SDO config, PDO mapping, CiA 402 |
| SoE | Servo profile (SERCOS) over EtherCAT | Only if a drive speaks SoE instead of CoE |
| FoE | File access over EtherCAT | Firmware update of drives (BOOTSTRAP) |
| EoE | Ethernet over EtherCAT | Tunnel IP to a slave (diagnostics/web UI); not RT |
| AoE | ADS over EtherCAT | Routable diagnostics/gateway access |

Mailbox message = mailbox header (address, length, type, counter) + protocol-specific
payload. The SM handshake guarantees whole-message delivery.

## 2. CoE = CANopen semantics over EtherCAT

CoE reuses CANopen (CiA 301) concepts but **without CAN's 8-byte limit**:

- **Object Dictionary (OD):** the device's entire parameter/data space, addressed by
  **16-bit Index** + **8-bit Sub-index**.
- **SDO (Service Data Object):** acyclic read/write of any OD entry (config path).
- **PDO (Process Data Object):** cyclic real-time data, mapped into the process image.
- **PDO mapping & assignment:** OD objects that define *which* OD entries are packed into
  the cyclic PDOs.
- **Emergency (EMCY):** asynchronous fault notification from the slave.

### Object Dictionary layout (index ranges)

| Index range | Content |
|-------------|---------|
| 0x0000–0x0FFF | Data types |
| **0x1000–0x1FFF** | **Communication profile (CiA 301):** device type, identity, SM/PDO config |
| 0x2000–0x5FFF | Manufacturer-specific |
| **0x6000–0x9FFF** | **Standardized device profile (CiA 402 for drives)** |
| 0xA000+ | Reserved / network vars |

### Important communication-area objects

| Index | Object | Notes |
|-------|--------|-------|
| 0x1000 | Device Type | |
| 0x1008/09/0A | Device name / HW / SW version | |
| 0x1018 | **Identity** | Vendor ID, Product Code, Revision, Serial — master matches on these |
| 0x1600–0x17FF | **RxPDO mapping** | Which OD entries go into master→slave PDOs |
| 0x1A00–0x1BFF | **TxPDO mapping** | Which OD entries go into slave→master PDOs |
| 0x1C00 | SM communication type | Which SM is mailbox vs process |
| **0x1C12** | **RxPDO assignment** | Which RxPDO mapping objects are active on SM2 |
| **0x1C13** | **TxPDO assignment** | Which TxPDO mapping objects are active on SM3 |
| 0x1C32/0x1C33 | SM output/input sync | DC/SM sync parameters, cycle times |

## 3. SDO protocol (config path)

- **Client = master**, **server = slave**. Master always initiates.
- **Download** = write to slave OD. **Upload** = read from slave OD (CANopen names it from
  the server's perspective).
- **Expedited transfer:** payload ≤ 4 bytes, fits in one message (most drive params).
- **Segmented / block transfer:** for larger data (strings, big records).
- **Abort:** on error, server returns a 32-bit **SDO abort code** (e.g. 0x06010002 =
  "attempt to write a read-only object", 0x06090011 = "sub-index does not exist",
  0x08000022 = "data cannot be transferred in current state").

SDO write encodes: command specifier, **index (LE)**, **sub-index**, size, data (LE).

### IgH master SDO API

- Startup/config-time (attached to a slave config, sent during PS/SO):
  `ecrt_slave_config_sdo8/16/32(sc, index, subindex, value)` and
  `ecrt_slave_config_sdo(sc, index, subindex, data, size)` and
  `ecrt_slave_config_complete_sdo(...)`.
- Runtime acyclic (from non-RT context or carefully):
  `ecrt_master_sdo_download(...)`, `ecrt_master_sdo_upload(...)`.

**Rule:** do bulk SDO configuration in **PRE-OP**; avoid SDOs in the hard-RT loop.

## 4. PDO mapping and assignment (the cyclic path)

Two-level structure:

1. **PDO mapping objects** (0x1600.. for Rx, 0x1A00.. for Tx): each lists the OD entries
   (index:subindex:bitlength) packed into that PDO. Example RxPDO 0x1600:
   - sub1 → 0x6040:00, 16 bits (controlword)
   - sub2 → 0x607A:00, 32 bits (target position)
   - sub3 → 0x6060:00, 8 bits (modes of operation)
2. **PDO assignment objects** (0x1C12 for SM2/Rx, 0x1C13 for SM3/Tx): list which PDO
   mapping objects are active. Example: 0x1C12 = {0x1600}, 0x1C13 = {0x1A00}.

Configuration procedure (in PRE-OP, via SDO) — standard "clear, fill, set count" pattern:

```
# Disable assignment
0x1C12:00 = 0
# Define RxPDO 0x1600 content
0x1600:00 = 0                 # clear count first
0x1600:01 = 0x6040_00_10      # controlword, 16 bit
0x1600:02 = 0x607A_00_20      # target position, 32 bit
0x1600:03 = 0x6060_00_08      # modes of operation, 8 bit
0x1600:00 = 3                 # set number of entries
# Assign it to SM2
0x1C12:01 = 0x1600
0x1C12:00 = 1
# (same pattern for 0x1A00 / 0x1C13 on the Tx side)
```

Many drives ship **fixed** PDO sets (flagged "F"); then you only choose which predefined
mapping to assign (or none — just use the default). If a drive's PDO is fixed, don't try
to remap it or it will refuse to reach OP ("invalid SM cfg").

### IgH master PDO configuration API

- `ecrt_slave_config_pdos(sc, n, syncs)` with `ec_sync_info_t` / `ec_pdo_info_t` /
  `ec_pdo_entry_info_t` describing SM→PDO→entries (mirrors the tables above).
- `ecrt_slave_config_reg_pdo_entry(sc, index, subindex, domain, &bit_pos)` to register a
  PDO entry into a **domain** and get its byte offset in the process image.
- Or `ecrt_domain_reg_pdo_entry_list(domain, regs)` to register many at once.

## 5. EMCY (Emergency) messages

- Sent by a slave when an internal fatal error occurs (over-current, over-temp, following
  error, encoder fault…).
- 8-byte payload: **error code (2)**, **error register (1, object 0x1001)**,
  manufacturer-specific (5).
- The master should subscribe/poll and log these; they explain why a drive dropped out of
  OP. CiA 402 also exposes error info via statusword fault bit and object 0x603F
  (error code) and 0x1001/0x1003 (error register/history).

## 6. What our master does with CoE (summary)

1. In PRE-OP: read **0x1018 Identity** to confirm the device; run the **SDO startup list**
   to set modes/limits and PDO assignment/mapping.
2. Register the mapped PDO entries into a **domain** (process image) so the cyclic loop
   can read/write them by offset.
3. In OP: only touch **PDOs** in the RT loop; use SDO only for occasional non-RT
   parameter changes (ideally from a separate, lower-priority thread).

Continue to [07-cia402-drive-profile.md](07-cia402-drive-profile.md).
