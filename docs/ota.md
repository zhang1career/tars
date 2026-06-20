# OTA — A/B Dual Bank (planned)

Firmware updates use **two independent flash slots**. LittleFS (apps/config) is **not** involved in OTA.

## Layout

| Bank | Sectors | Address | Size | Role |
|------|---------|---------|------|------|
| **A** | 0–6 | `0x08000000` | 384 KiB | Active firmware (current linker image) |
| **B** | 12–13 (Bank2) | `0x08100000` | 256 KiB | Staging / alternate firmware |

LittleFS remains at sectors **7–8** (`0x08060000`). Native app XIP slots start at sector **9**.

## Boot flow (target)

1. ROM / minimal **bootloader** reads boot metadata (CRC, version, `active_bank`).
2. Jump to **A** or **B** if image valid.
3. On failed boot from B, fall back to A.

## Upgrade flow (USB, Phase 1)

1. Host sends firmware image over CDC (`ota begin <size>` … binary … `ota commit`).
2. Device writes to **inactive** bank only (erase sectors 12–13, program, verify CRC).
3. Mark B pending; `ota reboot` swaps `active_bank` and resets.
4. New image runs; on success app confirms (`ota confirm`) or rollback on watchdog failure.

## API stub

See `App/tars_ota.c` — `TarsOta_GetActiveBank()`, `TarsOta_GetStagedBank()`, status only until bootloader lands.

## Flash tool

Day-to-day dev still uses `./tools/flash.sh` (partition mode, bank A only). OTA will program bank B without touching A or LittleFS.
