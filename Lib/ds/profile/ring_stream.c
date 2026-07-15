#include "ring_stream.h"
#include <stddef.h>

void ring_stream_init(ring_stream_t *rs, uint8_t *buf, uint32_t cap)
{
  if (rs == NULL)
  {
    return;
  }

  rs->buf = buf;
  rs->cap = cap;
  rs->head = 0U;
  rs->tail = 0U;
}

int ring_stream_push(ring_stream_t *rs, uint8_t byte)
{
  uint32_t next;

  if ((rs == NULL) || (rs->buf == NULL) || (rs->cap == 0U))
  {
    return -1;
  }

  next = (rs->head + 1U) % rs->cap;
  if (next == rs->tail)
  {
    return -1;
  }

  rs->buf[rs->head] = byte;
  rs->head = next;
  return 0;
}

int ring_stream_pop(ring_stream_t *rs, uint8_t *byte)
{
  if ((rs == NULL) || (rs->buf == NULL) || (byte == NULL))
  {
    return -1;
  }

  if (rs->head == rs->tail)
  {
    return -1;
  }

  *byte = rs->buf[rs->tail];
  rs->tail = (rs->tail + 1U) % rs->cap;
  return 0;
}

uint32_t ring_stream_push_buf(ring_stream_t *rs, const uint8_t *data, uint32_t len)
{
  uint32_t i;
  uint32_t pushed = 0U;

  if ((rs == NULL) || (data == NULL))
  {
    return 0U;
  }

  for (i = 0U; i < len; i++)
  {
    if (ring_stream_push(rs, data[i]) != 0)
    {
      break;
    }

    pushed++;
  }

  return pushed;
}
