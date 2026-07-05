#include "ring_slots.h"
#include <string.h>

void ring_slots_init(ring_slots_t *rs, char *slots, uint32_t slot_count, uint32_t slot_size)
{
  if (rs == NULL)
  {
    return;
  }

  ring_index_init(&rs->idx, slot_count);
  rs->slots = slots;
  rs->slot_size = slot_size;
  rs->browse = -1;

  if ((slots != NULL) && (slot_count > 0U) && (slot_size > 0U))
  {
    (void)memset(slots, 0, (size_t)slot_count * (size_t)slot_size);
  }
}

static char *ring_slots_slot_ptr(ring_slots_t *rs, uint32_t slot)
{
  return rs->slots + (slot * rs->slot_size);
}

void ring_slots_push(ring_slots_t *rs, const char *text)
{
  uint32_t slot;
  char *dst;

  if ((rs == NULL) || (text == NULL) || (rs->slots == NULL) || (rs->slot_size == 0U))
  {
    return;
  }

  slot = ring_index_push(&rs->idx);
  dst = ring_slots_slot_ptr(rs, slot);
  (void)strncpy(dst, text, rs->slot_size - 1U);
  dst[rs->slot_size - 1U] = '\0';
  rs->browse = -1;
}

const char *ring_slots_get(const ring_slots_t *rs, uint32_t age)
{
  uint32_t slot;

  if (rs == NULL)
  {
    return NULL;
  }

  slot = ring_index_slot_by_age(&rs->idx, age);
  if (slot >= rs->idx.cap)
  {
    return NULL;
  }

  return rs->slots + (slot * rs->slot_size);
}

void ring_slots_reset_browse(ring_slots_t *rs)
{
  if (rs != NULL)
  {
    rs->browse = -1;
  }
}

int ring_slots_browse_prev(ring_slots_t *rs)
{
  if ((rs == NULL) || (rs->idx.count == 0U))
  {
    return -1;
  }

  if (rs->browse < 0)
  {
    rs->browse = 0;
    return 0;
  }

  if ((uint32_t)(rs->browse + 1) < rs->idx.count)
  {
    rs->browse++;
    return 0;
  }

  return -1;
}

int ring_slots_browse_next(ring_slots_t *rs)
{
  if ((rs == NULL) || (rs->browse < 0))
  {
    return -1;
  }

  if (rs->browse == 0)
  {
    rs->browse = -1;
    return 0;
  }

  rs->browse--;
  return 0;
}

const char *ring_slots_browse_line(const ring_slots_t *rs)
{
  if ((rs == NULL) || (rs->browse < 0))
  {
    return NULL;
  }

  return ring_slots_get(rs, (uint32_t)rs->browse);
}
