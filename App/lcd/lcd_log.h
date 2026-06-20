#ifndef LCD_LOG_H
#define LCD_LOG_H

#include <stdint.h>
#include "usb_otg.h"

void LcdLog_Init(void);
void LcdLog_Clear(void);
void LcdLog_WriteLine(const char *line);
void LcdLog_Printf(const char *fmt, ...);
void LcdLog_SetStatusBanner(const char *line);
void LcdLog_RedrawLogRegion(void);
void LcdLog_Lock(void);
void LcdLog_Unlock(void);
void LcdLog_DrawString(uint16_t x, uint16_t y, const char *text);
void LcdLog_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
uint16_t LcdLog_GetWidth(void);
uint16_t LcdLog_GetHeight(void);
uint16_t LcdLog_GetLogTop(void);
uint16_t LcdLog_GetBgColor(void);
void LcdLog_NotifyRole(UsbOtgRole role);
void LcdLog_NotifyHostEvent(uint8_t connected);
void LcdLog_NotifyDeviceEvent(uint8_t connected);

#endif /* LCD_LOG_H */
