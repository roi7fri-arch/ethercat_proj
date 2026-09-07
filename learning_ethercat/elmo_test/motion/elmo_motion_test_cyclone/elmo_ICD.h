#ifndef ELMO_ICD_H
#define ELMO_ICD_H

#define ELMO_EL 1
#define ELMO_TR 2


typedef struct PACKED
{
	unsigned short switchOn					:1;
	unsigned short enableVoltage			:1;
	unsigned short quickStop				:1;
	unsigned short enableOperation			:1;
	unsigned short operationModeSpesific	:3;
	unsigned short faultReset				:1;
	unsigned short halt						:1;
	unsigned short reserved					:2;
	unsigned short manufactorSpecific		:5;
} Control_Word;

typedef struct PACKED
{
	unsigned short readyToSwitchOn			:1;
	unsigned short switchedOn				:1;
	unsigned short operationEnabled			:1;
	unsigned short fault					:1;
	unsigned short voltageEnabled			:1;
	unsigned short quickStop				:1;
	unsigned short switchOnDisabled			:1;
	unsigned short warning					:1;
	unsigned short reserved					:1;
	unsigned short remote					:1;
	unsigned short targetReached			:1;
	unsigned short internalLimitActive		:1;
	unsigned short operationalModeSpecific	:2;
	unsigned short reserved2				:2;
} StatusWord;

typedef struct PACKED
{
//	int32  target_velocity;
	Control_Word control_word;
	int16  target_torque;
//	Control_Word control_word2;
} out_ELMOt;

typedef struct PACKED
{
	int32  vx;
//	int32 velocity_demand;
//	int16  torque_demand;
	StatusWord status_word;
	int16 currentActualValue;
//	int32  position_actual;
//	uint32 digital_inputs;
//	int32 velocity_actual;
//	uint16 status_word2;
} in_ELMOt;

typedef enum
{
	ELMO_SW_FAULT,
	ELMO_SW_SEND_STAGE_1,
	ELMO_SW_SEND_STAGE_2,
	ELMO_SW_MOTOR_OPERATIONAL,
	ELMO_SW_MOTOR_ON
} ELMO_SW_ENUM;


#endif
