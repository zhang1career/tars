#ifndef TARS_FW_IDENTITY_H
#define TARS_FW_IDENTITY_H

#include <stdint.h>

#define TARS_FW_IDENTITY_MAGIC  0x5446574BU  /* 'TFWK' */

typedef struct {
  uint32_t magic;
  uint32_t image_crc32;
  uint32_t image_size;
} tars_fw_identity_t;

extern const tars_fw_identity_t g_tars_fw_identity;

const tars_fw_identity_t *TarsFwIdentity_Get(void);

#endif /* TARS_FW_IDENTITY_H */
