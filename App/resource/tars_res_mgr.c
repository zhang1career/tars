#include "tars_res_mgr.h"
#include "cmsis_os.h"
#include <stdio.h>
#include <string.h>

#define TARS_RES_MAX          48U
#define TARS_RES_TIM_DOMAINS  4U

typedef struct {
  char tenant[TARS_TENANT_LEN];
  char active[TARS_TENANT_LEN];
} tars_res_slot_t;

typedef struct {
  const char *tim_id;
  uint8_t     whole_timer;
  volatile char active_tenant[TARS_TENANT_LEN];
} tars_tim_domain_t;

static tars_res_slot_t s_slots[TARS_RES_MAX];
static uint32_t s_slot_count;
static osMutexId s_mutex;

static tars_tim_domain_t s_tim_domains[TARS_RES_TIM_DOMAINS];
static uint32_t s_tim_domain_count;
static int s_tim1_domain_idx = -1;

static int tenant_same(const char *a, const char *b)
{
  char ta[TARS_TENANT_LEN];
  char tb[TARS_TENANT_LEN];

  TarsTenant_Copy(ta, sizeof(ta), a);
  TarsTenant_Copy(tb, sizeof(tb), b);
  return (strcmp(ta, tb) == 0) ? 1 : 0;
}

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
      s_tim_domains[idx].active_tenant[0] = '\0';
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
}

static int res_find_index(const char *id, uint32_t *index_out)
{
  if (TarsMcuPinmap_FindCatalog(id, NULL, index_out) != 0)
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

    if ((pin_name != NULL) && (strcmp(gpio[i].pin_name, pin_name) == 0))
    {
      *index_out = idx;
      return 0;
    }
  }

  return TARS_RES_ERR_SCOPE;
}

static int res_grant_index(uint32_t idx, const char *tenant)
{
  if (idx >= s_slot_count)
  {
    return TARS_RES_ERR_SCOPE;
  }

  if (!TarsTenant_IsNone(s_slots[idx].active))
  {
    return TARS_RES_ERR_ACTIVE;
  }

  TarsTenant_Copy(s_slots[idx].tenant, sizeof(s_slots[idx].tenant), tenant);
  return 0;
}

static int res_acquire_index(uint32_t idx)
{
  if (idx >= s_slot_count)
  {
    return TARS_RES_ERR_SCOPE;
  }

  if (TarsTenant_IsNone(s_slots[idx].tenant))
  {
    return TARS_RES_ERR_OWNER;
  }

  if ((!TarsTenant_IsNone(s_slots[idx].active)) &&
      (!tenant_same(s_slots[idx].active, s_slots[idx].tenant)))
  {
    return TARS_RES_ERR_ACTIVE;
  }

  TarsTenant_Copy(s_slots[idx].active, sizeof(s_slots[idx].active), s_slots[idx].tenant);
  return 0;
}

static int res_release_index(uint32_t idx)
{
  if (idx >= s_slot_count)
  {
    return TARS_RES_ERR_SCOPE;
  }

  if (TarsTenant_IsNone(s_slots[idx].active))
  {
    return TARS_RES_ERR_OWNER;
  }

  s_slots[idx].active[0] = '\0';
  return 0;
}

static int res_acquire_pair_sorted(uint32_t a, uint32_t b)
{
  uint32_t first = a;
  uint32_t second = b;
  int st;

  if (first > second)
  {
    first = b;
    second = a;
  }

  st = res_acquire_index(first);
  if (st != 0)
  {
    return st;
  }

  st = res_acquire_index(second);
  if (st != 0)
  {
    (void)res_release_index(first);
    return st;
  }

  return 0;
}

static int res_release_pair_sorted(uint32_t a, uint32_t b)
{
  uint32_t first = a;
  uint32_t second = b;

  if (first > second)
  {
    first = b;
    second = a;
  }

  (void)res_release_index(second);
  (void)res_release_index(first);
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
    s_slots[i].tenant[0] = '\0';
    s_slots[i].active[0] = '\0';
  }

  tim_domains_build();

  if (s_mutex == NULL)
  {
    osMutexDef(res_mgr_mutex);
    s_mutex = osMutexCreate(osMutex(res_mgr_mutex));
  }
}

int TarsResMgr_Grant(const char *id, const char *tenant)
{
  uint32_t idx = 0U;
  const tars_res_catalog_entry_t *entry = NULL;
  int st;

  if ((id == NULL) || (tenant == NULL))
  {
    return TARS_RES_ERR_PARAM;
  }

  if (TarsTenant_ValidateGrantName(tenant) != 0)
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
    (void)TarsMcuPinmap_FindCatalog(id, &entry, &idx);
    st = res_grant_index(idx, tenant);
  }

  if ((st == 0) && (entry != NULL) && (entry->kind == TARS_RES_KIND_PWM))
  {
    const tars_mcu_pwm_entry_t *pwm = NULL;

    if (TarsMcuPinmap_ResolvePwm(id, &pwm) == 0)
    {
      uint32_t pin_idx = 0U;
      const tars_res_catalog_entry_t *pin_cat = NULL;

      if (res_find_gpio_index_by_pin(pwm->pin_name, &pin_idx) != 0)
      {
        if (TarsMcuPinmap_FindCatalog(pwm->pin_name, &pin_cat, &pin_idx) != 0)
        {
          st = TARS_RES_ERR_SCOPE;
        }
      }
      else
      {
        (void)TarsMcuPinmap_FindCatalog(pwm->pin_name, &pin_cat, &pin_idx);
      }

      if ((st == 0) && (pin_cat != NULL))
      {
        if (!TarsTenant_IsNone(s_slots[pin_idx].active))
        {
          st = TARS_RES_ERR_ACTIVE;
        }
        else
        {
          TarsTenant_Copy(s_slots[pin_idx].tenant,
                          sizeof(s_slots[pin_idx].tenant),
                          tenant);
        }
      }
    }
  }

  if ((st == 0) && (entry != NULL) && (entry->kind == TARS_RES_KIND_DAC))
  {
    const tars_mcu_dac_entry_t *dac = NULL;

    if (TarsMcuPinmap_ResolveDac(id, &dac) == 0)
    {
      uint32_t pin_idx = 0U;
      const tars_res_catalog_entry_t *pin_cat = NULL;

      if (res_find_gpio_index_by_pin(dac->pin_name, &pin_idx) != 0)
      {
        if (TarsMcuPinmap_FindCatalog(dac->pin_name, &pin_cat, &pin_idx) != 0)
        {
          st = TARS_RES_ERR_SCOPE;
        }
      }
      else
      {
        (void)TarsMcuPinmap_FindCatalog(dac->pin_name, &pin_cat, &pin_idx);
      }

      if ((st == 0) && (pin_cat != NULL))
      {
        if (!TarsTenant_IsNone(s_slots[pin_idx].active))
        {
          st = TARS_RES_ERR_ACTIVE;
        }
        else
        {
          TarsTenant_Copy(s_slots[pin_idx].tenant,
                          sizeof(s_slots[pin_idx].tenant),
                          tenant);
        }
      }
    }
  }

  osMutexRelease(s_mutex);
  return st;
}

int TarsResMgr_GetTenant(const char *id, char *out, uint32_t out_size)
{
  uint32_t idx = 0U;

  if ((out == NULL) || (out_size == 0U))
  {
    return TARS_RES_ERR_PARAM;
  }

  out[0] = '\0';

  if (res_find_index(id, &idx) != 0)
  {
    return TARS_RES_ERR_SCOPE;
  }

  if (osMutexWait(s_mutex, 100U) == osOK)
  {
    TarsTenant_Copy(out, out_size, s_slots[idx].tenant);
    osMutexRelease(s_mutex);
    return 0;
  }

  return TARS_RES_ERR_PARAM;
}

int TarsResMgr_GetActiveTenant(const char *id, char *out, uint32_t out_size)
{
  uint32_t idx = 0U;

  if ((out == NULL) || (out_size == 0U))
  {
    return TARS_RES_ERR_PARAM;
  }

  out[0] = '\0';

  if (res_find_index(id, &idx) != 0)
  {
    return TARS_RES_ERR_SCOPE;
  }

  if (osMutexWait(s_mutex, 100U) == osOK)
  {
    TarsTenant_Copy(out, out_size, s_slots[idx].active);
    osMutexRelease(s_mutex);
    return 0;
  }

  return TARS_RES_ERR_PARAM;
}

int TarsResMgr_TenantAssigned(const char *id)
{
  char tenant[TARS_TENANT_LEN];

  if (TarsResMgr_GetTenant(id, tenant, sizeof(tenant)) != 0)
  {
    return 0;
  }

  return TarsTenant_IsNone(tenant) ? 0 : 1;
}

int TarsResMgr_Acquire(const char *id)
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
    st = res_acquire_index(idx);
  }

  osMutexRelease(s_mutex);
  return st;
}

int TarsResMgr_Release(const char *id)
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
    st = res_release_index(idx);
  }

  osMutexRelease(s_mutex);
  return st;
}

int TarsResMgr_AcquirePwm(const char *channel)
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

  if ((!TarsTenant_IsNone(s_slots[pwm_idx].tenant)) &&
      (TarsTenant_IsNone(s_slots[pin_idx].active)))
  {
    TarsTenant_Copy(s_slots[pin_idx].tenant,
                    sizeof(s_slots[pin_idx].tenant),
                    s_slots[pwm_idx].tenant);
  }

  st = res_acquire_pair_sorted(pin_idx, pwm_idx);

  osMutexRelease(s_mutex);
  return st;
}

int TarsResMgr_ReleasePwm(const char *channel)
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

  (void)res_release_pair_sorted(pin_idx, pwm_idx);

  osMutexRelease(s_mutex);
  return 0;
}

int TarsResMgr_AcquireDac(const char *channel)
{
  const tars_mcu_dac_entry_t *dac = NULL;
  uint32_t dac_idx = 0U;
  uint32_t pin_idx = 0U;
  int st;

  if (channel == NULL)
  {
    return TARS_RES_ERR_PARAM;
  }

  if (TarsMcuPinmap_ResolveDac(channel, &dac) != 0)
  {
    return TARS_RES_ERR_SCOPE;
  }

  if (TarsMcuPinmap_FindCatalog(channel, NULL, &dac_idx) != 0)
  {
    return TARS_RES_ERR_SCOPE;
  }

  if (TarsMcuPinmap_FindCatalog(dac->pin_name, NULL, &pin_idx) != 0)
  {
    return TARS_RES_ERR_SCOPE;
  }

  if (osMutexWait(s_mutex, 100U) != osOK)
  {
    return TARS_RES_ERR_PARAM;
  }

  if ((!TarsTenant_IsNone(s_slots[dac_idx].tenant)) &&
      (TarsTenant_IsNone(s_slots[pin_idx].active)))
  {
    TarsTenant_Copy(s_slots[pin_idx].tenant,
                    sizeof(s_slots[pin_idx].tenant),
                    s_slots[dac_idx].tenant);
  }

  st = res_acquire_pair_sorted(pin_idx, dac_idx);

  osMutexRelease(s_mutex);
  return st;
}

int TarsResMgr_ReleaseDac(const char *channel)
{
  const tars_mcu_dac_entry_t *dac = NULL;
  uint32_t dac_idx = 0U;
  uint32_t pin_idx = 0U;

  if (channel == NULL)
  {
    return TARS_RES_ERR_PARAM;
  }

  if (TarsMcuPinmap_ResolveDac(channel, &dac) != 0)
  {
    return TARS_RES_ERR_SCOPE;
  }

  if (TarsMcuPinmap_FindCatalog(channel, NULL, &dac_idx) != 0)
  {
    return TARS_RES_ERR_SCOPE;
  }

  if (TarsMcuPinmap_FindCatalog(dac->pin_name, NULL, &pin_idx) != 0)
  {
    return TARS_RES_ERR_SCOPE;
  }

  if (osMutexWait(s_mutex, 100U) != osOK)
  {
    return TARS_RES_ERR_PARAM;
  }

  (void)res_release_pair_sorted(pin_idx, dac_idx);

  osMutexRelease(s_mutex);
  return 0;
}

int TarsResMgr_AcquireGpioPin(const char *pin_name)
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
    st = res_acquire_index(idx);
  }

  osMutexRelease(s_mutex);
  return st;
}

int TarsResMgr_ReleaseGpioPin(const char *pin_name)
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
    st = res_release_index(idx);
  }

  osMutexRelease(s_mutex);
  return st;
}

static const char *res_kind_text(tars_res_kind_t kind)
{
  if (kind == TARS_RES_KIND_PWM)
  {
    return "pwm";
  }
  if (kind == TARS_RES_KIND_DAC)
  {
    return "dac";
  }
  return "gpio";
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
    char line[112];
    const char *kind = res_kind_text(cat[i].kind);

    (void)snprintf(line,
                   sizeof(line),
                   "  %-12s %-4s tenant=%-14s active=%-14s\r\n",
                   cat[i].id,
                   kind,
                   TarsTenant_Display(s_slots[i].tenant),
                   TarsTenant_Display(s_slots[i].active));
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
                 "res: id=%s kind=%s tenant=%s active=%s lock=%u\r\n",
                 cat->id,
                 res_kind_text(cat->kind),
                 TarsTenant_Display(s_slots[idx].tenant),
                 TarsTenant_Display(s_slots[idx].active),
                 (unsigned)cat->lock_order);
}

int TarsResMgr_TimDomainAcquire(const char *tim_id, const char *tenant)
{
  int idx;
  int st = 0;

  if ((tim_id == NULL) || (tenant == NULL))
  {
    return TARS_RES_ERR_PARAM;
  }

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
    st = 0;
  }
  else if ((!TarsTenant_IsNone(s_tim_domains[(uint32_t)idx].active_tenant)) &&
           (!tenant_same(s_tim_domains[(uint32_t)idx].active_tenant, tenant)))
  {
    st = TARS_RES_ERR_ACTIVE;
  }
  else
  {
    TarsTenant_Copy((char *)s_tim_domains[(uint32_t)idx].active_tenant,
                    sizeof(s_tim_domains[0].active_tenant),
                    tenant);
  }

  osMutexRelease(s_mutex);
  return st;
}

void TarsResMgr_TimDomainRelease(const char *tim_id, const char *tenant)
{
  int idx;

  if ((tim_id == NULL) || (tenant == NULL))
  {
    return;
  }

  if (osMutexWait(s_mutex, 100U) != osOK)
  {
    return;
  }

  idx = tim_domain_find(tim_id);
  if ((idx >= 0) && tenant_same(s_tim_domains[(uint32_t)idx].active_tenant, tenant))
  {
    s_tim_domains[(uint32_t)idx].active_tenant[0] = '\0';
  }

  osMutexRelease(s_mutex);
}

void TarsResMgr_TimDomainForceSet(const char *tim_id, const char *tenant)
{
  int idx;

  if ((tim_id == NULL) || (tenant == NULL))
  {
    return;
  }

  if (osMutexWait(s_mutex, 100U) != osOK)
  {
    return;
  }

  idx = tim_domain_find(tim_id);
  if (idx >= 0)
  {
    TarsTenant_Copy((char *)s_tim_domains[(uint32_t)idx].active_tenant,
                    sizeof(s_tim_domains[0].active_tenant),
                    tenant);
  }

  osMutexRelease(s_mutex);
}

int TarsResMgr_TimDomainHeldBy(const char *tim_id, const char *tenant)
{
  int idx;
  int held = 0;

  if ((tim_id == NULL) || (tenant == NULL))
  {
    return 0;
  }

  if (osMutexWait(s_mutex, 100U) != osOK)
  {
    return 0;
  }

  idx = tim_domain_find(tim_id);
  if (idx >= 0)
  {
    held = tenant_same(s_tim_domains[(uint32_t)idx].active_tenant, tenant);
  }

  osMutexRelease(s_mutex);
  return held;
}

int TarsResMgr_Tim1HeldByFoc(void)
{
  if (s_tim1_domain_idx < 0)
  {
    return 0;
  }

  return tenant_same(s_tim_domains[(uint32_t)s_tim1_domain_idx].active_tenant,
                     TARS_TENANT_FOC);
}

int TarsResMgr_GetTimDomainActiveTenant(const char *tim_id, char *out, uint32_t out_size)
{
  int idx;

  if ((tim_id == NULL) || (out == NULL) || (out_size == 0U))
  {
    return TARS_RES_ERR_PARAM;
  }

  out[0] = '\0';

  if (osMutexWait(s_mutex, 100U) != osOK)
  {
    return TARS_RES_ERR_PARAM;
  }

  idx = tim_domain_find(tim_id);
  if (idx >= 0)
  {
    TarsTenant_Copy(out, out_size, s_tim_domains[(uint32_t)idx].active_tenant);
    osMutexRelease(s_mutex);
    return 0;
  }

  osMutexRelease(s_mutex);
  return TARS_RES_ERR_SCOPE;
}
