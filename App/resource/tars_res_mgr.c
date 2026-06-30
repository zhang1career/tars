#include "tars_res_mgr.h"
#include "cmsis_os.h"
#include <stdio.h>
#include <string.h>

#define TARS_RES_MAX   48U

typedef struct {
  tars_owner_t owner;
  tars_owner_t active;
} tars_res_slot_t;

static tars_res_slot_t s_slots[TARS_RES_MAX];
static uint32_t s_slot_count;
static osMutexId s_mutex;

static int res_find_index(const char *id, uint32_t *index_out)
{
  const tars_res_catalog_entry_t *entry = NULL;

  if (TarsMcuPinmap_FindCatalog(id, &entry, index_out) != 0)
  {
    return TARS_RES_ERR_SCOPE;
  }

  if (*index_out >= s_slot_count)
  {
    return TARS_RES_ERR_SCOPE;
  }

  return 0;
}

static int res_find_gpio_index_by_pin(const char *pin_name, uint32_t *index_out)
{
  uint32_t count = 0U;
  const tars_mcu_gpio_entry_t *gpio = TarsMcuPinmap_GetGpioTable(&count);
  uint32_t i;

  for (i = 0U; i < count; i++)
  {
    const tars_res_catalog_entry_t *cat = NULL;
    uint32_t idx = 0U;

    if (TarsMcuPinmap_FindCatalog(gpio[i].pin_name, &cat, &idx) != 0)
    {
      continue;
    }

    if ((pin_name != NULL) &&
        (strcmp(gpio[i].pin_name, pin_name) == 0))
    {
      *index_out = idx;
      return 0;
    }
  }

  (void)gpio;
  return TARS_RES_ERR_SCOPE;
}

static int res_grant_index(uint32_t idx, tars_owner_t new_owner)
{
  if (idx >= s_slot_count)
  {
    return TARS_RES_ERR_SCOPE;
  }

  if (s_slots[idx].active != TARS_OWNER_NONE)
  {
    return TARS_RES_ERR_ACTIVE;
  }

  if (s_slots[idx].owner == TARS_OWNER_SYSTEM)
  {
    return TARS_RES_ERR_SYSTEM;
  }

  s_slots[idx].owner = new_owner;
  return 0;
}

static int res_acquire_index(uint32_t idx, tars_owner_t owner)
{
  if (idx >= s_slot_count)
  {
    return TARS_RES_ERR_SCOPE;
  }

  if ((s_slots[idx].owner != owner) && (s_slots[idx].owner != TARS_OWNER_NONE))
  {
    return TARS_RES_ERR_OWNER;
  }

  if ((s_slots[idx].active != TARS_OWNER_NONE) && (s_slots[idx].active != owner))
  {
    return TARS_RES_ERR_ACTIVE;
  }

  s_slots[idx].active = owner;
  return 0;
}

static int res_release_index(uint32_t idx, tars_owner_t owner)
{
  if (idx >= s_slot_count)
  {
    return TARS_RES_ERR_SCOPE;
  }

  if (s_slots[idx].active != owner)
  {
    return TARS_RES_ERR_OWNER;
  }

  s_slots[idx].active = TARS_OWNER_NONE;
  return 0;
}

static int res_acquire_pair_sorted(uint32_t a, uint32_t b, tars_owner_t owner)
{
  uint32_t first = a;
  uint32_t second = b;
  int st;

  if (first > second)
  {
    first = b;
    second = a;
  }

  st = res_acquire_index(first, owner);
  if (st != 0)
  {
    return st;
  }

  st = res_acquire_index(second, owner);
  if (st != 0)
  {
    (void)res_release_index(first, owner);
    return st;
  }

  return 0;
}

static int res_release_pair_sorted(uint32_t a, uint32_t b, tars_owner_t owner)
{
  uint32_t first = a;
  uint32_t second = b;

  if (first > second)
  {
    first = b;
    second = a;
  }

  (void)res_release_index(second, owner);
  (void)res_release_index(first, owner);
  return 0;
}

void TarsResMgr_Init(void)
{
  uint32_t count = 0U;
  const tars_res_catalog_entry_t *cat = TarsMcuPinmap_GetResCatalog(&count);
  uint32_t i;

  if (count > TARS_RES_MAX)
  {
    count = TARS_RES_MAX;
  }

  s_slot_count = count;

  for (i = 0U; i < count; i++)
  {
    s_slots[i].owner = cat[i].default_owner;
    s_slots[i].active = TARS_OWNER_NONE;
  }

  if (s_mutex == NULL)
  {
    osMutexDef(res_mgr_mutex);
    s_mutex = osMutexCreate(osMutex(res_mgr_mutex));
  }
}

int TarsResMgr_Grant(const char *id, tars_owner_t new_owner)
{
  uint32_t idx = 0U;
  const tars_res_catalog_entry_t *entry = NULL;
  int st;

  if (id == NULL)
  {
    return TARS_RES_ERR_PARAM;
  }

  if (osMutexWait(s_mutex, 100U) != osOK)
  {
    return TARS_RES_ERR_PARAM;
  }

  st = res_find_index(id, &idx);
  if (st == 0)
  {
    st = res_grant_index(idx, new_owner);
  }

  if ((st == 0) && (TarsMcuPinmap_FindCatalog(id, &entry, &idx) == 0) &&
      (entry->kind == TARS_RES_KIND_PWM))
  {
    const tars_mcu_pwm_entry_t *pwm = NULL;

    if (TarsMcuPinmap_ResolvePwm(id, &pwm) == 0)
    {
      uint32_t pin_idx = 0U;

      if (res_find_gpio_index_by_pin(pwm->pin_name, &pin_idx) == 0)
      {
        if ((s_slots[pin_idx].active == TARS_OWNER_NONE) &&
            (s_slots[pin_idx].owner != TARS_OWNER_SYSTEM))
        {
          if ((s_slots[pin_idx].owner == TARS_OWNER_NONE) ||
              (s_slots[pin_idx].owner == new_owner))
          {
            s_slots[pin_idx].owner = new_owner;
          }
          else
          {
            st = TARS_RES_ERR_OWNER;
          }
        }
        else if (s_slots[pin_idx].active != TARS_OWNER_NONE)
        {
          st = TARS_RES_ERR_ACTIVE;
        }
      }
    }
  }

  osMutexRelease(s_mutex);
  return st;
}

int TarsResMgr_Acquire(const char *id, tars_owner_t owner)
{
  uint32_t idx = 0U;
  int st;

  if (id == NULL)
  {
    return TARS_RES_ERR_PARAM;
  }

  if (osMutexWait(s_mutex, 100U) != osOK)
  {
    return TARS_RES_ERR_PARAM;
  }

  st = res_find_index(id, &idx);
  if (st == 0)
  {
    st = res_acquire_index(idx, owner);
  }

  osMutexRelease(s_mutex);
  return st;
}

int TarsResMgr_Release(const char *id, tars_owner_t owner)
{
  uint32_t idx = 0U;
  int st;

  if (id == NULL)
  {
    return TARS_RES_ERR_PARAM;
  }

  if (osMutexWait(s_mutex, 100U) != osOK)
  {
    return TARS_RES_ERR_PARAM;
  }

  st = res_find_index(id, &idx);
  if (st == 0)
  {
    st = res_release_index(idx, owner);
  }

  osMutexRelease(s_mutex);
  return st;
}

tars_owner_t TarsResMgr_GetOwner(const char *id)
{
  uint32_t idx = 0U;
  tars_owner_t owner = TARS_OWNER_NONE;

  if (res_find_index(id, &idx) != 0)
  {
    return TARS_OWNER_NONE;
  }

  if (osMutexWait(s_mutex, 100U) == osOK)
  {
    owner = s_slots[idx].owner;
    osMutexRelease(s_mutex);
  }

  return owner;
}

tars_owner_t TarsResMgr_GetActive(const char *id)
{
  uint32_t idx = 0U;
  tars_owner_t active = TARS_OWNER_NONE;

  if (res_find_index(id, &idx) != 0)
  {
    return TARS_OWNER_NONE;
  }

  if (osMutexWait(s_mutex, 100U) == osOK)
  {
    active = s_slots[idx].active;
    osMutexRelease(s_mutex);
  }

  return active;
}

int TarsResMgr_AcquirePwm(const char *channel, tars_owner_t owner)
{
  const tars_mcu_pwm_entry_t *pwm = NULL;
  uint32_t pwm_idx = 0U;
  uint32_t pin_idx = 0U;
  int st;

  if (channel == NULL)
  {
    return TARS_RES_ERR_PARAM;
  }

  if (TarsMcuPinmap_ResolvePwm(channel, &pwm) != 0)
  {
    return TARS_RES_ERR_SCOPE;
  }

  if (TarsMcuPinmap_FindCatalog(channel, NULL, &pwm_idx) != 0)
  {
    return TARS_RES_ERR_SCOPE;
  }

  if (res_find_gpio_index_by_pin(pwm->pin_name, &pin_idx) != 0)
  {
    return TARS_RES_ERR_SCOPE;
  }

  if (osMutexWait(s_mutex, 100U) != osOK)
  {
    return TARS_RES_ERR_PARAM;
  }

  st = res_acquire_pair_sorted(pin_idx, pwm_idx, owner);

  osMutexRelease(s_mutex);
  return st;
}

int TarsResMgr_ReleasePwm(const char *channel, tars_owner_t owner)
{
  const tars_mcu_pwm_entry_t *pwm = NULL;
  uint32_t pwm_idx = 0U;
  uint32_t pin_idx = 0U;

  if (channel == NULL)
  {
    return TARS_RES_ERR_PARAM;
  }

  if (TarsMcuPinmap_ResolvePwm(channel, &pwm) != 0)
  {
    return TARS_RES_ERR_SCOPE;
  }

  if (TarsMcuPinmap_FindCatalog(channel, NULL, &pwm_idx) != 0)
  {
    return TARS_RES_ERR_SCOPE;
  }

  if (res_find_gpio_index_by_pin(pwm->pin_name, &pin_idx) != 0)
  {
    return TARS_RES_ERR_SCOPE;
  }

  if (osMutexWait(s_mutex, 100U) != osOK)
  {
    return TARS_RES_ERR_PARAM;
  }

  (void)res_release_pair_sorted(pin_idx, pwm_idx, owner);

  osMutexRelease(s_mutex);
  return 0;
}

int TarsResMgr_AcquireGpioPin(const char *pin_name, tars_owner_t owner)
{
  uint32_t idx = 0U;
  int st;

  if (pin_name == NULL)
  {
    return TARS_RES_ERR_PARAM;
  }

  if (osMutexWait(s_mutex, 100U) != osOK)
  {
    return TARS_RES_ERR_PARAM;
  }

  st = res_find_gpio_index_by_pin(pin_name, &idx);
  if (st == 0)
  {
    st = res_acquire_index(idx, owner);
  }

  osMutexRelease(s_mutex);
  return st;
}

int TarsResMgr_ReleaseGpioPin(const char *pin_name, tars_owner_t owner)
{
  uint32_t idx = 0U;
  int st;

  if (pin_name == NULL)
  {
    return TARS_RES_ERR_PARAM;
  }

  if (osMutexWait(s_mutex, 100U) != osOK)
  {
    return TARS_RES_ERR_PARAM;
  }

  st = res_find_gpio_index_by_pin(pin_name, &idx);
  if (st == 0)
  {
    st = res_release_index(idx, owner);
  }

  osMutexRelease(s_mutex);
  return st;
}

void TarsResMgr_FormatList(char *out, uint32_t out_size)
{
  uint32_t count = 0U;
  const tars_res_catalog_entry_t *cat = TarsMcuPinmap_GetResCatalog(&count);
  uint32_t i;

  if ((out == NULL) || (out_size == 0U))
  {
    return;
  }

  out[0] = '\0';

  for (i = 0U; (i < count) && (i < s_slot_count); i++)
  {
    char line[96];
    const char *kind = (cat[i].kind == TARS_RES_KIND_PWM) ? "pwm" : "gpio";

    (void)snprintf(line,
                   sizeof(line),
                   "  %-12s %-4s owner=%-5s active=%-5s\r\n",
                   cat[i].id,
                   kind,
                   TarsOwner_ToString(s_slots[i].owner),
                   TarsOwner_ToString(s_slots[i].active));
    strncat(out, line, out_size - strlen(out) - 1U);
  }
}

void TarsResMgr_FormatStatus(const char *id, char *out, uint32_t out_size)
{
  uint32_t idx = 0U;
  const tars_res_catalog_entry_t *cat = NULL;

  if ((out == NULL) || (out_size == 0U) || (id == NULL))
  {
    return;
  }

  if (TarsMcuPinmap_FindCatalog(id, &cat, &idx) != 0)
  {
    (void)snprintf(out, out_size, "res: unknown id %s\r\n", id);
    return;
  }

  if (idx >= s_slot_count)
  {
    (void)snprintf(out, out_size, "res: out of range %s\r\n", id);
    return;
  }

  (void)snprintf(out,
                 out_size,
                 "res: id=%s kind=%s owner=%s active=%s lock=%u\r\n",
                 cat->id,
                 (cat->kind == TARS_RES_KIND_PWM) ? "pwm" : "gpio",
                 TarsOwner_ToString(s_slots[idx].owner),
                 TarsOwner_ToString(s_slots[idx].active),
                 (unsigned)cat->lock_order);
}
