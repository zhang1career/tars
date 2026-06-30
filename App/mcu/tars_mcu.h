#ifndef TARS_MCU_H
#define TARS_MCU_H

#include <stdint.h>
#include "tars_mcu_pinmap.h"

int TarsMcu_GpioWrite(const char *pin_name, int value);
int TarsMcu_GpioRead(const char *pin_name, int *value_out);

int TarsMcu_PwmEnable(const char *channel, int enable);
int TarsMcu_PwmSetDuty(const char *channel, float duty_pct);
int TarsMcu_PwmSetFreq(const char *tim_id, uint32_t freq_hz);

int TarsMcu_ResGrant(const char *id, tars_owner_t owner);

void TarsMcu_FormatInfo(char *out, uint32_t out_size);
int TarsMcu_ShellHandle(const char *args, char *out, uint32_t out_size);

const char *TarsMcu_ResErrText(int code);

#endif /* TARS_MCU_H */
