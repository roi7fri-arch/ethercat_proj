/** \file
 * \brief Unit tests for the vendor-neutral CiA 402 layer (src/drive/cia402.c).
 *
 * Pure host test: no SOEM, no virtual bus, no hardware. Covers the statusword
 * -> state decoding table, the 0x06 -> 0x07 -> 0x0F enable ladder including the
 * fault-reset edge, safe stop, and that a full bring-up converges.
 */
#include <stdio.h>
#include <string.h>

#include "cia402.h"

static int g_pass = 0, g_fail = 0;

#define CHECK(cond, ...) do { \
      if (cond) { g_pass++; printf("  [PASS] " __VA_ARGS__); printf("\n"); } \
      else      { g_fail++; printf("  [FAIL] " __VA_ARGS__); printf("\n"); } \
   } while (0)

/* Representative statuswords for each state. Bit 4 (voltage enabled) and bit 9
 * (remote) are set where a real drive would set them, to prove the decoder
 * ignores bits that are not part of the state encoding. */
static void test_state_decoding(void)
{
   printf("TEST statusword -> state decoding\n");

   CHECK(cia402_state(0x0000) == CIA402_STATE_NOT_READY_TO_SWITCH_ON,
         "0x0000 -> Not Ready To Switch On");
   CHECK(cia402_state(0x0240) == CIA402_STATE_SWITCH_ON_DISABLED,
         "0x0240 -> Switch On Disabled");
   CHECK(cia402_state(0x0231) == CIA402_STATE_READY_TO_SWITCH_ON,
         "0x0231 -> Ready To Switch On");
   CHECK(cia402_state(0x0233) == CIA402_STATE_SWITCHED_ON,
         "0x0233 -> Switched On");
   CHECK(cia402_state(0x0237) == CIA402_STATE_OPERATION_ENABLED,
         "0x0237 -> Operation Enabled");
   CHECK(cia402_state(0x0217) == CIA402_STATE_QUICK_STOP_ACTIVE,
         "0x0217 -> Quick Stop Active");
   CHECK(cia402_state(0x021F) == CIA402_STATE_FAULT_REACTION_ACTIVE,
         "0x021F -> Fault Reaction Active");
   CHECK(cia402_state(0x0218) == CIA402_STATE_FAULT,
         "0x0218 -> Fault");

   /* Bits outside the state encoding must not change the decision. */
   CHECK(cia402_state(0x0637) == CIA402_STATE_OPERATION_ENABLED,
         "target-reached + internal-limit do not disturb the decode");
   CHECK(cia402_state(0x0298) == CIA402_STATE_FAULT,
         "warning bit does not mask a fault");

   CHECK(cia402_is_operational(0x0237),  "0x0237 reports operational");
   CHECK(!cia402_is_operational(0x0233), "0x0233 does not report operational");
   CHECK(cia402_has_fault(0x0218),       "0x0218 reports fault");
   CHECK(cia402_has_fault(0x021F),       "fault reaction counts as fault");
   CHECK(!cia402_has_fault(0x0237),      "operation enabled is not a fault");
}

static void test_enable_ladder(void)
{
   uint16_t cw;
   printf("TEST enable ladder 0x06 -> 0x07 -> 0x0F\n");

   cw = cia402_next_controlword(0x0240, 0x0000);
   CHECK(cw == CIA402_CW_SHUTDOWN, "Switch On Disabled -> 0x%04X shutdown (got 0x%04X)",
         CIA402_CW_SHUTDOWN, cw);

   cw = cia402_next_controlword(0x0231, cw);
   CHECK(cw == CIA402_CW_SWITCH_ON, "Ready To Switch On -> 0x%04X switch on (got 0x%04X)",
         CIA402_CW_SWITCH_ON, cw);

   cw = cia402_next_controlword(0x0233, cw);
   CHECK(cw == CIA402_CW_ENABLE_OPERATION, "Switched On -> 0x%04X enable op (got 0x%04X)",
         CIA402_CW_ENABLE_OPERATION, cw);

   cw = cia402_next_controlword(0x0237, cw);
   CHECK(cw == CIA402_CW_ENABLE_OPERATION, "Operation Enabled -> holds 0x%04X (got 0x%04X)",
         CIA402_CW_ENABLE_OPERATION, cw);
}

/* Fault reset is edge triggered: holding bit 7 high resets the drive once and
 * then does nothing. The old bring-up loop had exactly that bug. */
static void test_fault_reset_is_an_edge(void)
{
   uint16_t cw;
   printf("TEST fault reset produces a rising edge\n");

   cw = cia402_next_controlword(0x0218, 0x0000);
   CHECK(cw == CIA402_CW_FAULT_RESET, "fault -> raises bit 7 (got 0x%04X)", cw);

   cw = cia402_next_controlword(0x0218, cw);
   CHECK((cw & CIA402_CW_FAULT_RESET_BIT) == 0,
         "still in fault -> drops bit 7 to re-arm the edge (got 0x%04X)", cw);

   cw = cia402_next_controlword(0x0218, cw);
   CHECK(cw == CIA402_CW_FAULT_RESET, "next cycle raises bit 7 again (got 0x%04X)", cw);
}

/* Mode-specific bits 4..6 belong to the mode (e.g. "new set-point" in PP), and
 * bits 11..15 to the manufacturer. The ladder must not clobber them. */
static void test_reserved_bits_are_preserved(void)
{
   uint16_t cw;
   printf("TEST mode-specific / manufacturer bits survive\n");

   cw = cia402_next_controlword(0x0237, 0x8030);
   CHECK((cw & 0x8030) == 0x8030, "bits 4,5 and 15 preserved (got 0x%04X)", cw);
   CHECK((cw & CIA402_CW_MASK) == CIA402_CW_ENABLE_OPERATION,
         "command bits still 0x000F (got 0x%04X)", cw & CIA402_CW_MASK);
}

static void test_safe_stop(void)
{
   printf("TEST safe stop\n");

   CHECK(cia402_safe_stop_controlword(0x0237) == CIA402_CW_QUICK_STOP,
         "live drive -> quick stop");
   CHECK(cia402_safe_stop_controlword(0x0233) == CIA402_CW_QUICK_STOP,
         "switched on -> quick stop");
   CHECK(cia402_safe_stop_controlword(0x0240) == CIA402_CW_DISABLE_VOLTAGE,
         "already disabled -> disable voltage");
   CHECK(cia402_safe_stop_controlword(0x0218) == CIA402_CW_DISABLE_VOLTAGE,
         "faulted -> disable voltage");
}

/* Walk a drive that answers the way the standard says it should, and make sure
 * the ladder converges from every starting state within a few cycles. */
static void test_bringup_converges(void)
{
   static const uint16_t start[] = { 0x0000, 0x0240, 0x0231, 0x0233, 0x0218, 0x0217 };
   unsigned s;

   printf("TEST bring-up converges from any state\n");

   for (s = 0; s < sizeof(start) / sizeof(start[0]); s++)
   {
      uint16_t sw = start[s];
      uint16_t cw = 0;
      int cycle;

      for (cycle = 0; cycle < 16; cycle++)
      {
         cw = cia402_next_controlword(sw, cw);

         /* Minimal standard-compliant drive model. */
         switch (cia402_state(sw))
         {
            case CIA402_STATE_NOT_READY_TO_SWITCH_ON:
               sw = 0x0240; break;                       /* finishes booting  */
            case CIA402_STATE_FAULT:
            case CIA402_STATE_FAULT_REACTION_ACTIVE:
               if (cw & CIA402_CW_FAULT_RESET_BIT) sw = 0x0240;
               break;
            case CIA402_STATE_QUICK_STOP_ACTIVE:
               if ((cw & CIA402_CW_MASK) == CIA402_CW_DISABLE_VOLTAGE) sw = 0x0240;
               break;
            case CIA402_STATE_SWITCH_ON_DISABLED:
               if ((cw & CIA402_CW_MASK) == CIA402_CW_SHUTDOWN) sw = 0x0231;
               break;
            case CIA402_STATE_READY_TO_SWITCH_ON:
               if ((cw & CIA402_CW_MASK) == CIA402_CW_SWITCH_ON) sw = 0x0233;
               break;
            case CIA402_STATE_SWITCHED_ON:
               if ((cw & CIA402_CW_MASK) == CIA402_CW_ENABLE_OPERATION) sw = 0x0237;
               break;
            default:
               break;
         }

         if (cia402_is_operational(sw))
            break;
      }

      CHECK(cia402_is_operational(sw),
            "0x%04X (%s) reached Operation Enabled in %d cycles",
            start[s], cia402_state_name(cia402_state(start[s])), cycle + 1);
   }
}

static void test_names(void)
{
   printf("TEST names\n");

   CHECK(strcmp(cia402_state_name(CIA402_STATE_OPERATION_ENABLED),
                "Operation Enabled") == 0, "state name for Operation Enabled");
   CHECK(strcmp(cia402_mode_name(CIA402_MODE_CSP),
                "Cyclic Synchronous Position") == 0, "mode name for CSP");
   CHECK(strcmp(cia402_mode_name(-3), "vendor-specific") == 0,
         "negative mode reported as vendor-specific");

   CHECK(cia402_mode_is_cyclic(CIA402_MODE_CSP), "CSP is cyclic");
   CHECK(cia402_mode_is_cyclic(CIA402_MODE_CST), "CST is cyclic");
   CHECK(!cia402_mode_is_cyclic(CIA402_MODE_PP), "PP is not cyclic");
}

int main(void)
{
   printf("=== CiA 402 profile unit tests ===\n");

   test_state_decoding();
   test_enable_ladder();
   test_fault_reset_is_an_edge();
   test_reserved_bits_are_preserved();
   test_safe_stop();
   test_bringup_converges();
   test_names();

   printf("\n=== %d passed, %d failed ===\n", g_pass, g_fail);
   return g_fail ? 1 : 0;
}
