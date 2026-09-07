# 07 — CiA 402 Drive Profile (controlling the engine drivers)

CiA 402 (IEC 61800-7-201/301) is the standardized **device profile for motion
controllers and drives**. Our EtherCAT servo/engine drivers implement it over **CoE**.
This chapter is the practical "how to command a drive" reference.

## 1. The CiA 402 (DS402) drive state machine

The drive itself has an internal state machine (separate from the ESM in
[04](04-state-machine-esm.md)). The master transitions it by writing the **Controlword
(0x6040)** and observes the **Statusword (0x6041)**.

```
        Not Ready to Switch On
                |
        Switch On Disabled
                |  (shutdown)
        Ready to Switch On
                |  (switch on)
        Switched On
                |  (enable operation)
        Operation Enabled   <-- motor is energized & following commands
                |  (disable operation / quick stop / fault)
        Quick Stop Active / Fault Reaction Active
                |
             Fault  --(fault reset)--> Switch On Disabled
```

### Statusword (0x6041) bit decoding → state

Mask the low bits of the statusword to determine the state:

| Statusword (bits, `xxxx xxxx x0xx 1111` style) | State |
|-----------------------------------------------|-------|
| `.... .... .0.. 0000` (0x40 set: 0x40) | Not ready to switch on |
| `0x40` and bit6=1 → `0100 0000` | Switch On Disabled |
| `0010 0001` = 0x21 | Ready to Switch On |
| `0010 0011` = 0x23 | Switched On |
| `0010 0111` = 0x27 | Operation Enabled |
| `0000 0111` = 0x07 | Quick Stop Active |
| `0000 1111` = 0x0F | Fault Reaction Active |
| `0000 1000` = 0x08 | Fault |

Practical masks:
- `status & 0x004F == 0x0040` → Switch On Disabled
- `status & 0x006F == 0x0021` → Ready to Switch On
- `status & 0x006F == 0x0023` → Switched On
- `status & 0x006F == 0x0027` → **Operation Enabled** (ready to move)
- `status & 0x004F == 0x0008` → Fault

Other useful statusword bits:
- bit 3 = **Fault**
- bit 4 = Voltage enabled
- bit 5 = Quick stop (0 = quick stop active)
- bit 7 = Warning
- bit 10 = **Target reached**
- bit 11 = Internal limit active
- bits 12/13 = mode-specific (e.g., "following error", "set-point acknowledge",
  "homing attained")

### Controlword (0x6040) commands

| Command | Bits 3..0 (and 7) | Value (typical) | Transition |
|---------|-------------------|-----------------|------------|
| Shutdown | EN=0,QS=1,SO=0 | 0x0006 | → Ready to Switch On |
| Switch On | EN=0,QS=1,SO=1 | 0x0007 | → Switched On |
| Enable Operation | EO=1,EN=1,QS=1,SO=1 | 0x000F | → Operation Enabled |
| Disable Operation | EO=0 | 0x0007 | → Switched On |
| Disable Voltage | EN=0 | 0x0000 | → Switch On Disabled |
| Quick Stop | QS=0 | 0x0002 | → Quick Stop Active |
| **Fault Reset** | bit7 rising edge | 0x0080 | Fault → Switch On Disabled |

Controlword bits:
- bit 0 = Switch On
- bit 1 = Enable Voltage
- bit 2 = Quick Stop (active-low logic in the table)
- bit 3 = Enable Operation
- bit 7 = **Fault Reset** (0→1 edge clears fault)
- bits 4,5,6,8 = **mode-specific** (e.g. PP: new set-point / change-set-immediately;
  HM: homing start)

### Standard enable sequence (what our master runs each drive through)

```
loop each cycle until Operation Enabled:
  read statusword
  if Fault:                 controlword = 0x0080   # fault reset (edge)
  elif SwitchOnDisabled:    controlword = 0x0006   # shutdown
  elif ReadyToSwitchOn:     controlword = 0x0007   # switch on
  elif SwitchedOn:          controlword = 0x000F   # enable operation
  elif OperationEnabled:    -> done, now stream set-points
```

Do this **inside the cyclic PDO loop** (controlword/statusword are PDO-mapped), advancing
one transition per cycle. Never busy-wait on SDOs for this.

## 2. Modes of operation

Set via **Modes of Operation (0x6060, int8)**; read back via **Modes of Operation Display
(0x6061)**. For synchronized multi-axis motion over EtherCAT+DC, the **cyclic synchronous**
modes are used:

| Mode | 0x6060 value | Master streams (RxPDO) | Drive closes loop | Use |
|------|--------------|------------------------|-------------------|-----|
| **CSP** — Cyclic Synchronous Position | 8 | **Target position 0x607A** | position (+vel/torque FF) | **Most common for coordinated motion** |
| **CSV** — Cyclic Synchronous Velocity | 9 | **Target velocity 0x60FF** | velocity | velocity control |
| **CST** — Cyclic Synchronous Torque | 10 | **Target torque 0x6071** | torque/current | force/torque control |
| PP — Profile Position | 1 | Target position + trigger bits | drive generates profile | point-to-point (non-DC-critical) |
| PV — Profile Velocity | 3 | Target velocity | drive ramps | |
| PT — Profile Torque | 4 | Target torque | | |
| **HM** — Homing | 6 | Homing method/speeds | drive homes | establish reference |
| IP — Interpolated Position | 7 | Interpolation buffer | legacy of CSP | older drives |

In **CSP/CSV/CST** the master runs the trajectory generator and sends a fresh set-point
**every cycle**, synchronized by **SYNC0** (DC). This is the deterministic motion path.

## 3. Key CiA 402 objects (the ones we map/parameterize)

### Cyclic (PDO-mapped) objects

| Index:Sub | Name | Type | Dir | Modes |
|-----------|------|------|-----|-------|
| 0x6040:00 | Controlword | U16 | Rx | all |
| 0x6041:00 | Statusword | U16 | Tx | all |
| 0x6060:00 | Modes of operation | I8 | Rx | all |
| 0x6061:00 | Modes of operation display | I8 | Tx | all |
| 0x607A:00 | Target position | I32 | Rx | CSP/PP |
| 0x60FF:00 | Target velocity | I32 | Rx | CSV/PV |
| 0x6071:00 | Target torque | I16 | Rx | CST/PT |
| 0x6064:00 | Position actual value | I32 | Tx | all |
| 0x606C:00 | Velocity actual value | I32 | Tx | all |
| 0x6077:00 | Torque actual value | I16 | Tx | all |
| 0x60B1:00 | Velocity offset (feed-forward) | I32 | Rx | CSP |
| 0x60B2:00 | Torque offset (feed-forward) | I16 | Rx | CSP/CSV |
| 0x60FD:00 | Digital inputs | U32 | Tx | all |
| 0x60FE:00 | Digital outputs | U32 | Rx | all |

### Acyclic (SDO, set in PRE-OP) parameters

| Index:Sub | Name | Notes |
|-----------|------|-------|
| 0x6060 | Modes of operation | often also set once via SDO |
| 0x607A/0x607D | Target / software position limits | 0x607D:01 min, :02 max |
| 0x6081 | Profile velocity | PP mode |
| 0x6083/0x6084 | Profile accel / decel | |
| 0x6085 | Quick stop deceleration | |
| 0x6098 | Homing method | HM mode |
| 0x6099:01/02 | Homing speeds | |
| 0x609A | Homing acceleration | |
| 0x6091 | Gear ratio | |
| 0x608F | Position encoder resolution | scaling |
| 0x6072 | Max torque | limit |
| 0x6075 | Motor rated current | scaling for torque (per-mille of rated) |
| 0x6076 | Motor rated torque | |
| 0x603F | Error code | last fault (read) |
| 0x1001 | Error register | CiA 301 |

**Units caution:** target torque (0x6071) is usually in **per-thousand of motor rated
torque** (0x6076). Position units depend on the drive's factor group / encoder resolution.
Always verify each drive's ESI/manual for scaling before commanding motion.

## 4. Minimal CSP RxPDO / TxPDO the master maps

A typical CSP setup:

- **RxPDO (0x1600):** Controlword(0x6040), Target position(0x607A),
  optionally Modes(0x6060), Velocity offset(0x60B1), Torque offset(0x60B2).
- **TxPDO (0x1A00):** Statusword(0x6041), Position actual(0x6064),
  optionally Velocity actual(0x606C), Torque actual(0x6077), Modes display(0x6061).

## 5. Bring-up order for one axis (CSP)

1. ESM: INIT → PREOP.
2. SDO (PREOP): set 0x6060 = 8 (CSP), configure limits/scaling, set PDO assignment/mapping.
3. Configure DC: SYNC0 = control cycle (DC-Synchronous).
4. ESM: PREOP → SAFEOP; start cyclic frame; verify WKC + DC lock.
5. Seed **Target position = Position actual** every cycle **before** enabling (so the
   first enabled cycle commands zero motion — avoids a jump).
6. Run the enable sequence (0x06 → 0x07 → 0x0F) until **Operation Enabled**.
7. ESM: SAFEOP → OP.
8. Begin streaming trajectory into 0x607A each cycle.

**Safety:** on WKC error, slave dropout, or e-stop → command Quick Stop (0x6040=0x0002)
or Disable Voltage, and rely on the SM watchdog as the last line of defense.

Continue to [08-master-implementation.md](08-master-implementation.md).
