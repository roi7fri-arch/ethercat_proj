/** \file
 * \brief End-to-end telemetry tests: production config + SOEM master + virtual
 *        slave(s) + telemetry logger, with NO real hardware.
 *
 * Path exercised (all target code, verbatim):
 *   config_loader.c        parses a GUI JSON config into g_ecat_config
 *   elmo_config_setup.c    PO2SOconfig hook programs the PDO map over CoE
 *   telemetry.c            the logger under test
 *   slave_sim.c            virtual CiA402 axis (mirrors RxPDO -> TxPDO)
 *
 * The virtual slave echoes the command image (everything after the
 * controlword) back into the feedback image (after the statusword). So on a
 * healthy run the telemetry CSV must show fb_ActPos == cmd_TargetPos on every
 * row. Fault-injection hooks (slavesim_set_freeze / _wkc_drop) let us verify
 * the logger also captures anomalies: a stalled feedback and a WKC loss.
 *
 * Tests:
 *   1. single axis, healthy   -> fb tracks cmd every cycle
 *   2. two axes, healthy      -> both slaves logged, both aligned
 *   3. single axis, faults    -> feedback freeze + WKC drop show up in the CSV
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "ethercattype.h"
#include "nicdrv.h"
#include "ethercatbase.h"
#include "ethercatmain.h"
#include "ethercatconfig.h"
#include "ethercatcoe.h"
#include "ethercatprint.h"

#include "config_loader.h"
#include "elmo_config_setup.h"   /* g_ecat_config + elmo_platinum_setup_from_config */
#include "telemetry.h"
#include "slave_sim.h"

static char IOmap[8192];
static int  g_pass = 0, g_fail = 0;

#define CHECK(cond, ...) do { \
      if (cond) { g_pass++; printf("  [PASS] " __VA_ARGS__); printf("\n"); } \
      else      { g_fail++; printf("  [FAIL] " __VA_ARGS__); printf("\n"); } \
   } while (0)

#define NCYC       16
#define FAULT_CYC  8    /* inject faults starting at this cycle */

/* One CSP axis: CW + TargetPos (Rx), SW + ActPos (Tx). */
static const char *AXIS_JSON =
"{\n"
"  \"version\": 1,\n"
"  \"network\": { \"interface\": \"eth0\", \"cycle_time_us\": 250,\n"
"                \"number_of_cycles\": 16, \"distributed_clock\": true,\n"
"                \"sync0_shift_us\": 0, \"sync_kp_div\": 100, \"sync_ki_div\": 20 },\n"
"  \"slaves\": [\n"
"    { \"position\": 1, \"name\": \"Axis A\", \"mode_of_operation\": 8,\n"
"      \"rxpdo\": [ {\"index\":\"0x6040\",\"subindex\":0,\"bitlen\":16,\"name\":\"CW\"},\n"
"                  {\"index\":\"0x607A\",\"subindex\":0,\"bitlen\":32,\"name\":\"TargetPos\"} ],\n"
"      \"txpdo\": [ {\"index\":\"0x6041\",\"subindex\":0,\"bitlen\":16,\"name\":\"SW\"},\n"
"                  {\"index\":\"0x6064\",\"subindex\":0,\"bitlen\":32,\"name\":\"ActPos\"} ] }\n"
"  ]\n"
"}\n";

/* Two CSP axes (both position mode) so both feedback images mirror the ramp. */
static const char *TWO_AXIS_JSON =
"{\n"
"  \"version\": 1,\n"
"  \"network\": { \"interface\": \"eth0\", \"cycle_time_us\": 250,\n"
"                \"number_of_cycles\": 16, \"distributed_clock\": true,\n"
"                \"sync0_shift_us\": 0, \"sync_kp_div\": 100, \"sync_ki_div\": 20 },\n"
"  \"slaves\": [\n"
"    { \"position\": 1, \"name\": \"Axis A\", \"mode_of_operation\": 8,\n"
"      \"rxpdo\": [ {\"index\":\"0x6040\",\"subindex\":0,\"bitlen\":16,\"name\":\"CW\"},\n"
"                  {\"index\":\"0x607A\",\"subindex\":0,\"bitlen\":32,\"name\":\"TargetPos\"} ],\n"
"      \"txpdo\": [ {\"index\":\"0x6041\",\"subindex\":0,\"bitlen\":16,\"name\":\"SW\"},\n"
"                  {\"index\":\"0x6064\",\"subindex\":0,\"bitlen\":32,\"name\":\"ActPos\"} ] },\n"
"    { \"position\": 2, \"name\": \"Axis B\", \"mode_of_operation\": 8,\n"
"      \"rxpdo\": [ {\"index\":\"0x6040\",\"subindex\":0,\"bitlen\":16,\"name\":\"CW\"},\n"
"                  {\"index\":\"0x607A\",\"subindex\":0,\"bitlen\":32,\"name\":\"TargetPos\"} ],\n"
"      \"txpdo\": [ {\"index\":\"0x6041\",\"subindex\":0,\"bitlen\":16,\"name\":\"SW\"},\n"
"                  {\"index\":\"0x6064\",\"subindex\":0,\"bitlen\":32,\"name\":\"ActPos\"} ] }\n"
"  ]\n"
"}\n";

static void put_le32(uint8_t *p, int32_t v)
{
   uint32_t u = (uint32_t)v;
   p[0] = u & 0xFF; p[1] = (u >> 8) & 0xFF; p[2] = (u >> 16) & 0xFF; p[3] = u >> 24;
}

/* Bring the whole bus to OP using the production PO2SOconfig hook. */
static int run_to_op(int nslaves)
{
   int chk, s;
   slavesim_init_n(nslaves);
   if (!ec_init("sim")) return -1;
   if (ec_config_init(FALSE) <= 0) { ec_close(); return -2; }
   for (s = 1; s <= ec_slavecount; s++)
      ec_slave[s].PO2SOconfig = &elmo_platinum_setup_from_config;
   ec_config_map(&IOmap);
   ec_statecheck(0, EC_STATE_SAFE_OP, EC_TIMEOUTSTATE);

   ec_slave[0].state = EC_STATE_OPERATIONAL;
   ec_send_processdata();
   ec_receive_processdata(EC_TIMEOUTRET);
   ec_writestate(0);
   chk = 40;
   do {
      ec_send_processdata();
      ec_receive_processdata(EC_TIMEOUTRET);
      ec_statecheck(0, EC_STATE_OPERATIONAL, 50000);
   } while (chk-- && ec_slave[0].state != EC_STATE_OPERATIONAL);

   return (ec_slave[0].state == EC_STATE_OPERATIONAL) ? 0 : -3;
}

/* Copy field number `idx` (0-based) of a CSV line into out. Returns 0 on ok. */
static int csv_field(const char *line, int idx, char *out, int cap)
{
   int f = 0;
   const char *p = line;
   while (f < idx)
   {
      p = strchr(p, ',');
      if (!p) return -1;
      p++;
      f++;
   }
   {
      const char *e = strchr(p, ',');
      size_t n = e ? (size_t)(e - p) : strcspn(p, "\r\n");
      if (n >= (size_t)cap) n = cap - 1;
      memcpy(out, p, n);
      out[n] = '\0';
   }
   return 0;
}

/* Command a ramp into ec_slave[pos].outputs for this cycle. */
static int32_t command_ramp(int pos, int cyc)
{
   uint8_t *out = ec_slave[pos].outputs;
   int32_t target = 1000 + cyc * 25 + pos * 100000;   /* distinct per axis */
   out[0] = 0x0F; out[1] = 0x00;      /* controlword = enable */
   put_le32(out + 2, target);         /* TargetPos */
   return target;
}

static int32_t feedback_pos(int pos)
{
   const uint8_t *in = ec_slave[pos].inputs;
   return (int32_t)((uint32_t)in[2] | ((uint32_t)in[3] << 8) |
                    ((uint32_t)in[4] << 16) | ((uint32_t)in[5] << 24));
}

/* Field indices in the CSV (both JSON layouts share the same column order):
 * cycle,t_ns,wkc,latency_ns,exec_ns,dc_time,cmd_CW,cmd_TargetPos,fb_SW,fb_ActPos */
#define COL_WKC   2
#define COL_CMD   7
#define COL_FB    9

/* ---------------------------------------------------------------------- */
static void test_single_axis(void)
{
   telemetry_t tlm;
   int i, rc, r;
   int32_t last_target = 0;

   printf("TEST single axis, healthy\n");
   memset(&g_ecat_config, 0, sizeof(g_ecat_config));
   rc = ecat_config_load_string(AXIS_JSON, &g_ecat_config);
   CHECK(rc == 0, "config parsed (%s)", rc == 0 ? "ok" : ecat_config_last_error());
   if (rc != 0) return;

   r = run_to_op(1);
   CHECK(r == 0, "virtual bus reached OPERATIONAL");
   CHECK(ec_slave[1].Obytes == 6 && ec_slave[1].Ibytes == 6,
         "process image = 6 B out / 6 B in");
   if (r != 0) { ec_close(); return; }

   rc = telemetry_init(&tlm, &g_ecat_config, NCYC + 1);
   CHECK(rc == 0, "telemetry_init (%s)", rc == 0 ? "ok" : telemetry_last_error());
   if (rc != 0) { ec_close(); return; }
   telemetry_add_slave(&tlm, 1, ec_slave[1].outputs, ec_slave[1].Obytes,
                       ec_slave[1].inputs, ec_slave[1].Ibytes);

   for (i = 0; i < NCYC; i++)
   {
      int wkc;
      last_target = command_ramp(1, i);
      ec_send_processdata();
      wkc = ec_receive_processdata(EC_TIMEOUTRET);
      telemetry_sample(&tlm, i, (int64_t)i * 250000, wkc,
                       1000 + i, 40000 + i, (int64_t)i * 250000);
   }
   CHECK(feedback_pos(1) == last_target, "slave feedback mirrors last cmd (%d)", last_target);

   rc = telemetry_write(&tlm, "/tmp/tlm_sim");
   CHECK(rc == 0, "telemetry_write (%s)", rc == 0 ? "ok" : telemetry_last_error());
   {
      FILE *fp = fopen("/tmp/tlm_sim_slave1.csv", "r");
      char line[512];
      int rows = 0, aligned = 1;
      CHECK(fp != NULL, "CSV /tmp/tlm_sim_slave1.csv created");
      if (fp)
      {
         if (fgets(line, sizeof(line), fp)) {
            CHECK(strstr(line, "cmd_TargetPos") && strstr(line, "fb_ActPos"),
                  "header has cmd_TargetPos + fb_ActPos columns");
         }
         while (fgets(line, sizeof(line), fp)) {
            char cmd[32], fb[32];
            if (csv_field(line, COL_CMD, cmd, sizeof(cmd)) == 0 &&
                csv_field(line, COL_FB, fb, sizeof(fb)) == 0 &&
                atoi(cmd) != atoi(fb)) aligned = 0;
            rows++;
         }
         fclose(fp);
         CHECK(rows == NCYC, "CSV has %d data rows (expected %d)", rows, NCYC);
         CHECK(aligned, "every row: fb_ActPos == cmd_TargetPos");
      }
   }
   telemetry_free(&tlm);
   ec_close();
}

/* ---------------------------------------------------------------------- */
static void test_two_axis(void)
{
   telemetry_t tlm;
   int i, rc, r, k;
   const char *files[2] = { "/tmp/tlm2_slave1.csv", "/tmp/tlm2_slave2.csv" };

   printf("TEST two axes, healthy\n");
   memset(&g_ecat_config, 0, sizeof(g_ecat_config));
   rc = ecat_config_load_string(TWO_AXIS_JSON, &g_ecat_config);
   CHECK(rc == 0, "2-axis config parsed (%s)", rc == 0 ? "ok" : ecat_config_last_error());
   if (rc != 0) return;

   r = run_to_op(2);
   CHECK(ec_slavecount == 2, "2 slaves discovered");
   CHECK(r == 0, "both axes reached OPERATIONAL");
   if (r != 0) { ec_close(); return; }

   rc = telemetry_init(&tlm, &g_ecat_config, NCYC + 1);
   CHECK(rc == 0, "telemetry_init (%s)", rc == 0 ? "ok" : telemetry_last_error());
   if (rc != 0) { ec_close(); return; }
   telemetry_add_slave(&tlm, 1, ec_slave[1].outputs, ec_slave[1].Obytes,
                       ec_slave[1].inputs, ec_slave[1].Ibytes);
   telemetry_add_slave(&tlm, 2, ec_slave[2].outputs, ec_slave[2].Obytes,
                       ec_slave[2].inputs, ec_slave[2].Ibytes);

   for (i = 0; i < NCYC; i++)
   {
      int wkc;
      command_ramp(1, i);
      command_ramp(2, i);
      ec_send_processdata();
      wkc = ec_receive_processdata(EC_TIMEOUTRET);
      telemetry_sample(&tlm, i, (int64_t)i * 250000, wkc,
                       1000 + i, 40000 + i, (int64_t)i * 250000);
   }

   rc = telemetry_write(&tlm, "/tmp/tlm2");
   CHECK(rc == 0, "telemetry_write both slaves (%s)", rc == 0 ? "ok" : telemetry_last_error());

   for (k = 0; k < 2; k++)
   {
      FILE *fp = fopen(files[k], "r");
      char line[512];
      int rows = 0, aligned = 1;
      CHECK(fp != NULL, "CSV %s created", files[k]);
      if (fp)
      {
         if (fgets(line, sizeof(line), fp)) { /* skip header */ }
         while (fgets(line, sizeof(line), fp)) {
            char cmd[32], fb[32];
            if (csv_field(line, COL_CMD, cmd, sizeof(cmd)) == 0 &&
                csv_field(line, COL_FB, fb, sizeof(fb)) == 0 &&
                atoi(cmd) != atoi(fb)) aligned = 0;
            rows++;
         }
         fclose(fp);
         CHECK(rows == NCYC, "slave %d CSV has %d rows", k + 1, rows);
         CHECK(aligned, "slave %d: fb tracks cmd every row", k + 1);
      }
   }
   telemetry_free(&tlm);
   ec_close();
}

/* ---------------------------------------------------------------------- */
static void test_fault_injection(void)
{
   telemetry_t tlm;
   int i, rc, r;
   int32_t frozen_fb = 0;

   printf("TEST single axis, injected faults (feedback freeze + WKC drop)\n");
   memset(&g_ecat_config, 0, sizeof(g_ecat_config));
   rc = ecat_config_load_string(AXIS_JSON, &g_ecat_config);
   CHECK(rc == 0, "config parsed (%s)", rc == 0 ? "ok" : ecat_config_last_error());
   if (rc != 0) return;

   r = run_to_op(1);
   CHECK(r == 0, "virtual bus reached OPERATIONAL");
   if (r != 0) { ec_close(); return; }

   rc = telemetry_init(&tlm, &g_ecat_config, NCYC + 1);
   if (rc != 0) { CHECK(0, "telemetry_init (%s)", telemetry_last_error()); ec_close(); return; }
   telemetry_add_slave(&tlm, 1, ec_slave[1].outputs, ec_slave[1].Obytes,
                       ec_slave[1].inputs, ec_slave[1].Ibytes);

   for (i = 0; i < NCYC; i++)
   {
      int wkc;
      command_ramp(1, i);
      if (i == FAULT_CYC)          /* inject both faults mid-run */
      {
         frozen_fb = feedback_pos(1);   /* value the feedback will stick at */
         slavesim_set_freeze(1, 1);
         slavesim_set_wkc_drop(1, 1);
      }
      ec_send_processdata();
      wkc = ec_receive_processdata(EC_TIMEOUTRET);
      telemetry_sample(&tlm, i, (int64_t)i * 250000, wkc,
                       1000 + i, 40000 + i, (int64_t)i * 250000);
   }

   rc = telemetry_write(&tlm, "/tmp/tlm_fault");
   CHECK(rc == 0, "telemetry_write (%s)", rc == 0 ? "ok" : telemetry_last_error());

   {
      FILE *fp = fopen("/tmp/tlm_fault_slave1.csv", "r");
      char line[512];
      int row = 0;
      int healthy_ok = 1, stalled_ok = 1, wkc_full_ok = 1, wkc_dropped_ok = 1;
      int wkc_full = -1, wkc_bad = -1;
      CHECK(fp != NULL, "fault CSV created");
      if (fp)
      {
         if (fgets(line, sizeof(line), fp)) { /* header */ }
         while (fgets(line, sizeof(line), fp))
         {
            char cmd[32], fb[32], wkc[32];
            int c, f, w;
            csv_field(line, COL_CMD, cmd, sizeof(cmd));
            csv_field(line, COL_FB, fb, sizeof(fb));
            csv_field(line, COL_WKC, wkc, sizeof(wkc));
            c = atoi(cmd); f = atoi(fb); w = atoi(wkc);
            if (row < FAULT_CYC) {
               if (f != c) healthy_ok = 0;      /* pre-fault: fb tracks cmd */
               if (w != 3) wkc_full_ok = 0;     /* full WKC = 3             */
               wkc_full = w;
            } else {
               if (f != frozen_fb) stalled_ok = 0;   /* feedback frozen     */
               if (f == c) stalled_ok = 0;           /* and diverged from cmd*/
               if (w >= 3) wkc_dropped_ok = 0;       /* WKC lost            */
               wkc_bad = w;
            }
            row++;
         }
         fclose(fp);
         CHECK(healthy_ok,   "pre-fault rows: fb tracked cmd");
         CHECK(wkc_full_ok,  "pre-fault rows: WKC = 3 (full)");
         CHECK(stalled_ok,   "post-fault rows: fb frozen at %d, diverged from cmd", frozen_fb);
         CHECK(wkc_dropped_ok, "post-fault rows: WKC dropped (%d -> %d)", wkc_full, wkc_bad);
      }
   }
   telemetry_free(&tlm);
   ec_close();
}

int main(int argc, char *argv[])
{
   if (argc > 1 && strcmp(argv[1], "-v") == 0) slavesim_set_verbose(1);

   printf("=== telemetry <- SOEM master <- virtual slave(s) (no hardware) ===\n");
   test_single_axis();
   test_two_axis();
   test_fault_injection();

   printf("\n=== %d passed, %d failed ===\n", g_pass, g_fail);
   return g_fail ? 1 : 0;
}
