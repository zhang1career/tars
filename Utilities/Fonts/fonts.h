#ifndef __FONTS_H
#define __FONTS_H

#include <stdint.h>

typedef struct _tFont
{
  const uint8_t *table;
  uint16_t Width;
  uint16_t Height;
} sFONT;

extern sFONT Font8;

#endif /* __FONTS_H */
