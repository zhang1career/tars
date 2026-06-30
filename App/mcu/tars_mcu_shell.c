#include "tars_mcu.h"
#include "tars_mcu_pinmap.h"
#include "tars_res_mgr.h"
#include "tars_res_pwm.h"
#include <stdio.h>
#include <stdlib.h>
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
                 "  mcu res list|status <id>|grant <id> <owner>\r\n"
                 "  mcu pwm list|status [ch]|enable <ch> <0|1>\r\n"
                 "  mcu pwm duty <ch> <0-100>|freq <tim> <hz>\r\n"
                 "  mcu gpio write|read|list\r\n"
                 "  mcu pinmap\r\n"
                 "  owners: none gpio pwm foc system\r\n");
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
    (void)snprintf(out, out_size, "mcu pinmap (%s) periph:\r\n", TarsMcuPinmap_BoardId());
    TarsMcuPinmap_FormatPeriphMap(out + strlen(out), out_size - (uint32_t)strlen(out));
    (void)snprintf(out + strlen(out),
                   out_size - (uint32_t)strlen(out),
                   "pwm:\r\n");
    TarsMcuPinmap_FormatPwmList(out + strlen(out), out_size - (uint32_t)strlen(out));
    return 1;
  }

  if (mcu_str_eq(sub, "res"))
  {
    char id[24];
    char owner_text[16];

    if (mcu_str_eq(rest, "list"))
    {
      (void)snprintf(out, out_size, "mcu res list (%s):\r\n", TarsMcuPinmap_BoardId());
      TarsResMgr_FormatList(out + strlen(out), out_size - (uint32_t)strlen(out));
      return 1;
    }

    if (strncmp(rest, "status ", 7) == 0)
    {
      if (sscanf(rest + 7, "%23s", id) != 1)
      {
        (void)snprintf(out, out_size, "mcu res status: use res status <id>\r\n");
        return 1;
      }
      TarsResMgr_FormatStatus(id, out, out_size);
      return 1;
    }

    if (strncmp(rest, "grant ", 6) == 0)
    {
      tars_owner_t owner;

      if (sscanf(rest + 6, "%23s %15s", id, owner_text) != 2)
      {
        (void)snprintf(out,
                       out_size,
                       "mcu res grant: use res grant <id> <none|gpio|pwm|foc>\r\n");
        return 1;
      }

      if (TarsOwner_Parse(owner_text, &owner) != 0)
      {
        (void)snprintf(out, out_size, "mcu res grant: bad owner %s\r\n", owner_text);
        return 1;
      }

      {
        int st = TarsMcu_ResGrant(id, owner);

        if (st != 0)
        {
          (void)snprintf(out,
                         out_size,
                         "mcu res grant: id=%s err=%s\r\n",
                         id,
                         TarsMcu_ResErrText(st));
        }
        else
        {
          (void)snprintf(out,
                         out_size,
                         "mcu res grant: id=%s owner=%s\r\n",
                         id,
                         owner_text);
        }
      }
      return 1;
    }

    (void)snprintf(out, out_size, "mcu res: use list|status|grant\r\n");
    return 1;
  }

  if (mcu_str_eq(sub, "pwm"))
  {
    char ch[24];
    char tim_id[16];
    unsigned long val = 0UL;
    unsigned long duty_ul = 0UL;

    if (mcu_str_eq(rest, "list"))
    {
      (void)snprintf(out, out_size, "mcu pwm list (%s):\r\n", TarsMcuPinmap_BoardId());
      TarsMcuPinmap_FormatPwmList(out + strlen(out), out_size - (uint32_t)strlen(out));
      return 1;
    }

    if ((rest[0] == '\0') || (mcu_str_eq(rest, "status")))
    {
      (void)snprintf(out, out_size, "mcu pwm: use list|status <ch>|enable|duty|freq\r\n");
      return 1;
    }

    if (strncmp(rest, "status ", 7) == 0)
    {
      if (sscanf(rest + 7, "%23s", ch) != 1)
      {
        (void)snprintf(out, out_size, "mcu pwm status: use pwm status <ch>\r\n");
        return 1;
      }
      (void)TarsResPwm_GetStatus(ch, out, out_size);
      return 1;
    }

    if (strncmp(rest, "enable ", 7) == 0)
    {
      if (sscanf(rest + 7, "%23s %lu", ch, &val) != 2)
      {
        (void)snprintf(out, out_size, "mcu pwm enable: use enable <ch> <0|1>\r\n");
        return 1;
      }

      {
        int st = TarsMcu_PwmEnable(ch, (int)val);

        if (st != 0)
        {
          (void)snprintf(out,
                         out_size,
                         "mcu pwm enable: ch=%s err=%s\r\n",
                         ch,
                         TarsMcu_ResErrText(st));
        }
        else
        {
          (void)snprintf(out,
                         out_size,
                         "mcu pwm enable: ch=%s val=%lu\r\n",
                         ch,
                         val);
        }
      }
      return 1;
    }

    if (strncmp(rest, "duty ", 5) == 0)
    {
      const char *args = rest + 5;
      char *endptr = NULL;

      if (sscanf(args, "%23s", ch) != 1)
      {
        (void)snprintf(out, out_size, "mcu pwm duty: use duty <ch> <0-100>\r\n");
        return 1;
      }

      args += strlen(ch);
      while ((*args == ' ') || (*args == '\t'))
      {
        args++;
      }

      duty_ul = strtoul(args, &endptr, 0);
      if ((endptr == args) || (*endptr != '\0'))
      {
        (void)snprintf(out, out_size, "mcu pwm duty: use duty <ch> <0-100>\r\n");
        return 1;
      }

      if (duty_ul > 100UL)
      {
        duty_ul = 100UL;
      }

      {
        int st = TarsMcu_PwmSetDuty(ch, (float)duty_ul);

        if (st != 0)
        {
          (void)snprintf(out,
                         out_size,
                         "mcu pwm duty: ch=%s err=%s\r\n",
                         ch,
                         TarsMcu_ResErrText(st));
        }
        else
        {
          (void)snprintf(out,
                         out_size,
                         "mcu pwm duty: ch=%s duty=%lu\r\n",
                         ch,
                         duty_ul);
        }
      }
      return 1;
    }

    if (strncmp(rest, "freq ", 5) == 0)
    {
      if (sscanf(rest + 5, "%15s %lu", tim_id, &val) != 2)
      {
        (void)snprintf(out, out_size, "mcu pwm freq: use freq <timN> <hz>\r\n");
        return 1;
      }

      {
        int st = TarsMcu_PwmSetFreq(tim_id, (uint32_t)val);

        if (st != 0)
        {
          (void)snprintf(out,
                         out_size,
                         "mcu pwm freq: tim=%s err=%s\r\n",
                         tim_id,
                         TarsMcu_ResErrText(st));
        }
        else
        {
          (void)snprintf(out,
                         out_size,
                         "mcu pwm freq: tim=%s hz=%lu\r\n",
                         tim_id,
                         val);
        }
      }
      return 1;
    }

    (void)snprintf(out, out_size, "mcu pwm: use list|status|enable|duty|freq\r\n");
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

        if (wr != 0)
        {
          (void)snprintf(out,
                         out_size,
                         "mcu gpio write: pin=%s err=%s\r\n",
                         pin_name,
                         TarsMcu_ResErrText(wr));
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
        (void)snprintf(out, out_size, "mcu gpio read: pin=%s err=scope\r\n", pin_name);
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
