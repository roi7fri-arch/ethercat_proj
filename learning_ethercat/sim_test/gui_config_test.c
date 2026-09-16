/** \file
 * \brief End-to-end test: config GUI JSON -> production config loader ->
 *        SOEM master -> virtual slave(s).
 *
 * This drives the *real* production code path used on the target:
 *   config_loader.c        (parses tools/config_gui JSON into structs)
 *   elmo_config_setup.c    (PO2SOconfig hook that programs the PDO map via CoE)
 * against the in-process virtual bus. It proves that a configuration authored
 * in the GUI actually brings the drives to OP with the exact process-image
 * sizes described by the JSON.
 */
#include <stdio.h>
#include <string.h>

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
#include "slave_sim.h"

#define DEFAULT_CONFIG "../../tools/config_gui/example_config.json"

static char IOmap[8192];
static int  g_pass = 0, g_fail = 0;

#define CHECK(cond, ...) do { \
      if (cond) { g_pass++; printf("  [PASS] " __VA_ARGS__); printf("\n"); } \
      else      { g_fail++; printf("  [FAIL] " __VA_ARGS__); printf("\n"); } \
   } while (0)

static int pdo_bytes(const ecat_pdo_entry_t *e, int n)
{
   int bits = 0, i;
   for (i = 0; i < n; i++) bits += e[i].bitlen;
   return (bits + 7) / 8;
}

/* Bring the whole bus to OP using the production PO2SOconfig hook. */
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

/* Verify each mapped slave's process-image size matches its JSON PDO map. */
static void verify_sizes(void)
{
   int i;
   for (i = 0; i < g_ecat_config.slave_count; i++) {
      const ecat_slave_config_t *sc = &g_ecat_config.slaves[i];
      int ob = pdo_bytes(sc->rxpdo, sc->rxpdo_count);
      int ib = pdo_bytes(sc->txpdo, sc->txpdo_count);
      int pos = sc->position;
      CHECK(ec_slave[pos].Obytes == (uint32)ob,
            "slave %d '%s' Obytes=%d matches JSON RxPDO (%d B)",
            pos, sc->name, ec_slave[pos].Obytes, ob);
      CHECK(ec_slave[pos].Ibytes == (uint32)ib,
            "slave %d '%s' Ibytes=%d matches JSON TxPDO (%d B)",
            pos, sc->name, ec_slave[pos].Ibytes, ib);
   }
}

static void test_from_file(const char *path)
{
   printf("TEST GUI config file: %s\n", path);
   memset(&g_ecat_config, 0, sizeof(g_ecat_config));
   int rc = ecat_config_load_file(path, &g_ecat_config);
   CHECK(rc == 0, "config loaded (%s)", rc == 0 ? "ok" : ecat_config_last_error());
   if (rc != 0) return;

   printf("  interface=%s cycle=%dus slaves=%d\n",
          g_ecat_config.network.interface,
          g_ecat_config.network.cycle_time_us,
          g_ecat_config.slave_count);

   int r = run_to_op(g_ecat_config.slave_count);
   CHECK(r == 0, "bus reached OPERATIONAL from GUI config");
   verify_sizes();

   int exp = ec_group[0].outputsWKC * 2 + ec_group[0].inputsWKC;
   int wkc = ec_send_processdata(); wkc = ec_receive_processdata(EC_TIMEOUTRET);
   CHECK(wkc == exp, "cyclic WKC=%d matches expected %d", wkc, exp);
   ec_close();
}

/* A 2-axis configuration authored as if exported by the GUI. */
static const char *TWO_AXIS_JSON =
"{\n"
"  \"version\": 1,\n"
"  \"network\": { \"interface\": \"eth0\", \"cycle_time_us\": 500,\n"
"                \"number_of_cycles\": 0, \"distributed_clock\": false,\n"
"                \"sync0_shift_us\": 0 },\n"
"  \"slaves\": [\n"
"    { \"position\": 1, \"name\": \"Axis A\", \"mode_of_operation\": 8,\n"
"      \"rxpdo\": [ {\"index\":\"0x6040\",\"subindex\":0,\"bitlen\":16,\"name\":\"CW\"},\n"
"                  {\"index\":\"0x607A\",\"subindex\":0,\"bitlen\":32,\"name\":\"TargetPos\"} ],\n"
"      \"txpdo\": [ {\"index\":\"0x6041\",\"subindex\":0,\"bitlen\":16,\"name\":\"SW\"},\n"
"                  {\"index\":\"0x6064\",\"subindex\":0,\"bitlen\":32,\"name\":\"ActPos\"} ] },\n"
"    { \"position\": 2, \"name\": \"Axis B\", \"mode_of_operation\": 9,\n"
"      \"rxpdo\": [ {\"index\":\"0x6040\",\"subindex\":0,\"bitlen\":16,\"name\":\"CW\"},\n"
"                  {\"index\":\"0x60FF\",\"subindex\":0,\"bitlen\":32,\"name\":\"TargetVel\"} ],\n"
"      \"txpdo\": [ {\"index\":\"0x6041\",\"subindex\":0,\"bitlen\":16,\"name\":\"SW\"},\n"
"                  {\"index\":\"0x606C\",\"subindex\":0,\"bitlen\":32,\"name\":\"ActVel\"} ] }\n"
"  ]\n"
"}\n";

static void test_two_axis_string(void)
{
   printf("TEST GUI config (inline 2-axis JSON)\n");
   memset(&g_ecat_config, 0, sizeof(g_ecat_config));
   int rc = ecat_config_load_string(TWO_AXIS_JSON, &g_ecat_config);
   CHECK(rc == 0, "2-axis config parsed (%s)", rc == 0 ? "ok" : ecat_config_last_error());
   if (rc != 0) return;
   CHECK(g_ecat_config.slave_count == 2, "config has 2 slaves");

   int r = run_to_op(2);
   CHECK(ec_slavecount == 2, "2 slaves discovered on the virtual bus");
   CHECK(r == 0, "both axes reached OPERATIONAL");
   verify_sizes();

   int exp = ec_group[0].outputsWKC * 2 + ec_group[0].inputsWKC;
   int wkc = ec_send_processdata(); wkc = ec_receive_processdata(EC_TIMEOUTRET);
   CHECK(exp == 6, "expected WKC=6 for 2 axes");
   CHECK(wkc == exp, "cyclic WKC=%d matches expected", wkc);
   ec_close();
}

/* Vendor-agnostic config: drive profile, per-slave startup SDO list, custom
 * mapping-object bases, and verify_identity toggle. Proves the generic
 * (Elmo Platinum / Gold / ACS / generic-CiA402) config path end to end. */
static const char *GENERIC_JSON =
"{\n"
"  \"version\": 1,\n"
"  \"network\": { \"interface\": \"eth0\", \"cycle_time_us\": 500,\n"
"                \"number_of_cycles\": 0, \"distributed_clock\": false,\n"
"                \"verify_identity\": false },\n"
"  \"slaves\": [\n"
"    { \"position\": 1, \"name\": \"ACS Axis\", \"profile\": \"acs\", \"mode_of_operation\": 8,\n"
"      \"startup_sdo\": [\n"
"        {\"index\":\"0x6072\",\"subindex\":0,\"size\":2,\"value\":\"0x03E8\",\"comment\":\"max torque\"},\n"
"        {\"index\":\"0x60C2\",\"subindex\":1,\"size\":1,\"value\":1,\"comment\":\"ip time units\"} ],\n"
"      \"rxpdo\": [ {\"index\":\"0x6040\",\"subindex\":0,\"bitlen\":16,\"name\":\"CW\"},\n"
"                  {\"index\":\"0x607A\",\"subindex\":0,\"bitlen\":32,\"name\":\"TargetPos\"} ],\n"
"      \"txpdo\": [ {\"index\":\"0x6041\",\"subindex\":0,\"bitlen\":16,\"name\":\"SW\"},\n"
"                  {\"index\":\"0x6064\",\"subindex\":0,\"bitlen\":32,\"name\":\"ActPos\"} ] }\n"
"  ]\n"
"}\n";

static void test_generic_features(void)
{
   uint32_t v = 0;
   const ecat_slave_config_t *sc;
   printf("TEST vendor-agnostic config (profile / startup_sdo / map bases / verify_identity)\n");
   memset(&g_ecat_config, 0, sizeof(g_ecat_config));
   int rc = ecat_config_load_string(GENERIC_JSON, &g_ecat_config);
   CHECK(rc == 0, "generic config parsed (%s)", rc == 0 ? "ok" : ecat_config_last_error());
   if (rc != 0) return;

   sc = &g_ecat_config.slaves[0];
   CHECK(strcmp(sc->profile, "acs") == 0, "slave profile parsed = '%s'", sc->profile);
   CHECK(g_ecat_config.network.verify_identity == 0, "verify_identity=false parsed");

   /* Mapping bases default to the standard CiA402 objects when omitted. */
   CHECK(sc->rxpdo_map_base == 0x1600, "rxpdo_map_base defaults to 0x1600 (got 0x%04X)", sc->rxpdo_map_base);
   CHECK(sc->txpdo_map_base == 0x1A00, "txpdo_map_base defaults to 0x1A00 (got 0x%04X)", sc->txpdo_map_base);
   CHECK(sc->sm2_assign == 0x1C12, "sm2_assign defaults to 0x1C12 (got 0x%04X)", sc->sm2_assign);
   CHECK(sc->sm3_assign == 0x1C13, "sm3_assign defaults to 0x1C13 (got 0x%04X)", sc->sm3_assign);
   CHECK(sc->map_entries_per_obj == 8, "map_entries_per_obj defaults to 8 (got %d)", sc->map_entries_per_obj);

   CHECK(sc->startup_sdo_count == 2, "startup_sdo parsed 2 entries (got %d)", sc->startup_sdo_count);
   CHECK(sc->startup_sdo[0].index == 0x6072 && sc->startup_sdo[0].size == 2 &&
         sc->startup_sdo[0].value == 0x03E8,
         "startup_sdo[0] = 0x6072:0 size 2 val 0x3E8");

   /* Bring the axis to OP and confirm the master actually wrote the startup SDOs. */
   int r = run_to_op(1);
   CHECK(r == 0, "axis reached OPERATIONAL with generic config");
   CHECK(slavesim_od_get(1, 0x6072, 0, &v) && v == 0x03E8,
         "startup SDO 0x6072 applied to slave (read back 0x%X)", v);
   CHECK(slavesim_od_get(1, 0x60C2, 1, &v) && v == 1,
         "startup SDO 0x60C2:1 applied to slave (read back 0x%X)", v);
   ec_close();
}

int main(int argc, char *argv[])
{
   const char *path = (argc > 1 && argv[1][0] != '-') ? argv[1] : DEFAULT_CONFIG;
   if (argc > 1 && strcmp(argv[argc - 1], "-v") == 0) slavesim_set_verbose(1);

   vendors_register_all();

   printf("=== GUI config -> stack integration test ===\n");
   test_from_file(path);
   test_two_axis_string();
   test_generic_features();

   printf("\n=== %d passed, %d failed ===\n", g_pass, g_fail);
   return g_fail ? 1 : 0;
}
