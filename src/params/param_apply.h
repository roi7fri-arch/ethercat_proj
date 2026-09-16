#ifndef PARAM_APPLY_H
#define PARAM_APPLY_H

/*
 * Writing a parameter set to drives, and reading one back.
 *
 * Download   write every parameter over CoE, optionally reading each back to
 *            confirm the drive took the value it was given.
 * Upload     read the current value of every parameter in the set, so an
 *            engineer can capture a drive's live tuning into a file.
 *
 * CoE goes through drive_coe_ops_t rather than SOEM, so this engine is exactly
 * as testable as the vendor profiles are: the whole thing runs on the host
 * against an in-memory object dictionary. Pushing *files* needs a real stack
 * and lives in src/ecat/ecat_param.h.
 */

#include <stdint.h>
#include "param_set.h"
#include "drive_profile.h"

typedef enum {
   PARAM_OK = 0,
   PARAM_SKIPPED,        /* slave not present on the bus                    */
   PARAM_WRITE_FAILED,   /* SDO download refused                            */
   PARAM_READ_FAILED,    /* SDO upload refused during verification          */
   PARAM_MISMATCH        /* drive reported back a different value           */
} param_status_t;

typedef struct {
   int            entry;      /* index into param_set_t::entries            */
   int            slave;
   param_status_t status;
   int            wkc;
   uint32_t       readback;   /* meaningful when the value was read         */
} param_result_t;

typedef struct {
   param_result_t results[PARAM_MAX];
   int count;
   int ok;
   int failed;
   int mismatched;
} param_report_t;

/* Write every parameter whose `slave` is 0 (meaning all) or in
 * [1, slave_count].
 *
 * Verification is per-parameter and on by default. A drive that silently
 * clamps a value to its own legal range reports success on the write and a
 * different value on the read - exactly what a commissioning engineer needs to
 * see - so that is reported as a mismatch rather than hidden.
 *
 * Returns the number of problems (0 = everything applied and verified). */
int param_download(const param_set_t *set, const drive_coe_ops_t *coe,
                   int slave_count, param_report_t *report);

/* Read the current value of every parameter into `out`, a copy of `set` with
 * the values replaced. Returns the number that could not be read. */
int param_upload(const param_set_t *set, const drive_coe_ops_t *coe,
                 int slave_count, param_set_t *out, param_report_t *report);

void        param_report_print(const param_set_t *set, const param_report_t *r);
const char *param_status_name(param_status_t s);

#endif /* PARAM_APPLY_H */
