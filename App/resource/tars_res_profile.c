#include "tars_res_profile.h"
#include "tars_res_mgr.h"
#include "tars_res_pwm.h"
#include "tars_mcu_pinmap.h"
#include "tars_fw_identity.h"
#include "tars_lfs.h"
#include "tars_platform.h"
#include "tars_app.h"
#include "tars_crc.h"
#include <stdio.h>
#include <stddef.h>
#include <string.h>

#define TARS_RES_PROFILE_MAGIC      0x54525350UL  /* 'TRSP' */
#define TARS_RES_PROFILE_VERSION    2U
#define TARS_RES_PROFILE_MAX_GRANTS 32U
#define TARS_RES_PROFILE_MAX_PWM    16U
#define TARS_RES_PROFILE_MAX_TIM    4U
#define TARS_RES_PROFILE_ID_LEN     16U
#define TARS_RES_PROFILE_TIM_LEN    8U
#define TARS_BOARD_ID_STORE_LEN     24U

typedef struct __attribute__((packed)) {
  char     id[TARS_RES_PROFILE_ID_LEN];
  uint8_t  owner;
  uint8_t  reserved[3];
} tars_prof_grant_t;

typedef struct __attribute__((packed)) {
  char     channel[TARS_RES_PROFILE_ID_LEN];
  uint8_t  duty_pct;
  uint8_t  boot_enable;
  uint8_t  reserved[2];
} tars_prof_pwm_t;

typedef struct __attribute__((packed)) {
  char     tim_id[TARS_RES_PROFILE_TIM_LEN];
  uint32_t freq_hz;
} tars_prof_tim_t;

typedef struct __attribute__((packed)) {
  uint32_t magic;
  uint16_t version;
  uint16_t flags;
  char     board_id[TARS_BOARD_ID_STORE_LEN];
  uint32_t fw_crc32;
  uint32_t fw_image_size;
  uint32_t grant_count;
  uint32_t pwm_count;
  uint32_t tim_count;
  uint32_t body_crc32;
} tars_prof_hdr_t;

typedef struct {
  tars_prof_hdr_t hdr;
  tars_prof_grant_t grants[TARS_RES_PROFILE_MAX_GRANTS];
  tars_prof_pwm_t pwm[TARS_RES_PROFILE_MAX_PWM];
  tars_prof_tim_t tim[TARS_RES_PROFILE_MAX_TIM];
  uint8_t valid;
} tars_prof_staged_t;

static tars_prof_staged_t s_staged;
static uint8_t s_profile_file_buf[512];

static uint32_t profile_build_file_size(const tars_prof_hdr_t *hdr)
{
  return (uint32_t)sizeof(tars_prof_hdr_t) +
         hdr->grant_count * (uint32_t)sizeof(tars_prof_grant_t) +
         hdr->pwm_count * (uint32_t)sizeof(tars_prof_pwm_t) +
         hdr->tim_count * (uint32_t)sizeof(tars_prof_tim_t);
}

static int profile_encode_file(const tars_prof_hdr_t *hdr,
                               const tars_prof_grant_t *grants,
                               const tars_prof_pwm_t *pwm,
                               const tars_prof_tim_t *tim,
                               uint8_t *out,
                               uint32_t out_cap,
                               uint32_t *size_out)
{
  uint32_t size = profile_build_file_size(hdr);

  if ((size_out == NULL) || (size > out_cap))
  {
    return TARS_RES_PROFILE_ERR_PARAM;
  }

  memcpy(out, hdr, sizeof(tars_prof_hdr_t));
  size = (uint32_t)sizeof(tars_prof_hdr_t);

  if (hdr->grant_count > 0U)
  {
    memcpy(out + size, grants, hdr->grant_count * sizeof(tars_prof_grant_t));
    size += hdr->grant_count * (uint32_t)sizeof(tars_prof_grant_t);
  }
  if (hdr->pwm_count > 0U)
  {
    memcpy(out + size, pwm, hdr->pwm_count * sizeof(tars_prof_pwm_t));
    size += hdr->pwm_count * (uint32_t)sizeof(tars_prof_pwm_t);
  }
  if (hdr->tim_count > 0U)
  {
    memcpy(out + size, tim, hdr->tim_count * sizeof(tars_prof_tim_t));
    size += hdr->tim_count * (uint32_t)sizeof(tars_prof_tim_t);
  }

  *size_out = size;
  return 0;
}

static uint32_t profile_body_crc(const tars_prof_hdr_t *hdr,
                                 const tars_prof_grant_t *grants,
                                 const tars_prof_pwm_t *pwm,
                                 const tars_prof_tim_t *tim)
{
  uint32_t crc = 0xFFFFFFFFUL;
  uint32_t grant_bytes;
  uint32_t pwm_bytes;
  uint32_t tim_bytes;

  crc = TarsCrc32((const uint8_t *)(const void *)hdr,
                  (uint32_t)offsetof(tars_prof_hdr_t, body_crc32),
                  crc);

  grant_bytes = hdr->grant_count * (uint32_t)sizeof(tars_prof_grant_t);
  pwm_bytes = hdr->pwm_count * (uint32_t)sizeof(tars_prof_pwm_t);
  tim_bytes = hdr->tim_count * (uint32_t)sizeof(tars_prof_tim_t);

  if (grant_bytes > 0U)
  {
    crc = TarsCrc32((const uint8_t *)(const void *)grants, grant_bytes, crc);
  }
  if (pwm_bytes > 0U)
  {
    crc = TarsCrc32((const uint8_t *)(const void *)pwm, pwm_bytes, crc);
  }
  if (tim_bytes > 0U)
  {
    crc = TarsCrc32((const uint8_t *)(const void *)tim, tim_bytes, crc);
  }

  return crc ^ 0xFFFFFFFFUL;
}

static int profile_fw_token_matches(const tars_prof_hdr_t *hdr)
{
  const tars_fw_identity_t *id = TarsFwIdentity_Get();

  if ((id == NULL) || (id->image_size == 0U))
  {
    return 0;
  }

  return ((hdr->fw_crc32 == id->image_crc32) &&
          (hdr->fw_image_size == id->image_size)) ? 1 : 0;
}

static int profile_board_matches(const tars_prof_hdr_t *hdr)
{
  return (strncmp(hdr->board_id, TarsMcuPinmap_BoardId(), TARS_BOARD_ID_STORE_LEN) == 0) ? 1 : 0;
}

static tars_owner_t profile_catalog_default_owner(const tars_res_catalog_entry_t *cat)
{
  return cat->default_owner;
}

static int profile_collect_grants(tars_prof_grant_t *out, uint32_t *count_out)
{
  uint32_t cat_count = 0U;
  const tars_res_catalog_entry_t *cat = TarsMcuPinmap_GetResCatalog(&cat_count);
  uint32_t i;
  uint32_t n = 0U;

  for (i = 0U; i < cat_count; i++)
  {
    tars_owner_t runtime;
    tars_owner_t def;

    if (cat[i].id == NULL)
    {
      continue;
    }

    def = profile_catalog_default_owner(&cat[i]);
    runtime = TarsResMgr_GetOwner(cat[i].id);

    if (runtime == def)
    {
      continue;
    }

    if (runtime == TARS_OWNER_SYSTEM)
    {
      continue;
    }

    if (n >= TARS_RES_PROFILE_MAX_GRANTS)
    {
      break;
    }

    memset(out[n].id, 0, sizeof(out[n].id));
    strncpy(out[n].id, cat[i].id, TARS_RES_PROFILE_ID_LEN - 1U);
    out[n].owner = (uint8_t)runtime;
    out[n].reserved[0] = 0U;
    n++;
  }

  *count_out = n;
  return 0;
}

static int profile_collect_pwm(tars_prof_pwm_t *out, uint32_t *count_out)
{
  uint32_t pwm_count = 0U;
  const tars_mcu_pwm_entry_t *table = TarsMcuPinmap_GetPwmTable(&pwm_count);
  uint32_t i;
  uint32_t n = 0U;

  for (i = 0U; i < pwm_count; i++)
  {
    const char *ch = table[i].channel;
    int persist = 0;
    uint8_t duty = 0U;

    if ((ch == NULL) || (strncmp(ch, "tim1_ch", 7) == 0))
    {
      continue;
    }

    if (TarsResPwm_GetPersist(ch, &persist) != 0)
    {
      persist = 0;
    }

    if (TarsResPwm_GetDuty(ch, &duty) != 0)
    {
      duty = 0U;
    }

    if ((duty == 0U) && (persist == 0))
    {
      continue;
    }

    if (n >= TARS_RES_PROFILE_MAX_PWM)
    {
      break;
    }

    memset(out[n].channel, 0, sizeof(out[n].channel));
    strncpy(out[n].channel, ch, TARS_RES_PROFILE_ID_LEN - 1U);
    out[n].duty_pct = duty;
    out[n].boot_enable = (persist != 0) ? 1U : 0U;
    n++;
  }

  *count_out = n;
  return 0;
}

static int profile_collect_tim(tars_prof_tim_t *out, uint32_t *count_out)
{
  uint32_t pwm_count = 0U;
  const tars_mcu_pwm_entry_t *table = TarsMcuPinmap_GetPwmTable(&pwm_count);
  uint32_t i;
  uint32_t n = 0U;

  for (i = 0U; i < pwm_count; i++)
  {
    const char *tim_id = table[i].tim_id;
    uint32_t freq = 0U;
    uint32_t j;
    int seen = 0;

    if ((tim_id == NULL) || (table[i].tim == TIM1))
    {
      continue;
    }

    for (j = 0U; j < n; j++)
    {
      if (strncmp(out[j].tim_id, tim_id, TARS_RES_PROFILE_TIM_LEN) == 0)
      {
        seen = 1;
        break;
      }
    }

    if (seen != 0)
    {
      continue;
    }

    if (TarsResPwm_TimFreqConfigured(tim_id) == 0)
    {
      continue;
    }

    if (TarsResPwm_GetTimFreq(tim_id, &freq) != 0)
    {
      continue;
    }

    if (freq == 0U)
    {
      continue;
    }

    if (n >= TARS_RES_PROFILE_MAX_TIM)
    {
      break;
    }

    memset(out[n].tim_id, 0, sizeof(out[n].tim_id));
    strncpy(out[n].tim_id, tim_id, TARS_RES_PROFILE_TIM_LEN - 1U);
    out[n].freq_hz = freq;
    n++;
  }

  *count_out = n;
  return 0;
}

static int profile_ensure_config_dir(void)
{
  /* Profile lives under /sys (same partition dir as catalog). */
  if (TarsLfs_MkDir("/sys") != TARS_OK)
  {
    return TARS_RES_PROFILE_ERR_IO;
  }

  return 0;
}

static int profile_parse_buf(const uint8_t *buf, uint32_t size)
{
  const tars_prof_hdr_t *hdr = (const tars_prof_hdr_t *)(const void *)buf;
  uint32_t expected;
  uint32_t offset;
  const tars_prof_grant_t *grants;
  const tars_prof_pwm_t *pwm;
  const tars_prof_tim_t *tim;
  uint32_t body_crc;

  if (size < sizeof(tars_prof_hdr_t))
  {
    return TARS_RES_PROFILE_ERR_CRC;
  }

  if (hdr->magic != TARS_RES_PROFILE_MAGIC)
  {
    return TARS_RES_PROFILE_ERR_CRC;
  }

  if (hdr->version != TARS_RES_PROFILE_VERSION)
  {
    return TARS_RES_PROFILE_ERR_CRC;
  }

  if ((hdr->grant_count > TARS_RES_PROFILE_MAX_GRANTS) ||
      (hdr->pwm_count > TARS_RES_PROFILE_MAX_PWM) ||
      (hdr->tim_count > TARS_RES_PROFILE_MAX_TIM))
  {
    return TARS_RES_PROFILE_ERR_CRC;
  }

  expected = (uint32_t)sizeof(tars_prof_hdr_t) +
             hdr->grant_count * (uint32_t)sizeof(tars_prof_grant_t) +
             hdr->pwm_count * (uint32_t)sizeof(tars_prof_pwm_t) +
             hdr->tim_count * (uint32_t)sizeof(tars_prof_tim_t);

  if (size < expected)
  {
    return TARS_RES_PROFILE_ERR_CRC;
  }

  offset = (uint32_t)sizeof(tars_prof_hdr_t);
  grants = (const tars_prof_grant_t *)(const void *)(buf + offset);
  offset += hdr->grant_count * (uint32_t)sizeof(tars_prof_grant_t);
  pwm = (const tars_prof_pwm_t *)(const void *)(buf + offset);
  offset += hdr->pwm_count * (uint32_t)sizeof(tars_prof_pwm_t);
  tim = (const tars_prof_tim_t *)(const void *)(buf + offset);

  body_crc = profile_body_crc(hdr, grants, pwm, tim);
  if (body_crc != hdr->body_crc32)
  {
    return TARS_RES_PROFILE_ERR_CRC;
  }

  if (profile_board_matches(hdr) == 0)
  {
    return TARS_RES_PROFILE_ERR_CRC;
  }

  memset(&s_staged, 0, sizeof(s_staged));
  s_staged.hdr = *hdr;
  if (hdr->grant_count > 0U)
  {
    memcpy(s_staged.grants, grants, hdr->grant_count * sizeof(tars_prof_grant_t));
  }
  if (hdr->pwm_count > 0U)
  {
    memcpy(s_staged.pwm, pwm, hdr->pwm_count * sizeof(tars_prof_pwm_t));
  }
  if (hdr->tim_count > 0U)
  {
    memcpy(s_staged.tim, tim, hdr->tim_count * sizeof(tars_prof_tim_t));
  }

  s_staged.valid = 1U;
  return 0;
}

int TarsResProfile_HasStaged(void)
{
  return (s_staged.valid != 0U) ? 1 : 0;
}

int TarsResProfile_PurgeStale(void)
{
  uint32_t size = 0U;
  tars_status_t st;

  if (TarsLfs_IsMounted() == 0)
  {
    return 0;
  }

  st = TarsLfs_ReadFile(TARS_LFS_PATH_RES_PROFILE,
                       s_profile_file_buf,
                       (uint32_t)sizeof(s_profile_file_buf),
                       &size);
  if (st != TARS_OK)
  {
    return 0;
  }

  if (profile_parse_buf(s_profile_file_buf, size) != 0)
  {
    (void)TarsLfs_RemoveFile(TARS_LFS_PATH_RES_PROFILE);
    s_staged.valid = 0U;
    return 1;
  }

  if (profile_fw_token_matches(&s_staged.hdr) == 0)
  {
    (void)TarsLfs_RemoveFile(TARS_LFS_PATH_RES_PROFILE);
    s_staged.valid = 0U;
    return 1;
  }

  return 0;
}

int TarsResProfile_Load(void)
{
  uint32_t size = 0U;
  tars_status_t st;
  int pr;

  s_staged.valid = 0U;

  if (TarsLfs_IsMounted() == 0)
  {
    return TARS_RES_PROFILE_ERR_IO;
  }

  st = TarsLfs_ReadFile(TARS_LFS_PATH_RES_PROFILE,
                       s_profile_file_buf,
                       (uint32_t)sizeof(s_profile_file_buf),
                       &size);
  if (st != TARS_OK)
  {
    return TARS_RES_PROFILE_ERR_NONE;
  }

  pr = profile_parse_buf(s_profile_file_buf, size);
  if (pr != 0)
  {
    (void)TarsLfs_RemoveFile(TARS_LFS_PATH_RES_PROFILE);
    return pr;
  }

  if (profile_fw_token_matches(&s_staged.hdr) == 0)
  {
    (void)TarsLfs_RemoveFile(TARS_LFS_PATH_RES_PROFILE);
    s_staged.valid = 0U;
    return TARS_RES_PROFILE_ERR_STALE;
  }

  return 0;
}

static int profile_pwm0_boot_on_tim1(void)
{
  uint32_t i;

  if (s_staged.valid == 0U)
  {
    return 0;
  }

  for (i = 0U; i < s_staged.hdr.pwm_count; i++)
  {
    if ((s_staged.pwm[i].boot_enable != 0U) &&
        (strncmp(s_staged.pwm[i].channel, "pwm0", TARS_RES_PROFILE_ID_LEN) == 0))
    {
      return 1;
    }
  }

  return 0;
}

int TarsResProfile_Apply(void)
{
  uint32_t i;
  int st;

  if (s_staged.valid == 0U)
  {
    return TARS_RES_PROFILE_ERR_NONE;
  }

  if (profile_fw_token_matches(&s_staged.hdr) == 0)
  {
    (void)TarsLfs_RemoveFile(TARS_LFS_PATH_RES_PROFILE);
    s_staged.valid = 0U;
    return TARS_RES_PROFILE_ERR_STALE;
  }

  for (i = 0U; i < s_staged.hdr.grant_count; i++)
  {
    tars_owner_t owner = (tars_owner_t)s_staged.grants[i].owner;

    if (owner > TARS_OWNER_SYSTEM)
    {
      continue;
    }

    (void)TarsResMgr_Grant(s_staged.grants[i].id, owner);
  }

  for (i = 0U; i < s_staged.hdr.tim_count; i++)
  {
    if (strncmp(s_staged.tim[i].tim_id, "tim1", TARS_RES_PROFILE_TIM_LEN) == 0)
    {
      continue;
    }

    (void)TarsResPwm_SetFreq(s_staged.tim[i].tim_id, s_staged.tim[i].freq_hz);
  }

  for (i = 0U; i < s_staged.hdr.pwm_count; i++)
  {
    (void)TarsResPwm_SetDuty(s_staged.pwm[i].channel, (float)s_staged.pwm[i].duty_pct);
    (void)TarsResPwm_SetPersist(s_staged.pwm[i].channel, (int)s_staged.pwm[i].boot_enable);

    if (s_staged.pwm[i].boot_enable != 0U)
    {
      st = TarsResPwm_Enable(s_staged.pwm[i].channel, 1);
      (void)st;
    }
  }

  return 0;
}

int TarsResProfile_Save(void)
{
  tars_prof_hdr_t hdr;
  uint32_t grant_count = 0U;
  uint32_t pwm_count = 0U;
  uint32_t tim_count = 0U;
  uint32_t size = 0U;
  const tars_fw_identity_t *fw = TarsFwIdentity_Get();

  if (fw == NULL || fw->image_size == 0U)
  {
    return TARS_RES_PROFILE_ERR_IO;
  }

  if (TarsLfs_IsMounted() == 0)
  {
    return TARS_RES_PROFILE_ERR_IO;
  }

  if (profile_ensure_config_dir() != 0)
  {
    return TARS_RES_PROFILE_ERR_IO;
  }

  (void)profile_collect_grants(s_staged.grants, &grant_count);
  (void)profile_collect_pwm(s_staged.pwm, &pwm_count);
  (void)profile_collect_tim(s_staged.tim, &tim_count);

  memset(&hdr, 0, sizeof(hdr));
  hdr.magic = TARS_RES_PROFILE_MAGIC;
  hdr.version = TARS_RES_PROFILE_VERSION;
  hdr.flags = 0U;
  strncpy(hdr.board_id, TarsMcuPinmap_BoardId(), TARS_BOARD_ID_STORE_LEN - 1U);
  hdr.fw_crc32 = fw->image_crc32;
  hdr.fw_image_size = fw->image_size;
  hdr.grant_count = grant_count;
  hdr.pwm_count = pwm_count;
  hdr.tim_count = tim_count;
  hdr.body_crc32 = profile_body_crc(&hdr,
                                    s_staged.grants,
                                    s_staged.pwm,
                                    s_staged.tim);

  if (profile_encode_file(&hdr,
                          s_staged.grants,
                          s_staged.pwm,
                          s_staged.tim,
                          s_profile_file_buf,
                          (uint32_t)sizeof(s_profile_file_buf),
                          &size) != 0)
  {
    return TARS_RES_PROFILE_ERR_PARAM;
  }

  if (TarsLfs_WriteFile(TARS_LFS_PATH_RES_PROFILE, s_profile_file_buf, size) != TARS_OK)
  {
    return TARS_RES_PROFILE_ERR_IO;
  }

  {
    uint32_t on_disk = 0U;

    if ((TarsLfs_ReadFile(TARS_LFS_PATH_RES_PROFILE,
                          s_profile_file_buf,
                          (uint32_t)sizeof(s_profile_file_buf),
                          &on_disk) != TARS_OK) ||
        (on_disk != size))
    {
      return TARS_RES_PROFILE_ERR_IO;
    }
  }

  return profile_parse_buf(s_profile_file_buf, size);
}

int TarsResProfile_Clear(void)
{
  s_staged.valid = 0U;

  if (TarsLfs_IsMounted() == 0)
  {
    return TARS_RES_PROFILE_ERR_IO;
  }

  (void)TarsLfs_RemoveFile(TARS_LFS_PATH_RES_PROFILE);
  return 0;
}

int TarsResProfile_FormatStored(char *out, uint32_t out_size)
{
  uint32_t i;
  int written;

  if ((out == NULL) || (out_size == 0U))
  {
    return TARS_RES_PROFILE_ERR_PARAM;
  }

  if (s_staged.valid == 0U)
  {
    (void)snprintf(out, out_size, "res profile: (none staged)\r\n");
    return 0;
  }

  written = snprintf(out,
                     out_size,
                     "res profile: board=%s fw_crc=0x%08lx fw_size=%lu\r\n"
                     "  grants=%lu pwm=%lu tim=%lu\r\n",
                     s_staged.hdr.board_id,
                     (unsigned long)s_staged.hdr.fw_crc32,
                     (unsigned long)s_staged.hdr.fw_image_size,
                     (unsigned long)s_staged.hdr.grant_count,
                     (unsigned long)s_staged.hdr.pwm_count,
                     (unsigned long)s_staged.hdr.tim_count);
  if ((written < 0) || ((uint32_t)written >= out_size))
  {
    return 0;
  }

  for (i = 0U; i < s_staged.hdr.grant_count; i++)
  {
    written = (int)strlen(out);
    (void)snprintf(out + (uint32_t)written,
                   out_size - (uint32_t)written,
                   "  grant %s -> %s\r\n",
                   s_staged.grants[i].id,
                   TarsOwner_ToString((tars_owner_t)s_staged.grants[i].owner));
  }

  for (i = 0U; i < s_staged.hdr.pwm_count; i++)
  {
    written = (int)strlen(out);
    (void)snprintf(out + (uint32_t)written,
                   out_size - (uint32_t)written,
                   "  pwm %s duty=%u boot=%u\r\n",
                   s_staged.pwm[i].channel,
                   (unsigned)s_staged.pwm[i].duty_pct,
                   (unsigned)s_staged.pwm[i].boot_enable);
  }

  for (i = 0U; i < s_staged.hdr.tim_count; i++)
  {
    written = (int)strlen(out);
    (void)snprintf(out + (uint32_t)written,
                   out_size - (uint32_t)written,
                   "  tim %s freq=%lu\r\n",
                   s_staged.tim[i].tim_id,
                   (unsigned long)s_staged.tim[i].freq_hz);
  }

  return 0;
}

int TarsResProfile_Pwm0BootOnTim1(void)
{
  return profile_pwm0_boot_on_tim1();
}
