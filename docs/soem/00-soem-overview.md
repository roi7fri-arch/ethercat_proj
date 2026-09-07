# SOEM — Overview, Sources, and Licensing

Source downloaded into the workspace at [`third_party/SOEM`](../../third_party/SOEM)
(cloned from `https://github.com/OpenEtherCATsociety/SOEM`, `master`, project
version **2.0.0**). It builds cleanly on this Linux host (verified:
`cmake -S . -B build && cmake --build build` produced `libsoem` + all samples).

## What SOEM is

**SOEM (Simple Open EtherCAT Master)** is a small C library for writing EtherCAT
**MainDevices** (masters). It is *not* an application or a daemon — you link it into
your own program and drive the cycle yourself. This is exactly what we want for a
deterministic controller: **we own the RT loop**, SOEM just does the EtherCAT
mechanics (frame build/parse, SII/EEPROM, mailbox/CoE, PDO mapping, DC setup).

Key properties:
- Pure C, tiny footprint, no dynamic behavior in the cyclic path.
- Runs on Linux, Windows, and RTOS/bare-metal (via the `osal`/`oshw` ports).
- On Linux it talks to the NIC through a **raw packet socket** (`AF_PACKET`) — no
  kernel module required (unlike IgH). That means it needs `CAP_NET_RAW`.
- "Passive" timing model: nothing moves unless *you* call send/receive.

> Contrast with IgH (the other stack in our knowledge base, see
> [../ethercat/08-master-implementation.md](../ethercat/08-master-implementation.md)):
> IgH uses a kernel module + native NIC driver (has a `macb` driver for the Zynq
> GEM). SOEM stays 100% in user space over `AF_PACKET`. For our MPSoC both are
> viable; SOEM is simpler to bring up and port, IgH can give lower IRQ latency.

## Version note — this is the v2.0.0 API (important)

The repo we cloned is the **next-generation API**. It differs from the older,
widely-tutorialized **SOEM 1.4** API:

| Aspect | SOEM 1.4 (legacy) | SOEM 2.0.0 (what we have) |
|--------|-------------------|---------------------------|
| Context | Global `ec_*` state | Explicit `ecx_contextt ctx` passed to every call |
| Headers | `ethercat.h` | `soem/soem.h` (installs under `include/soem/`) |
| Init | `ec_init(ifname)` | `ecx_init(&ctx, ifname)` |
| Config | `ec_config_init`, `ec_config_map` | `ecx_config_init`, `ecx_config_map_group` |
| Mailbox | inline per-call | dedicated **cyclic mailbox handler** (`ecx_mbxhandler`, `ecx_slavembxcyclic`) |

Whenever you find SOEM example code online using `ec_send_processdata()` /
`ec_receive_processdata()` with no context argument, it is 1.4 code — translate it
to the `ecx_*(&ctx, ...)` form. See [01-soem-api-reference.md](01-soem-api-reference.md).

## Repository layout (what each folder is)

| Path | Purpose |
|------|---------|
| `include/soem/` | Public headers. Start at `soem.h` (umbrella include). |
| `src/` | Core library: `ec_main.c` (init/state/IO), `ec_config.c` (auto-config + PDO map), `ec_coe.c` (CoE/SDO/PDO), `ec_dc.c` (Distributed Clocks), `ec_base.c` (datagram primitives), `ec_foe/soe/eoe.c`, `ec_print.c`. |
| `osal/` | OS abstraction layer: `osal/linux/` (time, threads, sleep) — this is what we use. Also `win32/`, `rtk/`. |
| `oshw/` | OS hardware layer: `oshw/linux/nicdrv.c` = the raw-socket NIC driver + `oshw.c` (endian/adapter helpers). |
| `samples/` | Reference programs (see below). Our first code should be a modified `ec_sample`. |
| `contrib/` | Extra ports/tools. |
| `scripts/eniconv.py` | Converts an ENI XML into SOEM's ENI form. |
| `CMakeLists.txt` | Build. All sizes/timeouts are `-D` cache options (e.g. `EC_MAXSLAVE`, `EC_TIMEOUTRXM`). |

## Samples worth reading (in `samples/`)

- **`ec_sample/`** — full lifecycle: raw-socket init → config → PDO map → DC config
  → cyclic RT thread with a **DC↔Linux PI servo** (`ec_sync`) → OP → clean shutdown.
  This is our template. Annotated in [02-soem-linux-rt-and-build.md](02-soem-linux-rt-and-build.md).
- **`simple_ng/`** — minimal cyclic loop, good for first light-up.
- **`slaveinfo/`** — enumerates the bus, prints SII/CoE object dictionary, SM/FMMU,
  DC capability. **Run this first against real Elmo drives** to dump their OD/PDOs.
- **`eepromtool/`** — read/write slave EEPROM (SII). Rarely needed for drives.
- **`eni_test/`**, **`firm_update/`**, **`eoe_test/`** — ENI-driven config, FoE
  firmware update, EoE tunneling. Not needed for the first CSP bring-up.

## Licensing — read before shipping (action item, not optional)

**The license differs by SOEM version — this matters a lot for a proprietary product.**

### What we cloned: SOEM 2.0.0 → GPLv3 + commercial, NO linking exception

Every source file in `third_party/SOEM` carries this header (verified in
`src/ec_main.c`, all `include/soem/*.h`, samples, osal/oshw), and
`third_party/SOEM/LICENSE.md` confirms it:

> "This software is dual-licensed under GPLv3 and a commercial license."

- There is **no linking/"special" exception** in 2.0.0. Under the GPLv3 option,
  linking SOEM into our binary makes the **whole combined work GPLv3** (full source
  disclosure on distribution).
- For a **closed-source** product on 2.0.0 you must buy the **RT-Labs commercial
  license** (sales@rt-labs.com).

### The exception you saw: it's in the LEGACY SOEM 1.4 (GPLv2 + linking exception)

SOEM **1.4.0** source files end with *"Licensed under the GNU General Public License
version 2 **with exceptions**"*, and its `LICENSE` spells out a **linking
exception**:

> "As a special exception, ... you compile this file and **link it with other works
> to produce a work based on this file, this file does not by itself cause the
> resulting work to be covered by the GNU General Public License.** However the
> source code for this file must still be made available in accordance with section
> (3) of the GPL."

Practically, this is a GCC-runtime/Classpath-style exception: **1.4 can be linked
into a proprietary application without the whole app becoming GPL** — you only owe
the source of the (modified) SOEM files themselves. **This exception was dropped in
2.0.0.**

> 1.4's LICENSE also contains a Beckhoff clause: using SOEM to build/sell an EtherCAT
> master presumes an **EtherCAT Master License / Vendor ID from Beckhoff / ETG** —
> a separate IP requirement from the copyright license, and generally applicable to
> any commercial EtherCAT master regardless of stack.

### Version comparison

| | SOEM 1.4.0 (legacy) | SOEM 2.0.0 (cloned) |
|---|---|---|
| Copyleft | GPL**v2** | GPL**v3** |
| Linking exception | **Yes** (proprietary linking OK; publish only SOEM's own source) | **No** |
| Commercial path | Beckhoff master-license clause | Explicit RT-Labs commercial license |
| API | legacy global `ec_*` | new context `ecx_*` |

### Other stacks (for reference)

- ETH Zurich `elmo_ethercat_sdk` (studied for the Elmo OD/PDO layouts): **GPLv3** —
  fine as a reference, same distribution caveat if we reuse its code.
- IgH EtherCAT Master: **GPLv2** (kernel modules) with a commercial option.

> Genuine, hard-to-reverse decision — flag to the project owner early. Options:
> (a) SOEM 2.0.0 + **buy RT-Labs commercial license** (keeps the modern `ecx_` API);
> (b) SOEM 2.0.0 and **ship GPLv3** (open the product); or
> (c) SOEM **1.4** to inherit the **linking exception** for a closed product (at the
> cost of the legacy API and still needing the Beckhoff master license).
> Confirm with legal counsel before committing.

## Where SOEM fits in our architecture

SOEM replaces the "EtherCAT stack" box in
[../ethercat/10-master-app-architecture.md](../ethercat/10-master-app-architecture.md).
Our RT thread calls `ecx_send_processdata` / `ecx_receive_processdata` each cycle;
our CiA 402 logic reads/writes the PDO image that SOEM maps into `IOmap`. Elmo
specifics (object dictionary, PDO layout, bring-up) are in
[../elmo/](../elmo/00-elmo-platinum-overview.md).
