#include "lcd_io.h"
#include "main.h"
#include "spi.h"

static uint8_t s_lcd_io_ready;

static void lcd_io_write_byte(uint8_t value)
{
  (void)HAL_SPI_Transmit(&hspi5, &value, 1U, 1000U);
}

void LCD_IO_Init(void)
{
  GPIO_InitTypeDef gpio = {0};

  if (s_lcd_io_ready != 0U)
  {
    return;
  }

  s_lcd_io_ready = 1U;

  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  gpio.Pin = CSX_Pin;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(CSX_GPIO_Port, &gpio);

  gpio.Pin = WRX_DCX_Pin | RDX_Pin;
  HAL_GPIO_Init(GPIOD, &gpio);

  HAL_GPIO_WritePin(CSX_GPIO_Port, CSX_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(GPIOD, WRX_DCX_Pin | RDX_Pin, GPIO_PIN_SET);
}

void LCD_IO_WriteData(uint16_t reg_value)
{
  HAL_GPIO_WritePin(GPIOD, WRX_DCX_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(CSX_GPIO_Port, CSX_Pin, GPIO_PIN_RESET);
  lcd_io_write_byte((uint8_t)reg_value);
  HAL_GPIO_WritePin(CSX_GPIO_Port, CSX_Pin, GPIO_PIN_SET);
}

void LCD_IO_WriteReg(uint8_t reg)
{
  HAL_GPIO_WritePin(GPIOD, WRX_DCX_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(CSX_GPIO_Port, CSX_Pin, GPIO_PIN_RESET);
  lcd_io_write_byte(reg);
  HAL_GPIO_WritePin(CSX_GPIO_Port, CSX_Pin, GPIO_PIN_SET);
}

uint32_t LCD_IO_ReadData(uint16_t reg_value, uint8_t read_size)
{
  (void)reg_value;
  (void)read_size;
  return 0U;
}

void LCD_Delay(uint32_t delay)
{
  HAL_Delay(delay);
}
