#include "tars_res_gpio.h"
#include "tars_res_mgr.h"
#include "tars_mcu_pinmap.h"
#include "main.h"

#define TARS_RES_GPIO_MAX_BUTTONS   4U

typedef struct {
  GPIO_TypeDef    *port;
  uint16_t         pin;
  uint8_t          active_high;
  uint8_t          stable_pressed;
  uint16_t         debounce_ms;
  uint16_t         counter;
  tars_button_cb_t cb;
  uint8_t          in_use;
} tars_button_t;

static tars_button_t s_buttons[TARS_RES_GPIO_MAX_BUTTONS];
static uint32_t s_button_count;

static int gpio_lookup_entry(const char *pin_name, const tars_mcu_gpio_entry_t **entry_out)
{
  uint32_t count = 0U;
  const tars_mcu_gpio_entry_t *table = TarsMcuPinmap_GetGpioTable(&count);
  GPIO_TypeDef *port = NULL;
  uint16_t pin = 0U;
  uint32_t i;

  if (TarsMcuPinmap_ResolveGpio(pin_name, &port, &pin) != 0)
  {
    return -1;
  }

  for (i = 0U; i < count; i++)
  {
    if ((table[i].port == port) && (table[i].hal_pin == pin))
    {
      if (entry_out != NULL)
      {
        *entry_out = &table[i];
      }
      return 0;
    }
  }

  return -1;
}

static void gpio_enable_port_clock(GPIO_TypeDef *port)
{
  if (port == GPIOA) { __HAL_RCC_GPIOA_CLK_ENABLE(); }
  else if (port == GPIOB) { __HAL_RCC_GPIOB_CLK_ENABLE(); }
  else if (port == GPIOC) { __HAL_RCC_GPIOC_CLK_ENABLE(); }
  else if (port == GPIOD) { __HAL_RCC_GPIOD_CLK_ENABLE(); }
  else if (port == GPIOE) { __HAL_RCC_GPIOE_CLK_ENABLE(); }
  else if (port == GPIOF) { __HAL_RCC_GPIOF_CLK_ENABLE(); }
  else if (port == GPIOG) { __HAL_RCC_GPIOG_CLK_ENABLE(); }
  else if (port == GPIOH) { __HAL_RCC_GPIOH_CLK_ENABLE(); }
  else if (port == GPIOI) { __HAL_RCC_GPIOI_CLK_ENABLE(); }
}

static int gpio_is_input_only(GPIO_TypeDef *port, uint16_t pin)
{
  return (port == GPIOA && pin == GPIO_PIN_0) ? 1 : 0;
}

static void gpio_ensure_output(GPIO_TypeDef *port, uint16_t pin)
{
  GPIO_InitTypeDef gpio = {0};

  gpio_enable_port_clock(port);
  gpio.Pin = pin;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(port, &gpio);
}

int TarsResGpio_Write(const char *pin_name, int value)
{
  const tars_mcu_gpio_entry_t *entry = NULL;
  GPIO_TypeDef *port = NULL;
  uint16_t pin = 0U;
  int st;

  if (TarsMcuPinmap_ResolveGpio(pin_name, &port, &pin) != 0)
  {
    return TARS_RES_ERR_SCOPE;
  }

  if (gpio_is_input_only(port, pin) != 0)
  {
    return -2;
  }

  if (gpio_lookup_entry(pin_name, &entry) != 0)
  {
    return TARS_RES_ERR_SCOPE;
  }

  if (TarsResMgr_GetOwner(entry->pin_name) != TARS_OWNER_GPIO)
  {
    return TARS_RES_ERR_OWNER;
  }

  st = TarsResMgr_AcquireGpioPin(entry->pin_name, TARS_OWNER_GPIO);
  if (st != 0)
  {
    return st;
  }

  gpio_ensure_output(port, pin);
  HAL_GPIO_WritePin(port, pin, (value != 0) ? GPIO_PIN_SET : GPIO_PIN_RESET);

  (void)TarsResMgr_ReleaseGpioPin(entry->pin_name, TARS_OWNER_GPIO);
  return 0;
}

int TarsResGpio_Read(const char *pin_name, int *value_out)
{
  GPIO_TypeDef *port = NULL;
  uint16_t pin = 0U;

  if ((value_out == NULL) || (TarsMcuPinmap_ResolveGpio(pin_name, &port, &pin) != 0))
  {
    return TARS_RES_ERR_SCOPE;
  }

  *value_out = (HAL_GPIO_ReadPin(port, pin) == GPIO_PIN_SET) ? 1 : 0;
  return 0;
}

int TarsResGpio_ButtonRegister(const char *pin_name,
                               uint8_t active_high,
                               uint16_t debounce_ms,
                               tars_button_cb_t cb)
{
  GPIO_TypeDef *port = NULL;
  uint16_t pin = 0U;
  tars_button_t *btn;
  int id;

  if (s_button_count >= TARS_RES_GPIO_MAX_BUTTONS)
  {
    return -1;
  }

  if (TarsMcuPinmap_ResolveGpio(pin_name, &port, &pin) != 0)
  {
    return -1;
  }

  id = (int)s_button_count;
  btn = &s_buttons[id];
  btn->port = port;
  btn->pin = pin;
  btn->active_high = (active_high != 0U) ? 1U : 0U;
  btn->stable_pressed = 0U;
  btn->debounce_ms = (debounce_ms == 0U) ? 1U : debounce_ms;
  btn->counter = 0U;
  btn->cb = cb;
  btn->in_use = 1U;
  s_button_count++;

  return id;
}

int TarsResGpio_ButtonState(int button_id)
{
  if ((button_id < 0) || ((uint32_t)button_id >= s_button_count))
  {
    return 0;
  }

  return (s_buttons[button_id].stable_pressed != 0U) ? 1 : 0;
}

void TarsResGpio_SampleButtons(uint16_t tick_ms)
{
  uint32_t i;

  for (i = 0U; i < s_button_count; i++)
  {
    tars_button_t *btn = &s_buttons[i];
    uint8_t level;
    uint8_t pressed_now;
    uint16_t needed;

    if (btn->in_use == 0U)
    {
      continue;
    }

    level = (HAL_GPIO_ReadPin(btn->port, btn->pin) == GPIO_PIN_SET) ? 1U : 0U;
    pressed_now = (level == btn->active_high) ? 1U : 0U;

    if (pressed_now == btn->stable_pressed)
    {
      btn->counter = 0U;
      continue;
    }

    if (tick_ms == 0U)
    {
      tick_ms = 1U;
    }

    needed = (uint16_t)(btn->debounce_ms / tick_ms);
    if (needed == 0U)
    {
      needed = 1U;
    }

    btn->counter++;
    if (btn->counter >= needed)
    {
      btn->stable_pressed = pressed_now;
      btn->counter = 0U;

      if (btn->cb != NULL)
      {
        btn->cb((int)i, (int)pressed_now);
      }
    }
  }
}
