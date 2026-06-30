#include "tars_features.h"

#if !TARS_FEATURE_LCD

/*
 * No-op stubs for the LCD entry points called from non-LCD code
 * (freertos.c, tars_resource.c, tars_registry.c, usb_otg.c, tars_vfs.c) when
 * the LCD stack is compiled out (TARS_FEATURE_LCD=0). Keeps those call sites
 * free of #ifdefs. The real implementations live in App/lcd/*.c, gated on.
 */

#include "lcd_log.h"
#include "lcd_viewport.h"

void LcdLog_Init(void) {}
void LcdLog_WriteLine(const char *line) { (void)line; }
void LcdLog_Printf(const char *fmt, ...) { (void)fmt; }
void LcdLog_SetStatusBanner(const char *line) { (void)line; }
void LcdLog_NotifyRole(UsbOtgRole role) { (void)role; }
void LcdLog_NotifyHostEvent(uint8_t connected) { (void)connected; }
void LcdLog_NotifyDeviceEvent(uint8_t connected) { (void)connected; }

void LcdViewport_NextPage(void) {}
void LcdViewport_UpdateCurrentPage(void) {}

#endif /* !TARS_FEATURE_LCD */
