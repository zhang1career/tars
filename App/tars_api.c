#include "tars_app.h"
#include "tars_api.h"
#include "tars_mcu.h"
#include "tars_vfs.h"
#include "cmsis_os.h"

static tars_api_t s_api;

static void api_gpio_write(const char *pin_name, int value)
{
  (void)TarsMcu_GpioWrite(pin_name, value);
}

static int api_gpio_read(const char *pin_name)
{
  int value = 0;

  if (TarsMcu_GpioRead(pin_name, &value) != 0)
  {
    return 0;
  }

  return value;
}

static int api_pwm_enable(const char *channel, int enable)
{
  return TarsMcu_PwmEnable(channel, enable);
}

static int api_pwm_duty(const char *channel, float duty_pct)
{
  return TarsMcu_PwmSetDuty(channel, duty_pct);
}

static int api_res_save(void)
{
  return TarsMcu_ProfileSave();
}

static int api_res_load(void)
{
  return TarsMcu_ProfileLoad();
}

static int api_res_clear(void)
{
  return TarsMcu_ProfileClear();
}

static int api_pwm_persist(const char *channel, int boot_enable)
{
  return TarsMcu_PwmSetPersist(channel, boot_enable);
}

static void api_sleep_ms(uint32_t ms)
{
  osDelay(ms);
}

static void api_log(const char *msg)
{
  TarsVfs_WriteLog(msg);
}

void TarsApi_Init(void)
{
  TarsVfs_Init();
  s_api.api_version = TARS_API_VERSION;
  s_api.gpio_write = api_gpio_write;
  s_api.gpio_read = api_gpio_read;
  s_api.pwm_enable = api_pwm_enable;
  s_api.pwm_duty = api_pwm_duty;
  s_api.sleep_ms = api_sleep_ms;
  s_api.log = api_log;
  s_api.res_save = api_res_save;
  s_api.res_load = api_res_load;
  s_api.res_clear = api_res_clear;
  s_api.pwm_persist = api_pwm_persist;
}

const tars_api_t *TarsApp_GetApi(void)
{
  return &s_api;
}
