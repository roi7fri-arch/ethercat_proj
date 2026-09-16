/** \file
 * \brief SOEM binding for the drive-profile layer.
 */

#include "ecat_coe.h"

#include <stdio.h>

#include "ethercattype.h"
#include "nicdrv.h"
#include "ethercatbase.h"
#include "ethercatmain.h"
#include "ethercatcoe.h"

/* Parsed once in main(); see elmo_config_setup.h. */
extern ecat_config_t g_ecat_config;

static int coe_write(void *user, uint16_t slave, uint16_t index, uint8_t sub,
                     int complete_access, int size, const void *data)
{
   (void)user;
   /* SOEM takes a non-const pointer but does not modify the buffer. */
   return ec_SDOwrite((uint16)slave, index, sub,
                      (boolean)(complete_access ? TRUE : FALSE),
                      size, (void *)(uintptr_t)data, EC_TIMEOUTSAFE);
}

static int coe_read(void *user, uint16_t slave, uint16_t index, uint8_t sub,
                    int complete_access, int *size, void *data)
{
   (void)user;
   return ec_SDOread((uint16)slave, index, sub,
                     (boolean)(complete_access ? TRUE : FALSE),
                     size, data, EC_TIMEOUTSAFE);
}

static const drive_coe_ops_t g_ops = { coe_write, coe_read, NULL };

const drive_coe_ops_t *ecat_coe_ops(void) { return &g_ops; }

int ecat_coe_po2so_config(uint16_t slave)
{
   const ecat_slave_config_t *sc;
   const drive_profile_t *profile;
   drive_ctx_t ctx;

   sc = ecat_config_find_slave(&g_ecat_config, (int)slave);
   if (!sc)
   {
      printf("slave %d: no JSON configuration, leaving default PDO mapping\n",
             slave);
      return 0;
   }

   profile = drive_profile_for_slave(sc);

   ctx.coe   = &g_ops;
   ctx.slave = slave;
   ctx.cfg   = sc;

   printf("slave %d '%s': applying profile '%s'\n",
          slave, sc->name, profile->id);

   return drive_profile_run_setup(profile, &ctx);
}
