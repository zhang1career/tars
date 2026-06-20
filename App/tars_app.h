#ifndef TARS_APP_H
#define TARS_APP_H

#include <stdint.h>
#include "tars_platform.h"

#define TARS_APP_MAGIC            0x54504150UL /* 'TPAP' */
#define TARS_LUA_MAGIC            0x5450484CUL /* 'TPHL' */
#define TARS_CATALOG_MAGIC        0x54504354UL /* 'TPCT' */

#define TARS_API_VERSION          1U

#define TARS_NAME_MAX             16U
#define TARS_RESOURCE_MAX         16U
#define TARS_WHITELIST_GROUP_MAX  8U
#define TARS_WHITELIST_MEMBER_MAX 4U
#define TARS_RELOC_MAX            256U

#define TARS_LOAD_FIXED           0x0001U /* Phase 1: pre-linked to slot link_addr, XIP */
/* TARS_LOAD_RELOC / TARS_EXEC_SDRAM — not used (Phase 2 cancelled) */
#define TARS_LOAD_RELOC           0x0002U
#define TARS_EXEC_FLASH           0x0010U /* execute from internal flash slot */
#define TARS_EXEC_SDRAM           0x0020U

#define TARS_APP_TYPE_NATIVE      1U
#define TARS_APP_TYPE_LUA         2U

#define TARS_RESOURCE_GPIO        1U
#define TARS_RESOURCE_SPI         2U
#define TARS_RESOURCE_I2C         3U
#define TARS_RESOURCE_TIMER       4U
#define TARS_RESOURCE_RESERVED    0xFFU

typedef enum {
  TARS_OK = 0,
  TARS_ERR_PARAM = -1,
  TARS_ERR_MAGIC = -2,
  TARS_ERR_CRC = -3,
  TARS_ERR_API_VERSION = -4,
  TARS_ERR_RESOURCE = -5,
  TARS_ERR_CONFLICT = -6,
  TARS_ERR_NO_SLOT = -7,
  TARS_ERR_FLASH = -8,
  TARS_ERR_NOT_FOUND = -9,
  TARS_ERR_STATE = -10,
  TARS_ERR_RELOC = -11
} tars_status_t;

typedef struct {
  uint8_t  type;
  uint8_t  instance;
  uint16_t param;
} tars_resource_t;

typedef struct {
  uint8_t         count;
  tars_resource_t items[TARS_RESOURCE_MAX];
} tars_manifest_t;

typedef enum {
  TARS_RELOC_ABS32 = 1
} tars_reloc_type_t;

typedef struct {
  uint32_t offset;
  uint8_t  type;
  uint8_t  reserved[3];
} tars_reloc_t;

typedef struct {
  uint32_t        magic;
  uint16_t        header_size;
  uint16_t        flags;
  uint32_t        api_version;
  char            name[TARS_NAME_MAX];
  uint32_t        app_version;
  uint8_t         app_type;
  uint8_t         timeslice;
  uint16_t        reserved0;
  tars_manifest_t manifest;
  uint32_t        link_base;
  uint32_t        text_size;
  uint32_t        data_size;
  uint32_t        bss_size;
  uint32_t        entry_offset;
  uint32_t        reloc_count;
  uint32_t        payload_size;
  uint32_t        crc32;
} tars_app_hdr_t;

typedef struct {
  uint32_t        magic;
  uint16_t        header_size;
  uint16_t        flags;
  char            name[TARS_NAME_MAX];
  uint32_t        app_version;
  uint8_t         timeslice;
  uint8_t         priority;
  uint8_t         reserved[2];
  tars_manifest_t manifest;
  uint32_t        lua_size;
  uint32_t        crc32;
} tars_lua_hdr_t;

typedef struct {
  uint32_t api_version;
  void (*gpio_write)(uint32_t pin_id, int value);
  int (*gpio_read)(uint32_t pin_id);
  void (*sleep_ms)(uint32_t ms);
  void (*log)(const char *msg);
} tars_api_t;

typedef void (*tars_native_entry_fn)(const tars_api_t *api);

typedef struct {
  const char *name;
  uint8_t     timeslice;
  tars_manifest_t manifest;
  void (*init)(const tars_api_t *api);
  void (*tick)(void);
  void (*release)(void);
} tars_builtin_app_t;

typedef struct {
  char            name[TARS_NAME_MAX];
  uint8_t         app_type;
  uint8_t         timeslice;
  uint8_t         priority;
  uint8_t         submitted;
  uint8_t         loaded;
  uint16_t        flags;
  uint32_t        app_version;
  tars_manifest_t manifest;
  uint32_t        exec_addr;
  uint32_t        ram_addr;
  uint32_t        slot_index;
  tars_native_entry_fn entry;
} tars_app_runtime_t;

typedef struct {
  uint8_t  current_timeslice;
  uint8_t  slice_count;
  uint16_t slice_ms;
  char     running_name[TARS_NAME_MAX];
  uint8_t  has_running;
} tars_scheduler_info_t;

typedef struct {
  tars_resource_t resource;
  char            members[TARS_WHITELIST_MEMBER_MAX][TARS_NAME_MAX];
  uint8_t         member_count;
} tars_whitelist_group_t;

void TarsApp_Init(void);
void TarsApi_Init(void);
const tars_api_t *TarsApp_GetApi(void);

tars_status_t TarsApp_InstallNative(const uint8_t *blob, uint32_t blob_size, int32_t slot_hint);
tars_status_t TarsApp_InstallLua(const uint8_t *blob, uint32_t blob_size);
tars_status_t TarsApp_Submit(const char *name);
tars_status_t TarsApp_Revoke(const char *name);
tars_status_t TarsApp_Uninstall(const char *name);
tars_status_t TarsApp_RunOnce(const char *name);
tars_status_t TarsApp_List(char *out, uint32_t out_size);
tars_status_t TarsApp_ListSlots(char *out, uint32_t out_size);

void TarsApp_RegisterBuiltin(const tars_builtin_app_t *app);
void TarsApp_SchedulerTick(uint8_t timeslice);
void TarsApp_SchedulerTask(void const *argument);
void TarsApp_GetSchedulerInfo(tars_scheduler_info_t *info);

#endif /* TARS_APP_H */
