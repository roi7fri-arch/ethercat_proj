/** \file
 * \brief Motion sources: the safe "hold" default and the ECD_Motor model.
 */

#include "motion.h"
#include "ECD_Motor.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------ */
/* hold - command the measured position, zero velocity and torque            */
/* ------------------------------------------------------------------------ */
static int hold_init(motion_source_t *m, int n_axes, int cycle_time_us)
{
   (void)m; (void)n_axes; (void)cycle_time_us;
   return 0;
}

static void hold_step(motion_source_t *m, uint32_t cycle,
                      const motion_feedback_t *fb, motion_setpoint_t *sp,
                      int n_axes)
{
   int i;

   (void)m; (void)cycle;
   for (i = 0; i < n_axes; i++)
   {
      sp[i].position = fb ? fb[i].position_actual : 0;
      sp[i].velocity = 0;
      sp[i].torque   = 0;
   }
}

static motion_source_t g_hold = { "hold", NULL, hold_init, hold_step, NULL };

motion_source_t *motion_hold_create(void) { return &g_hold; }

/* ------------------------------------------------------------------------ */
/* ecd - the Simulink-generated ECD_Motor model                              */
/* ------------------------------------------------------------------------ */
typedef struct {
   RT_MODEL_ECD_Motor_T model;
   ExtU_ECD_Motor_T     in;
   ExtY_ECD_Motor_T     out;
   int                  started;
} ecd_state_t;

static ecd_state_t g_ecd_state;
static uint16_t    g_ecd_mode = 8;   /* default: cyclic synchronous position */

void motion_ecd_set_mode(uint16_t mode_of_operation)
{
   g_ecd_mode = mode_of_operation;
}

static int ecd_init(motion_source_t *m, int n_axes, int cycle_time_us)
{
   ecd_state_t *s = (ecd_state_t *)m->state;

   (void)n_axes; (void)cycle_time_us;

   /* The generated model owns its own storage; allocate it here, once, before
    * the RT loop starts so step() never touches the allocator. */
   s->model.blockIO = malloc(sizeof(B_ECD_Motor_T));
   s->model.dwork   = malloc(sizeof(DW_ECD_Motor_T));
   if (!s->model.blockIO || !s->model.dwork)
   {
      free(s->model.blockIO);
      free(s->model.dwork);
      s->model.blockIO = NULL;
      s->model.dwork = NULL;
      return -1;
   }

   ECD_Motor_initialize(&s->model, &s->in, &s->out);
   s->in.MMCIn_ModeOfOperation = g_ecd_mode;
   s->started = 1;
   return 0;
}

static void ecd_step(motion_source_t *m, uint32_t cycle,
                     const motion_feedback_t *fb, motion_setpoint_t *sp,
                     int n_axes)
{
   ecd_state_t *s = (ecd_state_t *)m->state;
   int i;

   (void)cycle; (void)fb;

   if (!s->started)
   {
      for (i = 0; i < n_axes; i++)
      {
         sp[i].position = fb ? fb[i].position_actual : 0;
         sp[i].velocity = 0;
         sp[i].torque   = 0;
      }
      return;
   }

   ECD_Motor_step(&s->model, &s->in, &s->out);

   /* The model produces one set of offsets; every axis follows it, which is
    * what the previous inline call to update_el_outputs() did. */
   for (i = 0; i < n_axes; i++)
   {
      sp[i].position = s->out.PosOffset;
      sp[i].velocity = s->out.VelOffset;
      sp[i].torque   = s->out.TorqueOffset;
   }
}

static void ecd_shutdown(motion_source_t *m)
{
   ecd_state_t *s = (ecd_state_t *)m->state;

   free(s->model.blockIO);
   free(s->model.dwork);
   s->model.blockIO = NULL;
   s->model.dwork = NULL;
   s->started = 0;
}

static motion_source_t g_ecd = { "ecd", &g_ecd_state,
                                 ecd_init, ecd_step, ecd_shutdown };

motion_source_t *motion_ecd_create(void)
{
   memset(&g_ecd_state, 0, sizeof(g_ecd_state));
   return &g_ecd;
}

/* ------------------------------------------------------------------------ */
/* Lookup and null-tolerant wrappers                                         */
/* ------------------------------------------------------------------------ */
motion_source_t *motion_source_create(const char *name)
{
   if (!name || !name[0])
      return motion_hold_create();
   if (strcmp(name, "hold") == 0)
      return motion_hold_create();
   if (strcmp(name, "ecd") == 0)
      return motion_ecd_create();
   return NULL;
}

int motion_init(motion_source_t *m, int n_axes, int cycle_time_us)
{
   if (!m)
      return 0;
   if (n_axes > MOTION_MAX_AXES)
      return -1;
   return m->init ? m->init(m, n_axes, cycle_time_us) : 0;
}

void motion_step(motion_source_t *m, uint32_t cycle,
                 const motion_feedback_t *fb, motion_setpoint_t *sp, int n_axes)
{
   if (m && m->step)
   {
      m->step(m, cycle, fb, sp, n_axes);
      return;
   }
   hold_step(NULL, cycle, fb, sp, n_axes);
}

void motion_shutdown(motion_source_t *m)
{
   if (m && m->shutdown)
      m->shutdown(m);
}
