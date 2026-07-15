/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    adc.h
  * @brief   ADC1 injected-group configuration for FOC phase-current + Vbus
  *          sensing. Hand-authored (not CubeMX-generated) for the motor
  *          control variant; see App/foc/tars_foc.c.
  ******************************************************************************
  */
/* USER CODE END Header */
#ifndef __ADC_H__
#define __ADC_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

extern ADC_HandleTypeDef hadc1;

void MX_ADC1_Init(void);

#ifdef __cplusplus
}
#endif

#endif /* __ADC_H__ */
