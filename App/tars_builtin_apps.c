#include "tars_builtin_apps.h"
#include "tars_app.h"

extern const tars_builtin_app_t g_app_motor_ctl;
extern const tars_builtin_app_t g_app_viewport;

void TarsBuiltin_RegisterAll(void)
{
  TarsApp_RegisterBuiltin(&g_app_motor_ctl);
  TarsApp_RegisterBuiltin(&g_app_viewport);
}
