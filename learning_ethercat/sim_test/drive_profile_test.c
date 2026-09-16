/** \file
 * \brief Unit tests for the drive-profile registry and generic setup
 *        (src/drive/drive_profile.c, src/vendors/).
 *
 * Pure host test. CoE access is bound to an in-memory object dictionary rather
 * than SOEM, which is precisely the point of drive_coe_ops_t: a vendor profile
 * can be verified with no bus, no hardware and no stack.
 */
#include <stdio.h>
#include <string.h>

#include "drive_profile.h"
#include "cia402.h"
#include "vendors.h"

static int g_pass = 0, g_fail = 0;

#define CHECK(cond, ...) do { \
      if (cond) { g_pass++; printf("  [PASS] " __VA_ARGS__); printf("\n"); } \
      else      { g_fail++; printf("  [FAIL] " __VA_ARGS__); printf("\n"); } \
   } while (0)

/* ------------------------------------------------------------------------ */
/* In-memory object dictionary standing in for a drive                       */
/* ------------------------------------------------------------------------ */
#define OD_MAX 256

typedef struct { uint16_t index; uint8_t sub; uint32_t value; int writes; } od_e_t;

typedef struct {
   od_e_t e[OD_MAX];
   int    count;
   int    write_calls;
} od_t;

static od_t g_od;

static od_e_t *od_slot(uint16_t index, uint8_t sub)
{
   int i;
   for (i = 0; i < g_od.count; i++)
      if (g_od.e[i].index == index && g_od.e[i].sub == sub)
         return &g_od.e[i];
   if (g_od.count >= OD_MAX)
      return NULL;
   g_od.e[g_od.count].index = index;
   g_od.e[g_od.count].sub   = sub;
   g_od.e[g_od.count].value = 0;
   g_od.e[g_od.count].writes = 0;
   return &g_od.e[g_od.count++];
}

static int od_write(void *user, uint16_t slave, uint16_t index, uint8_t sub,
                    int ca, int size, const void *data)
{
   od_e_t *s;
   uint32_t v = 0;

   (void)user; (void)slave; (void)ca;
   if (size > 0 && size <= 4)
      memcpy(&v, data, (size_t)size);

   s = od_slot(index, sub);
   if (!s)
      return 0;
   s->value = v;
   s->writes++;
   g_od.write_calls++;
   return 1;    /* working counter */
}

static int od_read(void *user, uint16_t slave, uint16_t index, uint8_t sub,
                   int ca, int *size, void *data)
{
   od_e_t *s;

   (void)user; (void)slave; (void)ca;
   s = od_slot(index, sub);
   if (!s || !size || *size <= 0 || *size > 4)
      return 0;
   memcpy(data, &s->value, (size_t)*size);
   return 1;
}

static const drive_coe_ops_t g_ops = { od_write, od_read, NULL };

static uint32_t od_get(uint16_t index, uint8_t sub)
{
   return od_slot(index, sub)->value;
}

static void od_clear(void) { memset(&g_od, 0, sizeof(g_od)); }

/* ------------------------------------------------------------------------ */
/* Configuration helpers                                                     */
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

static void build_cfg(ecat_slave_config_t *sc, const char *profile, int n_rx_extra)
{
   int i;

   memset(sc, 0, sizeof(*sc));
   sc->position = 1;
   snprintf(sc->name, sizeof(sc->name), "Axis A");
   snprintf(sc->profile, sizeof(sc->profile), "%s", profile);
   sc->mode_of_operation  = CIA402_MODE_CSP;
   sc->rxpdo_map_base     = ECAT_CFG_DEF_RXMAP_BASE;
   sc->txpdo_map_base     = ECAT_CFG_DEF_TXMAP_BASE;
   sc->sm2_assign         = ECAT_CFG_DEF_SM2_ASSIGN;
   sc->sm3_assign         = ECAT_CFG_DEF_SM3_ASSIGN;
   sc->map_entries_per_obj = ECAT_CFG_DEF_MAP_PER_OBJ;

   add_rx(sc, CIA402_OD_CONTROLWORD,     16);
   add_rx(sc, CIA402_OD_POSITION_OFFSET, 32);
   for (i = 0; i < n_rx_extra; i++)
      add_rx(sc, CIA402_OD_VELOCITY_OFFSET, 32);

   add_tx(sc, CIA402_OD_STATUSWORD,      16);
   add_tx(sc, CIA402_OD_POSITION_ACTUAL, 32);
}

/* ------------------------------------------------------------------------ */
/* Tests                                                                     */
/* ------------------------------------------------------------------------ */

static void test_registry(void)
{
   printf("TEST registry\n");

   drive_profile_reset();
   CHECK(drive_profile_count() == 0, "registry starts empty");
   CHECK(drive_profile_generic() != NULL, "generic profile always exists");
   CHECK(strcmp(drive_profile_generic()->id, "cia402_generic") == 0,
         "generic profile id");

   CHECK(vendors_register_all() == 0, "vendors_register_all succeeds");
   CHECK(drive_profile_count() == 2, "two Elmo families registered (got %d)",
         drive_profile_count());
   CHECK(drive_profile_find("elmo_platinum") != NULL, "elmo_platinum found");
   CHECK(drive_profile_find("elmo_gold") != NULL,     "elmo_gold found");
   CHECK(drive_profile_find("nope") == NULL,          "unknown id returns NULL");

   printf("   (expect a duplicate-registration complaint below)\n");
   CHECK(drive_profile_register(drive_profile_find("elmo_platinum")) == -1,
         "registering the same id twice is refused");

   drive_profile_list();
}

static void test_profile_selection(void)
{
   ecat_slave_config_t sc;

   printf("TEST profile selection from config\n");
   drive_profile_reset();
   vendors_register_all();

   build_cfg(&sc, "elmo_platinum", 0);
   CHECK(drive_profile_for_slave(&sc) == drive_profile_find("elmo_platinum"),
         "known profile selected by name");

   build_cfg(&sc, "copley_accelnet", 0);
   printf("   (expect a fallback notice below)\n");
   CHECK(drive_profile_for_slave(&sc) == drive_profile_generic(),
         "unknown profile falls back to generic instead of failing");

   build_cfg(&sc, "", 0);
   CHECK(drive_profile_for_slave(&sc) == drive_profile_generic(),
         "empty profile field uses generic");
}

/* The PDO mapping written over CoE must match the configuration exactly. */
static void test_generic_setup_writes_the_map(void)
{
   ecat_slave_config_t sc;
   drive_ctx_t ctx;

   printf("TEST generic setup programs the PDO map\n");
   drive_profile_reset();
   od_clear();

   build_cfg(&sc, "cia402_generic", 0);
   ctx.coe = &g_ops; ctx.slave = 1; ctx.cfg = &sc;

   CHECK(drive_profile_run_setup(drive_profile_generic(), &ctx) == 1,
         "setup reports success");

   CHECK(od_get(CIA402_OD_MODES_OF_OPERATION, 0) == CIA402_MODE_CSP,
         "mode of operation written to 0x6060");

   /* Rx: 0x1600 sub1 = 0x60400010, sub2 = 0x60B00020, count = 2 */
   CHECK(od_get(0x1600, 1) == 0x60400010u, "0x1600:01 = controlword 16 bit "
         "(got 0x%08X)", od_get(0x1600, 1));
   CHECK(od_get(0x1600, 2) == 0x60B00020u, "0x1600:02 = position offset 32 bit");
   CHECK(od_get(0x1600, 0) == 2,           "0x1600:00 entry count = 2");
   CHECK(od_get(0x1C12, 1) == 0x1600,      "SM2 assigned 0x1600");
   CHECK(od_get(0x1C12, 0) == 1,           "SM2 assign count = 1");

   CHECK(od_get(0x1A00, 1) == 0x60410010u, "0x1A00:01 = statusword");
   CHECK(od_get(0x1A00, 2) == 0x60640020u, "0x1A00:02 = position actual");
   CHECK(od_get(0x1C13, 1) == 0x1A00,      "SM3 assigned 0x1A00");
}

/* More entries than fit in one mapping object must spill into the next. */
static void test_map_splitting(void)
{
   ecat_slave_config_t sc;
   drive_ctx_t ctx;

   printf("TEST long PDO lists split across mapping objects\n");
   drive_profile_reset();
   od_clear();

   build_cfg(&sc, "cia402_generic", 9);   /* 2 + 9 = 11 Rx entries, 8 per object */
   ctx.coe = &g_ops; ctx.slave = 1; ctx.cfg = &sc;
   drive_profile_run_setup(drive_profile_generic(), &ctx);

   CHECK(sc.rxpdo_count == 11, "11 Rx entries configured");
   CHECK(od_get(0x1600, 0) == 8, "first object holds 8 entries");
   CHECK(od_get(0x1601, 0) == 3, "second object holds the remaining 3");
   CHECK(od_get(0x1C12, 1) == 0x1600 && od_get(0x1C12, 2) == 0x1601,
         "both objects assigned to SM2");
   CHECK(od_get(0x1C12, 0) == 2, "SM2 assign count = 2");
}

static void test_startup_sdos(void)
{
   ecat_slave_config_t sc;
   drive_ctx_t ctx;

   printf("TEST start-up SDOs from the config are applied\n");
   drive_profile_reset();
   od_clear();

   build_cfg(&sc, "cia402_generic", 0);
   sc.startup_sdo[0].index = CIA402_OD_MAX_TORQUE;
   sc.startup_sdo[0].subindex = 0;
   sc.startup_sdo[0].size = 2;
   sc.startup_sdo[0].value = 0x03E8;
   snprintf(sc.startup_sdo[0].comment, sizeof(sc.startup_sdo[0].comment),
            "max torque");
   sc.startup_sdo_count = 1;

   ctx.coe = &g_ops; ctx.slave = 1; ctx.cfg = &sc;
   drive_profile_run_setup(drive_profile_generic(), &ctx);

   CHECK(od_get(CIA402_OD_MAX_TORQUE, 0) == 0x03E8,
         "max torque written before the mapping");
}

/* The Platinum hook must clear the stale mapping objects of both axis groups
 * before the generic code writes the new map. */
static void test_elmo_platinum_pre_setup(void)
{
   ecat_slave_config_t sc;
   drive_ctx_t ctx;
   const drive_profile_t *p;

   printf("TEST Elmo Platinum pre-setup clears stale mapping objects\n");
   drive_profile_reset();
   vendors_register_all();
   od_clear();

   build_cfg(&sc, "elmo_platinum", 0);
   p = drive_profile_for_slave(&sc);
   ctx.coe = &g_ops; ctx.slave = 1; ctx.cfg = &sc;

   CHECK(drive_profile_run_setup(p, &ctx) == 1, "setup succeeds");
   CHECK(od_slot(0x1610, 0)->writes > 0, "second-axis Rx object 0x1610 cleared");
   CHECK(od_slot(0x1A10, 0)->writes > 0, "second-axis Tx object 0x1A10 cleared");
   /* The generic mapping still ran afterwards and won. */
   CHECK(od_get(0x1600, 0) == 2, "generic mapping applied after the clear");
}

static void test_fault_strings(void)
{
   const drive_profile_t *elmo;

   printf("TEST fault decoding\n");
   drive_profile_reset();
   vendors_register_all();
   elmo = drive_profile_find("elmo_platinum");

   CHECK(strcmp(drive_fault_string(elmo, 0x8611),
                "following error too large") == 0, "vendor code decoded");
   CHECK(strcmp(drive_fault_string(elmo, 0xFF01),
                "motor stuck (Elmo specific)") == 0, "manufacturer range decoded");
   /* Unknown to the vendor -> standard class, not a crash or empty string. */
   CHECK(strcmp(drive_fault_string(elmo, 0x4210), "temperature") == 0,
         "unknown vendor code falls back to the CiA 402 class");
   CHECK(strcmp(drive_fault_string(NULL, 0x3210), "voltage") == 0,
         "no profile at all still yields a class");
}

/* Prove the plug-in path end to end with a family that has its own setup and
 * exists only in this test - exactly what adding a new vendor looks like. */
static int g_acme_setup_calls;

static int acme_setup(const drive_ctx_t *ctx)
{
   uint32_t magic = 0xC0FFEE;
   g_acme_setup_calls++;
   ctx->coe->sdo_write(ctx->coe->user, ctx->slave, 0x2000, 0, 0,
                       (int)sizeof(magic), &magic);
   return 0;
}

static const char *acme_fault(uint16_t code)
{
   return (code == 0x1234) ? "acme widget jam" : NULL;
}

static const drive_profile_t g_acme = {
   "acme_servo", "Test-only drive family",
   0x00AC4E00u, NULL, 0,
   NULL, acme_setup, NULL, acme_fault, NULL
};

static void test_third_party_profile(void)
{
   ecat_slave_config_t sc;
   drive_ctx_t ctx;

   printf("TEST adding a new drive family needs no core changes\n");
   drive_profile_reset();
   vendors_register_all();
   od_clear();
   g_acme_setup_calls = 0;

   CHECK(drive_profile_register(&g_acme) == 0, "new family registers");
   CHECK(drive_profile_count() == 3, "registry now holds 3 families");

   build_cfg(&sc, "acme_servo", 0);
   ctx.coe = &g_ops; ctx.slave = 1; ctx.cfg = &sc;

   CHECK(drive_profile_run_setup(drive_profile_for_slave(&sc), &ctx) == 1,
         "its setup runs");
   CHECK(g_acme_setup_calls == 1, "custom setup hook called exactly once");
   CHECK(od_get(0x2000, 0) == 0xC0FFEE, "custom setup wrote its own object");
   CHECK(od_slot(0x1600, 1)->writes == 0,
         "custom setup replaced the generic mapping entirely");
   CHECK(strcmp(drive_fault_string(&g_acme, 0x1234), "acme widget jam") == 0,
         "its fault decoder is used");
}

int main(void)
{
   printf("=== drive profile unit tests ===\n");

   test_registry();
   test_profile_selection();
   test_generic_setup_writes_the_map();
   test_map_splitting();
   test_startup_sdos();
   test_elmo_platinum_pre_setup();
   test_fault_strings();
   test_third_party_profile();

   printf("\n=== %d passed, %d failed ===\n", g_pass, g_fail);
   return g_fail ? 1 : 0;
}
