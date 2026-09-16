/** \file
 * \brief FoE file download, including the BOOT-state dance.
 */

#include "ecat_param.h"
#include "ecat_foe.h"

#include <stdio.h>

#include "ethercattype.h"
#include "nicdrv.h"
#include "ethercatbase.h"
#include "ethercatmain.h"

/* Taking a slave to BOOT means switching its mailbox over to the *boot*
 * mailbox, whose address and size live in a different pair of SII words. This
 * is the sequence SOEM's firm_update example uses; without the remap the slave
 * is in BOOT but the master is still addressing the mailbox it used in INIT,
 * and every FoE packet silently goes nowhere. */
int ecat_param_enter_boot(int slave)
{
   uint64 data;

   if (slave < 1 || slave > ec_slavecount)
   {
      printf("[boot] slave %d is not on the bus\n", slave);
      return -1;
   }

   ec_slave[slave].state = EC_STATE_INIT;
   ec_writestate(slave);
   if (ec_statecheck(slave, EC_STATE_INIT, EC_TIMEOUTSTATE * 4) != EC_STATE_INIT)
   {
      printf("[boot] slave %d would not go to INIT\n", slave);
      return -1;
   }

   /* Boot receive mailbox -> SM0 */
   data = ec_readeeprom(slave, ECT_SII_BOOTRXMBX, EC_TIMEOUTEEP);
   ec_slave[slave].SM[0].StartAddr = (uint16)LO_WORD(data);
   ec_slave[slave].SM[0].SMlength  = (uint16)HI_WORD(data);
   ec_slave[slave].mbx_wo = (uint16)LO_WORD(data);
   ec_slave[slave].mbx_l  = (uint16)HI_WORD(data);

   /* Boot transmit mailbox -> SM1 */
   data = ec_readeeprom(slave, ECT_SII_BOOTTXMBX, EC_TIMEOUTEEP);
   ec_slave[slave].SM[1].StartAddr = (uint16)LO_WORD(data);
   ec_slave[slave].SM[1].SMlength  = (uint16)HI_WORD(data);
   ec_slave[slave].mbx_ro = (uint16)LO_WORD(data);
   ec_slave[slave].mbx_rl = (uint16)HI_WORD(data);

   if (ec_slave[slave].mbx_l == 0 || ec_slave[slave].mbx_rl == 0)
   {
      printf("[boot] slave %d advertises no boot mailbox - it probably does "
             "not support firmware update\n", slave);
      return -1;
   }

   ec_FPWR(ec_slave[slave].configadr, ECT_REG_SM0, sizeof(ec_smt),
           &ec_slave[slave].SM[0], EC_TIMEOUTRET);
   ec_FPWR(ec_slave[slave].configadr, ECT_REG_SM1, sizeof(ec_smt),
           &ec_slave[slave].SM[1], EC_TIMEOUTRET);

   ec_slave[slave].state = EC_STATE_BOOT;
   ec_writestate(slave);
   if (ec_statecheck(slave, EC_STATE_BOOT, EC_TIMEOUTSTATE * 10) != EC_STATE_BOOT)
   {
      printf("[boot] slave %d would not go to BOOT\n", slave);
      return -1;
   }

   printf("[boot] slave %d in BOOT (mailbox %u/%u bytes)\n",
          slave, ec_slave[slave].mbx_l, ec_slave[slave].mbx_rl);
   return 0;
}

int ecat_param_leave_boot(int slave)
{
   if (slave < 1 || slave > ec_slavecount)
      return -1;

   ec_slave[slave].state = EC_STATE_INIT;
   ec_writestate(slave);
   ec_statecheck(slave, EC_STATE_INIT, EC_TIMEOUTSTATE * 4);
   printf("[boot] slave %d back in INIT\n", slave);
   return 0;
}

int ecat_param_send_file(const param_file_t *file)
{
   int rc;

   if (!file)
      return -1;

   if (file->use_boot_state && ecat_param_enter_boot(file->slave) != 0)
      return -1;

   rc = ecat_foe_download_file(file->slave, file->remote_name, file->password,
                               file->path, EC_TIMEOUTSTATE);

   if (file->use_boot_state)
      ecat_param_leave_boot(file->slave);

   return rc;
}

int ecat_param_send_files(const param_set_t *set)
{
   int failures = 0;
   int i;

   if (!set)
      return -1;

   for (i = 0; i < set->file_count; i++)
      if (ecat_param_send_file(&set->files[i]) != 0)
         failures++;

   return failures;
}
