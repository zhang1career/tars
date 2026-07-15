#include "lcd_log.h"
#include "lcd_viewport.h"
#include "lcd_fb.h"
#include "lcd_text.h"
#include "ili9341.h"
#include "ltdc.h"
#include "fonts.h"
#include "main.h"
#include "cmsis_os.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#define LCD_LOG_TOP             16U
#define LCD_LOG_COLS            40U
#define LCD_LOG_ROWS            28U
#define LCD_LOG_CELL_W           6U
#define LCD_LOG_CELL_H          10U

#define LCD_COLOR_BLACK       0x0000U
#define LCD_COLOR_WHITE       0xFFFFU
#define LCD_COLOR_GREEN       0x07E0U
#define LCD_COLOR_YELLOW      0xFFE0U
#define LCD_COLOR_CYAN        0x07FFU
#define LCD_COLOR_RED         0xF800U

static uint16_t *s_draw_fb;
static char s_log_lines[LCD_LOG_ROWS][LCD_LOG_COLS + 1U];
static lcd_text_row_cache_t s_log_display_cache[LCD_LOG_ROWS];
static char s_status_banner[LCD_LOG_COLS + 1U] = "TARS USB Log";
static char s_banner_cache[LCD_LOG_COLS + 1U];
static uint16_t s_log_count;
static uint16_t s_text_color = LCD_COLOR_WHITE;
static uint16_t s_bg_color = LCD_COLOR_BLACK;
static osMutexId s_lcd_mutex;

osMutexDef(lcd_log_mutex);

static void lcd_log_lock(void)
{
  if (s_lcd_mutex != NULL)
  {
    (void)osMutexWait(s_lcd_mutex, osWaitForever);
  }
}

static void lcd_log_unlock(void)
{
  if (s_lcd_mutex != NULL)
  {
    (void)osMutexRelease(s_lcd_mutex);
  }
}

static void lcd_log_begin_frame(int copy_display)
{
  LcdFb_BeginFrame();
  s_draw_fb = LcdFb_GetDrawBuffer();

  if (copy_display != 0)
  {
    LcdFb_CopyDisplayToDraw();
  }
}

static void lcd_log_end_frame(void)
{
  LcdFb_EndFrame();
}

static void lcd_draw_pixel(uint16_t x, uint16_t y, uint16_t color)
{
  if ((s_draw_fb == NULL) || (x >= LCD_FB_WIDTH) || (y >= LCD_FB_HEIGHT))
  {
    return;
  }

  s_draw_fb[(y * LCD_FB_WIDTH) + x] = color;
}

static void lcd_fill_rect_cpu(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
  if (s_draw_fb != NULL)
  {
    LcdFb_FillRectOn(s_draw_fb, x, y, w, h, color);
  }
}

static void lcd_draw_char(uint16_t x, uint16_t y, char ch)
{
  const sFONT *font = &Font8;
  uint32_t index;

  if ((ch < 32) || (ch > 126))
  {
    ch = '?';
  }

  index = (uint32_t)(ch - 32U) * font->Height;

  for (uint16_t row = 0U; row < font->Height; row++)
  {
    uint8_t line = font->table[index + row];

    for (uint16_t col = 0U; col < font->Width; col++)
    {
      uint16_t color = ((line << col) & 0x80U) ? s_text_color : s_bg_color;
      lcd_draw_pixel(x + col, y + row, color);
    }
  }
}

static void lcd_draw_string(uint16_t x, uint16_t y, const char *text)
{
  uint16_t cx = x;

  while ((*text != '\0') && (cx + LCD_LOG_CELL_W <= LCD_FB_WIDTH))
  {
    lcd_draw_char(cx, y, *text);
    cx = (uint16_t)(cx + LCD_LOG_CELL_W);
    text++;
  }
}

static void lcd_draw_header(void)
{
  lcd_fill_rect_cpu(0U, 0U, LCD_FB_WIDTH, LCD_LOG_TOP, s_bg_color);
  lcd_draw_string(4U, 2U, s_status_banner);
}

static int lcd_log_should_draw(void)
{
  return (LcdViewport_GetPage() == LCD_VIEWPORT_PAGE_LOG) ? 1 : 0;
}

static void lcd_log_build_visible_rows(lcd_text_row_t *rows, uint16_t *count)
{
  uint16_t start = 0U;
  uint16_t n = 0U;

  if (s_log_count > LCD_LOG_ROWS)
  {
    start = (uint16_t)(s_log_count - LCD_LOG_ROWS);
  }

  for (uint16_t i = 0U; i < LCD_LOG_ROWS; i++)
  {
    if ((start + i) >= s_log_count)
    {
      break;
    }

    rows[n].x = 2U;
    rows[n].y = (uint16_t)(LCD_LOG_TOP + (i * LCD_LOG_CELL_H));
    rows[n].row_h = LCD_LOG_CELL_H;
    rows[n].text = s_log_lines[start + i];
    n++;
  }

  *count = n;
}

static void lcd_fill_content_area(void)
{
  lcd_fill_rect_cpu(0U,
                    LCD_LOG_TOP,
                    LCD_FB_WIDTH,
                    (uint16_t)(LCD_FB_HEIGHT - LCD_LOG_TOP),
                    s_bg_color);
}

static void lcd_log_paint_visible_rows(int copy_display)
{
  lcd_text_row_t rows[LCD_LOG_ROWS];
  uint16_t count = 0U;

  lcd_log_build_visible_rows(rows, &count);

  lcd_log_begin_frame(copy_display);
  lcd_draw_header();
  lcd_fill_content_area();
  strncpy(s_banner_cache, s_status_banner, LCD_LOG_COLS);
  s_banner_cache[LCD_LOG_COLS] = '\0';
  LcdText_DrawRowsUnlocked(rows, s_log_display_cache, count);
  lcd_log_end_frame();
}

void LcdLog_RedrawLogRegion(void)
{
  lcd_log_lock();
  lcd_log_paint_visible_rows(0);
  lcd_log_unlock();
}

void LcdLog_Lock(void)
{
  lcd_log_lock();
}

void LcdLog_Unlock(void)
{
  lcd_log_unlock();
}

void LcdLog_DrawString(uint16_t x, uint16_t y, const char *text)
{
  lcd_draw_string(x, y, text);
}

void LcdLog_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
  lcd_fill_rect_cpu(x, y, w, h, color);
}

uint16_t LcdLog_GetWidth(void)
{
  return LCD_FB_WIDTH;
}

uint16_t LcdLog_GetHeight(void)
{
  return LCD_FB_HEIGHT;
}

uint16_t LcdLog_GetLogTop(void)
{
  return LCD_LOG_TOP;
}

uint16_t LcdLog_GetBgColor(void)
{
  return s_bg_color;
}

void LcdLog_Init(void)
{
  s_log_count = 0U;

  if (s_lcd_mutex == NULL)
  {
    s_lcd_mutex = osMutexCreate(osMutex(lcd_log_mutex));
  }

  LcdFb_Init();
  s_draw_fb = (uint16_t *)LcdFb_GetDisplayBuffer();
  LcdText_ResetCache(s_log_display_cache, LCD_LOG_ROWS);
  s_banner_cache[0] = '\0';

  ili9341_Init();
  lcd_fill_rect_cpu(0U, 0U, LCD_FB_WIDTH, LCD_FB_HEIGHT, s_bg_color);
  lcd_draw_header();
  LcdViewport_Init();
  MX_LTDC_Init();
  (void)HAL_LTDC_ProgramLineEvent(&hltdc, 0U);
  ili9341_DisplayOn();

  memset(s_log_lines, 0, sizeof(s_log_lines));
  LcdLog_WriteLine("LCD log ready");
}

void LcdLog_SetStatusBannerText(const char *line)
{
  if (line == NULL)
  {
    line = "TARS USB Log";
  }

  strncpy(s_status_banner, line, LCD_LOG_COLS);
  s_status_banner[LCD_LOG_COLS] = '\0';
}

void LcdLog_DrawHeader(void)
{
  lcd_draw_header();
}

void LcdLog_FillContentArea(void)
{
  lcd_fill_content_area();
}

void LcdLog_BeginComposeFrame(int copy_display)
{
  lcd_log_begin_frame(copy_display);
}

void LcdLog_EndComposeFrame(void)
{
  lcd_log_end_frame();
}

void LcdLog_SetStatusBanner(const char *line)
{
  lcd_log_lock();
  LcdLog_SetStatusBannerText(line);

  if (strcmp(s_status_banner, s_banner_cache) != 0)
  {
    lcd_log_begin_frame(1);
    lcd_draw_header();
    strncpy(s_banner_cache, s_status_banner, LCD_LOG_COLS);
    s_banner_cache[LCD_LOG_COLS] = '\0';
    lcd_log_end_frame();
  }

  lcd_log_unlock();
}

void LcdLog_Clear(void)
{
  lcd_log_lock();
  s_log_count = 0U;
  memset(s_log_lines, 0, sizeof(s_log_lines));
  LcdText_ResetCache(s_log_display_cache, LCD_LOG_ROWS);

  if (lcd_log_should_draw() != 0)
  {
    lcd_log_begin_frame(1);
    lcd_draw_header();
    lcd_fill_content_area();
    lcd_log_end_frame();
  }

  lcd_log_unlock();
}

void LcdLog_WriteLine(const char *line)
{
  lcd_text_row_t rows[LCD_LOG_ROWS];
  uint16_t count = 0U;

  if (line == NULL)
  {
    return;
  }

  lcd_log_lock();

  if (s_log_count < LCD_LOG_ROWS)
  {
    strncpy(s_log_lines[s_log_count], line, LCD_LOG_COLS);
    s_log_lines[s_log_count][LCD_LOG_COLS] = '\0';
    s_log_count++;
  }
  else
  {
    memmove(s_log_lines[0], s_log_lines[1], sizeof(s_log_lines[0]) * (LCD_LOG_ROWS - 1U));
    strncpy(s_log_lines[LCD_LOG_ROWS - 1U], line, LCD_LOG_COLS);
    s_log_lines[LCD_LOG_ROWS - 1U][LCD_LOG_COLS] = '\0';
  }

  if (lcd_log_should_draw() != 0)
  {
    lcd_log_build_visible_rows(rows, &count);
    lcd_log_begin_frame(1);

    if (strcmp(s_status_banner, s_banner_cache) != 0)
    {
      lcd_draw_header();
      strncpy(s_banner_cache, s_status_banner, LCD_LOG_COLS);
      s_banner_cache[LCD_LOG_COLS] = '\0';
    }

    LcdText_UpdateRowsUnlocked(rows, s_log_display_cache, count);
    lcd_log_end_frame();
  }

  lcd_log_unlock();
}

void LcdLog_Printf(const char *fmt, ...)
{
  char buffer[LCD_LOG_COLS + 1U];
  va_list args;

  va_start(args, fmt);
  vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);

  LcdLog_WriteLine(buffer);
}

void LcdLog_NotifyRole(UsbOtgRole role)
{
  switch (role)
  {
  case USB_OTG_ROLE_DEVICE:
    s_text_color = LCD_COLOR_CYAN;
    LcdLog_Printf("USB cable: DEVICE (slave)");
    break;

  case USB_OTG_ROLE_HOST:
    s_text_color = LCD_COLOR_YELLOW;
    LcdLog_Printf("USB cable: HOST");
    break;

  default:
    s_text_color = LCD_COLOR_WHITE;
    LcdLog_WriteLine("USB cable: none");
    break;
  }

  s_text_color = LCD_COLOR_WHITE;
}

void LcdLog_NotifyHostEvent(uint8_t connected)
{
  if (connected != 0U)
  {
    s_text_color = LCD_COLOR_GREEN;
    LcdLog_WriteLine("USB host: device connected");
  }
  else
  {
    s_text_color = LCD_COLOR_RED;
    LcdLog_WriteLine("USB host: device disconnected");
  }

  s_text_color = LCD_COLOR_WHITE;
}

void LcdLog_NotifyDeviceEvent(uint8_t connected)
{
  if (connected != 0U)
  {
    s_text_color = LCD_COLOR_GREEN;
    LcdLog_WriteLine("USB device: PC connected");
  }
  else
  {
    s_text_color = LCD_COLOR_RED;
    LcdLog_WriteLine("USB device: PC disconnected");
  }

  s_text_color = LCD_COLOR_WHITE;
}
