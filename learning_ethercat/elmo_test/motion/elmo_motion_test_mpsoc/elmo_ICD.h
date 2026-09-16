#ifndef ELMO_ICD_H
#define ELMO_ICD_H

/* Elmo-specific process-image layout.
 *
 * The controlword/statusword bit layouts that used to be declared here are
 * plain CiA 402 and now live in src/drive/cia402.h, shared by every drive
 * family. Only the Elmo PDO struct layouts below are genuinely vendor-specific
 * - and those disappear too once the signal-binding layer replaces them.
 */
#include "cia402.h"

/* Legacy names kept so existing call sites keep compiling unchanged. */
typedef cia402_controlword_bits_t Control_Word;
typedef cia402_statusword_bits_t  StatusWord;

typedef struct PACKED
{
	Control_Word control_word;
//	int16  target_torque;
//	int8   modes_of_operation;
	int32 position_offset;
	int32 velocity_offset;
	int16 torque_offset;
//	Control_Word control_word2;
} out_ELMOt;

typedef struct PACKED
{
	StatusWord status_word;
	int8  mode_oper;
	uint16 error_code;
	int32 position_demand;
	int32 velocity_demand;
	int16 torque_demand;
	int32 position_actual;
	int32 velocity_actual;
	int16 torque_actual;
	int16 current_actual;
	int32 position_error_actual;
	float32 drive_temp;
	float32 sample_vq;
//	float64 socket_pos1;
//	float32 socket_velocity1;
//	float64 socket_pos2;
//	float32 socket_velocity2;
//	float64 socket_pos3;
//	float32 socket_velocity3;
//	uint32 analog_sensor_amp;
//	float32 sample_vq;
//	int32 velocity_sensor_actual;

//	uint32 dc_link_circuit_volt;
//	uint32 input_latch_local_time;
//	int32 position_demand_value;
//	uint32 digital_inputs;
//	int16 analog_inputs;
//	int32 auxiliary_position_act_val;
//	int16 current_actual_value;
//	int32 vx;
//	int32 velocity_demand;
//	int16  torque_demand;
//	int16 currentActualValue;
//	int32  position_actual;
//	uint32 digital_inputs;
//	int32 velocity_actual;
//	uint16 status_word2;
} in_ELMOt;

typedef struct PACKED
{
	Control_Word control_word_axis1;
	int32 position_offset_axis1;
	int32 velocity_offset_axis1;
	int16 torque_offset_axis1;
	Control_Word control_word_axis2;
	int32 position_offset_axis2;
	int32 velocity_offset_axis2;
	int16 torque_offset_axis2;
} out_ELMO_platt;

typedef struct PACKED
{
	StatusWord status_word_axis1;
	int8  mode_oper_axis1;
	uint16 error_code_axis1;
	int32 position_demand_axis1;
	int32 velocity_demand_axis1;
	int16 torque_demand_axis1;
	int32 position_actual_axis1;
	int32 velocity_actual_axis1;
	int16 torque_actual_axis1;
	int16 current_actual_axis1;
	int32 position_error_actual_axis1;
	float32 drive_temp_axis1;
	float32 sample_vq_axis1;
	StatusWord status_word_axis2;
	int8  mode_oper_axis2;
	uint16 error_code_axis2;
	int32 position_demand_axis2;
	int32 velocity_demand_axis2;
	int16 torque_demand_axis2;
	int32 position_actual_axis2;
	int32 velocity_actual_axis2;
	int16 torque_actual_axis2;
	int16 current_actual_axis2;
	int32 position_error_actual_axis2;
	float32 drive_temp_axis2;
	float32 sample_vq_axis2;
//	float64 socket_pos1;
//	float32 socket_velocity1;
//	float64 socket_pos2;
//	float32 socket_velocity2;
//	float64 socket_pos3;
//	float32 socket_velocity3;
//	uint32 analog_sensor_amp;
//	float32 sample_vq;
//	int32 velocity_sensor_actual;

//	uint32 dc_link_circuit_volt;
//	uint32 input_latch_local_time;
//	int32 position_demand_value;
//	uint32 digital_inputs;
//	int16 analog_inputs;
//	int32 auxiliary_position_act_val;
//	int16 current_actual_value;
//	int32 vx;
//	int32 velocity_demand;
//	int16  torque_demand;
//	int16 currentActualValue;
//	int32  position_actual;
//	uint32 digital_inputs;
//	int32 velocity_actual;
//	uint16 status_word2;
} in_ELMO_platt;


typedef enum
{
	ELMO_SW_FAULT,
	ELMO_SW_SEND_STAGE_1,
	ELMO_SW_SEND_STAGE_2,
	ELMO_SW_MOTOR_OPERATIONAL,
	ELMO_SW_MOTOR_ON
} ELMO_SW_ENUM;


#endif
