#ifndef JOYSTICK_ETHERCAT_H
#define JOYSTICK_ETHERCAT_H

#define EL_AXIS 0

#define TR_AXIS 2

#define JOY_MAX_VAL 32767

#define MAX_AMP 354

short get_el_torque_val();

short get_tr_torque_val();

void* run(void* data);


#endif
