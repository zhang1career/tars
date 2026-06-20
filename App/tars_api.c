#include "tars_app.h"
#include "tars_api.h"
#include "main.h"
#include "cmsis_os.h"

static tars_api_t s_api;

static void api_gpio_write(uint32_t pin_id, int value)
{
  (void)pin_id;
  (void)value;
}

static int api_gpio_read(uint32_t pin_id)
{
  (void)pin_id;
  return 0;
}

static void api_sleep_ms(uint32_t ms)
{
  osDelay(ms);
}

static void api_log(const char *msg)
{
  (void)msg;
}

void TarsApi_Init(void)
{
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
