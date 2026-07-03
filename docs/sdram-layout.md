# STM32F429 SDRAM Layout (TARS)

DISC1 on-board **8 MiB** SDRAM on FMC **Bank 2**, base **`0xD0000000`**
(`0xD0000000` … `0xD07FFFFF`). Unlike internal flash, there is no hardware
erase unit — regions are **software reservations** only.

Constants live in `App/tars_platform.h` unless noted. Default firmware builds with
**`TARS_ENABLE_LCD=OFF`** (motor variant); the LCD framebuffer is not allocated
in that configuration.

## Region map (low → high)

| Start        | Size   | End (excl.)  | Macro / source | Use |
|--------------|--------|--------------|----------------|-----|
| `0xD0000000` | 300 KiB | `0xD004B000` | `lcd_fb.c` | **LCD double framebuffer** (RGB565, 240×320×2). Only when `TARS_ENABLE_LCD=ON`. |
| `0xD0028000` | 512 KiB | `0xD00A8000` | `TARS_SDRAM_EXEC_*` | Native app **SDRAM execution / reloc** staging (post-MVP). |
| `0xD00C0000` | 128 KiB | `0xD00E0000` | `TARS_INSTALL_STAGING_*` | USB shell **app install** binary sink (`app install begin`). |
| `0xD00E0000` | 128 KiB | `0xD0100000` | `TARS_LUA_HEAP_*` | **Lua 5.1** allocator heap for scheduled apps. |
| `0xD0100000` | 256 KiB | `0xD0140000` | `TARS_PROBE_CAP_*` | **Probe** trigger-capture buffer → UART DMA. See [probe.md](probe.md). |
| `0xD0140000` | 16 KiB  | `0xD0144000` | `TARS_AWG_WAVE_BASE` + slot 0 | **AWG dac0** sample table (max 8192 × uint16). |
| `0xD0144000` | 16 KiB  | `0xD0148000` | slot 1 stride | **AWG dac1** sample table (max 8192 × uint16). |
| `0xD0148000` | ~7.5 MiB | `0xD0800000` | — | **Unreserved** (available for future use). |

**AWG total:** `TARS_AWG_WAVE_SIZE` = 32 KiB (`2 × TARS_AWG_CH_STRIDE`).

## AWG (dac0 / dac1)

Each channel owns a **fixed, non-overlapping** slice:

```
dac0 (PA4): 0xD0140000 .. 0xD0143FFF   (TARS_AWG_CH_MAX_POINTS = 8192 samples)
dac1 (PA5): 0xD0144000 .. 0xD0147FFF
```

- Populated by `mcu awg gen`, `mcu awg upload`, or `tools/awg-upload.py`.
- Streamed to the DAC by TIM7-triggered circular DMA (zero CPU during playback).
- Upload or `gen` on one channel does not touch the other region.
- See [mcu-shell.md](mcu-shell.md) (AWG section).

## LCD vs fixed regions

With **`TARS_ENABLE_LCD=ON`**, the LTDC double buffer occupies **`0xD0000000`**
for **300 KiB** (`LCD_FB_WIDTH` × `LCD_FB_HEIGHT` × 2 bytes × 2 buffers in
`App/lcd/lcd_fb.c`).

That overlaps the **`TARS_SDRAM_EXEC_*`** reservation at `0xD0028000`. Treat
LCD and the exec/staging map as **mutually exclusive** build modes:

| Build | LCD framebuffer | `TARS_SDRAM_EXEC_*` |
|-------|-----------------|---------------------|
| Default (`TARS_ENABLE_LCD=OFF`) | unused | valid |
| Display variant (`TARS_ENABLE_LCD=ON`) | `0xD0000000` | do not use concurrently |

Regions from **`0xD00C0000` upward** do not overlap the LCD framebuffer.

## Overlap and sharing rules

1. **Do not** place a new buffer inside a reserved range without updating
   `tars_platform.h` and this doc.
2. **App install** and **AWG upload** both use USB shell binary mode but write
   to different sinks (`INSTALL_STAGING` vs `AWG_WAVE_*`).
3. **Probe capture** and **AWG** may both be defined in RAM; avoid running
   capture and AWG on the same channel concurrently at the driver level — there
   is no MPU enforcement, only convention.
4. **Lua heap** is owned by the Lua allocator; do not alias it for DMA unless
   the heap is relocated in code.

## Shell / debug

- Flash map only: `sys part` (internal flash, not SDRAM).
- RTOS + Lua heap usage: `sys top`.
- AWG runtime: `mcu awg status dac0` / `dac1` (does not print addresses).

## Related docs

| Topic | Doc |
|-------|-----|
| Internal flash | [flash-layout.md](flash-layout.md) |
| Probe capture buffer | [probe.md](probe.md) |
| AWG commands | [mcu-shell.md](mcu-shell.md) |
| Constants | `App/tars_platform.h`, `App/lcd/lcd_fb.c` |
