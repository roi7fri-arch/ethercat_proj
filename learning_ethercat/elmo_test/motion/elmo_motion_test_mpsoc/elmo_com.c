#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/mman.h>
#include <fcntl.h> /*for using read write permissions flags when openning the files*/
#include <sched.h> /* for changing scehduler policy and priority*/
#include <signal.h>
#include <stdlib.h>
#include <pthread.h>

#include "varstable.h"
#include "ini.h"
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
#include "ECD_Motor.h"
#include "elmo_config_setup.h"
#include "telemetry.h"
#include "ecat_diag.h"


#define NSEC_PER_SEC 1000000000

extern char IOmap[4096];
in_ELMOt elmo_inTR[12000];
in_ELMOt elmo_inEL[12000];
out_ELMOt elmo_outTR[12000];
out_ELMOt elmo_outEL[12000];

int expectedWKC;
volatile int wkc;
pthread_t joystick_thread;
int shutdown = 0;

/* Set once the bus is OP so the recovery monitor only acts on real faults. */
volatile boolean inOP = FALSE;
uint8 currentgroup = 0;
pthread_t ecat_recovery_thread;

/* Background auto-recovery monitor (runs off the RT path). While the bus is OP,
 * a working-counter shortfall or a slave leaving OP triggers: ACK errors, push
 * SAFE_OP->OP, reconfigure a returning slave, or recover a fully lost one.
 * Mirrors the SOEM ecatcheck reference loop; timeout is config-driven. */
static void *ecat_recovery_check(void *ptr)
{
	int slave;
	int timeout = g_ecat_config.network.auto_recovery_timeout_us;
	int dc_tick = 0;
	(void)ptr;
	if (timeout <= 0)
		timeout = 500;
	while (!shutdown)
	{
		if (inOP && ((wkc < expectedWKC) || ec_group[currentgroup].docheckstate))
		{
			ec_group[currentgroup].docheckstate = FALSE;
			ec_readstate();
			for (slave = 1; slave <= ec_slavecount; slave++)
			{
				if ((ec_slave[slave].group == currentgroup) && (ec_slave[slave].state != EC_STATE_OPERATIONAL))
				{
					ec_group[currentgroup].docheckstate = TRUE;
					if (ec_slave[slave].state == (EC_STATE_SAFE_OP + EC_STATE_ERROR))
					{
						printf("ERROR : slave %d is in SAFE_OP + ERROR, attempting ack.\n", slave);
						ec_slave[slave].state = (EC_STATE_SAFE_OP + EC_STATE_ACK);
						ec_writestate(slave);
					}
					else if (ec_slave[slave].state == EC_STATE_SAFE_OP)
					{
						printf("WARNING : slave %d is in SAFE_OP, change to OPERATIONAL.\n", slave);
						ec_slave[slave].state = EC_STATE_OPERATIONAL;
						ec_writestate(slave);
					}
					else if (ec_slave[slave].state > 0)
					{
						if (ec_reconfig_slave(slave, timeout))
						{
							ec_slave[slave].islost = FALSE;
							printf("MESSAGE : slave %d reconfigured\n", slave);
						}
					}
					else if (!ec_slave[slave].islost)
					{
						ec_statecheck(slave, EC_STATE_OPERATIONAL, EC_TIMEOUTRET);
						if (!ec_slave[slave].state)
						{
							ec_slave[slave].islost = TRUE;
							printf("ERROR : slave %d lost\n", slave);
						}
					}
				}
				if (ec_slave[slave].islost)
				{
					if (!ec_slave[slave].state)
					{
						if (ec_recover_slave(slave, timeout))
						{
							ec_slave[slave].islost = FALSE;
							printf("MESSAGE : slave %d recovered\n", slave);
						}
					}
					else
					{
						ec_slave[slave].islost = FALSE;
						printf("MESSAGE : slave %d found\n", slave);
					}
				}
			}
			if (!ec_group[currentgroup].docheckstate)
				printf("OK : all slaves resumed OPERATIONAL.\n");
		}
		/* Drain any CoE Emergency / mailbox errors the slaves pushed. */
		ecat_diag_drain_errors();
		/* Low-rate DC sync-window watch (~every 2 s) while DC is active. */
		if (inOP && g_ecat_config.network.distributed_clock && (++dc_tick >= 200))
		{
			ecat_dc_diff_t dcw[ECAT_CFG_MAX_SLAVES];
			int nw = ecat_diag_dc_scan(dcw, ECAT_CFG_MAX_SLAVES);
			int bad = ecat_diag_dc_out_of_window(dcw, nw, ECAT_DC_SYNC_WINDOW_NS);
			if (bad)
				printf("WARNING: DC drift - %d slave(s) outside the %u ns sync window.\n",
				       bad, ECAT_DC_SYNC_WINDOW_NS);
			dc_tick = 0;
		}
		usleep(10000);
	}
	return NULL;
}

void sig_term_catch(int sig_num)
{
	shutdown = 1;
}

/* Add a signed nanosecond delta to an absolute timespec, keeping it normalized. */
static void add_timespec_ns(struct timespec *ts, int64 delta_ns)
{
	int64 nsec = (int64)ts->tv_nsec + delta_ns;
	ts->tv_sec += nsec / NSEC_PER_SEC;
	nsec = nsec % NSEC_PER_SEC;
	if (nsec < 0)
	{
		nsec += NSEC_PER_SEC;
		ts->tv_sec -= 1;
	}
	ts->tv_nsec = nsec;
}

/* PI controller that nudges the local cycle so it stays phase-locked to the
 * slaves' DC SYNC0. reftime = ec_DCtime, cycletime in ns; result in *offset.
 * kp_div/ki_div are the (config-tunable) proportional/integral divisors. */
static void ec_sync(int64 reftime, int64 cycletime, int64 kp_div, int64 ki_div, int64 *offset)
{
	static int64 integral = 0;
	int64 delta = reftime % cycletime;
	if (delta > (cycletime / 2))
		delta -= cycletime;
	if (delta > 0)
		integral++;
	if (delta < 0)
		integral--;
	if (kp_div <= 0)
		kp_div = 100;
	if (ki_div <= 0)
		ki_div = 20;
	*offset = -(delta / kp_div) - (integral / ki_div);
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

int main()
{

	  int dev_type = 0;
	  int dev_type_size = 4;
	  int data = 0;
	  int error = 0;
	  float tcValue = 0.0;
	  unsigned int wc = 0;
	  char eth_interface[50];
	  int number_of_cycles = 0;
	  int res = 0;
	  int i = 0, j;
	  int oloop, iloop, chk;
	  in_ELMOt *in_ELMO_EL, *in_ELMO_TR;
	  int word_size = 2;
	  StatusWord status;
	  Control_Word cw;
	  int16 output_buf[12001];
	  RT_MODEL_ECD_Motor_T rt_motor;
	  ExtU_ECD_Motor_T extu_motor;
	  ExtY_ECD_Motor_T exty_motor;
	  int once = 0;
	  telemetry_t tlm;
	  int recovery_running = 0;
	  ecat_diag_slave_t diag[ECAT_CFG_MAX_SLAVES];
	  int ndiag = 0;
	  ecat_topo_t topo[ECAT_CFG_MAX_SLAVES];
	  int ntopo = 0;
	  ecat_dc_diff_t dcd[ECAT_CFG_MAX_SLAVES];
	  int ndc = 0;

	  /****** diagnostic params *********/
	  struct timespec roundtrip_start, roundtrip_end, now, start, end, prev_start;
	  unsigned long max_execution = 0, max_latency = 0, min_execution = 100000000, min_latency = 100000000;
	  unsigned long execution_time = 0, latency_time = 0;
	  int fd;
	  int latency_target_value = 0;
	  int prev_diff;
	  int64 cycletime_ns = 250000;
	  int64 toff = 0;
	  unsigned long woke_early = 0;

	  struct sched_param param;
	  param.sched_priority = 90;
	  int max_cycle_time = 0;


	  mlockall(MCL_CURRENT|MCL_FUTURE);

	  sched_setscheduler(getpid(), SCHED_FIFO, &param);

	  signal(SIGINT, sig_term_catch);
	  signal(SIGTERM, sig_term_catch);

	  /***** Prevent the cpu from entering IDLE state *****/
	  fd = open("/dev/cpu_dma_latency", O_RDWR);
	  if(fd < 0) return 0;
	  write(fd, &latency_target_value, 4);

	  /* Load the EtherCAT configuration produced by tools/config_gui.
	   * Replaces the params.dat / cyclic_sync_*_mode.dat (.ini) files:
	   * interface, cycle count, mode of operation and PDO maps all come
	   * from this single JSON file, applied per-slave in PO2SOconfig. */
	  if (ecat_config_load_file("ethercat_config.json", &g_ecat_config) != 0)
	  {
			printf("error, cannot load ethercat_config.json: %s\n",
			       ecat_config_last_error());
			exit(-1);
	  }
	  ecat_config_print(&g_ecat_config);

	  strncpy(eth_interface, g_ecat_config.network.interface,
	          sizeof(eth_interface) - 1);
	  eth_interface[sizeof(eth_interface) - 1] = '\0';
	  number_of_cycles = g_ecat_config.network.number_of_cycles;
	  printf("ethernet interface = %s\n", eth_interface);
	  printf("number of cycles = %d\n", number_of_cycles);


	  /**** open control output parameters file for the motors ****/
	 /* res = open_out_file("sinus.txt", output_buf, 12001);
	  if(!res)
	  {
		  printf("failed open file\n");
		  return 1;
	  }
	  */
	  printf("Starting elmo communication\n");

	  /****** start joystick thread ***********/
//	  pthread_create(&joystick_thread, NULL, run, NULL);

	  /********* Algo output generation init**********/
	  rt_motor.blockIO = malloc(sizeof(B_ECD_Motor_T));
	  rt_motor.dwork = malloc(sizeof(DW_ECD_Motor_T));
	  ECD_Motor_initialize(&rt_motor, &extu_motor, &exty_motor);

	   /* initialise SOEM, bind socket to ifname. When a second NIC is set in the
	    * config, use cable redundancy so a single broken cable keeps the bus up. */
	   int ec_up;
	   if (g_ecat_config.network.redundant_interface[0])
	   {
	      ec_up = ec_init_redundant(eth_interface, g_ecat_config.network.redundant_interface);
	      if (ec_up)
	         printf("ec_init_redundant on %s + %s succeeded.\n",
	                eth_interface, g_ecat_config.network.redundant_interface);
	   }
	   else
	   {
	      ec_up = ec_init(eth_interface);
	      if (ec_up)
	         printf("ec_init on %s succeeded.\n", eth_interface);
	   }
	   if (ec_up)
	   {


	      /* find and auto-config slaves */
	/*      if ( ec_config(0, &IOmap) > 0 )
	      {
	         ec_configdc();
	      }
	  */
	      if ( ec_config_init(0) > 0 )
	         {
	            printf("%d slaves found and configured.\n",ec_slavecount);

	            /* Verify the drives on the bus match the configured vendor/product/
	             * revision (skips slaves with no expected_* set). Warns only.
	             * Skipped entirely when verify_identity is off (e.g. during
	             * hardware evaluation with an undecided drive family). */
	            if (g_ecat_config.network.verify_identity)
	            {
	               if (ecat_diag_verify_identity(&g_ecat_config) != 0)
	                  printf("WARNING: slave identity mismatch - check wiring / config.\n");
	            }
	            else
	               printf("identity verification disabled in config.\n");

	  	      /*link slave specific setup to preop->safeop hook. We do PDO mapping and can set some parameters with SDO messages for example: max motor current in mA*/
	  	      /* ec_config function will call this function */
	  	     ec_slave[1].PO2SOconfig = elmo_platinum_setup_from_config;
	  	    // ec_slave[2].PO2SOconfig = elmo_setup;
	  	    // ec_slave[3].PO2SOconfig = elmo_setup;

	            ec_config_map(&IOmap);

	            /*Locate Distributed Clocks slaves, measure propagation delays*/

	            ec_configdc();

	            /* Activate DC SYNC0 on each configured slave so the drives'
	             * control cycle is phase-locked to the master cycle time.
	             * cycle_time_us / sync0_shift_us come from the JSON config. */
	            if (g_ecat_config.network.distributed_clock)
	            {
	            	int s;
	            	uint32 dc_cycle_ns = (uint32)g_ecat_config.network.cycle_time_us * 1000u;
	            	int32  shift_ns = g_ecat_config.network.sync0_shift_us * 1000;
	            	for (s = 0; s < g_ecat_config.slave_count; s++)
	            	{
	            		int pos = g_ecat_config.slaves[s].position;
	            		ec_dcsync0(pos, TRUE, dc_cycle_ns, shift_ns);
	            	}
	            	printf("DC SYNC0 activated: cycle %u ns, shift %d ns\n",
	            	       dc_cycle_ns, shift_ns);
	            }
	            else
	            {
	            	printf("DC disabled in config; running SM-synchronous (free-run).\n");
	            }

	            /* print PDO's mapping */
	            si_map_sdo(1);
	           // si_map_sdo(2);
	           // si_map_sdo(3);

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

              	/* EtherCAT standard define the in order to enter state OP in slaves we need to send valid data to outputs.*/
	              /* send one valid process data to make outputs in slaves happy*/
		      //update_el_outputs(0);
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

	                 /* Baseline link-quality snapshot (before the monitor thread
	                  * starts, so the register reads don't race the RT loop). */
	                 ndiag = ecat_diag_scan(diag, ECAT_CFG_MAX_SLAVES);
	                 ecat_diag_print("baseline (entered OP)", diag, ndiag);

	                 /* Bus topology / link map (which ports are live, parents). */
	                 ntopo = ecat_diag_topology_scan(topo, ECAT_CFG_MAX_SLAVES);
	                 ecat_diag_topology_print("baseline (entered OP)", topo, ntopo);

	                 /* Distributed-clock sync-window check: warn if any slave's
	                  * clock is still outside the acceptable deviation. */
	                 if (g_ecat_config.network.distributed_clock)
	                 {
	                 	ndc = ecat_diag_dc_scan(dcd, ECAT_CFG_MAX_SLAVES);
	                 	ecat_diag_dc_print("baseline (entered OP)", dcd, ndc, ECAT_DC_SYNC_WINDOW_NS);
	                 	if (ecat_diag_dc_out_of_window(dcd, ndc, ECAT_DC_SYNC_WINDOW_NS))
	                 		printf("WARNING: DC not yet locked - some slaves outside the sync window.\n");
	                 }

	                 /* Launch the auto-recovery monitor now that we are in OP. */
	                 if (g_ecat_config.network.auto_recovery)
	                 {
	                 	inOP = TRUE;
	                 	if (pthread_create(&ecat_recovery_thread, NULL, ecat_recovery_check, NULL) == 0)
	                 	{
	                 		recovery_running = 1;
	                 		printf("auto-recovery monitor started (timeout %d us)\n",
	                 		       g_ecat_config.network.auto_recovery_timeout_us);
	                 	}
	                 	else
	                 	{
	                 		inOP = FALSE;
	                 		printf("WARNING: could not start auto-recovery thread\n");
	                 	}
	                 }

	                     /* cyclic loop */
	                 /********************************/
	                 startMotorTimerFunction(1);
	                // startMotorTimerFunction(2);

	                 /* diagnostic timer */
	             	clock_gettime(CLOCK_MONOTONIC, &start);

	             	/* cycle period from config: 500us -> 2kHz, 250us -> 4kHz */
	             	cycletime_ns = (int64)g_ecat_config.network.cycle_time_us * 1000;
	             	if (cycletime_ns <= 0) cycletime_ns = 250000;
//	             	now = start;
	//             	printf("%ld.%ld\n", start.tv_sec, start.tv_nsec);

	                 /********************************/
	                 /* Preallocate telemetry buffers (before the RT loop). */
	                 if (telemetry_init(&tlm, &g_ecat_config, number_of_cycles + 1) != 0)
	                 {
	                 	printf("telemetry init failed: %s\n", telemetry_last_error());
	                 }
	                 else
	                 {
	                 	int s;
	                 	for (s = 0; s < g_ecat_config.slave_count; s++)
	                 	{
	                 		int pos = g_ecat_config.slaves[s].position;
	                 		telemetry_add_slave(&tlm, pos,
	                 			ec_slave[pos].outputs, ec_slave[pos].Obytes,
	                 			ec_slave[pos].inputs,  ec_slave[pos].Ibytes);
	                 	}
	                 }

	                 for(i = 0; i <= number_of_cycles; i++)
	                 {
	                	if(shutdown) shutdown_func();
	                	/* next wakeup = one cycle later, nudged by the DC offset */
	             		add_timespec_ns(&start, cycletime_ns + toff);
	            		clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &start, NULL);
	            		clock_gettime(CLOCK_MONOTONIC, &end);


	            		// shimon
	            		if (once == 0)
	            		{
	            			once = 1;
							memmove(&prev_start, &end, sizeof(struct timespec));
	            		}
	            		else
	            		{
							prev_diff = (end.tv_sec * NSEC_PER_SEC  + end.tv_nsec)  - (prev_start.tv_sec * NSEC_PER_SEC +  prev_start.tv_nsec);
							memmove(&prev_start, &end, sizeof(struct timespec));
							if (prev_diff > max_cycle_time)
								max_cycle_time = prev_diff;
	            		}



	            		if((end.tv_sec * NSEC_PER_SEC  + end.tv_nsec) > (start.tv_sec * NSEC_PER_SEC +  start.tv_nsec))
	            		{
		            		latency_time = (end.tv_sec * NSEC_PER_SEC  + end.tv_nsec)  - (start.tv_sec * NSEC_PER_SEC +  start.tv_nsec);
	            		}
	            		else
	            		{
	            			woke_early++;
	            		}

                 		if(latency_time > max_latency) max_latency = latency_time;
                 		if(latency_time < min_latency) min_latency = latency_time;

	             		/**** update outputs for motors from file*****/
//	                	update_el_outputs(output_buf[i]);
//	                	update_tr_outputs(output_buf[i]);

                 		/***** update ouputs from Algo generation code *******/
                 		ECD_Motor_step(&rt_motor, &extu_motor, &exty_motor);
                 		update_el_outputs(exty_motor.PosOffset, exty_motor.VelOffset, exty_motor.TorqueOffset);
                 		// update_outputs_telemetry_buf(elmo_outTR + i, elmo_outEL + i); // legacy 2-axis log (invalid with 1 slave; telemetry_sample replaces it)
		             		/**** update outputs for motors from joystick*****/
//		                	update_el_outputs(get_el_torque_val());
//		                	update_tr_outputs(get_tr_torque_val());
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
	                     		// update_inputs_telemetry_buf(elmo_inTR + i, elmo_inEL + i); // legacy 2-axis log (telemetry_sample replaces it)
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

	                         /* keep the local cycle phase-locked to slave DC SYNC0 */
	                         if (g_ecat_config.network.distributed_clock)
	                         	ec_sync(ec_DCtime, cycletime_ns,
	                         			g_ecat_config.network.sync_kp_div,
	                         			g_ecat_config.network.sync_ki_div, &toff);

	                         /* one telemetry sample per cycle (RT-safe copy) */
	                         telemetry_sample(&tlm, i,
	                         		(int64_t)end.tv_sec * NSEC_PER_SEC + end.tv_nsec,
	                         		(int32_t)wkc, (int64_t)latency_time,
	                         		(int64_t)execution_time, (int64_t)ec_DCtime);

	                        /***** sleep for 1ms ****/
//	                     	clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &start, NULL);
	                     	/***** read clock after wakeup *****/
//	                     	clock_gettime(CLOCK_MONOTONIC, &end);
//	                     	execution_time = (end.tv_sec * NSEC_PER_SEC  + end.tv_nsec)  - (now.tv_sec * NSEC_PER_SEC +  now.tv_nsec);
	                        // usleep(500);

	                     }

	                     /* stop the auto-recovery monitor before tearing the bus down */
	                     if (recovery_running)
	                     {
	                     	inOP = FALSE;
	                     	pthread_cancel(ecat_recovery_thread);
	                     	pthread_join(ecat_recovery_thread, NULL);
	                     	recovery_running = 0;
	                     }

	                     /* Final link-quality summary: any CRC / lost-link growth
	                      * over the run shows up here (monitor thread now stopped). */
	                     ecat_diag_drain_errors();
	                     ndiag = ecat_diag_scan(diag, ECAT_CFG_MAX_SLAVES);
	                     ecat_diag_print("final (end of run)", diag, ndiag);
	                     if (ecat_diag_has_errors(diag, ndiag))
	                     	printf("WARNING: bus errors detected - check cabling/connectors.\n");
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
	   printf("** max roundtrip time is %ld nanoSec, min execution time is %ld nanoSec ****\n", max_execution, min_execution);
	   printf("** max latency time is %ld nanoSec, min latency time is %ld nanoSec ****\n", max_latency, min_latency);
	   printf("** max cycle time is %ld nanoSec ****\n", max_cycle_time);
	   printf("** woke early %lu times ****\n", woke_early);
	   if (telemetry_write(&tlm, "telemetry") != 0)
	   	printf("telemetry write failed: %s\n", telemetry_last_error());
	   telemetry_free(&tlm);
	   ec_close();
//	   create_log_file(elmo_inTR, elmo_inEL, elmo_outTR, elmo_outEL, 12000);
//	   pthread_cancel(joystick_thread);
	   return 0;
}

