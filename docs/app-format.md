# TARS App Format

## MVP scope: **Lua only**

| Kind | Status |
|------|--------|
| **Lua `.tlua`** | Active — install, schedule, shell |
| **Native `.tapp`** | **Disabled** (`TARS_MVP_LUA_ONLY`) — flash slots reserved |
| **Built-in C** | Optional — linked in firmware, not `.tapp` |

LittleFS: `/sys/catalog`, `/apps/<name>.tlua`.

## Unified API (`tars_api_t`)

Same capability table for future native C and current Lua bindings — see `App/tars_api.c` / `tars.gpio_*`.

## Native `.tapp` (post-MVP, **Phase 1 only**)

**Phase 2 (reloc / SDRAM) is not planned.** When native returns:

- C app compiled for a **fixed flash slot** (`tools/tars-app-fixed.ld`)
- Pack: `tars-pack.py native … --slot N` → link base `0x080A0000 + N×128K`
- Install writes blob to that slot; run = XIP + init `.data`/`.bss` (no reloc table)
- Entry: `void app_entry(const tars_api_t *api)`

Built-in C modules that ship **with the firmware** use `TarsApp_RegisterBuiltin()` — not `.tapp`.

## Lua `.tlua`

```bash
python3 tools/tars-pack.py lua apps/hello.lua -o hello.tlua \
  --name hello --timeslice 1 --priority 10
```

Cooperative scheduling: `tars.yield()` between timeslices.

## Shell (MVP)

```
app list | submit | revoke | uninstall | run
gpio write 13 0          # LD3 on (dev board)
fs ls /apps
sys top
```

Serial install: `python3 tools/tars-send.py hello.tlua -p /dev/tty.usbmodemXXXX`

See [scheduling.md](scheduling.md), [flash-layout.md](flash-layout.md).
