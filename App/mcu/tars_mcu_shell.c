#include "tars_mcu.h"
#include "tars_mcu_pinmap.h"
#include <stdio.h>
#include <string.h>

static int mcu_str_eq(const char *a, const char *b)
{
  if (a == NULL || b == NULL)
  {
    return 0;
  }

  return (strcmp(a, b) == 0) ? 1 : 0;
}

static void mcu_shell_help(char *out, uint32_t out_size)
{
  if ((out == NULL) || (out_size == 0U))
  {
    return;
  }

  (void)snprintf(out,
                 out_size,
                 "mcu commands:\r\n"
                 "  mcu info\r\n"
                 "  mcu gpio write <pgNN|alias> <0|1>\r\n"
                 "  mcu gpio read <pgNN|alias>\r\n"
                 "  mcu gpio list              (user GPIO on Morpho + LD3/LD4)\r\n"
                 "  mcu pinmap\r\n"
                 "  mcu tim|adc|dac|can|uart status  (stub)\r\n");
}

static void mcu_shell_stub_status(const char *resource, char *out, uint32_t out_size)
{
  if ((out == NULL) || (out_size == 0U))
  {
    return;
  }

  (void)snprintf(out,
                 out_size,
                 "mcu %s: stub (peripheral API not wired yet)\r\n",
                 resource);
}

int TarsMcu_ShellHandle(const char *args, char *out, uint32_t out_size)
{
  char sub[16];
  char rest[96];
  const char *p = args;

  if ((out == NULL) || (out_size == 0U))
  {
    return 0;
  }

  out[0] = '\0';

  if ((p == NULL) || (p[0] == '\0'))
  {
    TarsMcu_FormatInfo(out, out_size);
    strncat(out, "try: mcu help\r\n", out_size - strlen(out) - 1U);
    return 1;
  }

  if (sscanf(p, "%15s %95[^\n]", sub, rest) < 1)
  {
    return 0;
  }

  if (mcu_str_eq(sub, "help"))
  {
    mcu_shell_help(out, out_size);
    return 1;
  }

  if (mcu_str_eq(sub, "info"))
  {
    TarsMcu_FormatInfo(out, out_size);
    return 1;
  }

  if (mcu_str_eq(sub, "pinmap"))
  {
    (void)snprintf(out, out_size, "mcu pinmap (%s):\r\n", TarsMcuPinmap_BoardId());
    TarsMcuPinmap_FormatPeriphMap(out + strlen(out), out_size - (uint32_t)strlen(out));
    return 1;
  }

  if (mcu_str_eq(sub, "gpio"))
  {
    char pin_name[24];
    unsigned long val = 0UL;
    int gpio_val = 0;

    if (mcu_str_eq(rest, "list"))
    {
      (void)snprintf(out, out_size, "mcu gpio list (%s):\r\n", TarsMcuPinmap_BoardId());
      TarsMcuPinmap_FormatGpioList(out + strlen(out), out_size - (uint32_t)strlen(out));
      return 1;
    }

    if (strncmp(rest, "write ", 6) == 0)
    {
      if (sscanf(rest + 6, "%23s %lu", pin_name, &val) != 2)
      {
        (void)snprintf(out,
                       out_size,
                       "mcu gpio write: use gpio write <pgNN|alias> <0|1>\r\n");
        return 1;
      }

      {
        int wr = TarsMcu_GpioWrite(pin_name, (int)val);

        if (wr == -2)
        {
          (void)snprintf(out,
                         out_size,
                         "mcu gpio write: pin %s is input-only\r\n",
                         pin_name);
        }
        else if (wr != 0)
        {
          (void)snprintf(out, out_size, "mcu gpio write: unknown pin %s\r\n", pin_name);
        }
        else
        {
          (void)snprintf(out,
                         out_size,
                         "mcu gpio write: pin=%s val=%lu\r\n",
                         pin_name,
                         val);
        }
      }
      return 1;
    }

    if (strncmp(rest, "read ", 5) == 0)
    {
      if (sscanf(rest + 5, "%23s", pin_name) != 1)
      {
        (void)snprintf(out,
                       out_size,
                       "mcu gpio read: use gpio read <pgNN|alias>\r\n");
        return 1;
      }

      if (TarsMcu_GpioRead(pin_name, &gpio_val) != 0)
      {
        (void)snprintf(out, out_size, "mcu gpio read: unknown pin %s\r\n", pin_name);
      }
      else
      {
        (void)snprintf(out,
                       out_size,
                       "mcu gpio read: pin=%s val=%d\r\n",
                       pin_name,
                       gpio_val);
      }
      return 1;
    }

    (void)snprintf(out,
                   out_size,
                   "mcu gpio: use gpio write|read|list\r\n");
    return 1;
  }

  if ((strcmp(sub, "tim") == 0) ||
      (strcmp(sub, "adc") == 0) ||
      (strcmp(sub, "dac") == 0) ||
      (strcmp(sub, "can") == 0) ||
      (strcmp(sub, "uart") == 0))
  {
    if ((rest[0] == '\0') || (mcu_str_eq(rest, "status")))
    {
      mcu_shell_stub_status(sub, out, out_size);
      return 1;
    }
  }

  return 0;
}
