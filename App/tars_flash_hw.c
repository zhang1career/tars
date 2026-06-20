#include "tars_flash_hw.h"
#include "main.h"
#include "stm32f4xx_hal_flash_ex.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include <string.h>

static SemaphoreHandle_t s_flash_mutex;

static void flash_clear_flags(void)
{
  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                         FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);
}

static void flash_flush_caches(void)
{
  FLASH_FlushCaches();
}

void TarsFlash_Init(void)
{
  if (s_flash_mutex == NULL)
  {
    s_flash_mutex = xSemaphoreCreateMutex();
  }
}

void TarsFlash_Lock(void)
{
  if (s_flash_mutex != NULL)
  {
    (void)xSemaphoreTake(s_flash_mutex, portMAX_DELAY);
  }
}

void TarsFlash_Unlock(void)
{
  if (s_flash_mutex != NULL)
  {
    (void)xSemaphoreGive(s_flash_mutex);
  }
}

tars_status_t TarsFlash_Read(uint32_t address, void *dst, uint32_t size)
{
  if ((dst == NULL) || (size == 0U))
  {
    return TARS_ERR_PARAM;
  }

  memcpy(dst, (const void *)address, size);
  return TARS_OK;
}

tars_status_t TarsFlash_ProgramUnlocked(uint32_t address, const uint8_t *data, uint32_t size)
{
  uint32_t i;
  HAL_StatusTypeDef hal;

  if ((data == NULL) || (size == 0U))
  {
    return TARS_ERR_PARAM;
  }

  flash_clear_flags();
  HAL_FLASH_Unlock();

  for (i = 0U; i < size; i += 4U)
  {
    uint32_t word = 0xFFFFFFFFUL;
    uint32_t remain = size - i;
    uint32_t copy = (remain >= 4U) ? 4U : remain;

    memcpy(&word, data + i, copy);
    hal = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, address + i, word);

    if (hal != HAL_OK)
    {
      HAL_FLASH_Lock();
      return TARS_ERR_FLASH;
    }
  }

  HAL_FLASH_Lock();
  flash_flush_caches();
  return TARS_OK;
}

tars_status_t TarsFlash_EraseSectorUnlocked(uint32_t sector)
{
  FLASH_EraseInitTypeDef erase = {0};
  uint32_t sector_error = 0U;
  HAL_StatusTypeDef hal;

  flash_clear_flags();
  HAL_FLASH_Unlock();

  erase.TypeErase = FLASH_TYPEERASE_SECTORS;
  erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
  erase.Sector = sector;
  erase.NbSectors = 1U;

  hal = HAL_FLASHEx_Erase(&erase, &sector_error);
  HAL_FLASH_Lock();

  if (hal != HAL_OK)
  {
    return TARS_ERR_FLASH;
  }

  flash_flush_caches();
  return TARS_OK;
}

tars_status_t TarsFlash_Program(uint32_t address, const uint8_t *data, uint32_t size)
{
  tars_status_t status;

  TarsFlash_Lock();
  status = TarsFlash_ProgramUnlocked(address, data, size);
  TarsFlash_Unlock();
  return status;
}

tars_status_t TarsFlash_EraseSector(uint32_t sector)
{
  tars_status_t status;

  TarsFlash_Lock();
  status = TarsFlash_EraseSectorUnlocked(sector);
  TarsFlash_Unlock();
  return status;
}
