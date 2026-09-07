# 08 — Master Implementation: responsibilities, stacks, APIs

We are building the **MainDevice (master)**. This chapter covers what a master must do,
the configuration artifacts, and the two realistic open-source stacks for Linux on MPSoC:
**IgH EtherCAT Master (EtherLab)** and **SOEM**.

## 1. Master responsibilities

1. **Bus scan & identification** — discover slaves (auto-increment), read SII identity,
   compare to expected topology.
2. **Configuration** — assign station addresses; configure SM, FMMU, DC; push SDO startup
   parameters; build the **process image** (domains) from PDO mappings.
3. **State management** — drive ESM: INIT→PREOP→SAFEOP→OP; handle errors/recovery.
4. **Cyclic exchange** — every period: receive last frame, process inputs, run control,
   write outputs, queue and send the frame. Verify **WKC**.
5. **Distributed Clocks** — measure delays, compensate offsets, discipline drift, generate
   SYNC0; keep the RT loop phase-locked to the bus.
6. **Application logic** — CiA 402 FSM per drive, trajectory generation, safety reactions.
7. **Diagnostics** — WKC, AL status codes, EMCY, error counters, logging.

## 2. ESI vs ENI

- **ESI** (per device XML) → describes each slave (PDOs, mailbox, DC, objects).
- **ENI** (whole-network XML) → produced by a config tool from ESIs; a "boot script"
  telling the master exactly what to write to each slave and how the process image is laid
  out. TwinCAT, or tools like the ETG's, or `ethercat` CLI helpers can generate it.
- With **IgH** you typically **skip a full ENI** and configure from C using the API
  (mappings/SDOs in code). With some stacks (e.g. Acontis, or SOEM+config) an ENI can be
  consumed. For our project, code-based config (IgH style) is simplest and versionable.

## 3. IgH EtherCAT Master (EtherLab) — recommended for RT Linux

- Open source (GPLv2 kernel modules + LGPL user lib). Widely used for hard-RT motion on
  Linux, including with PREEMPT_RT and Xenomai.
- Architecture: a **master kernel module** + **native NIC drivers** (patched
  `e1000e`, `igb`, `r8169`, `macb`, generic driver) that hand raw frames to the master with
  low latency; a **userspace library** `libethercat` (`ecrt_*` API) for the application.
- Repo: `gitlab.com/etherlab.org/ethercat` (branch `stable-1.6`). CLI tool: `ethercat`.
- The master itself is **passive**: your RT thread drives timing (it calls send/receive).
  Realtime patches supported but not required.

### IgH lifecycle API (the core you'll use)

Setup (non-RT, once):
```c
ec_master_t   *master = ecrt_request_master(0);
ec_domain_t   *domain = ecrt_master_create_domain(master);

ec_slave_config_t *sc = ecrt_master_slave_config(
        master, alias, position, VENDOR_ID, PRODUCT_CODE);

// PDO mapping (from the tables in doc 06/07)
ecrt_slave_config_pdos(sc, EC_END, drive_syncs);

// SDO startup parameters (sent during PREOP->SAFEOP)
ecrt_slave_config_sdo8 (sc, 0x6060, 0, 8);      // CSP mode
ecrt_slave_config_sdo32(sc, 0x6072, 0, max_trq);

// Register PDO entries -> get byte offsets into the process image
unsigned int off_ctrl, off_status, off_tgtpos, off_actpos;
ec_pdo_entry_reg_t regs[] = {
  {alias,position,VENDOR_ID,PRODUCT_CODE, 0x6040,0, &off_ctrl},
  {alias,position,VENDOR_ID,PRODUCT_CODE, 0x6041,0, &off_status},
  {alias,position,VENDOR_ID,PRODUCT_CODE, 0x607A,0, &off_tgtpos},
  {alias,position,VENDOR_ID,PRODUCT_CODE, 0x6064,0, &off_actpos},
  {}
};
ecrt_domain_reg_pdo_entry_list(domain, regs);

// Distributed clocks (assign_activate from the device ESI!)
ecrt_slave_config_dc(sc, 0x0300, PERIOD_NS, sync0_shift_ns, 0, 0);

ecrt_master_activate(master);
uint8_t *pd = ecrt_domain_data(domain);   // pointer to process image
```

Cyclic (hard-RT thread, every period T):
```c
ecrt_master_receive(master);         // fetch last frame from NIC
ecrt_domain_process(domain);         // check WKC, expose inputs

// --- read feedback ---
uint16_t status = EC_READ_U16(pd + off_status);
int32_t  actpos = EC_READ_S32(pd + off_actpos);

// --- CiA402 FSM + trajectory ---
uint16_t ctrl; int32_t tgt;
control_step(status, actpos, &ctrl, &tgt);

// --- write outputs ---
EC_WRITE_U16(pd + off_ctrl,   ctrl);
EC_WRITE_S32(pd + off_tgtpos, tgt);

// --- DC discipline ---
ecrt_master_application_time(master, app_time_ns);
ecrt_master_sync_reference_clock(master);
ecrt_master_sync_slave_clocks(master);

ecrt_domain_queue(domain);           // queue datagrams into the frame
ecrt_master_send(master);            // transmit
```

WKC / domain state:
```c
ec_domain_state_t ds;
ecrt_domain_state(domain, &ds);
if (ds.wc_state != EC_WC_COMPLETE) { /* handle missing data */ }
```

`EC_READ_*/EC_WRITE_*` handle endianness. `EC_END` terminates the sync array.

### IgH `ethercat` CLI (bring-up & debugging)

- `ethercat slaves` — list slaves, states, alias/position.
- `ethercat xml` / `ethercat sii_read` — dump ESI/SII.
- `ethercat pdos` — show PDO mapping.
- `ethercat sdos` / `ethercat upload 0x6041 0` — inspect the OD.
- `ethercat states -p N op` — force a slave state.
- `ethercat master` — master/DC status.

## 4. SOEM (Simple Open EtherCAT Master) — alternative

- Pure **userspace** C library (Open EtherCAT Society), very portable (Linux, RTOS,
  bare-metal), uses raw sockets (`AF_PACKET`) on Linux.
- Lighter and easier to embed/audit; you own the RT thread and timing.
- Core flow: `ec_init(ifname)` → `ec_config_init()` → `ec_config_map(&IOmap)` →
  `ec_configdc()` → set slaves to OP (`ec_writestate`/`ec_statecheck`) → loop
  `ec_send_processdata()` / `ec_receive_processdata()`; `ec_SDOwrite/ec_SDOread` for SDOs;
  `ec_slave[i].outputs/inputs` point into the mapped IO.
- Trade-off vs IgH: SOEM via `AF_PACKET` may have slightly higher/less-controlled latency
  than IgH's in-kernel native drivers; both are used in production. For **tightest
  determinism on MPSoC**, IgH with a native `macb` (Zynq GEM) driver is attractive; SOEM
  is great for simpler/portable builds.

### Decision guidance for our project

| Criterion | IgH | SOEM |
|-----------|-----|------|
| Determinism / latency | Best (native kernel NIC driver, DMA) | Good (userspace raw socket) |
| Complexity to deploy | Higher (kernel modules per kernel version) | Lower (single userspace lib) |
| DC support | Mature, explicit API | Yes (`ec_configdc`, `ec_dcsync0`) |
| MPSoC/Zynq GEM (`macb`) | Native driver supported | Works via AF_PACKET |
| License | GPL/LGPL | GPLv2-with-exception (permissive-ish) |

Recommendation: **start with IgH** on PREEMPT_RT for the deterministic multi-axis loop;
keep SOEM in mind for a portable test harness or if kernel-module maintenance is a burden.

## 5. Process image / "domain" concept

A **domain** is a contiguous chunk of logical-address memory the master exchanges each
cycle (via LRW). PDO entries from all drives are registered into it and given byte offsets.
The RT loop just reads/writes those offsets — no per-slave frame juggling. This is the
concrete form of the "sorted process image" EtherCAT advertises.

## 6. Threading model (preview of doc 10)

- **One hard-RT thread** = the cyclic loop (SCHED_FIFO, high prio, pinned to an isolated
  core). Only PDOs + DC here.
- **One non-RT thread** = SDO config, diagnostics, logging, TCP/UI, EMCY handling.
- Communicate via lock-free/triple-buffer or bounded queues; never block the RT thread on
  mutexes held by non-RT code.

Continue to [09-xilinx-mpsoc-rt-linux.md](09-xilinx-mpsoc-rt-linux.md).
