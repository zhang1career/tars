#ifndef TARS_FLASH_HW_H
#define TARS_FLASH_HW_H

#include <stdint.h>
#include "tars_app.h"

void TarsFlash_Init(void);
void TarsFlash_Lock(void);
void TarsFlash_Unlock(void);

tars_status_t TarsFlash_Read(uint32_t address, void *dst, uint32_t size);
tars_status_t TarsFlash_Program(uint32_t address, const uint8_t *data, uint32_t size);
tars_status_t TarsFlash_EraseSector(uint32_t sector);

/* Used by LittleFS block device while LFS lock is already held. */
tars_status_t TarsFlash_ProgramUnlocked(uint32_t address, const uint8_t *data, uint32_t size);
tars_status_t TarsFlash_EraseSectorUnlocked(uint32_t sector);

#endif /* TARS_FLASH_HW_H */
