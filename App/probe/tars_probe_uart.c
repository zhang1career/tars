#include "tars_probe_uart.h"
#include "main.h"
#include "cmsis_os.h"
#include <string.h>

/*
 * Probe serial transport.
 *
 * Moved off USART1 (PA9/PA10) to UART5 (PC12=TX, PD2=RX) so TIM1's high-side
 * channels CH2/CH3 can reclaim PA9/PA10 for the FOC bring-up. UART5 is on APB1
 * (36 MHz); TX uses DMA1_Stream7/Channel4, RX is byte-by-byte interrupt.
 *
 * The driver owns the whole UART5 peripheral (clock, GPIO, HAL init, DMA,
 * NVIC) so no CubeMX-managed file needs editing. PC12/PD2 are NOT on the
 * ST-LINK VCP, so connect a USB-TTL adapter to PC12 (TX) / PD2 (RX) / GND.
 */

#define PROBE_RX_RING_SIZE   256U
#define PROBE_TX_TIMEOUT_MS  2000U

UART_HandleTypeDef huart5;
DMA_HandleTypeDef hdma_uart5_tx;

static uint8_t s_rx_ring[PROBE_RX_RING_SIZE];
static volatile uint32_t s_rx_head;
static volatile uint32_t s_rx_tail;
static volatile uint8_t s_rx_byte;

static volatile uint8_t s_tx_done = 1U;

static void probe_uart_periph_init(void)
{
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_UART5_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  /* PC12 -> UART5_TX */
  gpio.Pin = GPIO_PIN_12;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_PULLUP;
  gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio.Alternate = GPIO_AF8_UART5;
  HAL_GPIO_Init(GPIOC, &gpio);

  /* PD2 -> UART5_RX */
  gpio.Pin = GPIO_PIN_2;
  HAL_GPIO_Init(GPIOD, &gpio);

  huart5.Instance = UART5;
  huart5.Init.BaudRate = 115200;
  huart5.Init.WordLength = UART_WORDLENGTH_8B;
  huart5.Init.StopBits = UART_STOPBITS_1;
  huart5.Init.Parity = UART_PARITY_NONE;
  huart5.Init.Mode = UART_MODE_TX_RX;
  huart5.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart5.Init.OverSampling = UART_OVERSAMPLING_16;
  (void)HAL_UART_Init(&huart5);
}

static void probe_uart_dma_init(void)
{
  __HAL_RCC_DMA1_CLK_ENABLE();

  hdma_uart5_tx.Instance = DMA1_Stream7;
  hdma_uart5_tx.Init.Channel = DMA_CHANNEL_4;
  hdma_uart5_tx.Init.Direction = DMA_MEMORY_TO_PERIPH;
  hdma_uart5_tx.Init.PeriphInc = DMA_PINC_DISABLE;
  hdma_uart5_tx.Init.MemInc = DMA_MINC_ENABLE;
  hdma_uart5_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
  hdma_uart5_tx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
  hdma_uart5_tx.Init.Mode = DMA_NORMAL;
  hdma_uart5_tx.Init.Priority = DMA_PRIORITY_LOW;
  hdma_uart5_tx.Init.FIFOMode = DMA_FIFOMODE_ENABLE;
  hdma_uart5_tx.Init.FIFOThreshold = DMA_FIFO_THRESHOLD_FULL;
  hdma_uart5_tx.Init.MemBurst = DMA_MBURST_SINGLE;
  hdma_uart5_tx.Init.PeriphBurst = DMA_PBURST_SINGLE;
  (void)HAL_DMA_Init(&hdma_uart5_tx);

  __HAL_LINKDMA(&huart5, hdmatx, hdma_uart5_tx);

  HAL_NVIC_SetPriority(DMA1_Stream7_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream7_IRQn);

  HAL_NVIC_SetPriority(UART5_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(UART5_IRQn);
}

void TarsProbeUart_Init(void)
{
  s_rx_head = 0U;
  s_rx_tail = 0U;
  s_tx_done = 1U;

  probe_uart_periph_init();
  probe_uart_dma_init();

  (void)HAL_UART_Receive_IT(&huart5, (uint8_t *)&s_rx_byte, 1U);
}

int TarsProbeUart_Send(const uint8_t *data, uint32_t len)
{
  uint32_t waited = 0U;

  if ((data == NULL) || (len == 0U))
  {
    return 0;
  }

  /* Wait for any prior DMA TX to finish. */
  while ((s_tx_done == 0U) && (waited < PROBE_TX_TIMEOUT_MS))
  {
    osDelay(1);
    waited++;
  }

  s_tx_done = 0U;

  if (HAL_UART_Transmit_DMA(&huart5, (uint8_t *)data, (uint16_t)len) != HAL_OK)
  {
    s_tx_done = 1U;
    return -1;
  }

  waited = 0U;
  while ((s_tx_done == 0U) && (waited < PROBE_TX_TIMEOUT_MS))
  {
    osDelay(1);
    waited++;
  }

  return (s_tx_done != 0U) ? (int)len : -1;
}

int TarsProbeUart_SendStr(const char *s)
{
  if (s == NULL)
  {
    return 0;
  }

  return TarsProbeUart_Send((const uint8_t *)s, (uint32_t)strlen(s));
}

int TarsProbeUart_GetByte(uint8_t *out)
{
  if (out == NULL || s_rx_head == s_rx_tail)
  {
    return 0;
  }

  *out = s_rx_ring[s_rx_tail];
  s_rx_tail = (s_rx_tail + 1U) % PROBE_RX_RING_SIZE;
  return 1;
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance != UART5)
  {
    return;
  }

  uint32_t next = (s_rx_head + 1U) % PROBE_RX_RING_SIZE;
  if (next != s_rx_tail)
  {
    s_rx_ring[s_rx_head] = s_rx_byte;
    s_rx_head = next;
  }

  (void)HAL_UART_Receive_IT(&huart5, (uint8_t *)&s_rx_byte, 1U);
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == UART5)
  {
    s_tx_done = 1U;
  }
}
