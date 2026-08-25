#include "incoming.h"

static void switch_to_hsi(void)
{
  RCC_ClkInitTypeDef clk = {0};

  clk.ClockType = RCC_CLOCKTYPE_SYSCLK;
  clk.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  (void)HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_0);
  __HAL_RCC_PLL_DISABLE();
}

static int start_pll(uint32_t pll_src, uint32_t hse_state, uint32_t pllm)
{
  RCC_OscInitTypeDef osc = {0};
  RCC_ClkInitTypeDef clk = {0};

  osc.OscillatorType = RCC_OSCILLATORTYPE_HSE | RCC_OSCILLATORTYPE_HSI;
  osc.HSIState = RCC_HSI_ON;
  osc.HSEState = hse_state;
  osc.PLL.PLLState = RCC_PLL_ON;
  osc.PLL.PLLSource = pll_src;
  osc.PLL.PLLM = pllm;
  osc.PLL.PLLN = 72;
  osc.PLL.PLLP = RCC_PLLP_DIV2;
  osc.PLL.PLLQ = 3;
  if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
    return -1;
  }

  clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                  RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
  clk.APB1CLKDivider = RCC_HCLK_DIV2;
  clk.APB2CLKDivider = RCC_HCLK_DIV1;
  if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2) != HAL_OK) {
    return -1;
  }
  return 0;
}

void Incoming_ClockConfig(incoming_report_t *r)
{
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

  switch_to_hsi();
  if (start_pll(RCC_PLLSOURCE_HSE, RCC_HSE_BYPASS, 4U) == 0) {
    r->clock_src = INCOMING_CLK_HSE_BYP;
  } else {
    switch_to_hsi();
    if (start_pll(RCC_PLLSOURCE_HSE, RCC_HSE_ON, 4U) == 0) {
      r->clock_src = INCOMING_CLK_HSE_XTAL;
    } else {
      switch_to_hsi();
      if (start_pll(RCC_PLLSOURCE_HSI, RCC_HSE_OFF, 8U) != 0) {
        Error_Handler();
      }
      r->clock_src = INCOMING_CLK_HSI;
    }
  }

  r->clock_hz = HAL_RCC_GetSysClockFreq();
}
