#include "lcd_text.h"
#include "lcd_log.h"
#include <string.h>

static void lcd_text_draw_row(const lcd_text_row_t *row)
{
  LcdLog_FillRect(0U, row->y, LcdLog_GetWidth(), row->row_h, LcdLog_GetBgColor());
  if ((row->text != NULL) && (row->text[0] != '\0'))
  {
    LcdLog_DrawString(row->x, row->y, row->text);
  }
}

void LcdText_ResetCache(lcd_text_row_cache_t *cache, uint16_t count)
{
  uint16_t i;

  if (cache == NULL)
  {
    return;
  }

  for (i = 0U; i < count; i++)
  {
    cache[i].text[0] = '\0';
  }
}

void LcdText_DrawRowsUnlocked(const lcd_text_row_t *rows, lcd_text_row_cache_t *cache, uint16_t count)
{
  uint16_t i;

  if ((rows == NULL) || (cache == NULL) || (count == 0U))
  {
    return;
  }

  for (i = 0U; i < count; i++)
  {
    lcd_text_draw_row(&rows[i]);
    strncpy(cache[i].text, rows[i].text, LCD_TEXT_COLS);
    cache[i].text[LCD_TEXT_COLS] = '\0';
  }
}

void LcdText_PaintRows(const lcd_text_row_t *rows, lcd_text_row_cache_t *cache, uint16_t count)
{
  LcdLog_Lock();
  LcdLog_BeginComposeFrame(0);
  LcdText_DrawRowsUnlocked(rows, cache, count);
  LcdLog_EndComposeFrame();
  LcdLog_Unlock();
}

void LcdText_UpdateRowsUnlocked(const lcd_text_row_t *rows, lcd_text_row_cache_t *cache, uint16_t count)
{
  uint16_t i;

  if ((rows == NULL) || (cache == NULL) || (count == 0U))
  {
    return;
  }

  for (i = 0U; i < count; i++)
  {
    if (strcmp(rows[i].text, cache[i].text) == 0)
    {
      continue;
    }

    lcd_text_draw_row(&rows[i]);
    strncpy(cache[i].text, rows[i].text, LCD_TEXT_COLS);
    cache[i].text[LCD_TEXT_COLS] = '\0';
  }
}

void LcdText_UpdateRows(const lcd_text_row_t *rows, lcd_text_row_cache_t *cache, uint16_t count)
{
  uint16_t i;
  int any_change = 0;

  if ((rows == NULL) || (cache == NULL) || (count == 0U))
  {
    return;
  }

  for (i = 0U; i < count; i++)
  {
    if (strcmp(rows[i].text, cache[i].text) != 0)
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
  LcdLog_BeginComposeFrame(1);
  LcdText_UpdateRowsUnlocked(rows, cache, count);
  LcdLog_EndComposeFrame();
  LcdLog_Unlock();
}
