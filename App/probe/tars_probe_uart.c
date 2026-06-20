#include "tars_probe_uart.h"
#include "usart.h"
#include "main.h"
#include "cmsis_os.h"
#include <string.h>

#define PROBE_RX_RING_SIZE   256U
#define PROBE_TX_TIMEOUT_MS  2000U

DMA_HandleTypeDef hdma_usart1_tx;

static uint8_t s_rx_ring[PROBE_RX_RING_SIZE];
static volatile uint32_t s_rx_head;
static volatile uint32_t s_rx_tail;
static volatile uint8_t s_rx_byte;

static volatile uint8_t s_tx_done = 1U;

static void probe_uart_dma_init(void)
{
  __HAL_RCC_DMA2_CLK_ENABLE();

  hdma_usart1_tx.Instance = DMA2_Stream7;
  hdma_usart1_tx.Init.Channel = DMA_CHANNEL_4;
  hdma_usart1_tx.Init.Direction = DMA_MEMORY_TO_PERIPH;
  hdma_usart1_tx.Init.PeriphInc = DMA_PINC_DISABLE;
  hdma_usart1_tx.Init.MemInc = DMA_MINC_ENABLE;
  hdma_usart1_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
  hdma_usart1_tx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
  hdma_usart1_tx.Init.Mode = DMA_NORMAL;
  hdma_usart1_tx.Init.Priority = DMA_PRIORITY_LOW;
  hdma_usart1_tx.Init.FIFOMode = DMA_FIFOMODE_ENABLE;
  hdma_usart1_tx.Init.FIFOThreshold = DMA_FIFO_THRESHOLD_FULL;
  hdma_usart1_tx.Init.MemBurst = DMA_MBURST_SINGLE;
  hdma_usart1_tx.Init.PeriphBurst = DMA_PBURST_SINGLE;
  (void)HAL_DMA_Init(&hdma_usart1_tx);

  __HAL_LINKDMA(&huart1, hdmatx, hdma_usart1_tx);

  HAL_NVIC_SetPriority(DMA2_Stream7_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(DMA2_Stream7_IRQn);

  HAL_NVIC_SetPriority(USART1_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(USART1_IRQn);
}

void TarsProbeUart_Init(void)
{
  s_rx_head = 0U;
  s_rx_tail = 0U;
  s_tx_done = 1U;

  probe_uart_dma_init();

  (void)HAL_UART_Receive_IT(&huart1, (uint8_t *)&s_rx_byte, 1U);
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

  if (HAL_UART_Transmit_DMA(&huart1, (uint8_t *)data, (uint16_t)len) != HAL_OK)
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
  if (huart->Instance != USART1)
  {
    return;
  }

  uint32_t next = (s_rx_head + 1U) % PROBE_RX_RING_SIZE;
  if (next != s_rx_tail)
  {
    s_rx_ring[s_rx_head] = s_rx_byte;
    s_rx_head = next;
  }

  (void)HAL_UART_Receive_IT(&huart1, (uint8_t *)&s_rx_byte, 1U);
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART1)
  {
    s_tx_done = 1U;
  }
}
