#include "tars_mcu.h"
#include "tars_mcu_pinmap.h"
#include "tars_res_mgr.h"
#include "tars_res_pwm.h"
#include "tars_res_dac.h"
#include "tars_res_awg.h"
#include "tars_tenant.h"
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

static int mcu_rest_wants_help(const char *rest)
{
  return ((rest == NULL) || (rest[0] == '\0') || (strcmp(rest, "help") == 0)) ? 1 : 0;
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
                 "  mcu help          Show this help\r\n"
                 "  mcu info          Board and firmware info\r\n"
                 "  mcu res           Resource tenants and grants\r\n"
                 "  mcu pwm           PWM channels and timers\r\n"
                 "  mcu dac           Static DAC output level\r\n"
                 "  mcu awg           Arbitrary waveform generator\r\n"
                 "  mcu gpio          GPIO read and write\r\n"
                 "  mcu pinmap        Pin and peripheral map\r\n"
                 "  mcu spec          Design and runtime specs\r\n"
                 "try: mcu <subcmd> help for subcommand usage\r\n");
}

static void mcu_res_help(char *out, uint32_t out_size)
{
  if ((out == NULL) || (out_size == 0U))
  {
    return;
  }

  (void)snprintf(out,
                 out_size,
                 "mcu res commands:\r\n"
                 "  list                 List pin-map resources and tenants\r\n"
                 "  status <id>          Show resource tenant and active state\r\n"
                 "  grant <id> <tenant>  Assign tenant (or none to clear)\r\n"
                 "  save                 Persist grants and PWM profile to flash\r\n"
                 "  load                 Restore grants and PWM profile from flash\r\n"
                 "  clear                Erase persisted profile from flash\r\n"
                 "  profile show         Show stored profile snapshot\r\n");
}

static void mcu_pwm_help(char *out, uint32_t out_size)
{
  if ((out == NULL) || (out_size == 0U))
  {
    return;
  }

  (void)snprintf(out,
                 out_size,
                 "mcu pwm commands:\r\n"
                 "  list                 List PWM channels on this board\r\n"
                 "  status <ch>          Show channel runtime state\r\n"
                 "  enable <ch> <0|1>    Start or stop PWM output\r\n"
                 "  safe                 Force TIM1 bridge off (MOE=0, CCR=0)\r\n"
                 "  duty <ch> <0-100>    Set duty cycle (%%)\r\n"
                 "  freq <timN> <hz>     Set timer frequency (shared per TIM)\r\n"
                 "  polarity <ch> [pol]  Query or set output polarity (high|low)\r\n"
                 "  complement <ch> [0|1]  Enable/disable CHxN (advanced TIM only)\r\n"
                 "  complement persist <ch> [0|1]  Auto-enable complement on boot\r\n"
                 "  link ...             Phase-sync linked channels\r\n"
                 "  persist <ch> [0|1]   Auto-enable channel on boot\r\n");
}

static void mcu_dac_help(char *out, uint32_t out_size)
{
  if ((out == NULL) || (out_size == 0U))
  {
    return;
  }

  (void)snprintf(out,
                 out_size,
                 "mcu dac commands:\r\n"
                 "  list                 List DAC channels on this board\r\n"
                 "  status <ch>          Show channel runtime state\r\n"
                 "  enable <ch> <0|1>    Start or stop DAC output\r\n"
                 "  value <ch> <0-100>   Set output level (%% of full scale)\r\n");
}

static void mcu_awg_help(char *out, uint32_t out_size)
{
  if ((out == NULL) || (out_size == 0U))
  {
    return;
  }

  (void)snprintf(out,
                 out_size,
                 "mcu awg commands:\r\n"
                 "  status <ch>          Show channel waveform and playback state\r\n"
                 "  gen <ch> <wave> ...  Generate built-in waveform into buffer\r\n"
                 "  freq <ch> <hz>       Set output frequency\r\n"
                 "  enable <ch> <0|1>    Start or stop waveform playback\r\n"
                 "  link ...             Phase-sync linked channels\r\n");
}

static void mcu_gpio_help(char *out, uint32_t out_size)
{
  if ((out == NULL) || (out_size == 0U))
  {
    return;
  }

  (void)snprintf(out,
                 out_size,
                 "mcu gpio commands:\r\n"
                 "  list                      List GPIO pins on this board\r\n"
                 "  read <pgNN|alias>         Read pin level\r\n"
                 "  write <pgNN|alias> <0|1>  Write pin level (grant tenant first)\r\n");
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

  if (mcu_str_eq(sub, "spec"))
  {
    if ((rest[0] == '\0') || mcu_str_eq(rest, "list"))
    {
      TarsMcu_FormatSpecList(out, out_size);
      return 1;
    }

    if (TarsMcu_FormatSpec(rest, out, out_size) != 0)
    {
      return 1;
    }
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

    if (mcu_rest_wants_help(rest))
    {
      mcu_res_help(out, out_size);
      return 1;
    }

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
      if (sscanf(rest + 6, "%23s %15s", id, owner_text) != 2)
      {
        (void)snprintf(out,
                       out_size,
                       "mcu res grant: use res grant <id> <tenant|none>\r\n");
        return 1;
      }

      if ((strcmp(owner_text, "none") != 0) &&
          (TarsTenant_ValidateGrantName(owner_text) != 0))
      {
        (void)snprintf(out, out_size, "mcu res grant: bad tenant %s\r\n", owner_text);
        return 1;
      }

      {
        int st = TarsMcu_ResGrant(id, owner_text);

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
                         "mcu res grant: id=%s tenant=%s\r\n",
                         id,
                         owner_text);
        }
      }
      return 1;
    }

    if (mcu_str_eq(rest, "save"))
    {
      int st = TarsMcu_ProfileSave();

      if (st != 0)
      {
        (void)snprintf(out,
                       out_size,
                       "mcu res save: err=%s\r\n",
                       TarsMcu_ProfileErrText(st));
      }
      else
      {
        (void)snprintf(out, out_size, "mcu res save: ok\r\n");
      }
      return 1;
    }

    if (mcu_str_eq(rest, "load"))
    {
      int st = TarsMcu_ProfileLoad();

      if (st != 0)
      {
        (void)snprintf(out,
                       out_size,
                       "mcu res load: err=%s\r\n",
                       TarsMcu_ProfileErrText(st));
      }
      else
      {
        (void)snprintf(out, out_size, "mcu res load: ok\r\n");
      }
      return 1;
    }

    if (mcu_str_eq(rest, "clear"))
    {
      int st = TarsMcu_ProfileClear();

      if (st != 0)
      {
        (void)snprintf(out,
                       out_size,
                       "mcu res clear: err=%s\r\n",
                       TarsMcu_ProfileErrText(st));
      }
      else
      {
        (void)snprintf(out, out_size, "mcu res clear: ok\r\n");
      }
      return 1;
    }

    if (strncmp(rest, "profile ", 8) == 0)
    {
      if (mcu_str_eq(rest + 8, "show"))
      {
        (void)TarsMcu_ProfileFormatStored(out, out_size);
        return 1;
      }
    }

    mcu_res_help(out, out_size);
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

    if (mcu_rest_wants_help(rest) || mcu_str_eq(rest, "status"))
    {
      mcu_pwm_help(out, out_size);
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

    if (mcu_str_eq(rest, "safe"))
    {
      (void)TarsResPwm_Tim1ForceSafe();
      (void)snprintf(out, out_size, "mcu pwm safe: tim1 moe=0 ccr=0\r\n");
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
          if (TarsResPwm_IsRunning(ch) != 0)
          {
            (void)snprintf(out,
                           out_size,
                           "mcu pwm duty: ch=%s duty=%lu\r\n",
                           ch,
                           duty_ul);
          }
          else
          {
            (void)snprintf(out,
                           out_size,
                           "mcu pwm duty: ch=%s duty=%lu (stored; use enable to drive)\r\n",
                           ch,
                           duty_ul);
          }
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

    if (strncmp(rest, "polarity ", 9) == 0)
    {
      char pol[16];

      if (sscanf(rest + 9, "%23s %15s", ch, pol) == 1)
      {
        int cur = 0;

        if (TarsResPwm_GetPolarity(ch, &cur) != 0)
        {
          cur = 0;
        }

        (void)snprintf(out,
                       out_size,
                       "mcu pwm polarity: ch=%s pol=%s\r\n",
                       ch,
                       (cur != 0) ? "low" : "high");
        return 1;
      }

      if (sscanf(rest + 9, "%23s %15s", ch, pol) != 2)
      {
        (void)snprintf(out,
                       out_size,
                       "mcu pwm polarity: use polarity <ch> <high|low>\r\n");
        return 1;
      }

      {
        int low = 0;
        int st = TarsResPwm_ParsePolarity(pol, &low);

        if (st != 0)
        {
          (void)snprintf(out,
                         out_size,
                         "mcu pwm polarity: use polarity <ch> <high|low>\r\n");
          return 1;
        }

        st = TarsResPwm_SetPolarity(ch, low);
        if (st != 0)
        {
          (void)snprintf(out,
                         out_size,
                         "mcu pwm polarity: ch=%s err=%s\r\n",
                         ch,
                         TarsMcu_ResErrText(st));
        }
        else
        {
          (void)snprintf(out,
                         out_size,
                         "mcu pwm polarity: ch=%s pol=%s\r\n",
                         ch,
                         (low != 0) ? "low" : "high");
        }
      }
      return 1;
    }

    if (strncmp(rest, "complement ", 11) == 0)
    {
      const char *args = rest + 11;

      if (strncmp(args, "persist ", 8) == 0)
      {
        const char *pargs = args + 8;

        if (sscanf(pargs, "%23s %lu", ch, &val) == 1)
        {
          int boot = 0;

          if (TarsResPwm_GetComplementPersist(ch, &boot) != 0)
          {
            boot = 0;
          }

          (void)snprintf(out,
                         out_size,
                         "mcu pwm complement persist: ch=%s boot=%d\r\n",
                         ch,
                         boot);
          return 1;
        }

        if (sscanf(pargs, "%23s %lu", ch, &val) != 2)
        {
          (void)snprintf(out,
                         out_size,
                         "mcu pwm complement persist: use complement persist <ch> <0|1>\r\n");
          return 1;
        }

        {
          int st = TarsResPwm_SetComplementPersist(ch, (int)val);

          if (st != 0)
          {
            (void)snprintf(out,
                           out_size,
                           "mcu pwm complement persist: ch=%s err=%s\r\n",
                           ch,
                           TarsMcu_ResErrText(st));
          }
          else
          {
            (void)snprintf(out,
                           out_size,
                           "mcu pwm complement persist: ch=%s boot=%lu\r\n",
                           ch,
                           val);
          }
        }
        return 1;
      }

      if (sscanf(args, "%23s %lu", ch, &val) == 1)
      {
        int on = TarsResPwm_IsComplementRunning(ch);

        (void)snprintf(out,
                       out_size,
                       "mcu pwm complement: ch=%s comp=%d\r\n",
                       ch,
                       on);
        return 1;
      }

      if (sscanf(args, "%23s %lu", ch, &val) != 2)
      {
        (void)snprintf(out,
                       out_size,
                       "mcu pwm complement: use complement <ch> <0|1>\r\n");
        return 1;
      }

      {
        int st = TarsResPwm_SetComplement(ch, (int)val);

        if (st != 0)
        {
          (void)snprintf(out,
                         out_size,
                         "mcu pwm complement: ch=%s err=%s\r\n",
                         ch,
                         TarsMcu_ResErrText(st));
        }
        else
        {
          (void)snprintf(out,
                         out_size,
                         "mcu pwm complement: ch=%s comp=%lu\r\n",
                         ch,
                         val);
        }
      }
      return 1;
    }

    if (strncmp(rest, "link", 4) == 0 &&
        ((rest[4] == '\0') || (rest[4] == ' ')))
    {
      const char *lp = rest + 4;

      while (*lp == ' ')
      {
        lp++;
      }

      if (mcu_str_eq(lp, "status"))
      {
        (void)TarsResPwm_LinkGetStatus(out, out_size);
        return 1;
      }

      if (mcu_str_eq(lp, "resync"))
      {
        int st = TarsResPwm_LinkResync();

        if (st != 0)
        {
          (void)snprintf(out,
                         out_size,
                         "mcu pwm link resync: err=%s\r\n",
                         TarsMcu_ResErrText(st));
        }
        else
        {
          (void)TarsResPwm_LinkGetStatus(out, out_size);
        }
        return 1;
      }

      if (strncmp(lp, "offset ", 7) == 0)
      {
        long off = 0L;

        if (sscanf(lp + 7, "%ld", &off) != 1)
        {
          (void)snprintf(out,
                         out_size,
                         "mcu pwm link offset: use link offset <ticks>\r\n");
          return 1;
        }

        (void)TarsResPwm_LinkSetOffset((int32_t)off);
        (void)TarsResPwm_LinkGetStatus(out, out_size);
        return 1;
      }

      {
        long on = 0L;
        long off = 0L;
        int n = sscanf(lp, "%ld %ld", &on, &off);

        if (n < 1)
        {
          (void)snprintf(out,
                         out_size,
                         "mcu pwm link: use link <0|1> [offset]|offset <ticks>|resync|status\r\n");
          return 1;
        }

        if (n >= 2)
        {
          (void)TarsResPwm_LinkSet((int)on, (int32_t)off);
        }
        else
        {
          (void)TarsResPwm_LinkEnable((int)on);
        }

        (void)TarsResPwm_LinkGetStatus(out, out_size);
        return 1;
      }
    }

    if (strncmp(rest, "persist ", 8) == 0)
    {
      unsigned long boot_ul = 0UL;

      if (sscanf(rest + 8, "%23s %lu", ch, &boot_ul) != 2)
      {
        int cur = 0;

        if (sscanf(rest + 8, "%23s", ch) != 1)
        {
          (void)snprintf(out, out_size, "mcu pwm persist: use persist <ch> <0|1>\r\n");
          return 1;
        }

        if (TarsMcu_PwmGetPersist(ch, &cur) != 0)
        {
          cur = 0;
        }

        (void)snprintf(out,
                       out_size,
                       "mcu pwm persist: ch=%s boot=%d\r\n",
                       ch,
                       cur);
        return 1;
      }

      {
        int st = TarsMcu_PwmSetPersist(ch, (int)boot_ul);

        if (st != 0)
        {
          (void)snprintf(out,
                         out_size,
                         "mcu pwm persist: ch=%s err=%s\r\n",
                         ch,
                         TarsMcu_ResErrText(st));
        }
        else
        {
          (void)snprintf(out,
                         out_size,
                         "mcu pwm persist: ch=%s boot=%lu\r\n",
                         ch,
                         boot_ul);
        }
      }
      return 1;
    }

    mcu_pwm_help(out, out_size);
    return 1;
  }

  if (mcu_str_eq(sub, "dac"))
  {
    char ch[24];
    unsigned long val = 0UL;
    unsigned long level_ul = 0UL;

    if (mcu_str_eq(rest, "list"))
    {
      (void)snprintf(out, out_size, "mcu dac list (%s):\r\n", TarsMcuPinmap_BoardId());
      TarsMcuPinmap_FormatDacList(out + strlen(out), out_size - (uint32_t)strlen(out));
      return 1;
    }

    if (mcu_rest_wants_help(rest) || mcu_str_eq(rest, "status"))
    {
      mcu_dac_help(out, out_size);
      return 1;
    }

    if (strncmp(rest, "status ", 7) == 0)
    {
      if (sscanf(rest + 7, "%23s", ch) != 1)
      {
        (void)snprintf(out, out_size, "mcu dac status: use dac status <ch>\r\n");
        return 1;
      }
      (void)TarsResDac_GetStatus(ch, out, out_size);
      return 1;
    }

    if (strncmp(rest, "enable ", 7) == 0)
    {
      if (sscanf(rest + 7, "%23s %lu", ch, &val) != 2)
      {
        (void)snprintf(out, out_size, "mcu dac enable: use enable <ch> <0|1>\r\n");
        return 1;
      }

      {
        int st = TarsMcu_DacEnable(ch, (int)val);

        if (st != 0)
        {
          (void)snprintf(out,
                         out_size,
                         "mcu dac enable: ch=%s err=%s\r\n",
                         ch,
                         TarsMcu_ResErrText(st));
        }
        else
        {
          (void)snprintf(out,
                         out_size,
                         "mcu dac enable: ch=%s val=%lu\r\n",
                         ch,
                         val);
        }
      }
      return 1;
    }

    if (strncmp(rest, "value ", 6) == 0)
    {
      const char *args = rest + 6;
      char *endptr = NULL;

      if (sscanf(args, "%23s", ch) != 1)
      {
        (void)snprintf(out, out_size, "mcu dac value: use value <ch> <0-100>\r\n");
        return 1;
      }

      args += strlen(ch);
      while ((*args == ' ') || (*args == '\t'))
      {
        args++;
      }

      level_ul = strtoul(args, &endptr, 0);
      if ((endptr == args) || (*endptr != '\0'))
      {
        (void)snprintf(out, out_size, "mcu dac value: use value <ch> <0-100>\r\n");
        return 1;
      }

      if (level_ul > 100UL)
      {
        level_ul = 100UL;
      }

      {
        int st = TarsMcu_DacSetLevel(ch, (float)level_ul);

        if (st != 0)
        {
          (void)snprintf(out,
                         out_size,
                         "mcu dac value: ch=%s err=%s\r\n",
                         ch,
                         TarsMcu_ResErrText(st));
        }
        else
        {
          if (TarsResDac_IsRunning(ch) != 0)
          {
            (void)snprintf(out,
                           out_size,
                           "mcu dac value: ch=%s level=%lu\r\n",
                           ch,
                           level_ul);
          }
          else
          {
            (void)snprintf(out,
                           out_size,
                           "mcu dac value: ch=%s level=%lu (stored; use enable to drive)\r\n",
                           ch,
                           level_ul);
          }
        }
      }
      return 1;
    }

    mcu_dac_help(out, out_size);
    return 1;
  }

  if (mcu_str_eq(sub, "awg"))
  {
    char ch[24];
    unsigned long val = 0UL;

    if (mcu_rest_wants_help(rest) || mcu_str_eq(rest, "status"))
    {
      mcu_awg_help(out, out_size);
      return 1;
    }

    if (strncmp(rest, "status ", 7) == 0)
    {
      if (sscanf(rest + 7, "%23s", ch) != 1)
      {
        (void)snprintf(out, out_size, "mcu awg status: use awg status <ch>\r\n");
        return 1;
      }
      (void)TarsResAwg_GetStatus(ch, out, out_size);
      return 1;
    }

    if (strncmp(rest, "gen ", 4) == 0)
    {
      char wave_name[16];
      unsigned long points = 256UL;
      double ampl = 100.0;
      double offset = 50.0;
      double duty = 50.0;
      tars_awg_wave_t wave;
      int n;

      n = sscanf(rest + 4, "%23s %15s %lu %lf %lf %lf",
                 ch, wave_name, &points, &ampl, &offset, &duty);
      if (n < 2)
      {
        (void)snprintf(out,
                       out_size,
                       "mcu awg gen: use gen <ch> <wave> [pts] [ampl%%] [off%%] [duty%%]\r\n");
        return 1;
      }

      if (TarsResAwg_ParseWave(wave_name, &wave) != 0)
      {
        (void)snprintf(out,
                       out_size,
                       "mcu awg gen: bad wave %s (sin|square|tri|saw|dc|noise)\r\n",
                       wave_name);
        return 1;
      }

      {
        int st = TarsResAwg_Generate(ch, wave, (uint32_t)points, 0U,
                                     (float)ampl, (float)offset, (float)duty);

        if (st != 0)
        {
          (void)snprintf(out,
                         out_size,
                         "mcu awg gen: ch=%s err=%s\r\n",
                         ch,
                         TarsMcu_ResErrText(st));
        }
        else
        {
          (void)TarsResAwg_GetStatus(ch, out, out_size);
        }
      }
      return 1;
    }

    if (strncmp(rest, "freq ", 5) == 0)
    {
      if (sscanf(rest + 5, "%23s %lu", ch, &val) != 2)
      {
        (void)snprintf(out, out_size, "mcu awg freq: use freq <ch> <hz>\r\n");
        return 1;
      }

      {
        int st = TarsResAwg_SetFreq(ch, (uint32_t)val);

        if (st != 0)
        {
          (void)snprintf(out,
                         out_size,
                         "mcu awg freq: ch=%s err=%s\r\n",
                         ch,
                         TarsMcu_ResErrText(st));
        }
        else
        {
          (void)TarsResAwg_GetStatus(ch, out, out_size);
        }
      }
      return 1;
    }

    if (strncmp(rest, "link", 4) == 0 &&
        ((rest[4] == '\0') || (rest[4] == ' ')))
    {
      const char *lp = rest + 4;

      while (*lp == ' ')
      {
        lp++;
      }

      if (mcu_str_eq(lp, "status"))
      {
        (void)TarsResAwg_LinkGetStatus(out, out_size);
        return 1;
      }

      if (mcu_str_eq(lp, "resync"))
      {
        int st = TarsResAwg_LinkResync();

        if (st != 0)
        {
          (void)snprintf(out,
                         out_size,
                         "mcu awg link resync: err=%s\r\n",
                         TarsMcu_ResErrText(st));
        }
        else
        {
          (void)TarsResAwg_LinkGetStatus(out, out_size);
        }
        return 1;
      }

      if (strncmp(lp, "offset ", 7) == 0)
      {
        long off = 0L;

        if (sscanf(lp + 7, "%ld", &off) != 1)
        {
          (void)snprintf(out,
                         out_size,
                         "mcu awg link offset: use link offset <samples>\r\n");
          return 1;
        }

        (void)TarsResAwg_LinkSetOffset((int32_t)off);
        (void)TarsResAwg_LinkGetStatus(out, out_size);
        return 1;
      }

      {
        long on = 0L;
        long off = 0L;
        int n = sscanf(lp, "%ld %ld", &on, &off);

        if (n < 1)
        {
          (void)snprintf(out,
                         out_size,
                         "mcu awg link: use link <0|1> [offset]|offset <samples>|resync|status\r\n");
          return 1;
        }

        if (n >= 2)
        {
          (void)TarsResAwg_LinkSet((int)on, (int32_t)off);
        }
        else
        {
          (void)TarsResAwg_LinkEnable((int)on);
        }

        (void)TarsResAwg_LinkGetStatus(out, out_size);
        return 1;
      }
    }

    if (strncmp(rest, "enable ", 7) == 0)
    {
      if (sscanf(rest + 7, "%23s %lu", ch, &val) != 2)
      {
        (void)snprintf(out, out_size, "mcu awg enable: use enable <ch> <0|1>\r\n");
        return 1;
      }

      {
        int st = TarsResAwg_Enable(ch, (int)val);

        if (st != 0)
        {
          (void)snprintf(out,
                         out_size,
                         "mcu awg enable: ch=%s err=%s\r\n",
                         ch,
                         TarsMcu_ResErrText(st));
        }
        else
        {
          (void)snprintf(out,
                         out_size,
                         "mcu awg enable: ch=%s val=%lu\r\n",
                         ch,
                         val);
        }
      }
      return 1;
    }

    mcu_awg_help(out, out_size);
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

    if (mcu_rest_wants_help(rest))
    {
      mcu_gpio_help(out, out_size);
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

    mcu_gpio_help(out, out_size);
    return 1;
  }

  if ((strcmp(sub, "tim") == 0) ||
      (strcmp(sub, "adc") == 0) ||
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
