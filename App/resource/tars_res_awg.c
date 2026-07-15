#include "tars_res_awg.h"
#include "tars_res_dac.h"
#include "tars_res_mgr.h"
#include "tars_mcu_pinmap.h"
#include "tars_tenant.h"
#include "tars_platform.h"
#include "main.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* TIM7 (basic timer, APB1) is the shared sample clock. APB1 prescaler != 1 so
 * the timer clock is 2 x APB1 = 72 MHz. TIM6 is the HAL time base (off-limits).
 * dac0 and dac1 each have a circular DMA (Stream5/6) triggered by TIM7 TRGO. */
#define AWG_TIM_CLK_HZ      72000000UL
#define AWG_DEFAULT_POINTS  256U
#define AWG_DEFAULT_FREQ    1000U

#define AWG_MAX_SAMPLE_HZ   1000000UL

typedef struct {
  const tars_mcu_dac_entry_t *map;
  uint32_t dma_channel;
  DMA_Stream_TypeDef *dma_stream;
  uint32_t points;
  uint32_t freq_hz;
  uint32_t sample_hz;
  tars_awg_wave_t wave;
  float ampl_pct;
  float offset_pct;
  float duty_pct;
  uint8_t have_wave;
  uint8_t running;
  uint16_t *buf;
} awg_ch_t;

static DAC_HandleTypeDef s_hdac;
static TIM_HandleTypeDef s_htim;
DMA_HandleTypeDef        hdma_awg0; /* dac0: DMA1_Stream5 */
DMA_HandleTypeDef        hdma_awg1; /* dac1: DMA1_Stream6 */

static uint8_t s_hw_init;
static uint8_t s_tim_running;
static awg_ch_t s_ch[2];

static struct {
  uint8_t  enabled;
  int32_t  offset_samples;
} s_link;

#define AWG_CANON_MAX_POINTS  (TARS_AWG_CH_MAX_POINTS / 2U)

static int awg_slot_for_channel(const char *channel)
{
  if (channel == NULL)
  {
    return -1;
  }
  if (strcmp(channel, "dac0") == 0)
  {
    return 0;
  }
  if (strcmp(channel, "dac1") == 0)
  {
    return 1;
  }
  return -1;
}

static awg_ch_t *awg_ch_init_slot(int slot, const tars_mcu_dac_entry_t *map)
{
  awg_ch_t *ch = &s_ch[slot];

  ch->map = map;
  ch->dma_channel = DMA_CHANNEL_7;
  ch->dma_stream = (slot == 0) ? DMA1_Stream5 : DMA1_Stream6;
  ch->buf = (uint16_t *)(void *)(TARS_AWG_WAVE_BASE +
                                 ((uint32_t)slot * TARS_AWG_CH_STRIDE));

  if (ch->points == 0U)
  {
    ch->points = AWG_DEFAULT_POINTS;
  }
  if (ch->freq_hz == 0U)
  {
    ch->freq_hz = AWG_DEFAULT_FREQ;
  }
  return ch;
}

static DMA_HandleTypeDef *awg_dma_for_slot(int slot)
{
  return (slot == 0) ? &hdma_awg0 : &hdma_awg1;
}

static void awg_dac_clear_underrun(DAC_HandleTypeDef *hdac, uint32_t channel)
{
  if (channel == DAC_CHANNEL_1)
  {
    __HAL_DAC_CLEAR_FLAG(hdac, DAC_FLAG_DMAUDR1);
    __HAL_DAC_DISABLE_IT(hdac, DAC_IT_DMAUDR1);
  }
  else
  {
    __HAL_DAC_CLEAR_FLAG(hdac, DAC_FLAG_DMAUDR2);
    __HAL_DAC_DISABLE_IT(hdac, DAC_IT_DMAUDR2);
  }
}

static void awg_dma_force_off(DMA_HandleTypeDef *hdma)
{
  uint32_t tick;

  if (hdma->Instance == NULL)
  {
    return;
  }

  tick = HAL_GetTick();
  __HAL_DMA_DISABLE(hdma);

  while ((hdma->Instance->CR & DMA_SxCR_EN) != 0U)
  {
    if ((HAL_GetTick() - tick) > 10U)
    {
      hdma->Instance->CR = 0U;
      hdma->Instance->NDTR = 0U;
      hdma->Instance->M0AR = 0U;
      hdma->State = HAL_DMA_STATE_RESET;
      return;
    }
  }

  if (hdma->State != HAL_DMA_STATE_RESET)
  {
    (void)HAL_DMA_DeInit(hdma);
  }
}

static void awg_dma_irq_set(int slot, int enable)
{
  if (slot == 0)
  {
    if (enable != 0)
    {
      HAL_NVIC_EnableIRQ(DMA1_Stream5_IRQn);
    }
    else
    {
      HAL_NVIC_DisableIRQ(DMA1_Stream5_IRQn);
    }
  }
  else
  {
    if (enable != 0)
    {
      HAL_NVIC_EnableIRQ(DMA1_Stream6_IRQn);
    }
    else
    {
      HAL_NVIC_DisableIRQ(DMA1_Stream6_IRQn);
    }
  }
}

static int awg_any_running(void)
{
  return ((s_ch[0].running != 0U) || (s_ch[1].running != 0U)) ? 1 : 0;
}

int TarsResAwg_ParseWave(const char *name, tars_awg_wave_t *out)
{
  if ((name == NULL) || (out == NULL))
  {
    return -1;
  }

  if ((strcmp(name, "sin") == 0) || (strcmp(name, "sine") == 0))
  {
    *out = TARS_AWG_WAVE_SINE;
  }
  else if (strcmp(name, "square") == 0)
  {
    *out = TARS_AWG_WAVE_SQUARE;
  }
  else if ((strcmp(name, "tri") == 0) || (strcmp(name, "triangle") == 0))
  {
    *out = TARS_AWG_WAVE_TRIANGLE;
  }
  else if ((strcmp(name, "saw") == 0) || (strcmp(name, "sawtooth") == 0))
  {
    *out = TARS_AWG_WAVE_SAWTOOTH;
  }
  else if (strcmp(name, "dc") == 0)
  {
    *out = TARS_AWG_WAVE_DC;
  }
  else if (strcmp(name, "noise") == 0)
  {
    *out = TARS_AWG_WAVE_NOISE;
  }
  else
  {
    return -1;
  }

  return 0;
}

static const char *awg_wave_name(tars_awg_wave_t wave)
{
  switch (wave)
  {
  case TARS_AWG_WAVE_SINE:     return "sine";
  case TARS_AWG_WAVE_SQUARE:   return "square";
  case TARS_AWG_WAVE_TRIANGLE: return "triangle";
  case TARS_AWG_WAVE_SAWTOOTH: return "sawtooth";
  case TARS_AWG_WAVE_DC:       return "dc";
  case TARS_AWG_WAVE_NOISE:    return "noise";
  case TARS_AWG_WAVE_CUSTOM:   return "custom";
  default:                     return "?";
  }
}

static uint16_t awg_clamp12(float v)
{
  if (v < 0.0f)
  {
    v = 0.0f;
  }
  if (v > (float)TARS_DAC_MAX_VALUE)
  {
    v = (float)TARS_DAC_MAX_VALUE;
  }
  return (uint16_t)(v + 0.5f);
}

static void awg_fill_table_dest(const awg_ch_t *ch, uint16_t *dest)
{
  const float full = (float)TARS_DAC_MAX_VALUE;
  float center = (ch->offset_pct / 100.0f) * full;
  float peak = (ch->ampl_pct / 100.0f) * (full / 2.0f);
  uint32_t n = ch->points;
  uint32_t i;

  for (i = 0U; i < n; i++)
  {
    float v;

    switch (ch->wave)
    {
    case TARS_AWG_WAVE_SINE:
      v = center + peak * sinf((2.0f * (float)M_PI * (float)i) / (float)n);
      break;

    case TARS_AWG_WAVE_SQUARE:
    {
      uint32_t hi = (uint32_t)(((float)n * ch->duty_pct) / 100.0f + 0.5f);
      v = (i < hi) ? (center + peak) : (center - peak);
      break;
    }

    case TARS_AWG_WAVE_TRIANGLE:
    {
      float half = (float)n / 2.0f;
      float frac = (i < (uint32_t)half)
                     ? ((float)i / half)
                     : (2.0f - ((float)i / half));
      v = (center - peak) + (2.0f * peak * frac);
      break;
    }

    case TARS_AWG_WAVE_SAWTOOTH:
      v = (center - peak) + (2.0f * peak * ((float)i / (float)n));
      break;

    case TARS_AWG_WAVE_NOISE:
    {
      float r = ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f;
      v = center + peak * r;
      break;
    }

    case TARS_AWG_WAVE_DC:
    default:
      v = center;
      break;
    }

    dest[i] = awg_clamp12(v);
  }
}

static void awg_canon_snapshot(awg_ch_t *ch)
{
  if ((ch->points < 2U) || (ch->points > AWG_CANON_MAX_POINTS))
  {
    return;
  }

  memcpy((void *)(ch->buf + ch->points),
         (const void *)ch->buf,
         ch->points * sizeof(uint16_t));
}

static void awg_fill_table(awg_ch_t *ch)
{
  awg_fill_table_dest(ch, ch->buf);
  ch->have_wave = 1U;
  awg_canon_snapshot(ch);
}

static int32_t awg_link_norm_offset(int32_t off, uint32_t points)
{
  int32_t n;

  if (points == 0U)
  {
    return 0;
  }

  n = (int32_t)points;
  off %= n;
  if (off < 0)
  {
    off += n;
  }
  return off;
}

static uint32_t awg_dma_next_index(const awg_ch_t *ch)
{
  int slot = (int)(ch - s_ch);
  DMA_HandleTypeDef *hdma = awg_dma_for_slot(slot);
  uint32_t ndtr;

  if (ch->running == 0U)
  {
    return 0U;
  }

  ndtr = __HAL_DMA_GET_COUNTER(hdma);
  if (ndtr == 0U)
  {
    return 0U;
  }

  return (ch->points - ndtr) % ch->points;
}

static void awg_rotate_table(awg_ch_t *ch, const uint16_t *canon, uint32_t rot)
{
  uint32_t n = ch->points;
  uint32_t i;

  for (i = 0U; i < n; i++)
  {
    ch->buf[i] = canon[(i + rot) % n];
  }
}

static int awg_canon_into_scratch(awg_ch_t *ch, uint16_t **canon_out)
{
  uint16_t *scratch;

  if (canon_out == NULL)
  {
    return TARS_RES_ERR_PARAM;
  }

  if (ch->points < 2U)
  {
    return TARS_RES_ERR_PARAM;
  }

  if (ch->points > AWG_CANON_MAX_POINTS)
  {
    return TARS_RES_ERR_PARAM;
  }

  scratch = ch->buf + ch->points;

  if (ch->wave == TARS_AWG_WAVE_NOISE)
  {
    return TARS_RES_ERR_PARAM;
  }

  if (ch->wave == TARS_AWG_WAVE_CUSTOM)
  {
    *canon_out = scratch;
    return 0;
  }

  awg_fill_table_dest(ch, scratch);
  *canon_out = scratch;
  return 0;
}

static int awg_link_align_follower(void)
{
  awg_ch_t *master = &s_ch[0];
  awg_ch_t *follow = &s_ch[1];
  uint16_t *canon;
  uint32_t i0;
  uint32_t rot;
  int st;

  if (s_link.enabled == 0U)
  {
    return 0;
  }

  if (master->running == 0U)
  {
    return TARS_RES_ERR_ACTIVE;
  }

  if (follow->have_wave == 0U)
  {
    return TARS_RES_ERR_PARAM;
  }

  if (master->points != follow->points)
  {
    return TARS_RES_ERR_PARAM;
  }

  st = awg_canon_into_scratch(follow, &canon);
  if (st != 0)
  {
    return st;
  }

  i0 = awg_dma_next_index(master);
  rot = (uint32_t)awg_link_norm_offset(s_link.offset_samples + (int32_t)i0,
                                       follow->points);
  awg_rotate_table(follow, canon, rot);
  return 0;
}

static uint32_t awg_calc_timer(uint32_t sample_hz, uint16_t *psc_out, uint32_t *arr_out)
{
  uint32_t div;
  uint32_t psc = 0U;
  uint32_t arr;

  if (sample_hz == 0U)
  {
    sample_hz = 1U;
  }
  if (sample_hz > AWG_MAX_SAMPLE_HZ)
  {
    sample_hz = AWG_MAX_SAMPLE_HZ;
  }

  div = AWG_TIM_CLK_HZ / sample_hz;
  if (div < 1U)
  {
    div = 1U;
  }

  while ((div / (psc + 1U)) > 65536U)
  {
    psc++;
  }

  arr = div / (psc + 1U);
  if (arr < 1U)
  {
    arr = 1U;
  }

  *psc_out = (uint16_t)psc;
  *arr_out = arr - 1U;
  return AWG_TIM_CLK_HZ / ((psc + 1U) * arr);
}

/* Highest sample clock needed by any running channel (freq * points). */
static uint32_t awg_required_sample_hz(void)
{
  uint32_t max_hz = 0U;
  uint32_t i;

  for (i = 0U; i < 2U; i++)
  {
    uint32_t req;

    if (s_ch[i].running == 0U)
    {
      continue;
    }

    req = s_ch[i].freq_hz * s_ch[i].points;
    if (req > max_hz)
    {
      max_hz = req;
    }
  }

  return max_hz;
}

static int awg_hw_init(void)
{
  if (s_hw_init != 0U)
  {
    return 0;
  }

  __HAL_RCC_DMA1_CLK_ENABLE();
  __HAL_RCC_TIM7_CLK_ENABLE();

  s_hdac.Instance = DAC;
  if (HAL_DAC_Init(&s_hdac) != HAL_OK)
  {
    return -1;
  }

  s_hw_init = 1U;
  return 0;
}

static void awg_config_pin_analog(const tars_mcu_dac_entry_t *map)
{
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();
  gpio.Pin = map->hal_pin;
  gpio.Mode = GPIO_MODE_ANALOG;
  gpio.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(map->port, &gpio);
}

static int awg_tim_apply(void)
{
  TIM_MasterConfigTypeDef master = {0};
  uint16_t psc = 0U;
  uint32_t arr = 0U;
  uint32_t sample_hz;
  uint32_t i;

  sample_hz = awg_required_sample_hz();
  if (sample_hz == 0U)
  {
    return 0;
  }

  sample_hz = awg_calc_timer(sample_hz, &psc, &arr);

  for (i = 0U; i < 2U; i++)
  {
    if (s_ch[i].running != 0U)
    {
      s_ch[i].sample_hz = sample_hz;
    }
  }

  if (s_tim_running == 0U)
  {
    s_htim.Instance = TIM7;
    s_htim.Init.Prescaler = psc;
    s_htim.Init.CounterMode = TIM_COUNTERMODE_UP;
    s_htim.Init.Period = arr;
    s_htim.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    if (HAL_TIM_Base_Init(&s_htim) != HAL_OK)
    {
      return TARS_RES_ERR_PARAM;
    }

    master.MasterOutputTrigger = TIM_TRGO_UPDATE;
    master.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
    if (HAL_TIMEx_MasterConfigSynchronization(&s_htim, &master) != HAL_OK)
    {
      return TARS_RES_ERR_PARAM;
    }

    if (HAL_TIM_Base_Start(&s_htim) != HAL_OK)
    {
      return TARS_RES_ERR_PARAM;
    }

    s_tim_running = 1U;
  }
  else
  {
    __HAL_TIM_SET_PRESCALER(&s_htim, psc);
    __HAL_TIM_SET_AUTORELOAD(&s_htim, arr);
  }

  return 0;
}

static void awg_tim_stop_if_idle(void)
{
  if (awg_any_running() != 0)
  {
    return;
  }

  if (s_tim_running != 0U)
  {
    (void)HAL_TIM_Base_Stop(&s_htim);
    (void)HAL_TIM_Base_DeInit(&s_htim);
    s_tim_running = 0U;
  }
}

static void awg_tim_hold(void)
{
  if (s_tim_running != 0U)
  {
    __HAL_TIM_DISABLE(&s_htim);
  }
}

static void awg_tim_release(void)
{
  if (s_tim_running != 0U)
  {
    __HAL_TIM_ENABLE(&s_htim);
  }
}

static void awg_ch_stop(awg_ch_t *ch)
{
  int slot = (int)(ch - s_ch);
  DMA_HandleTypeDef *hdma;
  uint32_t dmaen;

  if (ch->running == 0U)
  {
    return;
  }

  hdma = awg_dma_for_slot(slot);
  dmaen = (ch->map->hal_channel == DAC_CHANNEL_1) ? DAC_CR_DMAEN1 : DAC_CR_DMAEN2;

  awg_dma_irq_set(slot, 0);
  CLEAR_BIT(s_hdac.Instance->CR, dmaen);
  __HAL_DAC_DISABLE(&s_hdac, ch->map->hal_channel);
  awg_dac_clear_underrun(&s_hdac, ch->map->hal_channel);
  awg_dma_force_off(hdma);
  s_hdac.State = HAL_DAC_STATE_READY;

  ch->running = 0U;

  if (awg_any_running() != 0)
  {
    (void)awg_tim_apply();
  }
  else
  {
    awg_tim_stop_if_idle();
  }
}

static int awg_dma_setup(awg_ch_t *ch)
{
  DMA_HandleTypeDef *hdma;
  int slot = (int)(ch - s_ch);

  hdma = awg_dma_for_slot(slot);
  awg_dma_force_off(hdma);

  hdma->Instance = ch->dma_stream;
  hdma->Init.Channel = ch->dma_channel;
  hdma->Init.Direction = DMA_MEMORY_TO_PERIPH;
  hdma->Init.PeriphInc = DMA_PINC_DISABLE;
  hdma->Init.MemInc = DMA_MINC_ENABLE;
  hdma->Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
  hdma->Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
  hdma->Init.Mode = DMA_CIRCULAR;
  hdma->Init.Priority = DMA_PRIORITY_HIGH;
  hdma->Init.FIFOMode = DMA_FIFOMODE_DISABLE;
  if (HAL_DMA_Init(hdma) != HAL_OK)
  {
    return TARS_RES_ERR_PARAM;
  }

  if (ch->map->hal_channel == DAC_CHANNEL_1)
  {
    __HAL_LINKDMA(&s_hdac, DMA_Handle1, hdma_awg0);
  }
  else
  {
    __HAL_LINKDMA(&s_hdac, DMA_Handle2, hdma_awg1);
  }

  awg_dma_irq_set(slot, 1);
  if (slot == 0)
  {
    HAL_NVIC_SetPriority(DMA1_Stream5_IRQn, 6, 0);
  }
  else
  {
    HAL_NVIC_SetPriority(DMA1_Stream6_IRQn, 6, 0);
  }
  return 0;
}

static int awg_ch_start(awg_ch_t *ch)
{
  DAC_ChannelConfTypeDef cfg = {0};
  DMA_HandleTypeDef *hdma;
  int slot;
  int st;

  if (awg_hw_init() != 0)
  {
    return TARS_RES_ERR_PARAM;
  }

  slot = (int)(ch - s_ch);
  hdma = awg_dma_for_slot(slot);

  awg_config_pin_analog(ch->map);

  st = awg_dma_setup(ch);
  if (st != 0)
  {
    return st;
  }

  cfg.DAC_Trigger = DAC_TRIGGER_T7_TRGO;
  cfg.DAC_OutputBuffer = DAC_OUTPUTBUFFER_ENABLE;
  if (HAL_DAC_ConfigChannel(&s_hdac, &cfg, ch->map->hal_channel) != HAL_OK)
  {
    awg_dma_force_off(hdma);
    return TARS_RES_ERR_PARAM;
  }

  awg_dac_clear_underrun(&s_hdac, ch->map->hal_channel);

  if (HAL_DAC_Start_DMA(&s_hdac,
                        ch->map->hal_channel,
                        (uint32_t *)(void *)ch->buf,
                        ch->points,
                        DAC_ALIGN_12B_R) != HAL_OK)
  {
    awg_dma_force_off(hdma);
    return TARS_RES_ERR_PARAM;
  }

  __HAL_DMA_DISABLE_IT(hdma, DMA_IT_HT | DMA_IT_TC);
  awg_dac_clear_underrun(&s_hdac, ch->map->hal_channel);

  ch->running = 1U;
  st = awg_tim_apply();
  if (st != 0)
  {
    awg_ch_stop(ch);
    return st;
  }

  return 0;
}

int TarsResAwg_Generate(const char *channel,
                        tars_awg_wave_t wave,
                        uint32_t points,
                        uint32_t freq_hz,
                        float ampl_pct,
                        float offset_pct,
                        float duty_pct)
{
  const tars_mcu_dac_entry_t *map = NULL;
  int slot = awg_slot_for_channel(channel);
  awg_ch_t *ch;

  if (slot < 0)
  {
    return TARS_RES_ERR_SCOPE;
  }
  if (TarsMcuPinmap_ResolveDac(channel, &map) != 0)
  {
    return TARS_RES_ERR_SCOPE;
  }
  if ((points < 2U) || (points > TARS_AWG_CH_MAX_POINTS))
  {
    return TARS_RES_ERR_PARAM;
  }

  ch = awg_ch_init_slot(slot, map);
  ch->wave = wave;
  ch->points = points;
  ch->freq_hz = (freq_hz > 0U) ? freq_hz : AWG_DEFAULT_FREQ;
  ch->ampl_pct = ampl_pct;
  ch->offset_pct = offset_pct;
  ch->duty_pct = duty_pct;

  awg_fill_table(ch);

  if (ch->running != 0U)
  {
    awg_ch_stop(ch);
    return awg_ch_start(ch);
  }

  return 0;
}

int TarsResAwg_SetFreq(const char *channel, uint32_t freq_hz)
{
  int slot = awg_slot_for_channel(channel);
  awg_ch_t *ch;

  if (slot < 0)
  {
    return TARS_RES_ERR_SCOPE;
  }
  if (freq_hz == 0U)
  {
    return TARS_RES_ERR_PARAM;
  }

  ch = &s_ch[slot];
  ch->freq_hz = freq_hz;

  if (ch->running == 0U)
  {
    return 0;
  }

  return awg_tim_apply();
}

int TarsResAwg_Enable(const char *channel, int enable)
{
  const tars_mcu_dac_entry_t *map = NULL;
  int slot = awg_slot_for_channel(channel);
  awg_ch_t *ch;
  int st;

  if (slot < 0)
  {
    return TARS_RES_ERR_SCOPE;
  }
  if (TarsMcuPinmap_ResolveDac(channel, &map) != 0)
  {
    return TARS_RES_ERR_SCOPE;
  }
  if (TarsResMgr_TenantAssigned(channel) == 0)
  {
    return TARS_RES_ERR_OWNER;
  }

  ch = awg_ch_init_slot(slot, map);

  if (enable == 0)
  {
    if (ch->running != 0U)
    {
      awg_ch_stop(ch);
      (void)TarsResMgr_ReleaseDac(channel);
    }
    return 0;
  }

  if (ch->running != 0U)
  {
    return 0;
  }

  if (ch->have_wave == 0U)
  {
    ch->wave = TARS_AWG_WAVE_SINE;
    ch->ampl_pct = 100.0f;
    ch->offset_pct = 50.0f;
    ch->duty_pct = 50.0f;
    awg_fill_table(ch);
  }

  st = TarsResMgr_AcquireDac(channel);
  if (st != 0)
  {
    return st;
  }

  if ((slot == 1) && (s_link.enabled != 0U))
  {
    awg_tim_hold();
    st = awg_link_align_follower();
    if (st != 0)
    {
      awg_tim_release();
      (void)TarsResMgr_ReleaseDac(channel);
      return st;
    }
  }

  st = awg_ch_start(ch);
  if ((slot == 1) && (s_link.enabled != 0U))
  {
    awg_tim_release();
  }
  if (st != 0)
  {
    (void)TarsResMgr_ReleaseDac(channel);
    return st;
  }

  return 0;
}

int TarsResAwg_UploadBegin(const char *channel,
                           uint32_t points,
                           uint8_t **buf_out,
                           uint32_t *bytes_out)
{
  const tars_mcu_dac_entry_t *map = NULL;
  int slot = awg_slot_for_channel(channel);
  awg_ch_t *ch;

  if ((buf_out == NULL) || (bytes_out == NULL))
  {
    return TARS_RES_ERR_PARAM;
  }
  if (slot < 0)
  {
    return TARS_RES_ERR_SCOPE;
  }
  if (TarsMcuPinmap_ResolveDac(channel, &map) != 0)
  {
    return TARS_RES_ERR_SCOPE;
  }
  if ((points < 2U) || (points > TARS_AWG_CH_MAX_POINTS))
  {
    return TARS_RES_ERR_PARAM;
  }

  ch = awg_ch_init_slot(slot, map);
  if (ch->running != 0U)
  {
    return TARS_RES_ERR_ACTIVE;
  }

  ch->wave = TARS_AWG_WAVE_CUSTOM;
  ch->points = points;
  ch->ampl_pct = 100.0f;
  ch->offset_pct = 50.0f;
  ch->duty_pct = 50.0f;
  ch->have_wave = 0U;

  *buf_out = (uint8_t *)(void *)ch->buf;
  *bytes_out = points * 2U;
  return 0;
}

int TarsResAwg_UploadComplete(const char *channel)
{
  int slot = awg_slot_for_channel(channel);
  awg_ch_t *ch;
  uint32_t i;

  if (slot < 0)
  {
    return TARS_RES_ERR_SCOPE;
  }

  ch = &s_ch[slot];
  if ((ch->buf == NULL) || (ch->points < 2U))
  {
    return TARS_RES_ERR_PARAM;
  }

  for (i = 0U; i < ch->points; i++)
  {
    ch->buf[i] &= (uint16_t)TARS_DAC_MAX_VALUE;
  }

  ch->wave = TARS_AWG_WAVE_CUSTOM;
  ch->have_wave = 1U;
  awg_canon_snapshot(ch);
  return 0;
}

int TarsResAwg_IsRunning(const char *channel)
{
  int slot = awg_slot_for_channel(channel);

  if (slot < 0)
  {
    return 0;
  }

  return (s_ch[slot].running != 0U) ? 1 : 0;
}

int TarsResAwg_GetStatus(const char *channel, char *out, uint32_t out_size)
{
  const tars_mcu_dac_entry_t *map = NULL;
  int slot = awg_slot_for_channel(channel);
  awg_ch_t *ch;
  uint32_t out_hz;

  if ((out == NULL) || (out_size == 0U))
  {
    return TARS_RES_ERR_PARAM;
  }
  if ((slot < 0) || (TarsMcuPinmap_ResolveDac(channel, &map) != 0))
  {
    (void)snprintf(out, out_size, "awg: unknown %s\r\n", channel ? channel : "?");
    return TARS_RES_ERR_SCOPE;
  }

  ch = &s_ch[slot];
  out_hz = (ch->sample_hz > 0U && ch->points > 0U) ? (ch->sample_hz / ch->points) : 0U;

  (void)snprintf(out,
                 out_size,
                 "awg: ch=%s pin=%s run=%u wave=%s points=%lu freq=%luHz "
                 "out=%luHz sample=%luHz ampl=%u%% offset=%u%% duty=%u%%\r\n",
                 map->channel,
                 map->pin_name,
                 (unsigned)ch->running,
                 awg_wave_name(ch->wave),
                 (unsigned long)ch->points,
                 (unsigned long)ch->freq_hz,
                 (unsigned long)out_hz,
                 (unsigned long)ch->sample_hz,
                 (unsigned)(ch->ampl_pct + 0.5f),
                 (unsigned)(ch->offset_pct + 0.5f),
                 (unsigned)(ch->duty_pct + 0.5f));
  return 0;
}

int TarsResAwg_LinkSet(int enable, int32_t offset_samples)
{
  s_link.enabled = (enable != 0) ? 1U : 0U;
  s_link.offset_samples = offset_samples;
  return 0;
}

int TarsResAwg_LinkEnable(int enable)
{
  s_link.enabled = (enable != 0) ? 1U : 0U;
  return 0;
}

int TarsResAwg_LinkSetOffset(int32_t offset_samples)
{
  s_link.offset_samples = offset_samples;
  return 0;
}

int TarsResAwg_LinkResync(void)
{
  awg_ch_t *follow = &s_ch[1];
  uint8_t was_running = follow->running;
  int st;

  if (s_link.enabled == 0U)
  {
    return TARS_RES_ERR_PARAM;
  }

  if (s_ch[0].running == 0U)
  {
    return TARS_RES_ERR_ACTIVE;
  }

  if (follow->running != 0U)
  {
    awg_tim_hold();
    awg_ch_stop(follow);
  }

  if (follow->have_wave == 0U)
  {
    awg_tim_release();
    return TARS_RES_ERR_PARAM;
  }

  if (TarsResMgr_TenantAssigned("dac1") == 0)
  {
    awg_tim_release();
    return TARS_RES_ERR_OWNER;
  }

  if (was_running == 0U)
  {
    st = TarsResMgr_AcquireDac("dac1");
    if (st != 0)
    {
      awg_tim_release();
      return st;
    }
  }

  st = awg_link_align_follower();
  if (st != 0)
  {
    awg_tim_release();
    if (was_running == 0U)
    {
      (void)TarsResMgr_ReleaseDac("dac1");
    }
    return st;
  }

  st = awg_ch_start(follow);
  awg_tim_release();
  if (st != 0)
  {
    if (was_running == 0U)
    {
      (void)TarsResMgr_ReleaseDac("dac1");
    }
    return st;
  }

  return 0;
}

int TarsResAwg_LinkGetStatus(char *out, uint32_t out_size)
{
  uint32_t idx0 = 0U;
  uint32_t idx1 = 0U;
  int32_t skew = 0;

  if ((out == NULL) || (out_size == 0U))
  {
    return TARS_RES_ERR_PARAM;
  }

  if (s_ch[0].running != 0U)
  {
    idx0 = awg_dma_next_index(&s_ch[0]);
  }
  if (s_ch[1].running != 0U)
  {
    idx1 = awg_dma_next_index(&s_ch[1]);
  }

  if ((s_ch[0].running != 0U) && (s_ch[1].running != 0U) && (s_ch[1].points > 0U))
  {
    skew = awg_link_norm_offset((int32_t)idx0 - (int32_t)idx1, s_ch[1].points);
  }

  (void)snprintf(out,
                 out_size,
                 "awg link: on=%u offset=%ld idx0=%lu idx1=%lu skew=%ld points0=%lu points1=%lu\r\n",
                 (unsigned)s_link.enabled,
                 (long)s_link.offset_samples,
                 (unsigned long)idx0,
                 (unsigned long)idx1,
                 (long)skew,
                 (unsigned long)s_ch[0].points,
                 (unsigned long)s_ch[1].points);
  return 0;
}
