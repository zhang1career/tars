#include "tars_hal.h"
#include <stdio.h>

void TarsHal_Init(void)
{
  /* Placeholders until WiFi/CAN/motor hardware is defined. */
}

void TarsHal_FormatStatus(char *out, uint32_t out_size)
{
  if ((out == NULL) || (out_size == 0U))
  {
    return;
  }

  (void)snprintf(out,
                 out_size,
                 "hal: wifi=stub can=stub motor=stub\r\n");
}
