# Copilot instructions — EtherCAT master

Read this before changing anything. It is short on purpose.

## What this is

A SOEM-based EtherCAT master for CiA 402 servo drives, running on Xilinx MPSoC +
RT-Linux. It started as a single Elmo-specific application and was restructured
so that supporting a new drive family is a configuration exercise, not a code
change.

## Build and test

```bash
make soem app tools    # libsoem.a, ecat_master, ecat_param_tool -> build/host/
make test              # every suite: 408 C assertions + a GUI metadata suite
make TARGET=aarch64    # cross-compile for the MPSoC
```

`make test` needs **no hardware**: an in-process virtual EtherCAT slave
(`learning_ethercat/sim_test/slave_sim.c`) answers the real SOEM master through
a simulated NIC. Always run it after a change.

## Layout and the one rule

```
src/config/    JSON bus configuration -> C structs        (no SOEM)
src/params/    parameter documents + download engine      (no SOEM)
src/pdo/       semantic signal -> process-image bit offset(no SOEM)
src/drive/     CiA 402 FSM, axis object, profile registry (no SOEM)
src/motion/    setpoint sources                           (no EtherCAT at all)
src/ecat/      THE ONLY CODE THAT TOUCHES SOEM
src/vendors/   one directory per drive family
src/tools/     standalone CLIs, each with its own main()
mk/            shared makefile fragments
```

Dependencies point one way only. A vendor file may include `src/drive` and
`src/pdo`; nothing in the core may include a vendor header. CoE access goes
through the `drive_coe_ops_t` vtable rather than SOEM directly — that is what
makes the drive and parameter layers testable on the host.

## Adding a drive family

Usually nothing: write a JSON config naming `cia402_generic`. Only if the drive
genuinely deviates from the standard, add `src/vendors/<vendor>/<family>.c`
defining a `drive_profile_t`, and one line in `src/vendors/vendors.c`. The
Makefile globs the directory, so no build edits are needed.
`learning_ethercat/sim_test/multivendor_sim_test.c` shows the whole path with a
family invented inside the test.

Anything that is plain CiA 402 — controlword bits, the 0x06→0x07→0x0F enable
ladder, 0x6040/0x6041/0x6060 — belongs in `src/drive/cia402.c`, never in a
vendor file.

## Two JSON documents, deliberately separate

| file | what | lifecycle |
|---|---|---|
| `ethercat_config.json` | bus shape: PDO maps, modes, identity | read by the master at **every** start-up |
| `*.params.json` | drive tuning: gains, limits, scaling | written into drive memory **once**, via the GUI |

Do not merge them. They change at completely different rates.

## Traps

- **`third_party/SOEM` is a git submodule** tracking upstream SOEM. Our patched
  copy is `third_party/soem-1.3.1`. Never put files in the submodule path.
- **Eclipse `.cproject` and `Debug/makefile` are dead.** They hard-code absolute
  paths from other developers' machines. Use the root `Makefile`.
- **Never put a `main()` under `src/`.** `mk/core.mk` globs those directories
  into the application; test mains live in `learning_ethercat/sim_test/`.
- Makefile fragments define rules, so any Makefile that includes them must set
  `.DEFAULT_GOAL := all` before the includes.
- `ecat_param_tool` needs raw-socket rights:
  `sudo setcap cap_net_raw,cap_net_admin+eip build/host/ecat_param_tool`

## Still legacy, not yet cleaned up

`learning_ethercat/elmo_test/motion/elmo_motion_test_mpsoc/` holds the RT
application. `data_functions.c`, `ini.c`, `varstable.c` and the dead
`elmo_setup()` / `elmo_platinum_setup()` in `elmo_com.c` are the old `.ini`/`.dat`
parameter path and are not used by the live code path.

## History

The restructuring is one commit with a long message explaining every decision
and the bugs it uncovered:

```bash
git log -1 pre-modular-refactor..main
git checkout pre-modular-refactor   # the state before it
```
