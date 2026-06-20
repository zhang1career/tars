#include "tars_lua.h"
#include "tars_app.h"
#include "tars_storage.h"
#include "usb_otg.h"
#include "main.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "cmsis_os.h"
#include <string.h>

static lua_State *s_lua;
static uint8_t *s_lua_heap_base;
static uint32_t s_lua_heap_used;
static volatile uint8_t s_run_pending;
static volatile uint8_t s_run_busy;
static char s_run_name[TARS_NAME_MAX];

static void lua_heap_reset(void)
{
  s_lua_heap_used = 0U;
}

static void *lua_sdram_alloc(void *ud, void *ptr, size_t osize, size_t nsize)
{
  uint8_t *new_ptr;

  (void)ud;

  if (nsize == 0U)
  {
    return NULL;
  }

  /* Keep every allocation 8-byte aligned. Lua TValue embeds a double
   * (lua_Number); ARM ldrd/strd fault on unaligned addresses even in
   * Normal memory, so the bump pointer must stay aligned. */
  {
    uint32_t aligned_size = ((uint32_t)nsize + 7U) & ~7U;

    if ((s_lua_heap_used + aligned_size) > TARS_LUA_HEAP_SIZE)
    {
      return NULL;
    }

    new_ptr = s_lua_heap_base + s_lua_heap_used;
    s_lua_heap_used += aligned_size;
  }

  if (ptr != NULL && osize > 0U)
  {
    size_t copy = osize;

    if (copy > nsize)
    {
      copy = nsize;
    }

    memcpy(new_ptr, ptr, copy);
  }

  return new_ptr;
}

void TarsLua_Init(void)
{
  if (s_lua != NULL)
  {
    lua_close(s_lua);
    s_lua = NULL;
  }

  lua_heap_reset();
  s_lua_heap_base = (uint8_t *)(void *)TARS_LUA_HEAP_BASE;
  s_lua = lua_newstate(lua_sdram_alloc, NULL);

  if (s_lua != NULL)
  {
    luaL_openlibs(s_lua);
  }
}

static int l_tars_gpio_write(lua_State *L)
{
  const tars_api_t *api = TarsApp_GetApi();
  uint32_t pin = (uint32_t)luaL_checkinteger(L, 1);
  int val = (int)luaL_checkinteger(L, 2);

  if (api != NULL && api->gpio_write != NULL)
  {
    api->gpio_write(pin, val);
  }

  return 0;
}

static int l_tars_sleep(lua_State *L)
{
  const tars_api_t *api = TarsApp_GetApi();
  uint32_t ms = (uint32_t)luaL_checkinteger(L, 1);

  if (api != NULL && api->sleep_ms != NULL)
  {
    api->sleep_ms(ms);
  }

  return 0;
}

static int l_tars_log(lua_State *L)
{
  const tars_api_t *api = TarsApp_GetApi();
  const char *msg = luaL_checkstring(L, 1);

  if (api != NULL && api->log != NULL)
  {
    api->log(msg);
  }

  return 0;
}

int luaopen_tars(lua_State *L)
{
  static const luaL_Reg tars_api[] = {
    {"gpio_write", l_tars_gpio_write},
    {"sleep", l_tars_sleep},
    {"log", l_tars_log},
    {NULL, NULL}
  };

  luaL_register(L, "tars", tars_api);
  return 1;
}

static tars_status_t tars_lua_ensure_init(void)
{
  if (s_lua == NULL)
  {
    TarsLua_Init();

    if (s_lua == NULL)
    {
      return TARS_ERR_STATE;
    }
  }

  return TARS_OK;
}

tars_status_t TarsLua_RunScript(const char *name, const char *source, uint32_t length)
{
  tars_status_t status;

  (void)name;

  if (source == NULL || length == 0U)
  {
    return TARS_ERR_STATE;
  }

  status = tars_lua_ensure_init();
  if (status != TARS_OK)
  {
    return status;
  }

  if (luaL_loadbuffer(s_lua, source, length, name) != 0)
  {
    const tars_api_t *api = TarsApp_GetApi();
    if (api != NULL && api->log != NULL)
    {
      api->log(lua_tostring(s_lua, -1));
    }
    lua_pop(s_lua, 1);
    return TARS_ERR_STATE;
  }

  if (lua_pcall(s_lua, 0, 0, 0) != 0)
  {
    const tars_api_t *api = TarsApp_GetApi();
    if (api != NULL && api->log != NULL)
    {
      api->log(lua_tostring(s_lua, -1));
    }
    lua_pop(s_lua, 1);
    return TARS_ERR_STATE;
  }

  return TARS_OK;
}

tars_status_t TarsLua_RunApp(const char *name)
{
  int32_t idx = TarsStorage_FindByName(name);
  const tars_storage_entry_t *entry;
  tars_status_t status;

  status = tars_lua_ensure_init();
  if (status != TARS_OK)
  {
    return status;
  }

  if (idx < 0)
  {
    return TARS_ERR_NOT_FOUND;
  }

  entry = TarsStorage_GetEntry((uint32_t)idx);

  if (entry == NULL || entry->app_type != TARS_APP_TYPE_LUA)
  {
    return TARS_ERR_STATE;
  }

  const tars_lua_hdr_t *hdr = (const tars_lua_hdr_t *)(const void *)entry->blob_addr;
  const char *src = (const char *)(const void *)(entry->blob_addr + hdr->header_size);

  return TarsLua_RunScript(name, src, hdr->lua_size);
}

void TarsLua_OnAcquire(const char *app_name)
{
  (void)app_name;
}

void TarsLua_OnRelease(const char *app_name)
{
  (void)app_name;
}

void TarsLua_RequestRun(const char *name)
{
  if ((name == NULL) || (s_run_busy != 0U) || (s_run_pending != 0U))
  {
    return;
  }

  strncpy(s_run_name, name, TARS_NAME_MAX);
  s_run_name[TARS_NAME_MAX - 1U] = '\0';
  s_run_pending = 1U;
}

void TarsLua_Task(void const *argument)
{
  int flushed = 0;
  uint8_t catalog_flush_done = 0U;

  (void)argument;

  for (;;)
  {
    if (s_run_pending != 0U)
    {
      s_run_pending = 0U;
      s_run_busy = 1U;
      (void)TarsLua_RunApp(s_run_name);
      s_run_busy = 0U;
    }

    if (catalog_flush_done == 0U && (HAL_GetTick() >= 3000U))
    {
      catalog_flush_done = 1U;
      if (TarsStorage_FlushCatalogIfDirty(&flushed) == TARS_OK && flushed != 0)
      {
        UsbOtg_RecoverDeviceCdc();
      }
    }

    osDelay(10);
  }
}
