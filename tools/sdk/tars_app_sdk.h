/*
 * PC-side SDK for post-MVP Phase 1 native apps (fixed flash slot, XIP).
 * MVP firmware rejects .tapp install (TARS_MVP_LUA_ONLY).
 */
#ifndef TARS_APP_SDK_H
#define TARS_APP_SDK_H

#include <stdint.h>

#define TARS_APP_MAGIC            0x54504150UL
#define TARS_API_VERSION          1U
#define TARS_NAME_MAX             16U
#define TARS_RESOURCE_MAX         16U
#define TARS_RESOURCE_GPIO        1U

#define TARS_LOAD_FIXED           0x0001U
#define TARS_EXEC_FLASH           0x0010U

typedef struct {
  uint8_t  type;
  uint8_t  instance;
  uint16_t param;
} tars_resource_t;

typedef struct {
  uint8_t         count;
  tars_resource_t items[TARS_RESOURCE_MAX];
} tars_manifest_t;

typedef struct {
  uint32_t api_version;
  void (*gpio_write)(uint32_t pin_id, int value);
  int (*gpio_read)(uint32_t pin_id);
  void (*sleep_ms)(uint32_t ms);
  void (*log)(const char *msg);
} tars_api_t;

void app_entry(const tars_api_t *api);

#endif /* TARS_APP_SDK_H */
