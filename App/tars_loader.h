#ifndef TARS_LOADER_H
#define TARS_LOADER_H

#include "tars_app.h"

typedef struct {
  uint16_t flags;
  uint32_t link_base;
  uint32_t load_addr;
  uint32_t exec_addr;
  uint32_t data_addr;
  uint32_t text_size;
  uint32_t data_size;
  uint32_t bss_size;
  uint32_t entry_offset;
} tars_loader_result_t;

tars_status_t TarsLoader_VerifyNativeBlob(const uint8_t *blob, uint32_t blob_size);
tars_status_t TarsLoader_LoadNative(const uint8_t *blob,
                                    uint32_t blob_size,
                                    tars_loader_result_t *result);
tars_status_t TarsLoader_VerifyLuaBlob(const uint8_t *blob, uint32_t blob_size);
tars_native_entry_fn TarsLoader_MakeEntry(uint32_t exec_addr, uint32_t entry_offset);

#endif /* TARS_LOADER_H */
