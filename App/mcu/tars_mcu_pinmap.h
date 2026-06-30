#ifndef TARS_MCU_PINMAP_H
#define TARS_MCU_PINMAP_H

#include <stdint.h>
#include "stm32f4xx_hal.h"

typedef enum {
  TARS_OWNER_NONE = 0,
  TARS_OWNER_GPIO,
  TARS_OWNER_PWM,
  TARS_OWNER_FOC,
  TARS_OWNER_SYSTEM
} tars_owner_t;

typedef enum {
  TARS_RES_KIND_GPIO = 0,
  TARS_RES_KIND_PWM  = 1
} tars_res_kind_t;

typedef struct {
  const char *pin_name;
  const char *alias;
  GPIO_TypeDef *port;
  uint16_t hal_pin;
  tars_owner_t default_owner;
} tars_mcu_gpio_entry_t;

typedef struct {
  const char *channel;
  const char *alias;
  TIM_TypeDef *tim;
  const char *tim_id;
  uint32_t hal_channel;
  GPIO_TypeDef *port;
  uint16_t hal_pin;
  uint32_t gpio_af;
  tars_owner_t default_owner;
  uint8_t advanced_tim;
  const char *pin_name;
} tars_mcu_pwm_entry_t;

typedef struct {
  const char *id;
  tars_res_kind_t kind;
  uint16_t lock_order;
  tars_owner_t default_owner;
  int16_t linked_pwm_idx;
  int16_t table_idx;
} tars_res_catalog_entry_t;

typedef struct {
  const char *signal;
  const char *pin_name;
} tars_mcu_periph_entry_t;

const char *TarsMcuPinmap_BoardId(void);
const char *TarsMcuPinmap_McuId(void);
const char *TarsMcuPinmap_PackageId(void);

const tars_mcu_gpio_entry_t *TarsMcuPinmap_GetGpioTable(uint32_t *count_out);
const tars_mcu_pwm_entry_t *TarsMcuPinmap_GetPwmTable(uint32_t *count_out);
const tars_res_catalog_entry_t *TarsMcuPinmap_GetResCatalog(uint32_t *count_out);
const tars_mcu_periph_entry_t *TarsMcuPinmap_GetPeriphTable(uint32_t *count_out);

int TarsMcuPinmap_ResolveGpio(const char *name, GPIO_TypeDef **port_out, uint16_t *pin_out);
int TarsMcuPinmap_ResolvePwm(const char *name, const tars_mcu_pwm_entry_t **entry_out);
int TarsMcuPinmap_FindCatalog(const char *id, const tars_res_catalog_entry_t **entry_out,
                              uint32_t *index_out);

void TarsMcuPinmap_FormatGpioList(char *out, uint32_t out_size);
void TarsMcuPinmap_FormatPwmList(char *out, uint32_t out_size);
void TarsMcuPinmap_FormatPeriphMap(char *out, uint32_t out_size);

const char *TarsOwner_ToString(tars_owner_t owner);
int TarsOwner_Parse(const char *text, tars_owner_t *owner_out);

#endif /* TARS_MCU_PINMAP_H */
