# STM32F429 Flash Layout (TARS)

2 MiB internal flash, dual bank. **Erase unit = sector** (16–128 KiB).

## Bank 1 — `0x08000000` … `0x080FFFFF` (1 MiB)

| Sector | Size   | Start        | Use |
|--------|--------|--------------|-----|
| 0–3    | 64 KB  | `0x08000000` | **Firmware slot A** (linker, XIP) |
| 4      | 64 KB  | `0x08010000` | |
| 5      | 128 KB | `0x08020000` | |
| 6      | 128 KB | `0x08040000` | |
| 7      | 128 KB | `0x08060000` | **LittleFS block 0** |
| 8      | 128 KB | `0x08080000` | **LittleFS block 1** |
| 9      | 128 KB | `0x080A0000` | Native slot 0 |
| 10     | 128 KB | `0x080C0000` | Native slot 1 |
| 11     | 128 KB | `0x080E0000` | Native slot 2 |

**Firmware:** sectors **0–6**, **384 KiB** (`TARS_FW_FLASH_*`).

**LittleFS:** sectors **7–8**, **256 KiB** (`TARS_LFS_FLASH_*`).

**Native XIP slots (sector 9+, post-MVP Phase 1):** base `0x080A0000`, 128 KiB stride. Not used while `TARS_MVP_LUA_ONLY`.

## Bank 2 — OTA (planned)

| Sector | Start        | Use |
|--------|--------------|-----|
| 12–13  | `0x08100000` | OTA firmware slot B |

## LittleFS paths

| Path | Content |
|------|---------|
| `/sys/catalog` | App registry |
| `/apps/<name>.tlua` | Lua app blobs |

## Rules

1. Linker + ST-LINK program: sectors **0–6** only (`./tools/flash.sh`, default partition mode).
2. Full-chip flash (may wipe apps): `./tools/flash.sh --full` or `FLASH_MODE=full`.
3. App install: LittleFS + native slot sectors only.
4. All writers use `TarsFlash_Lock()`.

See `App/tars_platform.h` for constants.
