#include "tars_resource.h"
#include "tars_res_gpio.h"
#include "tars_res_pwm.h"
#include "tars_res_mgr.h"
#include "tars_res_profile.h"
#include "tars_foc.h"
#include "tars_lfs.h"
#include "tars_storage.h"
#include "tars_motor.h"
#include "lcd_viewport.h"
#include "cmsis_os.h"

/* Motor control loop runs at 100 ms (TarsMotor uses dt = 0.1 s). */
#define TARS_RES_MOTOR_PERIOD_MS    100U

static uint8_t s_initialized;

static void resource_on_b1(int button_id, int pressed)
{
  (void)button_id;

  /* Act on the press edge only. LCD access is mutex-protected internally. */
  if (pressed != 0)
  {
    LcdViewport_NextPage();
  }
}

static void TarsResource_WaitForLfs(void)
{
  uint32_t waited_ms = 0U;

  TarsStorage_Init();

  while ((TarsLfs_IsMounted() == 0) && (waited_ms < 3000U))
  {
    osDelay(10);
    waited_ms += 10U;
  }
}

static void TarsResource_ApplyBootPolicy(void)
{
  int skip_foc_hw = 0;

  TarsResource_WaitForLfs();

  if (TarsLfs_IsMounted() != 0)
  {
    (void)TarsResProfile_Load();
  }

  TarsFoc_Init();

  if ((TarsResProfile_HasStaged() != 0) && (TarsResProfile_Pwm0BootOnTim1() != 0))
  {
    skip_foc_hw = 1;
  }

  if (skip_foc_hw == 0)
  {
    TarsFoc_BootHw();
  }

  (void)TarsResProfile_Apply();
}

void TarsResource_Init(void)
{
  if (s_initialized != 0U)
  {
    return;
  }

  TarsResMgr_Init();

  (void)TarsResGpio_ButtonRegister("b1", 1U, 20U, resource_on_b1);

  TarsResource_ApplyBootPolicy();

  s_initialized = 1U;
}

int TarsResource_GpioWrite(const char *pin_name, int value)
{
  return TarsResGpio_Write(pin_name, value);
}

int TarsResource_GpioRead(const char *pin_name, int *value_out)
{
  return TarsResGpio_Read(pin_name, value_out);
}

int TarsResource_PwmEnable(const char *channel, int enable)
{
  return TarsResPwm_Enable(channel, enable);
}

int TarsResource_PwmSetDuty(const char *channel, float duty_pct)
{
  return TarsResPwm_SetDuty(channel, duty_pct);
}

int TarsResource_PwmSetFreq(const char *tim_id, uint32_t freq_hz)
{
  return TarsResPwm_SetFreq(tim_id, freq_hz);
}

int TarsResource_ResGrant(const char *id, tars_owner_t owner)
{
  return TarsResMgr_Grant(id, owner);
}

int TarsResource_PwmSetPersist(const char *channel, int boot_enable)
{
  return TarsResPwm_SetPersist(channel, boot_enable);
}

int TarsResource_ProfileSave(void)
{
  return TarsResProfile_Save();
}

int TarsResource_ProfileLoad(void)
{
  int st = TarsResProfile_Load();

  if (st == TARS_RES_PROFILE_ERR_NONE)
  {
    return 0;
  }

  if (st != 0)
  {
    return st;
  }

  return TarsResProfile_Apply();
}

int TarsResource_ProfileClear(void)
{
  return TarsResProfile_Clear();
}

int TarsResource_ProfileFormatStored(char *out, uint32_t out_size)
{
  return TarsResProfile_FormatStored(out, out_size);
}

void TarsResource_Task(void const *argument)
{
  uint32_t motor_accum_ms = 0U;

  (void)argument;

  /* Let defaultTask bring up LCD/viewport and pin map before sampling. */
  osDelay(150);

  TarsResource_Init();

  for (;;)
  {
    TarsResGpio_SampleButtons(TARS_RES_TICK_MS);

    motor_accum_ms += TARS_RES_TICK_MS;
    if (motor_accum_ms >= TARS_RES_MOTOR_PERIOD_MS)
    {
      motor_accum_ms = 0U;
      TarsMotor_Step();
    }

    osDelay(TARS_RES_TICK_MS);
  }
}
