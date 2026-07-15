#include "tars_res_dac.h"
#include "tars_res_mgr.h"
#include "tars_mcu_pinmap.h"
#include "tars_tenant.h"
#include "main.h"
#include <stdio.h>
#include <string.h>

#define TARS_DAC_CH_SLOTS   4U

typedef struct {
  const tars_mcu_dac_entry_t *map;
  uint16_t value;
  uint8_t running;
} tars_dac_ch_t;

static DAC_HandleTypeDef s_hdac;
static uint8_t s_dac_init;
static tars_dac_ch_t s_ch_pool[TARS_DAC_CH_SLOTS];
static uint32_t s_ch_count;

static void dac_enable_port_clock(GPIO_TypeDef *port)
{
  if (port == GPIOA) { __HAL_RCC_GPIOA_CLK_ENABLE(); }
  else if (port == GPIOB) { __HAL_RCC_GPIOB_CLK_ENABLE(); }
  else if (port == GPIOC) { __HAL_RCC_GPIOC_CLK_ENABLE(); }
  else if (port == GPIOD) { __HAL_RCC_GPIOD_CLK_ENABLE(); }
  else if (port == GPIOE) { __HAL_RCC_GPIOE_CLK_ENABLE(); }
  else if (port == GPIOF) { __HAL_RCC_GPIOF_CLK_ENABLE(); }
  else if (port == GPIOG) { __HAL_RCC_GPIOG_CLK_ENABLE(); }
}

void HAL_DAC_MspInit(DAC_HandleTypeDef *hdac)
{
  (void)hdac;
  __HAL_RCC_DAC_CLK_ENABLE();
}

void HAL_DAC_MspDeInit(DAC_HandleTypeDef *hdac)
{
  (void)hdac;
  __HAL_RCC_DAC_CLK_DISABLE();
}

static int dac_find_ch_slot(const char *channel, int create)
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

  if (s_ch_count >= TARS_DAC_CH_SLOTS)
  {
    return -1;
  }

  i = s_ch_count++;
  s_ch_pool[i].map = NULL;
  s_ch_pool[i].value = 0U;
  s_ch_pool[i].running = 0U;
  return (int)i;
}

static int dac_ensure_init(void)
{
  if (s_dac_init != 0U)
  {
    return 0;
  }

  s_hdac.Instance = DAC;
  if (HAL_DAC_Init(&s_hdac) != HAL_OK)
  {
    return -1;
  }

  s_dac_init = 1U;
  return 0;
}

static int dac_config_pin_analog(const tars_mcu_dac_entry_t *map)
{
  GPIO_InitTypeDef gpio = {0};

  dac_enable_port_clock(map->port);
  gpio.Pin = map->hal_pin;
  gpio.Mode = GPIO_MODE_ANALOG;
  gpio.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(map->port, &gpio);
  return 0;
}

static int dac_configure_channel(const tars_mcu_dac_entry_t *map)
{
  DAC_ChannelConfTypeDef cfg = {0};

  if (dac_ensure_init() != 0)
  {
    return -1;
  }

  (void)dac_config_pin_analog(map);

  cfg.DAC_Trigger = DAC_TRIGGER_NONE;
  cfg.DAC_OutputBuffer = DAC_OUTPUTBUFFER_ENABLE;
  if (HAL_DAC_ConfigChannel(&s_hdac, &cfg, map->hal_channel) != HAL_OK)
  {
    return -1;
  }

  return 0;
}

static uint16_t dac_value_from_pct(float level_pct)
{
  uint32_t value;

  if (level_pct < 0.0f)
  {
    level_pct = 0.0f;
  }
  if (level_pct > 100.0f)
  {
    level_pct = 100.0f;
  }

  value = (uint32_t)((level_pct * (float)TARS_DAC_MAX_VALUE) / 100.0f + 0.5f);
  if (value > TARS_DAC_MAX_VALUE)
  {
    value = TARS_DAC_MAX_VALUE;
  }

  return (uint16_t)value;
}

static float dac_pct_from_value(uint16_t value)
{
  return ((float)value * 100.0f) / (float)TARS_DAC_MAX_VALUE;
}

int TarsResDac_Enable(const char *channel, int enable)
{
  const tars_mcu_dac_entry_t *map = NULL;
  int ch_slot;
  tars_dac_ch_t *ch;
  int st;

  if (TarsMcuPinmap_ResolveDac(channel, &map) != 0)
  {
    return TARS_RES_ERR_SCOPE;
  }

  if (TarsResMgr_TenantAssigned(channel) == 0)
  {
    return TARS_RES_ERR_OWNER;
  }

  ch_slot = dac_find_ch_slot(channel, 1);
  if (ch_slot < 0)
  {
    return TARS_RES_ERR_PARAM;
  }

  ch = &s_ch_pool[(uint32_t)ch_slot];

  if (enable == 0)
  {
    if (ch->running != 0U)
    {
      (void)HAL_DAC_Stop(&s_hdac, map->hal_channel);
      (void)TarsResMgr_ReleaseDac(channel);
      ch->running = 0U;
    }
    return 0;
  }

  if (ch->running != 0U)
  {
    return 0;
  }

  st = TarsResMgr_AcquireDac(channel);
  if (st != 0)
  {
    return st;
  }

  ch->map = map;

  if (dac_configure_channel(map) != 0)
  {
    (void)TarsResMgr_ReleaseDac(channel);
    return TARS_RES_ERR_PARAM;
  }

  if (HAL_DAC_SetValue(&s_hdac, map->hal_channel, DAC_ALIGN_12B_R, ch->value) != HAL_OK)
  {
    (void)TarsResMgr_ReleaseDac(channel);
    return TARS_RES_ERR_PARAM;
  }

  if (HAL_DAC_Start(&s_hdac, map->hal_channel) != HAL_OK)
  {
    (void)TarsResMgr_ReleaseDac(channel);
    return TARS_RES_ERR_PARAM;
  }

  ch->running = 1U;
  return 0;
}

int TarsResDac_SetLevel(const char *channel, float level_pct)
{
  const tars_mcu_dac_entry_t *map = NULL;
  int ch_slot;
  tars_dac_ch_t *ch;
  uint16_t value;

  if (TarsMcuPinmap_ResolveDac(channel, &map) != 0)
  {
    return TARS_RES_ERR_SCOPE;
  }

  value = dac_value_from_pct(level_pct);

  ch_slot = dac_find_ch_slot(channel, 1);
  if (ch_slot < 0)
  {
    return TARS_RES_ERR_PARAM;
  }

  ch = &s_ch_pool[(uint32_t)ch_slot];
  ch->map = map;
  ch->value = value;

  if (ch->running == 0U)
  {
    return 0;
  }

  if (HAL_DAC_SetValue(&s_hdac, map->hal_channel, DAC_ALIGN_12B_R, value) != HAL_OK)
  {
    return TARS_RES_ERR_PARAM;
  }

  return 0;
}

int TarsResDac_GetLevel(const char *channel, float *level_pct_out)
{
  int ch_slot;

  if ((channel == NULL) || (level_pct_out == NULL))
  {
    return TARS_RES_ERR_PARAM;
  }

  ch_slot = dac_find_ch_slot(channel, 0);
  if (ch_slot < 0)
  {
    *level_pct_out = 0.0f;
    return 0;
  }

  *level_pct_out = dac_pct_from_value(s_ch_pool[(uint32_t)ch_slot].value);
  return 0;
}

int TarsResDac_IsRunning(const char *channel)
{
  int ch_slot = dac_find_ch_slot(channel, 0);

  if (ch_slot < 0)
  {
    return 0;
  }

  return (s_ch_pool[(uint32_t)ch_slot].running != 0U) ? 1 : 0;
}

int TarsResDac_GetStatus(const char *channel, char *out, uint32_t out_size)
{
  const tars_mcu_dac_entry_t *map = NULL;
  int ch_slot;
  tars_dac_ch_t *ch;
  float level = 0.0f;

  if ((out == NULL) || (out_size == 0U))
  {
    return TARS_RES_ERR_PARAM;
  }

  if (TarsMcuPinmap_ResolveDac(channel, &map) != 0)
  {
    (void)snprintf(out, out_size, "dac: unknown %s\r\n", channel ? channel : "?");
    return TARS_RES_ERR_SCOPE;
  }

  char tenant[TARS_TENANT_LEN];
  char active[TARS_TENANT_LEN];

  ch_slot = dac_find_ch_slot(channel, 0);
  ch = (ch_slot >= 0) ? &s_ch_pool[(uint32_t)ch_slot] : NULL;
  (void)TarsResDac_GetLevel(channel, &level);

  (void)TarsResMgr_GetTenant(channel, tenant, sizeof(tenant));
  (void)TarsResMgr_GetActiveTenant(channel, active, sizeof(active));

  (void)snprintf(out,
                 out_size,
                 "dac: ch=%s pin=%s tenant=%s active=%s run=%u level=%u%% raw=%u\r\n",
                 map->channel,
                 map->pin_name,
                 TarsTenant_Display(tenant),
                 TarsTenant_Display(active),
                 (unsigned)((ch != NULL) ? ch->running : 0U),
                 (unsigned)(level + 0.5f),
                 (unsigned)((ch != NULL) ? ch->value : 0U));
  return 0;
}
