#include "tars_res_pwm.h"
#include "tars_res_mgr.h"
#include "tars_mcu_pinmap.h"
#include "main.h"
#include "tim.h"
#include <stdio.h>
#include <string.h>

#define TARS_PWM_TIM_SLOTS   4U
#define TARS_PWM_CH_SLOTS    16U
#define TARS_PWM_DEFAULT_HZ  1000U

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

  if ((tim == TIM1) || (tim == TIM9))
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

  {
    int slot = pwm_find_tim_slot(map->tim, 0);
    if (slot < 0)
    {
      return NULL;
    }
    return &s_tim_pool[(uint32_t)slot].handle;
  }
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

  /* Edge-aligned: F = CLK / ((PSC+1)*(ARR+1)). Hold PSC=0 for simplicity. */
  arr = (clk_hz / freq_hz);
  if (arr == 0U)
  {
    arr = 1U;
  }
  arr--;

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
    htim->Init.CounterMode = TIM_COUNTERMODE_UP;
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

static int pwm_configure_channel(const tars_mcu_pwm_entry_t *map, uint8_t duty_pct)
{
  TIM_HandleTypeDef *htim = pwm_tim_handle(map);
  TIM_OC_InitTypeDef oc = {0};
  uint32_t pulse;
  uint32_t arr;
  int slot;

  if (htim == NULL)
  {
    if (pwm_init_tim_instance(map, TARS_PWM_DEFAULT_HZ) != 0)
    {
      return -1;
    }
    htim = pwm_tim_handle(map);
  }

  if (htim == NULL)
  {
    return -1;
  }

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
  oc.OCPolarity = TIM_OCPOLARITY_HIGH;
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

static int pwm_foc_tim_active(void)
{
  if (TarsResMgr_GetActive("tim1_ch1") == TARS_OWNER_FOC)
  {
    return 1;
  }
  if (TarsResMgr_GetActive("tim1_ch2") == TARS_OWNER_FOC)
  {
    return 1;
  }
  if (TarsResMgr_GetActive("tim1_ch3") == TARS_OWNER_FOC)
  {
    return 1;
  }
  return 0;
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

  if (TarsResMgr_GetOwner(channel) != TARS_OWNER_PWM)
  {
    return TARS_RES_ERR_OWNER;
  }

  if ((map->advanced_tim != 0U) && (enable != 0) && (pwm_foc_tim_active() != 0))
  {
    return TARS_RES_ERR_ACTIVE;
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
          __HAL_TIM_MOE_DISABLE(htim);
        }
      }

      (void)TarsResMgr_ReleasePwm(channel, TARS_OWNER_PWM);

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
    }
    return 0;
  }

  st = TarsResMgr_AcquirePwm(channel, TARS_OWNER_PWM);
  if (st != 0)
  {
    return st;
  }

  ch->map = map;

  if (pwm_configure_channel(map, ch->duty_pct) != 0)
  {
    (void)TarsResMgr_ReleasePwm(channel, TARS_OWNER_PWM);
    return TARS_RES_ERR_PARAM;
  }

  htim = pwm_tim_handle(map);
  if (htim == NULL)
  {
    (void)TarsResMgr_ReleasePwm(channel, TARS_OWNER_PWM);
    return TARS_RES_ERR_PARAM;
  }

  {
    HAL_StatusTypeDef hal_st = HAL_TIM_PWM_Start(htim, map->hal_channel);

    if (hal_st != HAL_OK)
    {
      /* FOC init already starts TIM1 PWM channels (MOE off). Shell reuse is OK. */
      if ((map->tim != TIM1) || ((htim->Instance->CR1 & TIM_CR1_CEN) == 0U))
      {
        (void)TarsResMgr_ReleasePwm(channel, TARS_OWNER_PWM);
        return TARS_RES_ERR_PARAM;
      }
    }
  }

  if (map->advanced_tim != 0U)
  {
    __HAL_TIM_MOE_ENABLE(htim);
  }

  pwm_apply_compare(htim, map->hal_channel, pwm_pulse_from_duty(htim, ch->duty_pct));

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
      s_tim_pool[(uint32_t)slot].handle.Init.CounterMode = TIM_COUNTERMODE_UP;
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

  ch_slot = pwm_find_ch_slot(channel, 0);
  ch = (ch_slot >= 0) ? &s_ch_pool[(uint32_t)ch_slot] : NULL;

  written = snprintf(out,
                     out_size,
                     "pwm: ch=%s pin=%s tim=%s owner=%s active=%s run=%u duty=%u%%\r\n",
                     map->channel,
                     map->pin_name,
                     map->tim_id ? map->tim_id : "?",
                     TarsOwner_ToString(TarsResMgr_GetOwner(channel)),
                     TarsOwner_ToString(TarsResMgr_GetActive(channel)),
                     (unsigned)((ch != NULL) ? ch->running : 0U),
                     (unsigned)((ch != NULL) ? ch->duty_pct : 0U));
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
