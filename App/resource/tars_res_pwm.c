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
#define TARS_PWM_TIM_CLK_HZ  72000000U

typedef struct {
  TIM_TypeDef *instance;
  TIM_HandleTypeDef handle;
  uint32_t freq_hz;
  uint8_t init_done;
  uint8_t ref_count;
} tars_pwm_tim_t;

typedef struct {
  const tars_mcu_pwm_entry_t *map;
  float duty_pct;
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
  s_ch_pool[i].duty_pct = 0.0f;
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

  if ((htim == NULL) || (freq_hz == 0U))
  {
    return -1;
  }

  /* Edge-aligned: F = CLK / ((PSC+1)*(ARR+1)). Hold PSC=0 for simplicity. */
  arr = (TARS_PWM_TIM_CLK_HZ / freq_hz);
  if (arr == 0U)
  {
    arr = 1U;
  }
  arr--;

  __HAL_TIM_SET_PRESCALER(htim, 0U);
  __HAL_TIM_SET_AUTORELOAD(htim, arr);
  return 0;
}

static int pwm_init_tim_instance(const tars_mcu_pwm_entry_t *map, uint32_t freq_hz)
{
  int slot;
  tars_pwm_tim_t *rt;
  TIM_HandleTypeDef *htim;

  if (map->tim == TIM1)
  {
    htim = &htim1;
    slot = pwm_find_tim_slot(TIM1, 1);
  }
  else
  {
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
    htim->Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;

    if (map->advanced_tim != 0U)
    {
      if (HAL_TIM_PWM_Init(htim) != HAL_OK)
      {
        return -1;
      }
    }
    else
    {
      if (HAL_TIM_PWM_Init(htim) != HAL_OK)
      {
        return -1;
      }
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
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  gpio.Alternate = map->gpio_af;
  HAL_GPIO_Init(map->port, &gpio);
  return 0;
}

static int pwm_configure_channel(const tars_mcu_pwm_entry_t *map, float duty_pct)
{
  TIM_HandleTypeDef *htim = pwm_tim_handle(map);
  TIM_OC_InitTypeDef oc = {0};
  uint32_t pulse;
  uint32_t arr;

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

  if (map->tim != TIM1)
  {
    (void)pwm_config_pin_af(map);
  }

  arr = __HAL_TIM_GET_AUTORELOAD(htim);
  pulse = (uint32_t)((duty_pct / 100.0f) * (float)(arr + 1U));
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

  if (HAL_TIM_PWM_Start(htim, map->hal_channel) != HAL_OK)
  {
    (void)TarsResMgr_ReleasePwm(channel, TARS_OWNER_PWM);
    return TARS_RES_ERR_PARAM;
  }

  if (map->advanced_tim != 0U)
  {
    __HAL_TIM_MOE_ENABLE(htim);
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
  uint32_t arr;
  uint32_t pulse;

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

  ch_slot = pwm_find_ch_slot(channel, 1);
  if (ch_slot < 0)
  {
    return TARS_RES_ERR_PARAM;
  }

  ch = &s_ch_pool[(uint32_t)ch_slot];
  ch->map = map;
  ch->duty_pct = duty_pct;

  if (ch->running == 0U)
  {
    return 0;
  }

  htim = pwm_tim_handle(map);
  if (htim == NULL)
  {
    return TARS_RES_ERR_PARAM;
  }

  arr = __HAL_TIM_GET_AUTORELOAD(htim);
  pulse = (uint32_t)((duty_pct / 100.0f) * (float)(arr + 1U));
  if (pulse > arr)
  {
    pulse = arr;
  }

  __HAL_TIM_SET_COMPARE(htim, map->hal_channel, pulse);
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
  }
  else
  {
    int slot = pwm_find_tim_slot(tim, 0);
    if (slot < 0)
    {
      return TARS_RES_ERR_SCOPE;
    }
    htim = &s_tim_pool[(uint32_t)slot].handle;
    s_tim_pool[(uint32_t)slot].freq_hz = freq_hz;
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

  ch_slot = pwm_find_ch_slot(channel, 0);
  ch = (ch_slot >= 0) ? &s_ch_pool[(uint32_t)ch_slot] : NULL;

  (void)snprintf(out,
                 out_size,
                 "pwm: ch=%s pin=%s tim=%s owner=%s active=%s run=%u duty=%.1f%%\r\n",
                 map->channel,
                 map->pin_name,
                 map->tim_id ? map->tim_id : "?",
                 TarsOwner_ToString(TarsResMgr_GetOwner(channel)),
                 TarsOwner_ToString(TarsResMgr_GetActive(channel)),
                 (unsigned)((ch != NULL) ? ch->running : 0U),
                 (double)((ch != NULL) ? ch->duty_pct : 0.0));
  return 0;
}
