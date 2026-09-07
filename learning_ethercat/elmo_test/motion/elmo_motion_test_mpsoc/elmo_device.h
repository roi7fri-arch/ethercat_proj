#ifndef ELMO_DEVICE_H
#define ELMO_DEVICE_H

#include "elmo_ICD.h"

void shutdown_func();
void update_el_outputs(int32 pos, int32 vel, int16 torque);
void update_tr_outputs(int32 pos, int32 vel, int16 torque);
void update_platinum_outputs(int32 pos_axis_1, int32 vel_axis_1, int16 torque_axis_1, int32 pos_axis_2, int32 vel_axis_2, int16 torque_axis_2);
void update_inputs_telemetry_buf(in_ELMOt* elmo_inTR, in_ELMOt* elmo_inEL);
void update_outputs_telemetry_buf(out_ELMOt* elmo_outTR, out_ELMOt* elmo_outEL);
ELMO_SW_ENUM ParseStatusWord(StatusWord sw);
void startMotorTimerFunction(uint16 slave_no);
void create_log_file(in_ELMOt* elmo_inTR, in_ELMOt* elmo_inEL, out_ELMOt* elmo_outTR,
													out_ELMOt* elmo_outEL, int buf_size);

inline short calcTargetTorque(float amp) { return (short) ( (1000000 * amp) / 100000);}

#endif
