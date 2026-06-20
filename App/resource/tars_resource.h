#ifndef TARS_RESOURCE_H
#define TARS_RESOURCE_H

#include <stdint.h>
#include "tars_res_gpio.h"

/* Resource layer: a single dedicated FreeRTOS task owns periodic access to
 * on-chip hardware (GPIO sampling/debounce, low-rate control loops, ...).
 * High-rate / DMA / IRQ-driven peripherals (USB, LTDC, PWM generation) keep
 * their own drivers; the resource layer only provides their control surface. */

#define TARS_RES_TICK_MS    5U

void TarsResource_Init(void);
void TarsResource_Task(void const *argument);

/* Thread-safe GPIO accessors (delegate to the GPIO resource class). */
int TarsResource_GpioWrite(const char *pin_name, int value);
int TarsResource_GpioRead(const char *pin_name, int *value_out);

#endif /* TARS_RESOURCE_H */
