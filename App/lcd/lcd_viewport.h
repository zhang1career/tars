#ifndef LCD_VIEWPORT_H
#define LCD_VIEWPORT_H

#include <stdint.h>

typedef enum {
  LCD_VIEWPORT_PAGE_LOG = 0,
  LCD_VIEWPORT_PAGE_MOTOR = 1
} lcd_viewport_page_t;

void LcdViewport_Init(void);
lcd_viewport_page_t LcdViewport_GetPage(void);
void LcdViewport_SetPage(lcd_viewport_page_t page);
void LcdViewport_NextPage(void);
void LcdViewport_DrawCurrentPage(void);
void LcdViewport_PollInput(void);
int LcdViewport_IsLogPageActive(void);

#endif /* LCD_VIEWPORT_H */
