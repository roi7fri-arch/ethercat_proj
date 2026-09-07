# 09 — Xilinx MPSoC + Real-Time Linux for a Deterministic Master

Target hardware: **Xilinx Zynq UltraScale+ MPSoC** (e.g. ZU+ EG/EV/CG). The master runs on
the **APU** (quad Cortex-A53) under Linux. This chapter is about making the platform
deterministic enough for hard-RT EtherCAT.

## 1. MPSoC building blocks relevant to us

- **APU:** 4× Cortex-A53 (ARMv8-A, 64-bit, little-endian). We run Linux here and pin the
  RT loop to an isolated core.
- **RPU:** 2× Cortex-R5F (lockstep-capable) — optional hard-RT co-processor. An advanced
  option is to run the EtherCAT cyclic loop bare-metal/FreeRTOS on an R5 and let Linux on
  the A53s do supervision. For the first stage we keep everything on Linux/A53.
- **PS Ethernet (GEM):** Cadence Gigabit MAC, Linux driver **`macb`** (a.k.a.
  `cadence/macb`). IgH master has a native `macb` driver — good for low-latency framing.
- **PL (FPGA fabric):** could host an EtherCAT **slave** IP or an ethernet MAC, or offload
  DC/timestamping. Not required for a software master; useful later for a hardware-assisted
  design.
- **TCM / OCM / DDR:** keep RT code/data hot; avoid DDR contention on the RT path where
  possible.

## 2. Real-time Linux options

| Option | What it is | Fit |
|--------|-----------|-----|
| **PREEMPT_RT** (mainline RT patch) | Makes the kernel fully preemptible; IRQ threads; priority inheritance | **Primary choice.** Xilinx/AMD provides RT kernel configs; ~tens of µs worst-case latency achievable |
| Xenomai (dual kernel, Cobalt) | Co-kernel alongside Linux; sub-10 µs | Possible with IgH (has Xenomai support), heavier to maintain |
| Bare-metal/FreeRTOS on RPU | Hard-RT loop on Cortex-R5 | Best determinism; more integration work; stage-2 option |

For stage 1: **PREEMPT_RT on the A53s + IgH master**.

## 3. Getting a PREEMPT_RT kernel on ZynqMP

- Use AMD/Xilinx Yocto (**PetaLinux** / meta-xilinx) and enable the **`-rt`** kernel
  (`linux-xlnx` with the RT patch matching the kernel version), or apply the matching
  `patch-*-rtN` to `linux-xlnx`.
- Verify: `uname -v` shows `PREEMPT RT`; `CONFIG_PREEMPT_RT=y`.
- Build the **IgH master** against this exact kernel (kernel modules are version-locked).
  Rebuild modules whenever the kernel changes.

## 4. Determinism tuning checklist (the important part)

### CPU isolation & affinity
- **`isolcpus=3 nohz_full=3 rcu_nocbs=3`** (kernel cmdline) to dedicate core 3 to the RT
  loop; keep housekeeping on cores 0–2.
- Pin the RT thread with `pthread_setaffinity_np` / `taskset` to the isolated core.
- Move IRQs off the isolated core (`/proc/irq/*/smp_affinity`), **except** the NIC IRQ,
  which you may pin **to** the RT core (or keep it near it) to minimize wake latency.

### Scheduling
- RT thread: `SCHED_FIFO`, priority high (e.g. 80). Below the kernel's IRQ threads if using
  threaded IRQs.
- Lock memory: `mlockall(MCL_CURRENT|MCL_FUTURE)` — no page faults in the loop.
- Pre-fault stack and heap; no `malloc`/syscalls in the hot path.

### Kernel/CPU config
- Disable deep C-states / set CPU governor to **performance**
  (`cpupower frequency-set -g performance`); consider `cpuidle.off=1` or per-core PM QoS to
  bound wake latency.
- Disable frequency scaling jitter (fixed clocks).
- Turn off unneeded services, `irqbalance`, transparent hugepage defrag, etc.
- Consider `processor.max_cstate=1`, `idle=poll` (power cost) for the tightest jitter.

### Timing source
- Use `clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, ...)` with absolute deadlines to
  avoid drift; or a timerfd. Compute next wakeup = prev + period.
- Better: **phase-lock the loop to the DC reference clock** (PI controller adjusts the
  period slightly) so Linux time and bus time don't drift apart. See
  [05](05-distributed-clocks.md) §6–7.

### NIC / networking
- Give the master **exclusive** use of the EtherCAT port (no Linux IP stack on it; IgH
  native driver takes it over).
- Disable offloads that add latency/reorder on that interface if using a generic path.
- Prefer IgH **native** `macb`/`igb`/`e1000e` driver over the generic one for lower jitter.

### Measurement
- `cyclolatency`/**`cyclictest`** (from rt-tests) to characterize worst-case latency:
  `cyclictest -m -p80 -a3 -t1 -i1000 -h200` — aim for max latency ≪ cycle period.
- Log per-cycle: loop execution time, jitter (wake vs deadline), WKC, DC diff (0x092C).
- Establish a **cycle budget**: e.g. at 1 kHz (1000 µs) keep worst-case loop + send/recv +
  margin under ~300–500 µs; leave headroom for the SYNC0 shift.

## 5. Choosing the cycle time

- Start conservative: **1 kHz (1 ms)**. Confirm stable WKC and DC lock, low cyclictest max.
- Increase toward **2–8 kHz** only after determinism is proven and per-cycle work fits the
  budget with margin. More axes / bigger process image → more send/recv time per cycle.
- SYNC0 cycle must equal the loop period; verify each drive supports the chosen rate.

## 6. Safety & robustness on the platform

- **SM watchdog** on each drive (doc 03 §6): if the RT loop stalls, drives disable
  torque. Set WD > cycle but small enough to be safe (e.g. a few cycles).
- Handle NIC/link loss: detect (WKC/link) → command Quick Stop / disable → alarm.
- Watchdog the RT thread from a supervisor (e.g. systemd watchdog / hardware WDT on ZynqMP)
  so a hung master triggers a safe shutdown.
- Persist/rotate logs off the RT path.

## 7. Suggested stage-1 platform bring-up order

1. Boot PetaLinux (non-RT) on ZynqMP; confirm the EtherCAT GEM port and cabling to drives.
2. Switch to a **PREEMPT_RT** kernel; run `cyclictest` and tune (§4) until latency is good.
3. Build & load **IgH master** + native `macb` driver; `ethercat slaves` sees the drives.
4. Bring up **one axis** in CSP at 1 kHz (docs 07–08); prove WKC + DC lock.
5. Scale to all axes; add safety reactions; then push cycle rate if needed.

Continue to [10-master-app-architecture.md](10-master-app-architecture.md).
