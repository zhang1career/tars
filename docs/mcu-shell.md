# MCU shell and pin mapping

TARS exposes on-chip hardware through the **`mcu`** shell command. Peripheral names (e.g. `tim1_ch1`, `usart1_tx`) are MCU-generic; **which physical pin** they use is defined per board/package in a pin map CSV.

## Shell

```
mcu info
mcu help
mcu gpio write pg13 0      # LD3 on (active low on DISC1)
mcu gpio read pg13
mcu gpio list
mcu pinmap                  # peripheral signal -> pin routing
mcu tim status              # stub (future)
mcu adc|dac|can|uart status # stub (future)
```

Pin names use **bank + number**: `pg13` = port G pin 13. Board aliases such as `ld3` are also accepted when listed in the pin map.

## Pin map (single source: CSV)

| Path | Role |
|------|------|
| `tools/pinmap/<board>.csv` | **Source of truth** — edit this |
| `generated/pinmap/<board>.c` | Auto-generated at build time (gitignored) |

Build runs `tools/pinmap/pinmap-gen.py` before compile. Current board: **`stm32f429i-disc1`** (`TARS_BOARD_ID` in `App/tars_platform.h` and `CMakeLists.txt`).

When porting to another STM32 (F0/F3/F4, different package):

1. Add `tools/pinmap/<new-board>.csv`.
2. Set `TARS_BOARD_ID` in `CMakeLists.txt` and `App/tars_platform.h`.
3. Rebuild — generated C appears under `generated/pinmap/`.

Peripheral signal names stay the same across chips where the IP block is identical; only the `pin` column changes.

See also [tools/pinmap/README.md](../tools/pinmap/README.md).

## Lua API

```lua
tars.gpio_write("pg13", 0)
tars.gpio_read("pg13")
```

Only pins listed in the active GPIO table can be used.
