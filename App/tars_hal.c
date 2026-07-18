#include "tars_hal.h"
#include "tars_motor.h"
#include "tars_features.h"
#include "node_bus/tars_nodebus.h"
#include "i2c.h"
#include <stdio.h>

void TarsHal_Init(void)
{
  /* Motor control is owned by the built-in motor app. */
#if !TARS_FEATURE_LCD
  /* I2C2 already brought up in main(); bind the Node Bus driver. */
  TarsNodeBus_Init(&hi2c2);
#endif
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
