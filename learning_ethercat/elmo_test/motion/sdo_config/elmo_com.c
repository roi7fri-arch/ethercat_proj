#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/mman.h>
#include <fcntl.h> /*for using read write permissions flags when openning the files*/
#include <sched.h> /* for changing scehduler policy and priority*/
#include <signal.h>
#include <stdlib.h>

#include "ethercattype.h"
#include "nicdrv.h"
#include "ethercatbase.h"
#include "ethercatmain.h"
#include "ethercatconfig.h"
#include "ethercatcoe.h"
#include "ethercatdc.h"
#include "ethercatprint.h"

#include "params.h"


#define NSEC_PER_SEC 1000000000

extern char IOmap[4096];
in_ELMOt elmo_inTR[12000];
in_ELMOt elmo_inEL[12000];
out_ELMOt elmo_outTR[12000];
out_ELMOt elmo_outEL[12000];

int expectedWKC;
volatile int wkc;
pthread_t joystick_thread;

void sig_term_catch(int sig_num)
{
	printf("Got signal number %d\n", sig_num);
    printf("\nRequest init state for all slaves\n");
    /* request INIT state for all slaves */
    ec_slave[0].state = EC_STATE_INIT;
  //  ec_writestate(0);
	ec_close();
//	pthread_cancel(joystick_thread);
	exit(0);
}


int elmo_setup(uint16 slave)
{
	int retval;
	int wc = 0;
	int num_of_mapping = 0;
	int dev_type_size = 4;

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
	uint16 map_1c13[11] = {0x000a, 0x1a0a, 0x1a0b, 0x1a0e, 0x1a10, 0x1a11, 0x1a12, 0x1a13, 0x1a1a, 0x1a1c, 0x1a1f};


	  retval += ec_SDOwrite(slave, 0x1600, 0x0, 0, sizeof(num_of_mapping), &num_of_mapping,  EC_TIMEOUTSAFE);
	   wc = ec_SDOread(slave, 0x1600, 0x0, 0, &dev_type_size, &num_of_mapping, EC_TIMEOUTRXM);
	   printf("wc after read is %d\n", wc);
	   printf("number of mapped object is %d\n", num_of_mapping);
	   num_of_mapping = 0;
	   wc = ec_SDOread(slave, 0x1600, 01, 0, &dev_type_size, &num_of_mapping, EC_TIMEOUTRXM);
	   printf("mapped object is 0x%x\n", num_of_mapping);
	   wc = ec_SDOread(slave, 0x1600, 02, 0, &dev_type_size, &num_of_mapping, EC_TIMEOUTRXM);
	   printf("mapped object is 0x%x\n", num_of_mapping);

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


int elmo_platinum_setup(uint16 slave)
{
	#define ELMO_PLATINUM_PDO_MAP_SIZE	8
	int pdo_val = 0;
	int *pdo_array = NULL;
	int pdo_size = 0;
	int i = 0, j=0, tx_start_inx = 0, l;
	int Tx_sec = 0;
	int retval;
	uint8 objectValue8 = 0;
	uint32 objuectValue32 = 0;
	int rx_pdo_count = 0, tx_pdo_count = 0;
	unsigned char mode_of_oper = 0;
	int result=0;

	retval = 0;


	if (ini_init("params.dat") == 0)
	{
		printf("error, cannot open file for reading.\n");
		exit(-1);
	}

	if (ini_get_int("mode_of_oper", &mode_of_oper))
		printf("mode_of_oper = %d\n", mode_of_oper);

	switch(mode_of_oper)
	{
		case 8:
			result = pdo_ini_init("cyclic_sync_pos_mode.dat");
			break;
		case 9:
			result = pdo_ini_init("cyclic_sync_vel_mode.dat");
			break;
		case 10:
			result = pdo_ini_init("cyclic_sync_tor_mode.dat");
			break;
		default:
			printf("error, mode of operation is not valid.\n");
			exit(-1);
	}

	if(result == 0)
	{
		printf("error, cannot open file for reading.\n");
		exit(-1);
	}

	/* Configure mode of operation */
	retval += ec_SDOwrite(slave, 0x6060, 0x0, FALSE, sizeof(mode_of_oper), &mode_of_oper,  EC_TIMEOUTSAFE);
	mode_of_oper = 0;
	l = sizeof(mode_of_oper);
	/* Read mode of operation */
	retval += ec_SDOread(slave, 0x6061, 0x0, FALSE, &l, &mode_of_oper,  EC_TIMEOUTSAFE);
	printf("Configured mode of operation to ****  %d *****\n", mode_of_oper);

	pdo_ini_read_table();
	//display_table();

	/* Configure amplifier with flexible PDO */
	printf("Configure amplifier with flexible PDO ...\n");
	/* Clear RxPdo */
	retval += ec_SDOwrite(slave, 0x1c12, 0x0, FALSE, sizeof(objectValue8), &objectValue8,  EC_TIMEOUTSAFE); /* clear SM2 pdo 0x1c12 */
	retval += ec_SDOwrite(slave, 0x1600, 0x0, FALSE, sizeof(objectValue8), &objectValue8,  EC_TIMEOUTSAFE); /* clear RxPdo 0x1600 */
	/* Clear Axis 2 RxPdo */
	retval += ec_SDOwrite(slave, 0x1610, 0x0, FALSE, sizeof(objectValue8), &objectValue8,  EC_TIMEOUTSAFE); /* clear RxPdo 0x1610 */

	/* Clear TxPdo */
	objectValue8 = 0;
	retval += ec_SDOwrite(slave, 0x1c13, 0x0, FALSE, sizeof(objectValue8), &objectValue8,  EC_TIMEOUTSAFE); /* clear SM3 pdo 0x1c13 */
	retval += ec_SDOwrite(slave, 0x1a00, 0x0, FALSE, sizeof(objectValue8), &objectValue8,  EC_TIMEOUTSAFE); /* clear TxPdo 0x1a00 */
	retval += ec_SDOwrite(slave, 0x1a01, 0x0, FALSE, sizeof(objectValue8), &objectValue8,  EC_TIMEOUTSAFE); /* clear TxPdo 0x1a01 */
	retval += ec_SDOwrite(slave, 0x1a02, 0x0, FALSE, sizeof(objectValue8), &objectValue8,  EC_TIMEOUTSAFE); /* clear TxPdo 0x1a02 */
	/* Clear Axis 2 TxPdo */
	retval += ec_SDOwrite(slave, 0x1a10, 0x0, FALSE, sizeof(objectValue8), &objectValue8,  EC_TIMEOUTSAFE); /* clear TxPdo 0x1a10 */
	retval += ec_SDOwrite(slave, 0x1a11, 0x0, FALSE, sizeof(objectValue8), &objectValue8,  EC_TIMEOUTSAFE); /* clear TxPdo 0x1a11 */
	retval += ec_SDOwrite(slave, 0x1a12, 0x0, FALSE, sizeof(objectValue8), &objectValue8,  EC_TIMEOUTSAFE); /* clear TxPdo 0x1a12 */

	pdo_size = get_list_size();
	pdo_array = prepare_pdo_array();
	for(i = 0 ; i < pdo_size ; i++)
	{
		if(pdo_array[i] == 0x0)
		{
			Tx_sec = 1;
			i++;
			tx_start_inx = i;
		}

		/**** RxPDO definition section *****/
		if(Tx_sec == 0)
		{
			/* RxPDO Axis 1 */
			ec_SDOwrite(slave, 0x1600 + (i/ELMO_PLATINUM_PDO_MAP_SIZE), 0x1 + i, FALSE, sizeof(pdo_array[i]), &pdo_array[i],  EC_TIMEOUTSAFE);
			/* RxPDO Axis 2 */
			ec_SDOwrite(slave, 0x1610 + (i/ELMO_PLATINUM_PDO_MAP_SIZE), 0x1 + i, FALSE, sizeof(pdo_array[i]), &pdo_array[i],  EC_TIMEOUTSAFE);
			rx_pdo_count++;
			printf("RxPDO 0x%x, object index 0x%x\n", pdo_array[i], 0x1600 + (i/ELMO_PLATINUM_PDO_MAP_SIZE));
		}
		/**** TxPDO definition section *****/
		else
		{
			/* TxPDO Axis 1 */
			ec_SDOwrite(slave, 0x1a00 + ((i - tx_start_inx)/ELMO_PLATINUM_PDO_MAP_SIZE), 0x1 + ((i - tx_start_inx)%ELMO_PLATINUM_PDO_MAP_SIZE), 
					FALSE, sizeof(pdo_array[i]), &pdo_array[i],  EC_TIMEOUTSAFE);
			/* TxPDO Axis 2 */
			ec_SDOwrite(slave, 0x1a10 + ((i - tx_start_inx)/ELMO_PLATINUM_PDO_MAP_SIZE), 0x1 + ((i - tx_start_inx)%ELMO_PLATINUM_PDO_MAP_SIZE),
					FALSE, sizeof(pdo_array[i]), &pdo_array[i],  EC_TIMEOUTSAFE);
			tx_pdo_count++;
			printf("TxPDO 0x%x, object index 0x%x\n", pdo_array[i], 0x1a00 + ((i - tx_start_inx)/ELMO_PLATINUM_PDO_MAP_SIZE));
		}
	}

	objectValue8 = rx_pdo_count%ELMO_PLATINUM_PDO_MAP_SIZE;
	retval += ec_SDOwrite(slave, 0x1600, 0x0, FALSE, sizeof(objectValue8), &objectValue8,  EC_TIMEOUTSAFE); /* set the number of objects for Axis 1 */
	retval += ec_SDOwrite(slave, 0x1610, 0x0, FALSE, sizeof(objectValue8), &objectValue8,  EC_TIMEOUTSAFE); /* set the number of objects for Axis 2 */

	objectValue8 = ELMO_PLATINUM_PDO_MAP_SIZE;
	retval += ec_SDOwrite(slave, 0x1a00, 0x0, FALSE, sizeof(objectValue8), &objectValue8,  EC_TIMEOUTSAFE); /* set the number of objects */
	retval += ec_SDOwrite(slave, 0x1a10, 0x0, FALSE, sizeof(objectValue8), &objectValue8,  EC_TIMEOUTSAFE); /* set the number of objects */
	objectValue8 = tx_pdo_count%ELMO_PLATINUM_PDO_MAP_SIZE;
	retval += ec_SDOwrite(slave, 0x1a01, 0x0, FALSE, sizeof(objectValue8), &objectValue8,  EC_TIMEOUTSAFE); /* set the number of objects */
	retval += ec_SDOwrite(slave, 0x1a11, 0x0, FALSE, sizeof(objectValue8), &objectValue8,  EC_TIMEOUTSAFE); /* set the number of objects */
	//objectValue8 = tx_pdo_count%ELMO_PLATINUM_PDO_MAP_SIZE;
	//retval += ec_SDOwrite(slave, 0x1a02, 0x0, FALSE, sizeof(objectValue8), &objectValue8,  EC_TIMEOUTSAFE); /* set the number of objects */



	uint16 map_1c12[3] = {0x0002, 0x1600, 0x1610};
	/* SM3 (Inputs) PDO assignments Slave --> Master*/
	/* SM3 - SYNC Manager 3 for TxPDO */
	/* The first element in the array is the number of objects in this PDO */
	/* 0x1a0f is PDO index of 32 bit Velocity sensor actual value */
	/* 0x1a0a is PDO index of 16 bit Status word */
	/* 0x1a1f is PDO index of 16 bit Current actual value */
	uint16 map_1c13[5] = {0x0004, 0x1a00, 0x1a01, 0x1a10, 0x1a11};

	/* Config SM2 (Outputs) PDO assignments (Master --> Slave) with the help of SDO write to Dictionary Object of the slave we get as input*/
	/* 0x1c12 is the index in the Dictionary Object that store the PDO assignment of the outputs that we choose to send to the slave in the cyclic loop*/
	/* I think the limit is 30 x uint16 outputs from Elmo docs. 30 is the number of sub-indexs*/
	retval += ec_SDOwrite(slave, 0x1c12, 0x0, 1, sizeof(map_1c12), &map_1c12,  EC_TIMEOUTSAFE);
	/* Config SM3 (Inputs) PDO assignments (Slave --> Master) with the help of SDO write to Dictionary Object of the slave we get as input*/
	/* 0x1c13 is the index in the Dictionary Object that store the PDO assignment of the input that we choose to receive from the slave in the cyclic loop*/
	/* I think the limit is 35 x uint16 inputs from Elmo docs. 35 is the number of sub-indexs*/
	retval += ec_SDOwrite(slave, 0x1c13, 0x0, 1, sizeof(map_1c13), &map_1c13,  EC_TIMEOUTSAFE);

	printf("Elmo slave %d set, retval = %d\n", slave, retval);
	ini_close();
	pdo_ini_close();
	return 1;

}

#ifdef LOG
int elmo_platinum_setup(uint16 slave)
{
	int retval;
	int i;

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
	uint8 objectValue8 = 0;
	uint32 objuectValue32 = 0;
	uint32 map_RxPdo[2] = {0x60400010, 0x60600008};	/*control word, modes of operation*/
	uint32 map_TxPdo1[8] = {0x2fe40140, 0x2fe80120, 0x2fe40240, 0x2fe80220, 0x2fe40340, 0x2fe80320, 0x2fec0120, 0x36400020};

	uint32 map_TxPdo2[8] = {0x603f0010, 0x60410010, 0x60610008, 0x60620020, 0x60630020, 0x60640020, 0x60690020, 0x606b0020};

	uint32 map_TxPdo3[6] = {0x606c0020, 0x60740010, 0x60770010, 0x60780010, 0x60790020, 0xf6f00120};


	/* Configure amplifier with flexible PDO */
	printf("Configure amplifier with flexible PDO ...\n");
	/* Clear RxPdo */
	retval += ec_SDOwrite(slave, 0x1c12, 0x0, FALSE, sizeof(objectValue8), &objectValue8,  EC_TIMEOUTSAFE); /* clear SM2 pdo 0x1c12 */
	retval += ec_SDOwrite(slave, 0x1600, 0x0, FALSE, sizeof(objectValue8), &objectValue8,  EC_TIMEOUTSAFE); /* clear RxPdo 0x1600 */

	/* define RxPdo */
	//retval += ec_SDOwrite(slave, 0x1600, 0x1, TRUE, sizeof(map_RxPdo), &map_RxPdo,  EC_TIMEOUTSAFE);
	retval += ec_SDOwrite(slave, 0x1600, 0x1, FALSE, sizeof(map_RxPdo[0]), &map_RxPdo[0],  EC_TIMEOUTSAFE);
	//retval += ec_SDOwrite(slave, 0x1600, 0x2, FALSE, sizeof(map_RxPdo[1]), &map_RxPdo[1],  EC_TIMEOUTSAFE);
	//retval += ec_SDOwrite(slave, 0x1600, 0x3, FALSE, sizeof(map_RxPdo[2]), &map_RxPdo[2],  EC_TIMEOUTSAFE);
	objectValue8 = 1;
	retval += ec_SDOwrite(slave, 0x1600, 0x0, FALSE, sizeof(objectValue8), &objectValue8,  EC_TIMEOUTSAFE); /* set the number of objects */

	/* Clear TxPdo */
	objectValue8 = 0;
	retval += ec_SDOwrite(slave, 0x1c13, 0x0, FALSE, sizeof(objectValue8), &objectValue8,  EC_TIMEOUTSAFE); /* clear SM3 pdo 0x1c13 */
	retval += ec_SDOwrite(slave, 0x1a00, 0x0, FALSE, sizeof(objectValue8), &objectValue8,  EC_TIMEOUTSAFE); /* clear TxPdo 0x1a00 */
	retval += ec_SDOwrite(slave, 0x1a01, 0x0, FALSE, sizeof(objectValue8), &objectValue8,  EC_TIMEOUTSAFE); /* clear TxPdo 0x1a01 */
	retval += ec_SDOwrite(slave, 0x1a02, 0x0, FALSE, sizeof(objectValue8), &objectValue8,  EC_TIMEOUTSAFE); /* clear TxPdo 0x1a02 */

	/* define TxPdo */
	//retval += ec_SDOwrite(slave, 0x1a00, 0x1, TRUE, sizeof(map_TxPdo1), &map_TxPdo1,  EC_TIMEOUTSAFE);
	//retval += ec_SDOwrite(slave, 0x1a01, 0x1, TRUE, sizeof(map_TxPdo2), &map_TxPdo2,  EC_TIMEOUTSAFE);
	//retval += ec_SDOwrite(slave, 0x1a02, 0x1, TRUE, sizeof(map_TxPdo3), &map_TxPdo3,  EC_TIMEOUTSAFE);
	for(i = 0; i < 8; i++)
	{
		retval += ec_SDOwrite(slave, 0x1a00, 0x1 + i, FALSE, sizeof(map_TxPdo1[i]), &map_TxPdo1[i],  EC_TIMEOUTSAFE);
	}
	for(i = 0; i < 8; i++)
	{
		retval += ec_SDOwrite(slave, 0x1a01, 0x1 + i, FALSE, sizeof(map_TxPdo2[i]), &map_TxPdo2[i],  EC_TIMEOUTSAFE);
	}

	for(i = 0; i < 6; i++)
	{
		retval += ec_SDOwrite(slave, 0x1a02, 0x1 + i, FALSE, sizeof(map_TxPdo3[i]), &map_TxPdo3[i],  EC_TIMEOUTSAFE);
	}

	objectValue8 = 8;
	retval += ec_SDOwrite(slave, 0x1a00, 0x0, FALSE, sizeof(objectValue8), &objectValue8,  EC_TIMEOUTSAFE); /* set the number of objects */
	retval += ec_SDOwrite(slave, 0x1a01, 0x0, FALSE, sizeof(objectValue8), &objectValue8,  EC_TIMEOUTSAFE); /* set the number of objects */
	objectValue8 = 6;
	retval += ec_SDOwrite(slave, 0x1a02, 0x0, FALSE, sizeof(objectValue8), &objectValue8,  EC_TIMEOUTSAFE); /* set the number of objects */



	uint16 map_1c12[2] = {0x0001, 0x1600};
	/* SM3 (Inputs) PDO assignments Slave --> Master*/
	/* SM3 - SYNC Manager 3 for TxPDO */
	/* The first element in the array is the number of objects in this PDO */
	/* 0x1a0f is PDO index of 32 bit Velocity sensor actual value */
	/* 0x1a0a is PDO index of 16 bit Status word */
	/* 0x1a1f is PDO index of 16 bit Current actual value */
	uint16 map_1c13[4] = {0x0003, 0x1a00, 0x1a01, 0x1a02};/*, 0x1a0e, 0x1a10, 0x1a11, 0x1a12, 0x1a13, 0x1a1a, 0x1a1c, 0x1a1f};*/
	
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


#endif

int main(int argc, char *argv[])
{

	  int dev_type = 0;
	  int dev_type_size = 4;
	  int data = 0;
	  int error = 0;
	  float tcValue = 0.0;
	  unsigned int wc = 0;
	  char eth_interface[50];
	  int res = 0;
	  int i = 0, j;
	  int oloop, iloop, chk;
	  int word_size = 2;
	  int16 output_buf[12001];

	  /****** diagnostic params *********/
	  struct timespec roundtrip_start, roundtrip_end, now, start, end;
	  unsigned long max_execution = 0, max_latency = 0, min_execution = 100000000, min_latency = 100000000;
	  unsigned long execution_time, latency_time;
	  int fd;
	  int latency_target_value = 0;




	  signal(SIGINT, sig_term_catch);
	  signal(SIGTERM, sig_term_catch);

	   if (argc < 4 || argc > 5)
	   {
	      printf("Usage: sdo_config [slave number > 0] [index] [subindex] [Size in bytes] [value - in case of write command]\n");
	      if( argc > 2)
	         ctime = atoi(argv[2]);
	   }

	  if (ini_init("params.dat") == 0)
	  {
			printf("error, cannot open file for reading.\n");
			exit(-1);
	  }

		if (ini_get_string("eth_interface", &eth_interface))
			printf("ethernet interface = %s\n", eth_interface);



	  printf("Starting elmo communication\n");


	   /* initialise SOEM, bind socket to ifname */
	   if (ec_init(eth_interface))
	   {
	      printf("ec_init on %s succeeded.\n", eth_interface);


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

	            printf("Slaves mapped, state to SAFE_OP.\n");
	              /* wait for all slaves to reach SAFE_OP state */
	              ec_statecheck(0, EC_STATE_SAFE_OP,  EC_TIMEOUTSTATE * 4);

	              /********* ec_slave[0] is reserved for the master. Structure gets filled
	                 in by the configuration function ec_config().*******/
	              /******* Importent: ec_slave[0].Oloop is the total output bytes that we send to all the slaves together!
	               * ec_slave[0].iloop is the total input bytes we get from all the slaves together!
	               */
	              
	              /*****************************/
	              wc = ec_SDOread(1, 0x6075, 0x0, 0, &dev_type_size, &data, EC_TIMEOUTRXM);
	       	   	  printf("wc after read is %d\n", wc);
	       	   	  printf("Motor 1 rate value is %dmAmp\n", data);
	       	      data = 8;
	       	   	  retval += ec_SDOwrite(slave, 0x1a00, 0x0, FALSE, sizeof(data), &data,  EC_TIMEOUTSAFE); /* set the number of objects */

                 /****************************/

	                     ec_readstate();


	                 printf("Slave %d State=0x%2.2x StatusCode=0x%4.4x : %s\n",
	                             i, ec_slave[i].state, ec_slave[i].ALstatuscode, ec_ALstatuscode2string(ec_slave[i].ALstatuscode));

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

	   ec_close();
//	   create_log_file(elmo_inTR, elmo_inEL, elmo_outTR, elmo_outEL, 12000);
//	   pthread_cancel(joystick_thread);
	   return 0;
}

