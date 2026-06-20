#ifndef TARS_PLATFORM_H
#define TARS_PLATFORM_H

#include <stdint.h>

/* Internal flash layout (STM32F429 2 MiB) */
#define TARS_FLASH_BASE           0x08000000UL
#define TARS_FLASH_SIZE           (2U * 1024U * 1024U)

#define TARS_FW_MAX_SIZE          (512U * 1024U)

#define TARS_NATIVE_SLOT_COUNT    8U
#define TARS_NATIVE_SLOT_MAX      (48U * 1024U)
#define TARS_NATIVE_SLOT_STRIDE   (128U * 1024U)
#define TARS_NATIVE_SLOT_BASE     0x08080000UL

static inline uint32_t TarsNativeSlotAddress(uint32_t slot_index)
{
  return TARS_NATIVE_SLOT_BASE + (slot_index * TARS_NATIVE_SLOT_STRIDE);
}

#define TARS_LUA_POOL_BASE        0x08060000UL /* flash sector 6 (bank1) */
#define TARS_CATALOG_BASE         0x0807F000UL /* flash sector 7 tail, 4KB */
#define TARS_LUA_POOL_SIZE        (TARS_CATALOG_BASE - TARS_LUA_POOL_BASE)
#define TARS_CATALOG_SIZE         (4U * 1024U)

#define TARS_LUA_POOL_SECTOR      6U
#define TARS_CATALOG_SECTOR       7U

#define TARS_SDRAM_EXEC_BASE      0xD0028000UL
#define TARS_SDRAM_EXEC_SIZE      (512U * 1024U)

/* USB install staging (SDRAM) */
#define TARS_INSTALL_STAGING_BASE 0xD00C0000UL
#define TARS_INSTALL_STAGING_SIZE (128U * 1024U)

/* Lua VM heap in SDRAM */
#define TARS_LUA_HEAP_BASE        0xD00E0000UL
#define TARS_LUA_HEAP_SIZE        (128U * 1024U)

#endif /* TARS_PLATFORM_H */
