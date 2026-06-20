#include "tars_storage.h"
#include "tars_app.h"
#include "tars_crc.h"
#include "tars_platform.h"
#include "main.h"
#include "stm32f4xx_hal_flash_ex.h"
#include <stddef.h>
#include <string.h>

typedef struct {
  uint32_t magic;
  uint32_t version;
  uint32_t entry_count;
  uint32_t crc32;
} tars_catalog_hdr_t;

typedef struct {
  char     name[TARS_NAME_MAX];
  uint8_t  app_type;
  uint8_t  installed;
  uint8_t  submitted;
  uint8_t  timeslice;
  uint16_t flags;
  uint16_t reserved;
  uint32_t app_version;
  uint32_t blob_addr;
  uint32_t blob_size;
  uint32_t exec_addr;
  uint32_t slot_index;
  tars_manifest_t manifest;
} tars_catalog_entry_t;

static tars_catalog_entry_t s_catalog[TARS_STORAGE_MAX_ENTRIES];
static uint32_t s_catalog_count;
static uint32_t s_sdram_exec_next;
static uint32_t s_lua_pool_offset;
static volatile uint8_t s_catalog_dirty;

#define TARS_CATALOG_HDR_CRC_LEN  ((uint32_t)offsetof(tars_catalog_hdr_t, crc32))

static void storage_flash_clear_flags(void)
{
  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                         FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);
}

static void storage_flash_flush_caches(void)
{
  FLASH_FlushCaches();
}

static tars_status_t storage_flash_erase_sector(uint32_t sector)
{
  FLASH_EraseInitTypeDef erase = {0};
  uint32_t sector_error = 0U;
  HAL_StatusTypeDef hal;

  storage_flash_clear_flags();
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

  storage_flash_flush_caches();
  return TARS_OK;
}

static tars_status_t storage_flash_program(uint32_t address, const uint8_t *data, uint32_t size)
{
  uint32_t i;
  HAL_StatusTypeDef hal;

  if ((data == NULL) || (size == 0U))
  {
    return TARS_ERR_PARAM;
  }

  storage_flash_clear_flags();
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
  storage_flash_flush_caches();
  return TARS_OK;
}

static uint32_t storage_catalog_compute_crc(const tars_catalog_hdr_t *hdr,
                                          const tars_catalog_entry_t *entries,
                                          uint32_t entry_count)
{
  uint32_t crc = 0xFFFFFFFFUL;

  crc = TarsCrc32((const uint8_t *)(const void *)hdr, TARS_CATALOG_HDR_CRC_LEN, crc);
  crc = TarsCrc32((const uint8_t *)(const void *)entries,
                  entry_count * (uint32_t)sizeof(tars_catalog_entry_t),
                  crc);
  return crc ^ 0xFFFFFFFFUL;
}

static void storage_restore_lua_pool_offset(void)
{
  uint32_t i;

  for (i = 0U; i < s_catalog_count; i++)
  {
    if (s_catalog[i].app_type != TARS_APP_TYPE_LUA)
    {
      continue;
    }

    if (s_catalog[i].blob_addr < TARS_LUA_POOL_BASE)
    {
      continue;
    }

    uint32_t end = (s_catalog[i].blob_addr - TARS_LUA_POOL_BASE) + s_catalog[i].blob_size;

    if (end > s_lua_pool_offset)
    {
      s_lua_pool_offset = end;
    }
  }

  s_lua_pool_offset = (s_lua_pool_offset + 3U) & ~3U;
}

static tars_status_t storage_catalog_validate(void)
{
  const tars_catalog_hdr_t *hdr = (const tars_catalog_hdr_t *)(const void *)TARS_CATALOG_BASE;
  uint32_t crc;

  if (hdr->magic != TARS_CATALOG_MAGIC)
  {
    return TARS_ERR_NOT_FOUND;
  }

  if (hdr->entry_count > TARS_STORAGE_MAX_ENTRIES)
  {
    return TARS_ERR_CRC;
  }

  crc = storage_catalog_compute_crc(hdr,
                                    (const tars_catalog_entry_t *)(const void *)(TARS_CATALOG_BASE +
                                                                                 sizeof(tars_catalog_hdr_t)),
                                    hdr->entry_count);

  if (crc != hdr->crc32)
  {
    return TARS_ERR_CRC;
  }

  return TARS_OK;
}

static tars_status_t storage_catalog_save(void)
{
  tars_catalog_hdr_t hdr = {0};
  tars_status_t status;
  const tars_catalog_hdr_t *flash_hdr;

  hdr.magic = TARS_CATALOG_MAGIC;
  hdr.version = 1U;
  hdr.entry_count = s_catalog_count;
  hdr.crc32 = storage_catalog_compute_crc(&hdr, s_catalog, s_catalog_count);

  status = storage_flash_erase_sector(TARS_CATALOG_SECTOR);
  if (status != TARS_OK)
  {
    return status;
  }

  status = storage_flash_program(TARS_CATALOG_BASE, (const uint8_t *)(const void *)&hdr, sizeof(hdr));
  if (status != TARS_OK)
  {
    return status;
  }

  if (s_catalog_count > 0U)
  {
    status = storage_flash_program(TARS_CATALOG_BASE + sizeof(tars_catalog_hdr_t),
                                   (const uint8_t *)(const void *)s_catalog,
                                   s_catalog_count * (uint32_t)sizeof(tars_catalog_entry_t));
    if (status != TARS_OK)
    {
      return status;
    }
  }

  flash_hdr = (const tars_catalog_hdr_t *)(const void *)TARS_CATALOG_BASE;
  if (flash_hdr->magic != TARS_CATALOG_MAGIC)
  {
    return TARS_ERR_FLASH;
  }

  return storage_catalog_validate();
}

void TarsStorage_Init(void)
{
  const tars_catalog_hdr_t *hdr = (const tars_catalog_hdr_t *)(const void *)TARS_CATALOG_BASE;

  s_catalog_count = 0U;
  s_sdram_exec_next = 0U;
  s_lua_pool_offset = 0U;
  s_catalog_dirty = 0U;
  memset(s_catalog, 0, sizeof(s_catalog));

  if (storage_catalog_validate() == TARS_OK)
  {
    s_catalog_count = hdr->entry_count;
    memcpy(s_catalog,
           (const void *)(TARS_CATALOG_BASE + sizeof(tars_catalog_hdr_t)),
           s_catalog_count * sizeof(tars_catalog_entry_t));
    storage_restore_lua_pool_offset();
  }
}

int32_t TarsStorage_FindByName(const char *name)
{
  uint32_t i;

  for (i = 0U; i < s_catalog_count; i++)
  {
    if (strcmp(s_catalog[i].name, name) == 0)
    {
      return (int32_t)i;
    }
  }

  return -1;
}

tars_status_t TarsStorage_FindFreeNativeSlot(int32_t *slot_out)
{
  uint32_t slot;
  uint32_t i;

  for (slot = 0U; slot < TARS_NATIVE_SLOT_COUNT; slot++)
  {
    int used = 0;

    for (i = 0U; i < s_catalog_count; i++)
    {
      if ((s_catalog[i].app_type == TARS_APP_TYPE_NATIVE) &&
          (s_catalog[i].slot_index == slot))
      {
        used = 1;
        break;
      }
    }

    if (!used)
    {
      *slot_out = (int32_t)slot;
      return TARS_OK;
    }
  }

  return TARS_ERR_NO_SLOT;
}

tars_status_t TarsStorage_AllocSdramExec(uint32_t size, uint32_t *addr_out)
{
  uint32_t aligned = (size + 7U) & ~7U;

  if ((s_sdram_exec_next + aligned) > TARS_SDRAM_EXEC_SIZE)
  {
    s_sdram_exec_next = 0U;
  }

  if ((s_sdram_exec_next + aligned) > TARS_SDRAM_EXEC_SIZE)
  {
    return TARS_ERR_NO_SLOT;
  }

  *addr_out = TARS_SDRAM_EXEC_BASE + s_sdram_exec_next;
  s_sdram_exec_next += aligned;
  return TARS_OK;
}

tars_status_t TarsStorage_AllocLuaOffset(uint32_t size, uint32_t *offset_out)
{
  uint32_t aligned = (size + 3U) & ~3U;

  if ((s_lua_pool_offset + aligned) > TARS_LUA_POOL_SIZE)
  {
    return TARS_ERR_NO_SLOT;
  }

  *offset_out = s_lua_pool_offset;
  s_lua_pool_offset += aligned;
  return TARS_OK;
}

tars_status_t TarsStorage_WriteNativeSlot(uint32_t slot_index,
                                          const uint8_t *blob,
                                          uint32_t blob_size)
{
  uint32_t dst;

  if ((slot_index >= TARS_NATIVE_SLOT_COUNT) || (blob == NULL) ||
      (blob_size == 0U) || (blob_size > TARS_NATIVE_SLOT_MAX))
  {
    return TARS_ERR_PARAM;
  }

  dst = TarsNativeSlotAddress(slot_index);

  if (storage_flash_erase_sector(8U + slot_index) != TARS_OK)
  {
    return TARS_ERR_FLASH;
  }

  return storage_flash_program(dst, blob, blob_size);
}

static uint8_t s_lua_pool_erased;

tars_status_t TarsStorage_PrepareLuaWrite(uint32_t offset, uint32_t size)
{
  if ((offset + size) > TARS_LUA_POOL_SIZE)
  {
    return TARS_ERR_PARAM;
  }

  if (s_lua_pool_erased != 0U)
  {
    return TARS_OK;
  }

  if (offset > 0U)
  {
    s_lua_pool_erased = 1U;
    return TARS_OK;
  }

  if (storage_flash_erase_sector(TARS_LUA_POOL_SECTOR) != TARS_OK)
  {
    return TARS_ERR_FLASH;
  }

  s_lua_pool_erased = 1U;
  s_lua_pool_offset = 0U;
  return TARS_OK;
}

tars_status_t TarsStorage_WriteLuaPool(uint32_t offset,
                                       const uint8_t *data,
                                       uint32_t size)
{
  if ((data == NULL) || ((offset + size) > TARS_LUA_POOL_SIZE))
  {
    return TARS_ERR_PARAM;
  }

  return storage_flash_program(TARS_LUA_POOL_BASE + offset, data, size);
}

tars_status_t TarsStorage_AddEntry(const tars_storage_entry_t *entry)
{
  tars_status_t status;

  if ((entry == NULL) || (s_catalog_count >= TARS_STORAGE_MAX_ENTRIES))
  {
    return TARS_ERR_STATE;
  }

  s_catalog[s_catalog_count] = *(const tars_catalog_entry_t *)(const void *)entry;
  s_catalog_count++;

  status = storage_catalog_save();
  if (status != TARS_OK)
  {
    s_catalog_count--;
    return status;
  }

  return TARS_OK;
}

tars_status_t TarsStorage_UpdateEntry(uint32_t index, const tars_storage_entry_t *entry)
{
  if ((entry == NULL) || (index >= s_catalog_count))
  {
    return TARS_ERR_PARAM;
  }

  s_catalog[index] = *(const tars_catalog_entry_t *)(const void *)entry;
  s_catalog_dirty = 1U;
  return TARS_OK;
}

tars_status_t TarsStorage_FlushCatalogIfDirty(int *flushed_out)
{
  tars_status_t status;

  if (flushed_out != NULL)
  {
    *flushed_out = 0;
  }

  if (s_catalog_dirty == 0U)
  {
    return TARS_OK;
  }

  status = storage_catalog_save();
  if (status != TARS_OK)
  {
    return status;
  }

  s_catalog_dirty = 0U;

  if (flushed_out != NULL)
  {
    *flushed_out = 1;
  }

  return TARS_OK;
}

uint32_t TarsStorage_GetEntryCount(void)
{
  return s_catalog_count;
}

const tars_storage_entry_t *TarsStorage_GetEntry(uint32_t index)
{
  if (index >= s_catalog_count)
  {
    return NULL;
  }

  return (const tars_storage_entry_t *)(const void *)&s_catalog[index];
}

tars_status_t TarsStorage_RemoveEntry(const char *name)
{
  int32_t idx = TarsStorage_FindByName(name);
  uint32_t i;
  tars_status_t status;
  uint32_t old_count;

  if (idx < 0)
  {
    return TARS_ERR_NOT_FOUND;
  }

  old_count = s_catalog_count;

  for (i = (uint32_t)idx; (i + 1U) < s_catalog_count; i++)
  {
    s_catalog[i] = s_catalog[i + 1U];
  }

  s_catalog_count--;

  status = storage_catalog_save();
  if (status != TARS_OK)
  {
    s_catalog_count = old_count;
    return status;
  }

  return TARS_OK;
}

const uint8_t *TarsStorage_GetBlobPtr(uint32_t blob_addr)
{
  return (const uint8_t *)(const void *)blob_addr;
}

void TarsStorage_GetCatalogDiag(tars_catalog_diag_t *diag)
{
  const tars_catalog_hdr_t *hdr = (const tars_catalog_hdr_t *)(const void *)TARS_CATALOG_BASE;
  tars_status_t status;

  if (diag == NULL)
  {
    return;
  }

  diag->magic = hdr->magic;
  diag->entry_count = hdr->entry_count;
  diag->stored_crc = hdr->crc32;
  diag->validate_status = storage_catalog_validate();

  if (hdr->magic != TARS_CATALOG_MAGIC)
  {
    diag->computed_crc = 0U;
    return;
  }

  status = diag->validate_status;
  if (status == TARS_ERR_CRC)
  {
    diag->computed_crc = storage_catalog_compute_crc(
      hdr,
      (const tars_catalog_entry_t *)(const void *)(TARS_CATALOG_BASE + sizeof(tars_catalog_hdr_t)),
      hdr->entry_count);
  }
  else
  {
    diag->computed_crc = hdr->crc32;
  }

  (void)status;
}
