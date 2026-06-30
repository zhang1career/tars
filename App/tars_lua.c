#include "tars_lua.h"
#include "tars_app.h"
#include "tars_storage.h"
#include "tars_lfs.h"
#include "tars_platform.h"
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

static int tars_lua_register_api(lua_State *L);

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
    tars_lua_register_api(s_lua);
  }
}

static int l_tars_gpio_write(lua_State *L)
{
  const tars_api_t *api = TarsApp_GetApi();
  const char *pin_name = luaL_checkstring(L, 1);
  int val = (int)luaL_checkinteger(L, 2);

  if (api != NULL && api->gpio_write != NULL)
  {
    api->gpio_write(pin_name, val);
  }

  return 0;
}

static int l_tars_gpio_read(lua_State *L)
{
  const tars_api_t *api = TarsApp_GetApi();
  const char *pin_name = luaL_checkstring(L, 1);
  int val = 0;

  if (api != NULL && api->gpio_read != NULL)
  {
    val = api->gpio_read(pin_name);
  }

  lua_pushinteger(L, val);
  return 1;
}

static int l_tars_pwm_enable(lua_State *L)
{
  const tars_api_t *api = TarsApp_GetApi();
  const char *channel = luaL_checkstring(L, 1);
  int enable = (int)luaL_checkinteger(L, 2);
  int st = 0;

  if (api != NULL && api->pwm_enable != NULL)
  {
    st = api->pwm_enable(channel, enable);
  }

  lua_pushinteger(L, st);
  return 1;
}

static int l_tars_pwm_duty(lua_State *L)
{
  const tars_api_t *api = TarsApp_GetApi();
  const char *channel = luaL_checkstring(L, 1);
  float duty = (float)luaL_checknumber(L, 2);
  int st = 0;

  if (api != NULL && api->pwm_duty != NULL)
  {
    st = api->pwm_duty(channel, duty);
  }

  lua_pushinteger(L, st);
  return 1;
}

static int l_tars_res_save(lua_State *L)
{
  const tars_api_t *api = TarsApp_GetApi();
  int st = 0;

  if (api != NULL && api->api_version >= 2U && api->res_save != NULL)
  {
    st = api->res_save();
  }

  lua_pushinteger(L, st);
  return 1;
}

static int l_tars_res_load(lua_State *L)
{
  const tars_api_t *api = TarsApp_GetApi();
  int st = 0;

  if (api != NULL && api->api_version >= 2U && api->res_load != NULL)
  {
    st = api->res_load();
  }

  lua_pushinteger(L, st);
  return 1;
}

static int l_tars_res_clear(lua_State *L)
{
  const tars_api_t *api = TarsApp_GetApi();
  int st = 0;

  if (api != NULL && api->api_version >= 2U && api->res_clear != NULL)
  {
    st = api->res_clear();
  }

  lua_pushinteger(L, st);
  return 1;
}

static int l_tars_pwm_persist(lua_State *L)
{
  const tars_api_t *api = TarsApp_GetApi();
  const char *channel = luaL_checkstring(L, 1);
  int boot = (int)luaL_checkinteger(L, 2);
  int st = 0;

  if (api != NULL && api->api_version >= 2U && api->pwm_persist != NULL)
  {
    st = api->pwm_persist(channel, boot);
  }

  lua_pushinteger(L, st);
  return 1;
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

static int l_tars_yield(lua_State *L)
{
  return lua_yield(L, 0);
}

static void tars_lua_log_error(lua_State *L)
{
  const tars_api_t *api = TarsApp_GetApi();
  const char *msg = lua_tostring(L, -1);

  if ((msg != NULL) && (api != NULL) && (api->log != NULL))
  {
    api->log(msg);
  }

  lua_pop(L, 1);
}

static int s_co_ref = LUA_NOREF;
static char s_co_name[TARS_NAME_MAX];
static uint8_t s_co_yielded;

static void tars_lua_co_drop(void)
{
  if (s_co_ref != LUA_NOREF)
  {
    luaL_unref(s_lua, LUA_REGISTRYINDEX, s_co_ref);
    s_co_ref = LUA_NOREF;
  }

  s_co_yielded = 0U;
  s_co_name[0] = '\0';
}

static tars_status_t tars_lua_co_start(const char *name, const char *source, uint32_t length)
{
  lua_State *co;

  tars_lua_co_drop();

  co = lua_newthread(s_lua);
  if (co == NULL)
  {
    return TARS_ERR_STATE;
  }

  s_co_ref = luaL_ref(s_lua, LUA_REGISTRYINDEX);

  if (luaL_loadbuffer(co, source, length, name) != 0)
  {
    tars_lua_log_error(co);
    tars_lua_co_drop();
    return TARS_ERR_STATE;
  }

  strncpy(s_co_name, name, TARS_NAME_MAX);
  s_co_name[TARS_NAME_MAX - 1U] = '\0';
  return TARS_OK;
}

static tars_status_t tars_lua_co_resume(void)
{
  lua_State *co;
  int status;

  if (s_co_ref == LUA_NOREF)
  {
    return TARS_ERR_STATE;
  }

  lua_rawgeti(s_lua, LUA_REGISTRYINDEX, s_co_ref);
  co = lua_tothread(s_lua, -1);
  if (co == NULL)
  {
    lua_pop(s_lua, 1);
    tars_lua_co_drop();
    return TARS_ERR_STATE;
  }

  status = lua_resume(co, 0);
  lua_pop(s_lua, 1);

  if (status == 0)
  {
    tars_lua_co_drop();
    return TARS_OK;
  }

  if (status == LUA_YIELD)
  {
    s_co_yielded = 1U;
    return TARS_OK;
  }

  tars_lua_log_error(co);
  tars_lua_co_drop();
  return TARS_ERR_STATE;
}

#define TARS_LUA_FILE_MAX  4096U

typedef struct {
  char data[TARS_LUA_FILE_MAX];
  uint32_t size;
  uint32_t pos;
} tars_lua_file_t;

static int l_file_read(lua_State *L)
{
  tars_lua_file_t *f = (tars_lua_file_t *)luaL_checkudata(L, 1, "tars.file");
  uint32_t to_read = f->size - f->pos;
  const char *mode = luaL_optstring(L, 2, "*a");

  if (f == NULL)
  {
    lua_pushnil(L);
    return 1;
  }

  if (f->pos >= f->size)
  {
    lua_pushnil(L);
    return 1;
  }

  if ((mode[0] == '*') && (mode[1] == 'l'))
  {
    uint32_t start = f->pos;
    uint32_t i;

    for (i = f->pos; i < f->size; i++)
    {
      if (f->data[i] == '\n')
      {
        to_read = (i - f->pos) + 1U;
        break;
      }
    }

    if (to_read == (f->size - f->pos))
    {
      to_read = f->size - f->pos;
    }

    lua_pushlstring(L, f->data + start, to_read);
    f->pos += to_read;
    return 1;
  }

  if (lua_isnumber(L, 2))
  {
    uint32_t n = (uint32_t)lua_tointeger(L, 2);

    if (n < to_read)
    {
      to_read = n;
    }
  }

  lua_pushlstring(L, f->data + f->pos, to_read);
  f->pos += to_read;
  return 1;
}

static int l_file_close(lua_State *L)
{
  (void)luaL_checkudata(L, 1, "tars.file");
  return 0;
}

static int l_tars_open(lua_State *L)
{
  const char *path = luaL_checkstring(L, 1);
  const char *mode = luaL_optstring(L, 2, "r");
  tars_lua_file_t *f;
  uint32_t read_size = 0U;
  tars_status_t st;

  if (strcmp(mode, "r") != 0)
  {
    return luaL_error(L, "tars.open: only \"r\" mode supported");
  }

  f = (tars_lua_file_t *)lua_newuserdata(L, sizeof(tars_lua_file_t));
  f->size = 0U;
  f->pos = 0U;

  st = TarsLfs_ReadFile(path, (uint8_t *)f->data, TARS_LUA_FILE_MAX, &read_size);
  if (st != TARS_OK)
  {
    return luaL_error(L, "tars.open: %s not found", path);
  }

  f->size = read_size;

  luaL_getmetatable(L, "tars.file");
  lua_setmetatable(L, -2);
  return 1;
}

static const luaL_Reg tars_file_methods[] = {
  {"read", l_file_read},
  {"close", l_file_close},
  {NULL, NULL}
};

int luaopen_tars(lua_State *L)
{
  return tars_lua_register_api(L);
}

static int tars_lua_register_api(lua_State *L)
{
  static const luaL_Reg tars_api[] = {
    {"gpio_write", l_tars_gpio_write},
    {"gpio_read", l_tars_gpio_read},
    {"pwm_enable", l_tars_pwm_enable},
    {"pwm_duty", l_tars_pwm_duty},
    {"pwm_persist", l_tars_pwm_persist},
    {"res_save", l_tars_res_save},
    {"res_load", l_tars_res_load},
    {"res_clear", l_tars_res_clear},
    {"sleep", l_tars_sleep},
    {"log", l_tars_log},
    {"yield", l_tars_yield},
    {"open", l_tars_open},
    {NULL, NULL}
  };

  if (luaL_newmetatable(L, "tars.file") != 0)
  {
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    luaL_register(L, NULL, tars_file_methods);
    lua_pop(L, 1);
  }

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

  if (source == NULL || length == 0U)
  {
    return TARS_ERR_STATE;
  }

  status = tars_lua_ensure_init();
  if (status != TARS_OK)
  {
    return status;
  }

  status = tars_lua_co_start(name, source, length);
  if (status != TARS_OK)
  {
    return status;
  }

  do
  {
    status = tars_lua_co_resume();
  } while ((status == TARS_OK) && (s_co_yielded != 0U));

  return status;
}

tars_status_t TarsLua_RunApp(const char *name)
{
  int32_t idx = TarsStorage_FindByName(name);
  const tars_storage_entry_t *entry;
  const tars_lua_hdr_t *hdr;
  const char *src;
  uint8_t *blob_buf = (uint8_t *)(void *)TARS_INSTALL_STAGING_BASE;
  uint32_t blob_size = 0U;
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

  if ((s_co_yielded != 0U) && (strcmp(s_co_name, name) == 0))
  {
    return tars_lua_co_resume();
  }

  status = TarsStorage_ReadLuaBlob(name,
                                   blob_buf,
                                   TARS_INSTALL_STAGING_SIZE,
                                   &blob_size);
  if (status != TARS_OK)
  {
    return status;
  }

  hdr = (const tars_lua_hdr_t *)(const void *)blob_buf;
  src = (const char *)(const void *)(blob_buf + hdr->header_size);

  status = tars_lua_co_start(name, src, hdr->lua_size);
  if (status != TARS_OK)
  {
    return status;
  }

  return tars_lua_co_resume();
}

void TarsLua_OnAcquire(const char *app_name)
{
  (void)app_name;
}

void TarsLua_OnRelease(const char *app_name)
{
  (void)app_name;
}

void TarsLua_Reset(const char *name)
{
  if ((name == NULL) || (s_co_name[0] == '\0') || (strcmp(s_co_name, name) == 0))
  {
    tars_lua_co_drop();
  }
}

void TarsLua_GetHeapUsage(uint32_t *used_out, uint32_t *total_out)
{
  if (used_out != NULL)
  {
    *used_out = s_lua_heap_used;
  }

  if (total_out != NULL)
  {
    *total_out = TARS_LUA_HEAP_SIZE;
  }
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

      /* The Lua allocator is a bump pointer that never frees in place, so a
       * finished run would otherwise leak its heap until exhaustion. Once the
       * coroutine has fully completed (it is not parked on a yield), tear the
       * whole VM down and rebuild it: lua_close drops every reference and
       * TarsLua_Init rewinds the bump pointer to zero. Do NOT do this while a
       * coroutine is yielded, or cooperative apps would lose their state. */
      if (s_co_yielded == 0U)
      {
        TarsLua_Init();
      }

      s_run_busy = 0U;
    }

    if (catalog_flush_done == 0U && (HAL_GetTick() >= 3000U))
    {
      catalog_flush_done = 1U;
      (void)TarsStorage_FlushCatalogIfDirty(&flushed);
    }

    osDelay(10);
  }
}
