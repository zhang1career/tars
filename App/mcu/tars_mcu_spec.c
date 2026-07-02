#include "tars_mcu.h"
#include "tars_mcu_pinmap.h"
#include "tars_res_mgr.h"
#include "tars_res_pwm.h"
#include "tim.h"
#include "foc_params.h"
#include <stdio.h>
#include <string.h>

#define TARS_TIM9_PWM_HZ 1000U

static uint32_t spec_tim_design_freq_hz(const char *tim_id)
{
  if ((tim_id != NULL) && (strcmp(tim_id, "tim1") == 0))
  {
    return (uint32_t)FOC_PARAM_FPWM_HZ;
  }
  if ((tim_id != NULL) && (strcmp(tim_id, "tim9") == 0))
  {
    return TARS_TIM9_PWM_HZ;
  }
  return 1000U;
}

static int spec_tim_shell_freq_mutable(const char *tim_id)
{
  if ((tim_id != NULL) && (strcmp(tim_id, "tim1") == 0))
  {
    return 0;
  }
  return 1;
}

static const char *spec_pwm_mode(const char *tim_id)
{
  if ((tim_id != NULL) && (strcmp(tim_id, "tim1") == 0))
  {
    return "center_aligned";
  }
  return "edge_aligned";
}

static uint32_t spec_runtime_tim_freq_hz(TIM_TypeDef *tim, const char *tim_id)
{
  uint32_t freq = 0U;
  uint32_t arr;
  uint32_t clk_hz = 72000000U;

  if (tim == NULL)
  {
    return 0U;
  }

  if (tim == TIM1)
  {
    return (uint32_t)FOC_PARAM_FPWM_HZ;
  }

  if (TarsResPwm_GetTimFreq(tim_id, &freq) == 0)
  {
    return freq;
  }

  arr = tim->ARR;
  if (arr > 0U)
  {
    return clk_hz / (arr + 1U);
  }

  return spec_tim_design_freq_hz(tim_id);
}

static void spec_append(char *out, uint32_t out_size, const char *text)
{
  if ((out == NULL) || (out_size == 0U) || (text == NULL))
  {
    return;
  }

  (void)strncat(out, text, out_size - strlen(out) - 1U);
}

void TarsMcu_FormatSpecList(char *out, uint32_t out_size)
{
  uint32_t pwm_count = 0U;
  uint32_t gpio_count = 0U;
  const tars_mcu_pwm_entry_t *pwm = TarsMcuPinmap_GetPwmTable(&pwm_count);
  const tars_mcu_gpio_entry_t *gpio = TarsMcuPinmap_GetGpioTable(&gpio_count);
  uint32_t i;

  if ((out == NULL) || (out_size == 0U))
  {
    return;
  }

  out[0] = '\0';
  (void)snprintf(out,
                 out_size,
                 "mcu spec list (%s):\r\n  pwm:",
                 TarsMcuPinmap_BoardId());

  for (i = 0U; i < pwm_count; i++)
  {
    char line[32];
    (void)snprintf(line, sizeof(line), " %s", pwm[i].channel);
    spec_append(out, out_size, line);
  }

  spec_append(out, out_size, "\r\n  gpio:");
  for (i = 0U; i < gpio_count; i++)
  {
    char line[32];
    (void)snprintf(line, sizeof(line), " %s", gpio[i].pin_name);
    spec_append(out, out_size, line);
  }

  spec_append(out, out_size, "\r\n  timers: tim1 tim9\r\n");
}

static int spec_format_pwm(const char *id, char *out, uint32_t out_size)
{
  const tars_mcu_pwm_entry_t *map = NULL;
  TIM_TypeDef *tim;
  uint32_t design_freq;
  uint32_t runtime_freq;
  uint32_t arr;
  tars_owner_t owner;
  tars_owner_t active;
  uint8_t duty = 0U;
  int boot_enable = 0;

  if (TarsMcuPinmap_ResolvePwm(id, &map) != 0)
  {
    return -1;
  }

  tim = map->tim;
  design_freq = spec_tim_design_freq_hz(map->tim_id);
  runtime_freq = spec_runtime_tim_freq_hz(tim, map->tim_id);
  arr = (tim != NULL) ? tim->ARR : 0U;
  owner = TarsResMgr_GetOwner(map->channel);
  active = TarsResMgr_GetActive(map->channel);
  (void)TarsResPwm_GetDuty(map->channel, &duty);
  (void)TarsResPwm_GetPersist(map->channel, &boot_enable);

  (void)snprintf(out,
                 out_size,
                 "spec %s (pwm):\r\n"
                 "  design: tim=%s chan=%lu pin=%s owner_default=%s "
                 "freq_hz=%lu freq_source=%s shell_freq_mutable=%d pwm_mode=%s\r\n"
                 "  runtime: owner=%s active=%s duty=%u%% boot_enable=%d "
                 "arr=%lu tim_freq_hz=%lu cen=%lu\r\n",
                 map->channel,
                 map->tim_id ? map->tim_id : "?",
                 (unsigned long)((map->hal_channel == TIM_CHANNEL_1)   ? 1U :
                                 (map->hal_channel == TIM_CHANNEL_2)   ? 2U :
                                 (map->hal_channel == TIM_CHANNEL_3)   ? 3U :
                                 (map->hal_channel == TIM_CHANNEL_4)   ? 4U : 0U),
                 map->pin_name ? map->pin_name : "?",
                 TarsOwner_ToString(map->default_owner),
                 (unsigned long)design_freq,
                 (strcmp(map->tim_id, "tim1") == 0) ? "FOC_PARAM_FPWM_HZ" :
                 (strcmp(map->tim_id, "tim9") == 0) ? "TARS_TIM9_PWM_HZ" : "TARS_PWM_DEFAULT_HZ",
                 spec_tim_shell_freq_mutable(map->tim_id),
                 spec_pwm_mode(map->tim_id),
                 TarsOwner_ToString(owner),
                 TarsOwner_ToString(active),
                 (unsigned)duty,
                 boot_enable,
                 (unsigned long)arr,
                 (unsigned long)runtime_freq,
                 (unsigned long)((tim != NULL) &&
                                 ((tim->CR1 & TIM_CR1_CEN) != 0U) ? 1U : 0U));
  return 0;
}

static int spec_format_gpio(const char *id, char *out, uint32_t out_size)
{
  GPIO_TypeDef *port = NULL;
  uint16_t pin = 0U;
  uint32_t gpio_count = 0U;
  const tars_mcu_gpio_entry_t *table = TarsMcuPinmap_GetGpioTable(&gpio_count);
  uint32_t i;
  const tars_mcu_gpio_entry_t *entry = NULL;

  if (TarsMcuPinmap_ResolveGpio(id, &port, &pin) != 0)
  {
    return -1;
  }

  for (i = 0U; i < gpio_count; i++)
  {
    if ((table[i].port == port) && (table[i].hal_pin == pin))
    {
      entry = &table[i];
      break;
    }
  }

  if (entry == NULL)
  {
    return -1;
  }

  (void)snprintf(out,
                 out_size,
                 "spec %s (gpio):\r\n"
                 "  design: alias=%s owner_default=%s\r\n"
                 "  runtime: owner=%s active=%s\r\n",
                 entry->pin_name,
                 (entry->alias != NULL && entry->alias[0] != '\0') ? entry->alias : "-",
                 TarsOwner_ToString(entry->default_owner),
                 TarsOwner_ToString(TarsResMgr_GetOwner(entry->pin_name)),
                 TarsOwner_ToString(TarsResMgr_GetActive(entry->pin_name)));
  return 0;
}

static int spec_format_timer(const char *id, char *out, uint32_t out_size)
{
  TIM_TypeDef *tim = NULL;
  uint32_t design_freq;
  uint32_t runtime_freq;
  uint32_t arr;

  if (strcmp(id, "tim1") == 0)
  {
    tim = TIM1;
  }
  else if (strcmp(id, "tim9") == 0)
  {
    tim = TIM9;
  }
  else
  {
    return -1;
  }

  design_freq = spec_tim_design_freq_hz(id);
  runtime_freq = spec_runtime_tim_freq_hz(tim, id);
  arr = tim->ARR;

  (void)snprintf(out,
                 out_size,
                 "spec %s (timer):\r\n"
                 "  design: freq_hz=%lu pwm_mode=%s shell_freq_mutable=%d "
                 "freq_source=%s\r\n"
                 "  runtime: arr=%lu tim_freq_hz=%lu cen=%lu\r\n",
                 id,
                 (unsigned long)design_freq,
                 spec_pwm_mode(id),
                 spec_tim_shell_freq_mutable(id),
                 (strcmp(id, "tim1") == 0) ? "FOC_PARAM_FPWM_HZ" : "TARS_TIM9_PWM_HZ",
                 (unsigned long)arr,
                 (unsigned long)runtime_freq,
                 (unsigned long)((tim->CR1 & TIM_CR1_CEN) != 0U ? 1U : 0U));
  return 0;
}

int TarsMcu_FormatSpec(const char *id, char *out, uint32_t out_size)
{
  if ((id == NULL) || (out == NULL) || (out_size == 0U))
  {
    return -1;
  }

  out[0] = '\0';

  if (spec_format_pwm(id, out, out_size) == 0)
  {
    return 0;
  }
  if (spec_format_gpio(id, out, out_size) == 0)
  {
    return 0;
  }
  if (spec_format_timer(id, out, out_size) == 0)
  {
    return 0;
  }

  (void)snprintf(out, out_size, "mcu spec: unknown id=%s (try spec list)\r\n", id);
  return -1;
}
