#ifndef RING_INDEX_H
#define RING_INDEX_H

#include <stdint.h>

/* Circular index state shared by ring_stream and ring_slots. */

typedef struct {
  uint32_t cap;
  uint32_t head;
  uint32_t count;
} ring_index_t;

void ring_index_init(ring_index_t *idx, uint32_t cap);
uint32_t ring_index_next(const ring_index_t *idx, uint32_t i);
uint32_t ring_index_push(ring_index_t *idx);
uint32_t ring_index_slot_by_age(const ring_index_t *idx, uint32_t age);

#endif /* RING_INDEX_H */
