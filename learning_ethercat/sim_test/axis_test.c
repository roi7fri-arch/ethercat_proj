/** \file
 * \brief Unit tests for the generic axis object (src/drive/axis.c).
 *
 * Pure host test. A minimal CiA 402 drive model reads the controlword out of
 * the output image and writes a statusword plus feedback back into the input
 * image, exactly as a real slave would - so the axis is exercised through the
 * same process-image path the RT loop uses, with no shortcuts.
 *
 * Covers: single- and dual-axis nodes, the enable ladder over PDOs, hold
 * seeding (no step on enable), safe stop, fault recovery, and mode validation.
 */
#include <stdio.h>
#include <string.h>

#include "axis.h"

static int g_pass = 0, g_fail = 0;

#define CHECK(cond, ...) do { \
      if (cond) { g_pass++; printf("  [PASS] " __VA_ARGS__); printf("\n"); } \
      else      { g_fail++; printf("  [FAIL] " __VA_ARGS__); printf("\n"); } \
   } while (0)

/* ------------------------------------------------------------------------ */
/* A virtual CiA 402 drive sharing the process image with the axis under test */
/* ------------------------------------------------------------------------ */
typedef struct {
   uint8_t *out;  int out_bytes;   /* master -> drive */
   uint8_t *in;   int in_bytes;    /* drive  -> master */
   pdo_io_t view;                  /* the drive's own view of the image */
   uint16_t sw;
   int32_t  pos;
   int      powered;               /* set 0 to make the drive refuse to enable */
} vdrive_t;

static void vdrive_init(vdrive_t *d, const ecat_slave_config_t *sc, int axis_index,
                        uint8_t *out, int out_bytes, uint8_t *in, int in_bytes)
{
   memset(d, 0, sizeof(*d));
   d->out = out; d->out_bytes = out_bytes;
   d->in  = in;  d->in_bytes  = in_bytes;
   d->powered = 1;
   d->sw = 0x0000;                 /* Not Ready To Switch On */
   pdo_bind(&d->view, sc, 1, axis_index, out, out_bytes, in, in_bytes);
}

/* One drive cycle: consume the controlword, advance the FSA, publish feedback. */
static void vdrive_step(vdrive_t *d)
{
   uint16_t cw  = pdo_get_u16(&d->view, PDO_SIG_CONTROLWORD);
   uint16_t cmd = cw & CIA402_CW_MASK;

   switch (cia402_state(d->sw))
   {
      case CIA402_STATE_NOT_READY_TO_SWITCH_ON:
         d->sw = 0x0240; break;                              /* boot done */
      case CIA402_STATE_SWITCH_ON_DISABLED:
         if (cmd == CIA402_CW_SHUTDOWN)         d->sw = 0x0231;
         break;
      case CIA402_STATE_READY_TO_SWITCH_ON:
         if (cmd == CIA402_CW_SWITCH_ON)        d->sw = 0x0233;
         else if (cmd == CIA402_CW_QUICK_STOP)  d->sw = 0x0217;
         break;
      case CIA402_STATE_SWITCHED_ON:
         if (cmd == CIA402_CW_ENABLE_OPERATION && d->powered) d->sw = 0x0237;
         else if (cmd == CIA402_CW_QUICK_STOP)  d->sw = 0x0217;
         break;
      case CIA402_STATE_OPERATION_ENABLED:
         if (cmd == CIA402_CW_QUICK_STOP)       d->sw = 0x0217;
         else if (cmd == CIA402_CW_DISABLE_VOLTAGE) d->sw = 0x0240;
         break;
      case CIA402_STATE_QUICK_STOP_ACTIVE:
         if (cmd == CIA402_CW_DISABLE_VOLTAGE)  d->sw = 0x0240;
         break;
      case CIA402_STATE_FAULT:
      case CIA402_STATE_FAULT_REACTION_ACTIVE:
         if (cw & CIA402_CW_FAULT_RESET_BIT)    d->sw = 0x0240;
         break;
      default:
         break;
   }

   /* While enabled, follow the commanded position offset exactly. */
   if (cia402_is_operational(d->sw))
      d->pos = pdo_get_i32(&d->view, PDO_SIG_POSITION_OFFSET);

   /* Publish feedback. pdo_set_* refuses to write TxPDO roles (the slave owns
    * that image), so here - playing the slave - we poke the raw bytes. */
   {
      int32_t so = d->view.sig[PDO_SIG_STATUSWORD].bit_off / 8;
      int32_t po = d->view.sig[PDO_SIG_POSITION_ACTUAL].bit_off / 8;
      d->in[so + 0] = (uint8_t)(d->sw & 0xFF);
      d->in[so + 1] = (uint8_t)(d->sw >> 8);
      if (d->view.sig[PDO_SIG_POSITION_ACTUAL].present)
      {
         d->in[po + 0] = (uint8_t)(d->pos & 0xFF);
         d->in[po + 1] = (uint8_t)((d->pos >> 8) & 0xFF);
         d->in[po + 2] = (uint8_t)((d->pos >> 16) & 0xFF);
         d->in[po + 3] = (uint8_t)((d->pos >> 24) & 0xFF);
      }
   }
}

static void vdrive_set_fault(vdrive_t *d) { d->sw = 0x0218; }

/* ------------------------------------------------------------------------ */
/* Configurations                                                            */
/* ------------------------------------------------------------------------ */
static void add_rx(ecat_slave_config_t *sc, uint16_t idx, uint8_t bits)
{
   sc->rxpdo[sc->rxpdo_count].index  = idx;
   sc->rxpdo[sc->rxpdo_count].bitlen = bits;
   sc->rxpdo_count++;
}
static void add_tx(ecat_slave_config_t *sc, uint16_t idx, uint8_t bits)
{
   sc->txpdo[sc->txpdo_count].index  = idx;
   sc->txpdo[sc->txpdo_count].bitlen = bits;
   sc->txpdo_count++;
}

/* Rx 10 bytes, Tx 6 bytes, one axis, CSP. */
static void build_single(ecat_slave_config_t *sc)
{
   memset(sc, 0, sizeof(*sc));
   sc->position = 1;
   sc->mode_of_operation = CIA402_MODE_CSP;
   snprintf(sc->name, sizeof(sc->name), "Axis A");
   add_rx(sc, CIA402_OD_CONTROLWORD,     16);
   add_rx(sc, CIA402_OD_POSITION_OFFSET, 32);
   add_rx(sc, CIA402_OD_VELOCITY_OFFSET, 32);
   add_tx(sc, CIA402_OD_STATUSWORD,      16);
   add_tx(sc, CIA402_OD_POSITION_ACTUAL, 32);
}

/* Two axes behind one node, as an Elmo Platinum maps them. */
static void build_dual(ecat_slave_config_t *sc)
{
   memset(sc, 0, sizeof(*sc));
   sc->position = 1;
   sc->mode_of_operation = CIA402_MODE_CSP;
   snprintf(sc->name, sizeof(sc->name), "Platinum");
   add_rx(sc, CIA402_OD_CONTROLWORD,     16);
   add_rx(sc, CIA402_OD_POSITION_OFFSET, 32);
   add_rx(sc, CIA402_OD_CONTROLWORD,     16);
   add_rx(sc, CIA402_OD_POSITION_OFFSET, 32);
   add_tx(sc, CIA402_OD_STATUSWORD,      16);
   add_tx(sc, CIA402_OD_POSITION_ACTUAL, 32);
   add_tx(sc, CIA402_OD_STATUSWORD,      16);
   add_tx(sc, CIA402_OD_POSITION_ACTUAL, 32);
}

/* ------------------------------------------------------------------------ */
/* Tests                                                                     */
/* ------------------------------------------------------------------------ */

static void test_axis_count(void)
{
   ecat_slave_config_t single, dual;

   printf("TEST axis count derived from the PDO map\n");
   build_single(&single);
   build_dual(&dual);

   CHECK(pdo_axis_count(&single) == 1, "single-axis map reports 1 axis");
   CHECK(pdo_axis_count(&dual)   == 2, "dual-axis map reports 2 axes");
}

static void test_enable_over_pdos(void)
{
   ecat_slave_config_t sc;
   axis_t ax;
   vdrive_t drive;
   uint8_t out[10], in[6];
   int cycle;

   printf("TEST axis climbs to Operation Enabled over the process image\n");
   build_single(&sc);
   memset(out, 0, sizeof(out));
   memset(in, 0, sizeof(in));

   CHECK(axis_init(&ax, &sc, 1, 0, out, sizeof(out), in, sizeof(in)) == 0,
         "axis_init succeeds");
   CHECK(strcmp(ax.name, "Axis A") == 0, "single-axis name has no suffix");
   printf("   (expect no complaints below)\n");
   CHECK(axis_validate(&ax) == 0, "CSP map validates");

   vdrive_init(&drive, &sc, 0, out, sizeof(out), in, sizeof(in));

   for (cycle = 0; cycle < 20 && !axis_is_operational(&ax); cycle++)
   {
      axis_read(&ax);
      axis_enable_step(&ax);
      if (!axis_is_operational(&ax))
         axis_hold(&ax);
      vdrive_step(&drive);
   }

   axis_read(&ax);
   CHECK(axis_is_operational(&ax),
         "reached Operation Enabled in %d cycles", cycle);
   CHECK(ax.controlword == CIA402_CW_ENABLE_OPERATION,
         "holding 0x000F (got 0x%04X)", ax.controlword);
   CHECK(strcmp(axis_state_name(&ax), "Operation Enabled") == 0, "state name");
}

/* The classic hazard: a drive sitting at a non-zero position must not jump when
 * it is enabled. axis_hold() seeds the command from the feedback every cycle. */
static void test_hold_prevents_a_step(void)
{
   ecat_slave_config_t sc;
   axis_t ax;
   vdrive_t drive;
   uint8_t out[10], in[6];
   int cycle;

   printf("TEST hold seeds command from feedback (no step on enable)\n");
   build_single(&sc);
   memset(out, 0, sizeof(out));
   memset(in, 0, sizeof(in));

   axis_init(&ax, &sc, 1, 0, out, sizeof(out), in, sizeof(in));
   vdrive_init(&drive, &sc, 0, out, sizeof(out), in, sizeof(in));
   drive.pos = 123456;              /* parked well away from zero */
   vdrive_step(&drive);

   for (cycle = 0; cycle < 20 && !axis_is_operational(&ax); cycle++)
   {
      axis_read(&ax);
      axis_enable_step(&ax);
      if (!axis_is_operational(&ax))
         axis_hold(&ax);
      vdrive_step(&drive);
   }

   axis_read(&ax);
   CHECK(axis_is_operational(&ax), "drive enabled");
   CHECK(pdo_get_i32(&ax.io, PDO_SIG_POSITION_OFFSET) == 123456,
         "commanded position equals the parked position (got %d)",
         pdo_get_i32(&ax.io, PDO_SIG_POSITION_OFFSET));
   CHECK(ax.position_actual == 123456, "no movement occurred (got %d)",
         ax.position_actual);

   /* Now command a real move and check it lands. */
   axis_write_setpoint(&ax, 123500, 0, 0);
   vdrive_step(&drive);
   axis_read(&ax);
   CHECK(ax.position_actual == 123500, "setpoint followed (got %d)",
         ax.position_actual);
}

static void test_fault_recovery(void)
{
   ecat_slave_config_t sc;
   axis_t ax;
   vdrive_t drive;
   uint8_t out[10], in[6];
   int cycle;

   printf("TEST fault is detected and cleared\n");
   build_single(&sc);
   memset(out, 0, sizeof(out));
   memset(in, 0, sizeof(in));

   axis_init(&ax, &sc, 1, 0, out, sizeof(out), in, sizeof(in));
   vdrive_init(&drive, &sc, 0, out, sizeof(out), in, sizeof(in));

   for (cycle = 0; cycle < 20 && !axis_is_operational(&ax); cycle++)
   {
      axis_read(&ax); axis_enable_step(&ax);
      if (!axis_is_operational(&ax)) axis_hold(&ax);
      vdrive_step(&drive);
   }
   axis_read(&ax);
   CHECK(axis_is_operational(&ax), "enabled before the fault");
   CHECK(ax.fault_count == 0, "no faults counted yet");

   vdrive_set_fault(&drive);
   vdrive_step(&drive);
   axis_read(&ax);
   CHECK(axis_has_fault(&ax), "fault detected");
   CHECK(axis_state_changed(&ax), "state change reported on the cycle it happened");
   CHECK(ax.fault_count == 1, "fault counted once (got %u)", ax.fault_count);

   for (cycle = 0; cycle < 20 && !axis_is_operational(&ax); cycle++)
   {
      axis_read(&ax); axis_enable_step(&ax);
      if (!axis_is_operational(&ax)) axis_hold(&ax);
      vdrive_step(&drive);
   }
   axis_read(&ax);
   CHECK(axis_is_operational(&ax), "recovered to Operation Enabled in %d cycles",
         cycle);
}

static void test_safe_stop(void)
{
   ecat_slave_config_t sc;
   axis_t ax;
   vdrive_t drive;
   uint8_t out[10], in[6];
   int cycle;

   printf("TEST safe stop drops the drive out of Operation Enabled\n");
   build_single(&sc);
   memset(out, 0, sizeof(out));
   memset(in, 0, sizeof(in));

   axis_init(&ax, &sc, 1, 0, out, sizeof(out), in, sizeof(in));
   vdrive_init(&drive, &sc, 0, out, sizeof(out), in, sizeof(in));

   for (cycle = 0; cycle < 20 && !axis_is_operational(&ax); cycle++)
   {
      axis_read(&ax); axis_enable_step(&ax);
      if (!axis_is_operational(&ax)) axis_hold(&ax);
      vdrive_step(&drive);
   }
   CHECK(axis_is_operational(&ax), "enabled first");

   axis_read(&ax);
   axis_safe_stop(&ax);
   vdrive_step(&drive);
   axis_read(&ax);
   CHECK(ax.state == CIA402_STATE_QUICK_STOP_ACTIVE,
         "quick stop requested and honoured (state %s)", axis_state_name(&ax));
   CHECK(pdo_get_i32(&ax.io, PDO_SIG_VELOCITY_OFFSET) == 0,
         "velocity command zeroed by the stop");
}

static void test_dual_axis_set(void)
{
   ecat_slave_config_t sc;
   axis_set_t set;
   vdrive_t d0, d1;
   uint8_t out[16], in[12];
   int cycle, i;

   printf("TEST dual-axis node produces two independent axes\n");
   build_dual(&sc);
   memset(&set, 0, sizeof(set));
   memset(out, 0, sizeof(out));
   memset(in, 0, sizeof(in));

   CHECK(axis_set_add_slave(&set, &sc, 1, out, sizeof(out), in, sizeof(in)) == 2,
         "two axes added from one slave");
   CHECK(set.count == 2, "set holds 2 axes");
   CHECK(strcmp(set.axis[0].name, "Platinum:ax0") == 0 &&
         strcmp(set.axis[1].name, "Platinum:ax1") == 0,
         "axes named per index");
   printf("   (expect no complaints below)\n");
   CHECK(axis_set_validate(&set) == 0, "both axes validate");

   vdrive_init(&d0, &sc, 0, out, sizeof(out), in, sizeof(in));
   vdrive_init(&d1, &sc, 1, out, sizeof(out), in, sizeof(in));
   d0.pos = 1000;
   d1.pos = 2000;

   for (cycle = 0; cycle < 20; cycle++)
   {
      for (i = 0; i < set.count; i++)
      {
         axis_read(&set.axis[i]);
         axis_enable_step(&set.axis[i]);
         if (!axis_is_operational(&set.axis[i]))
            axis_hold(&set.axis[i]);
      }
      vdrive_step(&d0);
      vdrive_step(&d1);
   }
   for (i = 0; i < set.count; i++)
      axis_read(&set.axis[i]);

   CHECK(axis_is_operational(&set.axis[0]) && axis_is_operational(&set.axis[1]),
         "both axes reached Operation Enabled");
   CHECK(set.axis[0].position_actual == 1000, "axis 0 feedback (got %d)",
         set.axis[0].position_actual);
   CHECK(set.axis[1].position_actual == 2000, "axis 1 feedback (got %d)",
         set.axis[1].position_actual);

   axis_write_setpoint(&set.axis[0], 1111, 0, 0);
   axis_write_setpoint(&set.axis[1], 2222, 0, 0);
   vdrive_step(&d0);
   vdrive_step(&d1);
   axis_read(&set.axis[0]);
   axis_read(&set.axis[1]);
   CHECK(set.axis[0].position_actual == 1111 &&
         set.axis[1].position_actual == 2222,
         "axes move independently (got %d / %d)",
         set.axis[0].position_actual, set.axis[1].position_actual);
}

/* A CST axis needs torque feedback; this map has none, so start-up must refuse
 * rather than silently read zeros forever. */
static void test_mode_validation(void)
{
   ecat_slave_config_t sc;
   axis_t ax;
   uint8_t out[10], in[6];

   printf("TEST wrong mode for the map is rejected at start-up\n");
   build_single(&sc);
   sc.mode_of_operation = CIA402_MODE_CST;
   memset(out, 0, sizeof(out));
   memset(in, 0, sizeof(in));

   axis_init(&ax, &sc, 1, 0, out, sizeof(out), in, sizeof(in));
   printf("   (expect one complaint below)\n");
   CHECK(axis_validate(&ax) == 1, "CST without torque feedback is rejected");
}

int main(void)
{
   printf("=== axis unit tests ===\n");

   test_axis_count();
   test_enable_over_pdos();
   test_hold_prevents_a_step();
   test_fault_recovery();
   test_safe_stop();
   test_dual_axis_set();
   test_mode_validation();

   printf("\n=== %d passed, %d failed ===\n", g_pass, g_fail);
   return g_fail ? 1 : 0;
}
