#include "usbd_cdc_if.h"
#include "usb_device.h"
#include "shell.h"
#include "usb_otg.h"
#include "usbd_cdc.h"

#define APP_RX_DATA_SIZE  512U

static uint8_t s_user_rx_buffer[APP_RX_DATA_SIZE];

static int8_t CDC_Itf_Init(void);
static int8_t CDC_Itf_DeInit(void);
static int8_t CDC_Itf_Control(uint8_t cmd, uint8_t *pbuf, uint16_t length);
static int8_t CDC_Itf_Receive(uint8_t *buf, uint32_t *len);

USBD_CDC_ItfTypeDef USBD_CDC_fops = {
  CDC_Itf_Init,
  CDC_Itf_DeInit,
  CDC_Itf_Control,
  CDC_Itf_Receive
};

static int8_t CDC_Itf_Init(void)
{
  USBD_CDC_SetTxBuffer(&hUsbDeviceHS, s_user_rx_buffer, 0U);
  USBD_CDC_SetRxBuffer(&hUsbDeviceHS, s_user_rx_buffer);
  return USBD_OK;
}

static int8_t CDC_Itf_DeInit(void)
{
  Shell_CdcSetReady(0);
  return USBD_OK;
}

static int8_t CDC_Itf_Control(uint8_t cmd, uint8_t *pbuf, uint16_t length)
{
  (void)length;

  switch (cmd)
  {
  case CDC_SET_CONTROL_LINE_STATE:
    if ((pbuf != NULL) && ((pbuf[0] & 1U) != 0U))
    {
      Shell_CdcSetReady(1U);
      g_usb_device_link_event = 1U;
    }
    else
    {
      Shell_CdcSetReady(0U);
      g_usb_device_link_event = 2U;
    }
    break;

  default:
    break;
  }

  return USBD_OK;
}

static int8_t CDC_Itf_Receive(uint8_t *buf, uint32_t *len)
{
  Shell_CdcRxPush(buf, *len);
  USBD_CDC_SetRxBuffer(&hUsbDeviceHS, buf);
  USBD_CDC_ReceivePacket(&hUsbDeviceHS);
  return USBD_OK;
}
