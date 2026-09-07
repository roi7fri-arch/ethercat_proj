#ifndef MDCU_ELMO_ICD_H
#define MDCU_ELMO_ICD_H

#ifndef RCWS_Sim
#include <Common/InterfaceDefs.h>
#else
#include "InterfaceDefs.h"
#endif

#pragma once
#pragma pack(push, 1)
namespace InterfaceDefs
{

    typedef enum {
        EL_NODE_ID    = 0X1,
        TR_NODE_ID    = 0X2
    }ELMO_NODE_ID;

    typedef enum  {                             /* Element 302 */
      // ELMO to MDCU
       ELMO_2_MDCU_EL_EMERGENCY                       = 0x81,
       ELMO_2_MDCU_TR_EMERGENCY                       = 0x82,
       ELMO_2_MDCU_EL_TPDO1                           = 0x180 + EL_NODE_ID,
       ELMO_2_MDCU_TR_TPDO1                           = 0x180 + TR_NODE_ID,
       ELMO_2_MDCU_EL_POWER_UP_COB_ID                 = 0x700 + EL_NODE_ID, // 700 + node ID
       ELMO_2_MDCU_TR_POWER_UP_COB_ID                 = 0x700 + TR_NODE_ID,
       ELMO_2_MDCU_EL_SDO_RESPONSE                    = 0x580 + EL_NODE_ID,
       ELMO_2_MDCU_TR_SDO_RESPONSE                    = 0x580 + TR_NODE_ID,
       ELMO_2_MDCU_HEARTBEAT                          = 0x725,


       //MDCU to ELMO
       MDCU_2_ELMO_EL_RPDO1                           = 0x200 + EL_NODE_ID,
       MDCU_2_ELMO_TR_RPDO1                           = 0x200 + TR_NODE_ID,
       MDCU_2_ELMO_ELEVATION_SDO_COB_ID               = 0x600 + EL_NODE_ID,
       MDCU_2_ELMO_TRANSVERSE_SDO_COB_ID              = 0x600 + TR_NODE_ID,
       MDCU_2_ELMO_NMT_MODULE_CONTROL_COB_ID          = 0x000,
       MDCU_2_ELMO_SYNC_COB_ID                        = 0x080

    }ELMO_COB_ID;


    typedef enum
       {
          ELMO_NO_COM   = 0x0,
          ELMO_COM_OK   = 0x1
       }ELMO_COMMUNICATION_LINK;

    typedef enum{
        UPLOAD_SDO          = 0,
        DOWNLOAD_SDO        = 1,
        INIT_SDO_UPLOAD     = 2,
        INIT_SDO_DOWNLOAD   = 3
    }ELMO_SCS;

    typedef enum{
        ELMO_STATUS_WORD = 0x6041,
        ELMO_VX          = 0x6069,
        ELMO_VU          = 0x606C,
        ELMO_SR          = 0x1002,
        ELMO_MOTOR_RATED_CURRENT = 0x6075
    }ELMO_ADDRESS;


    typedef enum
    {
        UM_TORQUE_CONTROL_LOOP          = 1,
        UM_SPEED_CONTROL_LOOP           = 2,
        UM_STEPPER                      = 3,
        UM_RESERVED                     = 4,
        UM_POSITION_LOOP                = 5,
        UM_STEPPER_OPEN_OR_CLOSED_LOOP  = 6
    }UM_ENUM;

    enum eAmplfierStatus
    {
        AMPLFIER_OK 				= 0,
        AMPLFIER_UNDERVOLTAGE 		= 3,
        AMPLFIER_OVERVOLTAGE  		= 5,
        AMPLFIER_SAFTEY 	  		= 7,
        AMPLFIER_SHORT_PROTECTION 	= 11,
        AMPLFIER_OVER_TEMP			= 13,
        AMPLFIER_ADDITIONAL_ABORT	= 15
    };

    enum ELMO_ERROR_CODES{
    ELMO_ERROR_CODE_Short_circuit = 0x2340,
    ELMO_ERROR_CODE_Under_voltage = 0x3120,
    ELMO_ERROR_CODE_AC_fail_loss_of_phase = 0x3130,
    ELMO_ERROR_CODE_Over_voltage = 0x3310,
    ELMO_ERROR_CODE_Temperature_drive_overheating  = 0x4310,
    ELMO_ERROR_CODE_Gantry_position_error = 0x5280 ,
    ELMO_ERROR_CODE_Motor_disabled_by_INHIBIT_or_ABORT = 0x5441,
    ELMO_ERROR_CODE_Motor_disabled_by_switch_additional_abort_motion  = 0x5442,
    ELMO_ERROR_CODE_RPDO_failed = 0x6300,
    ELMO_ERROR_CODE_Motor_stuck = 0x7121,
    ELMO_ERROR_CODE_Feedback_error = 0x7300,
    ELMO_ERROR_CODE_Two_digital_Hall_sensors_were_changed_at_the_same_time  = 0x7381,
    ELMO_ERROR_CODE_Commutation_process_fail_during_motor_on = 0x7382,
    ELMO_ERROR_CODE_CAN_message_lost_corrupted_or_overrun = 0x8110,
    ELMO_ERROR_CODE_Heartbeat_event = 0x8130,
    ELMO_ERROR_CODE_Recovered_from_bus_off = 0x8140 ,
    ELMO_ERROR_CODE_Attempt_to_access_a_non_configured_RPDO = 0x8210,
    ELMO_ERROR_CODE_Peak_current_has_been_exceeded_Possible_reasons_are_drive_malfunction_or_bad_tuning_of_the_current_controller = 0x8311,
    ELMO_ERROR_CODE_Speed_tracking_error = 0x8480,
    ELMO_ERROR_CODE_Speed_limit_exceeded = 0x8481,
    ELMO_ERROR_CODE_Position_tracking_error = 0x8611,
    ELMO_ERROR_CODE_Position_limit_exceeded = 0x8680,
    ELMO_ERROR_CODE_Request_by_user_program_EMCY_function = 0xFF01,
    ELMO_ERROR_CODE_IP_mode_underflow_or_Interpolation_queue_full_or_Reference_received_in_a_wrong_index_or_Bad_PVT_send_order  = 0xFF02,
    ELMO_ERROR_CODE_Failed_to_start_motor = 0xFF10,
    ELMO_ERROR_CODE_Safety_Torque_Off_in_use = 0xFF20,
    ELMO_ERROR_CODE_Gantry_Slave_Disabled = 0xFF40
    };

    enum ELMO_SW_ENUM
    {
        ELMO_SW_FAULT,
        ELMO_SW_SEND_STAGE_1,
        ELMO_SW_SEND_STAGE_2,
        ELMO_SW_MOTOR_OPERATIONAL,
        ELMO_SW_MOTOR_ON
    };


    struct sMdcu2ElmoInitUpload
    {
        unsigned int x   : 5;
        unsigned int css : 3;
        unsigned int m   : 24;
        unsigned int reserved;
    };

    struct sElmo2MdcuResponse
    {
        unsigned int s               :1;
        unsigned int e               :1;
        unsigned int n               :2;
        unsigned int x               :1;
        unsigned int scs             :3;
        unsigned int m_index         :16;
        unsigned int m_subIndex      :8;
        unsigned int data;
    };



    struct sElmoTerminalCommand
    {
        char    byte0;
        short   elmoObjectAddr;
        char    subIndex;
        union{
        float   flData;
        int     nData;
        };
    };

    struct sElmoGeneralCommand
    {
        unsigned char byte0;
		unsigned char byte1;
		unsigned char byte2;
		unsigned char byte3;
		unsigned char byte4;
		unsigned char byte5;
		unsigned char byte6;
		unsigned char byte7;
    };

    /*------------------------- Structures -------------------------*/


    struct ELMO_SYNC {                                            /* Struct 20 */
       unsigned char Counter;
    };

    struct sPowerUp
    {
        char data;//allways 0
    };

    struct StatusWord
    {
        unsigned short readyToSwitchOn           :1;
        unsigned short switchedOn                :1;
        unsigned short operationEnabled          :1;
        unsigned short fault                     :1;
        unsigned short voltageEnabled            :1;
        unsigned short quickStop                 :1;
        unsigned short switchOnDisabled          :1;
        unsigned short warning                   :1;
        unsigned short reserved                  :1;
        unsigned short remote                    :1;
        unsigned short targetReached             :1;
        unsigned short internalLimitActive       :1;
        unsigned short operationalModeSpecific   :2;
        unsigned short reserved2                 :2;

    };

    struct ControlWord
    {
        unsigned short switchOn                  :1;
        unsigned short enableVoltage             :1;
        unsigned short quikStop                  :1;
        unsigned short enableOperation           :1;
        unsigned short operationModeSpesific     :3;
        unsigned short FaultReset                :1;
        unsigned short halt                      :1;
        unsigned short reserved                  :2;
        unsigned short manufactorSpecific        :5;
    };

    struct RPDO1
    {
        ControlWord cw;
        short targetTorque;
    };

    struct TPDO1
    {
        int vx;
        StatusWord sw;
		short currentActualValue;
    };

    struct SrStatus
    {
       unsigned int AmplifierStatus              :4;
       unsigned int ServoEnabled                 :1; //0 The servo is not enabled, 1 The servo is enabled
       unsigned int RefrenceMode                 :1;//Refer to RM command
       unsigned int FaultWhileMotorEnabled       :1; //This bit is cleared. during the Motor Enable procedure.
       unsigned int HomingOrCaptureSeqIsActive   :1; // 0 - HM[1] and HF[1] are not active. 1 - HM[1] or HF[1] is active

       unsigned int ReportToProfiler             :4; //0 - No motion was selected. 1 - Profile position mode(PTP) 2 - N/A 3 - Profile Velocity mode (JV) 4 - Profile Torque mode (TC) 5 - N/A 6 - Homing mode (DS-402 only) 7 - Interpolated position mode (DS-402 only) 8 - Cyclic sync position mode (DS-402 only) 9 - Cyclic sync velocity mode (DS-402 only) 10 - Cyclic sync torque mode(DS-402 only)
       unsigned int UserProgremRunning           :1;
       unsigned int CurrentLimitOn				 :1;  //0 No Current Limit, Peak current (PL[1]) can be applied. 1 Current is limited to CL[1].
       unsigned int SafetyInput1				 :1;  //(STO_DSP)
       unsigned int SafetyInput2				 :1;  //(STO_PWM)

       unsigned int RecorderStatus 				 :2; //0 The recorder is not active. 1- Waiting for a trigger. 2 - The recorder has completed its task. Valid data is ready for uploading. 3 - Recording is now active. Data is been fetched by the drive.
       unsigned int TargetReached				 :1;
       unsigned int ShuntActive					 :1;
       unsigned int MotorOn						 :1;
       unsigned int MovementStandstill			 :1;
       unsigned int HallAHallBHallCState		 :3;
       unsigned int STODiagEroor				 :1;
       unsigned int ProfilerSwitchStoped	   	 :1;
       unsigned int PTPBufferFull			 	 :1;
       unsigned int Spare                        :4;
    };

    struct sElmoEmergency
    {
        unsigned short ErrorCode;
        unsigned short ErrorRegister : 8;
        unsigned short ElmoErrorCode : 8;
        unsigned int ErrorCodeDataField;
    };
	
    struct sElmoDataReport
    {
        float fElAmpCommand;
        float fTrAmpCommand;
        float flElVelocityStatus;
        float flTrVelocityStatus;
        bool bEnableCommand;
        int nElMotorRatedCurrent;
        int nTrMotorRatedCurrent;
        float flElAmpControlAlg;
        float flTrAmpControlAlg;
        short shElControlWord;
        short shTrControlWord;
        short shElTargetTorqueToElmo;
        short shTrTargetTorqueToElmo;

        sElmoEmergency lastElElmoEmergency;
        sElmoEmergency lastTrElmoEmergency;
    };

	struct sElmoErrorReport
	{
		int m_nElErrorArr[10] = { 0 };
		int m_nTrErrorArr[10] = { 0 };
	};




}//namespace

#pragma pack(pop)

#endif // MDCU_ELMO_ICD_H

