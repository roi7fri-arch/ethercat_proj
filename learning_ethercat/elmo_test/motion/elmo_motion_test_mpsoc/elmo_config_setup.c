/* Config-driven PRE-OP -> SAFE-OP setup for Elmo Platinum drives.
 *
 * Replaces the params.dat / cyclic_sync_*_mode.dat (.ini) path: the mode of
 * operation and the RxPDO/TxPDO maps now come from the JSON produced by
 * tools/config_gui and parsed by config_loader into g_ecat_config.
 *
 * Kept separate from elmo_com.c so the original elmo_platinum_setup() stays
 * intact for reference; main() just points PO2SOconfig here instead.
 */
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

#include "elmo_config_setup.h"

/* Parsed once in main(); shared with elmo_com.c via elmo_config_setup.h. */
ecat_config_t g_ecat_config;

/* Elmo Platinum accepts at most 8 entries per PDO mapping object. Longer lists
 * are split across consecutive objects (0x1600, 0x1601, ... / 0x1a00, ...). */
#define ELMO_MAP_MAX_ENTRIES 8

/* Apply one PDO direction and assign the used mapping objects to its sync
 * manager. map_base = first mapping object (0x1600 Rx / 0x1a00 Tx),
 * sm_assign = SM PDO assign object (0x1c12 Rx / 0x1c13 Tx).
 * Returns the accumulated SDO working counter. */
static int apply_pdo_direction(uint16 slave, uint16 map_base, uint16 sm_assign,
                               const ecat_pdo_entry_t *entries, int count)
{
    int retval = 0;
    uint8 zero = 0;
    int n_objs = (count + ELMO_MAP_MAX_ENTRIES - 1) / ELMO_MAP_MAX_ENTRIES;
    int k, j;

    if (n_objs < 1)
        n_objs = 1; /* still clear and assign one (empty) object */

    /* Disable the SM assignment before rewriting the mapping objects. */
    retval += ec_SDOwrite(slave, sm_assign, 0x00, FALSE,
                          sizeof(zero), &zero, EC_TIMEOUTSAFE);

    for (k = 0; k < n_objs; k++)
    {
        uint16 map_idx = (uint16)(map_base + k);
        int start = k * ELMO_MAP_MAX_ENTRIES;
        int n_in = count - start;
        uint8 c;

        if (n_in > ELMO_MAP_MAX_ENTRIES)
            n_in = ELMO_MAP_MAX_ENTRIES;
        if (n_in < 0)
            n_in = 0;

        /* Clear entry count before (re)writing the sub-entries. */
        retval += ec_SDOwrite(slave, map_idx, 0x00, FALSE,
                              sizeof(zero), &zero, EC_TIMEOUTSAFE);

        for (j = 0; j < n_in; j++)
        {
            uint32 val = ecat_pdo_map_value(&entries[start + j]);
            retval += ec_SDOwrite(slave, map_idx, (uint8)(j + 1), FALSE,
                                  sizeof(val), &val, EC_TIMEOUTSAFE);
        }

        c = (uint8)n_in;
        retval += ec_SDOwrite(slave, map_idx, 0x00, FALSE,
                              sizeof(c), &c, EC_TIMEOUTSAFE);
    }

    /* Assign the (possibly multiple) mapping objects to the sync manager. */
    for (k = 0; k < n_objs; k++)
    {
        uint16 map_idx = (uint16)(map_base + k);
        retval += ec_SDOwrite(slave, sm_assign, (uint8)(k + 1), FALSE,
                              sizeof(map_idx), &map_idx, EC_TIMEOUTSAFE);
    }
    {
        uint8 assign_count = (uint8)n_objs;
        retval += ec_SDOwrite(slave, sm_assign, 0x00, FALSE,
                              sizeof(assign_count), &assign_count, EC_TIMEOUTSAFE);
    }

    return retval;
}

int elmo_platinum_setup_from_config(uint16 slave)
{
    const ecat_slave_config_t *sc;
    int retval = 0;
    int l;
    uint8 mode;

    sc = ecat_config_find_slave(&g_ecat_config, slave);
    if (sc == NULL)
    {
        printf("elmo_platinum_setup_from_config: no JSON config for slave %d\n", slave);
        return 0;
    }

    /* Modes of operation (0x6060), read back via 0x6061. */
    mode = (uint8)sc->mode_of_operation;
    retval += ec_SDOwrite(slave, 0x6060, 0x00, FALSE,
                          sizeof(mode), &mode, EC_TIMEOUTSAFE);

    mode = 0;
    l = sizeof(mode);
    retval += ec_SDOread(slave, 0x6061, 0x00, FALSE, &l, &mode, EC_TIMEOUTSAFE);
    printf("slave %d '%s': mode of operation requested %d, read back %d\n",
           slave, sc->name, sc->mode_of_operation, mode);

    /* RxPDO -> 0x1600.. assigned by SM2 (0x1c12). */
    retval += apply_pdo_direction(slave, 0x1600, 0x1c12, sc->rxpdo, sc->rxpdo_count);
    /* TxPDO -> 0x1a00.. assigned by SM3 (0x1c13). */
    retval += apply_pdo_direction(slave, 0x1a00, 0x1c13, sc->txpdo, sc->txpdo_count);

    printf("slave %d configured from JSON (rx=%d, tx=%d), SDO wkc sum = %d\n",
           slave, sc->rxpdo_count, sc->txpdo_count, retval);
    return 1;
}
