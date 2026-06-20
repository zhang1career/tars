#include "shell.h"
#include "tars_lfs.h"
#include "tars_vfs.h"
#include "tars_ota.h"
#include "tars_hal.h"
#include "tars_sys.h"
#include "tars_mcu.h"
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

static uint8_t s_rx_ring[SHELL_RX_RING_SIZE];
static volatile uint32_t s_rx_head;
static volatile uint32_t s_rx_tail;
static volatile uint8_t s_cdc_ready;

static char s_line[SHELL_LINE_SIZE];
static uint16_t s_line_len;
static uint8_t s_prompt_pending = 1U;

static shell_mode_t s_mode;
static uint32_t s_bin_target;
static uint32_t s_bin_received;
static int32_t s_bin_slot_hint;
static uint8_t *s_bin_buf;

static int shell_read_char(uint8_t *ch)
{
  if (s_rx_head == s_rx_tail)
  {
    return 0;
  }

  *ch = s_rx_ring[s_rx_tail];
  s_rx_tail = (s_rx_tail + 1U) % SHELL_RX_RING_SIZE;
  return 1;
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

  for (i = 0U; i < len; i++)
  {
    uint32_t next = (s_rx_head + 1U) % SHELL_RX_RING_SIZE;

    if (next == s_rx_tail)
    {
      break;
    }

    s_rx_ring[s_rx_head] = data[i];
    s_rx_head = next;
  }
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

static void shell_finish_binary_install(void)
{
  char msg[64];
  tars_status_t st;
  uint32_t magic;

  s_mode = SHELL_MODE_TEXT;

  if (s_bin_received != s_bin_target)
  {
    shell_write_str("install: incomplete\r\n");
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
  s_mode = SHELL_MODE_BINARY;
  shell_write_str("install: ready\r\n");
}

static void shell_execute_line(void)
{
  s_line[s_line_len] = '\0';

  if (s_line_len == 0U)
  {
    shell_show_prompt();
    return;
  }

  if (shell_str_eq(s_line, "help"))
  {
    shell_write_str(
      "Commands:\r\n"
      "  help              Show this help\r\n"
      "  status            Show USB role and link state\r\n"
      "  echo              Echo arguments\r\n"
      "  mcu               MCU info (try mcu help)\r\n"
      "  mcu gpio write pg13 0   LD3 on (active low)\r\n"
      "  mcu gpio read pg13      Read GPIO level\r\n"
      "  app list          List installed apps\r\n"
      "  app catalog       Show flash catalog status\r\n"
      "  app slots         Show native slot map\r\n"
      "  app install begin <size>  Receive .tlua blob (MVP)\r\n"
      "  app submit <name> Register app for scheduling\r\n"
      "  app revoke <name> Unregister app\r\n"
      "  app uninstall <name> Remove app from catalog\r\n"
      "  app run <name>    Run app once\r\n"
      "  fs info           LittleFS partition status\r\n"
      "  fs ls [path]      List directory (default /)\r\n"
      "  fs df             Show free/used blocks\r\n"
      "  fs stat <path>    File or directory metadata\r\n"
      "  fs cat <path>     Print file (text, truncated)\r\n"
      "  fs hex <path>     Hex dump of file (truncated)\r\n"
      "  fs mkdir <path>   Create directory\r\n"
      "  fs format         Erase and recreate LittleFS\r\n"
      "  fs rm <path>      Remove file on LittleFS\r\n"
      "  io log <cdc|lcd|both|none>  Route tars.log (default lcd)\r\n"
      "  io status         Show I/O routing\r\n"
      "  sched status      Scheduler timeslice + running app\r\n"
      "  sys part          Show flash partition map\r\n"
      "  sys top           RTOS/Lua heap + task list\r\n"
      "  ota status        OTA A/B bank status (stub)\r\n"
      "  hal status        WiFi/CAN/motor placeholder status\r\n");
  }
  else if (shell_str_eq(s_line, "status"))
  {
    char msg[96];
    (void)snprintf(msg, sizeof(msg),
                   "role=device cdc=%s state=%lu\r\n",
                   shell_usb_configured() ? "ready" : "down",
                   (unsigned long)hUsbDeviceHS.dev_state);
    shell_write_str(msg);
  }
  else if (strncmp(s_line, "echo ", 5) == 0)
  {
    shell_write_str(s_line + 5);
    shell_write_str("\r\n");
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
                   "computed_crc=0x%08lX validate=%d ram_entries=%lu lfs=%d\r\n",
                   (unsigned long)diag.magic,
                   (unsigned long)diag.entry_count,
                   (unsigned long)diag.stored_crc,
                   (unsigned long)diag.computed_crc,
                   (int)diag.validate_status,
                   (unsigned long)TarsStorage_GetEntryCount(),
                   TarsLfs_IsMounted());
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
    (void)snprintf(msg, sizeof(msg), "submit: %d\r\n", (int)st);
    shell_write_str(msg);
  }
  else if (strncmp(s_line, "app revoke ", 11) == 0)
  {
    char msg[64];
    tars_status_t st = TarsApp_Revoke(s_line + 11);
    (void)snprintf(msg, sizeof(msg), "revoke: %d\r\n", (int)st);
    shell_write_str(msg);
  }
  else if (strncmp(s_line, "app uninstall ", 14) == 0)
  {
    char msg[64];
    tars_status_t st = TarsApp_Uninstall(s_line + 14);
    (void)snprintf(msg, sizeof(msg), "uninstall: %d\r\n", (int)st);
    shell_write_str(msg);
  }
  else if (strncmp(s_line, "app run ", 8) == 0)
  {
    char msg[64];
    tars_status_t st = TarsApp_RunOnce(s_line + 8);
    (void)snprintf(msg, sizeof(msg), "run: %d\r\n", (int)st);
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
    (void)snprintf(msg, sizeof(msg), "fs mkdir: %d\r\n", (int)st);
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
    (void)snprintf(msg, sizeof(msg), "fs format: %d\r\n", (int)st);
    shell_write_str(msg);
  }
  else if (strncmp(s_line, "fs rm ", 6) == 0)
  {
    char msg[64];
    tars_status_t st = TarsLfs_RemoveFile(s_line + 6);
    (void)snprintf(msg, sizeof(msg), "fs rm: %d\r\n", (int)st);
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
    uint8_t sinks = TarsVfs_GetLogSinks();

    (void)snprintf(msg,
                   sizeof(msg),
                   "io: log_sinks=0x%02X console=/dev/console lcd=/dev/lcd\r\n",
                   (unsigned)sinks);
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
  return (strncmp(s_line, "app install begin ", 18) != 0) ? 1U : 0U;
}

static void shell_handle_char(uint8_t ch)
{
  if (ch == '\r' || ch == '\n')
  {
    if (shell_echo_enabled())
    {
      shell_write_str("\r\n");
    }

    shell_execute_line();
    s_line_len = 0U;
    return;
  }

  if (ch == 0x7FU || ch == 0x08U)
  {
    if (s_line_len > 0U)
    {
      s_line_len--;

      if (shell_echo_enabled())
      {
        shell_write_str("\b \b");
      }
    }

    return;
  }

  if (ch >= 0x20U && s_line_len < (SHELL_LINE_SIZE - 1U))
  {
    s_line[s_line_len++] = (char)ch;

    if (shell_echo_enabled())
    {
      char out[2] = {(char)ch, '\0'};
      shell_write_str(out);
    }
  }
}

void Shell_OnUsbConfigured(void)
{
  s_prompt_pending = 1U;
}

void Shell_Init(void)
{
  s_rx_head = 0U;
  s_rx_tail = 0U;
  s_line_len = 0U;
  s_prompt_pending = 1U;
  s_cdc_ready = 0U;
  s_mode = SHELL_MODE_TEXT;
  s_bin_buf = NULL;
  s_bin_target = 0U;
  s_bin_received = 0U;
  s_bin_slot_hint = -1;
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
        shell_finish_binary_install();
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
