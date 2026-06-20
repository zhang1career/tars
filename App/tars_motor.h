#ifndef TARS_MOTOR_H
#define TARS_MOTOR_H

#include <stdint.h>

typedef enum {
  TARS_MOTOR_STATE_IDLE = 0,
  TARS_MOTOR_STATE_RUN  = 1,
  TARS_MOTOR_STATE_FAULT = 2
} tars_motor_state_t;

typedef struct {
  float    position_deg;
  float    velocity_rpm;
  float    target_deg;
  float    pwm_duty;
  float    error_deg;
  uint32_t loop_count;
  uint8_t  state;
  uint8_t  fault_code;
} tars_motor_snapshot_t;

void TarsMotor_Init(void);
void TarsMotor_Step(void);
void TarsMotor_GetSnapshot(tars_motor_snapshot_t *out);

#endif /* TARS_MOTOR_H */
