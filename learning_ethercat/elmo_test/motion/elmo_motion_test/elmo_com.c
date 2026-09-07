#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>
#include <sys/mman.h>
#include <fcntl.h> /*for using read write permissions flags when openning the files*/
#include <sched.h> /* for changing scehduler policy and priority*/
#include <signal.h>
#include "ethercattype.h"
#include "nicdrv.h"
#include "ethercatbase.h"
#include "ethercatmain.h"
#include "ethercatconfig.h"
#include "ethercatcoe.h"
#include "ethercatdc.h"
#include "ethercatprint.h"
#include "slave_mapping_info.h"
#include "elmo_ICD.h"
#include "elmo_device.h"
#include "params.h"
#include "joystick_eth.h"


#define NSEC_PER_SEC 1000000000

extern char IOmap[4096];
in_ELMOt elmo_inTR[12000];
in_ELMOt elmo_inEL[12000];
out_ELMOt elmo_outTR[12000];
out_ELMOt elmo_outEL[12000];


int expectedWKC;
boolean inOP;
boolean needlf;
volatile int wkc;
pthread_t joystick_thread;
int shutdown = 0;

int64 toff, gl_delta;
int DCdiff;


void sig_term_catch(int sig_num)
{
	shutdown = 1;
}

/* PI calculation to get linux time synced to DC time */
void ec_sync(int64 reftime, int64 cycletime , int64 *offsettime)
{
   static int64 integral = 0;
   int64 delta;
   /* set linux sync point 50us later than DC sync, just as example */
   delta = (reftime + 50000) % cycletime;
   if(delta> (cycletime / 2)) { delta= delta - cycletime; }
   if(delta>0){ integral++; }
   if(delta<0){ integral--; }
   *offsettime = -(delta / 100) - (integral / 20);
   gl_delta = delta;
}

/* add ns to timespec */
void add_timespec(struct timespec *ts, int64 addtime)
{
   int64 sec, nsec;

   nsec = addtime % NSEC_PER_SEC;
   sec = (addtime - nsec) / NSEC_PER_SEC;
   ts->tv_sec += sec;
   ts->tv_nsec += nsec;
   if ( ts->tv_nsec > NSEC_PER_SEC )
   {
      nsec = ts->tv_nsec % NSEC_PER_SEC;
      ts->tv_sec += (ts->tv_nsec - nsec) / NSEC_PER_SEC;
      ts->tv_nsec = nsec;
   }
}


check_cyclic_mode()
{
	int ret = 0, l;
	uint16_t sync_mode;
	uint32_t sync0_cycle_time;
	uint32_t sync1_cycle_time;
	uint32_t minimum_cycle_time;
	uint32_t minimum_delay_time;
	uint32_t shift_time;
	uint16_t cycle_time_too_small;
	uint16_t shift_time_too_small;
	uint16_t sync_type_supported;
	uint16_t sm_event_missed_counter;
	uint32_t calc_time_needed;
	uint16 sm_output;
	uint16_t extrapol_cycles;
	uint8_t interpol_time_units;
	uint8_t interpol_time_inx;

	sm_output = 0;
	l = sizeof(sync_mode);
	printf("************\n");
	ret += ec_SDOread(1, 0x1c32, 0x01, FALSE, &l, &sync_mode, EC_TIMEOUTRXM);
	l = sizeof(sync0_cycle_time);
	ret += ec_SDOread(1, 0x1c32, 0x02, FALSE, &l, &sync0_cycle_time, EC_TIMEOUTRXM);
	l = sizeof(sync1_cycle_time);
	ret += ec_SDOread(1, 0x1c33, 0x02, FALSE, &l, &sync1_cycle_time, EC_TIMEOUTRXM);
	l = sizeof(shift_time);
	ret += ec_SDOread(1, 0x1c32, 0x03, FALSE, &l, &shift_time, EC_TIMEOUTRXM);
	l = sizeof(sync_type_supported);
	ret += ec_SDOread(1, 0x1c32, 0x04, FALSE, &l, &sync_type_supported, EC_TIMEOUTRXM);
	l = sizeof(minimum_cycle_time);
	ret += ec_SDOread(1, 0x1c32, 0x05, FALSE, &l, &minimum_cycle_time, EC_TIMEOUTRXM);
	l = sizeof(calc_time_needed);
	ret += ec_SDOread(1, 0x1c32, 0x06, FALSE, &l, &calc_time_needed, EC_TIMEOUTRXM);
	l = sizeof(minimum_delay_time);
	ret += ec_SDOread(1, 0x1c32, 0x07, FALSE, &l, &minimum_delay_time, EC_TIMEOUTRXM);
		l = sizeof(sm_event_missed_counter);
	ret += ec_SDOread(1, 0x1c32, 0x0b, FALSE, &l, &sm_event_missed_counter, EC_TIMEOUTRXM);
	l = sizeof(cycle_time_too_small);
	ret += ec_SDOread(1, 0x1c32, 0x0c, FALSE, &l, &cycle_time_too_small, EC_TIMEOUTRXM);
	l = sizeof(shift_time_too_small);
	ret += ec_SDOread(1, 0x1c32, 0x0d, FALSE, &l, &shift_time_too_small, EC_TIMEOUTRXM);
	l = sizeof(extrapol_cycles);
	ret += ec_SDOread(1, 0x3675, 0x00, FALSE, &l, &extrapol_cycles, EC_TIMEOUTRXM);
	l = sizeof(interpol_time_units);
	ret += ec_SDOread(1, 0x60c2, 0x01, FALSE, &l, &interpol_time_units, EC_TIMEOUTRXM);
	interpol_time_units = 2;
		//	ret += ec_SDOwrite(1, 0x60c2, 0x1, FALSE, sizeof(interpol_time_units), &interpol_time_units,  EC_TIMEOUTSAFE);
	interpol_time_inx = 253;
	//ret += ec_SDOwrite(1, 0x60c2, 0x2, FALSE, sizeof(interpol_time_inx), &interpol_time_inx,  EC_TIMEOUTSAFE);
	l = sizeof(interpol_time_inx);
	ret += ec_SDOread(1, 0x60c2, 0x02, FALSE, &l, &interpol_time_inx, EC_TIMEOUTRXM);


	printf("PDO syncmode %02x, sync0_cycle time %d ns, (min %d), sync1_cycle time %d ns,\n\\
			shift_time  %d, sync_type_supported 0x%x, calc_time_needed %d, min_delay_time %d,\n\\
			sm_event_missed %d, cycle too small %d, shift_time_too_small %d, get cycle time %d\n", sync_mode, sync0_cycle_time, minimum_cycle_time, sync1_cycle_time,
			shift_time, sync_type_supported, calc_time_needed, minimum_delay_time, sm_event_missed_counter, cycle_time_too_small, shift_time_too_small);
	printf("********* extrapolation cycles for time dependent motion modes  %d*****\n", extrapol_cycles);
	printf("********* interpol_time_units  %d*****\n", interpol_time_units);
	printf("********* interpol_time_inx  %d*****\n", interpol_time_inx);
}


int elmo_setup(uint16 slave)
{
	int retval;

	unsigned char mode_of_oper = 0;
	int l;
	retval = 0;
	/* Map velocity PDO assignment via Complete Access.*/
	/* SM2 (Outputs) PDO assignments Master --> Slave*/
	/* SM2 - SYNC Manager 2 for RxPDO */
	/* Elmo define function group for RxPDO and TxPDO */
	/* Moshe Bonen change default value of these PDO indexes but i think it's because the 8 data bytes limit!*/
	/* This defines inputs and outputs structs in data_function.h file */
	/* The first element in the array is the number of objects in this PDO */
	/* 0x160a is PDO index of Control Word */
	/* 0x160c is PDO index of Target Torque */
	/* I think we could have choose function group 0x1602 that represent Torque instead of config the two elements 0x160a and 0x160c separately*/
	uint16 map_1c12[3] = {0x0002, 0x160a, 0x161c};
	/* SM3 (Inputs) PDO assignments Slave --> Master*/
	/* SM3 - SYNC Manager 3 for TxPDO */
	/* The first element in the array is the number of objects in this PDO */
	/* 0x1a0f is PDO index of 32 bit Velocity sensor actual value */
	/* 0x1a0a is PDO index of 16 bit Status word */
	/* 0x1a1f is PDO index of 16 bit Current actual value */
	uint16 map_1c13[5] = {0x0004, 0x1a0a, 0x1a10 ,0x1a11, 0x1a1f};
	// check_cyclic_mode();
	/* Configure mode of operation */
	mode_of_oper = 9;	//velocity profiled mode!
	retval += ec_SDOwrite(slave, 0x6060, 0x0, FALSE, sizeof(mode_of_oper), &mode_of_oper,  EC_TIMEOUTSAFE);

	mode_of_oper = 0;
		l = sizeof(mode_of_oper);
		/* Read mode of operation */
		retval += ec_SDOread(slave, 0x6061, 0x0, FALSE, &l, &mode_of_oper,  EC_TIMEOUTSAFE);
		printf("Configured mode of operation to ****  %d *****\n", mode_of_oper);

	/* Config SM2 (Outputs) PDO assignments (Master --> Slave) with the help of SDO write to Dictionary Object of the slave we get as input*/
	/* 0x1c12 is the index in the Dictionary Object that store the PDO assignment of the outputs that we choose to send to the slave in the cyclic loop*/
	/* I think the limit is 30 x uint16 outputs from Elmo docs. 30 is the number of sub-indexs*/
	retval += ec_SDOwrite(slave, 0x1c12, 0x0, 1, sizeof(map_1c12), &map_1c12,  EC_TIMEOUTSAFE);
	/* Config SM3 (Inputs) PDO assignments (Slave --> Master) with the help of SDO write to Dictionary Object of the slave we get as input*/
	/* 0x1c13 is the index in the Dictionary Object that store the PDO assignment of the input that we choose to receive from the slave in the cyclic loop*/
	/* I think the limit is 35 x uint16 inputs from Elmo docs. 35 is the number of sub-indexs*/
	retval += ec_SDOwrite(slave, 0x1c13, 0x0, 1, sizeof(map_1c13), &map_1c13,  EC_TIMEOUTSAFE);
	printf("Elmo slave %d set, retval = %d\n", slave, retval);
	return 1;
}




int main()
{

	  int dev_type = 0;
	  int dev_type_size = 4;
	  int data = 0;
	  int error = 0;
	  float tcValue = 0.0;
	  unsigned int wc = 0;
	  int res = 0;
	  int i = 0, j;
	  int oloop, iloop, chk;
	  needlf = 0;
	  in_ELMOt *in_ELMO_EL, *in_ELMO_TR;
	  int word_size = 2;
	  StatusWord status;
	  Control_Word cw;
	  int16 output_buf[12001];
	  /****** diagnostic params *********/
	  struct timespec start, end, now, prev_start;
	  unsigned long max_execution = 0, min_execution = 100000000, latency_time, max_latency = 0, min_latency = 100000000;
	  unsigned long execution_time;
	  int fd;
	  int latency_target_value = 0;
	  int max_cycle_time = 0;
	  int prev_diff;
	  int once = 0;

	  struct timespec   ts, tleft;
	  int ht;
	  int temp;


	  struct sched_param param;
	  param.sched_priority = 90;


	  mlockall(MCL_CURRENT|MCL_FUTURE);

	  sched_setscheduler(getpid(), SCHED_FIFO, &param);

	  signal(SIGINT, sig_term_catch);
	  signal(SIGTERM, sig_term_catch);

	  /***** Prevent the cpu from entering IDLE state *****/
	  fd = open("/dev/cpu_dma_latency", O_RDWR);
	  if(fd < 0) return 0;
	  write(fd, &latency_target_value, 4);


	  printf("Starting elmo communication\n");

	  /**** open control output parameters file for the motors ****/
	  res = open_out_file("/mnt/storageDevice/projects/learning_ethercat/sinus.txt", output_buf, 12001);
	  if(!res)
	  {
		  printf("failed open file\n");
		  return 1;
	  }

	  /****** start joystick thread ***********/
	//  pthread_create(&joystick_thread, NULL, run, NULL);

	   /* initialise SOEM, bind socket to ifname */
	   if (ec_init("enx00116b663e42"))
	   {
	      printf("ec_init on eth0 succeeded.\n");


	      /* find and auto-config slaves */
	/*      if ( ec_config(0, &IOmap) > 0 )
	      {
	         ec_configdc();
	      }
	  */
	      if ( ec_config_init(0) > 0 )
	         {
	            printf("%d slaves found and configured.\n",ec_slavecount);

	  	      /*link slave specific setup to preop->safeop hook. We do PDO mapping and can set some parameters with SDO messages for example: max motor current in mA*/
	  	      /* ec_config function will call this function */
	  	     ec_slave[1].PO2SOconfig = elmo_setup;
	  	     ec_slave[2].PO2SOconfig = elmo_setup;

	            ec_config_map(&IOmap);

	            /*Locate Distributed Clocks slaves, measure propagation delays*/

	            ec_configdc();

	            /* print PDO's mapping */
	            si_map_sdo(1);
	            si_map_sdo(2);

	            //check_cyclic_mode();
	            printf("Slaves mapped, state to SAFE_OP.\n");
	              /* wait for all slaves to reach SAFE_OP state */
	              ec_statecheck(0, EC_STATE_SAFE_OP,  EC_TIMEOUTSTATE * 4);

	              /********* ec_slave[0] is reserved for the master. Structure gets filled
	                 in by the configuration function ec_config().*******/
	              /******* Importent: ec_slave[0].Oloop is the total output bytes that we send to all the slaves together!
	               * ec_slave[0].iloop is the total input bytes we get from all the slaves together!
	               */
	              oloop = ec_slave[0].Obytes;
	              printf("elmo total number of output bytes is %d\n", oloop);
	              if ((oloop == 0) && (ec_slave[0].Obits > 0)) oloop = 1;
	              if (oloop > 8) oloop = 8;
	              iloop = ec_slave[0].Ibytes;
	              printf("elmo total number of input bytes is %d\n", iloop);
	              if ((iloop == 0) && (ec_slave[0].Ibits > 0)) iloop = 1;
	              if (iloop > 8) iloop = 8;

	              printf("segments : %d : %d %d %d %d\n",ec_group[0].nsegments ,ec_group[0].IOsegment[0],ec_group[0].IOsegment[1],ec_group[0].IOsegment[2],ec_group[0].IOsegment[3]);

	              printf("Request operational state for all slaves\n");
	              expectedWKC = (ec_group[0].outputsWKC * 2) + ec_group[0].inputsWKC;
	              printf("Calculated workcounter %d\n", expectedWKC);
	              ec_slave[0].state = EC_STATE_OPERATIONAL;

	              /*****************************/
	              wc = ec_SDOread(1, 0x6075, 0x0, 0, &dev_type_size, &data, EC_TIMEOUTRXM);
	       	   	  printf("wc after read is %d\n", wc);
	       	   	  printf("Motor 1 rate value is %dmAmp\n", data);
		          wc = ec_SDOread(2, 0x6075, 0x0, 0, &dev_type_size, &data, EC_TIMEOUTRXM);
		       	  printf("wc after read is %d\n", wc);
		       	  printf("Motor 2 rate value is %dmAmp\n", data);
              	  wc = ec_SDOread(1, 0x6041, 0x0, 0, &word_size , &status, EC_TIMEOUTRXM);
              	  printf("wc after read is %d\n", wc);
              	  printf("status word value is 0x%x\n", status);
                 /****************************/

	              /* send one minimum_cycle_timevalid process data to make outputs in slaves happy*/

	              ec_send_processdata();
	              ec_receive_processdata(EC_TIMEOUTRET);
	              /* request OP state for all slaves */
	              ec_writestate(0);
	              chk = 40;
	              /* wait for all slaves to reach OP state */
	              do
	              {
	                 ec_send_processdata();
	                 ec_receive_processdata(EC_TIMEOUTRET);
	                 ec_statecheck(0, EC_STATE_OPERATIONAL, 50000);
	              }
	              while (chk-- && (ec_slave[0].state != EC_STATE_OPERATIONAL));


	              if (ec_slave[0].state == EC_STATE_OPERATIONAL )
	              {

	                 printf("Operational state reached for all slaves.\n");
	                 inOP = 1;
	                     /* Change states in the state machine of the Elmo motors.
	                      * You can find description in Gold Line DS-402 Implementation Guide doc about the states.*/
	                 	 /* The final state is ELMO MOTOR OPERATIONAL.*/
	                 /********************************/
	                 //check_cyclic_mode();
	                 startMotorTimerFunction(1);
	                 startMotorTimerFunction(2);
	              //   ec_dcsync0(1, TRUE, 1000000, 0); // SYNC0 on slave 1
	        /*     for(i = 1; i < 10; i++)
	             {
	            	 update_el_outputs(i);
	            	 ec_send_processdata();
					 ec_receive_processdata(EC_TIMEOUTRET);
					 usleep(2000);

	             }
	             */

		             clock_gettime(CLOCK_MONOTONIC, &ts);
		             ht = (ts.tv_nsec / 1000000) + 1; /* round to nearest ms */
		             ts.tv_nsec = ht * 1000000;
		             toff = 0;
	                 for(i = 0; i <= 12000; i++)
	                 {
	                	/**** calc clock for the next 1ms *****/
/*	             		start.tv_nsec = start.tv_nsec + 1000000;
	             		if(start.tv_nsec >= NSEC_PER_SEC)
	             		{
	             			start.tv_nsec -= NSEC_PER_SEC;
	             			start.tv_sec++;
	             		}
*/
/*
	                		 if(i==10)
	                	 	 {
								for(j=0; j<20; j++)
								 {
									 ec_send_processdata();
								     ec_receive_processdata(EC_TIMEOUTRET);
								     usleep(4000);
								 }
								  clock_gettime(CLOCK_MONOTONIC, &ts);
										             ht = (ts.tv_nsec / 1000000) + 1;
										             ts.tv_nsec = ht * 1000000;
	                	 	 }

	                	 	 if(i==20)
	                	 	 {
								for(j=0; j<20; j++)
								 {
									 ec_send_processdata();
								     ec_receive_processdata(EC_TIMEOUTRET);
								     usleep(4000);
								 }
								  clock_gettime(CLOCK_MONOTONIC, &ts);
										             ht = (ts.tv_nsec / 1000000) + 1;
										             ts.tv_nsec = ht * 1000000;
	                	 	 }
*/
	                	 /* calculate next cycle start */
	                	 add_timespec(&ts, 1000000 + toff);
	                	 /* wait to cycle start */
	                	 clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL);
						//clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &start, NULL);
						clock_gettime(CLOCK_MONOTONIC, &end);


						if((end.tv_sec * NSEC_PER_SEC  + end.tv_nsec) > (ts.tv_sec * NSEC_PER_SEC +  ts.tv_nsec))
						{
							latency_time = (end.tv_sec * NSEC_PER_SEC  + end.tv_nsec)  - (ts.tv_sec * NSEC_PER_SEC +  ts.tv_nsec);
						}
						else
						{
							printf("I woke up early\n");
						}

						if(latency_time > max_latency) max_latency = latency_time;
						if(latency_time < min_latency) min_latency = latency_time;

	             		/**** update outputs for motors from file*****/
	                	update_el_outputs(output_buf[i]);
	                	update_tr_outputs(output_buf[i]);
	                //	update_outputs_telemetry_buf(elmo_outTR + i, elmo_outEL + i);
		             		/**** update outputs for motors from joystick*****/
//		                	update_el_outputs(get_el_torque_val());
//		                	update_tr_outputs(get_tr_torque_val());
	                	/****** start measure roundtrip time *****/
	            		clock_gettime(CLOCK_MONOTONIC, &start);
	                    ec_send_processdata();
	                    wkc = ec_receive_processdata(EC_TIMEOUTRET);

	                         if(wkc >= expectedWKC)
	                         {
	                     		clock_gettime(CLOCK_MONOTONIC, &end);
	                     		execution_time = (end.tv_sec * NSEC_PER_SEC  + end.tv_nsec)  - (start.tv_sec * NSEC_PER_SEC +  start.tv_nsec);
	                     		if(execution_time > max_execution) max_execution = execution_time;
	                     		if(execution_time < min_execution) min_execution = execution_time;
	  						  /* calulate toff to get linux time and DC synced */
	  						  ec_sync(ec_DCtime, 1000000, &toff);
	  						temp = ec_DCtime % 1000000;
	  						//if(temp > 975000 || temp < 925000) printf("******* offset is %d\n", temp);
	  						update_inputs_telemetry_buf(elmo_inTR + i, elmo_inEL + i);
	  						 // printf("ec_DCtime is %ld, toff is %ld\n", ec_DCtime, toff);

/*
	                             for(j = 0 ; j < oloop; j++)
	                             {
	                                 printf(" %2.2x", *(ec_slave[1].outputs + j));
	                             }

	                             printf(" I:");
	                             for(j = 0 ; j < iloop; j++)
	                             {
	                                 printf(" %2.2x", *(ec_slave[1].inputs + j));
	                             }
	                             printf(" T:%lld\r",ec_DCtime);
*/
	                             needlf = 1;

	                         }
	                        /***** sleep for 1ms ****/
//	                     	clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &start, NULL);
	                     	/***** read clock after wakeup *****/
//	                     	clock_gettime(CLOCK_MONOTONIC, &end);
//	                     	execution_time = (end.tv_sec * NSEC_PER_SEC  + end.tv_nsec)  - (now.tv_sec * NSEC_PER_SEC +  now.tv_nsec);
	                        // usleep(2000);

	                     }
	                     inOP = 0;
	                 }
	                 else
	                 {
	                     printf("Not all slaves reached operational state.\n");
	                     ec_readstate();
	                     for(i = 1; i<=ec_slavecount ; i++)
	                     {
	                         if(ec_slave[i].state != EC_STATE_OPERATIONAL)
	                         {
	                             printf("Slave %d State=0x%2.2x StatusCode=0x%4.4x : %s\n",
	                                 i, ec_slave[i].state, ec_slave[i].ALstatuscode, ec_ALstatuscode2string(ec_slave[i].ALstatuscode));
	                         }
	                     }
	                 }
	              //	 ec_dcsync0(1, FALSE, 1000000, 0); // SYNC0 on slave 1
	                // check_cyclic_mode();
	                 printf("\nRequest init state for all slaves\n");
	                 ec_slave[0].state = EC_STATE_INIT;
	                 /* request INIT state for all slaves */
	                 ec_writestate(0);
	             }
	             else
	             {
	                 printf("No slaves found!\n");
	             }
	         }
	   printf("** max roundtrip time is %ld nanoSec, min execution time is %ld nanoSec ****\n", max_execution, min_execution);
	   printf("** max latency time is %ld nanoSec, min latency time is %ld nanoSec ****\n", max_latency, min_latency);
	   printf("** max cycle time is %ld nanoSec ****\n", max_cycle_time);
	   ec_close();
	   create_log_file(elmo_inTR, elmo_inEL, elmo_outTR, elmo_outEL, 12000);
	//   pthread_cancel(joystick_thread);
	   return 0;
}

