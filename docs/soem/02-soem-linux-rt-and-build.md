# SOEM on Linux — RT Integration, DC Sync, and Build

Target: **Xilinx Zynq UltraScale+ MPSoC, Cortex-A53, PREEMPT_RT Linux**, Cadence
GEM NIC (`macb`). This file explains how the `samples/ec_sample` pattern maps onto
our deterministic-master requirements (see
[../ethercat/09-realtime-and-determinism.md](../ethercat/09-realtime-and-determinism.md)).

## 1. NIC access: raw socket, not a driver

On Linux, `oshw/linux/nicdrv.c` opens an `AF_PACKET`/`SOCK_RAW` socket bound to the
interface and does its own tx/rx. Implications:

- The process needs **`CAP_NET_RAW`** (run as root, or
  `setcap cap_net_raw+ep <exe>`, or a systemd `AmbientCapabilities=CAP_NET_RAW`).
- **Dedicate the NIC to EtherCAT**: no IP stack, no NetworkManager, no other
  traffic. `ip link set eth0 up` but leave it unconfigured (no address). EtherCAT
  frames use EtherType `0x88A4`.
- Bind the NIC IRQ + our RT thread to an **isolated CPU** (`isolcpus`, IRQ affinity)
  so the ARP/other subsystems can't perturb the cycle.
- SOEM supports a **redundant** second NIC (`ecx_init_redundant`) for cable
  redundancy; not needed for the first bring-up.

## 2. The real-time cyclic thread

The cycle must be jitter-bounded. Pattern (from `ec_sample`, adapted):

```c
struct sched_param p = { .sched_priority = 80 };
pthread_setschedparam(pthread_self(), SCHED_FIFO, &p);   // RT priority
mlockall(MCL_CURRENT | MCL_FUTURE);                       // no page faults

struct timespec t;
clock_gettime(CLOCK_MONOTONIC, &t);
const long CYCLE_NS = 1000000;   // 1 kHz start point (tighten later)

for (;;) {
    // 1) write outputs into IOmap (controlword, target position...)
    ecx_send_processdata(&ctx);
    wkc = ecx_receive_processdata(&ctx, EC_TIMEOUTRET);
    if (wkc >= expectedWKC) {
        // 2) read inputs from IOmap (statusword, actual position...)
        // 3) run CiA402 state machine + control law
    }
    ecx_mbxhandler(&ctx, 0, 4);              // service CoE mailboxes

    // 4) phase-lock next wake to DC (see below), then sleep
    t.tv_nsec += CYCLE_NS + dc_adjust;
    normalize(&t);
    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &t, NULL);
}
```

Key RT rules (all in [../ethercat/09-realtime-and-determinism.md](../ethercat/09-realtime-and-determinism.md)):
`clock_nanosleep(TIMER_ABSTIME)` (absolute, no drift), `mlockall`, `SCHED_FIFO`,
isolated core, prefault the stack, **no malloc/printf/syscalls in the loop**.

## 3. Distributed Clocks: aligning the Linux cycle to DC

The drives run their SYNC0 interrupt off the DC reference clock; our Linux loop runs
off `CLOCK_MONOTONIC`. These two must be phase-locked or PDOs land in the wrong DC
window (torque ripple, following error). `ec_sample` uses a **PI controller** that
nudges each wake by looking at where `ctx.DCtime` falls inside the cycle:

```c
/* From ec_sample: drive the phase error toward a fixed offset in the cycle. */
void ec_sync(int64 reftime, int64 cycletime, int64 *offsettime) {
    static int64 integral = 0;
    int64 delta = (reftime) % cycletime;          // where are we in the window
    if (delta > cycletime/2) delta -= cycletime;  // signed error
    if (delta > 0) integral++;
    if (delta < 0) integral--;
    *offsettime = -(delta / 100) - (integral / 20);   // correction added to next sleep
}
```

Setup order: `ecx_configdc(&ctx)` → reach SAFE_OP with cyclic traffic already
running (so DC gets samples) → `ecx_dcsync0(&ctx, slave, TRUE, CYCLE_NS, shift)` per
Elmo slave → let the PI loop converge (a few hundred cycles) → request OP.
`CyclShift` places SYNC0 slightly *after* the expected frame arrival so data is fresh.

> Elmo DC detail: object **0x1C32/0x1C33** are the SM2/SM3 synchronization
> parameters (cycle time, shift); **0x2046** is the Elmo "ECAT DC Inhibit Time".
> These are covered in [../elmo/03-elmo-pdo-and-syncmanager.md](../elmo/03-elmo-pdo-and-syncmanager.md).

## 4. The supervisor / recovery thread

`ec_sample` runs a lower-priority `ecatcheck` thread that watches for:
`wkc < expectedWKC`, or `ctx.grouplist[0].docheckstate`. On a lost/degraded slave it
calls `ecx_statecheck`, then `ecx_reconfig_slave` / `ecx_recover_slave` to bring it
back without stopping the whole bus. Keep this **off the RT core**.

## 5. Building SOEM (verified on this host)

```bash
cd third_party/SOEM
cmake -S . -B build          # needs CMake >= 3.28 (host has 3.31)
cmake --build build -j       # produces build/libsoem.a + samples in build/
sudo ./build/slaveinfo eth0  # first real-hardware smoke test
```

Useful CMake cache options (all `-D...`): `EC_MAXSLAVE`, `EC_MAXGROUP`,
`EC_MAXMBX`, `EC_TIMEOUTRET`, `EC_TIMEOUTSTATE`, and the primary source MAC.
For cross-compiling to aarch64 (Zynq), pass a toolchain file:
`cmake -S . -B build-aarch64 -DCMAKE_TOOLCHAIN_FILE=<zynq-toolchain>.cmake`.

## 6. Bring-up checklist on the MPSoC

1. Kernel is PREEMPT_RT; `eth0` (GEM/`macb`) is up, unconfigured, isolated IRQ.
2. `slaveinfo eth0` enumerates the Elmo drive(s) → dump OD & PDOs; confirm CoE + DC.
3. Modify `ec_sample`: add the Elmo PO→SO hook (PDO map + `0x6060` mode = 8 for CSP).
4. Run at 1 kHz, no DC first (prove PDO exchange + CiA402 enable).
5. Enable DC SYNC0 + PI phase lock; verify low following error.
6. Tighten cycle toward the target rate; measure jitter (`cyclictest` alongside).

Elmo-specific electrical/state details continue in
[../elmo/04-elmo-bringup-with-soem.md](../elmo/04-elmo-bringup-with-soem.md).
