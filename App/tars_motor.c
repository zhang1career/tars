#include "tars_motor.h"
#include "tars_foc.h"
#include <stddef.h>

/*
 * Compatibility facade. The demo position loop has been replaced by the
 * firmware-resident FOC driver (App/foc/tars_foc.c), which wraps the
 * Simulink-generated controller. This shim keeps the legacy
 * tars_motor_snapshot_t API stable for existing consumers (LCD viewport,
 * tars_hal, probe motor_* metrics); richer FOC telemetry (id/iq/theta/vdc)
 * is exposed directly via TarsFoc_GetSnapshot().
 */

#define TARS_RAD_TO_DEG   57.2957795f

void TarsMotor_Init(void)
{
  TarsFoc_Init();
}

void TarsMotor_Step(void)
{
  TarsFoc_Step();
}

void TarsMotor_GetSnapshot(tars_motor_snapshot_t *out)
{
  tars_foc_snapshot_t foc;
  float theta_deg;

  if (out == NULL)
  {
    return;
  }

  TarsFoc_GetSnapshot(&foc);

  theta_deg = foc.theta_est_rad * TARS_RAD_TO_DEG;
  while (theta_deg >= 360.0f)
  {
    theta_deg -= 360.0f;
  }
  while (theta_deg < 0.0f)
  {
    theta_deg += 360.0f;
  }

  out->position_deg = theta_deg;                         /* electrical angle  */
  out->velocity_rpm = foc.speed_est_rpm;
  out->target_deg   = foc.speed_ref_rpm;                 /* speed command     */
  out->pwm_duty     = foc.duty_a;
  out->error_deg    = foc.speed_ref_rpm - foc.speed_est_rpm;
  out->loop_count   = foc.loop_count;
  out->state        = foc.state;                         /* enum values align */
  out->fault_code   = foc.fault_code;
}
