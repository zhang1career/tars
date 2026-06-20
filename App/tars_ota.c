#include "tars_ota.h"
#include "tars_platform.h"
#include <stdio.h>

/*
 * OTA (not MVP): A/B firmware banks only — no LittleFS involvement.
 *
 * Reserved layout (tars_platform.h):
 *   Bank A: TARS_FW_FLASH_*     sectors 0-6   active runtime today
 *   Bank B: TARS_OTA_FLASH_*    sectors 12-13 staging / alternate image
 *
 * Future USB flow (stub hooks only):
 *   ota begin <size>  -> erase/program inactive bank
 *   ota commit        -> verify CRC, mark pending
 *   ota reboot        -> bootloader swaps bank
 *
 * Boot metadata page TBD (e.g. backup register or tail of bank B).
 */

static tars_ota_bank_t s_active_bank = TARS_OTA_BANK_A;

void TarsOta_Init(void)
{
  /* Bootloader will set active bank; stub assumes A until OTA lands. */
  s_active_bank = TARS_OTA_BANK_A;
}

tars_ota_bank_t TarsOta_GetActiveBank(void)
{
  return s_active_bank;
}

tars_ota_bank_t TarsOta_GetStagedBank(void)
{
  return (s_active_bank == TARS_OTA_BANK_A) ? TARS_OTA_BANK_B : TARS_OTA_BANK_A;
}

void TarsOta_FormatStatus(char *out, uint32_t out_size)
{
  if ((out == NULL) || (out_size == 0U))
  {
    return;
  }

  (void)snprintf(out,
                 out_size,
                 "ota: active=%c staged=%c bankB=0x%08lX size=%luK (stub)\r\n",
                 (s_active_bank == TARS_OTA_BANK_A) ? 'A' : 'B',
                 (TarsOta_GetStagedBank() == TARS_OTA_BANK_A) ? 'A' : 'B',
                 (unsigned long)TARS_OTA_FLASH_BASE,
                 (unsigned long)(TARS_OTA_FLASH_SIZE / 1024U));
}
