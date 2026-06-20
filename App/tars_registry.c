#include "tars_app.h"
#include "tars_loader.h"
#include "tars_manifest.h"
#include "tars_storage.h"
#include "tars_lua.h"
#include "cmsis_os.h"
#include <stdio.h>
#include <string.h>

#define TARS_RUNTIME_MAX  (TARS_STORAGE_MAX_ENTRIES + 8U)

static tars_app_runtime_t s_runtime[TARS_RUNTIME_MAX];
static uint32_t s_runtime_count;
static tars_builtin_app_t s_builtins[8];
static uint32_t s_builtin_count;
static uint8_t s_last_timeslice;

static void registry_release_app(const tars_app_runtime_t *rt)
{
  uint32_t j;

  if (rt == NULL)
  {
    return;
  }

  for (j = 0U; j < s_builtin_count; j++)
  {
    if (strcmp(s_builtins[j].name, rt->name) == 0)
    {
      if (s_builtins[j].release != NULL)
      {
        s_builtins[j].release();
      }
      return;
    }
  }

  if (rt->app_type == TARS_APP_TYPE_LUA)
  {
    TarsLua_OnRelease(rt->name);
  }
}

static void registry_acquire_app(const tars_app_runtime_t *rt)
{
  uint32_t j;
  const tars_api_t *api = TarsApp_GetApi();

  if (rt == NULL)
  {
    return;
  }

  for (j = 0U; j < s_builtin_count; j++)
  {
    if (strcmp(s_builtins[j].name, rt->name) == 0)
    {
      if (s_builtins[j].init != NULL)
      {
        s_builtins[j].init(api);
      }
      return;
    }
  }

  if (rt->app_type == TARS_APP_TYPE_LUA)
  {
    TarsLua_OnAcquire(rt->name);
  }
}

static void registry_release_timeslice(uint8_t timeslice)
{
  uint32_t i;

  for (i = 0U; i < s_runtime_count; i++)
  {
    if (!s_runtime[i].submitted || (s_runtime[i].timeslice != timeslice))
    {
      continue;
    }

    registry_release_app(&s_runtime[i]);
  }
}

static void registry_run_timeslice(uint8_t timeslice)
{
  uint32_t i;

  for (i = 0U; i < s_runtime_count; i++)
  {
    if (!s_runtime[i].submitted || (s_runtime[i].timeslice != timeslice))
    {
      continue;
    }

    registry_acquire_app(&s_runtime[i]);

    if (s_runtime[i].app_type == TARS_APP_TYPE_NATIVE)
    {
      uint32_t j;

      for (j = 0U; j < s_builtin_count; j++)
      {
        if (strcmp(s_builtins[j].name, s_runtime[i].name) == 0)
        {
          if (s_builtins[j].tick != NULL)
          {
            s_builtins[j].tick();
          }
          break;
        }
      }

      if (j >= s_builtin_count)
      {
        (void)TarsApp_RunOnce(s_runtime[i].name);
      }
    }
    else if (s_runtime[i].app_type == TARS_APP_TYPE_LUA)
    {
      TarsLua_RequestRun(s_runtime[i].name);
    }
  }
}

static tars_status_t registry_fill_runtime_from_storage(void)
{
  uint32_t i;
  uint32_t count = TarsStorage_GetEntryCount();

  s_runtime_count = 0U;

  for (i = 0U; i < count; i++)
  {
    const tars_storage_entry_t *entry = TarsStorage_GetEntry(i);

    if ((entry == NULL) || (s_runtime_count >= TARS_RUNTIME_MAX))
    {
      break;
    }

    s_runtime[s_runtime_count].app_type = entry->app_type;
    s_runtime[s_runtime_count].timeslice = entry->timeslice;
    s_runtime[s_runtime_count].submitted = entry->submitted;
    s_runtime[s_runtime_count].loaded = 0U;
    s_runtime[s_runtime_count].flags = entry->flags;
    s_runtime[s_runtime_count].app_version = entry->app_version;
    s_runtime[s_runtime_count].manifest = entry->manifest;
    s_runtime[s_runtime_count].exec_addr = entry->exec_addr;
    s_runtime[s_runtime_count].slot_index = entry->slot_index;
    s_runtime[s_runtime_count].entry = NULL;
    strncpy(s_runtime[s_runtime_count].name, entry->name, TARS_NAME_MAX);
    s_runtime[s_runtime_count].name[TARS_NAME_MAX - 1U] = '\0';
    s_runtime_count++;
  }

  return TARS_OK;
}

static tars_status_t registry_load_native_runtime(tars_app_runtime_t *rt,
                                                  const uint8_t *blob,
                                                  uint32_t blob_size)
{
  const tars_app_hdr_t *hdr = (const tars_app_hdr_t *)(const void *)blob;
  tars_loader_result_t load = {0};
  tars_status_t status;

  if ((hdr->flags & TARS_LOAD_FIXED) != 0U)
  {
    load.load_addr = hdr->link_base;
  }
  else if ((hdr->flags & TARS_EXEC_SDRAM) != 0U)
  {
    status = TarsStorage_AllocSdramExec(hdr->text_size + hdr->data_size + hdr->bss_size,
                                        &load.load_addr);
    if (status != TARS_OK)
    {
      return status;
    }
  }
  else
  {
    int32_t slot = -1;

    if (TarsStorage_FindFreeNativeSlot(&slot) != TARS_OK)
    {
      return TARS_ERR_NO_SLOT;
    }

    load.load_addr = TarsNativeSlotAddress((uint32_t)slot);
    rt->slot_index = (uint32_t)slot;
  }

  status = TarsLoader_LoadNative(blob, blob_size, &load);
  if (status != TARS_OK)
  {
    return status;
  }

  rt->exec_addr = load.exec_addr;
  rt->entry = TarsLoader_MakeEntry(load.exec_addr, load.entry_offset);
  rt->loaded = 1U;
  return TARS_OK;
}

static tars_status_t registry_validate_submit(const char *name, const tars_manifest_t *manifest)
{
  tars_status_t status;

  status = TarsManifest_Validate(manifest);
  if (status != TARS_OK)
  {
    return status;
  }

  return TarsManifest_CheckConflict(name, manifest, s_runtime, s_runtime_count);
}

void TarsApp_Init(void)
{
  TarsApi_Init();
  TarsStorage_Init();
  TarsManifest_ClearWhitelist();
  registry_fill_runtime_from_storage();
}

void TarsApp_RegisterBuiltin(const tars_builtin_app_t *app)
{
  if ((app == NULL) || (s_builtin_count >= 8U) || (s_runtime_count >= TARS_RUNTIME_MAX))
  {
    return;
  }

  s_builtins[s_builtin_count++] = *app;

  s_runtime[s_runtime_count].app_type = TARS_APP_TYPE_NATIVE;
  s_runtime[s_runtime_count].timeslice = app->timeslice;
  s_runtime[s_runtime_count].submitted = 0U;
  s_runtime[s_runtime_count].loaded = 1U;
  s_runtime[s_runtime_count].manifest = app->manifest;
  s_runtime[s_runtime_count].entry = NULL;
  strncpy(s_runtime[s_runtime_count].name, app->name, TARS_NAME_MAX);
  s_runtime[s_runtime_count].name[TARS_NAME_MAX - 1U] = '\0';
  s_runtime_count++;
}

tars_status_t TarsApp_InstallNative(const uint8_t *blob, uint32_t blob_size, int32_t slot_hint)
{
  const tars_app_hdr_t *hdr;
  tars_storage_entry_t entry = {0};
  int32_t slot = slot_hint;
  tars_status_t status;
  tars_app_runtime_t rt = {0};

  status = TarsLoader_VerifyNativeBlob(blob, blob_size);
  if (status != TARS_OK)
  {
    return status;
  }

  hdr = (const tars_app_hdr_t *)(const void *)blob;

  if (TarsStorage_FindByName(hdr->name) >= 0)
  {
    status = TarsStorage_RemoveEntry(hdr->name);
    if (status != TARS_OK)
    {
      return status;
    }
  }

  if ((hdr->flags & TARS_LOAD_FIXED) != 0U)
  {
    slot = (int32_t)((hdr->link_base - TARS_NATIVE_SLOT_BASE) / TARS_NATIVE_SLOT_STRIDE);

    if ((slot < 0) || (slot >= (int32_t)TARS_NATIVE_SLOT_COUNT))
    {
      return TARS_ERR_PARAM;
    }

    status = TarsStorage_WriteNativeSlot((uint32_t)slot, blob, blob_size);
    if (status != TARS_OK)
    {
      return status;
    }

    rt.slot_index = (uint32_t)slot;
    rt.exec_addr = hdr->link_base;
  }
  else
  {
    if (slot < 0)
    {
      status = TarsStorage_FindFreeNativeSlot(&slot);
      if (status != TARS_OK)
      {
        return status;
      }
    }

    status = TarsStorage_WriteNativeSlot((uint32_t)slot, blob, blob_size);
    if (status != TARS_OK)
    {
      return status;
    }

    rt.slot_index = (uint32_t)slot;
    status = registry_load_native_runtime(&rt, blob, blob_size);
    if (status != TARS_OK)
    {
      return status;
    }
  }

  strncpy(entry.name, hdr->name, TARS_NAME_MAX);
  entry.app_type = TARS_APP_TYPE_NATIVE;
  entry.installed = 1U;
  entry.submitted = 0U;
  entry.timeslice = hdr->timeslice;
  entry.flags = hdr->flags;
  entry.app_version = hdr->app_version;
  entry.blob_addr = TarsNativeSlotAddress((uint32_t)slot);
  entry.blob_size = blob_size;
  entry.exec_addr = rt.exec_addr;
  entry.slot_index = rt.slot_index;
  entry.manifest = hdr->manifest;

  status = TarsStorage_AddEntry(&entry);
  if (status != TARS_OK)
  {
    return status;
  }

  registry_fill_runtime_from_storage();
  return TARS_OK;
}

tars_status_t TarsApp_InstallLua(const uint8_t *blob, uint32_t blob_size)
{
  const tars_lua_hdr_t *hdr;
  tars_storage_entry_t entry = {0};
  uint32_t offset = 0U;
  tars_status_t status;

  status = TarsLoader_VerifyLuaBlob(blob, blob_size);
  if (status != TARS_OK)
  {
    return status;
  }

  hdr = (const tars_lua_hdr_t *)(const void *)blob;

  if (TarsStorage_FindByName(hdr->name) >= 0)
  {
    status = TarsStorage_RemoveEntry(hdr->name);
    if (status != TARS_OK)
    {
      return status;
    }
  }

  status = TarsStorage_AllocLuaOffset(blob_size, &offset);
  if (status != TARS_OK)
  {
    return status;
  }

  status = TarsStorage_PrepareLuaWrite(offset, blob_size);
  if (status != TARS_OK)
  {
    return status;
  }

  status = TarsStorage_WriteLuaPool(offset, blob, blob_size);
  if (status != TARS_OK)
  {
    return status;
  }

  strncpy(entry.name, hdr->name, TARS_NAME_MAX);
  entry.app_type = TARS_APP_TYPE_LUA;
  entry.installed = 1U;
  entry.submitted = 0U;
  entry.timeslice = hdr->timeslice;
  entry.flags = hdr->flags;
  entry.app_version = hdr->app_version;
  entry.blob_addr = TARS_LUA_POOL_BASE + offset;
  entry.blob_size = blob_size;
  entry.manifest = hdr->manifest;

  status = TarsStorage_AddEntry(&entry);
  if (status != TARS_OK)
  {
    return status;
  }

  registry_fill_runtime_from_storage();
  return TARS_OK;
}

tars_status_t TarsApp_Submit(const char *name)
{
  int32_t storage_idx = TarsStorage_FindByName(name);
  uint32_t i;
  tars_status_t status;

  if (storage_idx < 0)
  {
    return TARS_ERR_NOT_FOUND;
  }

  for (i = 0U; i < s_runtime_count; i++)
  {
    if (strcmp(s_runtime[i].name, name) != 0)
    {
      continue;
    }

    status = registry_validate_submit(name, &s_runtime[i].manifest);
    if (status != TARS_OK)
    {
      return status;
    }

    s_runtime[i].submitted = 1U;

    {
      tars_storage_entry_t entry = *(const tars_storage_entry_t *)TarsStorage_GetEntry((uint32_t)storage_idx);
      entry.submitted = 1U;
      TarsStorage_UpdateEntry((uint32_t)storage_idx, &entry);
    }

    return TARS_OK;
  }

  return TARS_ERR_NOT_FOUND;
}

tars_status_t TarsApp_Revoke(const char *name)
{
  int32_t storage_idx = TarsStorage_FindByName(name);
  uint32_t i;

  if (storage_idx < 0)
  {
    return TARS_ERR_NOT_FOUND;
  }

  for (i = 0U; i < s_runtime_count; i++)
  {
    if (strcmp(s_runtime[i].name, name) == 0)
    {
      s_runtime[i].submitted = 0U;
      break;
    }
  }

  {
    tars_storage_entry_t entry = *(const tars_storage_entry_t *)TarsStorage_GetEntry((uint32_t)storage_idx);
    entry.submitted = 0U;
    TarsStorage_UpdateEntry((uint32_t)storage_idx, &entry);
  }

  return TARS_OK;
}

tars_status_t TarsApp_Uninstall(const char *name)
{
  tars_status_t status;

  if (TarsStorage_FindByName(name) < 0)
  {
    return TARS_ERR_NOT_FOUND;
  }

  status = TarsStorage_RemoveEntry(name);
  if (status != TARS_OK)
  {
    return status;
  }

  registry_fill_runtime_from_storage();
  return TARS_OK;
}

tars_status_t TarsApp_RunOnce(const char *name)
{
  uint32_t i;

  for (i = 0U; i < s_runtime_count; i++)
  {
    if (strcmp(s_runtime[i].name, name) != 0)
    {
      continue;
    }

    if (s_runtime[i].app_type == TARS_APP_TYPE_NATIVE)
    {
      const tars_storage_entry_t *entry = TarsStorage_GetEntry(TarsStorage_FindByName(name));
      tars_app_runtime_t rt = s_runtime[i];

      if ((entry != NULL) && (rt.entry == NULL))
      {
        registry_load_native_runtime(&rt,
                                     TarsStorage_GetBlobPtr(entry->blob_addr),
                                     entry->blob_size);
      }

      if (rt.entry != NULL)
      {
        rt.entry(TarsApp_GetApi());
      }

      return TARS_OK;
    }

    if (s_runtime[i].app_type == TARS_APP_TYPE_LUA)
    {
      TarsLua_RequestRun(name);
      return TARS_OK;
    }

    return TARS_ERR_STATE;
  }

  return TARS_ERR_NOT_FOUND;
}

tars_status_t TarsApp_List(char *out, uint32_t out_size)
{
  uint32_t i;
  int written = 0;

  if ((out == NULL) || (out_size == 0U))
  {
    return TARS_ERR_PARAM;
  }

  out[0] = '\0';

  if (s_runtime_count == 0U)
  {
    (void)snprintf(out, out_size, "(none)\r\n");
    return TARS_OK;
  }

  for (i = 0U; i < s_runtime_count; i++)
  {
    written += snprintf(out + written,
                        (written < (int)out_size) ? (out_size - (uint32_t)written) : 0U,
                        "%s type=%u slot=%lu ts=%u sub=%u\r\n",
                        s_runtime[i].name,
                        s_runtime[i].app_type,
                        (unsigned long)s_runtime[i].slot_index,
                        s_runtime[i].timeslice,
                        s_runtime[i].submitted);

    if ((uint32_t)written >= out_size)
    {
      break;
    }
  }

  return TARS_OK;
}

tars_status_t TarsApp_ListSlots(char *out, uint32_t out_size)
{
  uint32_t slot;
  int written = 0;

  if ((out == NULL) || (out_size == 0U))
  {
    return TARS_ERR_PARAM;
  }

  out[0] = '\0';

  for (slot = 0U; slot < TARS_NATIVE_SLOT_COUNT; slot++)
  {
    written += snprintf(out + written,
                        (written < (int)out_size) ? (out_size - (uint32_t)written) : 0U,
                        "slot%lu addr=0x%08lX size=%u\r\n",
                        (unsigned long)slot,
                        (unsigned long)TarsNativeSlotAddress(slot),
                        (unsigned)TARS_NATIVE_SLOT_MAX);

    if ((uint32_t)written >= out_size)
    {
      break;
    }
  }

  written += snprintf(out + written,
                      (written < (int)out_size) ? (out_size - (uint32_t)written) : 0U,
                      "sdram_exec=0x%08lX size=%u (Phase2 reloc)\r\n",
                      (unsigned long)TARS_SDRAM_EXEC_BASE,
                      (unsigned)TARS_SDRAM_EXEC_SIZE);

  return TARS_OK;
}

void TarsApp_SchedulerTick(uint8_t timeslice)
{
  if (s_last_timeslice != 0U && s_last_timeslice != timeslice)
  {
    registry_release_timeslice(s_last_timeslice);
  }

  registry_run_timeslice(timeslice);
  s_last_timeslice = timeslice;
}

void TarsApp_SchedulerTask(void const *argument)
{
  uint8_t slice = 1U;
  static uint8_t s_app_ready = 0U;

  (void)argument;

  if (s_app_ready == 0U)
  {
    TarsApp_Init();
    s_app_ready = 1U;
  }

  for (;;)
  {
    TarsApp_SchedulerTick(slice);
    slice++;

    if (slice > 8U)
    {
      slice = 1U;
    }

    osDelay(100);
  }
}
