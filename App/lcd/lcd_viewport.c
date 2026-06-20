#include "lcd_viewport.h"
#include "lcd_log.h"
#include "lcd_text.h"
#include "tars_motor.h"
#include "main.h"
#include <stdio.h>
#include <string.h>

#define MOTOR_TEXT_ROW_COUNT  9U
#define MOTOR_ROW_H           12U

static const lcd_viewport_page_kind_t s_page_kinds[LCD_VIEWPORT_PAGE_COUNT] = {
  LCD_VIEWPORT_PAGE_KIND_TEXT,
  LCD_VIEWPORT_PAGE_KIND_TEXT
};

static lcd_viewport_page_t s_page = LCD_VIEWPORT_PAGE_LOG;
static uint8_t s_btn_prev_pressed;

static lcd_text_row_cache_t s_motor_cache[MOTOR_TEXT_ROW_COUNT];
static char s_motor_line_buf[MOTOR_TEXT_ROW_COUNT][LCD_TEXT_COLS + 1U];

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

static void viewport_build_motor_rows(const tars_motor_snapshot_t *snap, lcd_text_row_t *rows)
{
  const uint16_t top = LcdLog_GetLogTop();
  uint16_t y = (uint16_t)(top + 2U);

  (void)snprintf(s_motor_line_buf[0], sizeof(s_motor_line_buf[0]), "Motor Control");
  rows[0].x = 4U;
  rows[0].y = y;
  rows[0].row_h = MOTOR_ROW_H;
  rows[0].text = s_motor_line_buf[0];
  y = (uint16_t)(y + MOTOR_ROW_H + 4U);

  (void)snprintf(s_motor_line_buf[1], sizeof(s_motor_line_buf[1]), "state: %s", motor_state_label(snap->state));
  rows[1].x = 4U;
  rows[1].y = y;
  rows[1].row_h = MOTOR_ROW_H;
  rows[1].text = s_motor_line_buf[1];
  y = (uint16_t)(y + MOTOR_ROW_H);

  (void)snprintf(s_motor_line_buf[2], sizeof(s_motor_line_buf[2]), "pos:   %+.1f deg", (double)snap->position_deg);
  rows[2].x = 4U;
  rows[2].y = y;
  rows[2].row_h = MOTOR_ROW_H;
  rows[2].text = s_motor_line_buf[2];
  y = (uint16_t)(y + MOTOR_ROW_H);

  (void)snprintf(s_motor_line_buf[3], sizeof(s_motor_line_buf[3]), "vel:   %+.1f rpm", (double)snap->velocity_rpm);
  rows[3].x = 4U;
  rows[3].y = y;
  rows[3].row_h = MOTOR_ROW_H;
  rows[3].text = s_motor_line_buf[3];
  y = (uint16_t)(y + MOTOR_ROW_H);

  (void)snprintf(s_motor_line_buf[4], sizeof(s_motor_line_buf[4]), "target:%+.1f deg", (double)snap->target_deg);
  rows[4].x = 4U;
  rows[4].y = y;
  rows[4].row_h = MOTOR_ROW_H;
  rows[4].text = s_motor_line_buf[4];
  y = (uint16_t)(y + MOTOR_ROW_H);

  (void)snprintf(s_motor_line_buf[5], sizeof(s_motor_line_buf[5]), "error: %+.1f deg", (double)snap->error_deg);
  rows[5].x = 4U;
  rows[5].y = y;
  rows[5].row_h = MOTOR_ROW_H;
  rows[5].text = s_motor_line_buf[5];
  y = (uint16_t)(y + MOTOR_ROW_H);

  (void)snprintf(s_motor_line_buf[6], sizeof(s_motor_line_buf[6]), "pwm:   %+.2f", (double)snap->pwm_duty);
  rows[6].x = 4U;
  rows[6].y = y;
  rows[6].row_h = MOTOR_ROW_H;
  rows[6].text = s_motor_line_buf[6];
  y = (uint16_t)(y + MOTOR_ROW_H);

  (void)snprintf(s_motor_line_buf[7], sizeof(s_motor_line_buf[7]), "loop:  %lu", (unsigned long)snap->loop_count);
  rows[7].x = 4U;
  rows[7].y = y;
  rows[7].row_h = MOTOR_ROW_H;
  rows[7].text = s_motor_line_buf[7];
  y = (uint16_t)(y + MOTOR_ROW_H + 4U);

  (void)snprintf(s_motor_line_buf[8], sizeof(s_motor_line_buf[8]), "B1: switch page");
  rows[8].x = 4U;
  rows[8].y = y;
  rows[8].row_h = MOTOR_ROW_H;
  rows[8].text = s_motor_line_buf[8];
}

static void viewport_paint_motor_page(const tars_motor_snapshot_t *snap)
{
  lcd_text_row_t rows[MOTOR_TEXT_ROW_COUNT];

  viewport_build_motor_rows(snap, rows);
  LcdLog_Lock();
  LcdLog_SetStatusBannerText("page:MOTOR");
  LcdLog_BeginComposeFrame(0);
  LcdLog_DrawHeader();
  LcdLog_FillContentArea();
  LcdText_DrawRowsUnlocked(rows, s_motor_cache, MOTOR_TEXT_ROW_COUNT);
  LcdLog_EndComposeFrame();
  LcdLog_Unlock();
}

static void viewport_update_motor_page(void)
{
  tars_motor_snapshot_t snap;
  lcd_text_row_t rows[MOTOR_TEXT_ROW_COUNT];
  uint16_t i;
  int any_change = 0;

  TarsMotor_GetSnapshot(&snap);
  viewport_build_motor_rows(&snap, rows);

  for (i = 0U; i < MOTOR_TEXT_ROW_COUNT; i++)
  {
    if (strcmp(rows[i].text, s_motor_cache[i].text) != 0)
    {
      any_change = 1;
      break;
    }
  }

  if (any_change == 0)
  {
    return;
  }

  LcdLog_Lock();
  LcdLog_BeginComposeFrame(0);
  LcdLog_FillContentArea();
  LcdText_DrawRowsUnlocked(rows, s_motor_cache, MOTOR_TEXT_ROW_COUNT);
  LcdLog_EndComposeFrame();
  LcdLog_Unlock();
}

void LcdViewport_Init(void)
{
  s_page = LCD_VIEWPORT_PAGE_LOG;
  s_btn_prev_pressed = 0U;
  LcdText_ResetCache(s_motor_cache, MOTOR_TEXT_ROW_COUNT);
}

lcd_viewport_page_t LcdViewport_GetPage(void)
{
  return s_page;
}

lcd_viewport_page_kind_t LcdViewport_GetPageKind(void)
{
  if (s_page >= LCD_VIEWPORT_PAGE_COUNT)
  {
    return LCD_VIEWPORT_PAGE_KIND_TEXT;
  }

  return s_page_kinds[s_page];
}

int LcdViewport_IsTextPageActive(void)
{
  return (LcdViewport_GetPageKind() == LCD_VIEWPORT_PAGE_KIND_TEXT) ? 1 : 0;
}

void LcdViewport_SetPage(lcd_viewport_page_t page)
{
  if (page >= LCD_VIEWPORT_PAGE_COUNT)
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
  lcd_viewport_page_t next = (lcd_viewport_page_t)((s_page + 1U) % LCD_VIEWPORT_PAGE_COUNT);
  LcdViewport_SetPage(next);
}

void LcdViewport_DrawCurrentPage(void)
{
  if (LcdViewport_GetPageKind() == LCD_VIEWPORT_PAGE_KIND_GRAPHICS)
  {
    return;
  }

  if (s_page == LCD_VIEWPORT_PAGE_LOG)
  {
    LcdLog_SetStatusBannerText("page:LOG");
    LcdLog_RedrawLogRegion();
    return;
  }

  if (s_page == LCD_VIEWPORT_PAGE_MOTOR)
  {
    tars_motor_snapshot_t snap;

    LcdText_ResetCache(s_motor_cache, MOTOR_TEXT_ROW_COUNT);
    TarsMotor_GetSnapshot(&snap);
    viewport_paint_motor_page(&snap);
  }
}

void LcdViewport_UpdateCurrentPage(void)
{
  if (LcdViewport_GetPageKind() != LCD_VIEWPORT_PAGE_KIND_TEXT)
  {
    return;
  }

  if (s_page == LCD_VIEWPORT_PAGE_MOTOR)
  {
    viewport_update_motor_page();
  }
}

void LcdViewport_PollInput(void)
{
  viewport_poll_button();
}
