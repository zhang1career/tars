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

int TarsResPwm_IsRunning(const char *channel);

int TarsResPwm_ParsePolarity(const char *name, int *low_out);
int TarsResPwm_SetPolarity(const char *channel, int low);
int TarsResPwm_GetPolarity(const char *channel, int *low_out);

/* pwm3 follows pwm0: TIM3 slave-reset on TIM1 TRGO when offset=0; otherwise
 * software snap on enable/resync. offset_ticks is mod follower period. */
int TarsResPwm_LinkGetConfig(int *enable_out, int32_t *offset_out);
int TarsResPwm_LinkSet(int enable, int32_t offset_ticks);
int TarsResPwm_LinkEnable(int enable);
int TarsResPwm_LinkSetOffset(int32_t offset_ticks);
int TarsResPwm_LinkResync(void);
int TarsResPwm_LinkGetStatus(char *out, uint32_t out_size);

#endif /* TARS_RES_PWM_H */
