#ifndef ELMO_DEVICE_H
#define ELMO_DEVICE_H

#include "elmo_ICD.h"

/* What used to be declared here - update_el_outputs(), update_tr_outputs(),
 * update_platinum_outputs(), update_*_telemetry_buf(), ParseStatusWord() and
 * startMotorTimerFunction() - now lives, vendor-neutral, in src/drive/axis.h
 * and src/drive/cia402.h. */

void shutdown_func();
void create_log_file(in_ELMOt* elmo_inTR, in_ELMOt* elmo_inEL, out_ELMOt* elmo_outTR,
													out_ELMOt* elmo_outEL, int buf_size);

#endif
