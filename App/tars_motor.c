#include "tars_motor.h"
#include "tim.h"
#include <math.h>

#define TARS_MOTOR_KP           0.15f
#define TARS_MOTOR_MAX_VEL      120.0f
#define TARS_MOTOR_DT_S         0.1f

static tars_motor_snapshot_t s_motor;
static float s_demo_phase;

void TarsMotor_Init(void)
{
  s_motor.position_deg = 0.0f;
  s_motor.velocity_rpm = 0.0f;
  s_motor.target_deg = 0.0f;
  s_motor.pwm_duty = 0.0f;
  s_motor.error_deg = 0.0f;
  s_motor.loop_count = 0U;
  s_motor.state = TARS_MOTOR_STATE_IDLE;
  s_motor.fault_code = 0U;
  s_demo_phase = 0.0f;

  (void)HAL_TIM_Base_Start(&htim1);
}

void TarsMotor_Step(void)
{
  float error;
  float pwm;
  float accel;

  s_demo_phase += TARS_MOTOR_DT_S;
  s_motor.target_deg = 45.0f * sinf(s_demo_phase * 0.5f);

  error = s_motor.target_deg - s_motor.position_deg;
  s_motor.error_deg = error;

  pwm = TARS_MOTOR_KP * error;
  if (pwm > 1.0f)
  {
    pwm = 1.0f;
  }
  else if (pwm < -1.0f)
  {
    pwm = -1.0f;
  }

  s_motor.pwm_duty = pwm;
  accel = pwm * TARS_MOTOR_MAX_VEL;

  s_motor.velocity_rpm += accel * TARS_MOTOR_DT_S;
  if (s_motor.velocity_rpm > TARS_MOTOR_MAX_VEL)
  {
    s_motor.velocity_rpm = TARS_MOTOR_MAX_VEL;
  }
  else if (s_motor.velocity_rpm < -TARS_MOTOR_MAX_VEL)
  {
    s_motor.velocity_rpm = -TARS_MOTOR_MAX_VEL;
  }

  s_motor.position_deg += (s_motor.velocity_rpm / 60.0f) * 360.0f * TARS_MOTOR_DT_S;
  s_motor.velocity_rpm *= 0.92f;

  s_motor.loop_count++;
  s_motor.state = TARS_MOTOR_STATE_RUN;
  s_motor.fault_code = 0U;
}

void TarsMotor_GetSnapshot(tars_motor_snapshot_t *out)
{
  if (out == NULL)
  {
    return;
  }

  *out = s_motor;
}
