#include "tars_builtin_apps.h"
#include "tars_app.h"
#include "tars_features.h"

#if TARS_FEATURE_LCD
extern const tars_builtin_app_t g_app_viewport;
#endif

void TarsBuiltin_RegisterAll(void)
{
  /* Motor control is driven by the hardware resource task, not the cooperative
   * scheduler. The viewport UI app only exists when the LCD is built in. */
#if TARS_FEATURE_LCD
  TarsApp_RegisterBuiltin(&g_app_viewport);
#else
  (void)0;
#endif
}
