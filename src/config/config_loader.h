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
#define ECAT_CFG_MAX_SDO_CMDS     32
#define ECAT_CFG_NAME_LEN         64
#define ECAT_CFG_IFACE_LEN        32

/* Vendor-neutral defaults for the PDO mapping / sync-manager assign objects.
 * Standard CiA402 drives (Elmo Platinum/Gold, ACS, ...) use these; a slave may
 * override them from the JSON when a family differs. */
#define ECAT_CFG_DEF_RXMAP_BASE   0x1600
#define ECAT_CFG_DEF_TXMAP_BASE   0x1A00
#define ECAT_CFG_DEF_SM2_ASSIGN   0x1C12
#define ECAT_CFG_DEF_SM3_ASSIGN   0x1C13
#define ECAT_CFG_DEF_MAP_PER_OBJ  8

typedef struct {
    uint16_t index;     /* CoE object index, e.g. 0x6040 */
    uint8_t  subindex;
    uint8_t  bitlen;    /* 1..64 */
    char     name[ECAT_CFG_NAME_LEN];
} ecat_pdo_entry_t;

/* A single vendor-specific PRE-OP init command (CoE SDO download). Lets each
 * drive family carry its own start-up parameters as data instead of code. */
typedef struct {
    uint16_t index;     /* CoE object index */
    uint8_t  subindex;
    uint8_t  size;      /* payload bytes: 1, 2 or 4 */
    uint32_t value;     /* little-endian value written */
    char     comment[ECAT_CFG_NAME_LEN];
} ecat_sdo_cmd_t;

typedef struct {
    int  position;      /* 1-based bus position */
    char name[ECAT_CFG_NAME_LEN];
    char profile[ECAT_CFG_NAME_LEN];            /* drive profile id, e.g. "elmo_platinum" (informational) */
    int  mode_of_operation;                     /* value written to 0x6060 */
    uint32_t expected_vendor_id;                /* 0 = don't verify (SII 0x0008) */
    uint32_t expected_product_code;             /* 0 = don't verify (SII 0x000A) */
    uint32_t expected_revision;                 /* 0 = don't verify (SII 0x000C) */
    uint16_t rxpdo_map_base;                    /* first Rx mapping object (default 0x1600) */
    uint16_t txpdo_map_base;                    /* first Tx mapping object (default 0x1A00) */
    uint16_t sm2_assign;                        /* SM2 PDO-assign object (default 0x1C12) */
    uint16_t sm3_assign;                        /* SM3 PDO-assign object (default 0x1C13) */
    int  map_entries_per_obj;                   /* max entries per mapping object (default 8) */
    ecat_pdo_entry_t rxpdo[ECAT_CFG_MAX_PDO_ENTRIES];
    int  rxpdo_count;
    ecat_pdo_entry_t txpdo[ECAT_CFG_MAX_PDO_ENTRIES];
    int  txpdo_count;
    ecat_sdo_cmd_t startup_sdo[ECAT_CFG_MAX_SDO_CMDS];
    int  startup_sdo_count;
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
    int  verify_identity;                       /* bool: check each slave's vendor/product/rev vs config (default on) */
} ecat_network_config_t;

typedef struct {
    int version;
    ecat_network_config_t network;
    ecat_slave_config_t   slaves[ECAT_CFG_MAX_SLAVES];
    int slave_count;
} ecat_config_t;

/* The configuration the application parsed at start-up. It lives here rather
 * than in a vendor-specific file so any layer can read it without pulling in a
 * particular drive family. */
extern ecat_config_t g_ecat_config;

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
