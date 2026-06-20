#ifndef TARS_OTA_H
#define TARS_OTA_H

#include <stdint.h>

typedef enum {
  TARS_OTA_BANK_A = 0,
  TARS_OTA_BANK_B = 1
} tars_ota_bank_t;

void TarsOta_Init(void);
tars_ota_bank_t TarsOta_GetActiveBank(void);
tars_ota_bank_t TarsOta_GetStagedBank(void);
void TarsOta_FormatStatus(char *out, uint32_t out_size);

#endif /* TARS_OTA_H */
