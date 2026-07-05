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
uart5_tx,pc12

[gpio]
pin,alias
pg13,ld3

[pwm]
channel,tim,chan,pin,af,default_tenant
pwm0,tim1,1,pa8,AF1,foc
```

- **Pin names** (`pa8`, `pg13`, …) vary by board/package.
- **GPIO aliases** (`ld3`, `b1`) are optional board labels for shell lookup.

## PWM channel IDs (`pwmN`)

Shell and API use **`pwm0`**, **`pwm1`**, … only. Timer/channel/pin mapping
lives in the CSV (`tim`, `chan`, `pin` columns) and in `mcu spec pwmN`.

**Numbering rule:** sort rows by `(tim asc, chan asc, pin asc)`, then assign
`pwm0`, `pwm1`, … in that order.

| Column | Role |
|--------|------|
| `channel` | Resource ID (`pwmN`) |
| `tim` / `chan` / `pin` / `af` | Hardware binding |
| `default_tenant` | Boot default (`none`, `foc`, `system`) |

Example map (stm32f429i-disc1):

| pwmN | tim | chan | pin |
|------|-----|------|-----|
| pwm0 | tim1 | 1 | pa8 |
| pwm1 | tim1 | 2 | pa9 |
| pwm2 | tim1 | 3 | pa10 |
| pwm3 | tim3 | 1 | pb4 |
| pwm4 | tim9 | 1 | pe2 |
| pwm5 | tim9 | 2 | pe4 |
| pwm6 | tim9 | 2 | pe6 |
| pwm7 | tim10 | 1 | pb8 |

When one timer channel maps to multiple pins (e.g. TIM9 CH2 on PE4 and PE6),
each pin gets its own `pwmN` row; disambiguation is by pin column, not by name.

## Manual generation

```bash
python3 tools/pinmap/pinmap-gen.py tools/pinmap/stm32f429i-disc1.csv \
  -o generated/pinmap/stm32f429i-disc1.c
```

CMake runs this automatically when `TARS_BOARD_ID` matches the CSV basename.
