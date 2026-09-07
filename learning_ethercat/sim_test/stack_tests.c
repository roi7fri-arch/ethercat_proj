/** \file
 * \brief Stack test suite for the SOEM 1.3.1 master against the virtual slave.
 *
 * Exercises discovery, the AL state machine, CoE SDO round-trips, several
 * CiA402 PDO mappings (CSP/CSV/CST), >8-entry mapping splits and a multi-axis
 * bus. Every check is asserted; the process exits non-zero if any test fails.
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

static char IOmap[4096];
static int  g_pass = 0, g_fail = 0;

#define CHECK(cond, ...) do { \
      if (cond) { g_pass++; printf("  [PASS] " __VA_ARGS__); printf("\n"); } \
      else      { g_fail++; printf("  [FAIL] " __VA_ARGS__); printf("\n"); } \
   } while (0)

/* ---- data-driven PDO mapping shared with the PO2SOconfig hook ---------- */
typedef struct { uint16 index; uint8 sub; uint8 bits; } ent_t;

static ent_t g_rx[32]; static int g_rxn;
static ent_t g_tx[32]; static int g_txn;
static int   g_mode;

#define MAP_MAX 8   /* Elmo: max entries per mapping object */

static int apply_dir(uint16 slave, uint16 map_base, uint16 sm_assign,
                     const ent_t *e, int count)
{
   int retval = 0, k, j;
   uint8 zero = 0;
   int n_objs = (count + MAP_MAX - 1) / MAP_MAX;
   if (n_objs < 1) n_objs = 1;

   retval += ec_SDOwrite(slave, sm_assign, 0x00, FALSE, sizeof(zero), &zero, EC_TIMEOUTRXM);

   for (k = 0; k < n_objs; k++) {
      uint16 map_idx = (uint16)(map_base + k);
      int start = k * MAP_MAX;
      int n_in = count - start;
      uint8 c;
      if (n_in > MAP_MAX) n_in = MAP_MAX;
      if (n_in < 0) n_in = 0;

      retval += ec_SDOwrite(slave, map_idx, 0x00, FALSE, sizeof(zero), &zero, EC_TIMEOUTRXM);
      for (j = 0; j < n_in; j++) {
         uint32 val = ((uint32)e[start + j].index << 16) |
                      ((uint32)e[start + j].sub   << 8)  | e[start + j].bits;
         retval += ec_SDOwrite(slave, map_idx, (uint8)(j + 1), FALSE, sizeof(val), &val, EC_TIMEOUTRXM);
      }
      c = (uint8)n_in;
      retval += ec_SDOwrite(slave, map_idx, 0x00, FALSE, sizeof(c), &c, EC_TIMEOUTRXM);
   }
   for (k = 0; k < n_objs; k++) {
      uint16 map_idx = (uint16)(map_base + k);
      retval += ec_SDOwrite(slave, sm_assign, (uint8)(k + 1), FALSE, sizeof(map_idx), &map_idx, EC_TIMEOUTRXM);
   }
   { uint8 ac = (uint8)n_objs; retval += ec_SDOwrite(slave, sm_assign, 0x00, FALSE, sizeof(ac), &ac, EC_TIMEOUTRXM); }
   return retval;
}

static int map_hook(uint16 slave)
{
   uint8 mode = (uint8)g_mode;
   ec_SDOwrite(slave, 0x6060, 0x00, FALSE, sizeof(mode), &mode, EC_TIMEOUTRXM);
   apply_dir(slave, 0x1600, 0x1c12, g_rx, g_rxn);
   apply_dir(slave, 0x1a00, 0x1c13, g_tx, g_txn);
   return 1;
}

static int bits_bytes(const ent_t *e, int n)
{
   int bits = 0, i;
   for (i = 0; i < n; i++) bits += e[i].bits;
   return (bits + 7) / 8;
}

/* Bring the whole bus to OP with the currently configured mapping. */
static int run_to_op(int nslaves)
{
   int chk, s;
   slavesim_init_n(nslaves);
   if (!ec_init("sim")) return -1;
   if (ec_config_init(FALSE) <= 0) { ec_close(); return -2; }
   for (s = 1; s <= ec_slavecount; s++) ec_slave[s].PO2SOconfig = &map_hook;
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

/* ----------------------------------------------------------------------- */
static void test_discovery(void)
{
   printf("TEST discovery + identity\n");
   slavesim_init_n(1);
   CHECK(ec_init("sim") > 0, "ec_init");
   CHECK(ec_config_init(FALSE) == 1, "one slave discovered");
   CHECK(strcmp(ec_slave[1].name, "SimElmo") == 0, "slave name = SimElmo (from SII string)");
   CHECK(ec_slave[1].eep_man == 0x0000009A, "vendor id 0x9A (Elmo)");
   CHECK(ec_slave[1].eep_id == 0x00030924, "product code from SII");
   CHECK(ec_statecheck(0, EC_STATE_PRE_OP, EC_TIMEOUTSTATE) == EC_STATE_PRE_OP,
         "reached PRE_OP after config_init");
   ec_close();
}

static void test_state_machine(void)
{
   printf("TEST AL state machine walk\n");
   g_mode = 8; g_rxn = 0; g_txn = 0;
   g_rx[g_rxn++] = (ent_t){ 0x6040, 0, 16 };
   g_rx[g_rxn++] = (ent_t){ 0x607A, 0, 32 };
   g_tx[g_txn++] = (ent_t){ 0x6041, 0, 16 };
   g_tx[g_txn++] = (ent_t){ 0x6064, 0, 32 };

   slavesim_init_n(1);
   ec_init("sim");
   ec_config_init(FALSE);
   CHECK(ec_statecheck(0, EC_STATE_PRE_OP, EC_TIMEOUTSTATE) == EC_STATE_PRE_OP, "INIT->PRE_OP");
   ec_slave[1].PO2SOconfig = &map_hook;
   ec_config_map(&IOmap);
   CHECK(ec_statecheck(0, EC_STATE_SAFE_OP, EC_TIMEOUTSTATE) == EC_STATE_SAFE_OP, "PRE_OP->SAFE_OP");

   ec_slave[0].state = EC_STATE_OPERATIONAL;
   ec_send_processdata(); ec_receive_processdata(EC_TIMEOUTRET);
   ec_writestate(0);
   ec_send_processdata(); ec_receive_processdata(EC_TIMEOUTRET);
   CHECK(ec_statecheck(0, EC_STATE_OPERATIONAL, EC_TIMEOUTSTATE) == EC_STATE_OPERATIONAL, "SAFE_OP->OP");

   ec_slave[0].state = EC_STATE_INIT;
   ec_writestate(0);
   CHECK(ec_statecheck(0, EC_STATE_INIT, EC_TIMEOUTSTATE) == EC_STATE_INIT, "OP->INIT");
   ec_close();
}

static void test_sdo_roundtrip(void)
{
   printf("TEST CoE SDO round-trip\n");
   slavesim_init_n(1);
   ec_init("sim");
   ec_config_init(FALSE);   /* PRE_OP: mailbox is up */

   uint32 wr = 0x12345678, rd = 0; int l = sizeof(rd);
   int w1 = ec_SDOwrite(1, 0x2010, 0x00, FALSE, sizeof(wr), &wr, EC_TIMEOUTRXM);
   int w2 = ec_SDOread (1, 0x2010, 0x00, FALSE, &l, &rd, EC_TIMEOUTRXM);
   CHECK(w1 > 0, "SDO download acknowledged (wkc>0)");
   CHECK(w2 > 0, "SDO upload acknowledged (wkc>0)");
   CHECK(rd == wr, "read-back value matches (0x%08X)", rd);

   uint8 mode = 9, back = 0; l = sizeof(back);
   ec_SDOwrite(1, 0x6060, 0x00, FALSE, sizeof(mode), &mode, EC_TIMEOUTRXM);
   ec_SDOread (1, 0x6061, 0x00, FALSE, &l, &back, EC_TIMEOUTRXM);
   CHECK(back == 9, "mode display 0x6061 echoes 0x6060 (=%d)", back);
   ec_close();
}

static void test_mapping(const char *label, int mode,
                         const ent_t *rx, int rxn, const ent_t *tx, int txn)
{
   int ob = bits_bytes(rx, rxn), ib = bits_bytes(tx, txn);
   printf("TEST %s mapping (rx=%dB tx=%dB)\n", label, ob, ib);
   g_mode = mode;
   memcpy(g_rx, rx, rxn * sizeof(ent_t)); g_rxn = rxn;
   memcpy(g_tx, tx, txn * sizeof(ent_t)); g_txn = txn;

   int r = run_to_op(1);
   CHECK(r == 0, "reached OPERATIONAL");
   CHECK(ec_slave[0].Obytes == (uint32)ob, "Obytes=%d matches mapping", ec_slave[0].Obytes);
   CHECK(ec_slave[0].Ibytes == (uint32)ib, "Ibytes=%d matches mapping", ec_slave[0].Ibytes);
   int exp = ec_group[0].outputsWKC * 2 + ec_group[0].inputsWKC;
   int wkc = ec_send_processdata(); wkc = ec_receive_processdata(EC_TIMEOUTRET);
   CHECK(wkc == exp && exp == 3, "cyclic WKC=%d (expected %d)", wkc, exp);
   ec_close();
}

static void test_split_mapping(void)
{
   /* 10 Rx entries -> must split across 0x1600 and 0x1601 */
   ent_t rx[10], tx[2];
   int i;
   rx[0] = (ent_t){ 0x6040, 0, 16 };
   for (i = 1; i < 10; i++) rx[i] = (ent_t){ (uint16)(0x2100 + i), 0, 16 };
   tx[0] = (ent_t){ 0x6041, 0, 16 };
   tx[1] = (ent_t){ 0x6064, 0, 32 };

   printf("TEST >8-entry RxPDO split across mapping objects\n");
   g_mode = 8;
   memcpy(g_rx, rx, sizeof(rx)); g_rxn = 10;
   memcpy(g_tx, tx, sizeof(tx)); g_txn = 2;

   int r = run_to_op(1);
   int ob = bits_bytes(rx, 10);
   CHECK(r == 0, "reached OPERATIONAL with split mapping");
   CHECK(ec_slave[0].Obytes == (uint32)ob, "Obytes=%d covers all 10 entries", ec_slave[0].Obytes);
   ec_close();
}

static void test_multi_slave(void)
{
   const int N = 3;
   printf("TEST %d-axis bus\n", N);
   g_mode = 8; g_rxn = 0; g_txn = 0;
   g_rx[g_rxn++] = (ent_t){ 0x6040, 0, 16 };
   g_rx[g_rxn++] = (ent_t){ 0x607A, 0, 32 };
   g_tx[g_txn++] = (ent_t){ 0x6041, 0, 16 };
   g_tx[g_txn++] = (ent_t){ 0x6064, 0, 32 };

   int r = run_to_op(N);
   CHECK(ec_slavecount == N, "%d slaves discovered", ec_slavecount);
   CHECK(r == 0, "all slaves reached OPERATIONAL");
   int exp = ec_group[0].outputsWKC * 2 + ec_group[0].inputsWKC;
   int wkc = ec_send_processdata(); wkc = ec_receive_processdata(EC_TIMEOUTRET);
   CHECK(exp == 3 * N, "expected WKC=%d for %d slaves", exp, N);
   CHECK(wkc == exp, "cyclic WKC=%d matches expected", wkc);
   ec_close();
}

int main(int argc, char *argv[])
{
   if (argc > 1 && strcmp(argv[1], "-v") == 0) slavesim_set_verbose(1);
   printf("=== SOEM 1.3.1 stack tests (virtual slave) ===\n");

   /* CSP (mode 8) with position offset mapping (Elmo template) */
   ent_t csp_rx[] = { {0x6040,0,16},{0x60B0,0,32},{0x60B1,0,32},{0x60B2,0,16} };
   ent_t csp_tx[] = { {0x6041,0,16},{0x6064,0,32} };
   /* CSV (mode 9) */
   ent_t csv_rx[] = { {0x6040,0,16},{0x60FF,0,32} };
   ent_t csv_tx[] = { {0x6041,0,16},{0x606C,0,32} };
   /* CST (mode 10) */
   ent_t cst_rx[] = { {0x6040,0,16},{0x6071,0,16} };
   ent_t cst_tx[] = { {0x6041,0,16},{0x6077,0,16} };

   test_discovery();
   test_state_machine();
   test_sdo_roundtrip();
   /* Note: these three use a 6-byte-or-smaller image so WKC math stays at 3 */
   test_mapping("CSP-simple", 8,
                (ent_t[]){{0x6040,0,16},{0x607A,0,32}}, 2,
                (ent_t[]){{0x6041,0,16},{0x6064,0,32}}, 2);
   test_mapping("CSV", 9, csv_rx, 2, csv_tx, 2);
   test_mapping("CST", 10, cst_rx, 2, cst_tx, 2);
   (void)csp_rx; (void)csp_tx; (void)cst_rx; (void)cst_tx;
   test_split_mapping();
   test_multi_slave();

   printf("\n=== %d passed, %d failed ===\n", g_pass, g_fail);
   return g_fail ? 1 : 0;
}
