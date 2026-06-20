#include "tars_resource.h"
#include "tars_res_gpio.h"
#include "tars_motor.h"
#include "lcd_viewport.h"
#include "cmsis_os.h"

/* Motor control loop runs at 100 ms (TarsMotor uses dt = 0.1 s). */
#define TARS_RES_MOTOR_PERIOD_MS    100U

static uint8_t s_initialized;

static void resource_on_b1(int button_id, int pressed)
{
  (void)button_id;

  /* Act on the press edge only. LCD access is mutex-protected internally. */
  if (pressed != 0)
  {
    LcdViewport_NextPage();
  }
}

void TarsResource_Init(void)
{
  if (s_initialized != 0U)
  {
    return;
  }

  /* GPIO inputs: B1 user button (PA0, active high, 20 ms debounce). */
  (void)TarsResGpio_ButtonRegister("b1", 1U, 20U, resource_on_b1);

  /* Low-rate control loops owned by the resource task. */
  TarsMotor_Init();

  s_initialized = 1U;
}

int TarsResource_GpioWrite(const char *pin_name, int value)
{
  return TarsResGpio_Write(pin_name, value);
}

int TarsResource_GpioRead(const char *pin_name, int *value_out)
{
  return TarsResGpio_Read(pin_name, value_out);
}

void TarsResource_Task(void const *argument)
{
  uint32_t motor_accum_ms = 0U;

  (void)argument;

  /* Let defaultTask bring up LCD/viewport and pin map before sampling. */
  osDelay(150);

  TarsResource_Init();

  for (;;)
  {
    TarsResGpio_SampleButtons(TARS_RES_TICK_MS);

    motor_accum_ms += TARS_RES_TICK_MS;
    if (motor_accum_ms >= TARS_RES_MOTOR_PERIOD_MS)
    {
      motor_accum_ms = 0U;
      TarsMotor_Step();
    }

    osDelay(TARS_RES_TICK_MS);
  }
}
