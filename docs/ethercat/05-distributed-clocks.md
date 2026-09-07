# 05 — Distributed Clocks (DC): the key to determinism

Distributed Clocks are **the** mechanism that makes EtherCAT deterministic for motion.
They give every slave a common time base (< 1 µs skew) so that all axes **act
simultaneously**, independent of communication jitter. For our multi-drive engine
application, DC is essential.

## 1. Why DC (and not just synchronous comms)

- Pure "everyone acts when the frame arrives" is fragile: any comms jitter directly
  becomes actuation jitter, and slaves at different bus positions see the frame at
  different times.
- DC instead distributes a **hardware clock**. Each slave triggers its own actions
  (apply outputs, latch inputs) from its **local synchronized clock**, not from frame
  arrival. So the master only has to get the frame there **early enough**; several µs of
  send jitter don't hurt accuracy.

## 2. The system time

- **64-bit** counter, unit **1 ns**, epoch **2000-01-01 00:00:00**.
- The **reference clock** = the DC unit of the **first DC-capable slave** (usually the
  first slave). All others are slaved to it.
- The master maintains an **application time** and disciplines the reference clock to it
  (or follows it), then the reference disciplines everyone else.

## 3. The three DC steps the master performs

### Step A — Propagation-delay measurement

- The master issues a **broadcast** that makes every ESC **latch its local receive time**
  at each port when the frame passes (both on the way "down" and, thanks to the ring, on
  the way back).
- From these latched timestamps the master computes, for each slave, the **cable/forward
  propagation delay** relative to the reference clock.
- Registers involved: **Receive Time Port 0..3** (0x0900+), **System Time** (0x0910),
  **Processing Unit Receive Time**.

### Step B — Offset compensation

- Each slave gets a **System Time Offset** so its local system time equals the reference
  clock's, and a **delay** value for its propagation delay.
- After this, all slaves' 64-bit clocks read (nearly) the same absolute time.
- Registers: **System Time Offset** (0x0920), **System Time Delay** (0x0928),
  **System Time Difference** (0x092C, used for drift monitoring).

### Step C — Continuous drift compensation

- Crystals drift. The master periodically distributes the reference time to all slaves so
  their **time control loop** (built into the ESC DC unit) speeds up/slows down the local
  clock to stay locked.
- The elegant trick: a single **ARMW/FRMW** datagram reads the reference clock's time and
  **writes it to all other slaves in the same pass** — one datagram disciplines the whole
  bus every cycle.

## 4. SYNC0 / SYNC1 signals

Each DC slave can generate hardware **sync pulses** from its synchronized clock:

- **SYNC0:** primary periodic interrupt (e.g., every 1 ms / 250 µs / 125 µs). The drive's
  control ISR uses SYNC0 to **apply outputs and latch feedback** at a precise instant.
- **SYNC1:** optional secondary pulse, offset from SYNC0 (used for staged sampling, e.g.
  read encoder then compute).

Master configures per slave:

- **Cycle Time SYNC0** (0x09A0) — must match the master's cyclic period (or an integer
  submultiple/multiple).
- **Cycle Time SYNC1** (0x09A4) — optional.
- **Start Time / Cycle Start** (0x0990, 64-bit) — the absolute system time of the first
  pulse; chosen a bit in the future so all slaves start together.
- **Activation** register (0x0981) — enables SYNC0/SYNC1 generation.

## 5. Sync modes seen at the drive (as in Beckhoff "DC" tab)

| Mode | Meaning | Determinism |
|------|---------|-------------|
| **FreeRun** | Slave runs on its own timer, unsynchronized to bus | Lowest |
| **SM-Synchronous** | Slave acts on SyncManager event (frame/PDO arrival) | Medium; sensitive to comms jitter |
| **DC-Synchronous** | Slave acts on **SYNC0** (and SYNC1) from distributed clock | **Highest — use this for motion** |

For CiA 402 drives in CSP/CSV/CST we use **DC-Synchronous** with SYNC0 = control cycle.

## 6. Master timing model (how our loop aligns to DC)

```
   |<---------------- cycle T (e.g. 1 ms) ---------------->|
   |                                                       |
t0 |  master wakes (RT timer)                              |
   |  ecrt_master_receive()  -> read last frame's inputs   |
   |  compute control (CiA402)                             |
   |  ecrt_master_send()     -> outputs on the wire        |
   |                                                        \
   |        frame propagates, reaches drives BEFORE SYNC0    \
   |                                                          v
       SYNC0 fires in every drive at the same absolute time -> outputs applied,
                                                               feedback latched
```

Key rules:

- The master's **application time** (`ecrt_master_application_time`) must be set each cycle
  from a monotonic clock aligned to the cycle.
- The master picks the **SYNC0 shift** so the frame reliably arrives before SYNC0
  (accounting for send time + propagation). Typical: SYNC0 slightly after the expected
  frame-arrival instant.
- The master must also **discipline its own wake-up** to the DC reference (or vice versa)
  to avoid slow drift between the Linux timer and the bus clock. Two approaches:
  1. **Master timer is the master:** set reference clock to follow master application
     time (`ecrt_master_sync_reference_clock`), then sync slaves
     (`ecrt_master_sync_slave_clocks`).
  2. **DC reference is the master:** read reference-clock drift and adjust the Linux
     wakeup (PI controller on the SYNC0 phase) so the RT thread rides the bus clock.

## 7. DC-related IgH API (for step 6)

- `ecrt_slave_config_dc(sc, assign_activate, sync0_cycle, sync0_shift, sync1_cycle,
  sync1_shift)` — configure a slave's DC/SYNC0/1.
- `ecrt_master_application_time(master, app_time_ns)` — provide the current app time each
  cycle.
- `ecrt_master_sync_reference_clock(master)` — queue datagram to write app time to the
  reference clock.
- `ecrt_master_sync_slave_clocks(master)` — queue the ARMW that disciplines all slaves.
- `ecrt_master_reference_clock_time(master, &time)` — read reference clock (for a PI
  phase controller).

## 8. Practical determinism checklist (DC side)

- All motion drives in **DC-Synchronous (SYNC0)** mode.
- `assign_activate` value comes from the ESI (per device) — don't guess it.
- SYNC0 cycle == RT loop period; SYNC0 shift tuned so frame arrives ~50–200 µs before
  SYNC0 (depends on chain length / cycle).
- Monitor **System Time Difference (0x092C)** per slave; it should stay well under 1 µs
  once locked. Rising values → the drift loop isn't converging.
- Don't request **OP** until DC is locked and WKC is correct for a number of cycles.

Continue to [06-mailbox-coe-soe-foe-eoe.md](06-mailbox-coe-soe-foe-eoe.md).
