# App scheduling

## Terms

| Field | Meaning |
|-------|---------|
| **sub=0** | Installed but **not** scheduled (`app revoke` or never submitted) |
| **sub=1** | **Submitted** — eligible to run on its timeslice |
| **timeslice (ts)** | Slot 1…8; scheduler rotates every **100 ms** |
| **priority (pri)** | 0–255, higher wins when multiple apps share the same timeslice |

## Behaviour

- Only **one** submitted app runs per timeslice tick (highest `pri`, round-robin on tie).
- On timeslice change, previous slice releases resources (`TarsLua_OnRelease` / native release).
- Active app is announced on CDC + LCD header: `running: <name> (ts=N pri=M)`.

## Header defaults (pack)

```bash
python3 tools/tars-pack.py lua app.lua -o app.tlua --name app \
  --timeslice 1 --priority 10
```

## Shell

```
app list          # shows ts, sub, pri
app submit hello
app revoke hello
sched status      # current ts, running app, slice period
```

## Lua execution model

Scripts run in a **coroutine**. Each scheduled tick calls `lua_resume` once.

- `tars.yield()` — pause until the next timeslice for this app
- Script ends — coroutine cleared; next tick starts fresh
- `app revoke` / `app uninstall` — resets coroutine state

`app run <name>` (one-shot) loops resume until the script finishes or errors.

`tars.sleep(ms)` still blocks the Lua task (use `yield` for cooperative scheduling).
