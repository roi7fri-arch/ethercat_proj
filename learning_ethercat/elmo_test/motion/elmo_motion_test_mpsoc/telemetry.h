#ifndef TELEMETRY_H
#define TELEMETRY_H

/* Config-driven, MATLAB-friendly telemetry logger.
 *
 * Captures, once per cycle, the command image we send (RxPDO) and the feedback
 * image the slave reports (TxPDO), aligned by cycle number, plus loop timing
 * (wakeup jitter, send->receive roundtrip) and the Distributed Clock time.
 * After the run it writes one CSV per slave with named columns decoded straight
 * from the JSON PDO map (cmd_* = master->slave, fb_* = slave->master), so the
 * control engineers can load it in MATLAB and measure the gap (in cycles)
 * between a commanded value and when the slave's feedback reflects it, as well
 * as following error, jitter, etc.
 *
 * Deliberately SOEM-free (plain <stdint.h>): the caller passes the live process
 * image pointers from ec_slave[pos].outputs/inputs. This keeps it unit-testable
 * on the host.
 */

#include <stdint.h>
#include "config_loader.h"

typedef struct {
    int position;                    /* EtherCAT position (ec_slave index)   */
    const uint8_t *out_img;          /* live RxPDO image (master -> slave)   */
    int out_bytes;
    const uint8_t *in_img;           /* live TxPDO image (slave -> master)   */
    int in_bytes;
    const ecat_slave_config_t *sc;   /* PDO map for decode + column names    */
    uint8_t *out_buf;                /* capacity * out_bytes snapshots       */
    uint8_t *in_buf;                 /* capacity * in_bytes  snapshots       */

    /* Optional master-computed following error = demand(0x6062) - actual(0x6064),
     * emitted as a "following_error_pos" column when both are in the TxPDO. */
    int fe_present;
    int fe_demand_bit, fe_demand_len;
    int fe_actual_bit, fe_actual_len;
} telemetry_slave_t;

typedef struct {
    int capacity;                    /* max samples                          */
    int count;                       /* samples captured so far              */
    int slave_count;                 /* registered slaves                    */
    const ecat_config_t *cfg;

    int32_t *cycle;                  /* [capacity] loop index                */
    int64_t *t_ns;                   /* [capacity] absolute monotonic ns     */
    int32_t *wkc;                    /* [capacity] working counter           */
    int64_t *latency_ns;             /* [capacity] wakeup jitter             */
    int64_t *exec_ns;                /* [capacity] send->receive roundtrip   */
    int64_t *dc_time;                /* [capacity] ec_DCtime                 */

    telemetry_slave_t slaves[ECAT_CFG_MAX_SLAVES];
} telemetry_t;

/* Allocate buffers for up to `capacity` samples (pass number_of_cycles + 1).
 * Returns 0 on success, -1 on failure (see telemetry_last_error()). */
int telemetry_init(telemetry_t *t, const ecat_config_t *cfg, int capacity);

/* Register a slave's live process-image pointers (call once, after
 * ec_config_map, before the cyclic loop). Returns 0 on success. */
int telemetry_add_slave(telemetry_t *t, int position,
                        const void *out_img, int out_bytes,
                        const void *in_img, int in_bytes);

/* Snapshot all registered images plus timing for this cycle. RT-safe:
 * only memcpy + scalar stores, no allocation or I/O. */
void telemetry_sample(telemetry_t *t, int32_t cycle, int64_t t_ns, int32_t wkc,
                      int64_t latency_ns, int64_t exec_ns, int64_t dc_time);

/* Write one CSV per slave: "<prefix>_slave<pos>.csv". Returns 0 on success. */
int telemetry_write(const telemetry_t *t, const char *prefix);

void telemetry_free(telemetry_t *t);
const char *telemetry_last_error(void);

#endif /* TELEMETRY_H */
