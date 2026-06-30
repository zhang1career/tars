#ifndef TARS_RES_PROFILE_H
#define TARS_RES_PROFILE_H

#include <stdint.h>

#define TARS_RES_PROFILE_ERR_PARAM   (-4)
#define TARS_RES_PROFILE_ERR_IO      (-8)
#define TARS_RES_PROFILE_ERR_CRC     (-3)
#define TARS_RES_PROFILE_ERR_STALE   (-9)
#define TARS_RES_PROFILE_ERR_NONE    (-10)

/* Load from LFS into RAM staging (validates file CRC; fw token checked on apply). */
int TarsResProfile_Load(void);

/* Remove stale file when fw identity mismatches; returns 1 if deleted. */
int TarsResProfile_PurgeStale(void);

/* Apply staged profile (grants, tim freq, pwm duty, boot enable). */
int TarsResProfile_Apply(void);

/* Collect runtime state and write profile file. */
int TarsResProfile_Save(void);

int TarsResProfile_Clear(void);

int TarsResProfile_FormatStored(char *out, uint32_t out_size);

int TarsResProfile_HasStaged(void);

/* 1 if staged profile requests pwm0 boot (TIM1 bring-up path selection). */
int TarsResProfile_Pwm0BootOnTim1(void);

#endif /* TARS_RES_PROFILE_H */
