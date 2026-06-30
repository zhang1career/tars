#include "tars_mcu.h"
#include "tars_resource.h"
#include "tars_res_pwm.h"

const char *TarsMcu_ResErrText(int code)
{
  switch (code)
  {
  case 0:
    return "ok";
  case TARS_RES_ERR_SCOPE:
    return "scope";
  case TARS_RES_ERR_ACTIVE:
    return "active";
  case TARS_RES_ERR_OWNER:
    return "owner";
  case TARS_RES_ERR_PARAM:
    return "param";
  case TARS_RES_ERR_SYSTEM:
    return "system";
  default:
    if (code == -2)
    {
      return "input-only";
    }
    return "error";
  }
}

int TarsMcu_PwmEnable(const char *channel, int enable)
{
  return TarsResource_PwmEnable(channel, enable);
}

int TarsMcu_PwmSetDuty(const char *channel, float duty_pct)
{
  return TarsResource_PwmSetDuty(channel, duty_pct);
}

int TarsMcu_PwmSetFreq(const char *tim_id, uint32_t freq_hz)
{
  return TarsResource_PwmSetFreq(tim_id, freq_hz);
}

int TarsMcu_ResGrant(const char *id, tars_owner_t owner)
{
  return TarsResource_ResGrant(id, owner);
}

int TarsMcu_PwmFormatStatus(const char *channel, char *out, uint32_t out_size)
{
  return TarsResPwm_GetStatus(channel, out, out_size);
}
