/** \file
 * \brief Virtual EtherCAT slave (software ESC) for testing a SOEM master
 *        without any real hardware.
 *
 * The emulator understands enough of the EtherCAT on-the-wire protocol to let
 * SOEM 1.3.1 discover one virtual Elmo-style CiA402 axis, walk the AL state
 * machine to OP and exchange cyclic process data with the correct Working
 * Counter (WKC).
 *
 * It is deliberately transport agnostic: it only ever sees the raw EtherCAT
 * payload (everything after the 14 byte Ethernet header) and mutates it in
 * place, exactly like a real ESC ASIC would while a frame passes through it.
 */
#ifndef SLAVE_SIM_H
#define SLAVE_SIM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum number of virtual slaves that can be daisy-chained on the bus. */
#define SLAVESIM_MAX_SLAVES 8

/** Reset to a single virtual slave in power-on state (INIT, SII loaded). */
void slavesim_init(void);
/** Reset to \a n identical virtual slaves (clamped to [1, SLAVESIM_MAX_SLAVES]). */
void slavesim_init_n(int n);

/** Identity the virtual slaves report in their SII (EEPROM), i.e. which drive
 *  they pretend to be. Call before slavesim_init*(). Defaults to Elmo Platinum.
 *  slavesim_impersonate() in slave_sim_profile.h sets this from a registered
 *  drive profile, which is how a Copley or Maxon bus is simulated without any
 *  hardware. */
void slavesim_set_identity(uint32_t vendor, uint32_t product, uint32_t revision);
void slavesim_get_identity(uint32_t *vendor, uint32_t *product, uint32_t *revision);

/** Build a default single-slave bus only if the caller has not configured one
 *  yet (used by the simulated NIC driver at setup time). */
void slavesim_ensure_default(void);

/** Enable (1) or disable (0) verbose logging of every datagram / unhandled
 *  register access. Off by default. */
void slavesim_set_verbose(int on);

/** Fault injection for tests. \a pos is the 1-based bus position, or 0 for all
 *  slaves. Flags reset on every slavesim_init*(), so set them after reaching OP.
 *
 *  freeze:   stop mirroring RxPDO -> TxPDO, so the feedback image stalls at its
 *            last value while the master keeps commanding (simulates a hung
 *            encoder / stale feedback).
 *  wkc_drop: withhold this slave's input-side Working Counter contribution, so
 *            the master sees WKC below the expected value (simulates a slave
 *            whose response is lost / not processed). */
void slavesim_set_freeze(int pos, int on);
void slavesim_set_wkc_drop(int pos, int on);

/** Inject an RX/CRC error-counter value on \a port (0..3) of the slave at
 *  1-based \a pos (0 = all). Served on register reads of the ESC error block
 *  (0x0300..0x0313), so ecat_diag sees the counter as if the ESC accumulated it. */
void slavesim_set_rxcrc(int pos, int port, uint8_t count);

/** Queue a CoE Emergency (EMCY) message on the slave at 1-based \a pos (0 = all).
 *  The master picks it up on the next ec_mbxreceive() for that slave and SOEM
 *  pushes it onto its error list (drained by ecat_diag_drain_errors). */
void slavesim_queue_emergency(int pos, uint16_t error_code, uint8_t error_reg);

/** Set the DC System Time Difference (ESC reg 0x092C) reported by the slave at
 *  1-based \a pos (0 = all). \a ns is the |deviation| magnitude, \a ahead != 0
 *  marks the local clock as ahead of the reference. Lets ecat_diag_dc_* see a
 *  drifting / locked distributed clock without a real DC-capable ESC. */
void slavesim_set_dcdiff(int pos, uint32_t ns, int ahead);

/** Copy the bytes received by the slave at 1-based \a pos via the last FoE
 *  download into \a buf (up to \a cap). Returns the total number of bytes the
 *  slave received (may exceed \a cap). Used to verify ecat_foe_download_*. */
int slavesim_foe_received(int pos, uint8_t *buf, int cap);

/** Read back a CoE object-dictionary value stored on the slave at 1-based \a pos
 *  (as written by the master via SDO download). Returns 1 and sets \a val when
 *  the object exists, 0 otherwise. Used to verify per-slave startup_sdo writes. */
int slavesim_od_get(int pos, uint16_t index, uint8_t sub, uint32_t *val);

/** Process one EtherCAT payload in place.
 *
 * \param ecat  pointer to the EtherCAT frame payload (frame + 14 byte ETH hdr),
 *              i.e. starting at the 2 byte EtherCAT frame header.
 * \param len   length in bytes of that payload.
 * \return 0 (the payload, including per-datagram WKC fields, is updated in place).
 */
int slavesim_process(uint8_t *ecat, int len);

#ifdef __cplusplus
}
#endif

#endif /* SLAVE_SIM_H */
