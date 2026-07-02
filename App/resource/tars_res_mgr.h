#ifndef TARS_RES_MGR_H
#define TARS_RES_MGR_H

#include <stdint.h>
#include "tars_mcu_pinmap.h"

/* Resource manager: compile-time catalog (pinmap) + runtime ownership/active
 * leases. Ownership is RAM-only (reset to pinmap defaults on boot / power
 * cycle). Shell `mcu res grant` changes ownership when idle. */

#define TARS_RES_ERR_SCOPE   (-1)  /* id not in pinmap catalog */
#define TARS_RES_ERR_ACTIVE  (-2)  /* resource in use */
#define TARS_RES_ERR_OWNER   (-3)  /* wrong owner / cannot steal */
#define TARS_RES_ERR_PARAM   (-4)
#define TARS_RES_ERR_SYSTEM  (-5)  /* system-owned, user cannot grant */

void TarsResMgr_Init(void);

int TarsResMgr_Grant(const char *id, tars_owner_t new_owner);
int TarsResMgr_Acquire(const char *id, tars_owner_t owner);
int TarsResMgr_Release(const char *id, tars_owner_t owner);

tars_owner_t TarsResMgr_GetOwner(const char *id);
tars_owner_t TarsResMgr_GetActive(const char *id);

void TarsResMgr_FormatList(char *out, uint32_t out_size);
void TarsResMgr_FormatStatus(const char *id, char *out, uint32_t out_size);

/* PWM channel acquire also claims the backing GPIO pin (sorted lock order). */
int TarsResMgr_AcquirePwm(const char *channel, tars_owner_t owner);
int TarsResMgr_ReleasePwm(const char *channel, tars_owner_t owner);

/* GPIO output acquires the pin resource before driving. */
int TarsResMgr_AcquireGpioPin(const char *pin_name, tars_owner_t owner);
int TarsResMgr_ReleaseGpioPin(const char *pin_name, tars_owner_t owner);

/* ---- Timer physical-resource domains ---------------------------------- *
 * A timer is a physical resource that peer business functions (foc, pwm...)
 * contend for. Advanced timers (TIM1) are WHOLE-TIMER exclusive: ARR / count
 * mode / MOE / the 6 complementary outputs are all coupled, so a single owner
 * drives the whole timer. General timers (TIM9) share frequency across
 * channels and are not arbitrated at this layer yet. Domains are derived from
 * the pin map at init (keyed by tim_id). */

/* WHOLE-TIMER: succeed if free or already ours; ERR_ACTIVE if a different
 * owner drives it. General timers always succeed (no whole-timer lock). */
int TarsResMgr_TimDomainAcquire(const char *tim_id, tars_owner_t owner);

/* Release the domain back to free if currently held by `owner`. */
void TarsResMgr_TimDomainRelease(const char *tim_id, tars_owner_t owner);

/* Unconditionally set the active driver (used for explicit handoff between
 * peer functions, e.g. pwm stealing an idle FOC timer or handing it back). */
void TarsResMgr_TimDomainForceSet(const char *tim_id, tars_owner_t owner);

/* Mutex-protected read of the current active driver (shell / task context). */
tars_owner_t TarsResMgr_TimDomainActiveOwner(const char *tim_id);

/* Lock-free read of the TIM1 whole-timer domain's active driver. Safe to call
 * from the 20 kHz FOC ISR (reads a volatile, never takes the mutex). */
tars_owner_t TarsResMgr_Tim1ActiveOwnerFast(void);

#endif /* TARS_RES_MGR_H */
