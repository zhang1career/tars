#ifndef RING_SLOTS_H
#define RING_SLOTS_H

#include <stdint.h>
#include "ring_index.h"

/* Fixed-size record ring (e.g. shell command history). */

typedef struct {
  ring_index_t idx;
  char *slots;
  uint32_t slot_size;
  int32_t browse;
} ring_slots_t;

void ring_slots_init(ring_slots_t *rs, char *slots, uint32_t slot_count, uint32_t slot_size);
void ring_slots_push(ring_slots_t *rs, const char *text);
const char *ring_slots_get(const ring_slots_t *rs, uint32_t age);
void ring_slots_reset_browse(ring_slots_t *rs);
int ring_slots_browse_prev(ring_slots_t *rs);
int ring_slots_browse_next(ring_slots_t *rs);
const char *ring_slots_browse_line(const ring_slots_t *rs);

#endif /* RING_SLOTS_H */
