#include "tars_sys.h"
#include "tars_lua.h"
#include "tars_platform.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <string.h>

#define TARS_SYS_TOP_MAX_TASKS  12U

void TarsSys_FormatTop(char *out, uint32_t out_size)
{
  TaskStatus_t tasks[TARS_SYS_TOP_MAX_TASKS];
  UBaseType_t task_count;
  uint32_t lua_used = 0U;
  uint32_t lua_total = 0U;
  int written = 0;
  UBaseType_t i;

  if ((out == NULL) || (out_size == 0U))
  {
    return;
  }

  out[0] = '\0';
  TarsLua_GetHeapUsage(&lua_used, &lua_total);

  written += snprintf(out + written,
                      (written < (int)out_size) ? (out_size - (uint32_t)written) : 0U,
                      "sys top:\r\n"
                      "  rtos heap free=%lu min=%lu\r\n"
                      "  lua heap used=%lu/%lu\r\n",
                      (unsigned long)xPortGetFreeHeapSize(),
                      (unsigned long)xPortGetMinimumEverFreeHeapSize(),
                      (unsigned long)lua_used,
                      (unsigned long)lua_total);

  task_count = uxTaskGetSystemState(tasks, TARS_SYS_TOP_MAX_TASKS, NULL);
  written += snprintf(out + written,
                      (written < (int)out_size) ? (out_size - (uint32_t)written) : 0U,
                      "  tasks=%lu (showing %lu)\r\n",
                      (unsigned long)uxTaskGetNumberOfTasks(),
                      (unsigned long)task_count);

  for (i = 0U; i < task_count; i++)
  {
    const char *state = "?";

    switch (tasks[i].eCurrentState)
    {
    case eRunning:
      state = "run";
      break;
    case eReady:
      state = "rdy";
      break;
    case eBlocked:
      state = "blk";
      break;
    case eSuspended:
      state = "sus";
      break;
    case eDeleted:
      state = "del";
      break;
    default:
      break;
    }

    written += snprintf(out + written,
                        (written < (int)out_size) ? (out_size - (uint32_t)written) : 0U,
                        "  %-12s %3s pri=%2lu stk=%lu\r\n",
                        tasks[i].pcTaskName,
                        state,
                        (unsigned long)tasks[i].uxCurrentPriority,
                        (unsigned long)tasks[i].usStackHighWaterMark);
    if ((uint32_t)written >= out_size)
    {
      break;
    }
  }
}
