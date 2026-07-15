#ifndef TARS_PROBE_UART_H
#define TARS_PROBE_UART_H

#include <stdint.h>

/* Probe transport over USART1 (PA9/PA10 = ST-Link VCP), 115200 8N1.
 * TX uses DMA2 Stream7; RX is interrupt-driven byte-by-byte into a ring. */

void TarsProbeUart_Init(void);

/* Queue/transmit bytes. Blocks (yielding) until the DMA TX completes. */
int TarsProbeUart_Send(const uint8_t *data, uint32_t len);

/* Convenience: NUL-terminated string. */
int TarsProbeUart_SendStr(const char *s);

/* Pull one received byte from the RX ring. Returns 1 if a byte was read. */
int TarsProbeUart_GetByte(uint8_t *out);

#endif /* TARS_PROBE_UART_H */
