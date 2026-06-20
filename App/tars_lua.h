#ifndef TARS_LUA_H
#define TARS_LUA_H

#include <stdint.h>
#include "tars_app.h"

void TarsLua_Init(void);
void TarsLua_Task(void const *argument);
void TarsLua_RequestRun(const char *name);
tars_status_t TarsLua_RunScript(const char *name, const char *source, uint32_t length);
tars_status_t TarsLua_RunApp(const char *name);
void TarsLua_OnAcquire(const char *app_name);
void TarsLua_OnRelease(const char *app_name);
void TarsLua_Reset(const char *name);
void TarsLua_GetHeapUsage(uint32_t *used_out, uint32_t *total_out);

#endif /* TARS_LUA_H */
