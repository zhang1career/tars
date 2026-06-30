#include "tars_mcu.h"
#include "tars_resource.h"
#include "tars_res_pwm.h"
#include "tars_res_profile.h"

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

int TarsMcu_PwmSetPersist(const char *channel, int boot_enable)
{
  return TarsResource_PwmSetPersist(channel, boot_enable);
}

int TarsMcu_PwmGetPersist(const char *channel, int *boot_enable_out)
{
  return TarsResPwm_GetPersist(channel, boot_enable_out);
}

int TarsMcu_ProfileSave(void)
{
  return TarsResource_ProfileSave();
}

int TarsMcu_ProfileLoad(void)
{
  return TarsResource_ProfileLoad();
}

int TarsMcu_ProfileClear(void)
{
  return TarsResource_ProfileClear();
}

int TarsMcu_ProfileFormatStored(char *out, uint32_t out_size)
{
  return TarsResource_ProfileFormatStored(out, out_size);
}

const char *TarsMcu_ProfileErrText(int code)
{
  switch (code)
  {
  case 0:
    return "ok";
  case TARS_RES_PROFILE_ERR_PARAM:
    return "param";
  case TARS_RES_PROFILE_ERR_CRC:
    return "crc";
  case TARS_RES_PROFILE_ERR_IO:
    return "io";
  case TARS_RES_PROFILE_ERR_STALE:
    return "stale";
  case TARS_RES_PROFILE_ERR_NONE:
    return "none";
  default:
    return TarsMcu_ResErrText(code);
  }
}

int TarsMcu_PwmFormatStatus(const char *channel, char *out, uint32_t out_size)
{
  return TarsResPwm_GetStatus(channel, out, out_size);
}
