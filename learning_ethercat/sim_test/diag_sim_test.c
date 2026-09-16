/** \file
 * \brief End-to-end diagnostics test: SOEM master + virtual slave(s) + the
 *        ecat_diag module reading ESC error counters over FPRD, NO hardware.
 *
 * Path exercised (target code, verbatim):
 *   config_loader.c     parses a GUI JSON config into g_ecat_config
 *   ecat_diag.c         the module under test (FPRD 0x0300 block + AL state)
 *   slave_sim.c         virtual CiA402 axis; now answers the error-counter block
 *
 * The virtual slave reports zero error counters on a healthy bus. The
 * slavesim_set_rxcrc() hook lets us stuff an RX/CRC counter into the ESC error
 * block so we can prove ecat_diag reads it back and flags the slave.
 *
 * Tests:
 *   1. single axis, healthy   -> read_ok, all counters 0, has_errors == 0
 *   2. single axis, injected  -> RX/CRC counter read back, has_errors == 1
 *   3. two axes, one faulty   -> only the faulty slave is flagged
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
#include "config_loader.h"   /* g_ecat_config */
#include "ecat_coe.h"        /* profile-dispatching PO2SOconfig hook */
#include "vendors.h"
#include "ecat_diag.h"
#include "ecat_foe.h"
#include "slave_sim.h"

static char IOmap[8192];
static int  g_pass = 0, g_fail = 0;

#define CHECK(cond, ...) do { \
      if (cond) { g_pass++; printf("  [PASS] " __VA_ARGS__); printf("\n"); } \
      else      { g_fail++; printf("  [FAIL] " __VA_ARGS__); printf("\n"); } \
   } while (0)

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

/* Bring the virtual bus up to OPERATIONAL with `nslaves` axes. */
static int run_to_op(int nslaves)
{
   int chk, s;
   slavesim_init_n(nslaves);
   if (!ec_init("sim")) return -1;
   if (ec_config_init(FALSE) <= 0) { ec_close(); return -2; }
   for (s = 1; s <= ec_slavecount; s++)
      ec_slave[s].PO2SOconfig = &ecat_coe_po2so_config;
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

static int rx_sum(const ecat_diag_slave_t *d)
{
   int p, s = 0;
   for (p = 0; p < ECAT_DIAG_MAX_PORTS; p++) s += d->rx_error[p];
   return s;
}

/* ---------------------------------------------------------------------- */
static void test_single_healthy(void)
{
   ecat_diag_slave_t diag[ECAT_CFG_MAX_SLAVES];
   int rc, r, n;

   printf("TEST single axis, healthy diagnostics\n");
   memset(&g_ecat_config, 0, sizeof(g_ecat_config));
   rc = ecat_config_load_string(AXIS_JSON, &g_ecat_config);
   CHECK(rc == 0, "config parsed (%s)", rc == 0 ? "ok" : ecat_config_last_error());
   if (rc != 0) return;

   r = run_to_op(1);
   CHECK(r == 0, "virtual bus reached OPERATIONAL");
   if (r != 0) { ec_close(); return; }

   n = ecat_diag_scan(diag, ECAT_CFG_MAX_SLAVES);
   CHECK(n == 1, "scanned 1 slave (got %d)", n);
   CHECK(n == 1 && diag[0].read_ok, "slave 1 register block read ok");
   CHECK(n == 1 && diag[0].al_state == EC_STATE_OPERATIONAL,
         "slave 1 AL state = OP (0x%02x)", n == 1 ? diag[0].al_state : 0);
   CHECK(n == 1 && rx_sum(&diag[0]) == 0, "slave 1 RX/CRC counters clear");
   CHECK(ecat_diag_has_errors(diag, n) == 0, "no errors flagged on healthy bus");

   ecat_diag_print("single axis healthy", diag, n);
   ec_close();
}

/* ---------------------------------------------------------------------- */
static void test_single_injected(void)
{
   ecat_diag_slave_t diag[ECAT_CFG_MAX_SLAVES];
   int rc, r, n;

   printf("TEST single axis, injected RX/CRC error\n");
   memset(&g_ecat_config, 0, sizeof(g_ecat_config));
   rc = ecat_config_load_string(AXIS_JSON, &g_ecat_config);
   if (rc != 0) { CHECK(0, "config parsed (%s)", ecat_config_last_error()); return; }

   r = run_to_op(1);
   CHECK(r == 0, "virtual bus reached OPERATIONAL");
   if (r != 0) { ec_close(); return; }

   /* Healthy first. */
   n = ecat_diag_scan(diag, ECAT_CFG_MAX_SLAVES);
   CHECK(ecat_diag_has_errors(diag, n) == 0, "clean before injection");

   /* Stuff 7 RX/CRC errors on port 1 of slave 1. */
   slavesim_set_rxcrc(1, 1, 7);
   n = ecat_diag_scan(diag, ECAT_CFG_MAX_SLAVES);
   CHECK(n == 1 && diag[0].read_ok, "re-read ok after injection");
   CHECK(n == 1 && diag[0].rx_error[1] == 7,
         "port-1 RX/CRC counter reads back 7 (got %d)",
         n == 1 ? diag[0].rx_error[1] : -1);
   CHECK(n == 1 && rx_sum(&diag[0]) == 7, "RX/CRC total = 7");
   CHECK(ecat_diag_has_errors(diag, n) == 1, "error flagged after injection");

   ecat_diag_print("single axis injected", diag, n);
   ec_close();
}

/* ---------------------------------------------------------------------- */
static void test_two_axis_one_faulty(void)
{
   ecat_diag_slave_t diag[ECAT_CFG_MAX_SLAVES];
   int rc, r, n;

   printf("TEST two axes, only slave 2 faulty\n");
   memset(&g_ecat_config, 0, sizeof(g_ecat_config));
   rc = ecat_config_load_string(TWO_AXIS_JSON, &g_ecat_config);
   if (rc != 0) { CHECK(0, "config parsed (%s)", ecat_config_last_error()); return; }

   r = run_to_op(2);
   CHECK(r == 0, "virtual bus reached OPERATIONAL");
   if (r != 0) { ec_close(); return; }

   slavesim_set_rxcrc(2, 0, 3);     /* fault on slave 2, port 0 only */
   n = ecat_diag_scan(diag, ECAT_CFG_MAX_SLAVES);
   CHECK(n == 2, "scanned 2 slaves (got %d)", n);
   CHECK(n == 2 && rx_sum(&diag[0]) == 0, "slave 1 remains clean");
   CHECK(n == 2 && diag[1].rx_error[0] == 3,
         "slave 2 port-0 RX/CRC = 3 (got %d)", n == 2 ? diag[1].rx_error[0] : -1);
   CHECK(ecat_diag_has_errors(&diag[0], 1) == 0, "slave 1 not flagged");
   CHECK(ecat_diag_has_errors(&diag[1], 1) == 1, "slave 2 flagged");

   ecat_diag_print("two axes, slave 2 faulty", diag, n);
   ec_close();
}

/* The virtual slave's SII reports these (build_sii in slave_sim.c). */
#define SIM_VENDOR   0x0000009AU
#define SIM_PRODUCT  0x00030924U
#define SIM_REVISION 0x00010420U

/* ---------------------------------------------------------------------- */
static void test_identity(void)
{
   int rc, r;

   printf("TEST slave identity verification\n");
   memset(&g_ecat_config, 0, sizeof(g_ecat_config));
   rc = ecat_config_load_string(AXIS_JSON, &g_ecat_config);
   if (rc != 0) { CHECK(0, "config parsed (%s)", ecat_config_last_error()); return; }

   r = run_to_op(1);
   CHECK(r == 0, "virtual bus reached OPERATIONAL");
   if (r != 0) { ec_close(); return; }

   /* No expected_* set -> nothing to check -> 0 mismatches. */
   CHECK(ecat_diag_verify_identity(&g_ecat_config) == 0, "no ids configured -> ok");

   /* Correct expected identity -> match. */
   g_ecat_config.slaves[0].expected_vendor_id    = SIM_VENDOR;
   g_ecat_config.slaves[0].expected_product_code = SIM_PRODUCT;
   g_ecat_config.slaves[0].expected_revision     = SIM_REVISION;
   CHECK(ecat_diag_verify_identity(&g_ecat_config) == 0, "correct ids -> 0 mismatches");

   /* Wrong product code -> one mismatch. */
   g_ecat_config.slaves[0].expected_product_code = 0xDEADBEEF;
   CHECK(ecat_diag_verify_identity(&g_ecat_config) == 1, "wrong product -> 1 mismatch");

   /* Wrong vendor + product + revision -> three mismatches. */
   g_ecat_config.slaves[0].expected_vendor_id = 0x1;
   g_ecat_config.slaves[0].expected_revision  = 0x2;
   CHECK(ecat_diag_verify_identity(&g_ecat_config) == 3, "all three wrong -> 3 mismatches");

   ec_close();
}

/* ---------------------------------------------------------------------- */
static void test_emergency(void)
{
   ec_mbxbuft mbx;
   int rc, r, drained;

   printf("TEST CoE Emergency (EMCY) capture\n");
   memset(&g_ecat_config, 0, sizeof(g_ecat_config));
   rc = ecat_config_load_string(AXIS_JSON, &g_ecat_config);
   if (rc != 0) { CHECK(0, "config parsed (%s)", ecat_config_last_error()); return; }

   r = run_to_op(1);
   CHECK(r == 0, "virtual bus reached OPERATIONAL");
   if (r != 0) { ec_close(); return; }

   CHECK(ecat_diag_drain_errors() == 0, "error list empty at start");

   /* Slave raises an emergency (e.g. 0x7300 sensor fault, error reg 0x01). */
   slavesim_queue_emergency(1, 0x7300, 0x01);
   ec_mbxreceive(1, &mbx, 20000);        /* master reads mailbox -> SOEM parses EMCY */
   CHECK(ec_iserror(), "emergency pushed onto SOEM error list");

   drained = ecat_diag_drain_errors();
   CHECK(drained == 1, "drain returned 1 emergency (got %d)", drained);
   CHECK(!ec_iserror(), "error list empty after drain");

   ec_close();
}

/* ---------------------------------------------------------------------- */
static void test_topology(void)
{
   ecat_topo_t topo[ECAT_CFG_MAX_SLAVES];
   int rc, r, n;

   printf("TEST bus topology / link-state\n");
   memset(&g_ecat_config, 0, sizeof(g_ecat_config));
   rc = ecat_config_load_string(TWO_AXIS_JSON, &g_ecat_config);
   if (rc != 0) { CHECK(0, "config parsed (%s)", ecat_config_last_error()); return; }

   r = run_to_op(2);
   CHECK(r == 0, "virtual bus reached OPERATIONAL");
   if (r != 0) { ec_close(); return; }

   n = ecat_diag_topology_scan(topo, ECAT_CFG_MAX_SLAVES);
   CHECK(n == 2, "scanned 2 slaves (got %d)", n);
   /* slave 1 is mid-chain: port0 (upstream) + port1 (downstream) = 2 links. */
   CHECK(n == 2 && topo[0].topology == 2,
         "slave 1 has 2 active links (got %d)", n == 2 ? topo[0].topology : -1);
   CHECK(n == 2 && (topo[0].activeports & 0x03) == 0x03,
         "slave 1 ports 0+1 active (0x%02x)", n == 2 ? topo[0].activeports : 0);
   /* slave 2 is end-of-line: only port0 open = 1 link. */
   CHECK(n == 2 && topo[1].topology == 1,
         "slave 2 is end-of-line, 1 link (got %d)", n == 2 ? topo[1].topology : -1);
   CHECK(n == 2 && topo[1].parent == 1,
         "slave 2 parent is slave 1 (got %d)", n == 2 ? topo[1].parent : -1);

   ecat_diag_topology_print("two axes, line", topo, n);
   ec_close();
}

/* ---------------------------------------------------------------------- */
static void test_dc_sync(void)
{
   ecat_dc_diff_t dcd[ECAT_CFG_MAX_SLAVES];
   int rc, r, n;

   printf("TEST DC sync-window monitoring\n");
   memset(&g_ecat_config, 0, sizeof(g_ecat_config));
   rc = ecat_config_load_string(AXIS_JSON, &g_ecat_config);
   if (rc != 0) { CHECK(0, "config parsed (%s)", ecat_config_last_error()); return; }

   r = run_to_op(1);
   CHECK(r == 0, "virtual bus reached OPERATIONAL");
   if (r != 0) { ec_close(); return; }

   /* Locked clock: 0 ns deviation -> inside the window. */
   n = ecat_diag_dc_scan(dcd, ECAT_CFG_MAX_SLAVES);
   CHECK(n == 1 && dcd[0].read_ok, "DC diff read ok");
   CHECK(n == 1 && dcd[0].diff_ns == 0, "deviation 0 ns at start");
   CHECK(ecat_diag_dc_out_of_window(dcd, n, ECAT_DC_SYNC_WINDOW_NS) == 0,
         "locked clock is inside the sync window");

   /* Drifted clock: 5000 ns ahead -> outside a 1000 ns window. */
   slavesim_set_dcdiff(1, 5000, 1);
   n = ecat_diag_dc_scan(dcd, ECAT_CFG_MAX_SLAVES);
   CHECK(n == 1 && dcd[0].diff_ns == 5000,
         "deviation reads back 5000 ns (got %u)", n == 1 ? dcd[0].diff_ns : 0);
   CHECK(n == 1 && dcd[0].ahead == 1, "sign decoded as 'ahead'");
   CHECK(ecat_diag_dc_out_of_window(dcd, n, ECAT_DC_SYNC_WINDOW_NS) == 1,
         "drifted clock flagged out-of-window");

   ecat_diag_dc_print("drifted 5us ahead", dcd, n, ECAT_DC_SYNC_WINDOW_NS);
   ec_close();
}

/* ---------------------------------------------------------------------- */
static void test_foe(void)
{
   uint8_t image[600];                /* > one 512 B FoE packet -> multi-packet */
   uint8_t got[1024];
   int rc, r, n, i, ok;

   printf("TEST FoE firmware download\n");
   memset(&g_ecat_config, 0, sizeof(g_ecat_config));
   rc = ecat_config_load_string(AXIS_JSON, &g_ecat_config);
   if (rc != 0) { CHECK(0, "config parsed (%s)", ecat_config_last_error()); return; }

   r = run_to_op(1);
   CHECK(r == 0, "virtual bus reached OPERATIONAL");
   if (r != 0) { ec_close(); return; }

   for (i = 0; i < (int)sizeof(image); i++) image[i] = (uint8_t)(i & 0xff);

   rc = ecat_foe_download_buffer(1, "fw.bin", 0x0, image, (int)sizeof(image), EC_TIMEOUTRXM);
   CHECK(rc == 0, "ec_FOEwrite completed ok (rc %d)", rc);

   n = slavesim_foe_received(1, got, sizeof(got));
   CHECK(n == (int)sizeof(image),
         "slave received all %d bytes (got %d)", (int)sizeof(image), n);
   ok = (n == (int)sizeof(image));
   for (i = 0; ok && i < n; i++)
      if (got[i] != (uint8_t)(i & 0xff)) ok = 0;
   CHECK(ok, "FoE payload matches byte-for-byte");

   ec_close();
}

int main(int argc, char **argv)
{
   if (argc > 1 && strcmp(argv[1], "-v") == 0) slavesim_set_verbose(1);

   vendors_register_all();

   printf("=== ecat_diag <- SOEM master <- virtual slave(s) (no hardware) ===\n");
   test_single_healthy();
   test_single_injected();
   test_two_axis_one_faulty();
   test_identity();
   test_emergency();
   test_topology();
   test_dc_sync();
   test_foe();

   printf("\n=== %d passed, %d failed ===\n", g_pass, g_fail);
   return g_fail ? 1 : 0;
}
