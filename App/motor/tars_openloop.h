#ifndef TARS_OPENLOOP_H
#define TARS_OPENLOOP_H

#include <stdint.h>

/* Open-loop commutation: 3-step (Plan A) or 6-step (two-phase-on). */

#define TARS_OPENLOOP_MODE_3STEP  3U
#define TARS_OPENLOOP_MODE_6STEP  6U

typedef struct {
  uint8_t  enabled;
  uint8_t  mode;         /* 3 or 6 */
  uint8_t  duty_pct;
  uint8_t  step;         /* 3-step: 0..2 phase; 6-step: 0..5 seq index */
  uint8_t  direction;    /* 0=CW, 1=CCW */
  uint16_t step_ms;
  uint16_t ramp_start_ms;  /* 0 = no ramp */
  uint16_t ramp_end_ms;
  uint16_t ramp_ms;
  uint32_t step_count;
  uint32_t loop_count;
  uint8_t  hall_raw;
  int8_t   hall_spin;    /* +1=CCW mech, -1=CW mech, 0=unknown (from Hall seq) */
  uint8_t  hall_invert;
  uint8_t  hall_sync_on;
  uint8_t  hall_locked;  /* 1 = Hall-only commute, kick timer off */
} tars_openloop_snapshot_t;

void TarsOpenloop_Init(void);

int  TarsOpenloop_Enable(int enable);
int  TarsOpenloop_LastEnableError(void);
int  TarsOpenloop_IsEnabled(void);

void TarsOpenloop_SetDutyPct(uint8_t pct);
void TarsOpenloop_SetStepMs(uint16_t ms);
void TarsOpenloop_SetRampMs(uint16_t start_ms, uint16_t end_ms, uint16_t ramp_ms);
void TarsOpenloop_SetDirection(int ccw); /* 0=CW, 1=CCW */
void TarsOpenloop_SetMode(uint8_t mode); /* 3 or 6 */
uint8_t TarsOpenloop_GetMode(void);

void TarsOpenloop_SetHallSync(int enable);
void TarsOpenloop_SetHallPhase(uint8_t phase);
int  TarsOpenloop_IsHallSync(void);

void TarsOpenloop_GetSnapshot(tars_openloop_snapshot_t *out);

/* 20 kHz (ADC1 JEOC, same as FOC). */
void TarsOpenloop_ControlLoopISR(void);

#endif /* TARS_OPENLOOP_H */
