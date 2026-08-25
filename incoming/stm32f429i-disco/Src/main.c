#include "incoming.h"

#include <string.h>

static incoming_report_t *const report =
    (incoming_report_t *)INCOMING_REPORT_ADDR;

static void gpio_early(void)
{
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();

  GPIO_InitTypeDef gpio = {0};
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;

  gpio.Pin = LD3_Pin | LD4_Pin;
  HAL_GPIO_Init(GPIOG, &gpio);
  HAL_GPIO_WritePin(GPIOG, LD3_Pin | LD4_Pin, GPIO_PIN_SET);

  gpio.Pin = NCS_MEMS_SPI_Pin | CSX_Pin;
  HAL_GPIO_Init(GPIOC, &gpio);
  HAL_GPIO_WritePin(GPIOC, NCS_MEMS_SPI_Pin | CSX_Pin, GPIO_PIN_SET);

  gpio.Pin = ACP_RST_Pin;
  HAL_GPIO_Init(ACP_RST_GPIO_Port, &gpio);

  gpio.Pin = RDX_Pin | WRX_DCX_Pin;
  HAL_GPIO_Init(GPIOD, &gpio);
  HAL_GPIO_WritePin(GPIOD, RDX_Pin | WRX_DCX_Pin, GPIO_PIN_SET);

  gpio.Mode = GPIO_MODE_INPUT;
  gpio.Pin = B1_Pin;
  HAL_GPIO_Init(B1_GPIO_Port, &gpio);
}

static void report_init(void)
{
  memset(report, 0, sizeof(*report));
  report->magic = INCOMING_REPORT_MAGIC;
  report->version = INCOMING_REPORT_VERSION;
}

static void finish_summary(void)
{
  if (report->fail_mask != 0U) {
    memcpy(report->summary, "INCOMING FAIL", 14);
  } else if (report->warn_mask != 0U) {
    memcpy(report->summary, "INCOMING WARN", 14);
  } else {
    memcpy(report->summary, "INCOMING PASS", 14);
  }
  report->done = 1U;
}

static void led_loop(void)
{
  const uint32_t fail = report->fail_mask;
  const uint32_t warn = report->warn_mask;

  for (;;) {
    if (fail != 0U) {
      HAL_GPIO_TogglePin(LD4_GPIO_Port, LD4_Pin);
      HAL_GPIO_WritePin(LD3_GPIO_Port, LD3_Pin, GPIO_PIN_SET);
      HAL_Delay(120);
    } else if (warn != 0U) {
      HAL_GPIO_TogglePin(LD3_GPIO_Port, LD3_Pin);
      HAL_GPIO_TogglePin(LD4_GPIO_Port, LD4_Pin);
      HAL_Delay(400);
    } else {
      HAL_GPIO_TogglePin(LD3_GPIO_Port, LD3_Pin);
      HAL_GPIO_WritePin(LD4_GPIO_Port, LD4_Pin, GPIO_PIN_SET);
      HAL_Delay(500);
    }
  }
}

void Incoming_SetPass(incoming_report_t *r, enum incoming_check bit)
{
  r->pass_mask |= (1u << (uint32_t)bit);
}

void Incoming_SetFail(incoming_report_t *r, enum incoming_check bit)
{
  r->fail_mask |= (1u << (uint32_t)bit);
}

void Incoming_SetWarn(incoming_report_t *r, enum incoming_check bit)
{
  r->warn_mask |= (1u << (uint32_t)bit);
}

void Incoming_SetSkip(incoming_report_t *r, enum incoming_check bit)
{
  r->skip_mask |= (1u << (uint32_t)bit);
}

void Error_Handler(void)
{
  report->fatal = 1U;
  memcpy(report->summary, "INCOMING FATAL", 15);
  report->done = 1U;
  for (;;) {
    HAL_GPIO_TogglePin(LD4_GPIO_Port, LD4_Pin);
    for (volatile uint32_t i = 0; i < 200000U; i++) {
    }
  }
}

int main(void)
{
  HAL_Init();
  gpio_early();
  report_init();
  Incoming_ClockConfig(report);
  Incoming_RunTests(report);
  finish_summary();
  led_loop();
}
