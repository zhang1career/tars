#include "tars_foc.h"
#include "tars_res_mgr.h"
#include "foc_step_stm32.h"
#include "foc_step_stm32_initialize.h"
#include "foc_params.h"
#include "main.h"

/* ------------------------------------------------------------------ */
/* Build-time configuration                                           */
/* ------------------------------------------------------------------ */
/* Motor-control variant: TIM1 advanced 6-PWM + ADC1 injected exist (see
 * Core/Src/tim.c, adc.c). Outputs stay disabled (MOE off) until an explicit
 * TarsFoc_Enable(1), so flashing this never energizes the bridge on its own. */
#ifndef TARS_FOC_DRIVE_PWM
#define TARS_FOC_DRIVE_PWM   1
#endif

#if TARS_FOC_DRIVE_PWM
#include "tim.h"
#include "adc.h"
#endif

/* Nominal bus used for the bench/idle path and as a guard before the real
 * Vdc measurement is available (single-sourced from the Simulink model). */
#define TARS_FOC_VDC_NOMINAL FOC_PARAM_VDC_V

#if TARS_FOC_DRIVE_PWM
/* ---- Board / sensor scaling (EDIT FOR YOUR POWER STAGE) ---------- */
/* Mirrors motor-ctrl-sim/stm32/foc_app.c; kept here because these are board
 * (not algorithm) parameters and must live in firmware. */
#define ADC_VREF        3.3f
#define ADC_FULL        4095.0f
#define ISHUNT_OHM      0.010f
#define IAMP_GAIN       20.0f
#define I_COUNTS_TO_A   (ADC_VREF / ADC_FULL / (ISHUNT_OHM * IAMP_GAIN))
#define VBUS_DIV        11.0f
#define V_COUNTS_TO_V   (ADC_VREF / ADC_FULL * VBUS_DIV)
static uint16_t s_ia_offset = 2048U;
static uint16_t s_ib_offset = 2048U;
static uint16_t s_ic_offset = 2048U;

/* Zero-current offset calibration: average N samples with the bridge OFF
 * (MOE disabled -> no current). Run at init and on demand (`motor cal`). */
#define TARS_FOC_CAL_SAMPLES   1024U
static volatile uint16_t s_cal_remaining;
static uint32_t s_cal_acc_ia;
static uint32_t s_cal_acc_ib;
static uint32_t s_cal_acc_ic;
#endif /* TARS_FOC_DRIVE_PWM */

/* ------------------------------------------------------------------ */
/* State                                                              */
/* ------------------------------------------------------------------ */
static tars_foc_snapshot_t s_snap;
static volatile float      s_speed_ref_rpm;
static volatile uint8_t    s_enable;
static uint8_t             s_initialized;

static void foc_store_snapshot(const tars_foc_snapshot_t *src)
{
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  s_snap = *src;
  if (primask == 0U)
  {
    __enable_irq();
  }
}

void TarsFoc_Init(void)
{
  if (s_initialized != 0U)
  {
    return;
  }

  foc_step_stm32_initialize();   /* zeroes the generated controller state */

  s_speed_ref_rpm = 0.0f;
  s_enable        = 0U;          /* SAFE default: controller disabled       */

  s_snap.theta_est_rad = 0.0f;
  s_snap.speed_est_rpm = 0.0f;
  s_snap.speed_ref_rpm = 0.0f;
  s_snap.id = 0.0f;
  s_snap.iq = 0.0f;
  s_snap.vdc = TARS_FOC_VDC_NOMINAL;
  s_snap.ia = 0.0f;
  s_snap.ib = 0.0f;
  s_snap.ic = 0.0f;
  s_snap.duty_a = 0.5f;
  s_snap.duty_b = 0.5f;
  s_snap.duty_c = 0.5f;
  s_snap.loop_count = 0U;
  s_snap.enabled = 0U;
  s_snap.state = TARS_FOC_STATE_IDLE;
  s_snap.fault_code = 0U;

  s_initialized = 1U;
}

void TarsFoc_BootHw(void)
{
#if TARS_FOC_DRIVE_PWM
  if (s_initialized == 0U)
  {
    TarsFoc_Init();
  }

  /* TODO(bring-up): calibrate s_ia/ib/ic_offset by averaging ADC samples with
   * the bridge disabled (MOE off) before the first enable. */

  /* Injected sampling is hardware-triggered by TIM1 TRGO; JEOC drives the
   * control loop. Start it, then start all three PWM channel pairs. */
  (void)HAL_ADCEx_InjectedStart_IT(&hadc1);

  (void)HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
  (void)HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_1);
  (void)HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2);
  (void)HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_2);
  (void)HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_3);
  (void)HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_3);

  /* Keep the bridge OFF: TIM1 counts (so ADC is triggered and telemetry runs)
   * but the gate outputs are disabled until TarsFoc_Enable(1). */
  __HAL_TIM_MOE_DISABLE(&htim1);

  /* Best-effort zero-current offset calibration now (bridge off). Re-run via
   * `motor cal` once the power stage is up and the motor is at rest. */
  TarsFoc_Calibrate();
#endif
}

void TarsFoc_Calibrate(void)
{
#if TARS_FOC_DRIVE_PWM
  s_cal_acc_ia = 0U;
  s_cal_acc_ib = 0U;
  s_cal_acc_ic = 0U;
  s_cal_remaining = TARS_FOC_CAL_SAMPLES;   /* armed; the ISR does the work */
#endif
}

void TarsFoc_SetSpeedRef(float rpm)
{
  s_speed_ref_rpm = rpm;
}

void TarsFoc_Enable(int enable)
{
  s_enable = (enable != 0) ? 1U : 0U;
#if TARS_FOC_DRIVE_PWM
  if (s_enable != 0U)
  {
    if ((TarsResMgr_Acquire("tim1_ch1", TARS_OWNER_FOC) != 0) ||
        (TarsResMgr_Acquire("tim1_ch2", TARS_OWNER_FOC) != 0) ||
        (TarsResMgr_Acquire("tim1_ch3", TARS_OWNER_FOC) != 0))
    {
      s_enable = 0U;
      return;
    }
    __HAL_TIM_MOE_ENABLE(&htim1);
  }
  else
  {
    __HAL_TIM_MOE_DISABLE(&htim1);
    (void)TarsResMgr_Release("tim1_ch1", TARS_OWNER_FOC);
    (void)TarsResMgr_Release("tim1_ch2", TARS_OWNER_FOC);
    (void)TarsResMgr_Release("tim1_ch3", TARS_OWNER_FOC);
  }
#endif
}

void TarsFoc_GetSnapshot(tars_foc_snapshot_t *out)
{
  uint32_t primask;

  if (out == NULL)
  {
    return;
  }

  primask = __get_PRIMASK();
  __disable_irq();
  *out = s_snap;
  if (primask == 0U)
  {
    __enable_irq();
  }
}

/* Run one controller iteration and publish telemetry. */
static void foc_run_once(float ia, float ib, float ic, float vdc)
{
  tars_foc_snapshot_t s;
  float da, db, dc, theta, speed, id, iq;
  float ref = s_speed_ref_rpm;
  float en  = (s_enable != 0U) ? 1.0f : 0.0f;

  foc_step_stm32(ia, ib, ic, vdc, ref, en,
                 &da, &db, &dc, &theta, &speed, &id, &iq);

  s.theta_est_rad = theta;
  s.speed_est_rpm = speed;
  s.speed_ref_rpm = ref;
  s.id = id;
  s.iq = iq;
  s.vdc = vdc;
  s.ia = ia;
  s.ib = ib;
  s.ic = ic;
  s.duty_a = da;
  s.duty_b = db;
  s.duty_c = dc;
  s.loop_count = s_snap.loop_count + 1U;
  s.enabled = s_enable;
  s.state = (s_enable != 0U) ? TARS_FOC_STATE_RUN : TARS_FOC_STATE_IDLE;
  s.fault_code = 0U;

  foc_store_snapshot(&s);
}

void TarsFoc_ControlLoopISR(void)
{
#if TARS_FOC_DRIVE_PWM
  uint16_t raw_ia = (uint16_t)HAL_ADCEx_InjectedGetValue(&hadc1, ADC_INJECTED_RANK_1);
  uint16_t raw_ib = (uint16_t)HAL_ADCEx_InjectedGetValue(&hadc1, ADC_INJECTED_RANK_2);
  uint16_t raw_ic = (uint16_t)HAL_ADCEx_InjectedGetValue(&hadc1, ADC_INJECTED_RANK_3);
  uint16_t raw_vdc = (uint16_t)HAL_ADCEx_InjectedGetValue(&hadc1, ADC_INJECTED_RANK_4);

  float ia;
  float ib;
  float ic;
  float vdc = (float)raw_vdc * V_COUNTS_TO_V;
  if (vdc < 6.0f)
  {
    vdc = TARS_FOC_VDC_NOMINAL;
  }

  if (s_cal_remaining != 0U)
  {
    /* Accumulate zero-current offsets (bridge is off during calibration). */
    s_cal_acc_ia += raw_ia;
    s_cal_acc_ib += raw_ib;
    s_cal_acc_ic += raw_ic;
    if (--s_cal_remaining == 0U)
    {
      s_ia_offset = (uint16_t)(s_cal_acc_ia / TARS_FOC_CAL_SAMPLES);
      s_ib_offset = (uint16_t)(s_cal_acc_ib / TARS_FOC_CAL_SAMPLES);
      s_ic_offset = (uint16_t)(s_cal_acc_ic / TARS_FOC_CAL_SAMPLES);
    }
    ia = 0.0f;
    ib = 0.0f;
    ic = 0.0f;
  }
  else
  {
    ia = ((float)raw_ia - (float)s_ia_offset) * I_COUNTS_TO_A;
    ib = ((float)raw_ib - (float)s_ib_offset) * I_COUNTS_TO_A;
    ic = ((float)raw_ic - (float)s_ic_offset) * I_COUNTS_TO_A;
  }

  foc_run_once(ia, ib, ic, vdc);

  {
    uint32_t arr = __HAL_TIM_GET_AUTORELOAD(&htim1);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, (uint32_t)(s_snap.duty_a * (float)arr));
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, (uint32_t)(s_snap.duty_b * (float)arr));
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, (uint32_t)(s_snap.duty_c * (float)arr));
  }
#endif /* TARS_FOC_DRIVE_PWM */
}

void TarsFoc_Step(void)
{
  if (s_initialized == 0U)
  {
    TarsFoc_Init();
  }

#if TARS_FOC_DRIVE_PWM
  /* The real current loop runs in TarsFoc_ControlLoopISR() at 20 kHz; nothing
   * to do at the low resource-task rate beyond letting telemetry settle. */
#else
  /* Bench mode: no hardware. Run one controller step with zero measured
   * currents so the Simulink-generated code is genuinely executed and the
   * telemetry / probe / LCD path stays live. NOTE: the controller's internal
   * timing assumes the 20 kHz PWM cadence, so estimates produced at the
   * resource-task rate are integration smoke-tests, not physical results. */
  foc_run_once(0.0f, 0.0f, 0.0f, TARS_FOC_VDC_NOMINAL);
#endif
}

#if TARS_FOC_DRIVE_PWM
/* ADC1 injected end-of-conversion (TIM1-triggered, 20 kHz) -> one FOC tick. */
void HAL_ADCEx_InjectedConvCpltCallback(ADC_HandleTypeDef *hadc)
{
  if (hadc->Instance == ADC1)
  {
    TarsFoc_ControlLoopISR();
  }
}
#endif
