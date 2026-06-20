#ifndef LCD_FB_H
#define LCD_FB_H

#include <stdint.h>

#define LCD_FB_WIDTH   240U
#define LCD_FB_HEIGHT  320U

void LcdFb_Init(void);
uint16_t *LcdFb_GetDrawBuffer(void);
const uint16_t *LcdFb_GetDisplayBuffer(void);

void LcdFb_BeginFrame(void);
void LcdFb_EndFrame(void);

void LcdFb_CopyDisplayToDraw(void);
void LcdFb_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void LcdFb_FillRectOn(uint16_t *fb, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);

#endif /* LCD_FB_H */
