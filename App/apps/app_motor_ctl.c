#include "tars_app.h"
#include "tars_motor.h"

static void motor_ctl_init(const tars_api_t *api)
{
  (void)api;
  TarsMotor_Init();
}

static void motor_ctl_tick(void)
{
  TarsMotor_Step();
}

static void motor_ctl_release(void)
{
}

static const tars_resource_t s_motor_timer = {
  TARS_RESOURCE_TIMER,
  1U,
  0U
};

static const tars_manifest_t s_motor_manifest = {
  .count = 1U,
  .items = { s_motor_timer }
};

const tars_builtin_app_t g_app_motor_ctl = {
  .name = "motor",
  .timeslice = 1U,
  .manifest = s_motor_manifest,
  .init = motor_ctl_init,
  .tick = motor_ctl_tick,
  .release = motor_ctl_release
};
