#include "tars_res_mgr.h"
#include "cmsis_os.h"
#include <stdio.h>
#include <string.h>

#define TARS_RES_MAX          48U
#define TARS_RES_TIM_DOMAINS  4U

typedef struct {
  tars_owner_t owner;
  tars_owner_t active;
} tars_res_slot_t;

/* Physical arbitration domain for one timer. `active_owner` is the peer
 * function currently driving the timer; it is volatile so the FOC ISR can
 * read it lock-free (see TarsResMgr_Tim1ActiveOwnerFast). */
typedef struct {
  const char           *tim_id;
  uint8_t               whole_timer;   /* 1 = advanced timer, exclusive */
  volatile tars_owner_t active_owner;
} tars_tim_domain_t;

static tars_res_slot_t s_slots[TARS_RES_MAX];
static uint32_t s_slot_count;
static osMutexId s_mutex;

static tars_tim_domain_t s_tim_domains[TARS_RES_TIM_DOMAINS];
static uint32_t s_tim_domain_count;
static int s_tim1_domain_idx = -1;

static int tim_domain_find(const char *tim_id)
{
  uint32_t i;

  if (tim_id == NULL)
  {
    return -1;
  }

  for (i = 0U; i < s_tim_domain_count; i++)
  {
    if (strcmp(s_tim_domains[i].tim_id, tim_id) == 0)
    {
      return (int)i;
    }
  }

  return -1;
}

static void tim_domains_build(void)
{
  uint32_t pwm_count = 0U;
  const tars_mcu_pwm_entry_t *pwm = TarsMcuPinmap_GetPwmTable(&pwm_count);
  uint32_t i;

  s_tim_domain_count = 0U;
  s_tim1_domain_idx = -1;

  for (i = 0U; i < pwm_count; i++)
  {
    int idx;

    if (pwm[i].tim_id == NULL)
    {
      continue;
    }

    idx = tim_domain_find(pwm[i].tim_id);
    if (idx < 0)
    {
      if (s_tim_domain_count >= TARS_RES_TIM_DOMAINS)
      {
        continue;
      }
      idx = (int)s_tim_domain_count++;
      s_tim_domains[idx].tim_id = pwm[i].tim_id;
      s_tim_domains[idx].whole_timer = 0U;
      s_tim_domains[idx].active_owner = TARS_OWNER_NONE;
    }

    if (pwm[i].advanced_tim != 0U)
    {
      s_tim_domains[idx].whole_timer = 1U;
    }

    if (pwm[i].tim == TIM1)
    {
      s_tim1_domain_idx = idx;
    }
  }

  /* TIM1 is FOC-owned by default (matches the pin map default_owner=foc on
   * tim1_ch1..3); FOC's boot ISR starts driving it before any grant. */
  if (s_tim1_domain_idx >= 0)
  {
    s_tim_domains[(uint32_t)s_tim1_domain_idx].active_owner = TARS_OWNER_FOC;
  }
}

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

  tim_domains_build();

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

      if (res_find_gpio_index_by_pin(pwm->pin_name, &pin_idx) != 0)
      {
        if (TarsMcuPinmap_FindCatalog(pwm->pin_name, NULL, &pin_idx) != 0)
        {
          st = TARS_RES_ERR_SCOPE;
        }
      }

      if (st == 0)
      {
        if (s_slots[pin_idx].owner == TARS_OWNER_SYSTEM)
        {
          st = TARS_RES_ERR_SYSTEM;
        }
        else if (s_slots[pin_idx].active != TARS_OWNER_NONE)
        {
          st = TARS_RES_ERR_ACTIVE;
        }
        else
        {
          s_slots[pin_idx].owner = new_owner;
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

  if (TarsMcuPinmap_FindCatalog(pwm->pin_name, NULL, &pin_idx) != 0)
  {
    return TARS_RES_ERR_SCOPE;
  }

  if (osMutexWait(s_mutex, 100U) != osOK)
  {
    return TARS_RES_ERR_PARAM;
  }

  /* Overlap: channel already granted to pwm may claim the backing pin when idle. */
  if ((s_slots[pwm_idx].owner == owner) &&
      (s_slots[pin_idx].active == TARS_OWNER_NONE) &&
      (s_slots[pin_idx].owner != TARS_OWNER_SYSTEM))
  {
    s_slots[pin_idx].owner = owner;
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

  if (TarsMcuPinmap_FindCatalog(pwm->pin_name, NULL, &pin_idx) != 0)
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

int TarsResMgr_TimDomainAcquire(const char *tim_id, tars_owner_t owner)
{
  int idx;
  int st = 0;

  if (osMutexWait(s_mutex, 100U) != osOK)
  {
    return TARS_RES_ERR_PARAM;
  }

  idx = tim_domain_find(tim_id);
  if (idx < 0)
  {
    st = TARS_RES_ERR_SCOPE;
  }
  else if (s_tim_domains[(uint32_t)idx].whole_timer == 0U)
  {
    /* General timer: channels share frequency but are not whole-timer
     * exclusive; no arbitration at this layer (yet). */
    st = 0;
  }
  else if ((s_tim_domains[(uint32_t)idx].active_owner != TARS_OWNER_NONE) &&
           (s_tim_domains[(uint32_t)idx].active_owner != owner))
  {
    st = TARS_RES_ERR_ACTIVE;
  }
  else
  {
    s_tim_domains[(uint32_t)idx].active_owner = owner;
  }

  osMutexRelease(s_mutex);
  return st;
}

void TarsResMgr_TimDomainRelease(const char *tim_id, tars_owner_t owner)
{
  int idx;

  if (osMutexWait(s_mutex, 100U) != osOK)
  {
    return;
  }

  idx = tim_domain_find(tim_id);
  if ((idx >= 0) && (s_tim_domains[(uint32_t)idx].active_owner == owner))
  {
    s_tim_domains[(uint32_t)idx].active_owner = TARS_OWNER_NONE;
  }

  osMutexRelease(s_mutex);
}

void TarsResMgr_TimDomainForceSet(const char *tim_id, tars_owner_t owner)
{
  int idx;

  if (osMutexWait(s_mutex, 100U) != osOK)
  {
    return;
  }

  idx = tim_domain_find(tim_id);
  if (idx >= 0)
  {
    s_tim_domains[(uint32_t)idx].active_owner = owner;
  }

  osMutexRelease(s_mutex);
}

tars_owner_t TarsResMgr_TimDomainActiveOwner(const char *tim_id)
{
  int idx;
  tars_owner_t owner = TARS_OWNER_NONE;

  if (osMutexWait(s_mutex, 100U) != osOK)
  {
    return TARS_OWNER_NONE;
  }

  idx = tim_domain_find(tim_id);
  if (idx >= 0)
  {
    owner = s_tim_domains[(uint32_t)idx].active_owner;
  }

  osMutexRelease(s_mutex);
  return owner;
}

tars_owner_t TarsResMgr_Tim1ActiveOwnerFast(void)
{
  if (s_tim1_domain_idx < 0)
  {
    /* No TIM1 domain on this board: default to FOC so its ISR is unaffected. */
    return TARS_OWNER_FOC;
  }

  return s_tim_domains[(uint32_t)s_tim1_domain_idx].active_owner;
}
