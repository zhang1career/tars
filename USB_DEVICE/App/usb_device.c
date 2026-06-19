#include "usb_device.h"
#include "usbd_core.h"
#include "usbd_desc.h"
#include "usbd_cdc.h"
#include "usbd_cdc_if.h"
#include "main.h"

USBD_HandleTypeDef hUsbDeviceHS;

void MX_USB_DEVICE_Init(void)
{
  if (USBD_Init(&hUsbDeviceHS, &VCP_Desc, 0) != USBD_OK)
  {
    Error_Handler();
  }

  if (USBD_RegisterClass(&hUsbDeviceHS, USBD_CDC_CLASS) != USBD_OK)
  {
    Error_Handler();
  }

  if (USBD_CDC_RegisterInterface(&hUsbDeviceHS, &USBD_CDC_fops) != USBD_OK)
  {
    Error_Handler();
  }

  if (USBD_Start(&hUsbDeviceHS) != USBD_OK)
  {
    Error_Handler();
  }
}

void MX_USB_DEVICE_DeInit(void)
{
  USBD_Stop(&hUsbDeviceHS);
  USBD_DeInit(&hUsbDeviceHS);
}
