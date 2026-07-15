#include "tars_lfs.h"
#include "tars_flash_hw.h"
#include "tars_platform.h"
#include "lfs.h"
#include <stdio.h>
#include <string.h>

static lfs_t s_lfs;
static struct lfs_config s_cfg;
static uint8_t s_lfs_mounted;
static uint8_t s_lfs_inited;
static uint8_t s_read_buf[4096];
static uint8_t s_prog_buf[4096];
static uint8_t s_lookahead_buf[64];
static uint8_t s_file_buf[4096];

static int lfs_file_open_cfg(const char *path, int flags, lfs_file_t *file)
{
  struct lfs_file_config fcfg = {0};

  fcfg.buffer = s_file_buf;
  return lfs_file_opencfg(&s_lfs, file, path, flags, &fcfg);
}

static int lfs_bd_read(const struct lfs_config *c, lfs_block_t block,
                       lfs_off_t off, void *buffer, lfs_size_t size)
{
  uint32_t addr = (uint32_t)(uintptr_t)c->context + (block * c->block_size) + off;

  (void)TarsFlash_Read(addr, buffer, size);
  return 0;
}

static int lfs_bd_prog(const struct lfs_config *c, lfs_block_t block,
                       lfs_off_t off, const void *buffer, lfs_size_t size)
{
  uint32_t addr = (uint32_t)(uintptr_t)c->context + (block * c->block_size) + off;

  if (TarsFlash_ProgramUnlocked(addr, (const uint8_t *)buffer, size) != TARS_OK)
  {
    return LFS_ERR_IO;
  }

  return 0;
}

static int lfs_bd_erase(const struct lfs_config *c, lfs_block_t block)
{
  uint32_t offset = block * c->block_size;
  uint32_t sector;

  (void)c;

  if ((offset % TARS_LFS_PHYS_ERASE_SIZE) != 0U)
  {
    return 0;
  }

  sector = TARS_LFS_FIRST_SECTOR + (offset / TARS_LFS_PHYS_ERASE_SIZE);
  if (TarsFlash_EraseSectorUnlocked(sector) != TARS_OK)
  {
    return LFS_ERR_IO;
  }

  return 0;
}

static int lfs_bd_sync(const struct lfs_config *c)
{
  (void)c;
  return 0;
}

static int lfs_bd_lock(const struct lfs_config *c)
{
  (void)c;
  TarsFlash_Lock();
  return 0;
}

static int lfs_bd_unlock(const struct lfs_config *c)
{
  (void)c;
  TarsFlash_Unlock();
  return 0;
}

static void lfs_config_init(void)
{
  memset(&s_cfg, 0, sizeof(s_cfg));
  s_cfg.context = (void *)(uintptr_t)TARS_LFS_FLASH_BASE;
  s_cfg.read = lfs_bd_read;
  s_cfg.prog = lfs_bd_prog;
  s_cfg.erase = lfs_bd_erase;
  s_cfg.sync = lfs_bd_sync;
#ifdef LFS_THREADSAFE
  s_cfg.lock = lfs_bd_lock;
  s_cfg.unlock = lfs_bd_unlock;
#endif
  s_cfg.read_size = 4U;
  s_cfg.prog_size = 4U;
  s_cfg.block_size = TARS_LFS_BLOCK_SIZE;
  s_cfg.block_count = TARS_LFS_BLOCK_COUNT;
  s_cfg.block_cycles = 500;
  s_cfg.cache_size = sizeof(s_read_buf);
  s_cfg.lookahead_size = sizeof(s_lookahead_buf);
  s_cfg.metadata_max = 4096U;
  s_cfg.read_buffer = s_read_buf;
  s_cfg.prog_buffer = s_prog_buf;
  s_cfg.lookahead_buffer = s_lookahead_buf;
}

static tars_status_t lfs_ensure_dirs(void)
{
  int err;

  err = lfs_mkdir(&s_lfs, "/sys");
  if (err != 0 && err != LFS_ERR_EXIST)
  {
    return TARS_ERR_FLASH;
  }

  err = lfs_mkdir(&s_lfs, TARS_LFS_PATH_CONFIG);
  if (err != 0 && err != LFS_ERR_EXIST)
  {
    return TARS_ERR_FLASH;
  }

  err = lfs_mkdir(&s_lfs, TARS_LFS_PATH_APPS);
  if (err != 0 && err != LFS_ERR_EXIST)
  {
    return TARS_ERR_FLASH;
  }

  return TARS_OK;
}

tars_status_t TarsLfs_Init(void)
{
  int err;

  if (s_lfs_inited != 0U)
  {
    return TARS_OK;
  }

  TarsFlash_Init();
  lfs_config_init();

  err = lfs_mount(&s_lfs, &s_cfg);
  if (err != 0)
  {
    err = lfs_format(&s_lfs, &s_cfg);
    if (err != 0)
    {
      return TARS_ERR_FLASH;
    }

    err = lfs_mount(&s_lfs, &s_cfg);
    if (err != 0)
    {
      return TARS_ERR_FLASH;
    }
  }

  s_lfs_mounted = 1U;
  s_lfs_inited = 1U;
  return lfs_ensure_dirs();
}

int TarsLfs_IsMounted(void)
{
  return (int)s_lfs_mounted;
}

tars_status_t TarsLfs_Format(void)
{
  int err;

  if (s_lfs_mounted != 0U)
  {
    (void)lfs_unmount(&s_lfs);
    s_lfs_mounted = 0U;
  }

  err = lfs_format(&s_lfs, &s_cfg);
  if (err != 0)
  {
    return TARS_ERR_FLASH;
  }

  err = lfs_mount(&s_lfs, &s_cfg);
  if (err != 0)
  {
    return TARS_ERR_FLASH;
  }

  s_lfs_mounted = 1U;
  return lfs_ensure_dirs();
}

tars_status_t TarsLfs_WriteFile(const char *path, const uint8_t *data, uint32_t size)
{
  lfs_file_t file;
  lfs_ssize_t written;
  int err;

  if ((path == NULL) || (data == NULL) || (size == 0U) || (s_lfs_mounted == 0U))
  {
    return TARS_ERR_PARAM;
  }

  err = lfs_file_open_cfg(path, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC, &file);
  if (err != 0)
  {
    return TARS_ERR_FLASH;
  }

  written = lfs_file_write(&s_lfs, &file, data, size);
  if (written != (lfs_ssize_t)size)
  {
    (void)lfs_file_close(&s_lfs, &file);
    return TARS_ERR_FLASH;
  }

  err = lfs_file_sync(&s_lfs, &file);
  if (err != 0)
  {
    (void)lfs_file_close(&s_lfs, &file);
    return TARS_ERR_FLASH;
  }

  err = lfs_file_close(&s_lfs, &file);
  if (err != 0)
  {
    return TARS_ERR_FLASH;
  }

  return TARS_OK;
}

tars_status_t TarsLfs_ReadFile(const char *path, uint8_t *data, uint32_t max_size, uint32_t *out_size)
{
  lfs_file_t file;
  lfs_ssize_t nread;
  int err;

  if ((path == NULL) || (data == NULL) || (max_size == 0U) || (s_lfs_mounted == 0U))
  {
    return TARS_ERR_PARAM;
  }

  err = lfs_file_open_cfg(path, LFS_O_RDONLY, &file);
  if (err != 0)
  {
    return TARS_ERR_NOT_FOUND;
  }

  nread = lfs_file_read(&s_lfs, &file, data, max_size);
  (void)lfs_file_close(&s_lfs, &file);

  if (nread < 0)
  {
    return TARS_ERR_FLASH;
  }

  if (out_size != NULL)
  {
    *out_size = (uint32_t)nread;
  }

  return TARS_OK;
}

tars_status_t TarsLfs_RemoveFile(const char *path)
{
  if ((path == NULL) || (s_lfs_mounted == 0U))
  {
    return TARS_ERR_PARAM;
  }

  if (lfs_remove(&s_lfs, path) != 0)
  {
    return TARS_ERR_NOT_FOUND;
  }

  return TARS_OK;
}

tars_status_t TarsLfs_MkDir(const char *path)
{
  int err;

  if ((path == NULL) || (s_lfs_mounted == 0U))
  {
    return TARS_ERR_PARAM;
  }

  err = lfs_mkdir(&s_lfs, path);
  if (err != 0 && err != LFS_ERR_EXIST)
  {
    return TARS_ERR_FLASH;
  }

  return TARS_OK;
}

tars_status_t TarsLfs_ListDir(const char *path, char *out, uint32_t out_size)
{
  lfs_dir_t dir;
  struct lfs_info info;
  int err;
  int written = 0;

  if ((path == NULL) || (out == NULL) || (out_size == 0U) || (s_lfs_mounted == 0U))
  {
    return TARS_ERR_PARAM;
  }

  out[0] = '\0';
  err = lfs_dir_open(&s_lfs, &dir, path);
  if (err != 0)
  {
    return TARS_ERR_NOT_FOUND;
  }

  while (true)
  {
    err = lfs_dir_read(&s_lfs, &dir, &info);
    if (err <= 0)
    {
      break;
    }

    if ((strcmp(info.name, ".") == 0) || (strcmp(info.name, "..") == 0))
    {
      continue;
    }

    written += snprintf(out + written,
                        (written < (int)out_size) ? (out_size - (uint32_t)written) : 0U,
                        "%s%s %lu\r\n",
                        info.name,
                        (info.type == LFS_TYPE_DIR) ? "/" : "",
                        (unsigned long)info.size);

    if ((uint32_t)written >= out_size)
    {
      break;
    }
  }

  (void)lfs_dir_close(&s_lfs, &dir);
  return TARS_OK;
}

void TarsLfs_FormatInfo(char *out, uint32_t out_size)
{
  if ((out == NULL) || (out_size == 0U))
  {
    return;
  }

  (void)snprintf(out,
                 out_size,
                 "lfs: base=0x%08lX size=%lu blocks=%u block_size=%u mounted=%u\r\n",
                 (unsigned long)TARS_LFS_FLASH_BASE,
                 (unsigned long)TARS_LFS_FLASH_SIZE,
                 (unsigned)TARS_LFS_BLOCK_COUNT,
                 (unsigned)TARS_LFS_BLOCK_SIZE,
                 (unsigned)s_lfs_mounted);
}

tars_status_t TarsLfs_FormatStat(const char *path, char *out, uint32_t out_size)
{
  struct lfs_info info;
  int err;

  if ((path == NULL) || (out == NULL) || (out_size == 0U) || (s_lfs_mounted == 0U))
  {
    return TARS_ERR_PARAM;
  }

  err = lfs_stat(&s_lfs, path, &info);
  if (err != 0)
  {
    return TARS_ERR_NOT_FOUND;
  }

  (void)snprintf(out,
                 out_size,
                 "%s type=%s size=%lu\r\n",
                 path,
                 (info.type == LFS_TYPE_DIR) ? "dir" : "file",
                 (unsigned long)info.size);
  return TARS_OK;
}

tars_status_t TarsLfs_FormatDf(char *out, uint32_t out_size)
{
  lfs_ssize_t used_blocks;
  uint32_t total_blocks;
  uint32_t free_blocks;

  if ((out == NULL) || (out_size == 0U) || (s_lfs_mounted == 0U))
  {
    return TARS_ERR_PARAM;
  }

  used_blocks = lfs_fs_size(&s_lfs);
  if (used_blocks < 0)
  {
    return TARS_ERR_FLASH;
  }

  total_blocks = TARS_LFS_BLOCK_COUNT;
  free_blocks = total_blocks - (uint32_t)used_blocks;

  (void)snprintf(out,
                 out_size,
                 "df: blocks=%u used=%ld free=%lu (%lu bytes free)\r\n",
                 (unsigned)total_blocks,
                 (long)used_blocks,
                 (unsigned long)free_blocks,
                 (unsigned long)(free_blocks * TARS_LFS_BLOCK_SIZE));
  return TARS_OK;
}

static tars_status_t lfs_read_prefix(const char *path, uint8_t *buf, uint32_t buf_size,
                                     uint32_t *out_total, uint32_t *out_read)
{
  lfs_file_t file;
  struct lfs_info info;
  lfs_ssize_t nread;
  int err;

  if (out_total != NULL)
  {
    *out_total = 0U;
  }

  if (out_read != NULL)
  {
    *out_read = 0U;
  }

  err = lfs_stat(&s_lfs, path, &info);
  if (err != 0)
  {
    return TARS_ERR_NOT_FOUND;
  }

  if (info.type != LFS_TYPE_REG)
  {
    return TARS_ERR_PARAM;
  }

  if (out_total != NULL)
  {
    *out_total = (uint32_t)info.size;
  }

  if (buf_size == 0U)
  {
    return TARS_OK;
  }

  err = lfs_file_open_cfg(path, LFS_O_RDONLY, &file);
  if (err != 0)
  {
    return TARS_ERR_FLASH;
  }

  nread = lfs_file_read(&s_lfs, &file, buf, buf_size);
  (void)lfs_file_close(&s_lfs, &file);

  if (nread < 0)
  {
    return TARS_ERR_FLASH;
  }

  if (out_read != NULL)
  {
    *out_read = (uint32_t)nread;
  }

  return TARS_OK;
}

tars_status_t TarsLfs_FormatCat(const char *path, char *out, uint32_t out_size, uint32_t max_bytes)
{
  uint8_t buf[512];
  uint32_t total = 0U;
  uint32_t shown = 0U;
  uint32_t copy;
  uint32_t i;
  tars_status_t st;
  int written = 0;

  if ((path == NULL) || (out == NULL) || (out_size == 0U))
  {
    return TARS_ERR_PARAM;
  }

  if (max_bytes == 0U || max_bytes > sizeof(buf))
  {
    max_bytes = sizeof(buf);
  }

  st = lfs_read_prefix(path, buf, max_bytes, &total, &shown);
  if (st != TARS_OK)
  {
    return st;
  }

  written = snprintf(out,
                     out_size,
                     "cat %s size=%lu shown=%lu%s\r\n",
                     path,
                     (unsigned long)total,
                     (unsigned long)shown,
                     (shown < total) ? " [truncated]" : "");
  if (written < 0 || (uint32_t)written >= out_size)
  {
    return TARS_OK;
  }

  copy = shown;
  if ((uint32_t)written + copy + 2U >= out_size)
  {
    copy = out_size - (uint32_t)written - 3U;
  }

  for (i = 0U; i < copy; i++)
  {
    char ch = (char)buf[i];

    if (ch == '\0' || (ch < 0x20 && ch != '\r' && ch != '\n') || ch == 0x7FU)
    {
      ch = '.';
    }

    out[written + (int)i] = ch;
  }

  out[written + (int)copy] = '\0';
  if (copy > 0U && (uint32_t)written + 2U < out_size)
  {
    out[written + (int)copy] = '\r';
    out[written + (int)copy + 1] = '\n';
    out[written + (int)copy + 2] = '\0';
  }

  return TARS_OK;
}

tars_status_t TarsLfs_FormatHex(const char *path, char *out, uint32_t out_size, uint32_t max_bytes)
{
  uint8_t buf[256];
  uint32_t total = 0U;
  uint32_t shown = 0U;
  uint32_t i;
  tars_status_t st;
  int written = 0;

  if ((path == NULL) || (out == NULL) || (out_size == 0U))
  {
    return TARS_ERR_PARAM;
  }

  if (max_bytes == 0U || max_bytes > sizeof(buf))
  {
    max_bytes = sizeof(buf);
  }

  st = lfs_read_prefix(path, buf, max_bytes, &total, &shown);
  if (st != TARS_OK)
  {
    return st;
  }

  written = snprintf(out,
                     out_size,
                     "hex %s size=%lu shown=%lu%s\r\n",
                     path,
                     (unsigned long)total,
                     (unsigned long)shown,
                     (shown < total) ? " [truncated]" : "");
  if (written < 0 || (uint32_t)written >= out_size)
  {
    return TARS_OK;
  }

  for (i = 0U; i < shown; i++)
  {
    int n;

    if ((uint32_t)written + 4U >= out_size)
    {
      break;
    }

    n = snprintf(out + written, out_size - (uint32_t)written, "%02X ", (unsigned)buf[i]);
    if (n <= 0)
    {
      break;
    }

    written += n;
    if (((i + 1U) % 16U) == 0U)
    {
      if ((uint32_t)written + 2U >= out_size)
      {
        break;
      }

      out[written++] = '\r';
      out[written++] = '\n';
      out[written] = '\0';
    }
  }

  if (shown > 0U && ((shown % 16U) != 0U) && (uint32_t)written + 2U < out_size)
  {
    out[written++] = '\r';
    out[written++] = '\n';
    out[written] = '\0';
  }

  return TARS_OK;
}
