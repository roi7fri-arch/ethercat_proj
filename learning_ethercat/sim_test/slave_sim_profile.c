/** \file
 * \brief Make the virtual slave impersonate a registered drive family.
 */

#include "slave_sim_profile.h"
#include "slave_sim.h"
#include "drive_profile.h"

#include <stdio.h>

int slavesim_impersonate(const char *profile_id)
{
   const drive_profile_t *p = drive_profile_find(profile_id);
   drive_identity_t id;

   if (!p)
   {
      printf("slavesim: no profile '%s' registered\n",
             profile_id ? profile_id : "(null)");
      return -1;
   }

   if (!p->sim_identity)
   {
      printf("slavesim: profile '%s' declares no simulated identity\n", p->id);
      return -1;
   }

   if (p->sim_identity(&id) != 0)
      return -1;

   slavesim_set_identity(id.vendor_id, id.product_code, id.revision);
   printf("slavesim: impersonating '%s' "
          "(vendor 0x%08X, product 0x%08X, rev 0x%08X)\n",
          p->id, id.vendor_id, id.product_code, id.revision);
   return 0;
}
