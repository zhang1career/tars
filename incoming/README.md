# Incoming inspection firmware

Standalone board / DUT bring-up programs. They are **not** TARS firmware:
no USB shell, no Lua, no LittleFS. Each subdirectory is one DUT family.

Results are written to a fixed SRAM mailbox and read back over SWD so a
board without a working VCP (ST-LINK/V2 on MB1075B) still reports.

| DUT | Path |
|-----|------|
| STM32F429I-DISCO / DISC1 (MB1075) | [`stm32f429i-disco/`](stm32f429i-disco/) |
