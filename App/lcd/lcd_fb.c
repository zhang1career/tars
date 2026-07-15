#include "lcd_fb.h"
#include "ltdc.h"
#include "dma2d.h"
#include "main.h"
#include <string.h>

#define LCD_FB_PIXELS   (LCD_FB_WIDTH * LCD_FB_HEIGHT)
#define LCD_FB_BYTES    (LCD_FB_PIXELS * 2U)
#define LCD_FB_ADDR0    0xD0000000UL
#define LCD_FB_ADDR1    (LCD_FB_ADDR0 + LCD_FB_BYTES)

static uint16_t *s_fb[2];
static uint8_t s_display_idx;
static volatile uint8_t s_swap_pending;
static volatile uint8_t s_vsync_flag;

static uint16_t *lcd_fb_draw_ptr(void)
{
  return s_fb[s_display_idx ^ 1U];
}

static void lcd_fb_dma2d_wait(void)
{
  (void)HAL_DMA2D_PollForTransfer(&hdma2d, 50U);
}

static void lcd_fb_dma2d_fill(uint32_t dst, uint16_t w, uint16_t h, uint16_t color, uint32_t out_offset)
{
  hdma2d.Init.Mode = DMA2D_R2M;
  hdma2d.Init.ColorMode = DMA2D_OUTPUT_RGB565;
  hdma2d.Init.OutputOffset = out_offset;
  hdma2d.LayerCfg[1].InputOffset = 0U;
  hdma2d.LayerCfg[1].InputColorMode = DMA2D_INPUT_RGB565;
  (void)HAL_DMA2D_Init(&hdma2d);
  (void)HAL_DMA2D_Start(&hdma2d, color, dst, w, h);
  lcd_fb_dma2d_wait();
}

static void lcd_fb_dma2d_copy(uint32_t src, uint32_t dst, uint16_t w, uint16_t h,
                              uint32_t src_offset, uint32_t dst_offset)
{
  hdma2d.Init.Mode = DMA2D_M2M;
  hdma2d.Init.ColorMode = DMA2D_OUTPUT_RGB565;
  hdma2d.Init.OutputOffset = dst_offset;
  hdma2d.LayerCfg[1].InputOffset = src_offset;
  hdma2d.LayerCfg[1].InputColorMode = DMA2D_INPUT_RGB565;
  hdma2d.LayerCfg[1].AlphaMode = DMA2D_NO_MODIF_ALPHA;
  (void)HAL_DMA2D_Init(&hdma2d);
  (void)HAL_DMA2D_Start(&hdma2d, src, dst, w, h);
  lcd_fb_dma2d_wait();
}

void HAL_LTDC_LineEventCallback(LTDC_HandleTypeDef *hltdc)
{
  (void)hltdc;

  if (s_swap_pending != 0U)
  {
    s_display_idx ^= 1U;
    (void)HAL_LTDC_SetAddress(hltdc, (uint32_t)s_fb[s_display_idx], 0U);
    s_swap_pending = 0U;
  }

  s_vsync_flag = 1U;
}

void LcdFb_Init(void)
{
  s_fb[0] = (uint16_t *)LCD_FB_ADDR0;
  s_fb[1] = (uint16_t *)LCD_FB_ADDR1;
  s_display_idx = 0U;
  s_swap_pending = 0U;
  s_vsync_flag = 0U;
}

uint16_t *LcdFb_GetDrawBuffer(void)
{
  return lcd_fb_draw_ptr();
}

const uint16_t *LcdFb_GetDisplayBuffer(void)
{
  return s_fb[s_display_idx];
}

void LcdFb_BeginFrame(void)
{
  uint32_t start = HAL_GetTick();

  while ((s_swap_pending != 0U) && ((HAL_GetTick() - start) < 50U))
  {
  }
}

void LcdFb_EndFrame(void)
{
  s_swap_pending = 1U;
  s_vsync_flag = 0U;
  (void)HAL_LTDC_ProgramLineEvent(&hltdc, 0U);

  {
    uint32_t start = HAL_GetTick();

    while ((s_swap_pending != 0U) && ((HAL_GetTick() - start) < 50U))
    {
    }
  }
}

void LcdFb_CopyDisplayToDraw(void)
{
  const uint16_t *src = s_fb[s_display_idx];
  uint16_t *dst = lcd_fb_draw_ptr();

  if (src == dst)
  {
    return;
  }

  lcd_fb_dma2d_copy((uint32_t)src, (uint32_t)dst, LCD_FB_WIDTH, LCD_FB_HEIGHT, 0U, 0U);
}

void LcdFb_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
  LcdFb_FillRectOn(lcd_fb_draw_ptr(), x, y, w, h, color);
}

void LcdFb_FillRectOn(uint16_t *fb, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
  if (fb == NULL)
  {
    return;
  }

  if ((x >= LCD_FB_WIDTH) || (y >= LCD_FB_HEIGHT) || (w == 0U) || (h == 0U))
  {
    return;
  }

  if ((x + w) > LCD_FB_WIDTH)
  {
    w = (uint16_t)(LCD_FB_WIDTH - x);
  }

  if ((y + h) > LCD_FB_HEIGHT)
  {
    h = (uint16_t)(LCD_FB_HEIGHT - y);
  }

  lcd_fb_dma2d_fill((uint32_t)&fb[(y * LCD_FB_WIDTH) + x],
                    w,
                    h,
                    color,
                    (uint32_t)(LCD_FB_WIDTH - x - w));
}
