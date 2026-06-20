#include "tars_app.h"
#include "lcd_log.h"
#include "lcd_viewport.h"

static void viewport_init(const tars_api_t *api)
{
  (void)api;
}

static void viewport_tick(void)
{
  LcdViewport_PollInput();

  if (LcdViewport_GetPage() == LCD_VIEWPORT_PAGE_MOTOR)
  {
    LcdViewport_DrawCurrentPage();
  }
  else
  {
    LcdLog_SetStatusBanner("page:LOG");
  }
}

static void viewport_release(void)
{
}

const tars_builtin_app_t g_app_viewport = {
  .name = "viewport",
  .timeslice = 2U,
  .manifest = { .count = 0U },
  .init = viewport_init,
  .tick = viewport_tick,
  .release = viewport_release
};
