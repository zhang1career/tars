#ifndef TARS_RES_DAC_H
#define TARS_RES_DAC_H

#include <stdint.h>

#define TARS_DAC_MAX_VALUE   4095U

int TarsResDac_Enable(const char *channel, int enable);
int TarsResDac_SetLevel(const char *channel, float level_pct);
int TarsResDac_GetLevel(const char *channel, float *level_pct_out);
int TarsResDac_GetStatus(const char *channel, char *out, uint32_t out_size);
int TarsResDac_IsRunning(const char *channel);

#endif /* TARS_RES_DAC_H */
