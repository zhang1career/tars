#ifndef USB_OTG_H
#define USB_OTG_H

#include <stdint.h>

typedef enum {
  USB_OTG_ROLE_NONE = 0,
  USB_OTG_ROLE_DEVICE,
  USB_OTG_ROLE_HOST
} UsbOtgRole;

extern volatile uint8_t g_usb_host_link_event;
extern volatile uint8_t g_usb_device_link_event;

UsbOtgRole UsbOtg_DetectRole(void);
UsbOtgRole UsbOtg_GetRole(void);
void UsbOtg_StartRole(UsbOtgRole role);
void UsbOtg_StopRole(UsbOtgRole role);
void UsbOtg_Task(void const *argument);

void UsbOtg_RecoverDeviceCdc(void);

#endif /* USB_OTG_H */
