#ifndef TARS_HALL6_H
#define TARS_HALL6_H

#include <stdint.h>

/* Hall six-step BLDC commutation on TIM1 (pwm0..2) + GPIO Hall inputs. */

typedef struct {
  uint8_t  hall_raw;     /* 3-bit Ha|Hb|Hc (1..6 valid) */
  uint8_t  step;         /* 0..5 commutation step */
  uint8_t  duty_pct;     /* PWM high-side duty [0..100] */
  uint8_t  enabled;
  uint8_t  direction;    /* 0=CW, 1=CCW */
  uint8_t  fault;        /* 1=invalid hall (0 or 7) */
  uint8_t  kick;         /* 1=open-loop alignment active */
  uint8_t  phase;        /* hall commutation offset 0..5 */
  uint32_t loop_count;
  uint32_t hall_changes;
} tars_hall6_snapshot_t;

void TarsHall6_Init(void);
void TarsHall6_BootHw(void);

int  TarsHall6_Enable(int enable);
int  TarsHall6_LastEnableError(void);
int  TarsHall6_IsEnabled(void);

void TarsHall6_SetDutyPct(uint8_t pct);
void TarsHall6_SetKickDutyPct(uint8_t pct);
void TarsHall6_SetPhaseOffset(uint8_t offset);
void TarsHall6_SetDirection(int ccw); /* 0=CW, 1=CCW */

void TarsHall6_GetSnapshot(tars_hall6_snapshot_t *out);
uint8_t TarsHall6_ReadHallRaw(void);

void TarsHall6_ImapReset(void);
void TarsHall6_ImapGet(uint32_t n[7], int32_t raw_a[7], int32_t raw_b[7], int32_t raw_c[7]);
void TarsHall6_GetTim1Gate(uint32_t *ccer, uint32_t *bdtr,
                           uint32_t *ccr1, uint32_t *ccr2, uint32_t *ccr3);

/* 20 kHz entry (same cadence as FOC ADC ISR). */
void TarsHall6_ControlLoopISR(void);

#endif /* TARS_HALL6_H */
