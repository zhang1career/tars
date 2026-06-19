#include "usb_otg.h"
#include "usb_device.h"
#include "usb_host.h"
#include "shell.h"
#include "lcd_log.h"
#include "main.h"
#include "cmsis_os.h"
#include "usbh_core.h"

static volatile UsbOtgRole s_active_role = USB_OTG_ROLE_NONE;

volatile uint8_t g_usb_host_link_event;
volatile uint8_t g_usb_device_link_event;

static void usb_otg_setup_detect_gpio(void)
{
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_GPIOB_CLK_ENABLE();

  gpio.Mode = GPIO_MODE_INPUT;
  gpio.Pull = GPIO_NOPULL;
  gpio.Pin = VBUS_HS_Pin;
  HAL_GPIO_Init(VBUS_HS_GPIO_Port, &gpio);

  gpio.Pull = GPIO_PULLUP;
  gpio.Pin = OTG_HS_ID_Pin;
  HAL_GPIO_Init(OTG_HS_ID_GPIO_Port, &gpio);
}

UsbOtgRole UsbOtg_DetectRole(void)
{
  GPIO_PinState vbus;
  GPIO_PinState id = GPIO_PIN_SET;

  if (s_active_role == USB_OTG_ROLE_NONE)
  {
    usb_otg_setup_detect_gpio();
    osDelay(5);
    id = HAL_GPIO_ReadPin(OTG_HS_ID_GPIO_Port, OTG_HS_ID_Pin);
  }

  vbus = HAL_GPIO_ReadPin(VBUS_HS_GPIO_Port, VBUS_HS_Pin);

  /* PC / Micro-B cable: external VBUS or ID grounded */
  if ((vbus == GPIO_PIN_SET) || (id == GPIO_PIN_RESET))
  {
    return USB_OTG_ROLE_DEVICE;
  }

  /* OTG host cable: ID floating/high, no external VBUS */
  return USB_OTG_ROLE_HOST;
}

UsbOtgRole UsbOtg_GetRole(void)
{
  return s_active_role;
}

void UsbOtg_StartRole(UsbOtgRole role)
{
  switch (role)
  {
  case USB_OTG_ROLE_DEVICE:
    Shell_Init();
    MX_USB_DEVICE_Init();
    break;

  case USB_OTG_ROLE_HOST:
    MX_USB_HOST_Init();
    break;

  default:
    break;
  }

  s_active_role = role;

  LcdLog_NotifyRole(role);
}

void UsbOtg_StopRole(UsbOtgRole role)
{
  switch (role)
  {
  case USB_OTG_ROLE_DEVICE:
    MX_USB_DEVICE_DeInit();
    Shell_CdcSetReady(0);
    break;

  case USB_OTG_ROLE_HOST:
    USBH_Stop(&hUsbHostHS);
    USBH_DeInit(&hUsbHostHS);
    break;

  default:
    break;
  }

  s_active_role = USB_OTG_ROLE_NONE;
  osDelay(200);
}

void UsbOtg_Task(void const *argument)
{
  UsbOtgRole role;
  UsbOtgRole last_role = USB_OTG_ROLE_NONE;

  (void)argument;

  for (;;)
  {
    role = UsbOtg_DetectRole();

    if (role != last_role)
    {
      if (last_role != USB_OTG_ROLE_NONE)
      {
        UsbOtg_StopRole(last_role);
      }

      if (role != USB_OTG_ROLE_NONE)
      {
        UsbOtg_StartRole(role);
      }

      last_role = role;
    }

    if (g_usb_host_link_event == 1U)
    {
      g_usb_host_link_event = 0U;
      LcdLog_NotifyHostEvent(1U);
    }
    else if (g_usb_host_link_event == 2U)
    {
      g_usb_host_link_event = 0U;
      LcdLog_NotifyHostEvent(0U);
    }

    if (g_usb_device_link_event == 1U)
    {
      g_usb_device_link_event = 0U;
      LcdLog_NotifyDeviceEvent(1U);
    }
    else if (g_usb_device_link_event == 2U)
    {
      g_usb_device_link_event = 0U;
      LcdLog_NotifyDeviceEvent(0U);
    }

    osDelay(200);
  }
}
