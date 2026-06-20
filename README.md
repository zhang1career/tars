# TARS — STM32F429 app platform

Discovery board (`STM32F429I-DISC1`): USB CDC shell, LittleFS, **Lua 5.1** apps.

**MVP:** Lua `.tlua` only. Native `.tapp` (Phase 1 fixed-slot XIP) is reserved for post-MVP; Phase 2 reloc is not planned.

## Quick start

```bash
cmake --preset Debug && cmake --build cmake-build-debug
./tools/flash.sh

python3 tools/tars-pack.py lua tools/examples/hello.lua \
  -o installs/hello.tlua --name hello
python3 tools/tars-send.py installs/hello.tlua -p /dev/tty.usbmodemXXXX

# serial shell (DTR on):
mcu gpio write pg13 0   # LD3 on
app submit hello
sched status
sys top
```

## Docs

- [MCU shell & pin map](docs/mcu-shell.md)
- [App formats](docs/app-format.md)
- [Flash layout](docs/flash-layout.md)
- [Scheduling](docs/scheduling.md)
- [OTA A/B](docs/ota.md) (stub, not MVP)

## Flash

| Command | Effect |
|---------|--------|
| `./tools/flash.sh` | Firmware sectors 0–6 only; **keeps LittleFS /apps** |
| `./tools/flash.sh --full` | May wipe app partition |
