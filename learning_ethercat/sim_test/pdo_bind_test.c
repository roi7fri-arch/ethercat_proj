/** \file
 * \brief Unit tests for the semantic PDO binding layer (src/pdo/pdo_bind.c).
 *
 * Pure host test: no SOEM, no virtual bus. Builds configurations by hand and
 * checks that roles resolve to the right bit offsets, that reads and writes hit
 * exactly the right bits, that a dual-axis slave binds each axis separately,
 * and that a truncated or incomplete map is rejected before the RT loop.
 */
#include <stdio.h>
#include <string.h>

#include "pdo_bind.h"
#include "cia402.h"

static int g_pass = 0, g_fail = 0;

#define CHECK(cond, ...) do { \
      if (cond) { g_pass++; printf("  [PASS] " __VA_ARGS__); printf("\n"); } \
      else      { g_fail++; printf("  [FAIL] " __VA_ARGS__); printf("\n"); } \
   } while (0)

static void add_rx(ecat_slave_config_t *sc, uint16_t index, uint8_t bits)
{
   sc->rxpdo[sc->rxpdo_count].index    = index;
   sc->rxpdo[sc->rxpdo_count].subindex = 0;
   sc->rxpdo[sc->rxpdo_count].bitlen   = bits;
   sc->rxpdo_count++;
}

static void add_tx(ecat_slave_config_t *sc, uint16_t index, uint8_t bits)
{
   sc->txpdo[sc->txpdo_count].index    = index;
   sc->txpdo[sc->txpdo_count].subindex = 0;
   sc->txpdo[sc->txpdo_count].bitlen   = bits;
   sc->txpdo_count++;
}

/* The CSP map the Elmo config actually uses:
 *   Rx: controlword(16) pos_off(32) vel_off(32) trq_off(16)   = 12 bytes
 *   Tx: statusword(16) mode(8) err(16) demand(32) actual(32)  = 13 bytes  */
static void build_csp(ecat_slave_config_t *sc)
{
   memset(sc, 0, sizeof(*sc));
   sc->position = 1;
   sc->mode_of_operation = CIA402_MODE_CSP;

   add_rx(sc, CIA402_OD_CONTROLWORD,     16);
   add_rx(sc, CIA402_OD_POSITION_OFFSET, 32);
   add_rx(sc, CIA402_OD_VELOCITY_OFFSET, 32);
   add_rx(sc, CIA402_OD_TORQUE_OFFSET,   16);

   add_tx(sc, CIA402_OD_STATUSWORD,          16);
   add_tx(sc, CIA402_OD_MODES_OF_OP_DISPLAY,  8);
   add_tx(sc, CIA402_OD_ERROR_CODE,          16);
   add_tx(sc, CIA402_OD_POSITION_DEMAND,     32);
   add_tx(sc, CIA402_OD_POSITION_ACTUAL,     32);
}

static void test_offsets(void)
{
   ecat_slave_config_t sc;
   pdo_io_t io;
   uint8_t out[12], in[13];

   printf("TEST role -> bit offset resolution\n");
   build_csp(&sc);
   memset(out, 0, sizeof(out));
   memset(in, 0, sizeof(in));

   CHECK(pdo_bind(&io, &sc, 1, 0, out, sizeof(out), in, sizeof(in)) == 0,
         "bind succeeds");

   CHECK(io.sig[PDO_SIG_CONTROLWORD].bit_off == 0 &&
         io.sig[PDO_SIG_CONTROLWORD].bit_len == 16, "controlword at bit 0, 16 wide");
   CHECK(io.sig[PDO_SIG_POSITION_OFFSET].bit_off == 16, "position offset at bit 16");
   CHECK(io.sig[PDO_SIG_VELOCITY_OFFSET].bit_off == 48, "velocity offset at bit 48");
   CHECK(io.sig[PDO_SIG_TORQUE_OFFSET].bit_off == 80,   "torque offset at bit 80");

   CHECK(io.sig[PDO_SIG_STATUSWORD].bit_off == 0,       "statusword at bit 0");
   CHECK(io.sig[PDO_SIG_MODE_DISPLAY].bit_off == 16,    "mode display at bit 16");
   CHECK(io.sig[PDO_SIG_ERROR_CODE].bit_off == 24,      "error code at bit 24");
   CHECK(io.sig[PDO_SIG_POSITION_DEMAND].bit_off == 40, "position demand at bit 40");
   CHECK(io.sig[PDO_SIG_POSITION_ACTUAL].bit_off == 72, "position actual at bit 72");

   /* Not in this map - must be reported absent, not guessed. */
   CHECK(!pdo_has(&io, PDO_SIG_TARGET_POSITION), "target position absent");
   CHECK(!pdo_has(&io, PDO_SIG_TORQUE_ACTUAL),   "torque actual absent");
   CHECK(pdo_has(&io, PDO_SIG_CONTROLWORD),      "controlword present");
}

static void test_read_write(void)
{
   ecat_slave_config_t sc;
   pdo_io_t io;
   uint8_t out[12], in[13];

   printf("TEST accessors hit the right bytes\n");
   build_csp(&sc);
   memset(out, 0, sizeof(out));
   memset(in, 0, sizeof(in));
   pdo_bind(&io, &sc, 1, 0, out, sizeof(out), in, sizeof(in));

   pdo_set_u16(&io, PDO_SIG_CONTROLWORD, 0x000F);
   CHECK(out[0] == 0x0F && out[1] == 0x00, "controlword 0x000F -> bytes 0,1");

   pdo_set_i32(&io, PDO_SIG_POSITION_OFFSET, 0x12345678);
   CHECK(out[2] == 0x78 && out[3] == 0x56 && out[4] == 0x34 && out[5] == 0x12,
         "position offset written little-endian at byte 2");

   /* Writing one signal must not disturb its neighbours. */
   CHECK(out[0] == 0x0F && out[6] == 0x00, "neighbouring signals untouched");

   pdo_set_i32(&io, PDO_SIG_VELOCITY_OFFSET, -1);
   CHECK(out[6] == 0xFF && out[7] == 0xFF && out[8] == 0xFF && out[9] == 0xFF,
         "velocity offset -1 -> all ones");
   pdo_set_i32(&io, PDO_SIG_VELOCITY_OFFSET, 0);
   CHECK(out[6] == 0 && out[7] == 0 && out[8] == 0 && out[9] == 0,
         "writing 0 clears the bits again");

   CHECK(pdo_get_u16(&io, PDO_SIG_CONTROLWORD) == 0x000F, "controlword reads back");
   CHECK(pdo_get_i32(&io, PDO_SIG_POSITION_OFFSET) == 0x12345678,
         "position offset reads back");

   /* Feedback image: pretend the slave filled it in. */
   in[0] = 0x37; in[1] = 0x02;                       /* statusword 0x0237 */
   in[9] = 0x00; in[10] = 0x00; in[11] = 0xFF; in[12] = 0xFF; /* actual = -65536 */
   CHECK(pdo_get_u16(&io, PDO_SIG_STATUSWORD) == 0x0237, "statusword decoded");
   CHECK(cia402_is_operational(pdo_get_u16(&io, PDO_SIG_STATUSWORD)),
         "statusword feeds straight into the CiA 402 layer");
   CHECK(pdo_get_i32(&io, PDO_SIG_POSITION_ACTUAL) == -65536,
         "negative position actual sign-extends (got %d)",
         pdo_get_i32(&io, PDO_SIG_POSITION_ACTUAL));
}

static void test_readonly_feedback(void)
{
   ecat_slave_config_t sc;
   pdo_io_t io;
   uint8_t out[12], in[13];

   printf("TEST feedback signals are not writable\n");
   build_csp(&sc);
   memset(out, 0, sizeof(out));
   memset(in, 0, sizeof(in));
   pdo_bind(&io, &sc, 1, 0, out, sizeof(out), in, sizeof(in));

   pdo_set_i32(&io, PDO_SIG_POSITION_ACTUAL, 0x7FFFFFFF);
   CHECK(in[9] == 0 && in[10] == 0 && in[11] == 0 && in[12] == 0,
         "writing a TxPDO role leaves the input image alone");

   /* An unmapped role must be inert, not a wild write. */
   pdo_set_i32(&io, PDO_SIG_TARGET_POSITION, 0x7FFFFFFF);
   CHECK(out[10] == 0 && out[11] == 0, "writing an unmapped role is a no-op");
   CHECK(pdo_get_i32(&io, PDO_SIG_TARGET_POSITION) == 0,
         "reading an unmapped role yields 0");
}

/* An Elmo Platinum is two axes behind one EtherCAT node: the same object index
 * appears twice in the map, once per axis. */
static void test_dual_axis(void)
{
   ecat_slave_config_t sc;
   pdo_io_t ax0, ax1;
   uint8_t out[20], in[8];

   printf("TEST dual-axis slave binds each axis separately\n");

   memset(&sc, 0, sizeof(sc));
   sc.position = 1;
   add_rx(&sc, CIA402_OD_CONTROLWORD,     16);   /* axis 0, bit 0   */
   add_rx(&sc, CIA402_OD_POSITION_OFFSET, 32);   /* axis 0, bit 16  */
   add_rx(&sc, CIA402_OD_CONTROLWORD,     16);   /* axis 1, bit 48  */
   add_rx(&sc, CIA402_OD_POSITION_OFFSET, 32);   /* axis 1, bit 64  */
   add_tx(&sc, CIA402_OD_STATUSWORD,      16);   /* axis 0, bit 0   */
   add_tx(&sc, CIA402_OD_STATUSWORD,      16);   /* axis 1, bit 16  */

   memset(out, 0, sizeof(out));
   memset(in, 0, sizeof(in));

   pdo_bind(&ax0, &sc, 1, 0, out, sizeof(out), in, sizeof(in));
   pdo_bind(&ax1, &sc, 1, 1, out, sizeof(out), in, sizeof(in));

   CHECK(ax0.sig[PDO_SIG_CONTROLWORD].bit_off == 0,  "axis 0 controlword at bit 0");
   CHECK(ax1.sig[PDO_SIG_CONTROLWORD].bit_off == 48, "axis 1 controlword at bit 48");
   CHECK(ax0.sig[PDO_SIG_POSITION_OFFSET].bit_off == 16, "axis 0 offset at bit 16");
   CHECK(ax1.sig[PDO_SIG_POSITION_OFFSET].bit_off == 64, "axis 1 offset at bit 64");
   CHECK(ax1.sig[PDO_SIG_STATUSWORD].bit_off == 16,  "axis 1 statusword at bit 16");

   pdo_set_i32(&ax0, PDO_SIG_POSITION_OFFSET, 111);
   pdo_set_i32(&ax1, PDO_SIG_POSITION_OFFSET, 222);
   CHECK(pdo_get_i32(&ax0, PDO_SIG_POSITION_OFFSET) == 111 &&
         pdo_get_i32(&ax1, PDO_SIG_POSITION_OFFSET) == 222,
         "the two axes do not alias each other");

   in[0] = 0x37; in[1] = 0x02;
   in[2] = 0x18; in[3] = 0x02;
   CHECK(cia402_is_operational(pdo_get_u16(&ax0, PDO_SIG_STATUSWORD)),
         "axis 0 reads Operation Enabled");
   CHECK(cia402_has_fault(pdo_get_u16(&ax1, PDO_SIG_STATUSWORD)),
         "axis 1 reads Fault");

   /* A third axis does not exist: binding must report absence, not alias. */
   {
      pdo_io_t ax2;
      pdo_bind(&ax2, &sc, 1, 2, out, sizeof(out), in, sizeof(in));
      CHECK(!pdo_has(&ax2, PDO_SIG_CONTROLWORD),
            "non-existent axis 2 binds nothing");
   }
}

static void test_validation(void)
{
   ecat_slave_config_t sc;
   pdo_io_t io;
   uint8_t out[12], in[13];
   const pdo_signal_t *req;
   int n;

   printf("TEST start-up validation catches bad maps\n");
   build_csp(&sc);
   memset(out, 0, sizeof(out));
   memset(in, 0, sizeof(in));

   n = pdo_required_for_mode(CIA402_MODE_CSP, &req);
   CHECK(n == 3, "CSP requires 3 signals (got %d)", n);

   pdo_bind(&io, &sc, 1, 0, out, sizeof(out), in, sizeof(in));
   printf("   (expect no complaints below)\n");
   CHECK(pdo_require(&io, req, n) == 0, "complete CSP map passes validation");

   /* CST needs torque feedback, which this map does not carry. */
   n = pdo_required_for_mode(CIA402_MODE_CST, &req);
   printf("   (expect one complaint below)\n");
   CHECK(pdo_require(&io, req, n) == 1, "CST on a CSP map is rejected");

   /* Process image smaller than the map claims - the exact failure mode the
    * old struct-cast approach turned into silent memory corruption. */
   n = pdo_required_for_mode(CIA402_MODE_CSP, &req);
   pdo_bind(&io, &sc, 1, 0, out, sizeof(out), in, 4);
   printf("   (expect one complaint below)\n");
   CHECK(pdo_require(&io, req, n) == 1, "truncated input image is rejected");

   /* And the accessors stay inside the buffer even so. */
   CHECK(pdo_get_i32(&io, PDO_SIG_POSITION_ACTUAL) == 0,
         "out-of-range read is clamped to 0 rather than reading past the image");
}

static void test_metadata(void)
{
   printf("TEST role metadata\n");

   CHECK(pdo_signal_dir(PDO_SIG_CONTROLWORD) == PDO_DIR_RX, "controlword is Rx");
   CHECK(pdo_signal_dir(PDO_SIG_STATUSWORD)  == PDO_DIR_TX, "statusword is Tx");
   CHECK(pdo_signal_default_index(PDO_SIG_CONTROLWORD) == CIA402_OD_CONTROLWORD,
         "controlword defaults to 0x6040");
   CHECK(strcmp(pdo_signal_name(PDO_SIG_POSITION_ACTUAL), "position_actual") == 0,
         "role name");
}

int main(void)
{
   printf("=== PDO binding unit tests ===\n");

   test_offsets();
   test_read_write();
   test_readonly_feedback();
   test_dual_axis();
   test_validation();
   test_metadata();

   printf("\n=== %d passed, %d failed ===\n", g_pass, g_fail);
   return g_fail ? 1 : 0;
}
