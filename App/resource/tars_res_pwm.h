#ifndef TARS_RES_PWM_H
#define TARS_RES_PWM_H

#include <stdint.h>

int TarsResPwm_Enable(const char *channel, int enable);
int TarsResPwm_SetDuty(const char *channel, float duty_pct);
int TarsResPwm_SetFreq(const char *tim_id, uint32_t freq_hz);
int TarsResPwm_GetStatus(const char *channel, char *out, uint32_t out_size);

int TarsResPwm_SetPersist(const char *channel, int boot_enable);
int TarsResPwm_GetPersist(const char *channel, int *boot_enable_out);
int TarsResPwm_GetDuty(const char *channel, uint8_t *duty_out);
int TarsResPwm_GetTimFreq(const char *tim_id, uint32_t *freq_hz_out);
int TarsResPwm_TimFreqConfigured(const char *tim_id);

#endif /* TARS_RES_PWM_H */
