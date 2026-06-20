#ifndef TARS_MCU_H
#define TARS_MCU_H

#include <stdint.h>

int TarsMcu_GpioWrite(const char *pin_name, int value);
int TarsMcu_GpioRead(const char *pin_name, int *value_out);

void TarsMcu_FormatInfo(char *out, uint32_t out_size);
int TarsMcu_ShellHandle(const char *args, char *out, uint32_t out_size);

#endif /* TARS_MCU_H */
