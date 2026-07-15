#include "ring_index.h"
#include <stddef.h>

void ring_index_init(ring_index_t *idx, uint32_t cap)
{
  if (idx == NULL)
  {
    return;
  }

  idx->cap = cap;
  idx->head = 0U;
  idx->count = 0U;
}

uint32_t ring_index_next(const ring_index_t *idx, uint32_t i)
{
  if ((idx == NULL) || (idx->cap == 0U))
  {
    return 0U;
  }

  return (i + 1U) % idx->cap;
}

uint32_t ring_index_push(ring_index_t *idx)
{
  uint32_t slot;

  if ((idx == NULL) || (idx->cap == 0U))
  {
    return 0U;
  }

  slot = idx->head;
  idx->head = ring_index_next(idx, idx->head);
  if (idx->count < idx->cap)
  {
    idx->count++;
  }

  return slot;
}

uint32_t ring_index_slot_by_age(const ring_index_t *idx, uint32_t age)
{
  if ((idx == NULL) || (age >= idx->count) || (idx->cap == 0U))
  {
    return idx != NULL ? idx->cap : 0U;
  }

  return (idx->head + idx->cap - 1U - age) % idx->cap;
}
