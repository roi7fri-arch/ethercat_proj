/** \file
 * \brief Registration of every built-in drive family.
 */

#include "vendors.h"

/* One declaration per family. Keep alphabetical. */
int elmo_register_profiles(void);

int vendors_register_all(void)
{
   int rc = 0;

   rc |= elmo_register_profiles();
   /* rc |= copley_register_profiles(); */
   /* rc |= maxon_register_profiles();  */

   return rc;
}
