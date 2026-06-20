#include "tars_builtin_apps.h"
#include "tars_app.h"

extern const tars_builtin_app_t g_app_viewport;

void TarsBuiltin_RegisterAll(void)
{
  /* Motor control is now driven by the hardware resource task, not the
   * cooperative scheduler. Only UI (viewport) remains a scheduled app. */
  TarsApp_RegisterBuiltin(&g_app_viewport);
}
