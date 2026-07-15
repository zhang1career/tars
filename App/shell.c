#include "shell.h"
#include "shell_hist.h"
#include "ring_stream.h"
#include "tars_lfs.h"
#include "tars_vfs.h"
#include "tars_ota.h"
#include "tars_hal.h"
#include "tars_sys.h"
#include "tars_mcu.h"
#include "tars_res_awg.h"
#include "tars_foc.h"
#include "usb_device.h"
#include "usbd_cdc.h"
#include "tars_app.h"
#include "tars_platform.h"
#include "tars_storage.h"
#include "main.h"
#include "cmsis_os.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SHELL_RX_RING_SIZE        512U
#define SHELL_LINE_SIZE           128U
#define SHELL_PROMPT                "tars> "
#define SHELL_BANNER                "\r\nTARS shell ready.\r\n" SHELL_PROMPT
#define SHELL_INSTALL_MAX         TARS_INSTALL_STAGING_SIZE

typedef enum {
  SHELL_MODE_TEXT = 0,
  SHELL_MODE_BINARY = 1
} shell_mode_t;

typedef enum {
  SHELL_BIN_APP_INSTALL = 0,
  SHELL_BIN_AWG_UPLOAD = 1
} shell_bin_sink_t;

static uint8_t s_rx_buf[SHELL_RX_RING_SIZE];
static ring_stream_t s_rx_stream;
static volatile uint8_t s_cdc_ready;

static char s_line[SHELL_LINE_SIZE];
static uint16_t s_line_len;
static uint16_t s_line_cursor;
static uint8_t s_prompt_pending = 1U;

typedef enum {
  SHELL_ESC_NONE = 0,
  SHELL_ESC_SEEN,
  SHELL_ESC_CSI
} shell_esc_state_t;

static shell_esc_state_t s_esc_state;

static shell_mode_t s_mode;
static uint32_t s_bin_target;
static uint32_t s_bin_received;
static int32_t s_bin_slot_hint;
static uint8_t *s_bin_buf;
static shell_bin_sink_t s_bin_sink;
static char s_bin_awg_ch[8];

static int shell_read_char(uint8_t *ch)
{
  return (ring_stream_pop(&s_rx_stream, ch) == 0) ? 1 : 0;
}

static uint8_t shell_usb_configured(void)
{
  return (hUsbDeviceHS.dev_state == USBD_STATE_CONFIGURED);
}

static uint8_t shell_link_active(void)
{
  return (shell_usb_configured() && (s_cdc_ready != 0U)) ? 1U : 0U;
}

static uint8_t shell_cdc_tx_idle(void)
{
  USBD_CDC_HandleTypeDef *hcdc =
      (USBD_CDC_HandleTypeDef *)hUsbDeviceHS.pClassDataCmsit[hUsbDeviceHS.classId];

  if (hcdc == NULL)
  {
    return 0U;
  }

  return (hcdc->TxState == 0U) ? 1U : 0U;
}

int Shell_CdcWrite(const char *data, uint16_t len)
{
  uint32_t start;

  if (!shell_usb_configured() || len == 0U)
  {
    return 0;
  }

  start = HAL_GetTick();
  while (!shell_cdc_tx_idle())
  {
    if ((HAL_GetTick() - start) >= 500U)
    {
      return 0;
    }

    osDelay(1);
  }

  if (USBD_CDC_SetTxBuffer(&hUsbDeviceHS, (uint8_t *)data, len) != USBD_OK)
  {
    return 0;
  }

  if (USBD_CDC_TransmitPacket(&hUsbDeviceHS) != USBD_OK)
  {
    return 0;
  }

  return (int)len;
}

void Shell_CdcRxPush(const uint8_t *data, uint32_t len)
{
  uint32_t i;

  if (s_mode == SHELL_MODE_BINARY && s_bin_buf != NULL)
  {
    for (i = 0U; i < len; i++)
    {
      if (s_bin_received >= s_bin_target)
      {
        break;
      }

      s_bin_buf[s_bin_received++] = data[i];
    }

    return;
  }

  (void)ring_stream_push_buf(&s_rx_stream, data, len);
}

uint8_t Shell_CdcIsReady(void)
{
  return shell_link_active();
}

void Shell_CdcSetReady(uint8_t ready)
{
  s_cdc_ready = ready;
}

static void shell_write_str(const char *str)
{
  (void)TarsVfs_Write(TARS_VFS_PATH_CONSOLE, str, (uint16_t)strlen(str));
}

static void shell_show_prompt(void)
{
  shell_write_str(SHELL_PROMPT);
}

static void shell_try_announce(void)
{
  if (!s_prompt_pending || !shell_usb_configured())
  {
    return;
  }

  if (Shell_CdcWrite(SHELL_BANNER, (uint16_t)strlen(SHELL_BANNER)) > 0)
  {
    s_prompt_pending = 0U;
  }
}

static int shell_str_eq(const char *a, const char *b)
{
  return strcmp(a, b) == 0;
}

static const char *shell_status_text(tars_status_t st)
{
  switch (st)
  {
  case TARS_OK:
    return "ok";
  case TARS_ERR_PARAM:
    return "param";
  case TARS_ERR_MAGIC:
    return "magic";
  case TARS_ERR_CRC:
    return "crc";
  case TARS_ERR_API_VERSION:
    return "api_version";
  case TARS_ERR_RESOURCE:
    return "resource";
  case TARS_ERR_CONFLICT:
    return "conflict";
  case TARS_ERR_NO_SLOT:
    return "no_slot";
  case TARS_ERR_FLASH:
    return "flash";
  case TARS_ERR_NOT_FOUND:
    return "not_found";
  case TARS_ERR_STATE:
    return "state";
  case TARS_ERR_RELOC:
    return "reloc";
  default:
    return "unknown";
  }
}

/* STM32 USB device stack states (usbd_def.h). */
static const char *shell_usb_state_text(uint8_t state)
{
  switch (state)
  {
  case 0x01U:
    return "default";
  case 0x02U:
    return "addressed";
  case 0x03U:
    return "configured";
  case 0x04U:
    return "suspended";
  default:
    return "unknown";
  }
}

static void shell_format_log_sinks(uint8_t sinks, char *out, uint32_t out_size)
{
  if ((out == NULL) || (out_size == 0U))
  {
    return;
  }

  if (sinks == 0U)
  {
    (void)snprintf(out, out_size, "none");
    return;
  }

  out[0] = '\0';
  if ((sinks & TARS_IO_SINK_CDC) != 0U)
  {
    (void)strncat(out, "cdc", out_size - strlen(out) - 1U);
  }
  if ((sinks & TARS_IO_SINK_LCD) != 0U)
  {
    if (out[0] != '\0')
    {
      (void)strncat(out, "+", out_size - strlen(out) - 1U);
    }
    (void)strncat(out, "lcd", out_size - strlen(out) - 1U);
  }
}

static void shell_finish_binary(void)
{
  char msg[64];
  tars_status_t st;
  uint32_t magic;

  s_mode = SHELL_MODE_TEXT;

  if (s_bin_received != s_bin_target)
  {
    shell_write_str((s_bin_sink == SHELL_BIN_AWG_UPLOAD)
                      ? "upload: incomplete\r\n"
                      : "install: incomplete\r\n");
    shell_show_prompt();
    return;
  }

  if (s_bin_sink == SHELL_BIN_AWG_UPLOAD)
  {
    int ust = TarsResAwg_UploadComplete(s_bin_awg_ch);

    (void)snprintf(msg, sizeof(msg), "upload: %s (%lu pts)\r\n",
                   (ust == 0) ? "ok" : "err",
                   (unsigned long)(s_bin_target / 2U));
    shell_write_str(msg);
    shell_show_prompt();
    return;
  }

  magic = *(const uint32_t *)(const void *)s_bin_buf;

  if (magic == TARS_APP_MAGIC)
  {
    st = TarsApp_InstallNative(s_bin_buf, s_bin_received, s_bin_slot_hint);
  }
  else if (magic == TARS_LUA_MAGIC)
  {
    st = TarsApp_InstallLua(s_bin_buf, s_bin_received);
  }
  else
  {
    st = TARS_ERR_MAGIC;
  }

  (void)snprintf(msg, sizeof(msg), "install: %d (%s)\r\n", (int)st, shell_status_text(st));
  shell_write_str(msg);
  shell_show_prompt();
}

static void shell_begin_binary(uint32_t size, int32_t slot_hint)
{
  if (size == 0U || size > SHELL_INSTALL_MAX)
  {
    shell_write_str("install: bad size\r\n");
    return;
  }

  s_bin_buf = (uint8_t *)(void *)TARS_INSTALL_STAGING_BASE;
  s_bin_target = size;
  s_bin_received = 0U;
  s_bin_slot_hint = slot_hint;
  s_bin_sink = SHELL_BIN_APP_INSTALL;
  s_mode = SHELL_MODE_BINARY;
  shell_write_str("install: ready\r\n");
}

static void shell_begin_awg_upload(const char *channel, uint32_t points)
{
  uint8_t *buf = NULL;
  uint32_t bytes = 0U;

  if (TarsResAwg_UploadBegin(channel, points, &buf, &bytes) != 0 ||
      buf == NULL || bytes == 0U)
  {
    shell_write_str("upload: rejected (check ch/points, stop channel first)\r\n");
    return;
  }

  s_bin_buf = buf;
  s_bin_target = bytes;
  s_bin_received = 0U;
  s_bin_slot_hint = -1;
  s_bin_sink = SHELL_BIN_AWG_UPLOAD;
  strncpy(s_bin_awg_ch, channel, sizeof(s_bin_awg_ch) - 1U);
  s_bin_awg_ch[sizeof(s_bin_awg_ch) - 1U] = '\0';
  s_mode = SHELL_MODE_BINARY;
  shell_write_str("upload: ready\r\n");
}

static void shell_execute_line(void)
{
  s_line[s_line_len] = '\0';

  if (s_line_len == 0U)
  {
    shell_show_prompt();
    return;
  }

  ShellHist_Push(s_line);

  if (shell_str_eq(s_line, "help"))
  {
    shell_write_str(
      "Commands:\r\n"
      "  help              Show this help\r\n"
      "  history           List recent commands (history N for last N)\r\n"
      "  status            USB role/CDC; state 1 default 2 addressed 3 configured 4 suspended\r\n"
      "  echo              Echo arguments\r\n"
      "  mcu               On-chip hardware (try mcu help)\r\n"
      "  app               Installed apps\r\n"
      "  fs                LittleFS\r\n"
      "  io                Log routing\r\n"
      "  sched             Scheduler\r\n"
      "  sys               Flash map / RTOS top\r\n"
      "  ota               OTA status (stub)\r\n"
      "  hal               HAL placeholders\r\n"
      "  motor             FOC motor control\r\n");
  }
  else if (strncmp(s_line, "history", 7) == 0 &&
           (s_line[7] == '\0' || s_line[7] == ' '))
  {
    uint32_t count = ShellHist_Count();
    uint32_t limit = count;
    const char *arg = s_line + 7;

    if ((arg[0] == ' ') && (arg[1] != '\0'))
    {
      unsigned long n = strtoul(arg + 1, NULL, 0);

      limit = (uint32_t)n;
      if (limit > count)
      {
        limit = count;
      }
    }

    if (count == 0U)
    {
      shell_write_str("history: (empty)\r\n");
    }
    else
    {
      uint32_t i;
      char msg[SHELL_LINE_SIZE + 16U];

      for (i = 0U; i < limit; i++)
      {
        uint32_t num = count - limit + i + 1U;
        uint32_t age = limit - 1U - i;
        const char *entry = ShellHist_Entry(age);

        if (entry == NULL)
        {
          break;
        }

        (void)snprintf(msg, sizeof(msg), "  %lu  %s\r\n",
                       (unsigned long)num, entry);
        shell_write_str(msg);
      }
    }
  }
  else if (shell_str_eq(s_line, "status"))
  {
    char msg[128];
    (void)snprintf(msg,
                   sizeof(msg),
                   "role=device cdc=%s state=%s (%lu)\r\n",
                   shell_usb_configured() ? "ready" : "down",
                   shell_usb_state_text(hUsbDeviceHS.dev_state),
                   (unsigned long)hUsbDeviceHS.dev_state);
    shell_write_str(msg);
  }
  else if (strncmp(s_line, "echo ", 5) == 0)
  {
    shell_write_str(s_line + 5);
    shell_write_str("\r\n");
  }
  else if (strncmp(s_line, "mcu awg upload ", 15) == 0)
  {
    char ch[8];
    unsigned long points = 0UL;

    if (sscanf(s_line + 15, "%7s %lu", ch, &points) != 2)
    {
      shell_write_str("upload: use mcu awg upload <ch> <points>\r\n");
    }
    else
    {
      shell_begin_awg_upload(ch, (uint32_t)points);
    }
  }
  else if (strncmp(s_line, "mcu", 3) == 0 && (s_line[3] == '\0' || s_line[3] == ' '))
  {
    char buffer[512];
    const char *args = (s_line[3] == ' ') ? (s_line + 4) : "";

    if (TarsMcu_ShellHandle(args, buffer, sizeof(buffer)) != 0)
    {
      shell_write_str(buffer);
    }
    else
    {
      shell_write_str("mcu: unknown subcommand (try mcu help)\r\n");
    }
  }
  else if (shell_str_eq(s_line, "app list"))
  {
    char buffer[512];
    (void)TarsApp_List(buffer, sizeof(buffer));
    shell_write_str(buffer);
  }
  else if (shell_str_eq(s_line, "app catalog"))
  {
    tars_catalog_diag_t diag;
    char msg[128];

    TarsStorage_GetCatalogDiag(&diag);
    (void)snprintf(msg, sizeof(msg),
                   "catalog: magic=0x%08lX entries=%lu stored_crc=0x%08lX "
                   "computed_crc=0x%08lX validate=%s ram_entries=%lu lfs=%s\r\n",
                   (unsigned long)diag.magic,
                   (unsigned long)diag.entry_count,
                   (unsigned long)diag.stored_crc,
                   (unsigned long)diag.computed_crc,
                   shell_status_text(diag.validate_status),
                   (unsigned long)TarsStorage_GetEntryCount(),
                   TarsLfs_IsMounted() ? "mounted" : "down");
    shell_write_str(msg);
  }
  else if (shell_str_eq(s_line, "app slots"))
  {
    char buffer[512];
    (void)TarsApp_ListSlots(buffer, sizeof(buffer));
    shell_write_str(buffer);
  }
  else if (strncmp(s_line, "app install begin ", 18) == 0)
  {
    char *endptr = NULL;
    unsigned long size = strtoul(s_line + 18, &endptr, 0);
    int32_t slot = -1;

    if (endptr != NULL && (*endptr == ' ' || *endptr == '\t'))
    {
      slot = (int32_t)strtol(endptr, NULL, 0);
    }

    shell_begin_binary((uint32_t)size, slot);
  }
  else if (strncmp(s_line, "app submit ", 11) == 0)
  {
    char msg[64];
    tars_status_t st = TarsApp_Submit(s_line + 11);
    (void)snprintf(msg, sizeof(msg), "submit: %d (%s)\r\n", (int)st, shell_status_text(st));
    shell_write_str(msg);
  }
  else if (strncmp(s_line, "app revoke ", 11) == 0)
  {
    char msg[64];
    tars_status_t st = TarsApp_Revoke(s_line + 11);
    (void)snprintf(msg, sizeof(msg), "revoke: %d (%s)\r\n", (int)st, shell_status_text(st));
    shell_write_str(msg);
  }
  else if (strncmp(s_line, "app uninstall ", 14) == 0)
  {
    char msg[64];
    tars_status_t st = TarsApp_Uninstall(s_line + 14);
    (void)snprintf(msg, sizeof(msg), "uninstall: %d (%s)\r\n", (int)st, shell_status_text(st));
    shell_write_str(msg);
  }
  else if (strncmp(s_line, "app run ", 8) == 0)
  {
    char msg[64];
    tars_status_t st = TarsApp_RunOnce(s_line + 8);
    (void)snprintf(msg, sizeof(msg), "run: %d (%s)\r\n", (int)st, shell_status_text(st));
    shell_write_str(msg);
  }
  else if (shell_str_eq(s_line, "fs info"))
  {
    char msg[128];
    TarsLfs_FormatInfo(msg, sizeof(msg));
    shell_write_str(msg);
  }
  else if (shell_str_eq(s_line, "fs df"))
  {
    char msg[128];
    if (TarsLfs_FormatDf(msg, sizeof(msg)) == TARS_OK)
    {
      shell_write_str(msg);
    }
    else
    {
      shell_write_str("fs df: error\r\n");
    }
  }
  else if (strncmp(s_line, "fs stat ", 8) == 0)
  {
    char msg[128];
    if (TarsLfs_FormatStat(s_line + 8, msg, sizeof(msg)) == TARS_OK)
    {
      shell_write_str(msg);
    }
    else
    {
      shell_write_str("fs stat: not found\r\n");
    }
  }
  else if (strncmp(s_line, "fs cat ", 7) == 0)
  {
    char buffer[640];
    if (TarsLfs_FormatCat(s_line + 7, buffer, sizeof(buffer), 480U) == TARS_OK)
    {
      shell_write_str(buffer);
    }
    else
    {
      shell_write_str("fs cat: not found\r\n");
    }
  }
  else if (strncmp(s_line, "fs hex ", 7) == 0)
  {
    char buffer[640];
    if (TarsLfs_FormatHex(s_line + 7, buffer, sizeof(buffer), 128U) == TARS_OK)
    {
      shell_write_str(buffer);
    }
    else
    {
      shell_write_str("fs hex: not found\r\n");
    }
  }
  else if (strncmp(s_line, "fs mkdir ", 9) == 0)
  {
    char msg[64];
    tars_status_t st = TarsLfs_MkDir(s_line + 9);
    (void)snprintf(msg, sizeof(msg), "fs mkdir: %d (%s)\r\n", (int)st, shell_status_text(st));
    shell_write_str(msg);
  }
  else if (strncmp(s_line, "fs ls", 5) == 0)
  {
    char buffer[512];
    const char *path = "/";

    if ((s_line[5] == ' ') && (s_line[6] != '\0'))
    {
      path = s_line + 6;
    }

    if (TarsLfs_ListDir(path, buffer, sizeof(buffer)) == TARS_OK)
    {
      shell_write_str(buffer);
    }
    else
    {
      shell_write_str("fs ls: not found\r\n");
    }
  }
  else if (shell_str_eq(s_line, "fs format"))
  {
    char msg[64];
    tars_status_t st = TarsLfs_Format();
    (void)snprintf(msg, sizeof(msg), "fs format: %d (%s)\r\n", (int)st, shell_status_text(st));
    shell_write_str(msg);
  }
  else if (strncmp(s_line, "fs rm ", 6) == 0)
  {
    char msg[64];
    tars_status_t st = TarsLfs_RemoveFile(s_line + 6);
    (void)snprintf(msg, sizeof(msg), "fs rm: %d (%s)\r\n", (int)st, shell_status_text(st));
    shell_write_str(msg);
  }
  else if (strncmp(s_line, "io log ", 7) == 0)
  {
    const char *arg = s_line + 7;
    char msg[64];

    if (shell_str_eq(arg, "cdc"))
    {
      TarsVfs_SetLogSinks(TARS_IO_SINK_CDC);
    }
    else if (shell_str_eq(arg, "lcd"))
    {
      TarsVfs_SetLogSinks(TARS_IO_SINK_LCD);
    }
    else if (shell_str_eq(arg, "both"))
    {
      TarsVfs_SetLogSinks(TARS_IO_SINK_BOTH);
    }
    else if (shell_str_eq(arg, "none"))
    {
      TarsVfs_SetLogSinks(0U);
    }
    else
    {
      shell_write_str("io log: use cdc|lcd|both|none\r\n");
      return;
    }

    (void)snprintf(msg, sizeof(msg), "io log: %s\r\n", arg);
    shell_write_str(msg);
  }
  else if (shell_str_eq(s_line, "io status"))
  {
    char msg[96];
    char sinks_text[16];
    uint8_t sinks = TarsVfs_GetLogSinks();

    shell_format_log_sinks(sinks, sinks_text, sizeof(sinks_text));
    (void)snprintf(msg,
                   sizeof(msg),
                   "io: log_sinks=%s console=/dev/console lcd=/dev/lcd\r\n",
                   sinks_text);
    shell_write_str(msg);
  }
  else if (shell_str_eq(s_line, "sys part"))
  {
    char msg[384];
    (void)snprintf(msg,
                   sizeof(msg),
                   "flash map:\r\n"
                   "  fw   0x%08lX + %luK sectors %u-%u\r\n"
                   "  lfs  0x%08lX + %luK sectors %u-%u\r\n"
                   "  native 0x%08lX stride %luK x %u slots\r\n"
                   "  ota  0x%08lX + %luK sector %u+\r\n",
                   (unsigned long)TARS_FW_FLASH_BASE,
                   (unsigned long)(TARS_FW_FLASH_SIZE / 1024U),
                   (unsigned)TARS_FW_FIRST_SECTOR,
                   (unsigned)TARS_FW_LAST_SECTOR,
                   (unsigned long)TARS_LFS_FLASH_BASE,
                   (unsigned long)(TARS_LFS_FLASH_SIZE / 1024U),
                   (unsigned)TARS_LFS_FIRST_SECTOR,
                   (unsigned)(TARS_LFS_FIRST_SECTOR + 1U),
                   (unsigned long)TARS_NATIVE_SLOT_BASE,
                   (unsigned long)(TARS_NATIVE_SLOT_STRIDE / 1024U),
                   (unsigned)TARS_NATIVE_SLOT_COUNT,
                   (unsigned long)TARS_OTA_FLASH_BASE,
                   (unsigned long)(TARS_OTA_FLASH_SIZE / 1024U),
                   (unsigned)TARS_OTA_FIRST_SECTOR);
    shell_write_str(msg);
  }
  else if (shell_str_eq(s_line, "sys top"))
  {
    char buffer[768];
    TarsSys_FormatTop(buffer, sizeof(buffer));
    shell_write_str(buffer);
  }
  else if (shell_str_eq(s_line, "sched status"))
  {
    tars_scheduler_info_t info;
    char msg[128];

    TarsApp_GetSchedulerInfo(&info);
    if (info.has_running != 0U)
    {
      (void)snprintf(msg,
                     sizeof(msg),
                     "sched: ts=%u/%u ms=%u running=%s\r\n",
                     (unsigned)info.current_timeslice,
                     (unsigned)info.slice_count,
                     (unsigned)info.slice_ms,
                     info.running_name);
    }
    else
    {
      (void)snprintf(msg,
                     sizeof(msg),
                     "sched: ts=%u/%u ms=%u running=(idle)\r\n",
                     (unsigned)info.current_timeslice,
                     (unsigned)info.slice_count,
                     (unsigned)info.slice_ms);
    }
    shell_write_str(msg);
  }
  else if (shell_str_eq(s_line, "ota status"))
  {
    char msg[128];
    TarsOta_FormatStatus(msg, sizeof(msg));
    shell_write_str(msg);
  }
  else if (shell_str_eq(s_line, "hal status"))
  {
    char msg[64];
    TarsHal_FormatStatus(msg, sizeof(msg));
    shell_write_str(msg);
  }
  else if (strncmp(s_line, "motor", 5) == 0 && (s_line[5] == '\0' || s_line[5] == ' '))
  {
    const char *args = (s_line[5] == ' ') ? (s_line + 6) : "";

    if (shell_str_eq(args, "enable"))
    {
      if (TarsFoc_Enable(1) != 0)
      {
        shell_write_str("motor: ENABLED (bridge live -- verify gate signals!)\r\n");
      }
      else
      {
        shell_write_str("motor: REFUSED (TIM1 held by shell PWM -- disable pwm0 first)\r\n");
      }
    }
    else if (shell_str_eq(args, "disable"))
    {
      TarsFoc_Enable(0);
      shell_write_str("motor: disabled (outputs tri-stated)\r\n");
    }
    else if (strncmp(args, "speed ", 6) == 0)
    {
      float rpm = (float)strtod(args + 6, NULL);
      TarsFoc_SetSpeedRef(rpm);
      char msg[48];
      (void)snprintf(msg, sizeof(msg), "motor: speed_ref=%.1f rpm\r\n", (double)rpm);
      shell_write_str(msg);
    }
    else if (shell_str_eq(args, "cal"))
    {
      TarsFoc_Calibrate();
      shell_write_str("motor: calibrating zero-current offsets (bridge off)\r\n");
    }
    else if (shell_str_eq(args, "status") || args[0] == '\0')
    {
      tars_foc_snapshot_t s;
      char msg[224];
      TarsFoc_GetSnapshot(&s);
      (void)snprintf(msg, sizeof(msg),
                     "motor: %s ref=%.1f spd=%.1frpm id=%.2f iq=%.2f vdc=%.1f\r\n"
                     "  ia=%.2f ib=%.2f ic=%.2f duty=%.2f/%.2f/%.2f theta=%.2f fault=%u\r\n",
                     (s.enabled ? "ON " : "off"),
                     (double)s.speed_ref_rpm, (double)s.speed_est_rpm,
                     (double)s.id, (double)s.iq, (double)s.vdc,
                     (double)s.ia, (double)s.ib, (double)s.ic,
                     (double)s.duty_a, (double)s.duty_b, (double)s.duty_c,
                     (double)s.theta_est_rad, (unsigned)s.fault_code);
      shell_write_str(msg);
    }
    else
    {
      shell_write_str("motor: enable | disable | speed <rpm> | cal | status\r\n");
    }
  }
  else
  {
    shell_write_str("Unknown command. Type 'help'.\r\n");
  }

  if (s_mode == SHELL_MODE_TEXT)
  {
    shell_show_prompt();
  }
}

static uint8_t shell_echo_enabled(void)
{
  if (strncmp(s_line, "app install begin ", 18) == 0)
  {
    return 0U;
  }
  if (strncmp(s_line, "mcu awg upload ", 15) == 0)
  {
    return 0U;
  }
  return 1U;
}

static void shell_line_redraw(void)
{
  uint16_t i;

  if (!shell_echo_enabled())
  {
    return;
  }

  shell_write_str("\r");
  shell_write_str(SHELL_PROMPT);
  s_line[s_line_len] = '\0';
  shell_write_str(s_line);
  shell_write_str("\x1b[K");
  for (i = s_line_cursor; i < s_line_len; i++)
  {
    shell_write_str("\b");
  }
}

static void shell_set_line(const char *line)
{
  if ((line == NULL) || (line[0] == '\0'))
  {
    s_line[0] = '\0';
    s_line_len = 0U;
  }
  else
  {
    (void)strncpy(s_line, line, SHELL_LINE_SIZE - 1U);
    s_line[SHELL_LINE_SIZE - 1U] = '\0';
    s_line_len = (uint16_t)strlen(s_line);
  }

  s_line_cursor = s_line_len;
  shell_line_redraw();
}

static void shell_insert_char(char ch)
{
  ShellHist_ResetBrowse();

  if (s_line_len >= (SHELL_LINE_SIZE - 1U))
  {
    return;
  }

  if (s_line_cursor == s_line_len)
  {
    s_line[s_line_len++] = ch;
    s_line_cursor++;

    if (shell_echo_enabled())
    {
      char out[2] = {ch, '\0'};
      shell_write_str(out);
    }
  }
  else
  {
    memmove(&s_line[s_line_cursor + 1U], &s_line[s_line_cursor],
            (size_t)(s_line_len - s_line_cursor));
    s_line[s_line_cursor] = ch;
    s_line_len++;
    s_line_cursor++;
    shell_line_redraw();
  }
}

static void shell_backspace(void)
{
  ShellHist_ResetBrowse();

  if (s_line_cursor == 0U)
  {
    return;
  }

  memmove(&s_line[s_line_cursor - 1U], &s_line[s_line_cursor],
          (size_t)(s_line_len - s_line_cursor));
  s_line_len--;
  s_line_cursor--;
  shell_line_redraw();
}

static void shell_cursor_left(void)
{
  if (s_line_cursor == 0U)
  {
    return;
  }

  s_line_cursor--;

  if (shell_echo_enabled())
  {
    shell_write_str("\b");
  }
}

static void shell_cursor_right(void)
{
  if (s_line_cursor >= s_line_len)
  {
    return;
  }

  if (shell_echo_enabled())
  {
    char out[2] = {s_line[s_line_cursor], '\0'};
    shell_write_str(out);
  }

  s_line_cursor++;
}

static void shell_handle_csi(char final)
{
  const char *hist_line;

  switch (final)
  {
  case 'A':
    if (ShellHist_Prev(&hist_line) == 0)
    {
      shell_set_line(hist_line);
    }
    break;
  case 'B':
    if (ShellHist_Next(&hist_line) == 0)
    {
      shell_set_line(hist_line);
    }
    break;
  case 'C':
    shell_cursor_right();
    break;
  case 'D':
    shell_cursor_left();
    break;
  default:
    break;
  }
}

static void shell_handle_char(uint8_t ch)
{
  if (s_esc_state == SHELL_ESC_CSI)
  {
    if (((ch >= 0x40U) && (ch <= 0x7EU)) || (ch == '~'))
    {
      shell_handle_csi((char)ch);
      s_esc_state = SHELL_ESC_NONE;
    }
    return;
  }

  if (s_esc_state == SHELL_ESC_SEEN)
  {
    if (ch == '[')
    {
      s_esc_state = SHELL_ESC_CSI;
    }
    else
    {
      s_esc_state = SHELL_ESC_NONE;
    }
    return;
  }

  if (ch == 0x1BU)
  {
    s_esc_state = SHELL_ESC_SEEN;
    return;
  }

  if (ch == '\r' || ch == '\n')
  {
    if (shell_echo_enabled())
    {
      shell_write_str("\r\n");
    }

    shell_execute_line();
    s_line_len = 0U;
    s_line_cursor = 0U;
    return;
  }

  if (ch == 0x7FU || ch == 0x08U)
  {
    shell_backspace();
    return;
  }

  if (ch >= 0x20U)
  {
    shell_insert_char((char)ch);
  }
}

void Shell_OnUsbConfigured(void)
{
  s_prompt_pending = 1U;
}

void Shell_Init(void)
{
  ring_stream_init(&s_rx_stream, s_rx_buf, SHELL_RX_RING_SIZE);
  ShellHist_Init();
  s_line_len = 0U;
  s_line_cursor = 0U;
  s_esc_state = SHELL_ESC_NONE;
  s_prompt_pending = 1U;
  s_cdc_ready = 0U;
  s_mode = SHELL_MODE_TEXT;
  s_bin_buf = NULL;
  s_bin_target = 0U;
  s_bin_received = 0U;
  s_bin_slot_hint = -1;
  s_bin_sink = SHELL_BIN_APP_INSTALL;
  s_bin_awg_ch[0] = '\0';
}

void Shell_Task(void const *argument)
{
  uint8_t ch;
  uint8_t was_configured = 0U;
  uint32_t configured_tick = 0U;

  (void)argument;

  for (;;)
  {
    if (shell_usb_configured())
    {
      if (!was_configured)
      {
        s_prompt_pending = 1U;
        was_configured = 1U;
        configured_tick = HAL_GetTick();
        if (s_cdc_ready == 0U)
        {
          s_cdc_ready = 1U;
        }
      }

      if (s_prompt_pending && ((HAL_GetTick() - configured_tick) >= 50U))
      {
        shell_try_announce();
      }

      if (s_mode == SHELL_MODE_BINARY &&
          s_bin_received >= s_bin_target &&
          s_bin_target > 0U)
      {
        shell_finish_binary();
      }
    }
    else
    {
      was_configured = 0U;
    }

    while (shell_read_char(&ch))
    {
      shell_handle_char(ch);
    }

    osDelay(10);
  }
}
