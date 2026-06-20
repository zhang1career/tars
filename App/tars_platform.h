#ifndef TARS_PLATFORM_H
#define TARS_PLATFORM_H

#include <stdint.h>

/* Internal flash layout (STM32F429 2 MiB) */
#define TARS_FLASH_BASE           0x08000000UL
#define TARS_FLASH_SIZE           (2U * 1024U * 1024U)

/* --- Program / firmware (sectors 0-6, 384 KiB) --- */
#define TARS_FW_FLASH_BASE        0x08000000UL
#define TARS_FW_FLASH_SIZE        (384U * 1024U)
#define TARS_FW_FLASH_END         (TARS_FW_FLASH_BASE + TARS_FW_FLASH_SIZE)
#define TARS_FW_FIRST_SECTOR      0U
#define TARS_FW_LAST_SECTOR       6U

/* Legacy alias used by docs */
#define TARS_FW_MAX_SIZE          TARS_FW_FLASH_SIZE

/* --- LittleFS app partition (sectors 7-8, 256 KiB) --- */
#define TARS_LFS_FLASH_BASE       0x08060000UL
#define TARS_LFS_FLASH_SIZE       (256U * 1024U)
#define TARS_LFS_FIRST_SECTOR     7U
#define TARS_LFS_BLOCK_SIZE       (4096U)
#define TARS_LFS_BLOCK_COUNT      (TARS_LFS_FLASH_SIZE / TARS_LFS_BLOCK_SIZE)
#define TARS_LFS_PHYS_ERASE_SIZE  (128U * 1024U)

#define TARS_LFS_PATH_CATALOG     "/sys/catalog"
#define TARS_LFS_PATH_APPS        "/apps"

/* --- OTA staging (future, Bank2 sectors 12-13) --- */
#define TARS_OTA_FLASH_BASE       0x08100000UL
#define TARS_OTA_FLASH_SIZE       (256U * 1024U)
#define TARS_OTA_FIRST_SECTOR     12U

/* --- Native XIP slots (sector 8+) --- */
#define TARS_NATIVE_SLOT_COUNT    8U
#define TARS_NATIVE_SLOT_MAX      (48U * 1024U)
#define TARS_NATIVE_SLOT_STRIDE   (128U * 1024U)
#define TARS_NATIVE_SLOT_BASE     0x080A0000UL

static inline uint32_t TarsNativeSlotAddress(uint32_t slot_index)
{
  return TARS_NATIVE_SLOT_BASE + (slot_index * TARS_NATIVE_SLOT_STRIDE);
}

static inline uint32_t TarsNativeSlotSector(uint32_t slot_index)
{
  return 9U + slot_index;
}

#define TARS_SDRAM_EXEC_BASE      0xD0028000UL
#define TARS_SDRAM_EXEC_SIZE      (512U * 1024U)

#define TARS_INSTALL_STAGING_BASE 0xD00C0000UL
#define TARS_INSTALL_STAGING_SIZE (128U * 1024U)

#define TARS_LUA_HEAP_BASE        0xD00E0000UL
#define TARS_LUA_HEAP_SIZE        (128U * 1024U)

/* --- Scheduler --- */
#define TARS_SCHED_SLICE_COUNT    8U
#define TARS_SCHED_SLICE_MS       100U
#define TARS_SCHED_DEFAULT_TS     1U
#define TARS_SCHED_DEFAULT_PRI    10U

/* MVP: Lua apps only. Native .tapp install/run disabled until post-MVP. */
#define TARS_MVP_LUA_ONLY         1

#endif /* TARS_PLATFORM_H */
