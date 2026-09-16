/** \file
 * \brief End-to-end multi-vendor test: a simulated bus with no Elmo on it.
 *
 * This is the test the whole refactor exists for. It defines a drive family
 * ("acme_servo") that lives entirely in this file - its own vendor and product
 * codes, its own start-up quirk, its own fault table - registers it, makes the
 * virtual slave impersonate it, and then runs the *production* code path:
 *
 *    JSON config -> profile registry -> PO2SOconfig -> PDO mapping over CoE
 *                -> ec_config_map -> axis binding -> CiA 402 enable ladder
 *
 * Nothing Elmo-specific is involved anywhere, and no file outside this one was
 * modified to make it work. If this passes, adding a real Copley/Maxon/Beckhoff
 * drive is a configuration exercise plus (optionally) one small vendor file.
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

#include "slave_sim.h"
#include "slave_sim_profile.h"
#include "config_loader.h"
#include "drive_profile.h"
#include "ecat_coe.h"
#include "axis.h"
#include "cia402.h"

static char IOmap[4096];
static int  g_pass = 0, g_fail = 0;

#define CHECK(cond, ...) do { \
      if (cond) { g_pass++; printf("  [PASS] " __VA_ARGS__); printf("\n"); } \
      else      { g_fail++; printf("  [FAIL] " __VA_ARGS__); printf("\n"); } \
   } while (0)

/* ------------------------------------------------------------------------ */
/* A drive family that exists only here                                      */
/* ------------------------------------------------------------------------ */
#define ACME_VENDOR   0x0000BEEFu
#define ACME_PRODUCT  0x00001234u
#define ACME_REVISION 0x00000007u

/* Acme drives need a proprietary "unlock" object written before their mapping
 * objects will accept anything. Exactly the kind of quirk a vendor file is for. */
#define ACME_OD_UNLOCK 0x2F00u

static int g_acme_unlock_calls;

static int acme_pre_setup(const drive_ctx_t *ctx)
{
   uint32_t key = 0x5A5A5A5Au;

   g_acme_unlock_calls++;
   ctx->coe->sdo_write(ctx->coe->user, ctx->slave, ACME_OD_UNLOCK, 0, 0,
                       (int)sizeof(key), &key);
   return 0;
}

static const char *acme_fault_string(uint16_t code)
{
   return (code == 0xFF42) ? "acme flux capacitor desync" : NULL;
}

static int acme_sim_identity(drive_identity_t *out)
{
   out->vendor_id    = ACME_VENDOR;
   out->product_code = ACME_PRODUCT;
   out->revision     = ACME_REVISION;
   return 0;
}

static const uint32_t g_acme_products[] = { ACME_PRODUCT };

static const drive_profile_t g_acme = {
   "acme_servo",
   "Acme Dynamics AX-1 servo drive",
   ACME_VENDOR,
   g_acme_products, 1,
   acme_pre_setup,      /* pre_setup  */
   NULL,                /* setup: plain CiA 402 is fine */
   NULL,                /* post_setup */
   acme_fault_string,
   acme_sim_identity
};

/* ------------------------------------------------------------------------ */
/* Configuration naming that family                                          */
/* ------------------------------------------------------------------------ */
static const char ACME_JSON[] =
"{ \"version\": 1,"
"  \"network\": { \"interface\": \"sim\", \"cycle_time_us\": 1000,"
"                 \"number_of_cycles\": 0, \"distributed_clock\": 0,"
"                 \"verify_identity\": 1 },"
"  \"slaves\": ["
"    { \"position\": 1, \"name\": \"Acme Axis\", \"profile\": \"acme_servo\","
"      \"mode_of_operation\": 8,"
"      \"expected_vendor_id\": \"0x0000BEEF\","
"      \"expected_product_code\": \"0x00001234\","
"      \"expected_revision\": \"0x00000007\","
"      \"startup_sdo\": ["
"        { \"index\": \"0x6072\", \"subindex\": 0, \"size\": 2,"
"          \"value\": \"0x03E8\", \"comment\": \"max torque\" } ],"
"      \"rxpdo\": ["
"        { \"index\": \"0x6040\", \"subindex\": 0, \"bitlen\": 16, \"name\": \"Controlword\" },"
"        { \"index\": \"0x60B0\", \"subindex\": 0, \"bitlen\": 32, \"name\": \"Position offset\" } ],"
"      \"txpdo\": ["
"        { \"index\": \"0x6041\", \"subindex\": 0, \"bitlen\": 16, \"name\": \"Statusword\" },"
"        { \"index\": \"0x6064\", \"subindex\": 0, \"bitlen\": 32, \"name\": \"Position actual\" } ] }"
"  ] }";

/* ------------------------------------------------------------------------ */
/* Bus bring-up using the production hook                                    */
/* ------------------------------------------------------------------------ */
static int run_to_op(void)
{
   int chk, s;

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

static void test_identity(void)
{
   uint32_t v = 0, p = 0, r = 0;

   printf("TEST virtual bus impersonates the Acme family\n");

   drive_profile_reset();
   CHECK(drive_profile_register(&g_acme) == 0, "acme_servo registered");

   CHECK(slavesim_impersonate("acme_servo") == 0, "virtual slave impersonates it");
   slavesim_get_identity(&v, &p, &r);
   CHECK(v == ACME_VENDOR && p == ACME_PRODUCT && r == ACME_REVISION,
         "SII identity is Acme, not Elmo (0x%08X / 0x%08X)", v, p);

   printf("   (expect a complaint about an unregistered profile below)\n");
   CHECK(slavesim_impersonate("does_not_exist") != 0,
         "impersonating an unknown family is refused");
}

static void test_bringup(void)
{
   uint32_t sii_vendor, sii_product;
   const ecat_slave_config_t *sc;
   int rc;

   printf("TEST full bring-up of a non-Elmo bus\n");

   g_acme_unlock_calls = 0;
   slavesim_init_n(1);

   memset(&g_ecat_config, 0, sizeof(g_ecat_config));
   rc = ecat_config_load_string(ACME_JSON, &g_ecat_config);
   CHECK(rc == 0, "config parsed (%s)", rc ? ecat_config_last_error() : "ok");
   CHECK(g_ecat_config.slave_count == 1, "one slave configured");

   sc = ecat_config_find_slave(&g_ecat_config, 1);
   CHECK(sc != NULL && strcmp(sc->profile, "acme_servo") == 0,
         "slave names the acme_servo profile");
   CHECK(drive_profile_for_slave(sc) == &g_acme,
         "registry resolves it to the Acme profile, not the generic fallback");

   CHECK(run_to_op() == 0, "bus reached OPERATIONAL");

   /* The bus really did present the Acme identity to SOEM. */
   sii_vendor  = ec_slave[1].eep_man;
   sii_product = ec_slave[1].eep_id;
   CHECK(sii_vendor == ACME_VENDOR, "SOEM read vendor 0x%08X", sii_vendor);
   CHECK(sii_product == ACME_PRODUCT, "SOEM read product 0x%08X", sii_product);
   CHECK(sii_vendor == sc->expected_vendor_id &&
         sii_product == sc->expected_product_code,
         "identity matches what the config expected");

   /* The vendor hook ran, and the generic mapping ran after it. */
   CHECK(g_acme_unlock_calls == 1,
         "Acme unlock quirk applied exactly once (got %d)", g_acme_unlock_calls);
   {
      uint32_t v = 0;
      CHECK(slavesim_od_get(1, ACME_OD_UNLOCK, 0, &v) && v == 0x5A5A5A5Au,
            "unlock key landed in the drive's object dictionary");
      CHECK(slavesim_od_get(1, CIA402_OD_MAX_TORQUE, 0, &v) && v == 0x03E8,
            "config start-up SDO applied");
      CHECK(slavesim_od_get(1, CIA402_OD_MODES_OF_OPERATION, 0, &v) &&
            v == CIA402_MODE_CSP, "mode of operation applied");
      CHECK(slavesim_od_get(1, 0x1600, 1, &v) && v == 0x60400010u,
            "PDO map programmed over CoE (0x1600:01 = 0x%08X)", v);
   }
}

static void test_axis_runs(void)
{
   const ecat_slave_config_t *sc = ecat_config_find_slave(&g_ecat_config, 1);
   axis_set_t axes;
   int cycle, n;

   printf("TEST the axis layer drives an Acme node like any other\n");

   memset(&axes, 0, sizeof(axes));
   n = axis_set_add_slave(&axes, sc, 1,
                          ec_slave[1].outputs, ec_slave[1].Obytes,
                          ec_slave[1].inputs,  ec_slave[1].Ibytes);
   CHECK(n == 1, "one axis bound (got %d)", n);
   printf("   (expect no complaints below)\n");
   CHECK(axis_set_validate(&axes) == 0, "PDO map satisfies CSP");

   /* The virtual slave mirrors RxPDO into TxPDO, so the statusword it reports
    * is whatever we last commanded. That is enough to prove the axis writes
    * the controlword to the right bits of the right image. */
   for (cycle = 0; cycle < 8; cycle++)
   {
      axis_read(&axes.axis[0]);
      axis_enable_step(&axes.axis[0]);
      axis_hold(&axes.axis[0]);
      ec_send_processdata();
      ec_receive_processdata(EC_TIMEOUTRET);
   }

   CHECK(axes.axis[0].controlword != 0,
         "controlword was commanded (0x%04X)", axes.axis[0].controlword);
   CHECK(ec_slave[1].outputs[0] == (axes.axis[0].controlword & 0xFF),
         "it reached byte 0 of the output image");

   axis_write_setpoint(&axes.axis[0], 0x11223344, 0, 0);
   CHECK(ec_slave[1].outputs[2] == 0x44 && ec_slave[1].outputs[5] == 0x11,
         "position offset landed at byte 2, little-endian");
}

static void test_fault_text(void)
{
   printf("TEST Acme fault decoding\n");

   CHECK(strcmp(drive_fault_string(&g_acme, 0xFF42),
                "acme flux capacitor desync") == 0, "vendor code decoded");
   CHECK(strcmp(drive_fault_string(&g_acme, 0x3210), "voltage") == 0,
         "unknown code falls back to the standard class");
}

int main(int argc, char **argv)
{
   if (argc > 1 && strcmp(argv[1], "-v") == 0) slavesim_set_verbose(1);

   printf("=== multi-vendor bring-up (no Elmo anywhere) ===\n");

   test_identity();
   test_bringup();
   test_axis_runs();
   test_fault_text();

   ec_slave[0].state = EC_STATE_INIT;
   ec_writestate(0);
   ec_close();

   printf("\n=== %d passed, %d failed ===\n", g_pass, g_fail);
   return g_fail ? 1 : 0;
}
