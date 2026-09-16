#ifndef PARAM_SET_H
#define PARAM_SET_H

/*
 * Drive parameter sets.
 *
 * A parameter set is a list of CoE objects with values, plus an optional list
 * of files to push over FoE. It is deliberately a *separate* document from the
 * bus configuration (config_loader.h):
 *
 *   ethercat_config.json   what the bus looks like - PDO maps, modes, identity.
 *                          Read by the master every time it starts.
 *   <name>.params.json     what the drives should be tuned to - gains, limits,
 *                          feedback scaling, homing offsets. Written to the
 *                          drives once, from the engineer's PC, and then stored
 *                          in the drive's own non-volatile memory.
 *
 * Keeping them apart matters: the bus config is deployment data that ships with
 * the machine, while a parameter set is commissioning data that is edited,
 * downloaded, read back and compared far more often.
 *
 * SOEM-free (plain <stdint.h>) so it can be unit-tested on the host.
 */

#include <stdint.h>

#define PARAM_MAX          256
#define PARAM_FILES_MAX    16
#define PARAM_NAME_LEN     64
#define PARAM_GROUP_LEN    48
#define PARAM_UNIT_LEN     24
#define PARAM_PATH_LEN     256

/* How to interpret the 32-bit payload. Determines the SDO size and how the GUI
 * and the verify step format and compare values. */
typedef enum {
   PARAM_TYPE_U8 = 0,
   PARAM_TYPE_I8,
   PARAM_TYPE_U16,
   PARAM_TYPE_I16,
   PARAM_TYPE_U32,
   PARAM_TYPE_I32,
   PARAM_TYPE_F32,
   PARAM_TYPE__COUNT
} param_type_t;

typedef struct {
   int      slave;                     /* 1-based bus position, 0 = all slaves */
   uint16_t index;
   uint8_t  subindex;
   param_type_t type;
   uint32_t value;                     /* raw little-endian payload           */
   int      verify;                    /* read back and compare after writing */
   char     name[PARAM_NAME_LEN];
   char     group[PARAM_GROUP_LEN];    /* free-form, for grouping in the GUI  */
   char     unit[PARAM_UNIT_LEN];
} param_entry_t;

/* A file to push to a drive over FoE (firmware, a gain table, an ESI blob...). */
typedef struct {
   int      slave;                     /* 1-based bus position                */
   char     path[PARAM_PATH_LEN];      /* local file on the engineer's PC     */
   char     remote_name[PARAM_NAME_LEN]; /* name used in the FoE transaction  */
   uint32_t password;
   int      use_boot_state;            /* take the slave to BOOT first        */
} param_file_t;

typedef struct {
   int version;
   char name[PARAM_NAME_LEN];
   char description[PARAM_NAME_LEN * 2];

   param_entry_t entries[PARAM_MAX];
   int entry_count;

   param_file_t files[PARAM_FILES_MAX];
   int file_count;
} param_set_t;

/* ------------------------------------------------------------------------ */
/* Load / save                                                               */
/* ------------------------------------------------------------------------ */
int param_set_load_file(const char *path, param_set_t *set);
int param_set_load_string(const char *json, param_set_t *set);

/* Serialise back to JSON. Returns a malloc'd string the caller must free, or
 * NULL on failure. */
char *param_set_to_json(const param_set_t *set);

/* Write the set to disk. Returns 0 on success. */
int param_set_save_file(const char *path, const param_set_t *set);

const char *param_set_last_error(void);

/* ------------------------------------------------------------------------ */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------ */
int         param_type_size(param_type_t t);     /* bytes on the wire: 1,2,4 */
const char *param_type_name(param_type_t t);
int         param_type_from_name(const char *name, param_type_t *out);

/* Format a raw payload the way its type says it should read (signed, unsigned
 * or float). `cap` should be at least 32. */
void param_format_value(char *dst, int cap, param_type_t type, uint32_t raw);

/* Parse a textual value ("1000", "-5", "0x3E8", "1.25") into the raw payload
 * for the given type. Returns 0 on success. */
int param_parse_value(const char *text, param_type_t type, uint32_t *out);

/* Compare two raw payloads as their type, masking off the bytes that are not
 * actually transferred for 1- and 2-byte objects. Returns non-zero if equal. */
int param_values_equal(param_type_t type, uint32_t a, uint32_t b);

void param_set_print(const param_set_t *set);

#endif /* PARAM_SET_H */
