#ifndef ECAT_COE_H
#define ECAT_COE_H

/*
 * Binds the vendor-neutral drive_coe_ops_t to SOEM's CoE SDO calls.
 *
 * This is the only file that connects src/drive and src/vendors to the actual
 * EtherCAT stack. Everything above it is testable on the host by supplying a
 * different drive_coe_ops_t (see sim_test/drive_profile_test.c).
 */

#include "drive_profile.h"

/* CoE operations backed by ec_SDOwrite / ec_SDOread. */
const drive_coe_ops_t *ecat_coe_ops(void);

/* PRE-OP -> SAFE-OP hook to install in ec_slave[n].PO2SOconfig.
 *
 * Looks the slave up in g_ecat_config, picks the drive profile named by its
 * "profile" field, and runs that profile's setup. Replaces the single
 * hard-coded elmo_platinum_setup_from_config assignment, so a bus can mix
 * drive families without any code change.
 *
 * Returns 1 on success, 0 on failure (SOEM's convention).
 */
int ecat_coe_po2so_config(uint16_t slave);

#endif /* ECAT_COE_H */
