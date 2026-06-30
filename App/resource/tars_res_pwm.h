#ifndef TARS_RES_PWM_H
#define TARS_RES_PWM_H

#include <stdint.h>

int TarsResPwm_Enable(const char *channel, int enable);
int TarsResPwm_SetDuty(const char *channel, float duty_pct);
int TarsResPwm_SetFreq(const char *tim_id, uint32_t freq_hz);
int TarsResPwm_GetStatus(const char *channel, char *out, uint32_t out_size);

#endif /* TARS_RES_PWM_H */
