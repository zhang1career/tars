#include "tars_mcu.h"
#include "tars_mcu_pinmap.h"
#include "tars_resource.h"
#include "main.h"
#include <stdio.h>

int TarsMcu_GpioWrite(const char *pin_name, int value)
{
  return TarsResource_GpioWrite(pin_name, value);
}

int TarsMcu_GpioRead(const char *pin_name, int *value_out)
{
  return TarsResource_GpioRead(pin_name, value_out);
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
