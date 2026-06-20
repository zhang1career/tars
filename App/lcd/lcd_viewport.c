#include "lcd_viewport.h"
#include "lcd_log.h"
#include "tars_motor.h"
#include "main.h"
#include <stdio.h>
#include <string.h>

static lcd_viewport_page_t s_page = LCD_VIEWPORT_PAGE_LOG;
static uint8_t s_btn_prev_pressed;

static const char *motor_state_label(uint8_t state)
{
  switch (state)
  {
  case TARS_MOTOR_STATE_IDLE:
    return "IDLE";
  case TARS_MOTOR_STATE_RUN:
    return "RUN";
  case TARS_MOTOR_STATE_FAULT:
    return "FAULT";
  default:
    return "?";
  }
}

static void viewport_poll_button(void)
{
  GPIO_PinState pressed = HAL_GPIO_ReadPin(B1_GPIO_Port, B1_Pin);

  if ((pressed == GPIO_PIN_SET) && (s_btn_prev_pressed == 0U))
  {
    LcdViewport_NextPage();
  }

  s_btn_prev_pressed = (pressed == GPIO_PIN_SET) ? 1U : 0U;
}

void LcdViewport_Init(void)
{
  s_page = LCD_VIEWPORT_PAGE_LOG;
  s_btn_prev_pressed = 0U;
}

lcd_viewport_page_t LcdViewport_GetPage(void)
{
  return s_page;
}

int LcdViewport_IsLogPageActive(void)
{
  return (s_page == LCD_VIEWPORT_PAGE_LOG) ? 1 : 0;
}

void LcdViewport_SetPage(lcd_viewport_page_t page)
{
  if (page > LCD_VIEWPORT_PAGE_MOTOR)
  {
    return;
  }

  if (s_page == page)
  {
    return;
  }

  s_page = page;
  LcdViewport_DrawCurrentPage();
}

void LcdViewport_NextPage(void)
{
  lcd_viewport_page_t next = (lcd_viewport_page_t)((s_page + 1U) % 2U);
  LcdViewport_SetPage(next);
}

static void viewport_draw_motor_page(void)
{
  tars_motor_snapshot_t snap;
  char line[48];
  uint16_t y;
  const uint16_t top = LcdLog_GetLogTop();
  const uint16_t row_h = 12U;

  TarsMotor_GetSnapshot(&snap);

  LcdLog_Lock();
  LcdLog_FillRect(0U, top, LcdLog_GetWidth(), (uint16_t)(LcdLog_GetHeight() - top), LcdLog_GetBgColor());
  LcdLog_DrawString(4U, top + 2U, "Motor Control");
  y = (uint16_t)(top + row_h + 4U);

  (void)snprintf(line, sizeof(line), "state: %s", motor_state_label(snap.state));
  LcdLog_DrawString(4U, y, line);
  y = (uint16_t)(y + row_h);

  (void)snprintf(line, sizeof(line), "pos:   %+.1f deg", (double)snap.position_deg);
  LcdLog_DrawString(4U, y, line);
  y = (uint16_t)(y + row_h);

  (void)snprintf(line, sizeof(line), "vel:   %+.1f rpm", (double)snap.velocity_rpm);
  LcdLog_DrawString(4U, y, line);
  y = (uint16_t)(y + row_h);

  (void)snprintf(line, sizeof(line), "target:%+.1f deg", (double)snap.target_deg);
  LcdLog_DrawString(4U, y, line);
  y = (uint16_t)(y + row_h);

  (void)snprintf(line, sizeof(line), "error: %+.1f deg", (double)snap.error_deg);
  LcdLog_DrawString(4U, y, line);
  y = (uint16_t)(y + row_h);

  (void)snprintf(line, sizeof(line), "pwm:   %+.2f", (double)snap.pwm_duty);
  LcdLog_DrawString(4U, y, line);
  y = (uint16_t)(y + row_h);

  (void)snprintf(line, sizeof(line), "loop:  %lu", (unsigned long)snap.loop_count);
  LcdLog_DrawString(4U, y, line);
  y = (uint16_t)(y + row_h + 4U);

  LcdLog_DrawString(4U, y, "B1: switch page");
  LcdLog_Unlock();
}

void LcdViewport_DrawCurrentPage(void)
{
  char banner[40];

  if (s_page == LCD_VIEWPORT_PAGE_LOG)
  {
    (void)snprintf(banner, sizeof(banner), "page:LOG");
    LcdLog_SetStatusBanner(banner);
    LcdLog_RedrawLogRegion();
    return;
  }

  (void)snprintf(banner, sizeof(banner), "page:MOTOR");
  LcdLog_SetStatusBanner(banner);
  viewport_draw_motor_page();
}

void LcdViewport_PollInput(void)
{
  viewport_poll_button();
}
