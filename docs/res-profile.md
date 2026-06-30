# Resource profile persistence

Runtime resource/PWM configuration can be saved to LittleFS:

```
/sys/res_profile.bin
```

## Shell

```text
mcu res save
mcu res load
mcu res clear
mcu res profile show
mcu pwm persist pwm0 1
```

## Firmware identity

Each profile stores `(fw_crc32, fw_image_size)` from the linked firmware binary.
On boot, a mismatch deletes the stale file (re-flash clears effective config).

Build embeds identity via two-pass link in `./tools/flash.sh`.

## Host tool

```bash
python3 tools/res-profile.py identity --elf build/Release/tars.elf
python3 tools/res-profile.py pack config.json -o profile.bin --elf build/Release/tars.elf
python3 tools/res-profile.py dump profile.bin
python3 tools/res-profile.py unpack profile.bin -o config.json
```

JSON example:

```json
{
  "board": "stm32f429i-disc1",
  "grants": [
    {"id": "pwm0", "owner": 2},
    {"id": "pa8", "owner": 2}
  ],
  "pwm": [
    {"channel": "pwm0", "duty": 50, "boot_enable": 1}
  ],
  "tim": [
    {"tim_id": "tim9", "freq_hz": 1000}
  ]
}
```

Owner codes: `0=none`, `1=gpio`, `2=pwm`, `3=foc`, `4=system`.

## Lua (API v2)

```lua
tars.res_save()
tars.res_load()
tars.res_clear()
tars.pwm_persist("pwm0", 1)
```
