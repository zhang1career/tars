#ifndef TARS_MCU_PINMAP_H
#define TARS_MCU_PINMAP_H

#include <stdint.h>
#include "stm32f4xx_hal.h"

/* Logical pin name: pg13 = port G pin 13 (optional leading 'p'). */
typedef struct {
  const char *pin_name;
  const char *alias;
  GPIO_TypeDef *port;
  uint16_t hal_pin;
} tars_mcu_gpio_entry_t;

/* Peripheral signal -> board pin routing (signal names are MCU-generic). */
typedef struct {
  const char *signal;
  const char *pin_name;
} tars_mcu_periph_entry_t;

const char *TarsMcuPinmap_BoardId(void);
const char *TarsMcuPinmap_McuId(void);
const char *TarsMcuPinmap_PackageId(void);

const tars_mcu_gpio_entry_t *TarsMcuPinmap_GetGpioTable(uint32_t *count_out);
const tars_mcu_periph_entry_t *TarsMcuPinmap_GetPeriphTable(uint32_t *count_out);

int TarsMcuPinmap_ResolveGpio(const char *name, GPIO_TypeDef **port_out, uint16_t *pin_out);
void TarsMcuPinmap_FormatGpioList(char *out, uint32_t out_size);
void TarsMcuPinmap_FormatPeriphMap(char *out, uint32_t out_size);

#endif /* TARS_MCU_PINMAP_H */
