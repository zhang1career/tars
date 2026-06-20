#include "tars_vfs.h"
#include "shell.h"
#include "lcd_log.h"
#include <stdio.h>
#include <string.h>

static uint8_t s_log_sinks = TARS_IO_SINK_LCD;

void TarsVfs_Init(void)
{
  /* App logs default to LCD; shell stays on /dev/console. Use `io log both` for CDC. */
  s_log_sinks = TARS_IO_SINK_LCD;
}

void TarsVfs_SetLogSinks(uint8_t sinks)
{
  s_log_sinks = sinks;
}

uint8_t TarsVfs_GetLogSinks(void)
{
  return s_log_sinks;
}

static int vfs_write_console(const char *data, uint16_t len)
{
  return Shell_CdcWrite(data, len);
}

static int vfs_write_lcd(const char *data, uint16_t len)
{
  char line[64];
  uint16_t copy = len;

  if (copy >= sizeof(line))
  {
    copy = (uint16_t)(sizeof(line) - 1U);
  }

  memcpy(line, data, copy);
  line[copy] = '\0';

  while (copy > 0U && (line[copy - 1U] == '\r' || line[copy - 1U] == '\n'))
  {
    line[--copy] = '\0';
  }

  if (copy == 0U)
  {
    return 0;
  }

  LcdLog_WriteLine(line);
  return (int)len;
}

int TarsVfs_Write(const char *path, const char *data, uint16_t len)
{
  if ((path == NULL) || (data == NULL) || (len == 0U))
  {
    return 0;
  }

  if (strcmp(path, TARS_VFS_PATH_CONSOLE) == 0)
  {
    return vfs_write_console(data, len);
  }

  if (strcmp(path, TARS_VFS_PATH_LCD) == 0)
  {
    return vfs_write_lcd(data, len);
  }

  if (strcmp(path, TARS_VFS_PATH_LOG) == 0)
  {
    int total = 0;

    if ((s_log_sinks & TARS_IO_SINK_CDC) != 0U)
    {
      total += vfs_write_console(data, len);
    }

    if ((s_log_sinks & TARS_IO_SINK_LCD) != 0U)
    {
      (void)vfs_write_lcd(data, len);
    }

    return total;
  }

  return 0;
}

void TarsVfs_WriteLog(const char *msg)
{
  char buf[80];
  int len;

  if (msg == NULL)
  {
    return;
  }

  len = snprintf(buf, sizeof(buf), "%s\r\n", msg);
  if (len <= 0)
  {
    return;
  }

  if ((uint32_t)len >= sizeof(buf))
  {
    len = (int)(sizeof(buf) - 1U);
  }

  if ((s_log_sinks & TARS_IO_SINK_CDC) != 0U)
  {
    (void)vfs_write_console(buf, (uint16_t)len);
  }

  if ((s_log_sinks & TARS_IO_SINK_LCD) != 0U)
  {
    (void)vfs_write_lcd(msg, (uint16_t)strlen(msg));
  }
}
