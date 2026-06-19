#ifndef USB_DEVICE_APP_H
#define USB_DEVICE_APP_H

#include "usbd_def.h"

extern USBD_HandleTypeDef hUsbDeviceHS;

void MX_USB_DEVICE_Init(void);
void MX_USB_DEVICE_DeInit(void);

#endif /* USB_DEVICE_APP_H */
