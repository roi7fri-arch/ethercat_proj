#ifndef PDO_BIND_H
#define PDO_BIND_H

/*
 * Semantic access to the EtherCAT process image.
 *
 * The problem this solves: the master used to overlay fixed C structs
 * (out_ELMOt / in_ELMOt) on ec_slave[x].outputs/inputs. That forces the JSON
 * PDO map and the struct declaration to agree byte-for-byte, nothing checks it,
 * and a mismatch corrupts memory silently at 4 kHz. It also means every new
 * drive family needs a new struct and a new set of update_*() functions.
 *
 * Instead we resolve *semantic roles* ("controlword", "position actual")
 * against the PDO map that was actually configured, once, at start-up:
 *
 *      bind  ->  role -> bit offset + bit length in the live image
 *      run   ->  pdo_set_i32(io, PDO_SIG_POSITION_OFFSET, x)
 *
 * A missing or mis-sized signal is reported once, by name, before the RT loop
 * starts - instead of becoming a wild pointer later. The technique is the same
 * one telemetry.c already uses to decode its CSV columns.
 *
 * Deliberately free of SOEM: the caller passes the raw image pointers from
 * ec_slave[pos].outputs / .inputs, so this is unit-testable on the host.
 */

#include <stdint.h>
#include "config_loader.h"

/* ------------------------------------------------------------------------ */
/* Semantic roles                                                            */
/* ------------------------------------------------------------------------ */
typedef enum {
   /* --- command: master -> slave (RxPDO) --- */
   PDO_SIG_CONTROLWORD = 0,
   PDO_SIG_MODE_OF_OPERATION,
   PDO_SIG_TARGET_POSITION,
   PDO_SIG_TARGET_VELOCITY,
   PDO_SIG_TARGET_TORQUE,
   PDO_SIG_POSITION_OFFSET,
   PDO_SIG_VELOCITY_OFFSET,
   PDO_SIG_TORQUE_OFFSET,
   PDO_SIG_MAX_TORQUE,

   /* --- feedback: slave -> master (TxPDO) --- */
   PDO_SIG_STATUSWORD,
   PDO_SIG_MODE_DISPLAY,
   PDO_SIG_ERROR_CODE,
   PDO_SIG_POSITION_DEMAND,
   PDO_SIG_POSITION_ACTUAL,
   PDO_SIG_VELOCITY_DEMAND,
   PDO_SIG_VELOCITY_ACTUAL,
   PDO_SIG_TORQUE_DEMAND,
   PDO_SIG_TORQUE_ACTUAL,

   PDO_SIG__COUNT
} pdo_signal_t;

typedef enum {
   PDO_DIR_RX = 0,   /* master -> slave, writable  */
   PDO_DIR_TX = 1    /* slave -> master, read-only */
} pdo_dir_t;

/* ------------------------------------------------------------------------ */
/* Binding result                                                            */
/* ------------------------------------------------------------------------ */
typedef struct {
   int32_t bit_off;   /* offset within the direction's image, -1 = unmapped */
   uint8_t bit_len;
   uint8_t present;
} pdo_bind_t;

/* One axis' view of a slave's process image.
 *
 * A slave may carry more than one axis (an Elmo Platinum is two), in which case
 * the same object index appears once per axis in the PDO list. `axis_index`
 * selects which occurrence to bind, so a dual-axis drive needs no special case
 * anywhere above this layer. */
typedef struct {
   int            position;    /* EtherCAT bus position (ec_slave index) */
   int            axis_index;  /* 0-based axis within that slave          */
   uint8_t       *out_img;     /* RxPDO image, writable                   */
   int            out_bytes;
   const uint8_t *in_img;      /* TxPDO image, read-only                  */
   int            in_bytes;
   pdo_bind_t     sig[PDO_SIG__COUNT];
} pdo_io_t;

/* ------------------------------------------------------------------------ */
/* Binding                                                                   */
/* ------------------------------------------------------------------------ */

/* Number of axes a slave carries, derived from its PDO map: one per occurrence
 * of the controlword. An Elmo Platinum maps 0x6040 twice and so reports 2;
 * a single-axis drive reports 1. This is why no configuration field is needed
 * to describe multi-axis nodes. */
int pdo_axis_count(const ecat_slave_config_t *sc);

/* Resolve every role against the slave's configured PDO map.
 *
 * Signals absent from the map are simply marked not-present; that is legal
 * (a CST drive has no target position). Use pdo_require() afterwards to assert
 * the ones your control mode actually needs.
 *
 * Returns 0 on success, -1 on bad arguments (see pdo_bind_last_error()). */
int pdo_bind(pdo_io_t *io, const ecat_slave_config_t *sc,
             int position, int axis_index,
             void *out_img, int out_bytes,
             const void *in_img, int in_bytes);

/* Assert that every signal in `required` is bound and that the image is large
 * enough to hold it. Returns the number of problems found, having described
 * each one via the callback (pass NULL to print to stderr). */
int pdo_require(const pdo_io_t *io, const pdo_signal_t *required, int count);

/* The signals a given CiA 402 mode of operation needs in order to run. Returns
 * the count and points *out at a static table. */
int pdo_required_for_mode(int mode, const pdo_signal_t **out);

/* Diagnostics. */
const char *pdo_signal_name(pdo_signal_t sig);
pdo_dir_t   pdo_signal_dir(pdo_signal_t sig);
uint16_t    pdo_signal_default_index(pdo_signal_t sig);
const char *pdo_bind_last_error(void);
void        pdo_bind_print(const pdo_io_t *io);

/* ------------------------------------------------------------------------ */
/* Accessors - RT-safe: no allocation, no I/O, no syscalls.                  */
/*                                                                           */
/* Reads of an unmapped signal return 0; writes to one are ignored. That is   */
/* deliberate: a control loop must not branch on binding state every cycle,   */
/* and pdo_require() has already refused to start if something vital is       */
/* missing.                                                                   */
/* ------------------------------------------------------------------------ */
int64_t  pdo_get_i64(const pdo_io_t *io, pdo_signal_t sig);
int32_t  pdo_get_i32(const pdo_io_t *io, pdo_signal_t sig);
int16_t  pdo_get_i16(const pdo_io_t *io, pdo_signal_t sig);
uint16_t pdo_get_u16(const pdo_io_t *io, pdo_signal_t sig);
int8_t   pdo_get_i8 (const pdo_io_t *io, pdo_signal_t sig);

void pdo_set_i64(pdo_io_t *io, pdo_signal_t sig, int64_t value);
void pdo_set_i32(pdo_io_t *io, pdo_signal_t sig, int32_t value);
void pdo_set_i16(pdo_io_t *io, pdo_signal_t sig, int16_t value);
void pdo_set_u16(pdo_io_t *io, pdo_signal_t sig, uint16_t value);
void pdo_set_i8 (pdo_io_t *io, pdo_signal_t sig, int8_t value);

/* True when the role was found in the configured map. */
int pdo_has(const pdo_io_t *io, pdo_signal_t sig);

#endif /* PDO_BIND_H */
