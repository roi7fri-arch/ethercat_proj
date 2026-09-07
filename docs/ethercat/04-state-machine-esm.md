# 04 — EtherCAT State Machine (ESM)

Every slave runs the **EtherCAT State Machine (ESM)**. The master drives all slaves
through these states during startup and monitors them at runtime. State is controlled via
the ESC **AL Control** register (0x0120) and reported via **AL Status** (0x0130); failures
are explained by **AL Status Code** (0x0134).

## 1. The states

```
        (power on)
            |
          INIT
            |  ^        BOOTSTRAP  (firmware update via FoE only)
   (SM mailbox cfg)      ^
            v            |
          PRE-OP  <------+
            |  ^   (mailbox active: SDO config here)
   (SM2/3 + FMMU cfg)
            v
         SAFE-OP
            |  ^   (inputs valid; outputs NOT yet applied by slave)
   (master sends valid outputs)
            v
           OP        (full operation: outputs applied, cyclic PDO running)
```

| State | Mailbox | Process data inputs | Process data outputs | Use |
|-------|---------|--------------------|----------------------|-----|
| **INIT** | No | No | No | Base state; only register access (APRD/FPWR). Master assigns station address, sets up mailbox SMs |
| **PRE-OP** | **Yes** | No | No | Mailbox (CoE/SDO) works. Master configures PDO mapping & drive parameters here |
| **SAFE-OP** | Yes | **Yes** (valid) | No (safe state) | Slave publishes inputs; outputs ignored/safe. Master starts cyclic frames, verifies WKC, aligns DC |
| **OP** | Yes | Yes | **Yes** | Normal operation; outputs applied. Motion happens here |
| **BOOTSTRAP** | Yes (FoE only) | No | No | Firmware download only; reached from INIT |

## 2. Transitions (names used in ESI "Startup" tab and stacks)

| Transition | From → To | What the master does |
|-----------|-----------|----------------------|
| IP | INIT → PRE-OP | Configure mailbox SM0/SM1; enable mailbox |
| PS | PRE-OP → SAFE-OP | Send SDO init list (PDO assign/map, params); configure SM2/SM3 + FMMU + DC |
| SO | SAFE-OP → OP | Begin sending valid outputs; request OP |
| SP | SAFE-OP → PRE-OP | Back off |
| OS | OP → SAFE-OP | Stop applying outputs |
| OI, SI, PI | any → INIT | Reset path |
| IB | INIT → BOOTSTRAP | Enter firmware update |

The **Startup** list in an ESI/ENI attaches SDO writes to `<PS>` or `<SO>` transitions.
That's exactly where our master will push CiA 402 configuration objects.

## 3. AL Control / AL Status registers

- **AL Control (0x0120), 16-bit:** low nibble = requested state (1=INIT, 2=PREOP,
  4=SAFEOP, 8=OP; 3=BOOTSTRAP). Bit 4 = **Error Ack** (acknowledge/clear error).
- **AL Status (0x0130), 16-bit:** low nibble = actual state; **bit 4 = Error indicator**
  (state change failed → current state shows e.g. "ERR PREOP").
- **AL Status Code (0x0134), 16-bit:** reason code, e.g.:
  - 0x0000 no error
  - 0x0011 invalid requested state change
  - 0x0016 invalid mailbox configuration
  - 0x001A synchronization error / watchdog
  - 0x001B sync manager watchdog
  - 0x0024/0x0025 invalid SM IN/OUT config (bad PDO sizes)
  - 0x002C/0x0030 DC/sync configuration errors
  - 0x001D/0x001E invalid output/input mapping

When a transition fails, the master reads AL Status Code to diagnose (very often a
mismatched PDO size → "invalid SM cfg").

## 4. Typical master startup sequence (per slave and network)

1. **Scan bus** (auto-increment addressing): count slaves, read SII identity
   (vendor/product/revision), read AL status. Confirm topology vs. expected.
2. Request **INIT** for all (BWR AL Control), clear errors.
3. Assign **configured station addresses** (FPWR).
4. Configure **mailbox** SMs → request **PRE-OP**.
5. In PRE-OP: run **CoE SDO** startup list — set modes, limits, and **PDO
   assignment/mapping** (objects 0x1C12/0x1C13, 0x1600.., 0x1A00..).
6. Configure **SM2/SM3**, **FMMU**, **watchdog**, and **DC** (SYNC0/1, cycle, offset).
7. Request **SAFE-OP**; start the **cyclic frame**; verify **WKC** and DC lock.
8. Send valid outputs; request **OP**. Confirm all slaves report OP.
9. Enter the real-time control loop (drive CiA 402 FSM to enable torque).

## 5. Runtime monitoring

- Each cycle: check returned **WKC** and periodically **BRD AL Status**.
- If a slave leaves OP (e.g., drive fault → drops to SAFEOP), the master detects it and
  must react (stop motion, alarm, attempt recovery).
- **AL Status Code** + CoE **Emergency (EMCY)** messages give the fault reason.

Continue to [05-distributed-clocks.md](05-distributed-clocks.md).
