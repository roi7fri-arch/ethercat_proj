#ifndef CONFIG_LOADER_H
#define CONFIG_LOADER_H

/*
 * Reads the JSON produced by tools/config_gui and exposes it as plain C structs.
 * Deliberately free of any SOEM/EtherCAT dependency so it can be unit-tested on
 * a host with plain gcc. The EtherCAT side consumes ecat_config_t on init.
 */

#include <stdint.h>

#define ECAT_CFG_MAX_SLAVES       16
#define ECAT_CFG_MAX_PDO_ENTRIES  64
#define ECAT_CFG_NAME_LEN         64
#define ECAT_CFG_IFACE_LEN        32

typedef struct {
    uint16_t index;     /* CoE object index, e.g. 0x6040 */
    uint8_t  subindex;
    uint8_t  bitlen;    /* 1..64 */
    char     name[ECAT_CFG_NAME_LEN];
} ecat_pdo_entry_t;

typedef struct {
    int  position;      /* 1-based bus position */
    char name[ECAT_CFG_NAME_LEN];
    int  mode_of_operation;                     /* value written to 0x6060 */
    uint32_t expected_vendor_id;                /* 0 = don't verify (SII 0x0008) */
    uint32_t expected_product_code;             /* 0 = don't verify (SII 0x000A) */
    uint32_t expected_revision;                 /* 0 = don't verify (SII 0x000C) */
    ecat_pdo_entry_t rxpdo[ECAT_CFG_MAX_PDO_ENTRIES];
    int  rxpdo_count;
    ecat_pdo_entry_t txpdo[ECAT_CFG_MAX_PDO_ENTRIES];
    int  txpdo_count;
} ecat_slave_config_t;

typedef struct {
    char interface[ECAT_CFG_IFACE_LEN];
    char redundant_interface[ECAT_CFG_IFACE_LEN]; /* "" = no cable redundancy (2nd NIC) */
    int  cycle_time_us;
    int  number_of_cycles;                      /* 0 = run forever */
    int  distributed_clock;                     /* bool: activate SYNC0 */
    int  sync0_shift_us;
    int  sync_kp_div;                           /* DC phase-lock PI: proportional divisor (larger = softer) */
    int  sync_ki_div;                           /* DC phase-lock PI: integral divisor (larger = softer) */
    int  auto_recovery;                         /* bool: monitor + auto-recover lost/errored slaves while in OP */
    int  auto_recovery_timeout_us;              /* per-slave reconfig/recover timeout (SOEM EC_TIMEOUTMON) */
} ecat_network_config_t;

typedef struct {
    int version;
    ecat_network_config_t network;
    ecat_slave_config_t   slaves[ECAT_CFG_MAX_SLAVES];
    int slave_count;
} ecat_config_t;

/* Load & validate a JSON config file. Returns 0 on success, negative on error. */
int ecat_config_load_file(const char *path, ecat_config_t *cfg);

/* Parse & validate from an in-memory JSON string. */
int ecat_config_load_string(const char *json, ecat_config_t *cfg);

/* Human-readable message describing the last failure. */
const char *ecat_config_last_error(void);

/* Find a slave by 1-based bus position; returns NULL if not present. */
const ecat_slave_config_t *ecat_config_find_slave(const ecat_config_t *cfg, int position);

/* Pack an entry into the CoE PDO mapping value: index<<16 | subindex<<8 | bitlen. */
uint32_t ecat_pdo_map_value(const ecat_pdo_entry_t *e);

/* Print the parsed configuration (diagnostics). */
void ecat_config_print(const ecat_config_t *cfg);

#endif /* CONFIG_LOADER_H */
