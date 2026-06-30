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

#endif /* TARS_RES_MGR_H */
