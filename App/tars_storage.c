#include "tars_storage.h"
#include "tars_lfs.h"
#include "tars_flash_hw.h"
#include "tars_app.h"
#include "tars_crc.h"
#include "tars_platform.h"
#include <stddef.h>
#include <stdio.h>
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
static volatile uint8_t s_catalog_dirty;

#define TARS_CATALOG_HDR_CRC_LEN  ((uint32_t)offsetof(tars_catalog_hdr_t, crc32))

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

static tars_status_t storage_catalog_validate_buf(const tars_catalog_hdr_t *hdr,
                                                  const tars_catalog_entry_t *entries)
{
  uint32_t crc;

  if (hdr->magic != TARS_CATALOG_MAGIC)
  {
    return TARS_ERR_NOT_FOUND;
  }

  if (hdr->entry_count > TARS_STORAGE_MAX_ENTRIES)
  {
    return TARS_ERR_CRC;
  }

  crc = storage_catalog_compute_crc(hdr, entries, hdr->entry_count);
  if (crc != hdr->crc32)
  {
    return TARS_ERR_CRC;
  }

  return TARS_OK;
}

static tars_status_t storage_catalog_save(void)
{
  tars_catalog_hdr_t hdr = {0};
  uint8_t file_buf[sizeof(tars_catalog_hdr_t) +
                   TARS_STORAGE_MAX_ENTRIES * sizeof(tars_catalog_entry_t)];

  hdr.magic = TARS_CATALOG_MAGIC;
  hdr.version = 1U;
  hdr.entry_count = s_catalog_count;
  hdr.crc32 = storage_catalog_compute_crc(&hdr, s_catalog, s_catalog_count);

  memcpy(file_buf, &hdr, sizeof(hdr));
  if (s_catalog_count > 0U)
  {
    memcpy(file_buf + sizeof(hdr),
           s_catalog,
           s_catalog_count * sizeof(tars_catalog_entry_t));
  }

  return TarsLfs_WriteFile(TARS_LFS_PATH_CATALOG,
                         file_buf,
                         (uint32_t)(sizeof(hdr) +
                                    s_catalog_count * sizeof(tars_catalog_entry_t)));
}

static tars_status_t storage_catalog_load(void)
{
  tars_catalog_hdr_t hdr;
  uint8_t file_buf[sizeof(tars_catalog_hdr_t) +
                   TARS_STORAGE_MAX_ENTRIES * sizeof(tars_catalog_entry_t)];
  uint32_t file_size = 0U;
  tars_status_t status;

  status = TarsLfs_ReadFile(TARS_LFS_PATH_CATALOG, file_buf, sizeof(file_buf), &file_size);
  if (status != TARS_OK)
  {
    return status;
  }

  if (file_size < sizeof(tars_catalog_hdr_t))
  {
    return TARS_ERR_CRC;
  }

  memcpy(&hdr, file_buf, sizeof(hdr));
  status = storage_catalog_validate_buf(
    &hdr,
    (const tars_catalog_entry_t *)(const void *)(file_buf + sizeof(tars_catalog_hdr_t)));
  if (status != TARS_OK)
  {
    return status;
  }

  s_catalog_count = hdr.entry_count;
  if (s_catalog_count > 0U)
  {
    memcpy(s_catalog,
           file_buf + sizeof(tars_catalog_hdr_t),
           s_catalog_count * sizeof(tars_catalog_entry_t));
  }

  return TARS_OK;
}

static uint8_t s_storage_inited;

void TarsStorage_Init(void)
{
  if (s_storage_inited != 0U)
  {
    return;
  }

  s_catalog_count = 0U;
  s_sdram_exec_next = 0U;
  s_catalog_dirty = 0U;
  memset(s_catalog, 0, sizeof(s_catalog));

  (void)TarsLfs_Init();
  (void)storage_catalog_load();
  s_storage_inited = 1U;
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

  if (TarsFlash_EraseSector(TarsNativeSlotSector(slot_index)) != TARS_OK)
  {
    return TARS_ERR_FLASH;
  }

  return TarsFlash_Program(dst, blob, blob_size);
}

tars_status_t TarsStorage_WriteLuaBlob(const char *name,
                                       const uint8_t *blob,
                                       uint32_t blob_size)
{
  char path[48];

  if ((name == NULL) || (blob == NULL) || (blob_size == 0U))
  {
    return TARS_ERR_PARAM;
  }

  (void)snprintf(path, sizeof(path), "%s/%s.tlua", TARS_LFS_PATH_APPS, name);
  return TarsLfs_WriteFile(path, blob, blob_size);
}

tars_status_t TarsStorage_ReadLuaBlob(const char *name,
                                      uint8_t *blob,
                                      uint32_t max_size,
                                      uint32_t *out_size)
{
  char path[48];

  if ((name == NULL) || (blob == NULL) || (max_size == 0U))
  {
    return TARS_ERR_PARAM;
  }

  (void)snprintf(path, sizeof(path), "%s/%s.tlua", TARS_LFS_PATH_APPS, name);
  return TarsLfs_ReadFile(path, blob, max_size, out_size);
}

tars_status_t TarsStorage_RemoveLuaBlob(const char *name)
{
  char path[48];

  if (name == NULL)
  {
    return TARS_ERR_PARAM;
  }

  (void)snprintf(path, sizeof(path), "%s/%s.tlua", TARS_LFS_PATH_APPS, name);
  return TarsLfs_RemoveFile(path);
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
  tars_status_t status;
  tars_catalog_entry_t prev;

  if ((entry == NULL) || (index >= s_catalog_count))
  {
    return TARS_ERR_PARAM;
  }

  prev = s_catalog[index];
  s_catalog[index] = *(const tars_catalog_entry_t *)(const void *)entry;

  status = storage_catalog_save();
  if (status != TARS_OK)
  {
    s_catalog[index] = prev;
    return status;
  }

  s_catalog_dirty = 0U;
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
  const tars_storage_entry_t *entry;

  if (idx < 0)
  {
    return TARS_ERR_NOT_FOUND;
  }

  entry = TarsStorage_GetEntry((uint32_t)idx);
  if ((entry != NULL) && (entry->app_type == TARS_APP_TYPE_LUA))
  {
    (void)TarsStorage_RemoveLuaBlob(name);
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
  tars_catalog_hdr_t hdr = {0};

  if (diag == NULL)
  {
    return;
  }

  hdr.magic = TARS_CATALOG_MAGIC;
  hdr.entry_count = s_catalog_count;
  hdr.crc32 = storage_catalog_compute_crc(&hdr, s_catalog, s_catalog_count);

  diag->magic = hdr.magic;
  diag->entry_count = hdr.entry_count;
  diag->stored_crc = hdr.crc32;
  diag->computed_crc = hdr.crc32;
  diag->validate_status = storage_catalog_validate_buf(&hdr, s_catalog);
}
