#ifndef LCD_TEXT_H
#define LCD_TEXT_H

#include <stdint.h>

#define LCD_TEXT_COLS  48U

typedef struct {
  uint16_t x;
  uint16_t y;
  uint16_t row_h;
  const char *text;
} lcd_text_row_t;

typedef struct {
  char text[LCD_TEXT_COLS + 1U];
} lcd_text_row_cache_t;

void LcdText_DrawRowsUnlocked(const lcd_text_row_t *rows, lcd_text_row_cache_t *cache, uint16_t count);
void LcdText_UpdateRowsUnlocked(const lcd_text_row_t *rows, lcd_text_row_cache_t *cache, uint16_t count);
void LcdText_PaintRows(const lcd_text_row_t *rows, lcd_text_row_cache_t *cache, uint16_t count);
void LcdText_UpdateRows(const lcd_text_row_t *rows, lcd_text_row_cache_t *cache, uint16_t count);
void LcdText_ResetCache(lcd_text_row_cache_t *cache, uint16_t count);

#endif /* LCD_TEXT_H */
