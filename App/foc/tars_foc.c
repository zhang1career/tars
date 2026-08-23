#include "tars_foc.h"
#include "tars_hall6.h"
#include "tars_openloop.h"
#include "tars_res_mgr.h"
#include "tars_res_pwm.h"
#include "foc_step_stm32.h"
#include "foc_step_stm32_initialize.h"
#include "foc_params.h"
#include "main.h"
#include "tim.h"

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
/* Bench bring-up: 12 V supply, PC5 divider often absent — override model 24 V. */
#define TARS_FOC_VDC_NOMINAL 12.0f

/* Bench wiring (Y=pwm0, G=pwm1, B=pwm2): swap G/B duties to match hall6 frame. */
#define TARS_FOC_SWAP_BC  1

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
static uint8_t             s_hall_assist;
static uint8_t             s_hall_phase;
static uint8_t             s_phase_remap;
static uint8_t             s_hall_last_raw;
static float               s_hall_theta_track;
static float               s_hall_w_filt;
static uint32_t            s_hall_isr_count;
static uint32_t            s_hall_edge_isr;
static uint32_t            s_run_ticks;

#define TARS_FOC_HOLD_TICKS      4000U   /* 200 ms: iq clamped, speed ramp held */
#define TARS_FOC_STARTUP_TICKS  12000U   /* 600 ms speed ramp after hold */
#define TARS_FOC_IQ_RAMP_TICKS   4000U   /* 200 ms iq limit ramp after hold */
#define TARS_FOC_IQ_LIM_HALL     0.50f

#define TARS_FOC_TS_S  5.0e-5f

volatile uint8_t g_tars_foc_hall_en;
volatile float   g_tars_foc_hall_theta;
volatile float   g_tars_foc_hall_w_est;
volatile float   g_tars_foc_iq_lim = 1.5f;

static const uint8_t s_hall_seq_cw[6]  = { 5U, 1U, 3U, 2U, 6U, 4U };
static const uint8_t s_hall_seq_ccw[6] = { 5U, 4U, 6U, 2U, 3U, 1U };

static uint8_t hall_seq_index(const uint8_t *seq, uint8_t hall)
{
  uint8_t i;

  for (i = 0U; i < 6U; i++)
  {
    if (seq[i] == hall)
    {
      return i;
    }
  }
  return 0U;
}

static float hall_theta_rad(uint8_t hall, uint8_t ccw)
{
  const uint8_t *seq;
  uint8_t idx;

  if ((hall < 1U) || (hall > 6U))
  {
    return s_hall_theta_track;
  }

  seq = (ccw != 0U) ? s_hall_seq_cw : s_hall_seq_ccw;
  idx = hall_seq_index(seq, hall);
  idx = (uint8_t)((idx + s_hall_phase) % 6U);
  return ((float)idx + 0.5f) * 1.04719755f;
}

static void remap_abc(float a, float b, float c, uint8_t map, float *o0, float *o1, float *o2)
{
  const float in[3] = { a, b, c };
  static const uint8_t tbl[6][3] = {
    { 0U, 1U, 2U }, { 0U, 2U, 1U }, { 1U, 0U, 2U },
    { 1U, 2U, 0U }, { 2U, 0U, 1U }, { 2U, 1U, 0U }
  };
  uint8_t m = (uint8_t)(map % 6U);

  *o0 = in[tbl[m][0]];
  *o1 = in[tbl[m][1]];
  *o2 = in[tbl[m][2]];
}

static float wrap_pi_f(float x)
{
  while (x > 3.14159265f)
  {
    x -= 6.2831853f;
  }
  while (x < -3.14159265f)
  {
    x += 6.2831853f;
  }
  return x;
}

static float foc_speed_ref_effective(void)
{
  /* Hall-assist bench frame: invert torque command vs shell speed sign. */
  if (s_hall_assist != 0U)
  {
    return -s_speed_ref_rpm;
  }
  return s_speed_ref_rpm;
}

static float foc_startup_ramp(void)
{
  uint32_t t;

  if (s_hall_assist == 0U)
  {
    return 1.0f;
  }
  if (s_run_ticks < TARS_FOC_HOLD_TICKS)
  {
    return 0.0f;
  }

  t = s_run_ticks - TARS_FOC_HOLD_TICKS;
  if (t >= TARS_FOC_STARTUP_TICKS)
  {
    return 1.0f;
  }

  return (float)t / (float)TARS_FOC_STARTUP_TICKS;
}

static void hall_refresh(void);

static void foc_update_iq_limit(void)
{
  uint32_t t;

  if (s_hall_assist == 0U)
  {
    g_tars_foc_iq_lim = 1.5f;
    return;
  }
  if (s_run_ticks < TARS_FOC_HOLD_TICKS)
  {
    g_tars_foc_iq_lim = 0.0f;
    return;
  }

  t = s_run_ticks - TARS_FOC_HOLD_TICKS;
  if (t >= TARS_FOC_IQ_RAMP_TICKS)
  {
    g_tars_foc_iq_lim = TARS_FOC_IQ_LIM_HALL;
    return;
  }

  g_tars_foc_iq_lim = 0.10f + (0.40f * ((float)t / (float)TARS_FOC_IQ_RAMP_TICKS));
}

static void hall_refresh(void)
{
  uint8_t hall;
  uint8_t ccw;
  float theta_center;

  if (s_hall_assist == 0U)
  {
    g_tars_foc_hall_en = 0U;
    return;
  }

  hall = TarsHall6_ReadHallRaw();
  ccw = (s_speed_ref_rpm < 0.0f) ? 1U : 0U;

  if ((hall >= 1U) && (hall <= 6U))
  {
    theta_center = hall_theta_rad(hall, ccw);
    /* Steady-state FOC frame (+180 deg vs hall6 commutation table). */
    g_tars_foc_hall_theta = wrap_pi_f(theta_center + 3.14159265f);
    s_hall_last_raw = hall;
  }

  {
    float ref = foc_speed_ref_effective() * foc_startup_ramp();
    g_tars_foc_hall_w_est = (6.2831853f * (float)FOC_PARAM_POLE_PAIRS / 60.0f) * ref;
  }
  g_tars_foc_hall_en = 1U;
}

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

  s_hall_assist = 1U;
  s_hall_phase = 3U;
  s_phase_remap = 0U;
  s_hall_last_raw = 0U;
  s_hall_theta_track = 0.0f;
  s_hall_w_filt = 0.0f;
  s_hall_isr_count = 0U;
  s_hall_edge_isr = 0U;
  g_tars_foc_hall_en = 0U;
  g_tars_foc_hall_theta = 0.0f;
  g_tars_foc_hall_w_est = 0.0f;

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
   * control loop. Run the timer for ADC triggers only — gate outputs (CCER/MOE)
   * stay off until an explicit motor enable. */
  (void)HAL_ADCEx_InjectedStart_IT(&hadc1);
  TarsTim1_StartBaseForAdc();

  TarsFoc_Calibrate();
#endif
}

void TarsFoc_Calibrate(void)
{
#if TARS_FOC_DRIVE_PWM
  /* TRGO only fires while TIM1 is updating; PWM_Stop in disable can clear CEN.
   * Re-arm complementary PWM with MOE off so ADC samples zero current. */
  TarsTim1_EnsurePwmStarted();
  __HAL_TIM_MOE_DISABLE(&htim1);
  __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 0U);
  __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, 0U);
  __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, 0U);
  s_cal_acc_ia = 0U;
  s_cal_acc_ib = 0U;
  s_cal_acc_ic = 0U;
  s_cal_remaining = TARS_FOC_CAL_SAMPLES;
#endif
}

void TarsFoc_SamplePhaseCurrents(float *ia, float *ib, float *ic)
{
#if TARS_FOC_DRIVE_PWM
  uint16_t raw_ia;
  uint16_t raw_ib;
  uint16_t raw_ic;

  if ((ia == 0) || (ib == 0) || (ic == 0))
  {
    return;
  }
  if (s_cal_remaining != 0U)
  {
    *ia = 0.0f;
    *ib = 0.0f;
    *ic = 0.0f;
    return;
  }

  raw_ia = (uint16_t)HAL_ADCEx_InjectedGetValue(&hadc1, ADC_INJECTED_RANK_1);
  raw_ib = (uint16_t)HAL_ADCEx_InjectedGetValue(&hadc1, ADC_INJECTED_RANK_2);
  raw_ic = (uint16_t)HAL_ADCEx_InjectedGetValue(&hadc1, ADC_INJECTED_RANK_3);
  *ia = ((float)raw_ia - (float)s_ia_offset) * I_COUNTS_TO_A;
  *ib = ((float)raw_ib - (float)s_ib_offset) * I_COUNTS_TO_A;
  *ic = ((float)raw_ic - (float)s_ic_offset) * I_COUNTS_TO_A;
#else
  if (ia != 0) { *ia = 0.0f; }
  if (ib != 0) { *ib = 0.0f; }
  if (ic != 0) { *ic = 0.0f; }
#endif
}

void TarsFoc_GetCalOffsets(uint16_t *ia, uint16_t *ib, uint16_t *ic, uint16_t *cal_left)
{
#if TARS_FOC_DRIVE_PWM
  if (ia != 0) { *ia = s_ia_offset; }
  if (ib != 0) { *ib = s_ib_offset; }
  if (ic != 0) { *ic = s_ic_offset; }
  if (cal_left != 0) { *cal_left = s_cal_remaining; }
#else
  if (ia != 0) { *ia = 0U; }
  if (ib != 0) { *ib = 0U; }
  if (ic != 0) { *ic = 0U; }
  if (cal_left != 0) { *cal_left = 0U; }
#endif
}

void TarsFoc_SetSpeedRef(float rpm)
{
  s_speed_ref_rpm = rpm;
}

void TarsFoc_SetHallAssist(int enable)
{
  s_hall_assist = (enable != 0) ? 1U : 0U;
  if (s_hall_assist == 0U)
  {
    g_tars_foc_hall_en = 0U;
  }
}

void TarsFoc_SetHallPhase(uint8_t offset)
{
  s_hall_phase = (uint8_t)(offset % 6U);
}

void TarsFoc_SetPhaseRemap(uint8_t map)
{
  s_phase_remap = (uint8_t)(map % 6U);
}

uint8_t TarsFoc_GetPhaseRemap(void)
{
  return s_phase_remap;
}

int TarsFoc_HallAssistEnabled(void)
{
  return (s_hall_assist != 0U) ? 1 : 0;
}

int TarsFoc_Enable(int enable)
{
  s_enable = (enable != 0) ? 1U : 0U;
#if TARS_FOC_DRIVE_PWM
  if (s_enable != 0U)
  {
    foc_step_stm32_init();
    s_hall_last_raw = 0U;
    s_hall_theta_track = 0.0f;
    s_hall_w_filt = 0.0f;
    s_hall_isr_count = 0U;
    s_hall_edge_isr = 0U;
    s_run_ticks = 0U;
    g_tars_foc_iq_lim = 0.0f;
    hall_refresh();
    if (s_hall_assist != 0U)
    {
      foc_step_stm32_hall_bootstrap(g_tars_foc_hall_theta, 0.0f);
    }

    /* TIM1 must not be held by shell PWM (peer function) before we commutate. */
    if (TarsResMgr_TimDomainAcquire("tim1", TARS_TENANT_FOC) != 0)
    {
      s_enable = 0U;
      return 0;
    }

    if ((TarsResMgr_Acquire("pwm0") != 0) ||
        (TarsResMgr_Acquire("pwm1") != 0) ||
        (TarsResMgr_Acquire("pwm2") != 0))
    {
      s_enable = 0U;
      TarsResMgr_TimDomainRelease("tim1", TARS_TENANT_FOC);
      return 0;
    }

    /* Re-init TIM1 for FOC SVPWM (hall6 may have left CCER in 6-step mode). */
    TarsTim1_ArmFocPwm();
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 0U);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, 0U);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, 0U);
    __HAL_TIM_MOE_ENABLE(&htim1);
  }
  else
  {
    (void)TarsResPwm_Tim1ForceSafe();
    (void)TarsResMgr_Release("pwm0");
    (void)TarsResMgr_Release("pwm1");
    (void)TarsResMgr_Release("pwm2");
    TarsResMgr_TimDomainRelease("tim1", TARS_TENANT_FOC);
  }
#endif
  return 1;
}

int TarsFoc_IsEnabled(void)
{
  return (s_enable != 0U) ? 1 : 0;
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
  float ref = foc_speed_ref_effective() * foc_startup_ramp();
  float en  = (s_enable != 0U) ? 1.0f : 0.0f;

  foc_step_stm32(ia, ib, ic, vdc, ref, en,
                 &da, &db, &dc, &theta, &speed, &id, &iq);

  s.theta_est_rad = theta;
  s.speed_est_rpm = speed;
  s.speed_ref_rpm = s_speed_ref_rpm;
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
  uint16_t raw_ia;
  uint16_t raw_ib;
  uint16_t raw_ic;
  float ia;
  float ib;
  float ic;
  float vdc = TARS_FOC_VDC_NOMINAL;

  /* RCR=0 center-aligned: TRGO at trough and peak. Keep trough (DIR=0). */
  if ((htim1.Instance->CR1 & TIM_CR1_DIR) != 0U)
  {
    return;
  }

  raw_ia = (uint16_t)HAL_ADCEx_InjectedGetValue(&hadc1, ADC_INJECTED_RANK_1);
  raw_ib = (uint16_t)HAL_ADCEx_InjectedGetValue(&hadc1, ADC_INJECTED_RANK_2);
  raw_ic = (uint16_t)HAL_ADCEx_InjectedGetValue(&hadc1, ADC_INJECTED_RANK_3);

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
#if TARS_FOC_SWAP_BC
    if (s_hall_assist == 0U)
    {
      float tmp = ib;
      ib = ic;
      ic = tmp;
    }
#endif
    if (s_hall_assist != 0U)
    {
      remap_abc(ia, ib, ic, s_phase_remap, &ia, &ib, &ic);
    }
  }

  if ((s_enable != 0U) && (s_hall_assist != 0U) && (s_run_ticks == TARS_FOC_HOLD_TICKS))
  {
    hall_refresh();
    foc_step_stm32_hall_bootstrap(g_tars_foc_hall_theta, 0.0f);
  }
  else
  {
    hall_refresh();
  }
  foc_update_iq_limit();
  foc_run_once(ia, ib, ic, vdc);

  if (s_enable != 0U)
  {
    s_run_ticks++;
  }

  /* Only drive the compare registers when FOC actually owns the TIM1 physical
   * domain. If a peer function (shell PWM) has taken TIM1, stay silent so it
   * is not fighting this loop for the CCRs. Lock-free volatile read. */
  if (TarsResMgr_Tim1HeldByFoc() != 0)
  {
    uint32_t arr = __HAL_TIM_GET_AUTORELOAD(&htim1);
    float da = s_snap.duty_a;
    float db = s_snap.duty_b;
    float dc = s_snap.duty_c;
#if TARS_FOC_SWAP_BC
    if (s_hall_assist == 0U)
    {
      db = s_snap.duty_c;
      dc = s_snap.duty_b;
    }
#endif
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, (uint32_t)(da * (float)arr));
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, (uint32_t)(db * (float)arr));
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, (uint32_t)(dc * (float)arr));
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
    if (TarsOpenloop_IsEnabled() != 0)
    {
      TarsOpenloop_ControlLoopISR();
      return;
    }
    if (TarsHall6_IsEnabled() != 0)
    {
      TarsHall6_ControlLoopISR();
      return;
    }
    TarsFoc_ControlLoopISR();
  }
}
#endif
