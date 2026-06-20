#ifndef TARS_HAL_H
#define TARS_HAL_H

#include <stdint.h>

void TarsHal_Init(void);
void TarsHal_FormatStatus(char *out, uint32_t out_size);

#endif /* TARS_HAL_H */
