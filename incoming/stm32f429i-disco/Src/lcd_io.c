#include "lcd_io.h"
#include "main.h"
#include "spi.h"

extern SPI_HandleTypeDef hspi5;

void LCD_IO_Init(void)
{
  HAL_GPIO_WritePin(CSX_GPIO_Port, CSX_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(WRX_DCX_GPIO_Port, WRX_DCX_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(RDX_GPIO_Port, RDX_Pin, GPIO_PIN_SET);
}

void LCD_IO_WriteReg(uint8_t reg)
{
  HAL_GPIO_WritePin(WRX_DCX_GPIO_Port, WRX_DCX_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(CSX_GPIO_Port, CSX_Pin, GPIO_PIN_RESET);
  (void)HAL_SPI_Transmit(&hspi5, &reg, 1U, 100U);
  HAL_GPIO_WritePin(CSX_GPIO_Port, CSX_Pin, GPIO_PIN_SET);
}

void LCD_IO_WriteData(uint16_t reg_value)
{
  uint8_t value = (uint8_t)reg_value;

  HAL_GPIO_WritePin(WRX_DCX_GPIO_Port, WRX_DCX_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(CSX_GPIO_Port, CSX_Pin, GPIO_PIN_RESET);
  (void)HAL_SPI_Transmit(&hspi5, &value, 1U, 100U);
  HAL_GPIO_WritePin(CSX_GPIO_Port, CSX_Pin, GPIO_PIN_SET);
}

uint32_t LCD_IO_ReadData(uint16_t reg_value, uint8_t read_size)
{
  uint8_t cmd = (uint8_t)reg_value;
  uint8_t buf[4] = {0};
  uint32_t value = 0U;
  uint8_t n = read_size;

  if (n > 4U) {
    n = 4U;
  }

  HAL_GPIO_WritePin(NCS_MEMS_SPI_GPIO_Port, NCS_MEMS_SPI_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(WRX_DCX_GPIO_Port, WRX_DCX_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(CSX_GPIO_Port, CSX_Pin, GPIO_PIN_RESET);
  (void)HAL_SPI_Transmit(&hspi5, &cmd, 1U, 100U);
  HAL_GPIO_WritePin(WRX_DCX_GPIO_Port, WRX_DCX_Pin, GPIO_PIN_SET);
  (void)HAL_SPI_Receive(&hspi5, buf, n, 100U);
  HAL_GPIO_WritePin(CSX_GPIO_Port, CSX_Pin, GPIO_PIN_SET);

  for (uint8_t i = 0U; i < n; i++) {
    value = (value << 8) | buf[i];
  }
  return value;
}

void LCD_Delay(uint32_t delay)
{
  HAL_Delay(delay);
}
