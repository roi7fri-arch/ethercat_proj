#ifndef DRIVE_PROFILE_H
#define DRIVE_PROFILE_H

/*
 * Drive-family plug-in interface.
 *
 * A "profile" is everything the master needs to know about one family of
 * drives that is *not* already covered by the CiA 402 standard: its vendor and
 * product identity, any start-up quirks, how to decode its manufacturer fault
 * codes, and what a simulated instance of it looks like.
 *
 * Everything else - PDO mapping, mode of operation, the enable ladder - is
 * standard and handled generically, so most families need no code at all: the
 * built-in "cia402_generic" profile plus a JSON file is enough. A family only
 * gets its own file under src/vendors/<vendor>/ when it genuinely deviates.
 *
 * SOEM-free by design. CoE access goes through drive_coe_ops_t, which the
 * application binds to SOEM (src/ecat/ecat_coe.c) and the tests bind to an
 * in-memory object dictionary. That is what makes vendor profiles unit-testable
 * on the host with no bus and no hardware.
 */

#include <stdint.h>
#include "config_loader.h"

/* ------------------------------------------------------------------------ */
/* CoE transport abstraction                                                 */
/* ------------------------------------------------------------------------ */
typedef struct {
   /* Return the working counter (>0 on success), matching SOEM's convention. */
   int (*sdo_write)(void *user, uint16_t slave, uint16_t index, uint8_t sub,
                    int complete_access, int size, const void *data);
   int (*sdo_read)(void *user, uint16_t slave, uint16_t index, uint8_t sub,
                   int complete_access, int *size, void *data);
   void *user;
} drive_coe_ops_t;

/* Everything a profile hook is given. */
typedef struct {
   const drive_coe_ops_t     *coe;
   uint16_t                   slave;   /* EtherCAT bus position */
   const ecat_slave_config_t *cfg;     /* this slave's parsed configuration */
} drive_ctx_t;

/* What a simulated instance of this family answers with, so the virtual bus in
 * sim_test can impersonate any supported drive (see slave_sim). */
typedef struct {
   uint32_t vendor_id;
   uint32_t product_code;
   uint32_t revision;
} drive_identity_t;

/* ------------------------------------------------------------------------ */
/* The profile                                                               */
/* ------------------------------------------------------------------------ */
typedef struct drive_profile {
   const char *id;             /* matches the JSON "profile" field           */
   const char *description;

   uint32_t        vendor_id;  /* 0 = unknown / do not check                 */
   const uint32_t *product_codes;
   int             n_product_codes;

   /* Optional hooks. Leave NULL to take the standard behaviour. */

   /* Run before the generic PRE-OP setup: last chance to unlock a
    * manufacturer-specific mode or clear a vendor latch. */
   int (*pre_setup)(const drive_ctx_t *ctx);

   /* Replaces the generic PRE-OP setup entirely. Almost never needed; use
    * pre_setup/post_setup instead so the family still benefits from the shared
    * PDO mapping code. */
   int (*setup)(const drive_ctx_t *ctx);

   /* Run after the generic PRE-OP setup completed. */
   int (*post_setup)(const drive_ctx_t *ctx);

   /* Decode a manufacturer error code (0x603F) to text. */
   const char *(*fault_string)(uint16_t error_code);

   /* Identity a simulated instance should report. */
   int (*sim_identity)(drive_identity_t *out);
} drive_profile_t;

/* ------------------------------------------------------------------------ */
/* Registry                                                                  */
/* ------------------------------------------------------------------------ */
#define DRIVE_PROFILE_MAX 16

/* Register a profile. The pointer must outlive the program (use a static).
 * Returns 0 on success, -1 if the table is full or the id already exists. */
int drive_profile_register(const drive_profile_t *profile);

/* Look up by id. Returns NULL when unknown. */
const drive_profile_t *drive_profile_find(const char *id);

/* Profile for a configured slave: its "profile" field if known, otherwise the
 * built-in generic CiA 402 profile. Never returns NULL. */
const drive_profile_t *drive_profile_for_slave(const ecat_slave_config_t *cfg);

/* The always-present fallback. */
const drive_profile_t *drive_profile_generic(void);

int                    drive_profile_count(void);
const drive_profile_t *drive_profile_at(int i);
void                   drive_profile_list(void);

/* Drop every registered profile (tests only). */
void drive_profile_reset(void);

/* ------------------------------------------------------------------------ */
/* Generic setup - the standard CiA 402 PRE-OP sequence                      */
/* ------------------------------------------------------------------------ */

/* Apply one PDO direction and assign the mapping objects to its sync manager.
 * map_base = first mapping object (0x1600 Rx / 0x1A00 Tx), sm_assign = the SM
 * PDO-assign object (0x1C12 / 0x1C13), per_obj = max entries per mapping
 * object. Lists longer than per_obj are split across consecutive objects.
 * Returns the accumulated working counter. */
int drive_apply_pdo_map(const drive_ctx_t *ctx,
                        uint16_t map_base, uint16_t sm_assign, int per_obj,
                        const ecat_pdo_entry_t *entries, int count);

/* Full standard sequence: start-up SDOs from the config, mode of operation,
 * then both PDO directions. Used by every family that has no `setup` hook. */
int drive_generic_setup(const drive_ctx_t *ctx);

/* Run the right sequence for this slave: pre_setup, then setup (or the generic
 * one), then post_setup. This is what the PRE-OP -> SAFE-OP hook calls.
 * Returns 1 on success, 0 on failure (SOEM's PO2SOconfig convention). */
int drive_profile_run_setup(const drive_profile_t *profile,
                            const drive_ctx_t *ctx);

/* Human-readable fault text: the family's decoder if it has one, otherwise the
 * standard CiA 402 error-code classes. */
const char *drive_fault_string(const drive_profile_t *profile, uint16_t code);

#endif /* DRIVE_PROFILE_H */
