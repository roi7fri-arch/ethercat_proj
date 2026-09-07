/*
 * Trial License - for use to evaluate programs for possible purchase as
 * an end-user only.
 *
 * File: ECD_Motor.c
 *
 * Code generated for Simulink model 'ECD_Motor'.
 *
 * Model version                  : 11.27
 * Simulink Coder version         : 23.2 (R2023b) 01-Aug-2023
 * C/C++ source code generated on : Mon Jul 22 15:27:31 2024
 *
 * Target selection: ert.tlc
 * Embedded hardware selection: ARM Compatible->ARM 64-bit (LLP64)
 * Code generation objectives: Unspecified
 * Validation result: Not run
 */

#include "ECD_Motor.h"
#include <math.h>
#include <stdint.h>
#include <string.h>

/* Block parameters (default storage) */
P_ECD_Motor_T ECD_Motor_P = {
  /* Variable: PositionGain
   * Referenced by: '<Root>/PosGain'
   */
  1000.0F,

  /* Variable: TorqueGain
   * Referenced by: '<Root>/TorqueGain'
   */
  142.857147F,

  /* Variable: VelocityGain
   * Referenced by: '<Root>/VelGain'
   */
  1000.0F,

  /* Mask Parameter: CompareToConstant_const
   * Referenced by: '<S1>/Constant'
   */
  { 8U, 9U, 10U },

  /* Expression: single( 10 )
   * Referenced by: '<S2>/MATLAB System'
   */
  10.0F,

  /* Expression: single( 100 )
   * Referenced by: '<S2>/MATLAB System'
   */
  100.0F,

  /* Expression: single( 0.05 )
   * Referenced by: '<S2>/MATLAB System'
   */
  0.05F,

  /* Expression: single( 1 )
   * Referenced by: '<S2>/MATLAB System'
   */
  1.0F,

  /* Expression: single( 2.5e-4 )
   * Referenced by: '<S2>/MATLAB System'
   */
  0.00025F,

  /* Expression: single( 4 )
   * Referenced by: '<S2>/MATLAB System'
   */
  4.0F,

  /* Expression: single( 2 )
   * Referenced by: '<S2>/MATLAB System'
   */
  2.0F,

  /* Expression: single( pi/3 )
   * Referenced by: '<S2>/MATLAB System'
   */
  1.04719758F,

  /* Expression: single( 0.1 )
   * Referenced by: '<S2>/MATLAB System'
   */
  0.1F,

  /* Computed Parameter: Sin_Y0
   * Referenced by: '<S2>/Sin'
   */
  0.0F,

  /* Computed Parameter: Freq_Y0
   * Referenced by: '<S2>/Freq'
   */
  0.0F,

  /* Computed Parameter: Zero_Value
   * Referenced by: '<Root>/Zero'
   */
  0.0F,

  /* Computed Parameter: Zero1_Value
   * Referenced by: '<Root>/Zero1'
   */
  0.0F,

  /* Computed Parameter: Zero2_Value
   * Referenced by: '<Root>/Zero2'
   */
  0.0F,

  /* Computed Parameter: DiscreteTimeIntegrator_gainval
   * Referenced by: '<Root>/Discrete-Time Integrator'
   */
  0.000125F,

  /* Computed Parameter: DiscreteTimeIntegrator_IC
   * Referenced by: '<Root>/Discrete-Time Integrator'
   */
  0.0F,

  /* Expression: false
   * Referenced by: '<S2>/MATLAB System'
   */
  false,

  /* Computed Parameter: DbState_Y0
   * Referenced by: '<S2>/DbState'
   */
  0U
};

/* Forward declaration for local functions */
static FreqSweepLin_ECD_Motor_T *ECD_M_FreqSweepLin_FreqSweepLin
  (FreqSweepLin_ECD_Motor_T *obj);

/*===========*
 * Constants *
 *===========*/
#define RT_PI                          3.14159265358979323846
#define RT_PIF                         3.1415927F
#define RT_LN_10                       2.30258509299404568402
#define RT_LN_10F                      2.3025851F
#define RT_LOG10E                      0.43429448190325182765
#define RT_LOG10EF                     0.43429449F
#define RT_E                           2.7182818284590452354
#define RT_EF                          2.7182817F

/*
 * UNUSED_PARAMETER(x)
 *   Used to specify that a function parameter (argument) is required but not
 *   accessed by the function body.
 */
#ifndef UNUSED_PARAMETER
#if defined(__LCC__)
#define UNUSED_PARAMETER(x)                                      /* do nothing */
#else

/*
 * This is the semi-ANSI standard way of indicating that an
 * unused function parameter is required.
 */
#define UNUSED_PARAMETER(x)            (void) (x)
#endif
#endif

static FreqSweepLin_ECD_Motor_T *ECD_M_FreqSweepLin_FreqSweepLin
  (FreqSweepLin_ECD_Motor_T *obj)
{
  FreqSweepLin_ECD_Motor_T *b_obj;

  /* Start for MATLABSystem: '<S2>/MATLAB System' */
  /*  Constructor */
  b_obj = obj;
  obj->isInitialized = 0;

  /*  Support name-value pair arguments when constructing object */
  obj->MinFreq = 10.0F;

  /* [Hz] */
  obj->MaxFreq = 100.0F;

  /* [Hz] */
  obj->Step = 0.05F;
  obj->dt = 0.0005F;
  obj->NumOfCycles = 1.0F;
  obj->MaxAcc = 4.0F;

  /* [rad/s^2] */
  obj->MaxVel = 2.0F;

  /* [rad/s] */
  obj->MaxAngle = 1.04719758F;

  /* [rad] */
  obj->Amp = 0.1F;

  /* [rad/s] */
  obj->EnableLimits = false;
  return b_obj;
}

/* Model step function */
void ECD_Motor_step(RT_MODEL_ECD_Motor_T *const ECD_Motor_M, ExtU_ECD_Motor_T
                    *ECD_Motor_U, ExtY_ECD_Motor_T *ECD_Motor_Y)
{
  B_ECD_Motor_T *ECD_Motor_B = ECD_Motor_M->blockIO;
  DW_ECD_Motor_T *ECD_Motor_DW = ECD_Motor_M->dwork;
  float DiscreteTimeIntegrator;
  float rtb_MultiportSwitch_idx_0;
  float rtb_MultiportSwitch_idx_1;
  float rtb_MultiportSwitch_idx_2;
  float varargin_1_idx_1;
  int32_t b_tmp;
  uint32_t tmp;
  uint16_t tmp_0;

  /* Outputs for Enabled SubSystem: '<Root>/Sin Generator' incorporates:
   *  EnablePort: '<S2>/Enable'
   */
  /* Logic: '<Root>/Logical Operator' incorporates:
   *  Constant: '<S1>/Constant'
   *  RelationalOperator: '<S1>/Compare'
   */
  if ((ECD_Motor_U->MMCIn_ModeOfOperation ==
       ECD_Motor_P.CompareToConstant_const[0]) ||
      (ECD_Motor_U->MMCIn_ModeOfOperation ==
       ECD_Motor_P.CompareToConstant_const[1]) ||
      (ECD_Motor_U->MMCIn_ModeOfOperation ==
       ECD_Motor_P.CompareToConstant_const[2])) {
    if (!ECD_Motor_DW->SinGenerator_MODE) {
      /* InitializeConditions for MATLABSystem: '<S2>/MATLAB System' */
      /*  Initialize / reset discrete-state properties */
      rtb_MultiportSwitch_idx_1 = ECD_Motor_DW->obj.MinFreq;
      varargin_1_idx_1 = floorf(log10f(rtb_MultiportSwitch_idx_1));
      if (varargin_1_idx_1 < 32768.0F) {
        if (varargin_1_idx_1 >= -32768.0F) {
          ECD_Motor_DW->obj.Power = (int16_t)varargin_1_idx_1;
        } else {
          ECD_Motor_DW->obj.Power = INT16_MIN;
        }
      } else {
        ECD_Motor_DW->obj.Power = INT16_MAX;
      }

      /* find min freq scale 1's 10's 100's */
      b_tmp = ECD_Motor_DW->obj.Power;
      rtb_MultiportSwitch_idx_1 = ECD_Motor_DW->obj.MinFreq - powf(10.0F, (float)
        b_tmp);
      rtb_MultiportSwitch_idx_2 = ECD_Motor_DW->obj.Step * powf(2.0F, (float)
        b_tmp);
      varargin_1_idx_1 = ceilf(rtb_MultiportSwitch_idx_1 /
        rtb_MultiportSwitch_idx_2);
      if (varargin_1_idx_1 < 65536.0F) {
        if (varargin_1_idx_1 >= 0.0F) {
          ECD_Motor_DW->obj.Ind = (uint16_t)varargin_1_idx_1;
        } else {
          ECD_Motor_DW->obj.Ind = 0U;
        }
      } else {
        ECD_Motor_DW->obj.Ind = UINT16_MAX;
      }

      /* find index in density */
      ECD_Motor_DW->obj.Phase = 0.0F;
      ECD_Motor_DW->obj.Tstop = 1.0F;
      ECD_Motor_DW->obj.t = 0.0F;
      ECD_Motor_DW->obj.State = 1U;
      ECD_Motor_DW->obj.w = 0.0F;

      /* End of InitializeConditions for MATLABSystem: '<S2>/MATLAB System' */
      ECD_Motor_DW->SinGenerator_MODE = true;
    }

    /* MATLABSystem: '<S2>/MATLAB System' */
    if (ECD_Motor_DW->obj.MinFreq != ECD_Motor_P.MATLABSystem_MinFreq) {
      ECD_Motor_DW->obj.MinFreq = ECD_Motor_P.MATLABSystem_MinFreq;
    }

    if (ECD_Motor_DW->obj.MaxFreq != ECD_Motor_P.MATLABSystem_MaxFreq) {
      ECD_Motor_DW->obj.MaxFreq = ECD_Motor_P.MATLABSystem_MaxFreq;
    }

    if (ECD_Motor_DW->obj.Step != ECD_Motor_P.MATLABSystem_Step) {
      ECD_Motor_DW->obj.Step = ECD_Motor_P.MATLABSystem_Step;
    }

    if (ECD_Motor_DW->obj.NumOfCycles != ECD_Motor_P.MATLABSystem_NumOfCycles) {
      ECD_Motor_DW->obj.NumOfCycles = ECD_Motor_P.MATLABSystem_NumOfCycles;
    }

    if (ECD_Motor_DW->obj.dt != ECD_Motor_P.MATLABSystem_dt) {
      ECD_Motor_DW->obj.dt = ECD_Motor_P.MATLABSystem_dt;
    }

    if (ECD_Motor_DW->obj.MaxAcc != ECD_Motor_P.MATLABSystem_MaxAcc) {
      ECD_Motor_DW->obj.MaxAcc = ECD_Motor_P.MATLABSystem_MaxAcc;
    }

    if (ECD_Motor_DW->obj.MaxVel != ECD_Motor_P.MATLABSystem_MaxVel) {
      ECD_Motor_DW->obj.MaxVel = ECD_Motor_P.MATLABSystem_MaxVel;
    }

    if (ECD_Motor_DW->obj.MaxAngle != ECD_Motor_P.MATLABSystem_MaxAngle) {
      ECD_Motor_DW->obj.MaxAngle = ECD_Motor_P.MATLABSystem_MaxAngle;
    }

    if (ECD_Motor_DW->obj.Amp != ECD_Motor_P.MATLABSystem_Amp) {
      ECD_Motor_DW->obj.Amp = ECD_Motor_P.MATLABSystem_Amp;
    }

    if (ECD_Motor_DW->obj.EnableLimits != ECD_Motor_P.MATLABSystem_EnableLimits)
    {
      ECD_Motor_DW->obj.EnableLimits = ECD_Motor_P.MATLABSystem_EnableLimits;
    }

    switch (ECD_Motor_DW->obj.State) {
     case 1U:
     case 3U:
      ECD_Motor_DW->obj.Phase += 6.28318548F * ECD_Motor_DW->obj.w *
        ECD_Motor_DW->obj.t;
      ECD_Motor_DW->obj.t = ECD_Motor_DW->obj.dt;
      b_tmp = ECD_Motor_DW->obj.Power;
      ECD_Motor_DW->obj.w = (float)ECD_Motor_DW->obj.Ind *
        ECD_Motor_DW->obj.Step * powf(2.0F, (float)b_tmp) + powf(10.0F, (float)
        b_tmp);
      varargin_1_idx_1 = roundf(ECD_Motor_DW->obj.NumOfCycles * ceilf
        (ECD_Motor_DW->obj.w) + 1.0F);
      if (varargin_1_idx_1 < 65536.0F) {
        if (varargin_1_idx_1 >= 0.0F) {
          tmp_0 = (uint16_t)varargin_1_idx_1;
        } else {
          tmp_0 = 0U;
        }
      } else {
        tmp_0 = UINT16_MAX;
      }

      ECD_Motor_DW->obj.Tstop = roundf((float)tmp_0 / ECD_Motor_DW->obj.w *
        10000.0F) / 10000.0F;
      if (ECD_Motor_DW->obj.EnableLimits) {
        rtb_MultiportSwitch_idx_1 = 6.28318548F * ECD_Motor_DW->obj.w;
        rtb_MultiportSwitch_idx_2 = ECD_Motor_DW->obj.MaxAcc /
          rtb_MultiportSwitch_idx_1;
        varargin_1_idx_1 = ECD_Motor_DW->obj.MaxVel;
        rtb_MultiportSwitch_idx_0 = ECD_Motor_DW->obj.MaxAngle / 2.0F *
          rtb_MultiportSwitch_idx_1;
        DiscreteTimeIntegrator = ECD_Motor_DW->obj.Amp;
        if (rtb_MultiportSwitch_idx_2 > varargin_1_idx_1) {
          rtb_MultiportSwitch_idx_2 = varargin_1_idx_1;
        }

        if (rtb_MultiportSwitch_idx_2 > rtb_MultiportSwitch_idx_0) {
          rtb_MultiportSwitch_idx_2 = rtb_MultiportSwitch_idx_0;
        }

        if (rtb_MultiportSwitch_idx_2 > DiscreteTimeIntegrator) {
          rtb_MultiportSwitch_idx_2 = DiscreteTimeIntegrator;
        }

        /* MATLABSystem: '<S2>/MATLAB System' */
        ECD_Motor_B->MATLABSystem_o1 = sinf(rtb_MultiportSwitch_idx_1 *
          ECD_Motor_DW->obj.t + ECD_Motor_DW->obj.Phase) *
          rtb_MultiportSwitch_idx_2;
      } else {
        /* MATLABSystem: '<S2>/MATLAB System' */
        ECD_Motor_B->MATLABSystem_o1 = sinf(6.28318548F * ECD_Motor_DW->obj.w *
          ECD_Motor_DW->obj.t + ECD_Motor_DW->obj.Phase) * ECD_Motor_DW->obj.Amp;
      }

      ECD_Motor_DW->obj.State = 2U;
      break;

     case 2U:
      /* During Freq */
      ECD_Motor_DW->obj.t += ECD_Motor_DW->obj.dt;
      if (ECD_Motor_DW->obj.EnableLimits) {
        rtb_MultiportSwitch_idx_1 = 6.28318548F * ECD_Motor_DW->obj.w;
        rtb_MultiportSwitch_idx_2 = ECD_Motor_DW->obj.MaxAcc /
          rtb_MultiportSwitch_idx_1;
        varargin_1_idx_1 = ECD_Motor_DW->obj.MaxVel;
        rtb_MultiportSwitch_idx_0 = ECD_Motor_DW->obj.MaxAngle / 2.0F *
          rtb_MultiportSwitch_idx_1;
        DiscreteTimeIntegrator = ECD_Motor_DW->obj.Amp;
        if (rtb_MultiportSwitch_idx_2 > varargin_1_idx_1) {
          rtb_MultiportSwitch_idx_2 = varargin_1_idx_1;
        }

        if (rtb_MultiportSwitch_idx_2 > rtb_MultiportSwitch_idx_0) {
          rtb_MultiportSwitch_idx_2 = rtb_MultiportSwitch_idx_0;
        }

        if (rtb_MultiportSwitch_idx_2 > DiscreteTimeIntegrator) {
          rtb_MultiportSwitch_idx_2 = DiscreteTimeIntegrator;
        }

        /* MATLABSystem: '<S2>/MATLAB System' */
        ECD_Motor_B->MATLABSystem_o1 = sinf(rtb_MultiportSwitch_idx_1 *
          ECD_Motor_DW->obj.t + ECD_Motor_DW->obj.Phase) *
          rtb_MultiportSwitch_idx_2;
      } else {
        /* MATLABSystem: '<S2>/MATLAB System' */
        ECD_Motor_B->MATLABSystem_o1 = sinf(6.28318548F * ECD_Motor_DW->obj.w *
          ECD_Motor_DW->obj.t + ECD_Motor_DW->obj.Phase) * ECD_Motor_DW->obj.Amp;
      }

      ECD_Motor_DW->obj.State = 2U;
      if (ECD_Motor_DW->obj.t >= ECD_Motor_DW->obj.Tstop) {
        /* End of Freq */
        tmp = ECD_Motor_DW->obj.Ind + 1U;
        if (tmp > 65535U) {
          tmp = 65535U;
        }

        ECD_Motor_DW->obj.Ind = (uint16_t)tmp;
        ECD_Motor_DW->obj.State = 3U;
        b_tmp = ECD_Motor_DW->obj.Power;
        if (ECD_Motor_DW->obj.Ind >= 9.0F * powf(10.0F, (float)b_tmp) /
            (ECD_Motor_DW->obj.Step * powf(2.0F, (float)b_tmp))) {
          b_tmp = ECD_Motor_DW->obj.Power + 1;
          if (b_tmp > 32767) {
            b_tmp = 32767;
          }

          ECD_Motor_DW->obj.Power = (int16_t)b_tmp;
          ECD_Motor_DW->obj.Ind = 0U;
        }

        b_tmp = ECD_Motor_DW->obj.Power;
        if ((float)ECD_Motor_DW->obj.Ind * ECD_Motor_DW->obj.Step * powf(2.0F,
             (float)b_tmp) + powf(10.0F, (float)b_tmp) >
            ECD_Motor_DW->obj.MaxFreq) {
          /* MATLABSystem: '<S2>/MATLAB System' */
          /* End Of Swipe */
          ECD_Motor_B->MATLABSystem_o1 = 0.0F;
          ECD_Motor_DW->obj.State = 4U;
          ECD_Motor_DW->obj.w = 0.0F;
        }
      }
      break;

     case 4U:
      /* MATLABSystem: '<S2>/MATLAB System' */
      ECD_Motor_B->MATLABSystem_o1 = 0.0F;
      ECD_Motor_DW->obj.w = 0.0F;
      ECD_Motor_DW->obj.State = 4U;
      break;

     default:
      /* MATLABSystem: '<S2>/MATLAB System' */
      ECD_Motor_B->MATLABSystem_o1 = 0.0F;
      ECD_Motor_DW->obj.w = 0.0F;
      ECD_Motor_DW->obj.State = 4U;
      break;
    }

    /* MATLABSystem: '<S2>/MATLAB System' */
    ECD_Motor_B->MATLABSystem_o2 = ECD_Motor_DW->obj.w;

    /* MATLABSystem: '<S2>/MATLAB System' */
    ECD_Motor_B->MATLABSystem_o3 = ECD_Motor_DW->obj.State;
  } else if (ECD_Motor_DW->SinGenerator_MODE) {
    /* Disable for MATLABSystem: '<S2>/MATLAB System' incorporates:
     *  Outport: '<S2>/Sin'
     */
    ECD_Motor_B->MATLABSystem_o1 = ECD_Motor_P.Sin_Y0;

    /* Disable for MATLABSystem: '<S2>/MATLAB System' incorporates:
     *  Outport: '<S2>/Freq'
     */
    ECD_Motor_B->MATLABSystem_o2 = ECD_Motor_P.Freq_Y0;

    /* Disable for MATLABSystem: '<S2>/MATLAB System' incorporates:
     *  Outport: '<S2>/DbState'
     */
    ECD_Motor_B->MATLABSystem_o3 = ECD_Motor_P.DbState_Y0;
    ECD_Motor_DW->SinGenerator_MODE = false;
  }

  /* End of Logic: '<Root>/Logical Operator' */
  /* End of Outputs for SubSystem: '<Root>/Sin Generator' */

  /* DiscreteIntegrator: '<Root>/Discrete-Time Integrator' */
  varargin_1_idx_1 = ECD_Motor_P.DiscreteTimeIntegrator_gainval *
    ECD_Motor_B->MATLABSystem_o1;

  /* DiscreteIntegrator: '<Root>/Discrete-Time Integrator' */
  DiscreteTimeIntegrator = varargin_1_idx_1 +
    ECD_Motor_DW->DiscreteTimeIntegrator_DSTATE;

  /* MultiPortSwitch: '<Root>/Multiport Switch' incorporates:
   *  Constant: '<Root>/Zero'
   *  Constant: '<Root>/Zero1'
   *  Constant: '<Root>/Zero2'
   */
  switch (ECD_Motor_U->MMCIn_ModeOfOperation) {
   case 8:
   case 9:
    rtb_MultiportSwitch_idx_0 = DiscreteTimeIntegrator;
    rtb_MultiportSwitch_idx_1 = ECD_Motor_B->MATLABSystem_o1;
    rtb_MultiportSwitch_idx_2 = ECD_Motor_P.Zero_Value;
    break;

   default:
    rtb_MultiportSwitch_idx_0 = ECD_Motor_P.Zero1_Value;
    rtb_MultiportSwitch_idx_1 = ECD_Motor_P.Zero2_Value;
    rtb_MultiportSwitch_idx_2 = ECD_Motor_B->MATLABSystem_o1;
    break;
  }

  /* End of MultiPortSwitch: '<Root>/Multiport Switch' */

  /* Update for DiscreteIntegrator: '<Root>/Discrete-Time Integrator' */
  ECD_Motor_DW->DiscreteTimeIntegrator_DSTATE = varargin_1_idx_1 +
    DiscreteTimeIntegrator;

  /* Outport generated from: '<Root>/Debug' incorporates:
   *  DataTypeConversion: '<Root>/Cast'
   */
  ECD_Motor_Y->Debug[0] = ECD_Motor_B->MATLABSystem_o1;
  ECD_Motor_Y->Debug[1] = ECD_Motor_B->MATLABSystem_o2;
  ECD_Motor_Y->Debug[2] = ECD_Motor_B->MATLABSystem_o3;

  /* Gain: '<Root>/PosGain' */
  varargin_1_idx_1 = fmodf(roundf(ECD_Motor_P.PositionGain *
    rtb_MultiportSwitch_idx_0), 4.2949673E+9F);

  /* Outport generated from: '<Root>/PosOffset' incorporates:
   *  Gain: '<Root>/PosGain'
   */
  ECD_Motor_Y->PosOffset = varargin_1_idx_1 < 0.0F ? -(int32_t)(uint32_t)-
    varargin_1_idx_1 : (int32_t)(uint32_t)varargin_1_idx_1;

  /* Gain: '<Root>/VelGain' */
  varargin_1_idx_1 = fmodf(roundf(ECD_Motor_P.VelocityGain *
    rtb_MultiportSwitch_idx_1), 4.2949673E+9F);

  /* Outport generated from: '<Root>/VelOffset' incorporates:
   *  Gain: '<Root>/VelGain'
   */
  ECD_Motor_Y->VelOffset = varargin_1_idx_1 < 0.0F ? -(int32_t)(uint32_t)-
    varargin_1_idx_1 : (int32_t)(uint32_t)varargin_1_idx_1;

  /* Gain: '<Root>/TorqueGain' */
  varargin_1_idx_1 = fmodf(roundf(ECD_Motor_P.TorqueGain *
    rtb_MultiportSwitch_idx_2), 65536.0F);

  /* Outport generated from: '<Root>/TorqueOffset' incorporates:
   *  Gain: '<Root>/TorqueGain'
   */
  ECD_Motor_Y->TorqueOffset = (int16_t)(varargin_1_idx_1 < 0.0F ? (int32_t)
    (int16_t)-(int16_t)(uint16_t)-varargin_1_idx_1 : (int32_t)(int16_t)(uint16_t)
    varargin_1_idx_1);
}

/* Model initialize function */
void ECD_Motor_initialize(RT_MODEL_ECD_Motor_T *const ECD_Motor_M,
  ExtU_ECD_Motor_T *ECD_Motor_U, ExtY_ECD_Motor_T *ECD_Motor_Y)
{
  B_ECD_Motor_T *ECD_Motor_B = ECD_Motor_M->blockIO;
  DW_ECD_Motor_T *ECD_Motor_DW = ECD_Motor_M->dwork;

  /* Registration code */

  /* block I/O */
  (void) memset(((void *) ECD_Motor_B), 0,
                sizeof(B_ECD_Motor_T));

  /* states (dwork) */
  (void) memset((void *)ECD_Motor_DW, 0,
                sizeof(DW_ECD_Motor_T));

  /* external inputs */
  ECD_Motor_U->MMCIn_ModeOfOperation = 0U;

  /* external outputs */
  (void)memset(ECD_Motor_Y, 0, sizeof(ExtY_ECD_Motor_T));

  {
    float x;
    float y;
    int32_t b_tmp;

    /* InitializeConditions for DiscreteIntegrator: '<Root>/Discrete-Time Integrator' */
    ECD_Motor_DW->DiscreteTimeIntegrator_DSTATE =
      ECD_Motor_P.DiscreteTimeIntegrator_IC;

    /* SystemInitialize for Enabled SubSystem: '<Root>/Sin Generator' */
    /* Start for MATLABSystem: '<S2>/MATLAB System' */
    ECD_M_FreqSweepLin_FreqSweepLin(&ECD_Motor_DW->obj);
    ECD_Motor_DW->obj.MinFreq = ECD_Motor_P.MATLABSystem_MinFreq;
    ECD_Motor_DW->obj.MaxFreq = ECD_Motor_P.MATLABSystem_MaxFreq;
    ECD_Motor_DW->obj.Step = ECD_Motor_P.MATLABSystem_Step;
    ECD_Motor_DW->obj.NumOfCycles = ECD_Motor_P.MATLABSystem_NumOfCycles;
    ECD_Motor_DW->obj.dt = ECD_Motor_P.MATLABSystem_dt;
    ECD_Motor_DW->obj.MaxAcc = ECD_Motor_P.MATLABSystem_MaxAcc;
    ECD_Motor_DW->obj.MaxVel = ECD_Motor_P.MATLABSystem_MaxVel;
    ECD_Motor_DW->obj.MaxAngle = ECD_Motor_P.MATLABSystem_MaxAngle;
    ECD_Motor_DW->obj.Amp = ECD_Motor_P.MATLABSystem_Amp;
    ECD_Motor_DW->obj.EnableLimits = ECD_Motor_P.MATLABSystem_EnableLimits;
    ECD_Motor_DW->obj.isInitialized = 1;

    /* InitializeConditions for MATLABSystem: '<S2>/MATLAB System' */
    /*         %% Common functions */
    /*  Perform one-time calculations, such as computing constants */
    /* find min freq scale 1's 10's 100's */
    /* find index in density */
    /*  Initialize / reset discrete-state properties */
    x = ECD_Motor_DW->obj.MinFreq;
    x = floorf(log10f(x));
    if (x < 32768.0F) {
      if (x >= -32768.0F) {
        ECD_Motor_DW->obj.Power = (int16_t)x;
      } else {
        ECD_Motor_DW->obj.Power = INT16_MIN;
      }
    } else {
      ECD_Motor_DW->obj.Power = INT16_MAX;
    }

    /* find min freq scale 1's 10's 100's */
    b_tmp = ECD_Motor_DW->obj.Power;
    x = ECD_Motor_DW->obj.MinFreq - powf(10.0F, (float)b_tmp);
    y = ECD_Motor_DW->obj.Step * powf(2.0F, (float)b_tmp);
    x = ceilf(x / y);
    if (x < 65536.0F) {
      if (x >= 0.0F) {
        ECD_Motor_DW->obj.Ind = (uint16_t)x;
      } else {
        ECD_Motor_DW->obj.Ind = 0U;
      }
    } else {
      ECD_Motor_DW->obj.Ind = UINT16_MAX;
    }

    /* find index in density */
    ECD_Motor_DW->obj.Phase = 0.0F;
    ECD_Motor_DW->obj.Tstop = 1.0F;
    ECD_Motor_DW->obj.t = 0.0F;
    ECD_Motor_DW->obj.State = 1U;
    ECD_Motor_DW->obj.w = 0.0F;

    /* End of InitializeConditions for MATLABSystem: '<S2>/MATLAB System' */

    /* SystemInitialize for MATLABSystem: '<S2>/MATLAB System' incorporates:
     *  Outport: '<S2>/Sin'
     */
    ECD_Motor_B->MATLABSystem_o1 = ECD_Motor_P.Sin_Y0;

    /* SystemInitialize for MATLABSystem: '<S2>/MATLAB System' incorporates:
     *  Outport: '<S2>/Freq'
     */
    ECD_Motor_B->MATLABSystem_o2 = ECD_Motor_P.Freq_Y0;

    /* SystemInitialize for MATLABSystem: '<S2>/MATLAB System' incorporates:
     *  Outport: '<S2>/DbState'
     */
    ECD_Motor_B->MATLABSystem_o3 = ECD_Motor_P.DbState_Y0;

    /* End of SystemInitialize for SubSystem: '<Root>/Sin Generator' */
  }
}

/* Model terminate function */
void ECD_Motor_terminate(RT_MODEL_ECD_Motor_T *const ECD_Motor_M)
{
  /* (no terminate code required) */
  UNUSED_PARAMETER(ECD_Motor_M);
}

/*
 * File trailer for generated code.
 *
 * [EOF]
 */
