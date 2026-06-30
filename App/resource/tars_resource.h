#ifndef TARS_RESOURCE_H
#define TARS_RESOURCE_H

#include <stdint.h>
#include "tars_res_gpio.h"
#include "tars_res_pwm.h"
#include "tars_res_mgr.h"

#define TARS_RES_TICK_MS    5U

void TarsResource_Init(void);
void TarsResource_Task(void const *argument);

int TarsResource_GpioWrite(const char *pin_name, int value);
int TarsResource_GpioRead(const char *pin_name, int *value_out);

int TarsResource_PwmEnable(const char *channel, int enable);
int TarsResource_PwmSetDuty(const char *channel, float duty_pct);
int TarsResource_PwmSetFreq(const char *tim_id, uint32_t freq_hz);
int TarsResource_PwmSetPersist(const char *channel, int boot_enable);

int TarsResource_ResGrant(const char *id, tars_owner_t owner);

int TarsResource_ProfileSave(void);
int TarsResource_ProfileLoad(void);
int TarsResource_ProfileClear(void);
int TarsResource_ProfileFormatStored(char *out, uint32_t out_size);

#endif /* TARS_RESOURCE_H */
