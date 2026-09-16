#ifndef AXIS_H
#define AXIS_H

/*
 * A single controllable axis.
 *
 * This is the object the control loop talks to. It owns nothing EtherCAT- or
 * vendor-specific: a process-image binding (src/pdo) plus the CiA 402 state
 * machine (src/drive/cia402) is all an axis is.
 *
 * It replaces the per-topology, per-vendor functions the loop used to call -
 * update_el_outputs(), update_tr_outputs(), update_platinum_outputs() - and the
 * ELMO_EL / ELMO_TR macros that hard-coded which bus position was which axis.
 *
 * Because pdo_axis_count() derives the axis count from the PDO map, a
 * dual-axis node (Elmo Platinum) and two single-axis nodes (Copley, Maxon)
 * produce the same array of axis_t and the loop cannot tell them apart.
 *
 * SOEM-free: the caller hands over the raw image pointers from
 * ec_slave[pos].outputs / .inputs.
 */

#include <stdint.h>
#include "cia402.h"
#include "pdo_bind.h"

#define AXIS_MAX       32
#define AXIS_NAME_LEN  64

typedef struct {
   /* identity */
   int      position;                 /* EtherCAT bus position               */
   int      axis_index;               /* 0-based axis within that slave      */
   char     name[AXIS_NAME_LEN];
   int      mode;                     /* CiA 402 mode of operation           */

   /* process image binding */
   pdo_io_t io;

   /* runtime state, refreshed by axis_read() */
   uint16_t statusword;
   uint16_t controlword;              /* what we sent last cycle             */
   cia402_state_t state;
   cia402_state_t prev_state;
   int32_t  position_actual;
   int32_t  velocity_actual;
   int16_t  torque_actual;
   uint16_t error_code;

   /* bookkeeping */
   uint32_t cycles_in_state;
   uint32_t fault_count;
} axis_t;

typedef struct {
   axis_t axis[AXIS_MAX];
   int    count;
} axis_set_t;

/* ------------------------------------------------------------------------ */
/* Setup                                                                     */
/* ------------------------------------------------------------------------ */

/* Bind one axis to a slave's process image. `axis_index` selects which set of
 * objects to use on a multi-axis node. Returns 0 on success. */
int axis_init(axis_t *ax, const ecat_slave_config_t *sc,
              int position, int axis_index,
              void *out_img, int out_bytes,
              const void *in_img, int in_bytes);

/* Check that the PDO map carries everything this axis' mode of operation
 * needs. Returns the number of problems (0 = good); each is described on
 * stderr. Call before entering the cyclic loop, never inside it. */
int axis_validate(const axis_t *ax);

/* Append every axis a slave carries (1, or 2 for a dual-axis drive) to the set.
 * Returns the number of axes added, or -1 on error. */
int axis_set_add_slave(axis_set_t *set, const ecat_slave_config_t *sc,
                       int position,
                       void *out_img, int out_bytes,
                       const void *in_img, int in_bytes);

/* Validate every axis in the set. Returns total problems found. */
int axis_set_validate(const axis_set_t *set);

void axis_print(const axis_t *ax);

/* ------------------------------------------------------------------------ */
/* Cyclic use - RT-safe: no allocation, no I/O, no syscalls.                 */
/* ------------------------------------------------------------------------ */

/* Latch statusword and feedback from the input image. Call once per cycle,
 * after ec_receive_processdata(). */
void axis_read(axis_t *ax);

/* Advance the CiA 402 enable ladder by one step and write the resulting
 * controlword. Call every cycle; the axis climbs to Operation Enabled on its
 * own and holds there. */
void axis_enable_step(axis_t *ax);

/* Command a stop from any state (quick stop where possible, disable voltage
 * otherwise). Use on working-counter loss, e-stop or link failure. */
void axis_safe_stop(axis_t *ax);

/* Seed the command values from the current feedback so that enabling the drive
 * does not produce a step. Call every cycle while the axis is not yet
 * operational. */
void axis_hold(axis_t *ax);

/* Write a cyclic setpoint. Which fields are used depends on the mode and on
 * what the PDO map carries; unmapped ones are ignored. */
void axis_write_setpoint(axis_t *ax, int32_t pos, int32_t vel, int16_t torque);

/* ------------------------------------------------------------------------ */
/* Queries                                                                   */
/* ------------------------------------------------------------------------ */
int  axis_is_operational(const axis_t *ax);
int  axis_has_fault(const axis_t *ax);
int  axis_state_changed(const axis_t *ax);   /* true on the cycle it changed */
const char *axis_state_name(const axis_t *ax);

#endif /* AXIS_H */
