#include "tars_hal.h"
#include "tars_motor.h"
#include <stdio.h>

void TarsHal_Init(void)
{
  /* Motor control is owned by the built-in motor app. */
}

void TarsHal_FormatStatus(char *out, uint32_t out_size)
{
  tars_motor_snapshot_t snap;

  if ((out == NULL) || (out_size == 0U))
  {
    return;
  }

  TarsMotor_GetSnapshot(&snap);

  (void)snprintf(out,
                 out_size,
                 "hal: wifi=stub can=stub motor=run loops=%lu pos=%.1f\r\n",
                 (unsigned long)snap.loop_count,
                 (double)snap.position_deg);
}
