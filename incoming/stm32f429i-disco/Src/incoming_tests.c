#include "incoming.h"
#include "ili9341.h"
#include "spi.h"

#include <string.h>

#define SDRAM_BASE          0xD0000000UL
#define SDRAM_SIZE          (8UL * 1024UL * 1024UL)
#define SDRAM_TIMEOUT       0xFFFFU

#define GYRO_WHOAMI_I3G4250D  0xD3U
#define GYRO_WHOAMI_L3GD20    0xD4U

#define VDD_PASS_MIN_MV  3100U
#define VDD_PASS_MAX_MV  3600U
#define VDD_WARN_MIN_MV  2800U
#define VDD_WARN_MAX_MV  3800U

SPI_HandleTypeDef hspi5;
static I2C_HandleTypeDef hi2c3;
static SDRAM_HandleTypeDef hsdram1;
static LTDC_HandleTypeDef hltdc;

static uint32_t sram_scratch[256];

static uint32_t xorshift32(uint32_t *state)
{
  uint32_t x = *state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  *state = x;
  return x;
}

static void mpu_sdram_normal(void)
{
  MPU_Region_InitTypeDef mpu = {0};

  HAL_MPU_Disable();
  mpu.Enable = MPU_REGION_ENABLE;
  mpu.Number = MPU_REGION_NUMBER0;
  mpu.BaseAddress = SDRAM_BASE;
  mpu.Size = MPU_REGION_SIZE_8MB;
  mpu.AccessPermission = MPU_REGION_FULL_ACCESS;
  mpu.IsBufferable = MPU_ACCESS_BUFFERABLE;
  mpu.IsCacheable = MPU_ACCESS_CACHEABLE;
  mpu.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
  mpu.TypeExtField = MPU_TEX_LEVEL1;
  mpu.SubRegionDisable = 0x00;
  mpu.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  HAL_MPU_ConfigRegion(&mpu);
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

static int mem_walk32(volatile uint32_t *base, uint32_t words)
{
  uint32_t i;
  uint32_t bit;

  for (i = 0; i < words; i++) {
    base[i] = 0U;
    if (base[i] != 0U) {
      return -1;
    }
  }
  for (bit = 1U; bit != 0U; bit <<= 1) {
    for (i = 0; i < words; i++) {
      base[i] = bit;
      if (base[i] != bit) {
        return -1;
      }
    }
  }
  return 0;
}

static void test_identity(incoming_report_t *r)
{
  r->idcode = DBGMCU->IDCODE;
  r->flash_kb = *(volatile uint16_t *)0x1FFF7A22U;
  r->uid[0] = *(volatile uint32_t *)0x1FFF7A10U;
  r->uid[1] = *(volatile uint32_t *)0x1FFF7A14U;
  r->uid[2] = *(volatile uint32_t *)0x1FFF7A18U;

  if ((r->idcode & 0xFFFU) == 0x419U) {
    Incoming_SetPass(r, INCOMING_MCU_ID);
  } else {
    Incoming_SetFail(r, INCOMING_MCU_ID);
  }

  if (r->flash_kb == 2048U) {
    Incoming_SetPass(r, INCOMING_FLASH_SIZE);
  } else if (r->flash_kb >= 512U) {
    Incoming_SetWarn(r, INCOMING_FLASH_SIZE);
  } else {
    Incoming_SetFail(r, INCOMING_FLASH_SIZE);
  }
}

static void test_internal_ram(incoming_report_t *r)
{
  if (mem_walk32(sram_scratch, 256U) == 0) {
    Incoming_SetPass(r, INCOMING_SRAM);
  } else {
    Incoming_SetFail(r, INCOMING_SRAM);
  }

  if (mem_walk32((volatile uint32_t *)0x10000000UL, 256U) == 0) {
    Incoming_SetPass(r, INCOMING_CCM);
  } else {
    Incoming_SetFail(r, INCOMING_CCM);
  }
}

static void test_vdd(incoming_report_t *r)
{
  uint16_t cal = *(volatile uint16_t *)0x1FFF7A2AU;
  uint16_t raw;

  __HAL_RCC_ADC1_CLK_ENABLE();
  ADC->CCR |= ADC_CCR_TSVREFE;
  ADC1->CR2 = ADC_CR2_ADON;
  ADC1->SQR3 = 17U;
  ADC1->SMPR1 = (7U << 21);
  HAL_Delay(1);
  ADC1->CR2 |= ADC_CR2_SWSTART;
  while ((ADC1->SR & ADC_SR_EOC) == 0U) {
  }
  raw = (uint16_t)ADC1->DR;

  r->vrefint_cal = cal;
  r->vrefint_raw = raw;
  if ((cal != 0U) && (raw != 0U)) {
    r->vdd_mv = (3300U * (uint32_t)cal) / (uint32_t)raw;
  }

  if ((r->vdd_mv >= VDD_PASS_MIN_MV) && (r->vdd_mv <= VDD_PASS_MAX_MV)) {
    Incoming_SetPass(r, INCOMING_VDD);
  } else if ((r->vdd_mv >= VDD_WARN_MIN_MV) && (r->vdd_mv <= VDD_WARN_MAX_MV)) {
    Incoming_SetWarn(r, INCOMING_VDD);
  } else {
    Incoming_SetFail(r, INCOMING_VDD);
  }
}

static void fmc_gpio(void)
{
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();
  __HAL_RCC_FMC_CLK_ENABLE();

  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio.Alternate = GPIO_AF12_FMC;

  gpio.Pin = A0_Pin | A1_Pin | A2_Pin | A3_Pin | A4_Pin | A5_Pin |
             SDNRAS_Pin | A6_Pin | A7_Pin | A8_Pin | A9_Pin;
  HAL_GPIO_Init(GPIOF, &gpio);

  gpio.Pin = SDNWE_Pin;
  HAL_GPIO_Init(SDNWE_GPIO_Port, &gpio);

  gpio.Pin = A10_Pin | A11_Pin | BA0_Pin | BA1_Pin | SDCLK_Pin | SDNCAS_Pin;
  HAL_GPIO_Init(GPIOG, &gpio);

  gpio.Pin = D4_Pin | D5_Pin | D6_Pin | D7_Pin | D8_Pin | D9_Pin |
             D10_Pin | D11_Pin | D12_Pin | NBL0_Pin | NBL1_Pin;
  HAL_GPIO_Init(GPIOE, &gpio);

  gpio.Pin = D13_Pin | D14_Pin | D15_Pin | D0_Pin | D1_Pin | D2_Pin | D3_Pin;
  HAL_GPIO_Init(GPIOD, &gpio);

  gpio.Pin = SDCKE1_Pin | SDNE1_Pin;
  HAL_GPIO_Init(GPIOB, &gpio);
}

static int sdram_init(void)
{
  FMC_SDRAM_TimingTypeDef timing = {0};
  FMC_SDRAM_CommandTypeDef cmd = {0};
  uint32_t sdclk = HAL_RCC_GetHCLKFreq() / 2U;
  uint32_t refresh = ((64UL * sdclk) / 8192UL) - 20UL;

  fmc_gpio();

  hsdram1.Instance = FMC_SDRAM_DEVICE;
  hsdram1.Init.SDBank = FMC_SDRAM_BANK2;
  hsdram1.Init.ColumnBitsNumber = FMC_SDRAM_COLUMN_BITS_NUM_8;
  hsdram1.Init.RowBitsNumber = FMC_SDRAM_ROW_BITS_NUM_12;
  hsdram1.Init.MemoryDataWidth = FMC_SDRAM_MEM_BUS_WIDTH_16;
  hsdram1.Init.InternalBankNumber = FMC_SDRAM_INTERN_BANKS_NUM_4;
  hsdram1.Init.CASLatency = FMC_SDRAM_CAS_LATENCY_3;
  hsdram1.Init.WriteProtection = FMC_SDRAM_WRITE_PROTECTION_DISABLE;
  hsdram1.Init.SDClockPeriod = FMC_SDRAM_CLOCK_PERIOD_2;
  hsdram1.Init.ReadBurst = FMC_SDRAM_RBURST_DISABLE;
  hsdram1.Init.ReadPipeDelay = FMC_SDRAM_RPIPE_DELAY_1;

  timing.LoadToActiveDelay = 2;
  timing.ExitSelfRefreshDelay = 7;
  timing.SelfRefreshTime = 4;
  timing.RowCycleDelay = 7;
  timing.WriteRecoveryTime = 3;
  timing.RPDelay = 2;
  timing.RCDDelay = 2;

  if (HAL_SDRAM_Init(&hsdram1, &timing) != HAL_OK) {
    return -1;
  }

  cmd.CommandTarget = FMC_SDRAM_CMD_TARGET_BANK2;
  cmd.AutoRefreshNumber = 1U;
  cmd.ModeRegisterDefinition = 0U;

  cmd.CommandMode = FMC_SDRAM_CMD_CLK_ENABLE;
  if (HAL_SDRAM_SendCommand(&hsdram1, &cmd, SDRAM_TIMEOUT) != HAL_OK) {
    return -1;
  }
  HAL_Delay(1);

  cmd.CommandMode = FMC_SDRAM_CMD_PALL;
  if (HAL_SDRAM_SendCommand(&hsdram1, &cmd, SDRAM_TIMEOUT) != HAL_OK) {
    return -1;
  }

  cmd.CommandMode = FMC_SDRAM_CMD_AUTOREFRESH_MODE;
  cmd.AutoRefreshNumber = 4U;
  if (HAL_SDRAM_SendCommand(&hsdram1, &cmd, SDRAM_TIMEOUT) != HAL_OK) {
    return -1;
  }

  cmd.CommandMode = FMC_SDRAM_CMD_LOAD_MODE;
  cmd.AutoRefreshNumber = 1U;
  cmd.ModeRegisterDefinition = 0x0230U; /* BL1, seq, CAS3, single write */
  if (HAL_SDRAM_SendCommand(&hsdram1, &cmd, SDRAM_TIMEOUT) != HAL_OK) {
    return -1;
  }

  if (HAL_SDRAM_ProgramRefreshRate(&hsdram1, refresh) != HAL_OK) {
    return -1;
  }
  return 0;
}

static int sdram_record_fail(incoming_report_t *r, uint32_t addr,
                             uint32_t expect, uint32_t got)
{
  r->sdram_fail_addr = addr;
  r->sdram_expect = expect;
  r->sdram_got = got;
  return -1;
}

static int test_sdram_patterns(incoming_report_t *r)
{
  volatile uint16_t *mem16 = (volatile uint16_t *)SDRAM_BASE;
  volatile uint32_t *mem32 = (volatile uint32_t *)SDRAM_BASE;
  uint32_t seed = 0xA5A5F00DU;
  uint32_t i;
  uint16_t bit;

  /* Data-bus walk at three addresses (start / mid / end). */
  for (i = 0; i < 3U; i++) {
    uint32_t off = (i == 0U) ? 0U : (i == 1U) ? (SDRAM_SIZE / 4U) : ((SDRAM_SIZE / 2U) - 2U);
    for (bit = 1U; bit != 0U; bit <<= 1) {
      mem16[off / 2U] = bit;
      if (mem16[off / 2U] != bit) {
        return sdram_record_fail(r, SDRAM_BASE + off, bit, mem16[off / 2U]);
      }
    }
  }

  /* Address uniqueness: store the address in each 1 KiB cell. */
  for (i = 0; i < SDRAM_SIZE; i += 1024U) {
    mem32[i / 4U] = SDRAM_BASE + i;
  }
  for (i = 0; i < SDRAM_SIZE; i += 1024U) {
    uint32_t expect = SDRAM_BASE + i;
    uint32_t got = mem32[i / 4U];
    if (got != expect) {
      return sdram_record_fail(r, expect, expect, got);
    }
  }

  /* PRNG over the first and last 64 KiB. */
  seed = 0xC0FFEEu;
  for (i = 0; i < (64U * 1024U); i += 4U) {
    mem32[i / 4U] = xorshift32(&seed);
  }
  seed = 0xC0FFEEu;
  for (i = 0; i < (64U * 1024U); i += 4U) {
    uint32_t expect = xorshift32(&seed);
    uint32_t got = mem32[i / 4U];
    if (got != expect) {
      return sdram_record_fail(r, SDRAM_BASE + i, expect, got);
    }
  }

  seed = 0xBEEFu;
  for (i = 0; i < (64U * 1024U); i += 4U) {
    uint32_t off = (SDRAM_SIZE - (64U * 1024U) + i) / 4U;
    mem32[off] = xorshift32(&seed);
  }
  seed = 0xBEEFu;
  for (i = 0; i < (64U * 1024U); i += 4U) {
    uint32_t off = (SDRAM_SIZE - (64U * 1024U) + i) / 4U;
    uint32_t expect = xorshift32(&seed);
    uint32_t got = mem32[off];
    if (got != expect) {
      return sdram_record_fail(r, SDRAM_BASE + off * 4U, expect, got);
    }
  }

  r->sdram_bytes = SDRAM_SIZE;
  return 0;
}

static void test_sdram(incoming_report_t *r)
{
  mpu_sdram_normal();
  if (sdram_init() != 0) {
    Incoming_SetFail(r, INCOMING_SDRAM);
    return;
  }
  if (test_sdram_patterns(r) == 0) {
    Incoming_SetPass(r, INCOMING_SDRAM);
  } else {
    Incoming_SetFail(r, INCOMING_SDRAM);
  }
}

static void spi5_init(void)
{
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_SPI5_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();

  gpio.Pin = SPI5_SCK_Pin | SPI5_MISO_Pin | SPI5_MOSI_Pin;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  gpio.Alternate = GPIO_AF5_SPI5;
  HAL_GPIO_Init(GPIOF, &gpio);

  hspi5.Instance = SPI5;
  hspi5.Init.Mode = SPI_MODE_MASTER;
  hspi5.Init.Direction = SPI_DIRECTION_2LINES;
  hspi5.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi5.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi5.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi5.Init.NSS = SPI_NSS_SOFT;
  hspi5.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_16;
  hspi5.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi5.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi5.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi5.Init.CRCPolynomial = 10;
  (void)HAL_SPI_Init(&hspi5);
}

static void test_gyro(incoming_report_t *r)
{
  uint8_t tx[2] = {0x8FU, 0x00U};
  uint8_t rx[2] = {0};

  HAL_GPIO_WritePin(CSX_GPIO_Port, CSX_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(NCS_MEMS_SPI_GPIO_Port, NCS_MEMS_SPI_Pin, GPIO_PIN_RESET);
  (void)HAL_SPI_TransmitReceive(&hspi5, tx, rx, 2U, 100U);
  HAL_GPIO_WritePin(NCS_MEMS_SPI_GPIO_Port, NCS_MEMS_SPI_Pin, GPIO_PIN_SET);

  r->gyro_whoami = rx[1];
  if ((rx[1] == GYRO_WHOAMI_I3G4250D) || (rx[1] == GYRO_WHOAMI_L3GD20)) {
    Incoming_SetPass(r, INCOMING_GYRO);
  } else {
    Incoming_SetFail(r, INCOMING_GYRO);
  }
}

static void i2c3_init(void)
{
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_I2C3_CLK_ENABLE();

  gpio.Mode = GPIO_MODE_AF_OD;
  gpio.Pull = GPIO_PULLUP;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  gpio.Alternate = GPIO_AF4_I2C3;

  gpio.Pin = I2C3_SDA_Pin;
  HAL_GPIO_Init(I2C3_SDA_GPIO_Port, &gpio);
  gpio.Pin = I2C3_SCL_Pin;
  HAL_GPIO_Init(I2C3_SCL_GPIO_Port, &gpio);

  hi2c3.Instance = I2C3;
  hi2c3.Init.ClockSpeed = 100000;
  hi2c3.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c3.Init.OwnAddress1 = 0;
  hi2c3.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c3.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c3.Init.OwnAddress2 = 0;
  hi2c3.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c3.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  (void)HAL_I2C_Init(&hi2c3);
}

static void test_touch(incoming_report_t *r)
{
  const uint8_t candidates[4] = {0x41U, 0x48U, 0x44U, 0x70U};
  uint8_t found = 0U;
  uint8_t i;

  for (i = 0; i < 4U; i++) {
    if (HAL_I2C_IsDeviceReady(&hi2c3, (uint16_t)(candidates[i] << 1), 2U, 20U) ==
        HAL_OK) {
      r->i2c_found[found] = candidates[i];
      if (found == 0U) {
        r->touch_addr7 = candidates[i];
      }
      found++;
    }
  }

  if (r->touch_addr7 == 0x41U) {
    uint8_t id[2] = {0};
    if (HAL_I2C_Mem_Read(&hi2c3, 0x82U, 0x00U, I2C_MEMADD_SIZE_8BIT, id, 2U,
                         50U) == HAL_OK) {
      r->touch_id = ((uint32_t)id[0] << 8) | id[1];
    }
  }

  if (found != 0U) {
    Incoming_SetPass(r, INCOMING_TOUCH);
  } else {
    Incoming_SetFail(r, INCOMING_TOUCH);
  }
}

static int ltdc_init(void)
{
  LTDC_LayerCfgTypeDef layer = {0};
  RCC_PeriphCLKInitTypeDef periph = {0};
  GPIO_InitTypeDef gpio = {0};

  periph.PeriphClockSelection = RCC_PERIPHCLK_LTDC;
  periph.PLLSAI.PLLSAIN = 96;
  periph.PLLSAI.PLLSAIR = 4;
  periph.PLLSAIDivR = RCC_PLLSAIDIVR_8;
  if (HAL_RCCEx_PeriphCLKConfig(&periph) != HAL_OK) {
    return -1;
  }

  __HAL_RCC_LTDC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();

  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  gpio.Alternate = GPIO_AF14_LTDC;

  gpio.Pin = ENABLE_Pin;
  HAL_GPIO_Init(ENABLE_GPIO_Port, &gpio);
  gpio.Pin = B5_Pin | VSYNC_Pin | G2_Pin | R4_Pin | R5_Pin;
  HAL_GPIO_Init(GPIOA, &gpio);
  gpio.Pin = R3_Pin | R6_Pin | G4_Pin | G5_Pin | B6_Pin | B7_Pin;
  gpio.Alternate = GPIO_AF14_LTDC;
  HAL_GPIO_Init(GPIOB, &gpio);
  /* PB0/PB1 are AF9 on this package for R3/R6. */
  gpio.Pin = R3_Pin | R6_Pin;
  gpio.Alternate = GPIO_AF9_LTDC;
  HAL_GPIO_Init(GPIOB, &gpio);
  gpio.Alternate = GPIO_AF14_LTDC;
  gpio.Pin = HSYNC_Pin | G6_Pin | R2_Pin;
  HAL_GPIO_Init(GPIOC, &gpio);
  gpio.Pin = G7_Pin | B2_Pin;
  HAL_GPIO_Init(GPIOD, &gpio);
  gpio.Pin = R7_Pin | DOTCLK_Pin | G3_Pin | B3_Pin | B4_Pin;
  HAL_GPIO_Init(GPIOG, &gpio);

  hltdc.Instance = LTDC;
  hltdc.Init.HSPolarity = LTDC_HSPOLARITY_AL;
  hltdc.Init.VSPolarity = LTDC_VSPOLARITY_AL;
  hltdc.Init.DEPolarity = LTDC_DEPOLARITY_AL;
  hltdc.Init.PCPolarity = LTDC_PCPOLARITY_IPC;
  hltdc.Init.HorizontalSync = 9;
  hltdc.Init.VerticalSync = 1;
  hltdc.Init.AccumulatedHBP = 29;
  hltdc.Init.AccumulatedVBP = 3;
  hltdc.Init.AccumulatedActiveW = 269;
  hltdc.Init.AccumulatedActiveH = 323;
  hltdc.Init.TotalWidth = 279;
  hltdc.Init.TotalHeigh = 327;
  hltdc.Init.Backcolor.Blue = 0;
  hltdc.Init.Backcolor.Green = 0;
  hltdc.Init.Backcolor.Red = 0;
  if (HAL_LTDC_Init(&hltdc) != HAL_OK) {
    return -1;
  }

  layer.WindowX0 = 0;
  layer.WindowX1 = 240;
  layer.WindowY0 = 0;
  layer.WindowY1 = 320;
  layer.PixelFormat = LTDC_PIXEL_FORMAT_RGB565;
  layer.Alpha = 255;
  layer.Alpha0 = 0;
  layer.BlendingFactor1 = LTDC_BLENDING_FACTOR1_PAxCA;
  layer.BlendingFactor2 = LTDC_BLENDING_FACTOR2_PAxCA;
  layer.FBStartAdress = SDRAM_BASE;
  layer.ImageWidth = 240;
  layer.ImageHeight = 320;
  return (HAL_LTDC_ConfigLayer(&hltdc, &layer, 0) == HAL_OK) ? 0 : -1;
}

static void lcd_color_bars(void)
{
  static const uint16_t bars[8] = {
      0xF800U, 0x07E0U, 0x001FU, 0xFFE0U,
      0x07FFU, 0xF81FU, 0xFFFFU, 0x0000U};
  volatile uint16_t *fb = (volatile uint16_t *)SDRAM_BASE;
  uint32_t y;
  uint32_t x;

  for (y = 0; y < 320U; y++) {
    for (x = 0; x < 240U; x++) {
      fb[y * 240U + x] = bars[x / 30U];
    }
  }
}

static void test_lcd(incoming_report_t *r)
{
  uint16_t id;

  HAL_GPIO_WritePin(ACP_RST_GPIO_Port, ACP_RST_Pin, GPIO_PIN_RESET);
  HAL_Delay(5);
  HAL_GPIO_WritePin(ACP_RST_GPIO_Port, ACP_RST_Pin, GPIO_PIN_SET);
  HAL_Delay(10);

  ili9341_Init();
  id = ili9341_ReadID();
  r->lcd_id = id;

  if ((r->fail_mask & (1u << INCOMING_SDRAM)) != 0U) {
    Incoming_SetSkip(r, INCOMING_LCD);
    return;
  }

  if (ltdc_init() != 0) {
    Incoming_SetFail(r, INCOMING_LCD);
    return;
  }
  lcd_color_bars();

  if (id == ILI9341_ID) {
    Incoming_SetPass(r, INCOMING_LCD);
  } else {
    /* Command path + LTDC programmed; ID read is often write-only on this panel. */
    Incoming_SetWarn(r, INCOMING_LCD);
  }
}

void Incoming_RunTests(incoming_report_t *r)
{
  r->button = (HAL_GPIO_ReadPin(B1_GPIO_Port, B1_Pin) == GPIO_PIN_SET) ? 1U : 0U;

  test_identity(r);
  test_internal_ram(r);
  test_vdd(r);
  test_sdram(r);

  spi5_init();
  test_gyro(r);

  i2c3_init();
  test_touch(r);

  test_lcd(r);
}
