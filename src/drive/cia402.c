/** \file
 * \brief CiA 402 drive profile: state decoding and the enable ladder.
 *
 * Vendor-neutral by construction - see cia402.h. No SOEM, no EtherCAT, no I/O,
 * no allocation: every function here is pure and safe to call from the RT loop.
 */

#include "cia402.h"

/* The state is encoded in statusword bits 0-3, 5 and 6. Two masks are needed
 * because Switch On Disabled and the two fault states ignore bit 5 (quick
 * stop), while the four live states require it. */
#define SW_MASK_A  0x004Fu   /* bits 0,1,2,3,6 */
#define SW_MASK_B  0x006Fu   /* bits 0,1,2,3,5,6 */

cia402_state_t cia402_state(uint16_t statusword)
{
   if ((statusword & SW_MASK_A) == 0x0000u)
      return CIA402_STATE_NOT_READY_TO_SWITCH_ON;
   if ((statusword & SW_MASK_A) == 0x0040u)
      return CIA402_STATE_SWITCH_ON_DISABLED;
   if ((statusword & SW_MASK_B) == 0x0021u)
      return CIA402_STATE_READY_TO_SWITCH_ON;
   if ((statusword & SW_MASK_B) == 0x0023u)
      return CIA402_STATE_SWITCHED_ON;
   if ((statusword & SW_MASK_B) == 0x0027u)
      return CIA402_STATE_OPERATION_ENABLED;
   if ((statusword & SW_MASK_B) == 0x0007u)
      return CIA402_STATE_QUICK_STOP_ACTIVE;
   if ((statusword & SW_MASK_A) == 0x000Fu)
      return CIA402_STATE_FAULT_REACTION_ACTIVE;
   if ((statusword & SW_MASK_A) == 0x0008u)
      return CIA402_STATE_FAULT;

   return CIA402_STATE_UNKNOWN;
}

uint16_t cia402_next_controlword(uint16_t statusword, uint16_t prev_cw)
{
   /* Keep whatever the caller put in the mode-specific and manufacturer bits;
    * we only own the command bits. */
   uint16_t keep = (uint16_t)(prev_cw & ~CIA402_CW_MASK);
   uint16_t cmd;

   switch (cia402_state(statusword))
   {
      case CIA402_STATE_FAULT:
      case CIA402_STATE_FAULT_REACTION_ACTIVE:
         /* Fault reset is triggered by a 0->1 edge on bit 7, so we must drop
          * the bit for one cycle before raising it again. Holding it high (as
          * the old bring-up loop did) resets the drive exactly once and then
          * silently stops working. */
         cmd = (prev_cw & CIA402_CW_FAULT_RESET_BIT)
                  ? CIA402_CW_DISABLE_VOLTAGE
                  : CIA402_CW_FAULT_RESET;
         break;

      case CIA402_STATE_SWITCH_ON_DISABLED:
         cmd = CIA402_CW_SHUTDOWN;            /* 0x06 */
         break;

      case CIA402_STATE_READY_TO_SWITCH_ON:
         cmd = CIA402_CW_SWITCH_ON;           /* 0x07 */
         break;

      case CIA402_STATE_SWITCHED_ON:
      case CIA402_STATE_OPERATION_ENABLED:
         cmd = CIA402_CW_ENABLE_OPERATION;    /* 0x0F */
         break;

      case CIA402_STATE_QUICK_STOP_ACTIVE:
         /* Leave quick stop by dropping to Switch On Disabled, then the ladder
          * above picks it up again on the following cycle. */
         cmd = CIA402_CW_DISABLE_VOLTAGE;
         break;

      case CIA402_STATE_NOT_READY_TO_SWITCH_ON:
      case CIA402_STATE_UNKNOWN:
      default:
         /* Drive is still booting or reporting something we cannot interpret:
          * command nothing and look again next cycle. */
         cmd = CIA402_CW_DISABLE_VOLTAGE;
         break;
   }

   return (uint16_t)(keep | cmd);
}

uint16_t cia402_safe_stop_controlword(uint16_t statusword)
{
   switch (cia402_state(statusword))
   {
      case CIA402_STATE_OPERATION_ENABLED:
      case CIA402_STATE_SWITCHED_ON:
      case CIA402_STATE_READY_TO_SWITCH_ON:
         /* Quick stop bit is active LOW: clearing bit 2 requests the stop. */
         return CIA402_CW_QUICK_STOP;
      default:
         return CIA402_CW_DISABLE_VOLTAGE;
   }
}

int cia402_is_operational(uint16_t statusword)
{
   return cia402_state(statusword) == CIA402_STATE_OPERATION_ENABLED;
}

int cia402_has_fault(uint16_t statusword)
{
   cia402_state_t s = cia402_state(statusword);
   return (s == CIA402_STATE_FAULT) ||
          (s == CIA402_STATE_FAULT_REACTION_ACTIVE);
}

const char *cia402_state_name(cia402_state_t state)
{
   switch (state)
   {
      case CIA402_STATE_NOT_READY_TO_SWITCH_ON: return "Not Ready To Switch On";
      case CIA402_STATE_SWITCH_ON_DISABLED:     return "Switch On Disabled";
      case CIA402_STATE_READY_TO_SWITCH_ON:     return "Ready To Switch On";
      case CIA402_STATE_SWITCHED_ON:            return "Switched On";
      case CIA402_STATE_OPERATION_ENABLED:      return "Operation Enabled";
      case CIA402_STATE_QUICK_STOP_ACTIVE:      return "Quick Stop Active";
      case CIA402_STATE_FAULT_REACTION_ACTIVE:  return "Fault Reaction Active";
      case CIA402_STATE_FAULT:                  return "Fault";
      case CIA402_STATE_UNKNOWN:                return "Unknown";
   }
   return "Unknown";
}

const char *cia402_mode_name(int mode)
{
   switch (mode)
   {
      case CIA402_MODE_NONE: return "none";
      case CIA402_MODE_PP:   return "Profile Position";
      case CIA402_MODE_VL:   return "Velocity";
      case CIA402_MODE_PV:   return "Profile Velocity";
      case CIA402_MODE_TQ:   return "Profile Torque";
      case CIA402_MODE_HM:   return "Homing";
      case CIA402_MODE_IP:   return "Interpolated Position";
      case CIA402_MODE_CSP:  return "Cyclic Synchronous Position";
      case CIA402_MODE_CSV:  return "Cyclic Synchronous Velocity";
      case CIA402_MODE_CST:  return "Cyclic Synchronous Torque";
      default:               return "vendor-specific";
   }
}

int cia402_mode_is_cyclic(int mode)
{
   return (mode == CIA402_MODE_CSP) ||
          (mode == CIA402_MODE_CSV) ||
          (mode == CIA402_MODE_CST);
}
