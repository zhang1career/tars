#include "tars_res_pwm.h"
#include "tars_tenant.h"
#include "tars_res_mgr.h"
#include "tars_mcu_pinmap.h"
#include "tars_foc.h"
#include "main.h"
#include "tim.h"
#include <stdio.h>
#include <string.h>

#define TARS_PWM_TIM_SLOTS   4U
#define TARS_PWM_CH_SLOTS    16U
#define TARS_PWM_DEFAULT_HZ  1000U

#define TARS_PWM_LINK_MASTER    "pwm0"
#define TARS_PWM_LINK_FOLLOWER  "pwm3"

typedef struct {
  uint8_t enabled;
  int32_t offset_ticks;
} tars_pwm_link_t;

static tars_pwm_link_t s_link;

typedef struct {
  TIM_TypeDef *instance;
  TIM_HandleTypeDef handle;
  uint32_t freq_hz;
  uint8_t init_done;
  uint8_t ref_count;
} tars_pwm_tim_t;

typedef struct {
  const tars_mcu_pwm_entry_t *map;
  uint8_t duty_pct;
  uint8_t boot_enable;
  uint8_t running;
  uint8_t polarity_low;
  uint8_t polarity_explicit;
  int8_t tim_slot;
} tars_pwm_ch_t;

static tars_pwm_tim_t s_tim_pool[TARS_PWM_TIM_SLOTS];
static tars_pwm_ch_t s_ch_pool[TARS_PWM_CH_SLOTS];
static uint32_t s_ch_count;

static void pwm_enable_port_clock(GPIO_TypeDef *port)
{
  if (port == GPIOA) { __HAL_RCC_GPIOA_CLK_ENABLE(); }
  else if (port == GPIOB) { __HAL_RCC_GPIOB_CLK_ENABLE(); }
  else if (port == GPIOC) { __HAL_RCC_GPIOC_CLK_ENABLE(); }
  else if (port == GPIOD) { __HAL_RCC_GPIOD_CLK_ENABLE(); }
  else if (port == GPIOE) { __HAL_RCC_GPIOE_CLK_ENABLE(); }
  else if (port == GPIOF) { __HAL_RCC_GPIOF_CLK_ENABLE(); }
  else if (port == GPIOG) { __HAL_RCC_GPIOG_CLK_ENABLE(); }
}

static void pwm_enable_tim_clock(TIM_TypeDef *tim)
{
  if (tim == TIM1) { __HAL_RCC_TIM1_CLK_ENABLE(); }
  else if (tim == TIM2) { __HAL_RCC_TIM2_CLK_ENABLE(); }
  else if (tim == TIM3) { __HAL_RCC_TIM3_CLK_ENABLE(); }
  else if (tim == TIM4) { __HAL_RCC_TIM4_CLK_ENABLE(); }
  else if (tim == TIM5) { __HAL_RCC_TIM5_CLK_ENABLE(); }
  else if (tim == TIM8) { __HAL_RCC_TIM8_CLK_ENABLE(); }
  else if (tim == TIM9) { __HAL_RCC_TIM9_CLK_ENABLE(); }
  else if (tim == TIM10) { __HAL_RCC_TIM10_CLK_ENABLE(); }
  else if (tim == TIM11) { __HAL_RCC_TIM11_CLK_ENABLE(); }
  else if (tim == TIM12) { __HAL_RCC_TIM12_CLK_ENABLE(); }
  else if (tim == TIM13) { __HAL_RCC_TIM13_CLK_ENABLE(); }
  else if (tim == TIM14) { __HAL_RCC_TIM14_CLK_ENABLE(); }
}

static uint32_t pwm_tim_clk_hz(TIM_TypeDef *tim)
{
  uint32_t pclk;
  uint32_t ppre;

  if ((tim == TIM1) || (tim == TIM9) || (tim == TIM10) || (tim == TIM12))
  {
    return 72000000U;
  }

  if ((tim == TIM8) || (tim == TIM10) ||
      (tim == TIM11))
  {
    pclk = HAL_RCC_GetPCLK2Freq();
    ppre = (RCC->CFGR & RCC_CFGR_PPRE2) >> RCC_CFGR_PPRE2_Pos;
  }
  else
  {
    pclk = HAL_RCC_GetPCLK1Freq();
    ppre = (RCC->CFGR & RCC_CFGR_PPRE1) >> RCC_CFGR_PPRE1_Pos;
  }

  if (ppre != 0U)
  {
    pclk *= 2U;
  }

  /* Sanity: on this board SYSCLK is 72 MHz and TIM9 sits on APB2. */
  if (pclk < 32000000U)
  {
    pclk = 72000000U;
  }

  return pclk;
}

static int pwm_pin_index(uint16_t hal_pin)
{
  uint32_t i;

  for (i = 0U; i < 16U; i++)
  {
    if (hal_pin == (uint16_t)(1U << i))
    {
      return (int)i;
    }
  }

  return -1;
}

static void pwm_force_update(TIM_HandleTypeDef *htim)
{
  if (htim != NULL)
  {
    htim->Instance->EGR = TIM_EGR_UG;
  }
}

static void pwm_disable_oc_preload(TIM_TypeDef *tim, uint32_t channel)
{
  if (tim == NULL)
  {
    return;
  }

  if (channel == TIM_CHANNEL_1)
  {
    tim->CCMR1 &= ~TIM_CCMR1_OC1PE;
  }
  else if (channel == TIM_CHANNEL_2)
  {
    tim->CCMR1 &= ~TIM_CCMR1_OC2PE;
  }
  else if (channel == TIM_CHANNEL_3)
  {
    tim->CCMR2 &= ~TIM_CCMR2_OC3PE;
  }
  else if (channel == TIM_CHANNEL_4)
  {
    tim->CCMR2 &= ~TIM_CCMR2_OC4PE;
  }
}

static uint32_t pwm_pulse_from_duty(TIM_HandleTypeDef *htim, uint8_t duty_pct)
{
  uint32_t arr = __HAL_TIM_GET_AUTORELOAD(htim);
  uint32_t pulse = (((arr + 1U) * (uint32_t)duty_pct) / 100U);

  if (pulse > arr)
  {
    pulse = arr;
  }

  return pulse;
}

static void pwm_apply_compare(TIM_HandleTypeDef *htim, uint32_t channel, uint32_t pulse)
{
  __HAL_TIM_SET_COMPARE(htim, channel, pulse);
  pwm_disable_oc_preload(htim->Instance, channel);
  pwm_force_update(htim);
}

static int pwm_find_tim_slot(TIM_TypeDef *tim, int create)
{
  uint32_t i;

  for (i = 0U; i < TARS_PWM_TIM_SLOTS; i++)
  {
    if ((s_tim_pool[i].init_done != 0U) && (s_tim_pool[i].instance == tim))
    {
      return (int)i;
    }
  }

  if (create == 0)
  {
    return -1;
  }

  for (i = 0U; i < TARS_PWM_TIM_SLOTS; i++)
  {
    if (s_tim_pool[i].init_done == 0U)
    {
      s_tim_pool[i].instance = tim;
      s_tim_pool[i].freq_hz = TARS_PWM_DEFAULT_HZ;
      s_tim_pool[i].ref_count = 0U;
      s_tim_pool[i].init_done = 1U;
      return (int)i;
    }
  }

  return -1;
}

static int pwm_find_ch_slot(const char *channel, int create)
{
  uint32_t i;

  for (i = 0U; i < s_ch_count; i++)
  {
    if ((s_ch_pool[i].map != NULL) &&
        (strcmp(s_ch_pool[i].map->channel, channel) == 0))
    {
      return (int)i;
    }
  }

  if (create == 0)
  {
    return -1;
  }

  if (s_ch_count >= TARS_PWM_CH_SLOTS)
  {
    return -1;
  }

  i = s_ch_count++;
  s_ch_pool[i].map = NULL;
  s_ch_pool[i].duty_pct = 0U;
  s_ch_pool[i].boot_enable = 0U;
  s_ch_pool[i].running = 0U;
  s_ch_pool[i].polarity_low = 0U;
  s_ch_pool[i].polarity_explicit = 0U;
  s_ch_pool[i].tim_slot = -1;
  return (int)i;
}

static TIM_HandleTypeDef *pwm_tim_handle(const tars_mcu_pwm_entry_t *map)
{
  if (map->tim == TIM1)
  {
    return &htim1;
  }

  if (map->tim == TIM9)
  {
    return &htim9;
  }

  if (map->tim == TIM10)
  {
    return &htim10;
  }

  if (map->tim == TIM12)
  {
    return &htim12;
  }

  {
    int slot = pwm_find_tim_slot(map->tim, 0);
    if (slot < 0)
    {
      return NULL;
    }
    return &s_tim_pool[(uint32_t)slot].handle;
  }
}

static uint8_t pwm_tim_prefers_center_aligned(TIM_TypeDef *tim)
{
  return (tim == TIM3) ? 1U : 0U;
}

static int pwm_apply_tim_timing(TIM_HandleTypeDef *htim, uint32_t freq_hz)
{
  uint32_t arr;
  uint32_t clk_hz;

  if ((htim == NULL) || (freq_hz == 0U))
  {
    return -1;
  }

  clk_hz = pwm_tim_clk_hz(htim->Instance);

  /* TIM1 is owned by FOC init (center-aligned ~20 kHz). Shell PWM only
   * adjusts compare; do not rewrite ARR/prescaler. */
  if (htim->Instance == TIM1)
  {
    return 0;
  }

  if (pwm_tim_prefers_center_aligned(htim->Instance) != 0U)
  {
    /* Center-aligned: F = CLK / (2*(ARR)). Match TIM1 FOC timing. */
    arr = clk_hz / (2U * freq_hz);
    if (arr == 0U)
    {
      arr = 1U;
    }
  }
  else
  {
    /* Edge-aligned: F = CLK / ((PSC+1)*(ARR+1)). Hold PSC=0 for simplicity. */
    arr = (clk_hz / freq_hz);
    if (arr == 0U)
    {
      arr = 1U;
    }
    arr--;
  }

  __HAL_TIM_SET_PRESCALER(htim, 0U);
  __HAL_TIM_SET_AUTORELOAD(htim, arr);
  pwm_force_update(htim);
  return 0;
}

static int pwm_init_tim_instance(const tars_mcu_pwm_entry_t *map, uint32_t freq_hz)
{
  int slot;
  TIM_HandleTypeDef *htim;

  if (map->tim == TIM1)
  {
    htim = &htim1;
    slot = pwm_find_tim_slot(TIM1, 1);
    if (slot >= 0)
    {
      s_tim_pool[(uint32_t)slot].freq_hz = freq_hz;
    }
    return 0;
  }
  else if (map->tim == TIM9)
  {
    htim = &htim9;
    slot = pwm_find_tim_slot(TIM9, 1);
    if (slot < 0)
    {
      return -1;
    }
    s_tim_pool[(uint32_t)slot].freq_hz = freq_hz;
    (void)pwm_apply_tim_timing(htim, freq_hz);
    return 0;
  }
  else if (map->tim == TIM10)
  {
    htim = &htim10;
    slot = pwm_find_tim_slot(TIM10, 1);
    if (slot < 0)
    {
      return -1;
    }
    s_tim_pool[(uint32_t)slot].freq_hz = freq_hz;
    (void)pwm_apply_tim_timing(htim, freq_hz);
    return 0;
  }
  else if (map->tim == TIM12)
  {
    htim = &htim12;
    slot = pwm_find_tim_slot(TIM12, 1);
    if (slot < 0)
    {
      return -1;
    }
    s_tim_pool[(uint32_t)slot].freq_hz = freq_hz;
    (void)pwm_apply_tim_timing(htim, freq_hz);
    return 0;
  }
  else
  {
    tars_pwm_tim_t *rt;

    slot = pwm_find_tim_slot(map->tim, 1);
    if (slot < 0)
    {
      return -1;
    }

    rt = &s_tim_pool[(uint32_t)slot];
    htim = &rt->handle;
    htim->Instance = map->tim;
    pwm_enable_tim_clock(map->tim);

    htim->Init.Prescaler = 0U;
    htim->Init.CounterMode = (pwm_tim_prefers_center_aligned(map->tim) != 0U)
        ? TIM_COUNTERMODE_CENTERALIGNED1
        : TIM_COUNTERMODE_UP;
    htim->Init.Period = 1000U;
    htim->Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim->Init.RepetitionCounter = 0U;
    htim->Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

    if (HAL_TIM_PWM_Init(htim) != HAL_OK)
    {
      return -1;
    }

    rt->freq_hz = freq_hz;
    (void)pwm_apply_tim_timing(htim, freq_hz);
  }

  if (slot >= 0)
  {
    s_tim_pool[(uint32_t)slot].freq_hz = freq_hz;
  }

  return 0;
}

static int pwm_config_pin_af(const tars_mcu_pwm_entry_t *map)
{
  GPIO_InitTypeDef gpio = {0};

  pwm_enable_port_clock(map->port);
  gpio.Pin = map->hal_pin;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio.Alternate = map->gpio_af;
  HAL_GPIO_Init(map->port, &gpio);
  return 0;
}

static uint32_t pwm_map_default_freq_hz(const tars_mcu_pwm_entry_t *map)
{
  if ((map != NULL) && (map->default_freq_hz != 0U))
  {
    return map->default_freq_hz;
  }

  return TARS_PWM_DEFAULT_HZ;
}

static void pwm_ch_sync_defaults(tars_pwm_ch_t *ch, const tars_mcu_pwm_entry_t *map)
{
  if ((ch == NULL) || (map == NULL))
  {
    return;
  }

  if (ch->polarity_explicit == 0U)
  {
    ch->polarity_low = map->default_polarity_low;
  }
}

static uint8_t pwm_ch_polarity_low(const tars_mcu_pwm_entry_t *map, int ch_slot)
{
  if ((ch_slot >= 0) && (s_ch_pool[(uint32_t)ch_slot].polarity_explicit != 0U))
  {
    return s_ch_pool[(uint32_t)ch_slot].polarity_low;
  }

  if (map != NULL)
  {
    return map->default_polarity_low;
  }

  return 0U;
}

static uint32_t pwm_oc_polarity_hal(uint8_t polarity_low)
{
  return (polarity_low != 0U) ? TIM_OCPOLARITY_LOW : TIM_OCPOLARITY_HIGH;
}

static const char *pwm_polarity_name(uint8_t polarity_low)
{
  return (polarity_low != 0U) ? "low" : "high";
}

static int pwm_configure_channel(const tars_mcu_pwm_entry_t *map, uint8_t duty_pct)
{
  TIM_HandleTypeDef *htim = pwm_tim_handle(map);
  TIM_OC_InitTypeDef oc = {0};
  uint32_t pulse;
  uint32_t arr;
  int slot;
  int ch_slot;
  uint8_t polarity_low;

  if (htim == NULL)
  {
    if (pwm_init_tim_instance(map, pwm_map_default_freq_hz(map)) != 0)
    {
      return -1;
    }
    htim = pwm_tim_handle(map);
  }

  if (htim == NULL)
  {
    return -1;
  }

  ch_slot = pwm_find_ch_slot(map->channel, 0);
  polarity_low = pwm_ch_polarity_low(map, ch_slot);

  slot = pwm_find_tim_slot(map->tim, 0);
  if (slot >= 0)
  {
    (void)pwm_apply_tim_timing(htim, s_tim_pool[(uint32_t)slot].freq_hz);
  }

  arr = __HAL_TIM_GET_AUTORELOAD(htim);
  pulse = (((arr + 1U) * (uint32_t)duty_pct) / 100U);
  if (pulse > arr)
  {
    pulse = arr;
  }

  oc.OCMode = TIM_OCMODE_PWM1;
  oc.Pulse = pulse;
  oc.OCPolarity = pwm_oc_polarity_hal(polarity_low);
  oc.OCFastMode = TIM_OCFAST_DISABLE;

  if (HAL_TIM_PWM_ConfigChannel(htim, &oc, map->hal_channel) != HAL_OK)
  {
    return -1;
  }

  pwm_disable_oc_preload(htim->Instance, map->hal_channel);
  pwm_apply_compare(htim, map->hal_channel, pulse);
  (void)pwm_config_pin_af(map);

  return 0;
}

/* TIM1 can be taken by shell PWM only while FOC is idle (bridge not
 * commutating). A running controller holds the timer exclusively. */
static int pwm_foc_tim_active(void)
{
  return TarsFoc_IsEnabled();
}

static int32_t pwm_link_norm_offset(int32_t off, uint32_t period)
{
  int32_t n;

  if (period == 0U)
  {
    return 0;
  }

  n = (int32_t)period;
  off %= n;
  if (off < 0)
  {
    off += n;
  }
  return off;
}

static uint32_t pwm_link_period_ticks(TIM_TypeDef *tim, uint8_t center_aligned)
{
  uint32_t arr = tim->ARR;

  if (center_aligned != 0U)
  {
    return 2U * arr;
  }

  return arr + 1U;
}

static uint8_t pwm_link_tim_center_aligned(TIM_TypeDef *tim)
{
  uint32_t cms = (tim->CR1 & TIM_CR1_CMS) >> TIM_CR1_CMS_Pos;
  return (cms != 0U) ? 1U : 0U;
}

static uint32_t pwm_link_phase_ticks(TIM_TypeDef *tim)
{
  uint32_t arr = tim->ARR;
  uint32_t cnt = tim->CNT;

  if (pwm_link_tim_center_aligned(tim) == 0U)
  {
    return cnt;
  }

  if ((tim->CR1 & TIM_CR1_DIR) == 0U)
  {
    return cnt;
  }

  return (2U * arr) - cnt;
}

static int pwm_link_apply_follower_timing(TIM_HandleTypeDef *follow_htim,
                                          TIM_HandleTypeDef *master_htim,
                                          const tars_mcu_pwm_entry_t *follow,
                                          uint8_t duty_pct)
{
  TIM_OC_InitTypeDef oc = {0};
  uint32_t arr = master_htim->Instance->ARR;
  uint32_t pulse;

  if ((follow_htim == NULL) || (master_htim == NULL) || (follow == NULL))
  {
    return TARS_RES_ERR_PARAM;
  }

  follow_htim->Init.Prescaler = (uint16_t)master_htim->Instance->PSC;
  follow_htim->Init.CounterMode = TIM_COUNTERMODE_CENTERALIGNED1;
  follow_htim->Init.Period = arr;
  follow_htim->Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  follow_htim->Init.RepetitionCounter = (uint8_t)master_htim->Instance->RCR;
  follow_htim->Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

  if (HAL_TIM_PWM_Init(follow_htim) != HAL_OK)
  {
    return TARS_RES_ERR_PARAM;
  }

  /* Mirror live TIM1 counter mode / update policy so TRGO rate matches. */
  {
    TIM_TypeDef *tim1 = master_htim->Instance;
    TIM_TypeDef *tim3 = follow_htim->Instance;
    uint32_t cr1 = tim3->CR1;

    cr1 &= ~(TIM_CR1_CMS | TIM_CR1_URS | TIM_CR1_ARPE);
    cr1 |= (tim1->CR1 & (TIM_CR1_CMS | TIM_CR1_URS | TIM_CR1_ARPE));
    tim3->CR1 = cr1;
    tim3->PSC = tim1->PSC;
    tim3->ARR = tim1->ARR;
    tim3->RCR = tim1->RCR;
    pwm_force_update(follow_htim);
  }

  pulse = pwm_pulse_from_duty(follow_htim, duty_pct);
  oc.OCMode = TIM_OCMODE_PWM1;
  oc.Pulse = pulse;
  oc.OCPolarity = pwm_oc_polarity_hal(pwm_ch_polarity_low(follow, pwm_find_ch_slot(follow->channel, 0)));
  oc.OCFastMode = TIM_OCFAST_DISABLE;

  if (HAL_TIM_PWM_ConfigChannel(follow_htim, &oc, follow->hal_channel) != HAL_OK)
  {
    return TARS_RES_ERR_PARAM;
  }

  pwm_disable_oc_preload(follow_htim->Instance, follow->hal_channel);
  pwm_apply_compare(follow_htim, follow->hal_channel, pulse);
  pwm_force_update(follow_htim);
  return 0;
}

static void pwm_link_restore_follower_timing(TIM_HandleTypeDef *follow_htim,
                                             const tars_mcu_pwm_entry_t *follow,
                                             uint8_t duty_pct)
{
  int slot;
  uint32_t freq_hz = pwm_map_default_freq_hz(follow);

  if ((follow_htim == NULL) || (follow == NULL))
  {
    return;
  }

  slot = pwm_find_tim_slot(follow->tim, 0);
  if (slot >= 0)
  {
    freq_hz = s_tim_pool[(uint32_t)slot].freq_hz;
  }

  follow_htim->Init.Prescaler = 0U;
  follow_htim->Init.CounterMode = TIM_COUNTERMODE_UP;
  follow_htim->Init.Period = 1000U;
  follow_htim->Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  follow_htim->Init.RepetitionCounter = 0U;
  follow_htim->Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

  if (HAL_TIM_PWM_Init(follow_htim) == HAL_OK)
  {
    if (pwm_tim_prefers_center_aligned(follow->tim) != 0U)
    {
      follow_htim->Init.CounterMode = TIM_COUNTERMODE_CENTERALIGNED1;
      if (HAL_TIM_PWM_Init(follow_htim) == HAL_OK)
      {
        (void)pwm_apply_tim_timing(follow_htim, freq_hz);
      }
    }
    else
    {
      (void)pwm_apply_tim_timing(follow_htim, freq_hz);
    }
    (void)pwm_configure_channel(follow, duty_pct);
  }
}

static void pwm_link_hw_apply(TIM_HandleTypeDef *htim, int enable, int32_t offset_ticks)
{
  if ((htim == NULL) || (htim->Instance != TIM3))
  {
    return;
  }

  if ((enable != 0) && (offset_ticks == 0))
  {
    TIM_SlaveConfigTypeDef sc = {0};

    sc.SlaveMode = TIM_SLAVEMODE_RESET;
    sc.InputTrigger = TIM_TS_ITR0;
    sc.TriggerPolarity = TIM_TRIGGERPOLARITY_RISING;
    sc.TriggerFilter = 0U;
    (void)HAL_TIM_SlaveConfigSynchronization(htim, &sc);
  }
  else
  {
    htim->Instance->SMCR &= ~(TIM_SMCR_SMS | TIM_SMCR_TS | TIM_SMCR_MSM);
  }
}

static int pwm_link_wait_master_update(TIM_TypeDef *tim1)
{
  uint32_t spin = 2000000U;

  tim1->SR = (uint16_t)~TIM_SR_UIF;
  while (((tim1->SR & TIM_SR_UIF) == 0U) && (spin > 0U))
  {
    spin--;
  }

  tim1->SR = (uint16_t)~TIM_SR_UIF;
  return (spin > 0U) ? 0 : TARS_RES_ERR_PARAM;
}

static int pwm_link_resolve_pair(const tars_mcu_pwm_entry_t **master_out,
                                 const tars_mcu_pwm_entry_t **follow_out,
                                 TIM_HandleTypeDef **master_htim_out,
                                 TIM_HandleTypeDef **follow_htim_out,
                                 int require_follower_htim)
{
  const tars_mcu_pwm_entry_t *master = NULL;
  const tars_mcu_pwm_entry_t *follow = NULL;
  TIM_HandleTypeDef *master_htim = NULL;
  TIM_HandleTypeDef *follow_htim = NULL;

  if ((master_out == NULL) || (follow_out == NULL) ||
      (master_htim_out == NULL) || (follow_htim_out == NULL))
  {
    return TARS_RES_ERR_PARAM;
  }

  if ((TarsMcuPinmap_ResolvePwm(TARS_PWM_LINK_MASTER, &master) != 0) ||
      (TarsMcuPinmap_ResolvePwm(TARS_PWM_LINK_FOLLOWER, &follow) != 0))
  {
    return TARS_RES_ERR_SCOPE;
  }

  if ((master->tim != TIM1) || (follow->tim != TIM3))
  {
    return TARS_RES_ERR_PARAM;
  }

  master_htim = pwm_tim_handle(master);
  if (master_htim == NULL)
  {
    return TARS_RES_ERR_PARAM;
  }

  follow_htim = pwm_tim_handle(follow);
  if ((require_follower_htim != 0) && (follow_htim == NULL))
  {
    return TARS_RES_ERR_PARAM;
  }

  *master_out = master;
  *follow_out = follow;
  *master_htim_out = master_htim;
  *follow_htim_out = follow_htim;
  return 0;
}

static int pwm_link_prepare_follower(void)
{
  const tars_mcu_pwm_entry_t *master = NULL;
  const tars_mcu_pwm_entry_t *follow = NULL;
  TIM_HandleTypeDef *master_htim = NULL;
  TIM_HandleTypeDef *follow_htim = NULL;
  uint8_t duty = 50U;
  int ch_slot;
  int st;

  if (s_link.enabled == 0U)
  {
    return 0;
  }

  st = pwm_link_resolve_pair(&master, &follow, &master_htim, &follow_htim, 1);
  if (st != 0)
  {
    return st;
  }

  ch_slot = pwm_find_ch_slot(TARS_PWM_LINK_FOLLOWER, 0);
  if (ch_slot >= 0)
  {
    duty = s_ch_pool[(uint32_t)ch_slot].duty_pct;
  }

  return pwm_link_apply_follower_timing(follow_htim, master_htim, follow, duty);
}

static int pwm_link_snap_phase(void)
{
  const tars_mcu_pwm_entry_t *master = NULL;
  const tars_mcu_pwm_entry_t *follow = NULL;
  TIM_HandleTypeDef *master_htim = NULL;
  TIM_HandleTypeDef *follow_htim = NULL;
  TIM_TypeDef *tim1;
  TIM_TypeDef *tim3;
  uint32_t period;
  int st;

  if (s_link.enabled == 0U)
  {
    return 0;
  }

  st = pwm_link_resolve_pair(&master, &follow, &master_htim, &follow_htim, 1);
  if (st != 0)
  {
    return st;
  }

  if (TarsResPwm_IsRunning(TARS_PWM_LINK_MASTER) == 0)
  {
    return TARS_RES_ERR_ACTIVE;
  }

  tim1 = master_htim->Instance;
  tim3 = follow_htim->Instance;
  period = pwm_link_period_ticks(tim1, 1U);

  pwm_link_hw_apply(follow_htim, 1, s_link.offset_ticks);

  if (s_link.offset_ticks == 0)
  {
    tim3->CNT = tim1->CNT;
  }
  else
  {
    uint32_t cnt = (uint32_t)pwm_link_norm_offset((int32_t)tim1->CNT + s_link.offset_ticks,
                                                  period);
    tim3->CNT = cnt;
  }

  return 0;
}

static int pwm_link_snap_follower(void)
{
  int st;

  st = pwm_link_prepare_follower();
  if (st != 0)
  {
    return st;
  }

  return pwm_link_snap_phase();
}

int TarsResPwm_Enable(const char *channel, int enable)
{
  const tars_mcu_pwm_entry_t *map = NULL;
  int ch_slot;
  tars_pwm_ch_t *ch;
  TIM_HandleTypeDef *htim;
  int st;

  if (TarsMcuPinmap_ResolvePwm(channel, &map) != 0)
  {
    return TARS_RES_ERR_SCOPE;
  }

  if (TarsResMgr_TenantAssigned(channel) == 0)
  {
    return TARS_RES_ERR_OWNER;
  }

  if ((map->advanced_tim != 0U) && (enable != 0) && (pwm_foc_tim_active() != 0))
  {
    return TARS_RES_ERR_ACTIVE;
  }

  /* Handoff: take the TIM1 physical domain from an idle FOC so its ISR stops
   * writing the neutral compares and this channel's duty actually sticks. */
  if ((map->advanced_tim != 0U) && (enable != 0))
  {
    char tenant[TARS_TENANT_LEN];

    (void)TarsResMgr_GetTenant(channel, tenant, sizeof(tenant));
    TarsResMgr_TimDomainForceSet(map->tim_id, tenant);
  }

  ch_slot = pwm_find_ch_slot(channel, 1);
  if (ch_slot < 0)
  {
    return TARS_RES_ERR_PARAM;
  }

  ch = &s_ch_pool[(uint32_t)ch_slot];

  if (enable != 0)
  {
    if (ch->running != 0U)
    {
      htim = pwm_tim_handle(map);
      if (htim != NULL)
      {
        pwm_apply_compare(htim, map->hal_channel, pwm_pulse_from_duty(htim, ch->duty_pct));
      }
      return 0;
    }
  }

  if (enable == 0)
  {
    if (ch->running != 0U)
    {
      htim = pwm_tim_handle(map);
      if (htim != NULL)
      {
        (void)HAL_TIM_PWM_Stop(htim, map->hal_channel);
        if (map->advanced_tim != 0U)
        {
          char tenant[TARS_TENANT_LEN];

          __HAL_TIM_MOE_DISABLE(htim);
          (void)TarsResMgr_GetTenant(channel, tenant, sizeof(tenant));
          TarsResMgr_TimDomainRelease(map->tim_id, tenant);
        }
      }

      (void)TarsResMgr_ReleasePwm(channel);

      {
        int tslot = pwm_find_tim_slot(map->tim, 0);
        if (tslot >= 0)
        {
          if (s_tim_pool[(uint32_t)tslot].ref_count > 0U)
          {
            s_tim_pool[(uint32_t)tslot].ref_count--;
          }
        }
      }

      ch->running = 0U;

      if (strcmp(channel, TARS_PWM_LINK_FOLLOWER) == 0)
      {
        TIM_HandleTypeDef *f_htim = pwm_tim_handle(map);
        pwm_link_hw_apply(f_htim, 0, 0);
        if (s_link.enabled != 0U)
        {
          pwm_link_restore_follower_timing(f_htim, map, ch->duty_pct);
        }
      }
    }
    return 0;
  }

  st = TarsResMgr_AcquirePwm(channel);
  if (st != 0)
  {
    return st;
  }

  ch->map = map;
  pwm_ch_sync_defaults(ch, map);

  if ((ch->duty_pct == 0U) && (map->default_duty_pct != 0U))
  {
    ch->duty_pct = map->default_duty_pct;
  }

  if (pwm_configure_channel(map, ch->duty_pct) != 0)
  {
    (void)TarsResMgr_ReleasePwm(channel);
    return TARS_RES_ERR_PARAM;
  }

  if ((s_link.enabled != 0U) &&
      (strcmp(channel, TARS_PWM_LINK_FOLLOWER) == 0))
  {
    st = pwm_link_prepare_follower();
    if (st != 0)
    {
      (void)TarsResMgr_ReleasePwm(channel);
      return st;
    }
  }

  htim = pwm_tim_handle(map);
  if (htim == NULL)
  {
    (void)TarsResMgr_ReleasePwm(channel);
    return TARS_RES_ERR_PARAM;
  }

  {
    HAL_StatusTypeDef hal_st = HAL_TIM_PWM_Start(htim, map->hal_channel);

    if (hal_st != HAL_OK)
    {
      /* FOC init already starts TIM1 PWM channels (MOE off). Shell reuse is OK. */
      if ((map->tim != TIM1) || ((htim->Instance->CR1 & TIM_CR1_CEN) == 0U))
      {
        (void)TarsResMgr_ReleasePwm(channel);
        return TARS_RES_ERR_PARAM;
      }
    }
  }

  if (map->advanced_tim != 0U)
  {
    __HAL_TIM_MOE_ENABLE(htim);
  }

  pwm_apply_compare(htim, map->hal_channel, pwm_pulse_from_duty(htim, ch->duty_pct));

  if ((s_link.enabled != 0U) &&
      (strcmp(channel, TARS_PWM_LINK_FOLLOWER) == 0))
  {
    st = pwm_link_snap_phase();
    if (st != 0)
    {
      (void)HAL_TIM_PWM_Stop(htim, map->hal_channel);
      (void)TarsResMgr_ReleasePwm(channel);
      {
        int tslot_err = pwm_find_tim_slot(map->tim, 0);
        if (tslot_err >= 0)
        {
          if (s_tim_pool[(uint32_t)tslot_err].ref_count > 0U)
          {
            s_tim_pool[(uint32_t)tslot_err].ref_count--;
          }
        }
      }
      ch->running = 0U;
      return st;
    }
  }

  {
    int tslot = pwm_find_tim_slot(map->tim, 0);
    if (tslot >= 0)
    {
      ch->tim_slot = (int8_t)tslot;
      s_tim_pool[(uint32_t)tslot].ref_count++;
    }
  }

  ch->running = 1U;
  return 0;
}

int TarsResPwm_SetDuty(const char *channel, float duty_pct)
{
  const tars_mcu_pwm_entry_t *map = NULL;
  int ch_slot;
  tars_pwm_ch_t *ch;
  TIM_HandleTypeDef *htim;
  uint8_t duty;

  if (TarsMcuPinmap_ResolvePwm(channel, &map) != 0)
  {
    return TARS_RES_ERR_SCOPE;
  }

  if (duty_pct < 0.0f)
  {
    duty_pct = 0.0f;
  }
  if (duty_pct > 100.0f)
  {
    duty_pct = 100.0f;
  }

  duty = (uint8_t)duty_pct;

  ch_slot = pwm_find_ch_slot(channel, 1);
  if (ch_slot < 0)
  {
    return TARS_RES_ERR_PARAM;
  }

  ch = &s_ch_pool[(uint32_t)ch_slot];
  ch->map = map;
  pwm_ch_sync_defaults(ch, map);
  ch->duty_pct = duty;

  if (ch->running == 0U)
  {
    return 0;
  }

  htim = pwm_tim_handle(map);
  if (htim == NULL)
  {
    return TARS_RES_ERR_PARAM;
  }

  pwm_apply_compare(htim, map->hal_channel, pwm_pulse_from_duty(htim, duty));
  return 0;
}

int TarsResPwm_IsRunning(const char *channel)
{
  int ch_slot = pwm_find_ch_slot(channel, 0);

  if (ch_slot < 0)
  {
    return 0;
  }

  return (s_ch_pool[(uint32_t)ch_slot].running != 0U) ? 1 : 0;
}

int TarsResPwm_SetFreq(const char *tim_id, uint32_t freq_hz)
{
  uint32_t pwm_count = 0U;
  const tars_mcu_pwm_entry_t *table = TarsMcuPinmap_GetPwmTable(&pwm_count);
  TIM_TypeDef *tim = NULL;
  TIM_HandleTypeDef *htim = NULL;
  uint32_t i;

  if ((tim_id == NULL) || (freq_hz == 0U))
  {
    return TARS_RES_ERR_PARAM;
  }

  for (i = 0U; i < pwm_count; i++)
  {
    if ((table[i].tim_id != NULL) && (strcmp(table[i].tim_id, tim_id) == 0))
    {
      tim = table[i].tim;
      break;
    }
  }

  if (tim == NULL)
  {
    return TARS_RES_ERR_SCOPE;
  }

  if (tim == TIM1)
  {
    if (pwm_foc_tim_active() != 0)
    {
      return TARS_RES_ERR_ACTIVE;
    }
    htim = &htim1;
    (void)pwm_find_tim_slot(TIM1, 1);
    return 0;
  }
  else if (tim == TIM9)
  {
    int slot = pwm_find_tim_slot(TIM9, 1);

    if (slot < 0)
    {
      return TARS_RES_ERR_SCOPE;
    }

    htim = &htim9;
    s_tim_pool[(uint32_t)slot].freq_hz = freq_hz;
  }
  else
  {
    int slot = pwm_find_tim_slot(tim, 1);
    if (slot < 0)
    {
      return TARS_RES_ERR_SCOPE;
    }
    htim = &s_tim_pool[(uint32_t)slot].handle;
    s_tim_pool[(uint32_t)slot].freq_hz = freq_hz;

    if (s_tim_pool[(uint32_t)slot].handle.Instance == NULL)
    {
      s_tim_pool[(uint32_t)slot].handle.Instance = tim;
      pwm_enable_tim_clock(tim);
      s_tim_pool[(uint32_t)slot].handle.Init.Prescaler = 0U;
      s_tim_pool[(uint32_t)slot].handle.Init.CounterMode =
          (pwm_tim_prefers_center_aligned(tim) != 0U)
          ? TIM_COUNTERMODE_CENTERALIGNED1
          : TIM_COUNTERMODE_UP;
      s_tim_pool[(uint32_t)slot].handle.Init.Period = 1000U;
      s_tim_pool[(uint32_t)slot].handle.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
      s_tim_pool[(uint32_t)slot].handle.Init.RepetitionCounter = 0U;
      s_tim_pool[(uint32_t)slot].handle.Init.AutoReloadPreload =
          TIM_AUTORELOAD_PRELOAD_DISABLE;
      if (HAL_TIM_PWM_Init(&s_tim_pool[(uint32_t)slot].handle) != HAL_OK)
      {
        return TARS_RES_ERR_PARAM;
      }
    }
  }

  if (tim != TIM1)
  {
    int slot = pwm_find_tim_slot(tim, 0);
    if (slot >= 0)
    {
      s_tim_pool[(uint32_t)slot].freq_hz = freq_hz;
    }
  }

  if (pwm_apply_tim_timing(htim, freq_hz) != 0)
  {
    return TARS_RES_ERR_PARAM;
  }

  for (i = 0U; i < s_ch_count; i++)
  {
    if ((s_ch_pool[i].running != 0U) &&
        (s_ch_pool[i].map != NULL) &&
        (s_ch_pool[i].map->tim == tim))
    {
      (void)TarsResPwm_SetDuty(s_ch_pool[i].map->channel, s_ch_pool[i].duty_pct);
    }
  }

  return 0;
}

int TarsResPwm_GetStatus(const char *channel, char *out, uint32_t out_size)
{
  const tars_mcu_pwm_entry_t *map = NULL;
  int ch_slot;
  tars_pwm_ch_t *ch;

  if ((out == NULL) || (out_size == 0U))
  {
    return TARS_RES_ERR_PARAM;
  }

  if (TarsMcuPinmap_ResolvePwm(channel, &map) != 0)
  {
    (void)snprintf(out, out_size, "pwm: unknown %s\r\n", channel ? channel : "?");
    return TARS_RES_ERR_SCOPE;
  }

  int written;
  int pin_idx;
  uint32_t moder = 0U;
  uint32_t afr = 0U;
  TIM_TypeDef *tim = map->tim;

  char tenant[TARS_TENANT_LEN];
  char active[TARS_TENANT_LEN];
  char tim_drv[TARS_TENANT_LEN];

  ch_slot = pwm_find_ch_slot(channel, 0);
  ch = (ch_slot >= 0) ? &s_ch_pool[(uint32_t)ch_slot] : NULL;

  (void)TarsResMgr_GetTenant(channel, tenant, sizeof(tenant));
  (void)TarsResMgr_GetActiveTenant(channel, active, sizeof(active));
  (void)TarsResMgr_GetTimDomainActiveTenant(map->tim_id, tim_drv, sizeof(tim_drv));

  written = snprintf(out,
                     out_size,
                     "pwm: ch=%s pin=%s tim=%s tenant=%s active=%s drv=%s "
                     "run=%u duty=%u%% pol=%s\r\n",
                     map->channel,
                     map->pin_name,
                     map->tim_id ? map->tim_id : "?",
                     TarsTenant_Display(tenant),
                     TarsTenant_Display(active),
                     TarsTenant_Display(tim_drv),
                     (unsigned)((ch != NULL) ? ch->running : 0U),
                     (unsigned)((ch != NULL) ? ch->duty_pct : 0U),
                     pwm_polarity_name(pwm_ch_polarity_low(map, ch_slot)));
  if ((written < 0) || ((uint32_t)written >= out_size))
  {
    return 0;
  }

  pin_idx = pwm_pin_index(map->hal_pin);
  if (pin_idx >= 0)
  {
    moder = (map->port->MODER >> ((uint32_t)pin_idx * 2U)) & 3U;
    if (pin_idx < 8)
    {
      afr = (map->port->AFR[0] >> ((uint32_t)pin_idx * 4U)) & 0xFU;
    }
    else
    {
      afr = (map->port->AFR[1] >> ((uint32_t)(pin_idx - 8) * 4U)) & 0xFU;
    }
  }

  if (map->advanced_tim != 0U)
  {
    (void)snprintf(out + (uint32_t)written,
                   out_size - (uint32_t)written,
                   "  hw: tim_cr1=0x%08lx cen=%lu ccen=0x%04lx ccmr1=0x%08lx ccr1=%lu ccr2=%lu arr=%lu cnt=%lu pin_moder=%lu pin_afr=%lu moe=%lu bdtr=0x%04lx\r\n",
                   (unsigned long)tim->CR1,
                   (unsigned long)((tim->CR1 & TIM_CR1_CEN) != 0U ? 1U : 0U),
                   (unsigned long)(tim->CCER & 0xFFFFU),
                   (unsigned long)tim->CCMR1,
                   (unsigned long)tim->CCR1,
                   (unsigned long)tim->CCR2,
                   (unsigned long)tim->ARR,
                   (unsigned long)tim->CNT,
                   (unsigned long)moder,
                   (unsigned long)afr,
                   (unsigned long)((tim->BDTR & TIM_BDTR_MOE) != 0U ? 1U : 0U),
                   (unsigned long)(tim->BDTR & 0xFFFFU));
  }
  else
  {
    (void)snprintf(out + (uint32_t)written,
                   out_size - (uint32_t)written,
                   "  hw: tim_cr1=0x%08lx cen=%lu ccen=0x%04lx ccmr1=0x%08lx ccr1=%lu ccr2=%lu arr=%lu cnt=%lu pin_moder=%lu pin_afr=%lu\r\n",
                   (unsigned long)tim->CR1,
                   (unsigned long)((tim->CR1 & TIM_CR1_CEN) != 0U ? 1U : 0U),
                   (unsigned long)(tim->CCER & 0xFFFFU),
                   (unsigned long)tim->CCMR1,
                   (unsigned long)tim->CCR1,
                   (unsigned long)tim->CCR2,
                   (unsigned long)tim->ARR,
                   (unsigned long)tim->CNT,
                   (unsigned long)moder,
                   (unsigned long)afr);
  }
  return 0;
}

int TarsResPwm_SetPersist(const char *channel, int boot_enable)
{
  int ch_slot;

  if (channel == NULL)
  {
    return TARS_RES_ERR_PARAM;
  }

  ch_slot = pwm_find_ch_slot(channel, 1);
  if (ch_slot < 0)
  {
    return TARS_RES_ERR_PARAM;
  }

  s_ch_pool[(uint32_t)ch_slot].boot_enable = (boot_enable != 0) ? 1U : 0U;
  return 0;
}

int TarsResPwm_GetPersist(const char *channel, int *boot_enable_out)
{
  int ch_slot;

  if ((channel == NULL) || (boot_enable_out == NULL))
  {
    return TARS_RES_ERR_PARAM;
  }

  ch_slot = pwm_find_ch_slot(channel, 0);
  if (ch_slot < 0)
  {
    *boot_enable_out = 0;
    return 0;
  }

  *boot_enable_out = (int)s_ch_pool[(uint32_t)ch_slot].boot_enable;
  return 0;
}

int TarsResPwm_GetDuty(const char *channel, uint8_t *duty_out)
{
  int ch_slot;

  if ((channel == NULL) || (duty_out == NULL))
  {
    return TARS_RES_ERR_PARAM;
  }

  ch_slot = pwm_find_ch_slot(channel, 0);
  if (ch_slot < 0)
  {
    *duty_out = 0U;
    return 0;
  }

  *duty_out = s_ch_pool[(uint32_t)ch_slot].duty_pct;
  return 0;
}

int TarsResPwm_GetTimFreq(const char *tim_id, uint32_t *freq_hz_out)
{
  uint32_t pwm_count = 0U;
  const tars_mcu_pwm_entry_t *table = TarsMcuPinmap_GetPwmTable(&pwm_count);
  TIM_TypeDef *tim = NULL;
  uint32_t i;

  if ((tim_id == NULL) || (freq_hz_out == NULL))
  {
    return TARS_RES_ERR_PARAM;
  }

  for (i = 0U; i < pwm_count; i++)
  {
    if ((table[i].tim_id != NULL) && (strcmp(table[i].tim_id, tim_id) == 0))
    {
      tim = table[i].tim;
      break;
    }
  }

  if (tim == NULL)
  {
    return TARS_RES_ERR_SCOPE;
  }

  if (tim == TIM1)
  {
    return TARS_RES_ERR_PARAM;
  }

  {
    int slot = pwm_find_tim_slot(tim, 0);

    if (slot >= 0)
    {
      *freq_hz_out = s_tim_pool[(uint32_t)slot].freq_hz;
      return 0;
    }
  }

  if (tim == TIM9)
  {
    uint32_t arr = __HAL_TIM_GET_AUTORELOAD(&htim9);

    if (arr > 0U)
    {
      *freq_hz_out = pwm_tim_clk_hz(TIM9) / (arr + 1U);
    }
    else
    {
      *freq_hz_out = TARS_PWM_DEFAULT_HZ;
    }
    return 0;
  }

  *freq_hz_out = TARS_PWM_DEFAULT_HZ;
  return 0;
}

int TarsResPwm_TimFreqConfigured(const char *tim_id)
{
  uint32_t pwm_count = 0U;
  const tars_mcu_pwm_entry_t *table = TarsMcuPinmap_GetPwmTable(&pwm_count);
  TIM_TypeDef *tim = NULL;
  uint32_t i;

  if (tim_id == NULL)
  {
    return 0;
  }

  for (i = 0U; i < pwm_count; i++)
  {
    if ((table[i].tim_id != NULL) && (strcmp(table[i].tim_id, tim_id) == 0))
    {
      tim = table[i].tim;
      break;
    }
  }

  if ((tim == NULL) || (tim == TIM1))
  {
    return 0;
  }

  return (pwm_find_tim_slot(tim, 0) >= 0) ? 1 : 0;
}

int TarsResPwm_ParsePolarity(const char *name, int *low_out)
{
  if ((name == NULL) || (low_out == NULL))
  {
    return TARS_RES_ERR_PARAM;
  }

  if ((strcmp(name, "high") == 0) || (strcmp(name, "h") == 0) || (strcmp(name, "0") == 0))
  {
    *low_out = 0;
    return 0;
  }

  if ((strcmp(name, "low") == 0) || (strcmp(name, "l") == 0) || (strcmp(name, "1") == 0) ||
      (strcmp(name, "invert") == 0) || (strcmp(name, "inverted") == 0))
  {
    *low_out = 1;
    return 0;
  }

  return TARS_RES_ERR_PARAM;
}

int TarsResPwm_SetPolarity(const char *channel, int low)
{
  const tars_mcu_pwm_entry_t *map = NULL;
  int ch_slot;
  tars_pwm_ch_t *ch;

  if (TarsMcuPinmap_ResolvePwm(channel, &map) != 0)
  {
    return TARS_RES_ERR_SCOPE;
  }

  ch_slot = pwm_find_ch_slot(channel, 1);
  if (ch_slot < 0)
  {
    return TARS_RES_ERR_PARAM;
  }

  ch = &s_ch_pool[(uint32_t)ch_slot];
  ch->map = map;
  ch->polarity_low = (low != 0) ? 1U : 0U;
  ch->polarity_explicit = 1U;

  if (ch->running != 0U)
  {
    if (pwm_configure_channel(map, ch->duty_pct) != 0)
    {
      return TARS_RES_ERR_PARAM;
    }
  }

  return 0;
}

int TarsResPwm_GetPolarity(const char *channel, int *low_out)
{
  const tars_mcu_pwm_entry_t *map = NULL;
  int ch_slot;

  if ((channel == NULL) || (low_out == NULL))
  {
    return TARS_RES_ERR_PARAM;
  }

  if (TarsMcuPinmap_ResolvePwm(channel, &map) != 0)
  {
    return TARS_RES_ERR_SCOPE;
  }

  ch_slot = pwm_find_ch_slot(channel, 0);
  *low_out = (int)pwm_ch_polarity_low(map, ch_slot);
  return 0;
}

int TarsResPwm_LinkGetConfig(int *enable_out, int32_t *offset_out)
{
  if (enable_out != NULL)
  {
    *enable_out = (s_link.enabled != 0U) ? 1 : 0;
  }

  if (offset_out != NULL)
  {
    *offset_out = s_link.offset_ticks;
  }

  return 0;
}

int TarsResPwm_LinkSet(int enable, int32_t offset_ticks)
{
  s_link.enabled = (enable != 0) ? 1U : 0U;
  s_link.offset_ticks = offset_ticks;

  if (s_link.enabled == 0U)
  {
    const tars_mcu_pwm_entry_t *follow = NULL;
    TIM_HandleTypeDef *follow_htim = NULL;
    int ch_slot;

    if (TarsMcuPinmap_ResolvePwm(TARS_PWM_LINK_FOLLOWER, &follow) == 0)
    {
      follow_htim = pwm_tim_handle(follow);
      pwm_link_hw_apply(follow_htim, 0, 0);
      if ((follow_htim != NULL) && (TarsResPwm_IsRunning(TARS_PWM_LINK_FOLLOWER) != 0))
      {
        ch_slot = pwm_find_ch_slot(TARS_PWM_LINK_FOLLOWER, 0);
        pwm_link_restore_follower_timing(
            follow_htim,
            follow,
            (ch_slot >= 0) ? s_ch_pool[(uint32_t)ch_slot].duty_pct : 50U);
      }
    }
  }

  return 0;
}

int TarsResPwm_LinkEnable(int enable)
{
  s_link.enabled = (enable != 0) ? 1U : 0U;

  if (s_link.enabled == 0U)
  {
    const tars_mcu_pwm_entry_t *follow = NULL;
    TIM_HandleTypeDef *follow_htim = NULL;
    int ch_slot;

    if (TarsMcuPinmap_ResolvePwm(TARS_PWM_LINK_FOLLOWER, &follow) == 0)
    {
      follow_htim = pwm_tim_handle(follow);
      pwm_link_hw_apply(follow_htim, 0, 0);
      if ((follow_htim != NULL) && (TarsResPwm_IsRunning(TARS_PWM_LINK_FOLLOWER) != 0))
      {
        ch_slot = pwm_find_ch_slot(TARS_PWM_LINK_FOLLOWER, 0);
        pwm_link_restore_follower_timing(
            follow_htim,
            follow,
            (ch_slot >= 0) ? s_ch_pool[(uint32_t)ch_slot].duty_pct : 50U);
      }
    }
  }

  return 0;
}

int TarsResPwm_LinkSetOffset(int32_t offset_ticks)
{
  s_link.offset_ticks = offset_ticks;
  return 0;
}

int TarsResPwm_LinkResync(void)
{
  if (s_link.enabled == 0U)
  {
    return TARS_RES_ERR_PARAM;
  }

  if (TarsResPwm_IsRunning(TARS_PWM_LINK_FOLLOWER) != 0)
  {
    return pwm_link_snap_phase();
  }

  return pwm_link_snap_follower();
}

int TarsResPwm_LinkGetStatus(char *out, uint32_t out_size)
{
  const tars_mcu_pwm_entry_t *master = NULL;
  const tars_mcu_pwm_entry_t *follow = NULL;
  TIM_HandleTypeDef *master_htim = NULL;
  TIM_HandleTypeDef *follow_htim = NULL;
  TIM_TypeDef *tim1;
  TIM_TypeDef *tim3;
  uint32_t period = 0U;
  uint32_t cnt_m = 0U;
  uint32_t cnt_f = 0U;
  uint32_t expect = 0U;
  int32_t skew = 0;
  int st;

  if ((out == NULL) || (out_size == 0U))
  {
    return TARS_RES_ERR_PARAM;
  }

  st = pwm_link_resolve_pair(&master, &follow, &master_htim, &follow_htim, 0);
  if (st != 0)
  {
    (void)snprintf(out,
                   out_size,
                   "pwm link: on=%u master=%s follower=%s offset=%ld (pair err)\r\n",
                   (unsigned)s_link.enabled,
                   TARS_PWM_LINK_MASTER,
                   TARS_PWM_LINK_FOLLOWER,
                   (long)s_link.offset_ticks);
    return st;
  }

  tim1 = master_htim->Instance;
  period = tim1->ARR + 1U;

  if (TarsResPwm_IsRunning(TARS_PWM_LINK_MASTER) != 0)
  {
    cnt_m = tim1->CNT;
  }

  if ((follow_htim != NULL) && (TarsResPwm_IsRunning(TARS_PWM_LINK_FOLLOWER) != 0))
  {
    tim3 = follow_htim->Instance;
    cnt_f = tim3->CNT;
  }

  if ((period > 0U) && (TarsResPwm_IsRunning(TARS_PWM_LINK_MASTER) != 0) &&
      (TarsResPwm_IsRunning(TARS_PWM_LINK_FOLLOWER) != 0))
  {
    expect = (uint32_t)pwm_link_norm_offset((int32_t)cnt_m + s_link.offset_ticks, period);
    skew = (int32_t)cnt_f - (int32_t)expect;
    if (skew > (int32_t)(period / 2U))
    {
      skew -= (int32_t)period;
    }
    else if (skew < -(int32_t)(period / 2U))
    {
      skew += (int32_t)period;
    }
  }

  (void)snprintf(out,
                 out_size,
                 "pwm link: on=%u master=%s follower=%s offset=%ld "
                 "cnt_m=%lu cnt_f=%lu expect=%lu skew=%ld period=%lu\r\n",
                 (unsigned)s_link.enabled,
                 TARS_PWM_LINK_MASTER,
                 TARS_PWM_LINK_FOLLOWER,
                 (long)s_link.offset_ticks,
                 (unsigned long)cnt_m,
                 (unsigned long)cnt_f,
                 (unsigned long)expect,
                 (long)skew,
                 (unsigned long)period);
  return 0;
}
