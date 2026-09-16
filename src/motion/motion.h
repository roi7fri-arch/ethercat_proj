#ifndef MOTION_H
#define MOTION_H

/*
 * Where the setpoints come from.
 *
 * The RT loop used to call ECD_Motor_step() inline, which welded one specific
 * Simulink-generated model into the EtherCAT transport code. Swapping it for a
 * trajectory generator, a joystick, a network setpoint feed or a test ramp
 * meant editing the loop.
 *
 * A motion source is instead a small vtable: given the feedback the bus just
 * delivered, produce one setpoint per axis. It knows nothing about EtherCAT,
 * CoE, PDOs or drive vendors, and the loop knows nothing about how the numbers
 * were produced.
 *
 * Contract: step() is called exactly once per cycle from the RT thread. It must
 * not allocate, block, log or make syscalls.
 */

#include <stdint.h>

#define MOTION_MAX_AXES 32

/* What the master commands to one axis this cycle. Which fields the drive
 * actually uses depends on its mode of operation. */
typedef struct {
   int32_t position;
   int32_t velocity;
   int16_t torque;
} motion_setpoint_t;

/* What the bus reported for one axis this cycle. */
typedef struct {
   int32_t position_actual;
   int32_t velocity_actual;
   int16_t torque_actual;
   int     operational;    /* drive is in Operation Enabled */
} motion_feedback_t;

typedef struct motion_source {
   const char *name;
   void       *state;      /* implementation private */

   /* Called once before the loop. Returns 0 on success. */
   int  (*init)(struct motion_source *m, int n_axes, int cycle_time_us);

   /* Called once per cycle. Fill sp[0..n_axes-1]. RT-safe. */
   void (*step)(struct motion_source *m, uint32_t cycle,
                const motion_feedback_t *fb, motion_setpoint_t *sp, int n_axes);

   /* Called once after the loop. May free resources. */
   void (*shutdown)(struct motion_source *m);
} motion_source_t;

/* ------------------------------------------------------------------------ */
/* Built-in sources                                                          */
/* ------------------------------------------------------------------------ */

/* Commands every axis to stay exactly where it is, with zero velocity and
 * torque. The safe default, and what the loop should run when no algorithm is
 * configured or when one has been stopped. */
motion_source_t *motion_hold_create(void);

/* The Simulink-generated ECD_Motor model (src/motion/ECD_Motor.c), producing
 * the same position/velocity/torque offsets it always did. Every axis receives
 * the same setpoint, which is what the previous inline call did too. */
motion_source_t *motion_ecd_create(void);

/* The model's one input: the mode of operation it should generate for. Set it
 * before motion_init(). It used to be read from an uninitialised stack struct
 * in main(), so the model ran on whatever happened to be on the stack. */
void motion_ecd_set_mode(uint16_t mode_of_operation);

/* Look a source up by name ("hold", "ecd"). Returns NULL if unknown. */
motion_source_t *motion_source_create(const char *name);

/* Convenience wrappers that tolerate a NULL source (treated as "hold"). */
int  motion_init(motion_source_t *m, int n_axes, int cycle_time_us);
void motion_step(motion_source_t *m, uint32_t cycle,
                 const motion_feedback_t *fb, motion_setpoint_t *sp, int n_axes);
void motion_shutdown(motion_source_t *m);

#endif /* MOTION_H */
