#ifndef ECAT_DIAG_H
#define ECAT_DIAG_H

/* Per-slave EtherCAT diagnostics: AL state + ESC error counters.
 *
 * Reads the standard EtherCAT Slave Controller error registers (0x0300..0x0313)
 * plus the AL status, giving TwinCAT-style link-quality visibility: a rising
 * CRC / lost-link counter flags a degrading cable long before the slave drops
 * out of OP. Uses SOEM directly (ec_FPRD on the slave's configured address), so
 * unlike telemetry.c it is NOT host-testable without a slave that answers
 * register reads. Intended for low-rate use (start-up baseline, shutdown
 * summary, or the off-RT monitor thread) - never per RT cycle.
 */

#include <stdint.h>

#include "config_loader.h"

#define ECAT_DIAG_MAX_PORTS 4

typedef struct {
    int      position;                         /* ec_slave index (1-based)          */
    uint16_t configadr;                        /* configured station address        */
    uint16_t al_state;                         /* AL state (Init/PreOp/SafeOp/OP+err)*/
    uint16_t al_status_code;                   /* AL status code (0 = no error)      */
    uint8_t  rx_invalid[ECAT_DIAG_MAX_PORTS];  /* invalid-frame counter  (0x0300 lo) */
    uint8_t  rx_error[ECAT_DIAG_MAX_PORTS];    /* RX CRC error counter    (0x0300 hi) */
    uint8_t  fwd_error[ECAT_DIAG_MAX_PORTS];   /* forwarded RX error      (0x0308)    */
    uint8_t  lost_link[ECAT_DIAG_MAX_PORTS];   /* lost-link counter       (0x0310)    */
    uint8_t  proc_error;                       /* EPU processing errors   (0x030C)    */
    uint8_t  pdi_error;                        /* PDI error counter       (0x030D)    */
    int      read_ok;                          /* 1 if the register read succeeded    */
} ecat_diag_slave_t;

/* Read AL state + ESC error counters for one slave (1-based position).
 * Returns 0 on success, -1 if the register read failed (wkc <= 0). */
int ecat_diag_read(int position, ecat_diag_slave_t *out);

/* Read slaves 1..ec_slavecount into out[] (up to cap entries). Calls
 * ec_readstate() first so AL state/code are current. Returns count read. */
int ecat_diag_scan(ecat_diag_slave_t *out, int cap);

/* Print a one-line-per-slave table (AL state/code + error-counter totals). */
void ecat_diag_print(const char *title, const ecat_diag_slave_t *d, int n);

/* Returns non-zero if any slave shows an AL error or a non-zero error counter. */
int ecat_diag_has_errors(const ecat_diag_slave_t *d, int n);

/* Drain SOEM's error list (CoE Emergency / mailbox / packet errors that slaves
 * pushed asynchronously). Prints each formatted line. Returns the count drained
 * (0 if none pending). Safe to call from the low-rate monitor path. */
int ecat_diag_drain_errors(void);

/* Verify the live bus identity (SII VendorID/ProductCode/RevisionNo) against the
 * expected_* fields in cfg. Fields set to 0 are skipped. Must run after
 * ec_config_init() has populated ec_slave[].eep_man/eep_id/eep_rev. Prints each
 * mismatch and returns the number of mismatches (0 = all verified/ok). */
int ecat_diag_verify_identity(const ecat_config_t *cfg);

/* ------------------------------------------------------------------------- */
/* Topology / link-state (TwinCAT "online topology" view).                   */
/* ------------------------------------------------------------------------- */
typedef struct {
    int      position;       /* ec_slave index (1-based)                       */
    uint16_t configadr;      /* configured station address                     */
    uint8_t  topology;       /* number of active links on this slave (1..3)    */
    uint8_t  activeports;    /* bitmap ....3210 of ports with a live link       */
    uint16_t parent;         /* upstream slave number (0 = master)             */
    uint8_t  parentport;     /* port on the parent this slave hangs off         */
    int      has_dc;         /* 1 if the slave is DC-capable                    */
} ecat_topo_t;

/* Fill out[] with the link topology of slaves 1..ec_slavecount (up to cap).
 * Pure read of ec_slave[] populated by ec_config_init() - no bus I/O. Returns
 * the number of entries written. */
int ecat_diag_topology_scan(ecat_topo_t *out, int cap);

/* Print one line per slave: active ports, link count, parent - so a cable
 * pulled mid-chain (branch turning into end-of-line) is immediately visible. */
void ecat_diag_topology_print(const char *title, const ecat_topo_t *t, int n);

/* ------------------------------------------------------------------------- */
/* Distributed-clock sync-window monitoring (ESC reg 0x092C).                */
/* ------------------------------------------------------------------------- */
#define ECAT_DC_SYNC_WINDOW_NS 1000u   /* default acceptable |DC deviation|    */

typedef struct {
    int      position;       /* ec_slave index (1-based)                       */
    uint16_t configadr;      /* configured station address                     */
    int      has_dc;         /* ec_slave[pos].hasdc (informational)            */
    uint32_t diff_ns;        /* |System Time Difference| in ns (0x092C bits 0..30) */
    int      ahead;          /* 1 = local clock ahead of the reference clock    */
    int      read_ok;        /* 1 if the register read succeeded                */
} ecat_dc_diff_t;

/* Read the DC System Time Difference (reg 0x092C) for one slave (1-based).
 * Returns 0 on success, -1 if the register read failed. */
int ecat_diag_dc_read(int position, ecat_dc_diff_t *out);

/* Read the DC deviation for slaves 1..ec_slavecount (up to cap). Returns the
 * number of entries written. */
int ecat_diag_dc_scan(ecat_dc_diff_t *out, int cap);

/* Print one line per slave with its deviation and an in/out-of-window flag. */
void ecat_diag_dc_print(const char *title, const ecat_dc_diff_t *d, int n,
                        uint32_t window_ns);

/* Returns the number of slaves whose |deviation| exceeds window_ns (0 = the
 * whole bus is locked inside the sync window). Skips slaves that failed to read. */
int ecat_diag_dc_out_of_window(const ecat_dc_diff_t *d, int n, uint32_t window_ns);

#endif /* ECAT_DIAG_H */
