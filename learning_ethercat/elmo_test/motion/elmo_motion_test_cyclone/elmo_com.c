#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/mman.h>
#include <fcntl.h> /*for using read write permissions flags when openning the files*/
#include <sched.h> /* for changing scehduler policy and priority*/

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

int expectedWKC;
volatile int wkc;



int elmo_setup(uint16 slave)
{
	int retval;


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
	uint16 map_1c12[3] = {0x0002, 0x160a, 0x160c};
	/* SM3 (Inputs) PDO assignments Slave --> Master*/
	/* SM3 - SYNC Manager 3 for TxPDO */
	/* The first element in the array is the number of objects in this PDO */
	/* 0x1a0f is PDO index of 32 bit Velocity sensor actual value */
	/* 0x1a0a is PDO index of 16 bit Status word */
	/* 0x1a1f is PDO index of 16 bit Current actual value */
	uint16 map_1c13[4] = {0x0003, 0x1a0F, 0x1a0a, 0x1a1f};

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


void tcCommand(int slave_num)
{
	  int dev_type = 0;
	  int dev_type_size = 4;
	  int data = 0;
	  int status = 0;
	  int error = 0;
	  float tcValue = 0.0;
	  unsigned int wc = 0;
	   data = 0;
	   wc = ec_SDOwrite( slave_num, 0x3146, 0x1, 0, 4, (void *)&data,  EC_TIMEOUTTXM);
	   printf("wc after write is %d\n", wc);
	   //enter unit mode 1 - Torque control mode
	   data = 1;
	   wc = ec_SDOwrite( slave_num, 0x3214, 0x1, 0, 4, (void *)&data,  EC_TIMEOUTTXM);
	   printf("wc after write is %d\n", wc);
	   usleep(20000);
	   //Let's read status register.
	   wc = ec_SDOread( slave_num, 0x31e5, 0x1, 0, &dev_type_size, &status, EC_TIMEOUTRXM);
	   printf("wc after read is %d\n", wc);
	   printf("status register value is 0x%x\n", status);

	   //enter motor enable mode
	   data = 1;
	   wc = ec_SDOwrite( slave_num, 0x3146, 0x1, 0, 4, (void *)&data,  EC_TIMEOUTTXM);
	   printf("wc after write is %d\n", wc);

	   data = 0;
	   //Let's read status register.
	   wc = ec_SDOread( slave_num, 0x31e5, 0x1, 0, &dev_type_size, &status, EC_TIMEOUTRXM);
	  printf("wc after read is %d\n", wc);
	  printf("status register value is 0x%x\n", status);

	  //tc command of 1.0 Amp
	   tcValue = 1.0;
	   wc = ec_SDOwrite( slave_num, 0x31f0, 0x1, 0, 4, (void *)&tcValue,  EC_TIMEOUTTXM);
	   if(wc == 0)
	   {
		   printf("TcCommand failed\n");
	   }

	   //Let's read status register.
/*	   wc = ec_SDOread(1, 0x31e5, 0x1, 0, &dev_type_size, &status, EC_TIMEOUTRXM);
	   printf("wc after read is %d\n", wc);
	   printf("status register value is 0x%x\n", status)
*/

	   /***** read error after abort message *****/
/*	   wc = ec_SDOread(1, 0x2081, 0x4, 0, &dev_type_size, &error, EC_TIMEOUTRXM);
	   printf("wc after read is %d\n", wc);
	   printf("error value is 0x%x\n", error);
*/
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
	  in_ELMOt *in_ELMO_EL, *in_ELMO_TR;
	  int word_size = 2;
	  StatusWord status;
	  Control_Word cw;
	  int16 output_buf[12001];
	  pthread_t joystick_thread;
	  int counter = 0;
	  /****** diagnostic params *********/
	  struct timespec roundtrip_start, roundtrip_end, now, start, end;
	  unsigned long max_execution = 0, max_latency = 0, min_execution = 100000000, min_latency = 100000000;
	  unsigned long execution_time, latency_time;
	  int fd;
	  int latency_target_value = 0;

	  struct sched_param param;
	  param.sched_priority = 90;


	//  mlockall(MCL_CURRENT|MCL_FUTURE);

	  sched_setscheduler(getpid(), SCHED_FIFO, &param);

	  /***** Prevent the cpu from entering IDLE state *****/
	  fd = open("/dev/cpu_dma_latency", O_RDWR);
	  if(fd < 0) return 0;
	  write(fd, &latency_target_value, 4);


	  printf("Starting elmo communication\n");

	  /**** open control output parameters file for the motors ****/
	  res = open_out_file("/sinus.txt", output_buf, 12001);
	  if(!res)
	  {
		  printf("failed open file\n");
		  return 1;
	  }

	  /****** start joystick thread ***********/
//	  pthread_create(&joystick_thread, NULL, run, NULL);

	   /* initialise SOEM, bind socket to ifname */
	   if (ec_init("eth0"))
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

	            ec_configdc();

	            /* print PDO's mapping */
	            si_map_sdo(1);
	            si_map_sdo(2);


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

	              /* send one valid process data to make outputs in slaves happy*/
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

	                     /* cyclic loop */
	                 /********************************/
	                 startMotorTimerFunction(1);
	                 startMotorTimerFunction(2);

	                 /* diagnostic timer */
	             	clock_gettime(CLOCK_MONOTONIC, &start);
//	             	now = start;
	//             	printf("%ld.%ld\n", start.tv_sec, start.tv_nsec);

	                 /********************************/
	             	for(j = 0; j < 100; j++)
	             	{
						 for(i = 0; i <= 12000; i++)
						 {
							/**** calc clock for the next 1ms *****/
							start.tv_nsec = start.tv_nsec + 1000000;
							if(start.tv_nsec >= NSEC_PER_SEC)
							{
								start.tv_nsec -= NSEC_PER_SEC;
								start.tv_sec++;
							}
							clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &start, NULL);
							clock_gettime(CLOCK_MONOTONIC, &end);
							if((end.tv_sec * NSEC_PER_SEC  + end.tv_nsec) > (start.tv_sec * NSEC_PER_SEC +  start.tv_nsec))
							{
								latency_time = (end.tv_sec * NSEC_PER_SEC  + end.tv_nsec)  - (start.tv_sec * NSEC_PER_SEC +  start.tv_nsec);
							}
							else
							{
								printf("I woke up early\n");
							}

							if(latency_time > max_latency) max_latency = latency_time;
							if(latency_time < min_latency) min_latency = latency_time;

							/**** update outputs for motors from file*****/
//							update_el_outputs(output_buf[i]);
//							update_tr_outputs(output_buf[i]);
								/**** update outputs for motors from joystick*****/
			                	update_el_outputs(get_el_torque_val());
			                	update_tr_outputs(get_tr_torque_val());
							/****** start measure roundtrip time *****/
							clock_gettime(CLOCK_MONOTONIC, &roundtrip_start);
							ec_send_processdata();
							wkc = ec_receive_processdata(EC_TIMEOUTRET);

								 if(wkc >= expectedWKC)
								 {
									clock_gettime(CLOCK_MONOTONIC, &roundtrip_end);
									execution_time = (roundtrip_end.tv_sec * NSEC_PER_SEC  + roundtrip_end.tv_nsec)  - (roundtrip_start.tv_sec * NSEC_PER_SEC +  roundtrip_start.tv_nsec);
									if(execution_time > max_execution) max_execution = execution_time;
									if(execution_time < min_execution) min_execution = execution_time;
							   // 	 in_ELMO_EL = (in_ELMOt*)ec_slave[ELMO_EL].inputs;
							   // 	 in_ELMO_TR = (in_ELMOt*)ec_slave[ELMO_TR].inputs;
							   // 	 printf("EL pos_act is 0x%x  ", in_ELMO_EL->currentActualValue);
							   // 	 printf("EL vel is 0x%x   ", in_ELMO_EL->vx);
							   // 	 printf("TR pos_act is 0x%x  ", in_ELMO_TR->currentActualValue);
							   // 	 printf("TR vel is 0x%x   ", in_ELMO_TR->vx);
							   //     printf("Processdata cycle %4d, WKC %d , O:\r", i, wkc);
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
								 }
	//							 printf("****  wkc is less than expected\n");
								/***** sleep for 1ms ****/
	//	                     	clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &start, NULL);
								/***** read clock after wakeup *****/
	//	                     	clock_gettime(CLOCK_MONOTONIC, &end);
	//	                     	execution_time = (end.tv_sec * NSEC_PER_SEC  + end.tv_nsec)  - (now.tv_sec * NSEC_PER_SEC +  now.tv_nsec);
								// usleep(5000);

							 }
	             		}
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
	   printf("** max roundtrip time is %ld nanoSec, min execution time is %ld nanoSec, counter is %d ****\n", max_execution, min_execution, counter);
	   printf("** max latency time is %ld nanoSec, min latency time is %ld nanoSec ****\n", max_latency, min_latency);
	   return 0;
}

