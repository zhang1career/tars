#include "shell.h"
#include "usb_device.h"
#include "usbd_cdc.h"
#include "cmsis_os.h"
#include <stdio.h>
#include <string.h>

#define SHELL_RX_RING_SIZE   512U
#define SHELL_LINE_SIZE      128U
#define SHELL_PROMPT           "tars> "

static uint8_t s_rx_ring[SHELL_RX_RING_SIZE];
static volatile uint32_t s_rx_head;
static volatile uint32_t s_rx_tail;
static volatile uint8_t s_cdc_ready;

static char s_line[SHELL_LINE_SIZE];
static uint16_t s_line_len;
static uint8_t s_prompt_pending = 1U;

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

int Shell_CdcWrite(const char *data, uint16_t len)
{
  if (!s_cdc_ready || hUsbDeviceHS.dev_state != USBD_STATE_CONFIGURED || len == 0U)
  {
    return 0;
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
  for (uint32_t i = 0U; i < len; i++)
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
  return s_cdc_ready;
}

void Shell_CdcSetReady(uint8_t ready)
{
  s_cdc_ready = ready;
}

static void shell_write_str(const char *str)
{
  (void)Shell_CdcWrite(str, (uint16_t)strlen(str));
}

static void shell_show_prompt(void)
{
  shell_write_str(SHELL_PROMPT);
}

static int shell_str_eq(const char *a, const char *b)
{
  return strcmp(a, b) == 0;
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
      "  help    Show this help\r\n"
      "  status  Show USB role and link state\r\n"
      "  echo    Echo arguments\r\n");
  }
  else if (shell_str_eq(s_line, "status"))
  {
    char msg[96];
    (void)snprintf(msg, sizeof(msg),
                   "role=device cdc=%s state=%lu\r\n",
                   s_cdc_ready ? "ready" : "down",
                   (unsigned long)hUsbDeviceHS.dev_state);
    shell_write_str(msg);
  }
  else if (strncmp(s_line, "echo ", 5) == 0)
  {
    shell_write_str(s_line + 5);
    shell_write_str("\r\n");
  }
  else
  {
    shell_write_str("Unknown command. Type 'help'.\r\n");
  }

  shell_show_prompt();
}

static void shell_handle_char(uint8_t ch)
{
  if (ch == '\r' || ch == '\n')
  {
    shell_write_str("\r\n");
    shell_execute_line();
    s_line_len = 0U;
    return;
  }

  if (ch == 0x7FU || ch == 0x08U)
  {
    if (s_line_len > 0U)
    {
      s_line_len--;
      shell_write_str("\b \b");
    }
    return;
  }

  if (ch >= 0x20U && s_line_len < (SHELL_LINE_SIZE - 1U))
  {
    s_line[s_line_len++] = (char)ch;
    char out[2] = {(char)ch, '\0'};
    shell_write_str(out);
  }
}

void Shell_Init(void)
{
  s_rx_head = 0U;
  s_rx_tail = 0U;
  s_line_len = 0U;
  s_prompt_pending = 1U;
  s_cdc_ready = 0U;
}

void Shell_Task(void const *argument)
{
  uint8_t ch;

  (void)argument;

  for (;;)
  {
    if (Shell_CdcIsReady())
    {
      if (s_prompt_pending)
      {
        shell_write_str("\r\nTARS shell ready.\r\n");
        shell_show_prompt();
        s_prompt_pending = 0U;
      }

      while (shell_read_char(&ch))
      {
        shell_handle_char(ch);
      }
    }

    osDelay(10);
  }
}
