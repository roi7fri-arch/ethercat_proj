#ifndef ELMO_CONFIG_SETUP_H
#define ELMO_CONFIG_SETUP_H

#include "ethercattype.h"
#include "config_loader.h"

/* Parsed once in main() from the JSON produced by tools/config_gui. */
extern ecat_config_t g_ecat_config;

/* PRE-OP -> SAFE-OP hook (assign to ec_slave[n].PO2SOconfig).
 * Reads this slave's mode of operation and RxPDO/TxPDO maps from g_ecat_config
 * and applies them via CoE SDOs. Returns 1 on success, 0 if no config found. */
int elmo_platinum_setup_from_config(uint16 slave);

#endif /* ELMO_CONFIG_SETUP_H */
