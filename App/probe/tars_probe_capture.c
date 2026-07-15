#include "tars_probe_capture.h"
#include "tars_probe_uart.h"
#include "tars_platform.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#define PROBE_CAP_DEFAULT_DEPTH   (64U * 1024U)
#define PROBE_CAP_DEFAULT_RATE    1000000U   /* 1 Msps */
#define PROBE_CAP_TX_CHUNK        (16U * 1024U)

static uint16_t *const s_cap_buf = (uint16_t *)TARS_PROBE_CAP_BASE;
static uint32_t s_depth_bytes = PROBE_CAP_DEFAULT_DEPTH;
static uint32_t s_rate_hz = PROBE_CAP_DEFAULT_RATE;
static volatile tars_probe_cap_state_t s_state = TARS_PROBE_CAP_IDLE;

void TarsProbeCapture_Init(void)
{
  s_depth_bytes = PROBE_CAP_DEFAULT_DEPTH;
  s_rate_hz = PROBE_CAP_DEFAULT_RATE;
  s_state = TARS_PROBE_CAP_IDLE;
}

void TarsProbeCapture_SetDepth(uint32_t bytes)
{
  if (bytes < 2U)
  {
    bytes = 2U;
  }

  if (bytes > TARS_PROBE_CAP_SIZE)
  {
    bytes = TARS_PROBE_CAP_SIZE;
  }

  s_depth_bytes = bytes & ~1U; /* keep whole uint16 samples */
}

void TarsProbeCapture_SetRate(uint32_t hz)
{
  if (hz == 0U)
  {
    hz = 1U;
  }

  s_rate_hz = hz;
}

uint32_t TarsProbeCapture_Depth(void)
{
  return s_depth_bytes;
}

uint32_t TarsProbeCapture_Rate(void)
{
  return s_rate_hz;
}

tars_probe_cap_state_t TarsProbeCapture_State(void)
{
  return s_state;
}

int TarsProbeCapture_Trigger(void)
{
  uint32_t samples = s_depth_bytes / 2U;
  uint32_t i;
  float phase_step;

  /* Synthetic source: a few cycles of sine across the capture window with a
   * little deterministic dither. Replace this loop with ADC+DMA later; the
   * downstream streaming path stays the same. */
  phase_step = (2.0f * 3.14159265f * 16.0f) / (float)((samples > 0U) ? samples : 1U);

  for (i = 0U; i < samples; i++)
  {
    float v = sinf((float)i * phase_step);
    int32_t s = (int32_t)(2048.0f + (1800.0f * v)) + (int32_t)(i & 0x07U);

    if (s < 0)
    {
      s = 0;
    }
    else if (s > 4095)
    {
      s = 4095;
    }

    s_cap_buf[i] = (uint16_t)s;
  }

  s_state = TARS_PROBE_CAP_READY;
  return 0;
}

int TarsProbeCapture_Stream(void)
{
  char header[32];
  uint32_t len = s_depth_bytes;
  char len_str[16];
  int n;
  const uint8_t *p = (const uint8_t *)s_cap_buf;
  uint32_t sent = 0U;

  if (s_state != TARS_PROBE_CAP_READY)
  {
    TarsProbeUart_SendStr("CAP:ERR no-data\r\n");
    return -1;
  }

  /* SCPI definite-length block: #<ndigits><length><raw bytes> */
  n = snprintf(len_str, sizeof(len_str), "%lu", (unsigned long)len);
  (void)snprintf(header, sizeof(header), "#%d%s", n, len_str);
  (void)TarsProbeUart_SendStr(header);

  while (sent < len)
  {
    uint32_t chunk = len - sent;
    if (chunk > PROBE_CAP_TX_CHUNK)
    {
      chunk = PROBE_CAP_TX_CHUNK;
    }

    if (TarsProbeUart_Send(p + sent, chunk) < 0)
    {
      return -1;
    }

    sent += chunk;
  }

  TarsProbeUart_SendStr("\r\n");
  return 0;
}
