#include <stdio.h>
#include <string.h>

#include "ethercattype.h"
#include "nicdrv.h"
#include "ethercatbase.h"
#include "ethercatmain.h"
#include "ethercatconfig.h"
#include "ethercatcoe.h"
#include "ethercatdc.h"
#include "ethercatprint.h"
#include "data_functions.h"
#include "elmo_ICD.h"


void update_el_outputs(int16 torque_val)
{
	float trqcm= 1.0;
	out_ELMOt *out_ELMO_EL;
	/*Here we get pointer to iomap - slave I/O pointers - pointer for SM2 outputs*/
	out_ELMO_EL = (out_ELMOt*)ec_slave[ELMO_EL].outputs;
	out_ELMO_EL->control_word.enableOperation = 1;
	out_ELMO_EL->control_word.switchOn = 1;
	out_ELMO_EL->control_word.quickStop = 1;
	out_ELMO_EL->control_word.enableVoltage= 1;
	out_ELMO_EL->target_torque = torque_val;//calcTargetTorque(trqcm);
}

void update_tr_outputs(int16 torque_val)
{
	float trqcm= 1.0;
	out_ELMOt *out_ELMO_TR;
	/*Here we get pointer to iomap - slave I/O pointers - pointer for SM2 outputs*/
	out_ELMO_TR = (out_ELMOt*)ec_slave[ELMO_TR].outputs;
	out_ELMO_TR->control_word.enableOperation = 1;
	out_ELMO_TR->control_word.switchOn = 1;
	out_ELMO_TR->control_word.quickStop = 1;
	out_ELMO_TR->control_word.enableVoltage = 1;
	out_ELMO_TR->target_torque = torque_val;//calcTargetTorque(trqcm);
	//out_ELMO_TR->target_velocity = 0x60;
}


ELMO_SW_ENUM ParseStatusWord(StatusWord sw){
     if(sw.fault)
     {
         return ELMO_SW_FAULT;
     }
     else if(sw.switchOnDisabled && !(sw.readyToSwitchOn))//240 -- > 221
     {
         return ELMO_SW_SEND_STAGE_1;
     }
     else if(sw.readyToSwitchOn && sw.quickStop && !(sw.switchedOn))//221 --> 223
     {
         return ELMO_SW_SEND_STAGE_2;
     }
     else if(sw.readyToSwitchOn && sw.quickStop && sw.switchedOn && !(sw.operationEnabled) /*&& sw.voltageEnabled*/)
     {
         return ELMO_SW_MOTOR_ON;
     }
     else if(sw.readyToSwitchOn && sw.quickStop && sw.switchedOn && sw.operationEnabled && sw.voltageEnabled)
     {
         return ELMO_SW_MOTOR_OPERATIONAL;
     }
	 return ELMO_SW_FAULT;
 }


void startMotorTimerFunction(uint16 slave_no)
{
	int word_size = 2;
	StatusWord status;
	Control_Word cw;
	int wc = 0;
	while(1)
	{
		memset(&status, 0, sizeof(status));
		memset(&cw, 0, sizeof(status));
	   //Let's read status word.
	   wc = ec_SDOread(slave_no, 0x6041, 0x0, 0, &word_size , &status, EC_TIMEOUTRXM);
	   printf("wc after read is %d\n", wc);
	   printf("status word value is 0x%x\n", status);
	   ELMO_SW_ENUM swResult = ParseStatusWord(status);
   	   printf("********** status is %d\n", swResult);
	   switch(swResult)
	   {
		case ELMO_SW_FAULT:
		   {
//			   SendResetErrorsCommandEl();
//			   SendElSWRequest();
			   break;
		   }

		case ELMO_SW_SEND_STAGE_1: //240
		   {
			   cw.enableVoltage = 1;
			   cw.quickStop = 1;
			   wc = ec_SDOwrite( slave_no, 0x6040, 0x0, 0, word_size, (void *)&cw,  EC_TIMEOUTTXM);
			   printf("wc after write is %d\n", wc);
			   printf("sent El ELMO_SW_SEND_STAGE_1\n");
		   break;
		   }
	   case ELMO_SW_SEND_STAGE_2://221 //223
		  {
			  cw.switchOn = 1;
			  cw.enableVoltage = 1;
			  cw.quickStop = 1;
			   wc = ec_SDOwrite( slave_no, 0x6040, 0x0, 0, word_size, (void *)&cw,  EC_TIMEOUTTXM);
			   printf("wc after write is %d\n", wc);
		  printf("sent El ELMO_SW_SEND_STAGE_2\n");
		  break;
		  }
	   case ELMO_SW_MOTOR_ON:
		  {
		  cw.switchOn = 1;
		  cw.enableVoltage = 1;
		  cw.quickStop = 1;
		  cw.enableOperation = 1;
		   wc = ec_SDOwrite( slave_no, 0x6040, 0x0, 0, word_size, (void *)&cw,  EC_TIMEOUTTXM);
		   printf("wc after write is %d\n", wc);
		  printf("sent El ELMO_SW_MOTOR_ON\n");
		  break;
		  }
		case ELMO_SW_MOTOR_OPERATIONAL:
		   {
/*		   m_bIsElMotorOperational = true;

		   //Enable operation
		   m_ElRPDO1.cw.switchOn = 1;
		   m_ElRPDO1.cw.enableVoltage = 1;
		   m_ElRPDO1.cw.quikStop = 1;
		   m_ElRPDO1.cw.enableOperation = 1;
		   m_pTopicDispatcher->StopTimer(m_ElStartMotorTimerId);
		   m_bIsElMotorOk = true;
		   m_bStepsNotAllowd = false;
		   rsiLOG(DEBUG_LEVEL) <<"sent El ELMO_SW_MOTOR_OPERATIONAL";*/
		   break;
		   }
	   }//switch
	   if(ELMO_SW_MOTOR_OPERATIONAL == swResult) return;
	}
}

