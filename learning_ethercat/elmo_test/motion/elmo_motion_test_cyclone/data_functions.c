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


#define ELMO_EL 1
#define ELMO_TR 2


void set_output_int16(uint16 slave_no, uint8 module_index, int16 value)
{
	uint8 *data_ptr;

	/*Here we get pointer to iomap - slave I/O pointers - pointer for SM2 outputs*/
	data_ptr = ec_slave[slave_no].outputs;
	/* Move pointer to correct module index */
	data_ptr += module_index * 2;
	/* Read value byte by byte since all targets can't handle misaligned addresses */
	*data_ptr++ = (value >> 0) & 0xFF;
	*data_ptr++ = (value >> 8) & 0xFF;
}

void set_output_int32(uint16 slave_no, uint8 module_index, int32 value)
{
	uint8 *data_ptr;

	/*Here we get pointer to iomap - slave I/O pointers - pointer for SM2 outputs*/
	data_ptr = ec_slave[slave_no].outputs;
	/* Move pointer to correct module index */
	data_ptr += module_index * 4;
	/* Read value byte by byte since all targets can't handle misaligned addresses */
	*data_ptr++ = (value >> 0) & 0xFF;
	*data_ptr++ = (value >> 8) & 0xFF;
	*data_ptr++ = (value >> 16) & 0xFF;
	*data_ptr++ = (value >> 24) & 0xFF;
}
