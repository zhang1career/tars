#ifndef TARS_LFS_H
#define TARS_LFS_H

#include <stdint.h>
#include "tars_app.h"

tars_status_t TarsLfs_Init(void);
int TarsLfs_IsMounted(void);
tars_status_t TarsLfs_Format(void);

tars_status_t TarsLfs_WriteFile(const char *path, const uint8_t *data, uint32_t size);
tars_status_t TarsLfs_ReadFile(const char *path, uint8_t *data, uint32_t max_size, uint32_t *out_size);
tars_status_t TarsLfs_RemoveFile(const char *path);
tars_status_t TarsLfs_MkDir(const char *path);

tars_status_t TarsLfs_ListDir(const char *path, char *out, uint32_t out_size);
void TarsLfs_FormatInfo(char *out, uint32_t out_size);
tars_status_t TarsLfs_FormatStat(const char *path, char *out, uint32_t out_size);
tars_status_t TarsLfs_FormatDf(char *out, uint32_t out_size);
tars_status_t TarsLfs_FormatCat(const char *path, char *out, uint32_t out_size, uint32_t max_bytes);
tars_status_t TarsLfs_FormatHex(const char *path, char *out, uint32_t out_size, uint32_t max_bytes);

#endif /* TARS_LFS_H */
