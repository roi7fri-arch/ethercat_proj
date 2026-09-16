#ifndef CIA402_H
#define CIA402_H

/*
 * CiA 402 (CANopen drives and motion control) profile - the parts that every
 * compliant servo drive shares, regardless of who made it.
 *
 * This file exists because the same logic used to live in elmo_device.c under
 * Elmo-flavoured names (ELMO_SW_ENUM, ParseStatusWord, Control_Word). None of
 * it is Elmo-specific: the statusword encoding, the state machine and the
 * 0x06 -> 0x07 -> 0x0F enable ladder are defined by the standard, so Copley,
 * Maxon, Beckhoff, Kollmorgen and ACS drives all behave identically here.
 *
 * Deliberately free of SOEM and of any EtherCAT dependency (plain <stdint.h>)
 * so it can be unit-tested on the host and reused over CANopen if needed.
 * Vendor-specific behaviour belongs in src/vendors/<vendor>/, not here.
 */

#include <stdint.h>

/* ------------------------------------------------------------------------ */
/* Standard object dictionary indices (CiA 402 profile area 0x6000-0x67FF)   */
/* ------------------------------------------------------------------------ */
#define CIA402_OD_ERROR_CODE          0x603Fu
#define CIA402_OD_CONTROLWORD         0x6040u
#define CIA402_OD_STATUSWORD          0x6041u
#define CIA402_OD_QUICK_STOP_OPTION   0x605Au
#define CIA402_OD_MODES_OF_OPERATION  0x6060u
#define CIA402_OD_MODES_OF_OP_DISPLAY 0x6061u
#define CIA402_OD_POSITION_DEMAND     0x6062u
#define CIA402_OD_POSITION_ACTUAL     0x6064u
#define CIA402_OD_FOLLOWING_ERR_WIN   0x6065u
#define CIA402_OD_VELOCITY_DEMAND     0x606Bu
#define CIA402_OD_VELOCITY_ACTUAL     0x606Cu
#define CIA402_OD_TARGET_TORQUE       0x6071u
#define CIA402_OD_MAX_TORQUE          0x6072u
#define CIA402_OD_MOTOR_RATED_CURRENT 0x6075u
#define CIA402_OD_TORQUE_DEMAND       0x6074u
#define CIA402_OD_TORQUE_ACTUAL       0x6077u
#define CIA402_OD_TARGET_POSITION     0x607Au
#define CIA402_OD_SW_POS_LIMIT        0x607Du
#define CIA402_OD_PROFILE_VELOCITY    0x6081u
#define CIA402_OD_POSITION_OFFSET     0x60B0u
#define CIA402_OD_VELOCITY_OFFSET     0x60B1u
#define CIA402_OD_TORQUE_OFFSET       0x60B2u
#define CIA402_OD_TARGET_VELOCITY     0x60FFu
#define CIA402_OD_SUPPORTED_MODES     0x6502u

/* ------------------------------------------------------------------------ */
/* Modes of operation (written to 0x6060, echoed by 0x6061)                  */
/* ------------------------------------------------------------------------ */
typedef enum {
   CIA402_MODE_NONE = 0,
   CIA402_MODE_PP   = 1,    /* Profile Position                             */
   CIA402_MODE_VL   = 2,    /* Velocity (frequency converter)               */
   CIA402_MODE_PV   = 3,    /* Profile Velocity                             */
   CIA402_MODE_TQ   = 4,    /* Profile Torque                               */
   CIA402_MODE_HM   = 6,    /* Homing                                       */
   CIA402_MODE_IP   = 7,    /* Interpolated Position                        */
   CIA402_MODE_CSP  = 8,    /* Cyclic Synchronous Position                  */
   CIA402_MODE_CSV  = 9,    /* Cyclic Synchronous Velocity                  */
   CIA402_MODE_CST  = 10    /* Cyclic Synchronous Torque                    */
} cia402_mode_t;

/* ------------------------------------------------------------------------ */
/* Controlword (0x6040) - commands and individual bits                       */
/* ------------------------------------------------------------------------ */
#define CIA402_CW_SWITCH_ON_BIT       0x0001u
#define CIA402_CW_ENABLE_VOLTAGE_BIT  0x0002u
#define CIA402_CW_QUICK_STOP_BIT      0x0004u  /* active LOW                 */
#define CIA402_CW_ENABLE_OP_BIT       0x0008u
#define CIA402_CW_FAULT_RESET_BIT     0x0080u  /* rising-edge triggered      */
#define CIA402_CW_HALT_BIT            0x0100u

/* The standard command words. Mask 0x008F selects the command bits. */
#define CIA402_CW_MASK                0x008Fu
#define CIA402_CW_SHUTDOWN            0x0006u  /* -> Ready To Switch On      */
#define CIA402_CW_SWITCH_ON           0x0007u  /* -> Switched On             */
#define CIA402_CW_ENABLE_OPERATION    0x000Fu  /* -> Operation Enabled       */
#define CIA402_CW_DISABLE_VOLTAGE     0x0000u  /* -> Switch On Disabled      */
#define CIA402_CW_QUICK_STOP          0x0002u  /* -> Quick Stop Active       */
#define CIA402_CW_FAULT_RESET         0x0080u  /* Fault -> Switch On Disabled*/

/* ------------------------------------------------------------------------ */
/* Statusword (0x6041)                                                       */
/* ------------------------------------------------------------------------ */
#define CIA402_SW_READY_TO_SWITCH_ON  0x0001u
#define CIA402_SW_SWITCHED_ON         0x0002u
#define CIA402_SW_OPERATION_ENABLED   0x0004u
#define CIA402_SW_FAULT               0x0008u
#define CIA402_SW_VOLTAGE_ENABLED     0x0010u
#define CIA402_SW_QUICK_STOP          0x0020u  /* 0 = quick stop active      */
#define CIA402_SW_SWITCH_ON_DISABLED  0x0040u
#define CIA402_SW_WARNING             0x0080u
#define CIA402_SW_REMOTE              0x0200u
#define CIA402_SW_TARGET_REACHED      0x0400u
#define CIA402_SW_INTERNAL_LIMIT      0x0800u

/* ------------------------------------------------------------------------ */
/* Drive state machine (CiA 402 figure "Power drive system FSA")             */
/* ------------------------------------------------------------------------ */
typedef enum {
   CIA402_STATE_UNKNOWN = 0,            /* statusword matches no valid state */
   CIA402_STATE_NOT_READY_TO_SWITCH_ON,
   CIA402_STATE_SWITCH_ON_DISABLED,
   CIA402_STATE_READY_TO_SWITCH_ON,
   CIA402_STATE_SWITCHED_ON,
   CIA402_STATE_OPERATION_ENABLED,
   CIA402_STATE_QUICK_STOP_ACTIVE,
   CIA402_STATE_FAULT_REACTION_ACTIVE,
   CIA402_STATE_FAULT
} cia402_state_t;

/* ------------------------------------------------------------------------ */
/* Bitfield views of the two words.                                          */
/*                                                                           */
/* These are the structs that used to be called Control_Word / StatusWord in  */
/* elmo_ICD.h. Layout is unchanged (LSB-first on the little-endian gcc        */
/* targets we build for), so they can still be overlaid on a process image.   */
/* New code should prefer the mask/accessor functions below: bitfield layout  */
/* is implementation-defined and does not survive a change of compiler or     */
/* endianness.                                                               */
/* ------------------------------------------------------------------------ */
#ifndef CIA402_PACKED
#  define CIA402_PACKED __attribute__((__packed__))
#endif

typedef struct CIA402_PACKED {
   unsigned short switchOn               : 1;
   unsigned short enableVoltage          : 1;
   unsigned short quickStop              : 1;
   unsigned short enableOperation        : 1;
   unsigned short operationModeSpesific  : 3;
   unsigned short faultReset             : 1;
   unsigned short halt                   : 1;
   unsigned short reserved               : 2;
   unsigned short manufactorSpecific     : 5;
} cia402_controlword_bits_t;

typedef struct CIA402_PACKED {
   unsigned short readyToSwitchOn        : 1;
   unsigned short switchedOn             : 1;
   unsigned short operationEnabled       : 1;
   unsigned short fault                  : 1;
   unsigned short voltageEnabled         : 1;
   unsigned short quickStop              : 1;
   unsigned short switchOnDisabled       : 1;
   unsigned short warning                : 1;
   unsigned short reserved               : 1;
   unsigned short remote                 : 1;
   unsigned short targetReached          : 1;
   unsigned short internalLimitActive    : 1;
   unsigned short operationalModeSpecific: 2;
   unsigned short reserved2              : 2;
} cia402_statusword_bits_t;

/* ------------------------------------------------------------------------ */
/* API                                                                       */
/* ------------------------------------------------------------------------ */

/* Decode a raw statusword into the drive state. Encodings that match no
 * defined state return CIA402_STATE_UNKNOWN; callers should treat that as a
 * fault condition. */
cia402_state_t cia402_state(uint16_t statusword);

/* Next controlword to send in order to walk the drive up to Operation Enabled.
 * Feed it the statusword just received and the controlword sent last cycle;
 * call it every cycle and the drive climbs the ladder on its own:
 *
 *   Fault              -> 0x0080 fault reset (edge-generated via prev_cw)
 *   Switch On Disabled -> 0x0006 shutdown
 *   Ready To Switch On -> 0x0007 switch on
 *   Switched On        -> 0x000F enable operation
 *   Operation Enabled  -> 0x000F hold
 *
 * Only the command bits (0x008F) are produced; mode-specific bits 4..6 and any
 * manufacturer bits from prev_cw are preserved. */
uint16_t cia402_next_controlword(uint16_t statusword, uint16_t prev_cw);

/* Controlword that brings the drive to a safe stop from any state. Uses quick
 * stop while the drive is live, disable voltage otherwise. */
uint16_t cia402_safe_stop_controlword(uint16_t statusword);

/* Convenience predicates. */
int cia402_is_operational(uint16_t statusword);
int cia402_has_fault(uint16_t statusword);

/* Human-readable names for logging and diagnostics. */
const char *cia402_state_name(cia402_state_t state);
const char *cia402_mode_name(int mode);

/* True when the mode is one of the cyclic synchronous modes, i.e. the master
 * must supply a fresh setpoint every cycle. */
int cia402_mode_is_cyclic(int mode);

#endif /* CIA402_H */
