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

extern shutdown;

void shutdown_func()
{
	printf("\nRequest init state for all slaves\n");
	/* request INIT state for all slaves */
	ec_slave[0].state = EC_STATE_INIT;
	ec_writestate(0);
	ec_close();
	//	pthread_cancel(joystick_thread);
	exit(0);
}


void update_el_outputs(int16 torque_val)
{
	out_ELMOt *out_ELMO_EL;
	/*Here we get pointer to iomap - slave I/O pointers - pointer for SM2 outputs*/
	out_ELMO_EL = (out_ELMOt*)ec_slave[ELMO_EL].outputs;
	if(out_ELMO_EL == NULL) printf("******* null pointer\n");
	out_ELMO_EL->control_word.enableOperation = 1;
	out_ELMO_EL->control_word.switchOn = 1;
	out_ELMO_EL->control_word.quickStop = 1;
	out_ELMO_EL->control_word.enableVoltage= 1;
	out_ELMO_EL->target_velocity = torque_val;//calcTargetTorque(trqcm);
}

void update_tr_outputs(int16 torque_val)
{
	out_ELMOt *out_ELMO_TR;
	/*Here we get pointer to iomap - slave I/O pointers - pointer for SM2 outputs*/
	out_ELMO_TR = (out_ELMOt*)ec_slave[ELMO_TR].outputs;
	if(out_ELMO_TR == NULL) printf("******* null pointer\n");
	out_ELMO_TR->control_word.enableOperation = 1;
	out_ELMO_TR->control_word.switchOn = 1;
	out_ELMO_TR->control_word.quickStop = 1;
	out_ELMO_TR->control_word.enableVoltage = 1;
	out_ELMO_TR->target_velocity= torque_val;//calcTargetTorque(trqcm);
	//out_ELMO_TR->target_velocity = 0x60;
}


void update_inputs_telemetry_buf(in_ELMOt* elmo_inTR, in_ELMOt* elmo_inEL)
{
	*elmo_inTR = *((in_ELMOt*)ec_slave[ELMO_TR].inputs);
	*elmo_inEL = *((in_ELMOt*)ec_slave[ELMO_EL].inputs);
}

void update_outputs_telemetry_buf(out_ELMOt* elmo_outTR, out_ELMOt* elmo_outEL)
{
	*elmo_outTR = *((out_ELMOt*)ec_slave[ELMO_TR].outputs);
	*elmo_outEL = *((out_ELMOt*)ec_slave[ELMO_EL].outputs);
}


ELMO_SW_ENUM ParseStatusWord(StatusWord sw){
     if(sw.fault)
     {
    	 printf("*** Fault field in status word is 1\n");
    	 //I think i can get fault reason in an emergency message. Object 0x1003
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
		if(shutdown) shutdown_func();
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
			   cw.faultReset = 1;
			   wc = ec_SDOwrite( slave_no, 0x6040, 0x0, 0, word_size, (void *)&cw,  EC_TIMEOUTTXM);
			   printf("wc after write is %d\n", wc);
			   printf("sent Fault Reset command\n");
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


void create_log_file(in_ELMOt* elmo_inTR, in_ELMOt* elmo_inEL, out_ELMOt* elmo_outTR,
													out_ELMOt* elmo_outEL, int buf_size)
{
	FILE *fTR, *fEL, *fTR_out, *fEL_out;
	int i=0;
	unsigned char line_buf[300];
	fTR = fopen("elmoTR_in.log", "w+");
	if (fTR == NULL) {
		printf("Something is wrong\n");
	}
	fEL = fopen("elmoEL_in.log", "w+");
	if (fTR == NULL) {
		printf("Something is wrong\n");
	}
	fTR_out = fopen("elmoTR_out.log", "w+");
	if (fTR == NULL) {
		printf("Something is wrong\n");
	}
	fEL_out = fopen("elmoEL_out.log", "w+");
	if (fTR == NULL) {
		printf("Something is wrong\n");
	}

	/**********************  Inputs files *****************/
	//Write fields name to the first line in file.
	sprintf(line_buf, "Record_Num\tStatus_word\tVelocity_demand\
\tVelocity_actual\tCurrent_actual_val\n");
	fprintf(fTR, line_buf);
	fprintf(fEL, line_buf);

	for(i=0; i <= buf_size; i++)
	{
		sprintf(line_buf, "%d\t\t%d\t\t%d\t\t%d\t\t%d\n",i+1, elmo_inTR[i].status_word,
						elmo_inTR[i].velocity_demand,
						elmo_inTR[i].velocity_actual,elmo_inTR[i].currentActualValue);
		fprintf(fTR, line_buf);
	}
//	fflush(fTR);
	for(i=0; i <= buf_size; i++)
	{
		sprintf(line_buf, "%d\t\t%d\t\t%d\t\t%d\t\t%d\n",i+1, elmo_inEL[i].status_word,
						elmo_inEL[i].velocity_demand,
						elmo_inEL[i].velocity_actual,elmo_inEL[i].currentActualValue);
		fprintf(fEL, line_buf);
	}
//	fflush(fEL);

	/**********************  Outpus files *****************/
	sprintf(line_buf, "Record_Num\tcontrol_word\ttarget_velocity\n");
	fprintf(fTR_out, line_buf);
	fprintf(fEL_out, line_buf);

	for(i=0; i <= buf_size; i++)
	{
		sprintf(line_buf, "%d\t\t%d\t\t%d\n",i+1, elmo_outTR[i].control_word,
						elmo_outTR[i].target_velocity);
		fprintf(fTR_out, line_buf);
	}

//	fflush(fTR_out);

	for(i=0; i <= buf_size; i++)
	{
		sprintf(line_buf, "%d\t\t%d\t\t%d\n",i+1, elmo_outEL[i].control_word,
						elmo_outEL[i].target_velocity);
		fprintf(fEL_out, line_buf);
	}

//	fflush(fEL_out);
	fdatasync(fEL);
	fdatasync(fTR);
	fdatasync(fEL_out);
	fdatasync(fTR_out);
	sync();

	fclose(fTR);
	fclose(fEL);
	fclose(fTR_out);
	fclose(fEL_out);
}

