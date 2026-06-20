#ifndef TARS_VFS_H
#define TARS_VFS_H

#include <stdint.h>

#define TARS_VFS_PATH_CONSOLE     "/dev/console"
#define TARS_VFS_PATH_LCD         "/dev/lcd"
#define TARS_VFS_PATH_LOG         "/dev/log"

#define TARS_IO_SINK_CDC          0x01U
#define TARS_IO_SINK_LCD          0x02U
#define TARS_IO_SINK_BOTH         (TARS_IO_SINK_CDC | TARS_IO_SINK_LCD)

void TarsVfs_Init(void);
int TarsVfs_Write(const char *path, const char *data, uint16_t len);
void TarsVfs_WriteLog(const char *msg);
void TarsVfs_SetLogSinks(uint8_t sinks);
uint8_t TarsVfs_GetLogSinks(void);

#endif /* TARS_VFS_H */
