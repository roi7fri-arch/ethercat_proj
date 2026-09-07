#ifndef ELMO_DEVICE_H
#define ELMO_DEVICE_H

#include "elmo_ICD.h"

void update_el_outputs(int16 torque_val);
void update_tr_outputs(int16 torque_val);
ELMO_SW_ENUM ParseStatusWord(StatusWord sw);
void startMotorTimerFunction(uint16 slave_no);

inline short calcTargetTorque(float amp) { return (short) ( (1000000 * amp) / 100000);}

#endif
