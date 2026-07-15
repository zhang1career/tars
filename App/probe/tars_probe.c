#include "tars_probe.h"
#include "tars_probe_uart.h"
#include "tars_probe_capture.h"
#include "tars_probe_metrics.h"
#include "tars_platform.h"
#include "cmsis_os.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

#define PROBE_LINE_MAX        96U
#define PROBE_OUT_MAX         384U
#define PROBE_TELEM_DEFAULT   1000U

static char s_line[PROBE_LINE_MAX];
static uint16_t s_line_len;

static uint8_t s_telem_on = 1U;
static uint32_t s_telem_period_ms = PROBE_TELEM_DEFAULT;

static void probe_upper(char *s)
{
  while (*s != '\0')
  {
    *s = (char)toupper((unsigned char)*s);
    s++;
  }
}

static void probe_trim(char *s)
{
  size_t n = strlen(s);

  while (n > 0U && (s[n - 1U] == '\r' || s[n - 1U] == '\n' ||
                    s[n - 1U] == ' ' || s[n - 1U] == '\t'))
  {
    s[--n] = '\0';
  }
}

static const tars_probe_metric_t *probe_find_metric(const char *name)
{
  uint32_t i;

  for (i = 0U; i < g_probe_metric_count; i++)
  {
    if (strcasecmp(g_probe_metrics[i].name, name) == 0)
    {
      return &g_probe_metrics[i];
    }
  }

  return NULL;
}

static void probe_build_all_metrics(char *out, uint32_t out_size, const char *prefix)
{
  uint32_t i;
  int w = 0;

  out[0] = '\0';
  if (prefix != NULL)
  {
    w = snprintf(out, out_size, "%s", prefix);
  }

  for (i = 0U; i < g_probe_metric_count; i++)
  {
    float v = TarsProbeMetrics_Read(g_probe_metrics[i].provider, g_probe_metrics[i].arg);

    w += snprintf(out + w,
                  (w < (int)out_size) ? (out_size - (uint32_t)w) : 0U,
                  "%s%s=%.3f",
                  (i == 0U) ? "" : ",",
                  g_probe_metrics[i].name,
                  (double)v);

    if ((uint32_t)w >= out_size)
    {
      break;
    }
  }

  (void)strncat(out, "\r\n", out_size - strlen(out) - 1U);
}

static void probe_dispatch(char *line)
{
  char out[PROBE_OUT_MAX];

  probe_trim(line);
  if (line[0] == '\0')
  {
    return;
  }

  /* Uppercase a working copy for keyword matching; keep args from original. */
  {
    char up[PROBE_LINE_MAX];

    strncpy(up, line, sizeof(up));
    up[sizeof(up) - 1U] = '\0';
    probe_upper(up);

    if (strcmp(up, "*IDN?") == 0)
    {
      (void)snprintf(out, sizeof(out), "TARS,PROBE,%s,1.0\r\n", TARS_BOARD_ID);
      TarsProbeUart_SendStr(out);
      return;
    }

    if (strcmp(up, "*RST") == 0)
    {
      TarsProbeCapture_Init();
      s_telem_on = 1U;
      s_telem_period_ms = PROBE_TELEM_DEFAULT;
      TarsProbeUart_SendStr("OK\r\n");
      return;
    }

    if (strcmp(up, "*TRG") == 0 || strcmp(up, "CAP:TRIG") == 0)
    {
      (void)TarsProbeCapture_Trigger();
      (void)TarsProbeCapture_Stream();
      return;
    }

    if (strcmp(up, "CAP:FETC?") == 0)
    {
      (void)TarsProbeCapture_Stream();
      return;
    }

    if (strcmp(up, "CAP:STAT?") == 0)
    {
      (void)snprintf(out, sizeof(out), "%s\r\n",
                     (TarsProbeCapture_State() == TARS_PROBE_CAP_READY) ? "READY" : "IDLE");
      TarsProbeUart_SendStr(out);
      return;
    }

    if (strcmp(up, "CAP:DEPT?") == 0)
    {
      (void)snprintf(out, sizeof(out), "%lu\r\n", (unsigned long)TarsProbeCapture_Depth());
      TarsProbeUart_SendStr(out);
      return;
    }

    if (strcmp(up, "CAP:RATE?") == 0)
    {
      (void)snprintf(out, sizeof(out), "%lu\r\n", (unsigned long)TarsProbeCapture_Rate());
      TarsProbeUart_SendStr(out);
      return;
    }

    if (strncmp(up, "CAP:DEPT ", 9) == 0)
    {
      TarsProbeCapture_SetDepth((uint32_t)strtoul(line + 9, NULL, 0));
      TarsProbeUart_SendStr("OK\r\n");
      return;
    }

    if (strncmp(up, "CAP:RATE ", 9) == 0)
    {
      TarsProbeCapture_SetRate((uint32_t)strtoul(line + 9, NULL, 0));
      TarsProbeUart_SendStr("OK\r\n");
      return;
    }

    if (strcmp(up, "MEAS:ALL?") == 0)
    {
      probe_build_all_metrics(out, sizeof(out), NULL);
      TarsProbeUart_SendStr(out);
      return;
    }

    if (strncmp(up, "MEAS? ", 6) == 0)
    {
      const tars_probe_metric_t *m = probe_find_metric(line + 6);

      if (m == NULL)
      {
        TarsProbeUart_SendStr("ERR unknown-metric\r\n");
      }
      else
      {
        (void)snprintf(out, sizeof(out), "%.3f\r\n",
                       (double)TarsProbeMetrics_Read(m->provider, m->arg));
        TarsProbeUart_SendStr(out);
      }
      return;
    }

    if (strcmp(up, "METR:LIST?") == 0)
    {
      uint32_t i;
      for (i = 0U; i < g_probe_metric_count; i++)
      {
        (void)snprintf(out, sizeof(out), "%s,%s\r\n",
                       g_probe_metrics[i].name, g_probe_metrics[i].unit);
        TarsProbeUart_SendStr(out);
      }
      return;
    }

    if (strcmp(up, "TELE:STAT?") == 0)
    {
      TarsProbeUart_SendStr(s_telem_on ? "ON\r\n" : "OFF\r\n");
      return;
    }

    if (strcmp(up, "TELE:STAT ON") == 0)
    {
      s_telem_on = 1U;
      TarsProbeUart_SendStr("OK\r\n");
      return;
    }

    if (strcmp(up, "TELE:STAT OFF") == 0)
    {
      s_telem_on = 0U;
      TarsProbeUart_SendStr("OK\r\n");
      return;
    }

    if (strcmp(up, "TELE:PER?") == 0)
    {
      (void)snprintf(out, sizeof(out), "%lu\r\n", (unsigned long)s_telem_period_ms);
      TarsProbeUart_SendStr(out);
      return;
    }

    if (strncmp(up, "TELE:PER ", 9) == 0)
    {
      uint32_t p = (uint32_t)strtoul(line + 9, NULL, 0);
      s_telem_period_ms = (p < 50U) ? 50U : p;
      TarsProbeUart_SendStr("OK\r\n");
      return;
    }

    TarsProbeUart_SendStr("ERR unknown-cmd\r\n");
  }
}

static void probe_rx_pump(void)
{
  uint8_t ch;

  while (TarsProbeUart_GetByte(&ch) != 0)
  {
    if (ch == '\n' || ch == '\r')
    {
      if (s_line_len > 0U)
      {
        s_line[s_line_len] = '\0';
        probe_dispatch(s_line);
        s_line_len = 0U;
      }
      continue;
    }

    if (s_line_len < (PROBE_LINE_MAX - 1U))
    {
      s_line[s_line_len++] = (char)ch;
    }
    else
    {
      s_line_len = 0U; /* overflow: drop line */
    }
  }
}

void TarsProbe_Init(void)
{
  s_line_len = 0U;
  s_telem_on = 1U;
  s_telem_period_ms = PROBE_TELEM_DEFAULT;

  TarsProbeUart_Init();
  TarsProbeCapture_Init();
}

void TarsProbe_Task(void const *argument)
{
  uint32_t telem_accum_ms = 0U;
  const uint32_t tick_ms = 10U;

  (void)argument;

  osDelay(300); /* let platform init settle */
  TarsProbe_Init();

  TarsProbeUart_SendStr("\r\nTARS probe ready (SCPI 115200). Try *IDN?\r\n");

  for (;;)
  {
    probe_rx_pump();

    if (s_telem_on != 0U)
    {
      telem_accum_ms += tick_ms;
      if (telem_accum_ms >= s_telem_period_ms)
      {
        char out[PROBE_OUT_MAX];
        telem_accum_ms = 0U;
        probe_build_all_metrics(out, sizeof(out), "TELM:");
        TarsProbeUart_SendStr(out);
      }
    }

    osDelay(tick_ms);
  }
}
