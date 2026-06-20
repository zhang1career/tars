# Board pin maps (source of truth)

Each board has one CSV file: `<board-id>.csv`.

At build time, `pinmap-gen.py` converts it to C under `generated/pinmap/` (gitignored).

## CSV format

```csv
@board,stm32f429i-disc1
@mcu,stm32f429
@package,lqfp176

[periph]
signal,pin
tim1_ch1,pa8

[gpio]
pin,alias
pg13,ld3
```

- **Peripheral signals** (`tim1_ch1`, `usart1_tx`, …) are MCU-generic.
- **Pin names** (`pa8`, `pg13`, …) vary by board/package.
- **GPIO aliases** (`ld3`) are optional board labels.

## Manual generation

```bash
python3 tools/pinmap/pinmap-gen.py tools/pinmap/stm32f429i-disc1.csv \
  -o generated/pinmap/stm32f429i-disc1.c
```

CMake runs this automatically when `TARS_BOARD_ID` matches the CSV basename.
