/*
 * Trial License - for use to evaluate programs for possible purchase as
 * an end-user only.
 *
 * File: ECD_Motor.h
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

#ifndef RTW_HEADER_ECD_Motor_h_
#define RTW_HEADER_ECD_Motor_h_
#ifndef ECD_Motor_COMMON_INCLUDES_
#define ECD_Motor_COMMON_INCLUDES_
#include <stdbool.h>
#include <stdint.h>
#endif                                 /* ECD_Motor_COMMON_INCLUDES_ */

#include <string.h>

/* Macros for accessing real-time model data structure */
#ifndef rtmGetErrorStatus
#define rtmGetErrorStatus(rtm)         ((rtm)->errorStatus)
#endif

#ifndef rtmSetErrorStatus
#define rtmSetErrorStatus(rtm, val)    ((rtm)->errorStatus = (val))
#endif

/* Forward declaration for rtModel */
typedef struct tag_RTM_ECD_Motor_T RT_MODEL_ECD_Motor_T;

#ifndef struct_tag_1ERgRj8iRb4CcUcbxfgMaF
#define struct_tag_1ERgRj8iRb4CcUcbxfgMaF

struct tag_1ERgRj8iRb4CcUcbxfgMaF
{
  int32_t isInitialized;
  float MinFreq;
  float MaxFreq;
  float Step;
  float NumOfCycles;
  float dt;
  float MaxAcc;
  float MaxVel;
  float MaxAngle;
  float Amp;
  bool EnableLimits;
  float Phase;
  float Tstop;
  uint16_t Ind;
  float t;
  float w;
  uint8_t State;
  int16_t Power;
};

#endif                                 /* struct_tag_1ERgRj8iRb4CcUcbxfgMaF */

#ifndef typedef_FreqSweepLin_ECD_Motor_T
#define typedef_FreqSweepLin_ECD_Motor_T

typedef struct tag_1ERgRj8iRb4CcUcbxfgMaF FreqSweepLin_ECD_Motor_T;

#endif                                 /* typedef_FreqSweepLin_ECD_Motor_T */

/* Block signals (default storage) */
typedef struct {
  float MATLABSystem_o1;               /* '<S2>/MATLAB System' */
  float MATLABSystem_o2;               /* '<S2>/MATLAB System' */
  uint8_t MATLABSystem_o3;             /* '<S2>/MATLAB System' */
} B_ECD_Motor_T;

/* Block states (default storage) for system '<Root>' */
typedef struct {
  FreqSweepLin_ECD_Motor_T obj;        /* '<S2>/MATLAB System' */
  float DiscreteTimeIntegrator_DSTATE; /* '<Root>/Discrete-Time Integrator' */
  bool SinGenerator_MODE;              /* '<Root>/Sin Generator' */
} DW_ECD_Motor_T;

/* External inputs (root inport signals with default storage) */
typedef struct {
  uint16_t MMCIn_ModeOfOperation;      /* '<Root>/MMCIn_ModeOfOperation' */
} ExtU_ECD_Motor_T;

/* External outputs (root outports fed by signals with default storage) */
typedef struct {
  int32_t PosOffset;                   /* '<Root>/PosOffset' */
  int32_t VelOffset;                   /* '<Root>/VelOffset' */
  int16_t TorqueOffset;                /* '<Root>/TorqueOffset' */
  float Debug[3];                      /* '<Root>/Debug' */
} ExtY_ECD_Motor_T;

/* Parameters (default storage) */
struct P_ECD_Motor_T_ {
  float PositionGain;                  /* Variable: PositionGain
                                        * Referenced by: '<Root>/PosGain'
                                        */
  float TorqueGain;                    /* Variable: TorqueGain
                                        * Referenced by: '<Root>/TorqueGain'
                                        */
  float VelocityGain;                  /* Variable: VelocityGain
                                        * Referenced by: '<Root>/VelGain'
                                        */
  uint8_t CompareToConstant_const[3]; /* Mask Parameter: CompareToConstant_const
                                       * Referenced by: '<S1>/Constant'
                                       */
  float MATLABSystem_MinFreq;          /* Expression: single( 10 )
                                        * Referenced by: '<S2>/MATLAB System'
                                        */
  float MATLABSystem_MaxFreq;          /* Expression: single( 100 )
                                        * Referenced by: '<S2>/MATLAB System'
                                        */
  float MATLABSystem_Step;             /* Expression: single( 0.05 )
                                        * Referenced by: '<S2>/MATLAB System'
                                        */
  float MATLABSystem_NumOfCycles;      /* Expression: single( 1 )
                                        * Referenced by: '<S2>/MATLAB System'
                                        */
  float MATLABSystem_dt;               /* Expression: single( 2.5e-4 )
                                        * Referenced by: '<S2>/MATLAB System'
                                        */
  float MATLABSystem_MaxAcc;           /* Expression: single( 4 )
                                        * Referenced by: '<S2>/MATLAB System'
                                        */
  float MATLABSystem_MaxVel;           /* Expression: single( 2 )
                                        * Referenced by: '<S2>/MATLAB System'
                                        */
  float MATLABSystem_MaxAngle;         /* Expression: single( pi/3 )
                                        * Referenced by: '<S2>/MATLAB System'
                                        */
  float MATLABSystem_Amp;              /* Expression: single( 0.1 )
                                        * Referenced by: '<S2>/MATLAB System'
                                        */
  float Sin_Y0;                        /* Computed Parameter: Sin_Y0
                                        * Referenced by: '<S2>/Sin'
                                        */
  float Freq_Y0;                       /* Computed Parameter: Freq_Y0
                                        * Referenced by: '<S2>/Freq'
                                        */
  float Zero_Value;                    /* Computed Parameter: Zero_Value
                                        * Referenced by: '<Root>/Zero'
                                        */
  float Zero1_Value;                   /* Computed Parameter: Zero1_Value
                                        * Referenced by: '<Root>/Zero1'
                                        */
  float Zero2_Value;                   /* Computed Parameter: Zero2_Value
                                        * Referenced by: '<Root>/Zero2'
                                        */
  float DiscreteTimeIntegrator_gainval;
                           /* Computed Parameter: DiscreteTimeIntegrator_gainval
                            * Referenced by: '<Root>/Discrete-Time Integrator'
                            */
  float DiscreteTimeIntegrator_IC;
                                /* Computed Parameter: DiscreteTimeIntegrator_IC
                                 * Referenced by: '<Root>/Discrete-Time Integrator'
                                 */
  bool MATLABSystem_EnableLimits;      /* Expression: false
                                        * Referenced by: '<S2>/MATLAB System'
                                        */
  uint8_t DbState_Y0;                  /* Computed Parameter: DbState_Y0
                                        * Referenced by: '<S2>/DbState'
                                        */
};

/* Parameters (default storage) */
typedef struct P_ECD_Motor_T_ P_ECD_Motor_T;

/* Real-time Model Data Structure */
struct tag_RTM_ECD_Motor_T {
  const char * volatile errorStatus;
  B_ECD_Motor_T *blockIO;
  DW_ECD_Motor_T *dwork;
};

/* Block parameters (default storage) */
extern P_ECD_Motor_T ECD_Motor_P;

/* Model entry point functions */
extern void ECD_Motor_initialize(RT_MODEL_ECD_Motor_T *const ECD_Motor_M,
  ExtU_ECD_Motor_T *ECD_Motor_U, ExtY_ECD_Motor_T *ECD_Motor_Y);
extern void ECD_Motor_step(RT_MODEL_ECD_Motor_T *const ECD_Motor_M,
  ExtU_ECD_Motor_T *ECD_Motor_U, ExtY_ECD_Motor_T *ECD_Motor_Y);
extern void ECD_Motor_terminate(RT_MODEL_ECD_Motor_T *const ECD_Motor_M);

/*-
 * The generated code includes comments that allow you to trace directly
 * back to the appropriate location in the model.  The basic format
 * is <system>/block_name, where system is the system number (uniquely
 * assigned by Simulink) and block_name is the name of the block.
 *
 * Use the MATLAB hilite_system command to trace the generated code back
 * to the model.  For example,
 *
 * hilite_system('<S3>')    - opens system 3
 * hilite_system('<S3>/Kp') - opens and selects block Kp which resides in S3
 *
 * Here is the system hierarchy for this model
 *
 * '<Root>' : 'ECD_Motor'
 * '<S1>'   : 'ECD_Motor/Compare To Constant'
 * '<S2>'   : 'ECD_Motor/Sin Generator'
 */
#endif                                 /* RTW_HEADER_ECD_Motor_h_ */

/*
 * File trailer for generated code.
 *
 * [EOF]
 */
