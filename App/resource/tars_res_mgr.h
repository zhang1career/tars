#ifndef TARS_RES_MGR_H
#define TARS_RES_MGR_H

#include <stdint.h>
#include "tars_mcu_pinmap.h"
#include "tars_tenant.h"

/* Resource manager: compile-time catalog (pinmap) + runtime tenant leases.
 * Tenant names are user-assigned via `mcu res grant`; no boot defaults.
 * Persist grants/PWM state with `mcu res save`. */

#define TARS_RES_ERR_SCOPE   (-1)
#define TARS_RES_ERR_ACTIVE  (-2)
#define TARS_RES_ERR_OWNER   (-3)
#define TARS_RES_ERR_PARAM   (-4)
#define TARS_RES_ERR_SYSTEM  (-5)

void TarsResMgr_Init(void);

int TarsResMgr_Grant(const char *id, const char *tenant);
int TarsResMgr_GetTenant(const char *id, char *out, uint32_t out_size);
int TarsResMgr_GetActiveTenant(const char *id, char *out, uint32_t out_size);
int TarsResMgr_TenantAssigned(const char *id);

int TarsResMgr_Acquire(const char *id);
int TarsResMgr_Release(const char *id);

void TarsResMgr_FormatList(char *out, uint32_t out_size);
void TarsResMgr_FormatStatus(const char *id, char *out, uint32_t out_size);

int TarsResMgr_AcquirePwm(const char *channel);
int TarsResMgr_ReleasePwm(const char *channel);

int TarsResMgr_AcquireDac(const char *channel);
int TarsResMgr_ReleaseDac(const char *channel);

int TarsResMgr_AcquireGpioPin(const char *pin_name);
int TarsResMgr_ReleaseGpioPin(const char *pin_name);

int TarsResMgr_TimDomainAcquire(const char *tim_id, const char *tenant);
void TarsResMgr_TimDomainRelease(const char *tim_id, const char *tenant);
void TarsResMgr_TimDomainForceSet(const char *tim_id, const char *tenant);
int TarsResMgr_TimDomainHeldBy(const char *tim_id, const char *tenant);
int TarsResMgr_GetTimDomainActiveTenant(const char *tim_id, char *out, uint32_t out_size);
int TarsResMgr_Tim1HeldByFoc(void);

#endif /* TARS_RES_MGR_H */
