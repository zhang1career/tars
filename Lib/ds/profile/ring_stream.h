#ifndef RING_STREAM_H
#define RING_STREAM_H

#include <stdint.h>

/* Byte-stream ring buffer (producer / consumer). */

typedef struct {
  uint8_t *buf;
  uint32_t cap;
  volatile uint32_t head;
  volatile uint32_t tail;
} ring_stream_t;

void ring_stream_init(ring_stream_t *rs, uint8_t *buf, uint32_t cap);
int ring_stream_push(ring_stream_t *rs, uint8_t byte);
int ring_stream_pop(ring_stream_t *rs, uint8_t *byte);
uint32_t ring_stream_push_buf(ring_stream_t *rs, const uint8_t *data, uint32_t len);

#endif /* RING_STREAM_H */
