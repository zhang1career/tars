# STM32F429I-DISCO incoming inspect

Bring-up firmware for MB1075 (`32F429IDISCOVERY` / `STM32F429I-DISC1`) after
MCU rework or as a board incoming check.

The on-board ST-LINK/V2 (PID `0x3748`) has **no VCP**. Results live in a
fixed SRAM mailbox at `0x2002F000` (`incoming_report.h`).

## What it checks

| Check | Pass condition |
|-------|----------------|
| MCU ID | `DBGMCU_IDCODE` device `0x419` (F42x/F43x) |
| Flash size | factory word = 2048 KiB |
| SRAM / CCM | walking-ones on scratch + CCM |
| VDD | 3.10–3.60 V pass; 2.80–3.80 V warn (via VREFINT) |
| SDRAM | 8 MiB at `0xD0000000`: data walk, address uniqueness, PRNG |
| Gyro | SPI5 `WHO_AM_I` = `0xD3` (I3G4250D) or `0xD4` (L3GD20) |
| Touch | I2C3 ACK at `0x41` (STMPE811) or `0x48` (SX8651) |
| LCD | ILI9341 init + LTDC color bars; ID `0x9341` is pass, missing ID is warn |

LEDs after the suite:

- **LD3 slow blink** — all pass
- **LD3/LD4 alternate** — warn only (typical: VDD ~2.9 V or LCD ID unread)
- **LD4 fast blink** — at least one fail

The panel should show eight vertical color bars if SDRAM + LTDC came up.

## Build / flash / read

From this directory:

```bash
cmake --preset Release && cmake --build --preset Release
python3 tools/run.py
```

`tools/run.py` programs the ELF, waits for `done`, dumps the mailbox, and
prints a pass/fail table. Use `--no-flash` to only read a report already
in SRAM.

Manual OpenOCD read:

```text
mdw 0x2002F000 32
```

`summary` is a 32-byte ASCII field (`INCOMING PASS` / `WARN` / `FAIL`).
