#include "tars_loader.h"
#include "tars_crc.h"
#include "main.h"
#include <stddef.h>
#include <string.h>

static uint32_t loader_crc32_blob(const uint8_t *hdr_prefix,
                                  uint32_t hdr_prefix_len,
                                  const uint8_t *payload,
                                  uint32_t payload_len)
{
  uint32_t crc = 0xFFFFFFFFUL;

  crc = TarsCrc32(hdr_prefix, hdr_prefix_len, crc);
  crc = TarsCrc32(payload, payload_len, crc);
  return crc ^ 0xFFFFFFFFUL;
}

static tars_status_t loader_validate_header(const tars_app_hdr_t *hdr, uint32_t blob_size)
{
  if (hdr->magic != TARS_APP_MAGIC)
  {
    return TARS_ERR_MAGIC;
  }

  if (hdr->api_version > TARS_API_VERSION)
  {
    return TARS_ERR_API_VERSION;
  }

  if (hdr->header_size < sizeof(tars_app_hdr_t))
  {
    return TARS_ERR_PARAM;
  }

  if ((hdr->flags & (TARS_LOAD_FIXED | TARS_LOAD_RELOC)) == 0U)
  {
    return TARS_ERR_PARAM;
  }

  if ((hdr->flags & (TARS_LOAD_FIXED | TARS_LOAD_RELOC)) ==
      (TARS_LOAD_FIXED | TARS_LOAD_RELOC))
  {
    return TARS_ERR_PARAM;
  }

  if (hdr->payload_size > (blob_size - hdr->header_size))
  {
    return TARS_ERR_PARAM;
  }

  if (hdr->reloc_count > TARS_RELOC_MAX)
  {
    return TARS_ERR_RELOC;
  }

  return TARS_OK;
}

static tars_status_t loader_apply_relocs(uint32_t load_base,
                                         uint8_t *image,
                                         uint32_t image_size,
                                         const tars_reloc_t *relocs,
                                         uint32_t reloc_count)
{
  uint32_t i;

  for (i = 0U; i < reloc_count; i++)
  {
    uint32_t offset = relocs[i].offset;

    if ((offset + sizeof(uint32_t)) > image_size)
    {
      return TARS_ERR_RELOC;
    }

    if (relocs[i].type == TARS_RELOC_ABS32)
    {
      uint32_t *word = (uint32_t *)(void *)(image + offset);
      *word += load_base;
    }
    else
    {
      return TARS_ERR_RELOC;
    }
  }

  return TARS_OK;
}

static void loader_zero_bss(uint32_t bss_addr, uint32_t bss_size)
{
  if ((bss_addr != 0U) && (bss_size != 0U))
  {
    memset((void *)bss_addr, 0, bss_size);
  }
}

tars_status_t TarsLoader_VerifyNativeBlob(const uint8_t *blob, uint32_t blob_size)
{
  const tars_app_hdr_t *hdr;
  uint32_t crc;
  uint32_t expected_payload;

  if ((blob == NULL) || (blob_size < sizeof(tars_app_hdr_t)))
  {
    return TARS_ERR_PARAM;
  }

  hdr = (const tars_app_hdr_t *)(const void *)blob;

  if (loader_validate_header(hdr, blob_size) != TARS_OK)
  {
    return TARS_ERR_PARAM;
  }

  expected_payload = hdr->text_size + hdr->data_size +
                     (hdr->reloc_count * (uint32_t)sizeof(tars_reloc_t));

  if (hdr->payload_size != expected_payload)
  {
    return TARS_ERR_PARAM;
  }

  crc = loader_crc32_blob(blob,
                          (uint32_t)offsetof(tars_app_hdr_t, crc32),
                          blob + hdr->header_size,
                          hdr->payload_size);

  if (crc != hdr->crc32)
  {
    return TARS_ERR_CRC;
  }

  return TARS_OK;
}

tars_status_t TarsLoader_LoadNative(const uint8_t *blob,
                                    uint32_t blob_size,
                                    tars_loader_result_t *result)
{
  const tars_app_hdr_t *hdr;
  const uint8_t *payload;
  const tars_reloc_t *relocs;
  tars_status_t status;
  uint32_t image_size;

  if ((blob == NULL) || (result == NULL))
  {
    return TARS_ERR_PARAM;
  }

  memset(result, 0, sizeof(*result));

  status = TarsLoader_VerifyNativeBlob(blob, blob_size);
  if (status != TARS_OK)
  {
    return status;
  }

  hdr = (const tars_app_hdr_t *)(const void *)blob;
  payload = blob + hdr->header_size;
  relocs = (const tars_reloc_t *)(const void *)(payload + hdr->text_size + hdr->data_size);

  result->flags = hdr->flags;
  result->entry_offset = hdr->entry_offset;
  result->link_base = hdr->link_base;
  result->bss_size = hdr->bss_size;
  result->text_size = hdr->text_size;
  result->data_size = hdr->data_size;
  image_size = hdr->text_size + hdr->data_size;

  if ((hdr->flags & TARS_LOAD_FIXED) != 0U)
  {
    result->exec_addr = hdr->link_base;
    result->data_addr = hdr->link_base + hdr->text_size;
    result->load_addr = hdr->link_base;

    if (image_size > 0U)
    {
      memcpy((void *)result->exec_addr, payload, image_size);
    }

    loader_zero_bss(result->data_addr + hdr->data_size, hdr->bss_size);
    return TARS_OK;
  }

  if ((hdr->flags & TARS_EXEC_SDRAM) != 0U)
  {
    result->exec_addr = result->load_addr;
  }
  else
  {
    result->exec_addr = result->load_addr;
  }

  result->data_addr = result->exec_addr + hdr->text_size;

  if (image_size > 0U)
  {
    memcpy((void *)result->exec_addr, payload, image_size);
  }

  if ((hdr->flags & TARS_LOAD_RELOC) != 0U)
  {
    status = loader_apply_relocs(result->exec_addr,
                                 (uint8_t *)(void *)result->exec_addr,
                                 image_size,
                                 relocs,
                                 hdr->reloc_count);
    if (status != TARS_OK)
    {
      return status;
    }
  }

  loader_zero_bss(result->data_addr + hdr->data_size, hdr->bss_size);
  return TARS_OK;
}

tars_status_t TarsLoader_VerifyLuaBlob(const uint8_t *blob, uint32_t blob_size)
{
  const tars_lua_hdr_t *hdr;
  uint32_t crc;

  if ((blob == NULL) || (blob_size < sizeof(tars_lua_hdr_t)))
  {
    return TARS_ERR_PARAM;
  }

  hdr = (const tars_lua_hdr_t *)(const void *)blob;

  if (hdr->magic != TARS_LUA_MAGIC)
  {
    return TARS_ERR_MAGIC;
  }

  if (hdr->header_size < sizeof(tars_lua_hdr_t))
  {
    return TARS_ERR_PARAM;
  }

  if (hdr->lua_size > (blob_size - hdr->header_size))
  {
    return TARS_ERR_PARAM;
  }

  crc = loader_crc32_blob(blob,
                          (uint32_t)offsetof(tars_lua_hdr_t, crc32),
                          blob + hdr->header_size,
                          hdr->lua_size);

  if (crc != hdr->crc32)
  {
    return TARS_ERR_CRC;
  }

  return TARS_OK;
}

tars_native_entry_fn TarsLoader_MakeEntry(uint32_t exec_addr, uint32_t entry_offset)
{
  return (tars_native_entry_fn)(void *)(exec_addr + entry_offset);
}
