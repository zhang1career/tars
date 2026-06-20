#ifndef LCD_LOG_H
#define LCD_LOG_H

#include <stdint.h>
#include "usb_otg.h"

void LcdLog_Init(void);
void LcdLog_Clear(void);
void LcdLog_WriteLine(const char *line);
void LcdLog_Printf(const char *fmt, ...);
void LcdLog_SetStatusBanner(const char *line);
void LcdLog_NotifyRole(UsbOtgRole role);
void LcdLog_NotifyHostEvent(uint8_t connected);
void LcdLog_NotifyDeviceEvent(uint8_t connected);

#endif /* LCD_LOG_H */
