#ifndef SHELL_H
#define SHELL_H

#include <stdint.h>

void Shell_Init(void);
void Shell_Task(void const *argument);
int Shell_CdcWrite(const char *data, uint16_t len);
void Shell_CdcRxPush(const uint8_t *data, uint32_t len);
uint8_t Shell_CdcIsReady(void);
void Shell_CdcSetReady(uint8_t ready);
void Shell_OnUsbConfigured(void);

#endif /* SHELL_H */
