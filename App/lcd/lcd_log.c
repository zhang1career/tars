#include "lcd_log.h"
#include "ili9341.h"
#include "ltdc.h"
#include "fonts.h"
#include "main.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#define LCD_FB_ADDR           0xD0000000UL
#define LCD_WIDTH             240U
#define LCD_HEIGHT            320U
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

static uint16_t *s_framebuffer;
static char s_log_lines[LCD_LOG_ROWS][LCD_LOG_COLS + 1U];
static uint16_t s_log_count;
static uint16_t s_cursor_x;
static uint16_t s_cursor_y;
static uint16_t s_text_color = LCD_COLOR_WHITE;
static uint16_t s_bg_color = LCD_COLOR_BLACK;
static volatile uint8_t s_vblank_ready;

void HAL_LTDC_LineEventCallback(LTDC_HandleTypeDef *hltdc)
{
  (void)hltdc;
  s_vblank_ready = 1U;
}

static void lcd_wait_vblank(void)
{
  s_vblank_ready = 0U;
  (void)HAL_LTDC_ProgramLineEvent(&hltdc, (uint32_t)(LCD_HEIGHT - 1U));

  uint32_t start = HAL_GetTick();
  while ((s_vblank_ready == 0U) && ((HAL_GetTick() - start) < 50U))
  {
  }
}

static void lcd_draw_pixel(uint16_t x, uint16_t y, uint16_t color)
{
  if (x >= LCD_WIDTH || y >= LCD_HEIGHT)
  {
    return;
  }

  s_framebuffer[(y * LCD_WIDTH) + x] = color;
}

static void lcd_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
  for (uint16_t row = 0U; row < h; row++)
  {
    for (uint16_t col = 0U; col < w; col++)
    {
      lcd_draw_pixel(x + col, y + row, color);
    }
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

  while ((*text != '\0') && (cx + LCD_LOG_CELL_W <= LCD_WIDTH))
  {
    lcd_draw_char(cx, y, *text);
    cx = (uint16_t)(cx + LCD_LOG_CELL_W);
    text++;
  }
}

static void lcd_draw_header(void)
{
  lcd_fill_rect(0U, 0U, LCD_WIDTH, LCD_LOG_TOP, s_bg_color);
  lcd_draw_string(4U, 2U, "TARS USB Log");
}

static void lcd_redraw_log_region(void)
{
  uint16_t start = 0U;

  if (s_log_count > LCD_LOG_ROWS)
  {
    start = (uint16_t)(s_log_count - LCD_LOG_ROWS);
  }

  lcd_wait_vblank();
  lcd_fill_rect(0U, LCD_LOG_TOP, LCD_WIDTH, (uint16_t)(LCD_HEIGHT - LCD_LOG_TOP), s_bg_color);

  for (uint16_t i = 0U; i < LCD_LOG_ROWS; i++)
  {
    if ((start + i) >= s_log_count)
    {
      break;
    }

    lcd_draw_string(2U, (uint16_t)(LCD_LOG_TOP + (i * LCD_LOG_CELL_H)), s_log_lines[start + i]);
  }
}

static void lcd_draw_log_line(uint16_t vis_row, const char *text)
{
  uint16_t y = (uint16_t)(LCD_LOG_TOP + (vis_row * LCD_LOG_CELL_H));

  lcd_wait_vblank();
  lcd_fill_rect(0U, y, LCD_WIDTH, LCD_LOG_CELL_H, s_bg_color);
  lcd_draw_string(2U, y, text);
}

void LcdLog_Init(void)
{
  s_framebuffer = (uint16_t *)LCD_FB_ADDR;
  s_log_count = 0U;
  s_cursor_x = 0U;
  s_cursor_y = 0U;

  ili9341_Init();
  lcd_fill_rect(0U, 0U, LCD_WIDTH, LCD_HEIGHT, s_bg_color);
  lcd_draw_header();
  MX_LTDC_Init();
  (void)HAL_LTDC_ProgramLineEvent(&hltdc, (uint32_t)(LCD_HEIGHT - 1U));
  ili9341_DisplayOn();

  memset(s_log_lines, 0, sizeof(s_log_lines));
  LcdLog_WriteLine("LCD log ready");
}

void LcdLog_Clear(void)
{
  s_log_count = 0U;
  memset(s_log_lines, 0, sizeof(s_log_lines));
  lcd_wait_vblank();
  lcd_fill_rect(0U, LCD_LOG_TOP, LCD_WIDTH, (uint16_t)(LCD_HEIGHT - LCD_LOG_TOP), s_bg_color);
}

void LcdLog_WriteLine(const char *line)
{
  if (line == NULL)
  {
    return;
  }

  if (s_log_count < LCD_LOG_ROWS)
  {
    strncpy(s_log_lines[s_log_count], line, LCD_LOG_COLS);
    s_log_lines[s_log_count][LCD_LOG_COLS] = '\0';
    s_log_count++;
    lcd_draw_log_line((uint16_t)(s_log_count - 1U), s_log_lines[s_log_count - 1U]);
  }
  else
  {
    memmove(s_log_lines[0], s_log_lines[1], sizeof(s_log_lines[0]) * (LCD_LOG_ROWS - 1U));
    strncpy(s_log_lines[LCD_LOG_ROWS - 1U], line, LCD_LOG_COLS);
    s_log_lines[LCD_LOG_ROWS - 1U][LCD_LOG_COLS] = '\0';
    lcd_redraw_log_region();
  }
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
