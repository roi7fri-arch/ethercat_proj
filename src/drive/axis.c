/** \file
 * \brief Generic CiA 402 axis: process-image binding + drive state machine.
 *
 * See axis.h. Nothing in here knows which vendor built the drive or which bus
 * position it sits at; both come from the parsed configuration.
 */

#include "axis.h"

#include <stdio.h>
#include <string.h>

int axis_init(axis_t *ax, const ecat_slave_config_t *sc,
              int position, int axis_index,
              void *out_img, int out_bytes,
              const void *in_img, int in_bytes)
{
   if (!ax || !sc)
      return -1;

   memset(ax, 0, sizeof(*ax));
   ax->position   = position;
   ax->axis_index = axis_index;
   ax->mode       = sc->mode_of_operation;
   ax->state      = CIA402_STATE_UNKNOWN;
   ax->prev_state = CIA402_STATE_UNKNOWN;

   /* Only suffix the name when the node really carries several axes, so a
    * single-axis drive keeps the plain name from the config. The precision
    * bounds the copy so the suffix always fits. */
   if (pdo_axis_count(sc) > 1)
      snprintf(ax->name, sizeof(ax->name), "%.*s:ax%d",
               (int)(sizeof(ax->name) - 8), sc->name, axis_index);
   else
      snprintf(ax->name, sizeof(ax->name), "%.*s",
               (int)(sizeof(ax->name) - 1), sc->name);

   return pdo_bind(&ax->io, sc, position, axis_index,
                   out_img, out_bytes, in_img, in_bytes);
}

int axis_validate(const axis_t *ax)
{
   const pdo_signal_t *req;
   int n;

   if (!ax)
      return 1;

   n = pdo_required_for_mode(ax->mode, &req);
   return pdo_require(&ax->io, req, n);
}

int axis_set_add_slave(axis_set_t *set, const ecat_slave_config_t *sc,
                       int position,
                       void *out_img, int out_bytes,
                       const void *in_img, int in_bytes)
{
   int n_axes, i;

   if (!set || !sc)
      return -1;

   n_axes = pdo_axis_count(sc);
   if (n_axes < 1)
   {
      fprintf(stderr,
              "slave %d '%s': no controlword in the RxPDO map - not a drive?\n",
              position, sc->name);
      return -1;
   }

   if (set->count + n_axes > AXIS_MAX)
   {
      fprintf(stderr, "slave %d '%s': more than %d axes configured\n",
              position, sc->name, AXIS_MAX);
      return -1;
   }

   for (i = 0; i < n_axes; i++)
   {
      if (axis_init(&set->axis[set->count], sc, position, i,
                    out_img, out_bytes, in_img, in_bytes) != 0)
         return -1;
      set->count++;
   }

   return n_axes;
}

int axis_set_validate(const axis_set_t *set)
{
   int problems = 0;
   int i;

   if (!set)
      return 1;

   for (i = 0; i < set->count; i++)
      problems += axis_validate(&set->axis[i]);

   return problems;
}

void axis_print(const axis_t *ax)
{
   if (!ax)
      return;

   printf("axis '%s' @ slave %d.%d  mode %d (%s)\n",
          ax->name, ax->position, ax->axis_index,
          ax->mode, cia402_mode_name(ax->mode));
   pdo_bind_print(&ax->io);
}

/* ------------------------------------------------------------------------ */
/* Cyclic                                                                    */
/* ------------------------------------------------------------------------ */

void axis_read(axis_t *ax)
{
   cia402_state_t st;

   if (!ax)
      return;

   ax->statusword      = pdo_get_u16(&ax->io, PDO_SIG_STATUSWORD);
   ax->position_actual = pdo_get_i32(&ax->io, PDO_SIG_POSITION_ACTUAL);
   ax->velocity_actual = pdo_get_i32(&ax->io, PDO_SIG_VELOCITY_ACTUAL);
   ax->torque_actual   = pdo_get_i16(&ax->io, PDO_SIG_TORQUE_ACTUAL);
   ax->error_code      = pdo_get_u16(&ax->io, PDO_SIG_ERROR_CODE);

   st = cia402_state(ax->statusword);
   ax->prev_state = ax->state;
   ax->state      = st;

   if (st != ax->prev_state)
   {
      ax->cycles_in_state = 0;
      if (st == CIA402_STATE_FAULT || st == CIA402_STATE_FAULT_REACTION_ACTIVE)
         ax->fault_count++;
   }
   else if (ax->cycles_in_state < 0xFFFFFFFFu)
   {
      ax->cycles_in_state++;
   }
}

static void axis_send_controlword(axis_t *ax, uint16_t cw)
{
   ax->controlword = cw;
   pdo_set_u16(&ax->io, PDO_SIG_CONTROLWORD, cw);
}

void axis_enable_step(axis_t *ax)
{
   if (!ax)
      return;

   axis_send_controlword(ax,
      cia402_next_controlword(ax->statusword, ax->controlword));
}

void axis_safe_stop(axis_t *ax)
{
   if (!ax)
      return;

   axis_send_controlword(ax, cia402_safe_stop_controlword(ax->statusword));
   axis_hold(ax);
}

void axis_hold(axis_t *ax)
{
   if (!ax)
      return;

   /* Command = feedback, so the moment the drive enables there is no step.
    * Offsets are the cyclic-mode command path used by this project; target
    * position is written too when the map carries it. */
   pdo_set_i32(&ax->io, PDO_SIG_POSITION_OFFSET,   ax->position_actual);
   pdo_set_i32(&ax->io, PDO_SIG_TARGET_POSITION,   ax->position_actual);
   pdo_set_i32(&ax->io, PDO_SIG_VELOCITY_OFFSET,   0);
   pdo_set_i32(&ax->io, PDO_SIG_TARGET_VELOCITY,   0);
   pdo_set_i16(&ax->io, PDO_SIG_TORQUE_OFFSET,     0);
   pdo_set_i16(&ax->io, PDO_SIG_TARGET_TORQUE,     0);
}

void axis_write_setpoint(axis_t *ax, int32_t pos, int32_t vel, int16_t torque)
{
   if (!ax)
      return;

   pdo_set_i32(&ax->io, PDO_SIG_POSITION_OFFSET, pos);
   pdo_set_i32(&ax->io, PDO_SIG_VELOCITY_OFFSET, vel);
   pdo_set_i16(&ax->io, PDO_SIG_TORQUE_OFFSET,   torque);

   /* Drives configured with the plain target objects instead of the offsets
    * get the same values; whichever is not mapped is ignored. */
   pdo_set_i32(&ax->io, PDO_SIG_TARGET_POSITION, pos);
   pdo_set_i32(&ax->io, PDO_SIG_TARGET_VELOCITY, vel);
   pdo_set_i16(&ax->io, PDO_SIG_TARGET_TORQUE,   torque);
}

/* ------------------------------------------------------------------------ */
/* Queries                                                                   */
/* ------------------------------------------------------------------------ */

int axis_is_operational(const axis_t *ax)
{
   return ax && ax->state == CIA402_STATE_OPERATION_ENABLED;
}

int axis_has_fault(const axis_t *ax)
{
   return ax && (ax->state == CIA402_STATE_FAULT ||
                 ax->state == CIA402_STATE_FAULT_REACTION_ACTIVE);
}

int axis_state_changed(const axis_t *ax)
{
   return ax && (ax->state != ax->prev_state);
}

const char *axis_state_name(const axis_t *ax)
{
   return ax ? cia402_state_name(ax->state) : "?";
}
