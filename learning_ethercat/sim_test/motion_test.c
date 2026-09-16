/** \file
 * \brief Unit tests for the motion-source abstraction (src/motion/motion.c).
 *
 * Pure host test. Verifies the vtable contract, the safe "hold" default, the
 * ECD_Motor adapter, and that a source written entirely outside the core plugs
 * in without any change to the loop - which is the whole point of the layer.
 */
#include <stdio.h>
#include <string.h>

#include "motion.h"

static int g_pass = 0, g_fail = 0;

#define CHECK(cond, ...) do { \
      if (cond) { g_pass++; printf("  [PASS] " __VA_ARGS__); printf("\n"); } \
      else      { g_fail++; printf("  [FAIL] " __VA_ARGS__); printf("\n"); } \
   } while (0)

static void test_hold(void)
{
   motion_source_t *m;
   motion_feedback_t fb[3];
   motion_setpoint_t sp[3];
   int i;

   printf("TEST hold source\n");

   m = motion_hold_create();
   CHECK(m != NULL && strcmp(m->name, "hold") == 0, "hold source created");
   CHECK(motion_init(m, 3, 250) == 0, "init succeeds");

   memset(fb, 0, sizeof(fb));
   memset(sp, 0xAB, sizeof(sp));
   fb[0].position_actual = 1000;
   fb[1].position_actual = -2000;
   fb[2].position_actual = 0;

   motion_step(m, 0, fb, sp, 3);

   for (i = 0; i < 3; i++)
      CHECK(sp[i].position == fb[i].position_actual &&
            sp[i].velocity == 0 && sp[i].torque == 0,
            "axis %d commanded to its measured position (%d)", i, sp[i].position);

   motion_shutdown(m);
}

static void test_null_source_is_safe(void)
{
   motion_feedback_t fb[2];
   motion_setpoint_t sp[2];

   printf("TEST a NULL source behaves like hold\n");

   memset(fb, 0, sizeof(fb));
   memset(sp, 0x7F, sizeof(sp));
   fb[0].position_actual = 555;
   fb[1].position_actual = 777;

   CHECK(motion_init(NULL, 2, 250) == 0, "init on NULL is a no-op");
   motion_step(NULL, 0, fb, sp, 2);
   CHECK(sp[0].position == 555 && sp[1].position == 777,
         "NULL source still holds position rather than commanding garbage");
   CHECK(sp[0].velocity == 0 && sp[0].torque == 0, "and zero velocity/torque");
   motion_shutdown(NULL);
}

static void test_ecd(void)
{
   motion_source_t *m;
   motion_feedback_t fb[2];
   motion_setpoint_t sp[2];
   int moved = 0;
   int c;

   printf("TEST ECD_Motor source\n");

   m = motion_ecd_create();
   CHECK(m != NULL && strcmp(m->name, "ecd") == 0, "ecd source created");
   motion_ecd_set_mode(8);              /* cyclic synchronous position */
   CHECK(motion_init(m, 2, 250) == 0, "init allocates the model");

   memset(fb, 0, sizeof(fb));
   memset(sp, 0, sizeof(sp));

   /* Run a few hundred cycles; the model is a sweep generator, so something
    * must change on at least one output. */
   for (c = 0; c < 500; c++)
   {
      motion_step(m, (uint32_t)c, fb, sp, 2);
      if (sp[0].position != 0 || sp[0].velocity != 0 || sp[0].torque != 0)
         moved = 1;
   }

   CHECK(moved, "model produced a non-zero setpoint for mode 8");
   CHECK(sp[0].position == sp[1].position &&
         sp[0].velocity == sp[1].velocity &&
         sp[0].torque   == sp[1].torque,
         "every axis receives the same setpoint");

   motion_shutdown(m);

   /* After shutdown the adapter must fall back to holding position rather than
    * dereferencing the freed model. */
   fb[0].position_actual = 4242;
   motion_step(m, 0, fb, sp, 2);
   CHECK(sp[0].position == 4242 && sp[0].velocity == 0,
         "stepping a shut-down source holds position instead of crashing");
}

static void test_lookup(void)
{
   printf("TEST lookup by name\n");

   CHECK(motion_source_create("hold") != NULL, "\"hold\" resolves");
   CHECK(motion_source_create("ecd") != NULL,  "\"ecd\" resolves");
   CHECK(motion_source_create("") != NULL,     "empty name falls back to hold");
   CHECK(motion_source_create(NULL) != NULL,   "NULL name falls back to hold");
   CHECK(motion_source_create("nope") == NULL, "unknown name returns NULL");
   CHECK(motion_init(motion_hold_create(), MOTION_MAX_AXES + 1, 250) == -1,
         "too many axes is rejected at init");
}

/* A source defined entirely here - i.e. outside src/motion - to prove the loop
 * needs no change to accept a new trajectory generator. */
typedef struct { int32_t ramp; int init_calls; int shutdown_calls; } ramp_state_t;

static ramp_state_t g_ramp;

static int ramp_init(motion_source_t *m, int n_axes, int cycle_time_us)
{
   ramp_state_t *s = (ramp_state_t *)m->state;
   (void)n_axes; (void)cycle_time_us;
   s->ramp = 0;
   s->init_calls++;
   return 0;
}

static void ramp_step(motion_source_t *m, uint32_t cycle,
                      const motion_feedback_t *fb, motion_setpoint_t *sp,
                      int n_axes)
{
   ramp_state_t *s = (ramp_state_t *)m->state;
   int i;

   (void)cycle;
   s->ramp += 10;
   for (i = 0; i < n_axes; i++)
   {
      sp[i].position = fb[i].position_actual + s->ramp;
      sp[i].velocity = 10;
      sp[i].torque   = 0;
   }
}

static void ramp_shutdown(motion_source_t *m)
{
   ((ramp_state_t *)m->state)->shutdown_calls++;
}

static void test_custom_source(void)
{
   motion_source_t custom = { "ramp", &g_ramp,
                              ramp_init, ramp_step, ramp_shutdown };
   motion_feedback_t fb[1];
   motion_setpoint_t sp[1];

   printf("TEST a source defined outside the core plugs straight in\n");

   memset(&g_ramp, 0, sizeof(g_ramp));
   memset(fb, 0, sizeof(fb));
   memset(sp, 0, sizeof(sp));
   fb[0].position_actual = 100;

   CHECK(motion_init(&custom, 1, 250) == 0, "custom init runs");
   CHECK(g_ramp.init_calls == 1, "init called exactly once");

   motion_step(&custom, 0, fb, sp, 1);
   CHECK(sp[0].position == 110, "first step ramps by 10 (got %d)", sp[0].position);
   motion_step(&custom, 1, fb, sp, 1);
   CHECK(sp[0].position == 120, "second step ramps again (got %d)", sp[0].position);
   CHECK(sp[0].velocity == 10, "velocity commanded too");

   motion_shutdown(&custom);
   CHECK(g_ramp.shutdown_calls == 1, "shutdown called exactly once");
}

int main(void)
{
   printf("=== motion source unit tests ===\n");

   test_hold();
   test_null_source_is_safe();
   test_ecd();
   test_lookup();
   test_custom_source();

   printf("\n=== %d passed, %d failed ===\n", g_pass, g_fail);
   return g_fail ? 1 : 0;
}
