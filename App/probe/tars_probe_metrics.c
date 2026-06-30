#include "tars_probe_metrics.h"
#include "tars_lua.h"
#include "tars_motor.h"
#include "tars_foc.h"
#include "tars_res_gpio.h"
#include "main.h"
#include "FreeRTOS.h"
#include "task.h"
#include "cmsis_os.h"

float TarsProbeMetrics_Read(uint8_t provider, uint32_t arg)
{
  switch (provider)
  {
  case TARS_PROBE_SRC_UPTIME:
    return (float)HAL_GetTick();

  case TARS_PROBE_SRC_TICK_HZ:
    return (float)configTICK_RATE_HZ;

  case TARS_PROBE_SRC_HEAP_FREE:
    return (float)xPortGetFreeHeapSize();

  case TARS_PROBE_SRC_HEAP_MIN_FREE:
    return (float)xPortGetMinimumEverFreeHeapSize();

  case TARS_PROBE_SRC_LUA_HEAP_USED:
  {
    uint32_t used = 0U;
    uint32_t total = 0U;
    TarsLua_GetHeapUsage(&used, &total);
    return (float)used;
  }

  case TARS_PROBE_SRC_TASK_COUNT:
    return (float)uxTaskGetNumberOfTasks();

  case TARS_PROBE_SRC_MOTOR_POS:
  {
    tars_motor_snapshot_t snap;
    TarsMotor_GetSnapshot(&snap);
    return snap.position_deg;
  }

  case TARS_PROBE_SRC_MOTOR_VEL:
  {
    tars_motor_snapshot_t snap;
    TarsMotor_GetSnapshot(&snap);
    return snap.velocity_rpm;
  }

  case TARS_PROBE_SRC_MOTOR_LOOPS:
  {
    tars_motor_snapshot_t snap;
    TarsMotor_GetSnapshot(&snap);
    return (float)snap.loop_count;
  }

  case TARS_PROBE_SRC_FOC_ID:
  {
    tars_foc_snapshot_t foc;
    TarsFoc_GetSnapshot(&foc);
    return foc.id;
  }

  case TARS_PROBE_SRC_FOC_IQ:
  {
    tars_foc_snapshot_t foc;
    TarsFoc_GetSnapshot(&foc);
    return foc.iq;
  }

  case TARS_PROBE_SRC_FOC_THETA:
  {
    tars_foc_snapshot_t foc;
    TarsFoc_GetSnapshot(&foc);
    return foc.theta_est_rad * 57.2957795f;
  }

  case TARS_PROBE_SRC_FOC_SPEED:
  {
    tars_foc_snapshot_t foc;
    TarsFoc_GetSnapshot(&foc);
    return foc.speed_est_rpm;
  }

  case TARS_PROBE_SRC_FOC_VDC:
  {
    tars_foc_snapshot_t foc;
    TarsFoc_GetSnapshot(&foc);
    return foc.vdc;
  }

  case TARS_PROBE_SRC_BUTTON:
    return (float)TarsResGpio_ButtonState((int)arg);

  default:
    return 0.0f;
  }
}
