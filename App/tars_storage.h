#ifndef TARS_STORAGE_H
#define TARS_STORAGE_H

#include "tars_app.h"

#define TARS_STORAGE_MAX_ENTRIES  16U

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
} tars_storage_entry_t;

typedef struct {
  uint32_t        magic;
  uint32_t        entry_count;
  uint32_t        stored_crc;
  uint32_t        computed_crc;
  tars_status_t   validate_status;
} tars_catalog_diag_t;

void TarsStorage_Init(void);
int32_t TarsStorage_FindByName(const char *name);
tars_status_t TarsStorage_FindFreeNativeSlot(int32_t *slot_out);
tars_status_t TarsStorage_AllocSdramExec(uint32_t size, uint32_t *addr_out);
tars_status_t TarsStorage_AllocLuaOffset(uint32_t size, uint32_t *offset_out);
tars_status_t TarsStorage_WriteNativeSlot(uint32_t slot_index,
                                          const uint8_t *blob,
                                          uint32_t blob_size);
tars_status_t TarsStorage_PrepareLuaWrite(uint32_t offset, uint32_t size);
tars_status_t TarsStorage_WriteLuaPool(uint32_t offset,
                                       const uint8_t *data,
                                       uint32_t size);
tars_status_t TarsStorage_AddEntry(const tars_storage_entry_t *entry);
tars_status_t TarsStorage_UpdateEntry(uint32_t index, const tars_storage_entry_t *entry);
tars_status_t TarsStorage_FlushCatalogIfDirty(int *flushed_out);
uint32_t TarsStorage_GetEntryCount(void);
const tars_storage_entry_t *TarsStorage_GetEntry(uint32_t index);
tars_status_t TarsStorage_RemoveEntry(const char *name);
const uint8_t *TarsStorage_GetBlobPtr(uint32_t blob_addr);
void TarsStorage_GetCatalogDiag(tars_catalog_diag_t *diag);

#endif /* TARS_STORAGE_H */
