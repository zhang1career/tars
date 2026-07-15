#include "shell_hist.h"
#include "ring_slots.h"
#include "tars_platform.h"
#include <string.h>

static ring_slots_t s_hist;

void ShellHist_Init(void)
{
  ring_slots_init(&s_hist,
                  (char *)(uintptr_t)TARS_SHELL_HIST_BASE,
                  TARS_SHELL_HIST_SLOTS,
                  TARS_SHELL_HIST_SLOT_SIZE);
}

void ShellHist_Push(const char *line)
{
  const char *newest;

  if ((line == NULL) || (line[0] == '\0'))
  {
    return;
  }

  newest = ring_slots_get(&s_hist, 0U);
  if ((newest != NULL) && (strcmp(newest, line) == 0))
  {
    ring_slots_reset_browse(&s_hist);
    return;
  }

  ring_slots_push(&s_hist, line);
}

void ShellHist_ResetBrowse(void)
{
  ring_slots_reset_browse(&s_hist);
}

int ShellHist_Prev(const char **line)
{
  if (line == NULL)
  {
    return -1;
  }

  if (ring_slots_browse_prev(&s_hist) != 0)
  {
    return -1;
  }

  *line = ring_slots_browse_line(&s_hist);
  return 0;
}

int ShellHist_Next(const char **line)
{
  if (line == NULL)
  {
    return -1;
  }

  if (ring_slots_browse_next(&s_hist) != 0)
  {
    return -1;
  }

  *line = ring_slots_browse_line(&s_hist);
  return 0;
}

uint32_t ShellHist_Count(void)
{
  return s_hist.idx.count;
}

const char *ShellHist_Entry(uint32_t age)
{
  return ring_slots_get(&s_hist, age);
}
