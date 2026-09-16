#include <stdio.h>
#include <string.h>
#include <unistd.h>

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
/* The per-topology output helpers that used to live here - update_el_outputs(),
 * update_tr_outputs(), update_platinum_outputs() - are gone. They overlaid the
 * fixed out_ELMOt struct on ec_slave[ELMO_EL].outputs, which hard-coded both
 * the vendor's PDO layout and which bus position was which axis, and forced the
 * controlword permanently to "enable operation" with no state machine at all.
 *
 * Their replacement is src/drive/axis.c: axis_read() / axis_enable_step() /
 * axis_write_setpoint(), driven by offsets resolved from the configured PDO map
 * at start-up. Likewise ParseStatusWord() and the blocking SDO bring-up in
 * startMotorTimerFunction() are now cia402_state() and the in-loop ladder.
 */

#ifdef LOG
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
	sprintf(line_buf, "Record_Num\tStatus_word\tMode\tPos_actual_val\tVelocity_demand\
\tVelocity_actual\tTorque_demand\tTorque_actual\tControl_effort\tDigital_inputs\tCurrent_actual_val\n");
	fprintf(fTR, line_buf);
	fprintf(fEL, line_buf);

	for(i=0; i <= buf_size; i++)
	{
		sprintf(line_buf, "%d\t\t%d\t\t%d\t\t%d\t\t%d\t\t%d\t\t%d\t\t%d\t\t%d\t\t%d\t\t%d\n",i+1, elmo_inTR[i].status_word,
						elmo_inTR[i].mode_oper,elmo_inTR[i].position_actual, elmo_inTR[i].velocity_demand,
						elmo_inTR[i].velocity_actual,elmo_inTR[i].torque_demand, elmo_inTR[i].torque_actual,
						elmo_inTR[i].control_effort, elmo_inTR[i].digital_inputs, elmo_inTR[i].current_actual_value);
		fprintf(fTR, line_buf);
	}
//	fflush(fTR);
	for(i=0; i <= buf_size; i++)
	{
		sprintf(line_buf, "%d\t\t%d\t\t%d\t\t%d\t\t%d\t\t%d\t\t%d\t\t%d\t\t%d\t\t%d\t\t%d\n",i+1, elmo_inEL[i].status_word,
						elmo_inEL[i].mode_oper,elmo_inEL[i].position_actual, elmo_inEL[i].velocity_demand,
						elmo_inEL[i].velocity_actual,elmo_inEL[i].torque_demand, elmo_inEL[i].torque_actual,
						elmo_inEL[i].control_effort, elmo_inEL[i].digital_inputs, elmo_inEL[i].current_actual_value);
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

#endif
