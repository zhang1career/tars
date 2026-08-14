#include "tars_hall6.h"
#include "tars_res_mgr.h"
#include "tars_res_pwm.h"
#include "tars_tenant.h"
#include "tars_foc.h"
#include "main.h"
#include "tim.h"
#include "adc.h"

#define TARS_HALL6_TENANT     "hall6"

#define TARS_HALL6_HA_PORT    GPIOE
#define TARS_HALL6_HA_PIN     GPIO_PIN_2
#define TARS_HALL6_HB_PORT    GPIOE
#define TARS_HALL6_HB_PIN     GPIO_PIN_3
#define TARS_HALL6_HC_PORT    GPIOE
#define TARS_HALL6_HC_PIN     GPIO_PIN_4

#define TARS_HALL6_DEFAULT_DUTY   6U
#define TARS_HALL6_DEFAULT_RUN    7U
#define TARS_HALL6_MAX_RUN_DUTY   8U
#define TARS_HALL6_KICK_INTERVAL  600U    /* 20 kHz -> 30 ms/step */
#define TARS_HALL6_KICK_SYNC_MIN  2U
#define TARS_HALL6_KICK_MAX_STEPS 40U
#define TARS_HALL6_MOE_MIN_STEPS  2U

/* Per-phase drive: float, low-side on, PWM high-side. */
typedef enum {
  HALL6_OFF = 0,
  HALL6_LOW = 1,
  HALL6_PWM = 2
} hall6_phase_mode_t;

typedef struct {
  hall6_phase_mode_t u;
  hall6_phase_mode_t v;
  hall6_phase_mode_t w;
} hall6_step_t;

/* Standard 120° Hall -> two-phase-on (U=pwm0/Y, V=pwm1/G, W=pwm2/B). */
static const hall6_step_t s_table_cw[8] = {
  { HALL6_OFF, HALL6_OFF, HALL6_OFF }, /* 0 invalid */
  { HALL6_PWM, HALL6_OFF, HALL6_LOW }, /* 1: U+ W- */
  { HALL6_LOW, HALL6_PWM, HALL6_OFF }, /* 2: V+ U- */
  { HALL6_OFF, HALL6_PWM, HALL6_LOW }, /* 3: V+ W- */
  { HALL6_LOW, HALL6_OFF, HALL6_PWM }, /* 4: W+ V- */
  { HALL6_PWM, HALL6_LOW, HALL6_OFF }, /* 5: U+ V- */
  { HALL6_OFF, HALL6_LOW, HALL6_PWM }, /* 6: W+ U- */
  { HALL6_OFF, HALL6_OFF, HALL6_OFF }, /* 7 invalid */
};

static tars_hall6_snapshot_t s_snap;
static volatile uint8_t s_enable;
static uint8_t s_initialized;
static uint8_t s_gpio_ready;
static uint8_t s_last_hall;
static uint8_t s_last_gpio_hall;
static uint8_t s_kick_active;
static uint8_t s_kick_seq_idx;
static uint32_t s_kick_div;
static uint32_t s_kick_steps;
static uint8_t s_kick_sync;
static uint8_t s_phase_offset;
static uint8_t s_kick_duty_pct;
static uint8_t s_run_duty_pct;
static uint8_t s_moe_pending;

/* Electrical rotation order (120 deg Hall). */
static const uint8_t s_seq_cw[6] = { 5U, 1U, 3U, 2U, 6U, 4U };
static const uint8_t s_seq_ccw[6] = { 5U, 4U, 6U, 2U, 3U, 1U };

static void hall6_store(const tars_hall6_snapshot_t *src)
{
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  s_snap = *src;
  if (primask == 0U)
  {
    __enable_irq();
  }
}

static void hall6_commutate(uint8_t hall);
static void hall6_moe_release(void);

static void hall6_init_gpio(void)
{
  GPIO_InitTypeDef gpio = {0};

  if (s_gpio_ready != 0U)
  {
    return;
  }

  __HAL_RCC_GPIOE_CLK_ENABLE();
  gpio.Pin = TARS_HALL6_HA_PIN | TARS_HALL6_HB_PIN | TARS_HALL6_HC_PIN;
  gpio.Mode = GPIO_MODE_INPUT;
  gpio.Pull = GPIO_PULLUP;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOE, &gpio);
  s_gpio_ready = 1U;
}

uint8_t TarsHall6_ReadHallRaw(void)
{
  uint8_t ha;
  uint8_t hb;
  uint8_t hc;

  hall6_init_gpio();
  ha = (HAL_GPIO_ReadPin(TARS_HALL6_HA_PORT, TARS_HALL6_HA_PIN) != GPIO_PIN_RESET) ? 1U : 0U;
  hb = (HAL_GPIO_ReadPin(TARS_HALL6_HB_PORT, TARS_HALL6_HB_PIN) != GPIO_PIN_RESET) ? 1U : 0U;
  hc = (HAL_GPIO_ReadPin(TARS_HALL6_HC_PORT, TARS_HALL6_HC_PIN) != GPIO_PIN_RESET) ? 1U : 0U;
  return (uint8_t)(ha | (hb << 1) | (hc << 2));
}

static const hall6_step_t *hall6_lookup(uint8_t hall, uint8_t ccw)
{
  const hall6_step_t *st;

  if (hall >= 8U)
  {
    return &s_table_cw[0];
  }

  st = &s_table_cw[hall];
  if (ccw == 0U)
  {
    return st;
  }

  /* CCW: swap + and - on each valid step (reverse torque). */
  static hall6_step_t rev;
  rev.u = st->u;
  rev.v = st->v;
  rev.w = st->w;
  if (rev.u == HALL6_PWM) { rev.u = HALL6_LOW; }
  else if (rev.u == HALL6_LOW) { rev.u = HALL6_PWM; }
  if (rev.v == HALL6_PWM) { rev.v = HALL6_LOW; }
  else if (rev.v == HALL6_LOW) { rev.v = HALL6_PWM; }
  if (rev.w == HALL6_PWM) { rev.w = HALL6_LOW; }
  else if (rev.w == HALL6_LOW) { rev.w = HALL6_PWM; }
  return &rev;
}

static const uint8_t *hall6_seq(uint8_t ccw)
{
  /*
   * Match openloop wiring (Y=pwm0, G=pwm1, B=pwm2): host CCW uses seq_cw.
   */
  return (ccw != 0U) ? s_seq_cw : s_seq_ccw;
}

static uint8_t hall6_seq_index(const uint8_t *seq, uint8_t hall)
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

static uint8_t hall6_map_hall(uint8_t raw)
{
  const uint8_t *seq;

  if ((raw == 0U) || (raw == 7U))
  {
    return raw;
  }

  seq = hall6_seq(s_snap.direction);
  return seq[(hall6_seq_index(seq, raw) + s_phase_offset) % 6U];
}

static void hall6_kick_begin(uint8_t hall)
{
  const uint8_t *seq = hall6_seq(s_snap.direction);

  s_kick_active = 1U;
  s_kick_seq_idx = hall6_seq_index(seq, hall);
  s_kick_div = 0U;
  s_kick_steps = 0U;
  s_kick_sync = 0U;
  s_last_gpio_hall = hall;
}

static void hall6_kick_step(void)
{
  const uint8_t *seq = hall6_seq(s_snap.direction);
  tars_hall6_snapshot_t snap;

  s_kick_seq_idx = (uint8_t)((s_kick_seq_idx + 1U) % 6U);
  s_kick_steps++;
  hall6_commutate(hall6_map_hall(seq[s_kick_seq_idx]));

  snap = s_snap;
  snap.kick = 1U;
  hall6_store(&snap);

  if ((s_kick_sync >= TARS_HALL6_KICK_SYNC_MIN) ||
      (s_kick_steps >= TARS_HALL6_KICK_MAX_STEPS))
  {
    s_kick_active = 0U;
    snap.kick = 0U;
    s_snap.duty_pct = s_run_duty_pct;
    snap.duty_pct = s_run_duty_pct;
    hall6_store(&snap);
    if (s_moe_pending != 0U)
    {
      hall6_moe_release();
    }
  }
}

static void hall6_moe_release(void)
{
  uint8_t hall;

  if (s_moe_pending == 0U)
  {
    return;
  }

  s_moe_pending = 0U;
  __HAL_TIM_MOE_ENABLE(&htim1);
  hall = TarsHall6_ReadHallRaw();
  hall6_commutate(hall6_map_hall(hall));
}

static void hall6_apply_phase(TIM_TypeDef *tim, uint32_t ccer_e, uint32_t ccer_ne,
                              volatile uint32_t *ccr, hall6_phase_mode_t mode, uint32_t pulse)
{
  switch (mode)
  {
  case HALL6_OFF:
    tim->CCER &= ~(ccer_e | ccer_ne);
    *ccr = 0U;
    break;
  case HALL6_PWM:
    tim->CCER |= (ccer_e | ccer_ne);
    *ccr = pulse;
    break;
  case HALL6_LOW:
    /* Static low-side ON: disable high FET, keep complementary output active. */
    tim->CCER &= ~ccer_e;
    tim->CCER |= ccer_ne;
    *ccr = 0U;
    break;
  default:
    tim->CCER &= ~(ccer_e | ccer_ne);
    *ccr = 0U;
    break;
  }
}

static void hall6_apply_step(const hall6_step_t *st, uint32_t pulse)
{
  TIM_TypeDef *tim = htim1.Instance;

  hall6_apply_phase(tim, TIM_CCER_CC1E, TIM_CCER_CC1NE, &tim->CCR1, st->u, pulse);
  hall6_apply_phase(tim, TIM_CCER_CC2E, TIM_CCER_CC2NE, &tim->CCR2, st->v, pulse);
  hall6_apply_phase(tim, TIM_CCER_CC3E, TIM_CCER_CC3NE, &tim->CCR3, st->w, pulse);
}

static void hall6_commutate(uint8_t table_hall)
{
  const hall6_step_t *st;
  uint32_t arr;
  uint32_t pulse;
  tars_hall6_snapshot_t snap;
  const uint8_t *seq;

  if ((table_hall == 0U) || (table_hall == 7U))
  {
    snap = s_snap;
    snap.fault = 1U;
    hall6_store(&snap);
    return;
  }

  st = hall6_lookup(table_hall, 0U);
  arr = __HAL_TIM_GET_AUTORELOAD(&htim1);
  pulse = (arr * (uint32_t)s_snap.duty_pct) / 100U;
  hall6_apply_step(st, pulse);

  seq = hall6_seq(s_snap.direction);
  snap = s_snap;
  snap.fault = 0U;
  snap.step = hall6_seq_index(seq, table_hall);
  hall6_store(&snap);
  s_last_hall = table_hall;
}

void TarsHall6_Init(void)
{
  if (s_initialized != 0U)
  {
    return;
  }

  s_snap.duty_pct = TARS_HALL6_DEFAULT_DUTY;
  s_kick_duty_pct = TARS_HALL6_DEFAULT_DUTY;
  s_run_duty_pct = TARS_HALL6_DEFAULT_RUN;
  s_phase_offset = 3U;
  s_snap.phase = 3U;
  s_snap.direction = 0U;
  s_last_hall = 0xFFU;
  s_enable = 0U;
  s_initialized = 1U;
}

void TarsHall6_BootHw(void)
{
  if (s_initialized == 0U)
  {
    TarsHall6_Init();
  }
  hall6_init_gpio();
}

void TarsHall6_SetDutyPct(uint8_t pct)
{
  if (pct > TARS_HALL6_MAX_RUN_DUTY)
  {
    pct = TARS_HALL6_MAX_RUN_DUTY;
  }
  s_run_duty_pct = pct;
  if (s_kick_active == 0U)
  {
    s_snap.duty_pct = pct;
  }
}

void TarsHall6_SetKickDutyPct(uint8_t pct)
{
  if (pct > TARS_HALL6_MAX_RUN_DUTY)
  {
    pct = TARS_HALL6_MAX_RUN_DUTY;
  }
  s_kick_duty_pct = pct;
  if (s_kick_active != 0U)
  {
    s_snap.duty_pct = pct;
  }
}

void TarsHall6_SetPhaseOffset(uint8_t offset)
{
  s_phase_offset = (uint8_t)(offset % 6U);
  s_snap.phase = s_phase_offset;
}

void TarsHall6_SetDirection(int ccw)
{
  s_snap.direction = (ccw != 0) ? 1U : 0U;
}

int TarsHall6_IsEnabled(void)
{
  return (s_enable != 0U) ? 1 : 0;
}

void TarsHall6_GetSnapshot(tars_hall6_snapshot_t *out)
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

static int hall6_start_tim1_pwm(void)
{
  TarsTim1_EnsurePwmStarted();
  return 1;
}

static volatile int s_last_enable_err;

int TarsHall6_LastEnableError(void)
{
  return s_last_enable_err;
}

int TarsHall6_Enable(int enable)
{
  tars_hall6_snapshot_t snap;

  if (enable != 0)
  {
    s_last_enable_err = 0;

    if (TarsFoc_IsEnabled() != 0)
    {
      s_last_enable_err = 1;
      return 0;
    }

    if (TarsResMgr_TimDomainAcquire("tim1", TARS_HALL6_TENANT) != 0)
    {
      s_last_enable_err = 2;
      return 0;
    }

    if ((TarsResMgr_Acquire("pwm0") != 0) ||
        (TarsResMgr_Acquire("pwm1") != 0) ||
        (TarsResMgr_Acquire("pwm2") != 0))
    {
      s_last_enable_err = 3;
      TarsResMgr_TimDomainRelease("tim1", TARS_HALL6_TENANT);
      return 0;
    }

    if (hall6_start_tim1_pwm() == 0)
    {
      s_last_enable_err = 4;
      (void)TarsResMgr_Release("pwm0");
      (void)TarsResMgr_Release("pwm1");
      (void)TarsResMgr_Release("pwm2");
      TarsResMgr_TimDomainRelease("tim1", TARS_HALL6_TENANT);
      return 0;
    }

    s_moe_pending = 1U;
    TarsTim1_HardwareSafe();

    s_enable = 1U;
    s_last_hall = 0xFFU;
    s_last_gpio_hall = 0xFFU;
    snap = s_snap;
    snap.enabled = 1U;
    snap.loop_count = 0U;
    snap.hall_changes = 0U;
    snap.kick = 1U;
    s_snap.duty_pct = s_kick_duty_pct;
    snap.duty_pct = s_kick_duty_pct;
    hall6_store(&snap);
    {
      uint8_t hall = TarsHall6_ReadHallRaw();
      uint8_t mapped = hall6_map_hall(hall);
      snap.hall_raw = hall;
      hall6_store(&snap);
      hall6_kick_begin(hall);
      hall6_commutate(mapped);
    }
    return 1;
  }

  s_enable = 0U;
  s_kick_active = 0U;
  s_moe_pending = 0U;
  (void)TarsResPwm_Tim1ForceSafe();
  (void)TarsResMgr_Release("pwm0");
  (void)TarsResMgr_Release("pwm1");
  (void)TarsResMgr_Release("pwm2");
  TarsResMgr_TimDomainRelease("tim1", TARS_HALL6_TENANT);

  snap = s_snap;
  snap.enabled = 0U;
  hall6_store(&snap);
  return 1;
}

void TarsHall6_ControlLoopISR(void)
{
  tars_hall6_snapshot_t snap;
  uint8_t hall;
  uint8_t hall_edge = 0U;

  if (s_enable == 0U)
  {
    return;
  }

  hall = TarsHall6_ReadHallRaw();

  if (hall != s_last_gpio_hall)
  {
    tars_hall6_snapshot_t edge = s_snap;
    edge.hall_changes++;
    hall6_store(&edge);

    if (s_kick_active != 0U)
    {
      s_kick_sync++;
    }
    s_last_gpio_hall = hall;
    hall_edge = 1U;
  }

  if (s_kick_active != 0U)
  {
    s_kick_div++;
    if (s_kick_div >= TARS_HALL6_KICK_INTERVAL)
    {
      s_kick_div = 0U;
      hall6_kick_step();
    }

    if ((s_moe_pending != 0U) &&
        ((s_kick_steps >= TARS_HALL6_MOE_MIN_STEPS) || (s_kick_sync >= 1U)))
    {
      hall6_moe_release();
    }
  }
  else if (hall_edge != 0U)
  {
    if (s_moe_pending != 0U)
    {
      hall6_moe_release();
    }
    hall6_commutate(hall6_map_hall(hall));
  }

  snap = s_snap;
  snap.hall_raw = hall;
  snap.kick = s_kick_active;
  snap.phase = s_phase_offset;
  snap.loop_count++;
  hall6_store(&snap);
}
