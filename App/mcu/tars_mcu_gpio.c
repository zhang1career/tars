#include "tars_mcu.h"
#include "tars_mcu_pinmap.h"
#include "main.h"
#include <stdio.h>

int TarsMcu_GpioWrite(const char *pin_name, int value)
{
  GPIO_TypeDef *port = NULL;
  uint16_t pin = 0U;

  if (TarsMcuPinmap_ResolveGpio(pin_name, &port, &pin) != 0)
  {
    return -1;
  }

  HAL_GPIO_WritePin(port, pin, (value != 0) ? GPIO_PIN_SET : GPIO_PIN_RESET);
  return 0;
}

int TarsMcu_GpioRead(const char *pin_name, int *value_out)
{
  GPIO_TypeDef *port = NULL;
  uint16_t pin = 0U;

  if ((value_out == NULL) || (TarsMcuPinmap_ResolveGpio(pin_name, &port, &pin) != 0))
  {
    return -1;
  }

  *value_out = (HAL_GPIO_ReadPin(port, pin) == GPIO_PIN_SET) ? 1 : 0;
  return 0;
}

void TarsMcu_FormatInfo(char *out, uint32_t out_size)
{
  if ((out == NULL) || (out_size == 0U))
  {
    return;
  }

  (void)snprintf(out,
                 out_size,
                 "mcu: %s (%s/%s)\r\n",
                 TarsMcuPinmap_BoardId(),
                 TarsMcuPinmap_McuId(),
                 TarsMcuPinmap_PackageId());
}
