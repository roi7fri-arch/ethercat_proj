
#include <stdio.h>
#include "varstable.h"
#include "ini.h"
#include <stdlib.h>

#define ELMO_PLATINUM_PDO_MAP_SIZE	8

int main(void)
{
	int pdo_val = 0;
	int *pdo_array = NULL;
	int pdo_size = 0;
	int i = 0, j=0, tx_start_inx = 0;
	int Tx_sec = 0;

	int rx_pdo_count = 0, tx_pdo_count = 0;
	int mode_of_oper = 0;

	if (ini_init("mode.dat") == 0)
	{
		printf("error, cannot open file for reading.\n");
		exit(-1);
	}

	if (ini_get_int("mode_of_oper", &mode_of_oper))
		printf("mode_of_oper = %d\n", mode_of_oper);
	ini_close();

	if (pdo_ini_init("cyclic_sync_pos_mode.dat") == 0)
	{
		printf("error, cannot open file for reading.\n");
		exit(-1);
	}

	pdo_ini_read_table();
	display_table();

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
			//ec_SDOwrite(slave, 0x1600 + (i/ELMO_PLATINUM_PDO_MAP_SIZE), 0x1, FALSE, sizeof(pdo_array[i]), &pdo_array[i],  EC_TIMEOUTSAFE);		
			printf("RxPDO 0x%x, object index 0x%x\n", pdo_array[i], 0x1600 + (i/ELMO_PLATINUM_PDO_MAP_SIZE));
			rx_pdo_count++;
		}
		/**** TxPDO definition section *****/
		else
		{
			//ec_SDOwrite(slave, 0x1a00 + ((i - tx_start_inx)/ELMO_PLATINUM_PDO_MAP_SIZE), 0x1 + i, FALSE, sizeof(pdo_array[i]), &pdo_array[i],  EC_TIMEOUTSAFE);
			printf("TxPDO 0x%x, object index 0x%x\n", pdo_array[i], 0x1a00 + ((i - tx_start_inx)/ELMO_PLATINUM_PDO_MAP_SIZE));
			tx_pdo_count++;
		}
	}
	printf("rx_pdo_count is %d, tx_pdo_count is %d\n", rx_pdo_count, tx_pdo_count);
	pdo_ini_close();
}

