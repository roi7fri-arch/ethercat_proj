#ifndef ECAT_FOE_H
#define ECAT_FOE_H

/* File-over-EtherCAT (FoE) firmware / file download to a slave.
 *
 * This is a MANUAL, opt-in maintenance action - it is deliberately NOT called
 * anywhere in the normal cyclic start-up/run path. Writing firmware to a drive
 * is not reversible and a bad image can brick the device, so the caller must
 * invoke it explicitly (e.g. a dedicated CLI flag) with the bus stopped in a
 * safe state. The slave must be in a FoE-capable state (typically BOOT, and it
 * must advertise the FoE mailbox protocol) before calling.
 *
 * Uses SOEM's ec_FOEwrite() underneath, so like ecat_diag it needs a live bus
 * (or the virtual slave sim) - it is validated end-to-end against slave_sim.c.
 */

#include <stdint.h>

/* Download an in-memory image to `slave` (1-based) under the FoE `filename`.
 * `password` is the drive-specific FoE password (0 if none). `timeout_us` is
 * the per-mailbox-cycle timeout (EC_TIMEOUTRXM is a sane default).
 * Returns 0 on success, negative on error (SOEM work-counter/FoE error code). */
int ecat_foe_download_buffer(int slave, const char *filename, uint32_t password,
                             const void *data, int size, int timeout_us);

/* Same as above but reads the image from a local file first. Returns 0 on
 * success, -1 if the file cannot be read, or the ec_FOEwrite error otherwise. */
int ecat_foe_download_file(int slave, const char *filename, uint32_t password,
                           const char *localpath, int timeout_us);

#endif /* ECAT_FOE_H */
