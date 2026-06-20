# TARS App Format (Phase 1 + Phase 2)

## Overview

| Kind | Install source | Execute from | Loader mode |
|------|----------------|--------------|-------------|
| Built-in native C | Linked into firmware | Direct call | n/a |
| Dynamic native C (Phase 1) | `.tapp` fixed slot | Internal Flash XIP | `TARS_LOAD_FIXED` |
| Dynamic native C (Phase 2) | `.tapp` reloc | Flash auto-slot or SDRAM | `TARS_LOAD_RELOC` |
| Lua app | `.tlua` source blob | eLua VM (future) | interpreted |

## Flash map (STM32F429 2 MiB)

| Region | Address | Size |
|--------|---------|------|
| Firmware | `0x08000000` | ≤ 512 KiB |
| Native slot 0..7 | `0x08080000 + n*128K` | 8 × 128 KiB sector, 48 KiB max blob |
| Lua pool | `0x08100000` | 128 KiB |
| Catalog | `0x08120000` | 128 KiB |

## Native header (`tars_app_hdr_t`)

- Magic: `TPAP` (`0x54504150`)
- `flags`:
  - `TARS_LOAD_FIXED (0x0001)` — Phase 1, `link_base` must match slot
  - `TARS_LOAD_RELOC (0x0002)` — Phase 2, apply reloc table
  - `TARS_EXEC_FLASH (0x0010)` — execute from flash
  - `TARS_EXEC_SDRAM (0x0020)` — copy to SDRAM and execute
- Payload layout: `[text][data][relocs]`
- CRC32 over header + payload

## Phase 1: fixed slot workflow

```bash
arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -ffreestanding -nostdlib \
  -T tools/tars-app-fixed.ld -Wl,--defsym=TARS_SLOT_INDEX=0 \
  -I tools/sdk app.c -o app.elf

python3 tools/tars-pack.py native app.elf -o app.tapp \
  --name chaser --slot 0 --mode fixed
```

Device: `app install` (binary transfer TBD) → writes slot 0 → `app submit chaser`.

## Phase 2: relocatable workflow

```bash
arm-none-eabi-gcc ... -T tools/tars-app-pic.ld app.c -o app.elf

python3 tools/tars-pack.py native app.elf -o app.tapp \
  --name sensor --mode reloc
```

Loader picks a free flash slot **or** SDRAM region (`0xD0028000`), copies image, applies `TARS_RELOC_ABS32` entries (`word += load_base`).

## Lua header (`tars_lua_hdr_t`)

- Magic: `TPHL`
- Payload: UTF-8 `.lua` source (not precompiled)
- Same `tars_manifest_t` as native apps

```bash
python3 tools/tars-pack.py lua apps/blink.lua -o blink.tlua --name blink
```

## Resource manifest

- Up to 16 resources per app: `{type, instance, param}`
- **No time dimension** in resource ownership
- Default: exclusive — submit fails if another submitted app holds the same resource
- Whitelist: platform table allows named apps to share a resource with slot handover (`on_acquire` / `on_release` in future)

## Shell commands

```
app list
app slots
app submit <name>
app revoke <name>
app run <name>
```

## API table

Native apps implement:

```c
void app_entry(const tars_api_t *api);
```

All hardware access goes through `tars_api_t` (version `TARS_API_VERSION`).

## Serial install (CDC)

```
# On device (via screen):
app install begin <size> [slot]

# On PC:
python3 tools/tars-send.py blink.tlua -p /dev/tty.usbmodemXXXX
```

Device responds `install: ready`, receives raw bytes, then `install: 0` on success.

## Lua runtime

Lua 5.1 embedded in SDRAM heap (`0xD00E0000`). REPL bindings: `tars.gpio_write`, `tars.sleep`, `tars.log`.
