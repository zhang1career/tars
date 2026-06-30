#include "tars_mcu_pinmap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static int pinmap_stricmp(const char *a, const char *b)
{
  unsigned char ca;
  unsigned char cb;

  if (a == NULL || b == NULL)
  {
    return 1;
  }

  for (;;)
  {
    ca = (unsigned char)tolower((unsigned char)*a++);
    cb = (unsigned char)tolower((unsigned char)*b++);

    if (ca != cb)
    {
      return (int)ca - (int)cb;
    }

    if (ca == '\0')
    {
      return 0;
    }
  }
}

const char *TarsOwner_ToString(tars_owner_t owner)
{
  switch (owner)
  {
  case TARS_OWNER_NONE:
    return "none";
  case TARS_OWNER_GPIO:
    return "gpio";
  case TARS_OWNER_PWM:
    return "pwm";
  case TARS_OWNER_FOC:
    return "foc";
  case TARS_OWNER_SYSTEM:
    return "system";
  default:
    return "?";
  }
}

int TarsOwner_Parse(const char *text, tars_owner_t *owner_out)
{
  if ((text == NULL) || (owner_out == NULL))
  {
    return -1;
  }

  if (pinmap_stricmp(text, "none") == 0)
  {
    *owner_out = TARS_OWNER_NONE;
  }
  else if (pinmap_stricmp(text, "gpio") == 0)
  {
    *owner_out = TARS_OWNER_GPIO;
  }
  else if (pinmap_stricmp(text, "pwm") == 0)
  {
    *owner_out = TARS_OWNER_PWM;
  }
  else if (pinmap_stricmp(text, "foc") == 0)
  {
    *owner_out = TARS_OWNER_FOC;
  }
  else if (pinmap_stricmp(text, "system") == 0)
  {
    *owner_out = TARS_OWNER_SYSTEM;
  }
  else
  {
    return -1;
  }

  return 0;
}

static int pinmap_parse_pin_name(const char *name, char *bank_out, int *num_out)
{
  const char *p = name;
  char bank;
  int num;

  if (name == NULL || bank_out == NULL || num_out == NULL)
  {
    return -1;
  }

  if (p[0] == 'p')
  {
    p++;
  }

  bank = (char)tolower((unsigned char)p[0]);
  if (bank < 'a' || bank > 'i')
  {
    return -1;
  }

  num = (int)strtol(p + 1, NULL, 10);
  if (num < 0 || num > 15)
  {
    return -1;
  }

  *bank_out = bank;
  *num_out = num;
  return 0;
}

static int pinmap_match_name(const char *entry_name, const char *alias,
                             const char *name, char bank, int pin_num)
{
  char entry_bank;
  int entry_num;

  if (name == NULL)
  {
    return 0;
  }

  if ((alias != NULL) && (pinmap_stricmp(alias, name) == 0))
  {
    return 1;
  }

  if ((entry_name != NULL) && (pinmap_stricmp(entry_name, name) == 0))
  {
    return 1;
  }

  if (pinmap_parse_pin_name(entry_name, &entry_bank, &entry_num) != 0)
  {
    return 0;
  }

  return (entry_bank == bank && entry_num == pin_num) ? 1 : 0;
}

int TarsMcuPinmap_ResolveGpio(const char *name, GPIO_TypeDef **port_out, uint16_t *pin_out)
{
  uint32_t count = 0U;
  const tars_mcu_gpio_entry_t *table = TarsMcuPinmap_GetGpioTable(&count);
  char bank = '\0';
  int pin_num = -1;
  uint32_t i;

  if ((name == NULL) || (port_out == NULL) || (pin_out == NULL))
  {
    return -1;
  }

  if (pinmap_parse_pin_name(name, &bank, &pin_num) != 0)
  {
    bank = '\0';
    pin_num = -1;
  }

  for (i = 0U; i < count; i++)
  {
    if (pinmap_match_name(table[i].pin_name, table[i].alias, name, bank, pin_num))
    {
      *port_out = table[i].port;
      *pin_out = table[i].hal_pin;
      return 0;
    }
  }

  return -1;
}

int TarsMcuPinmap_ResolvePwm(const char *name, const tars_mcu_pwm_entry_t **entry_out)
{
  uint32_t count = 0U;
  const tars_mcu_pwm_entry_t *table = TarsMcuPinmap_GetPwmTable(&count);
  uint32_t i;

  if ((name == NULL) || (entry_out == NULL))
  {
    return -1;
  }

  for (i = 0U; i < count; i++)
  {
    if (pinmap_match_name(table[i].channel, table[i].alias, name, '\0', -1))
    {
      *entry_out = &table[i];
      return 0;
    }
  }

  return -1;
}

int TarsMcuPinmap_FindCatalog(const char *id, const tars_res_catalog_entry_t **entry_out,
                              uint32_t *index_out)
{
  uint32_t count = 0U;
  const tars_res_catalog_entry_t *table = TarsMcuPinmap_GetResCatalog(&count);
  uint32_t i;

  if ((id == NULL) || (entry_out == NULL))
  {
    return -1;
  }

  for (i = 0U; i < count; i++)
  {
    if (pinmap_stricmp(table[i].id, id) == 0)
    {
      *entry_out = &table[i];
      if (index_out != NULL)
      {
        *index_out = i;
      }
      return 0;
    }
  }

  return -1;
}

void TarsMcuPinmap_FormatGpioList(char *out, uint32_t out_size)
{
  uint32_t count = 0U;
  const tars_mcu_gpio_entry_t *table = TarsMcuPinmap_GetGpioTable(&count);
  uint32_t i;

  if ((out == NULL) || (out_size == 0U))
  {
    return;
  }

  out[0] = '\0';

  for (i = 0U; i < count; i++)
  {
    char line[96];

    if (table[i].alias != NULL)
    {
      (void)snprintf(line,
                     sizeof(line),
                     "  %s (%s) owner=%s\r\n",
                     table[i].pin_name,
                     table[i].alias,
                     TarsOwner_ToString(table[i].default_owner));
    }
    else
    {
      (void)snprintf(line,
                     sizeof(line),
                     "  %s owner=%s\r\n",
                     table[i].pin_name,
                     TarsOwner_ToString(table[i].default_owner));
    }

    strncat(out, line, out_size - strlen(out) - 1U);
  }
}

void TarsMcuPinmap_FormatPwmList(char *out, uint32_t out_size)
{
  uint32_t count = 0U;
  const tars_mcu_pwm_entry_t *table = TarsMcuPinmap_GetPwmTable(&count);
  uint32_t i;

  if ((out == NULL) || (out_size == 0U))
  {
    return;
  }

  out[0] = '\0';

  for (i = 0U; i < count; i++)
  {
    char line[112];

    (void)snprintf(line,
                   sizeof(line),
                   "  %s -> %s pin=%s owner=%s\r\n",
                   table[i].channel,
                   (table[i].advanced_tim != 0U) ? "adv" : "pwm",
                   table[i].pin_name,
                   TarsOwner_ToString(table[i].default_owner));
    strncat(out, line, out_size - strlen(out) - 1U);
  }
}

void TarsMcuPinmap_FormatPeriphMap(char *out, uint32_t out_size)
{
  uint32_t count = 0U;
  const tars_mcu_periph_entry_t *table = TarsMcuPinmap_GetPeriphTable(&count);
  uint32_t i;

  if ((out == NULL) || (out_size == 0U))
  {
    return;
  }

  out[0] = '\0';

  for (i = 0U; i < count; i++)
  {
    char line[48];

    (void)snprintf(line,
                   sizeof(line),
                   "  %-12s -> %s\r\n",
                   table[i].signal,
                   table[i].pin_name);
    strncat(out, line, out_size - strlen(out) - 1U);
  }
}
