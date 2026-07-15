#ifndef LCD_VIEWPORT_H
#define LCD_VIEWPORT_H

#include <stdint.h>

typedef enum {
  LCD_VIEWPORT_PAGE_LOG = 0,
  LCD_VIEWPORT_PAGE_MOTOR = 1,
  LCD_VIEWPORT_PAGE_COUNT
} lcd_viewport_page_t;

typedef enum {
  LCD_VIEWPORT_PAGE_KIND_TEXT = 0,
  LCD_VIEWPORT_PAGE_KIND_GRAPHICS = 1
} lcd_viewport_page_kind_t;

void LcdViewport_Init(void);
lcd_viewport_page_t LcdViewport_GetPage(void);
lcd_viewport_page_kind_t LcdViewport_GetPageKind(void);
int LcdViewport_IsTextPageActive(void);
void LcdViewport_SetPage(lcd_viewport_page_t page);
void LcdViewport_NextPage(void);
void LcdViewport_DrawCurrentPage(void);
void LcdViewport_UpdateCurrentPage(void);
void LcdViewport_PollInput(void);

#endif /* LCD_VIEWPORT_H */
