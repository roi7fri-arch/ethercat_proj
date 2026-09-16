#ifndef ECAT_PARAM_H
#define ECAT_PARAM_H

/*
 * Pushing files to a drive over FoE, including the BOOT-state transition.
 *
 * Separate from src/params/param_apply.h because this part genuinely needs the
 * EtherCAT state machine and the SII, whereas writing CoE parameters does not.
 *
 * Writing firmware is not reversible and a bad image can brick a drive, so
 * nothing here is ever reached from the cyclic path - only from the explicit
 * maintenance tool (src/tools/ecat_param_tool.c).
 */

#include "param_set.h"

/* Take `slave` to BOOT state, remapping its mailbox sync managers to the boot
 * mailbox advertised in the SII. Returns 0 on success. */
int ecat_param_enter_boot(int slave);

/* Return `slave` to INIT. */
int ecat_param_leave_boot(int slave);

/* Push one file, handling the BOOT transition when the entry asks for it.
 * Returns 0 on success. */
int ecat_param_send_file(const param_file_t *file);

/* Push every file in the set. Returns the number of failures. */
int ecat_param_send_files(const param_set_t *set);

#endif /* ECAT_PARAM_H */
