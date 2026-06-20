#include "tars_app.h"
#include "tars_api.h"
#include "tars_vfs.h"
#include "main.h"
#include "cmsis_os.h"

static tars_api_t s_api;

static void api_gpio_write(uint32_t pin_id, int value)
{
  GPIO_TypeDef *port = NULL;
  uint16_t pin = 0U;

  if (pin_id == 13U)
  {
    port = LD3_GPIO_Port;
    pin = LD3_Pin;
  }
  else if (pin_id == 14U)
  {
    port = LD4_GPIO_Port;
    pin = LD4_Pin;
  }
  else
  {
    return;
  }

  HAL_GPIO_WritePin(port, pin, (value != 0) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static int api_gpio_read(uint32_t pin_id)
{
  GPIO_TypeDef *port = NULL;
  uint16_t pin = 0U;

  if (pin_id == 13U)
  {
    port = LD3_GPIO_Port;
    pin = LD3_Pin;
  }
  else if (pin_id == 14U)
  {
    port = LD4_GPIO_Port;
    pin = LD4_Pin;
  }
  else
  {
    return 0;
  }

  return (HAL_GPIO_ReadPin(port, pin) == GPIO_PIN_SET) ? 1 : 0;
}

static void api_sleep_ms(uint32_t ms)
{
  osDelay(ms);
}

static void api_log(const char *msg)
{
  TarsVfs_WriteLog(msg);
}

void TarsApi_Init(void)
{
  TarsVfs_Init();
  s_api.api_version = TARS_API_VERSION;
  s_api.gpio_write = api_gpio_write;
  s_api.gpio_read = api_gpio_read;
  s_api.sleep_ms = api_sleep_ms;
  s_api.log = api_log;
}

const tars_api_t *TarsApp_GetApi(void)
{
  return &s_api;
}
