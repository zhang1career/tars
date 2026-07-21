# MCU shell and pin mapping

TARS exposes on-chip hardware through the **`mcu`** shell command. Peripheral
names are MCU-generic; physical pins come from the board pin map CSV.

## Resource tenants

Resources (GPIO pins, PWM channels, DAC channels) are listed in the pin map.
**No boot-time tenant assignment** — all resources start unassigned. Runtime
assignment lives in RAM; persist with `mcu res save` / restore on next boot.

Assign a **tenant** label (alphanumeric + underscore, 1–15 chars) when idle
(not **active**):

```
mcu res list
mcu res grant pwm0 gate_driver
mcu res grant pwm7 bst_refresh
mcu res grant dac0 analog_out
mcu res grant pg13 board_leds
mcu res grant pwm0 foc            # motor control (before motor enable)
mcu res grant pwm0 none          # clear assignment
mcu res status pwm0
mcu res save                     # persist grants + PWM boot state
```

Reserved names: `none` (unassigned), `system` (internal; cannot be granted).
`foc` is a normal tenant label for motor PWM — grant explicitly or via saved profile.

Conflict checks when using a resource:

1. Request is within the pin map catalog (**scope**)
2. Target is not **active** (held by another function)
3. A tenant is assigned (`mcu res grant` first; `none` blocks use)

## GPIO

```
mcu gpio write pg13 0      # LD3 on (active low)
mcu gpio read pg13
mcu gpio list
```

Grant a tenant before write (e.g. `mcu res grant pg13 board_leds`).

## PWM

Grant tenant, set duty/frequency, then enable:

```
mcu pwm list
mcu res grant pwm0 gate_driver
mcu pwm duty pwm0 50
mcu pwm freq tim9 1000
mcu pwm enable pwm0 1
mcu pwm status pwm0
mcu pwm enable pwm0 0
```

PWM IDs are **`pwmN`** in pin map order: `(tim, chan, pin)` ascending.
Use `mcu spec pwmN` or `tools/pinmap/<board>.csv` to look up timer/channel/pin.

TIM1 channels (**pwm0**–**pwm2**) use the motor timer; grant a tenant before
shell PWM. FOC takes **tim1** at runtime when the motor bridge is active.

Shared timers: all channels on the same TIM share one frequency (`mcu pwm freq tim9 …`).

**pwm7** (TIM10 / PB8, Morpho CN12) and **pwm3** (TIM3 / PB4) default to **20 kHz / 50%**:

```
mcu res grant pwm7 bst_refresh
mcu pwm enable pwm7 1
mcu pwm status pwm7
mcu spec pwm7

mcu res grant pwm3 pwm
mcu pwm enable pwm3 1
mcu spec pwm3
```

## DAC

STM32F429 dual DAC: **dac0 → PA4**, **dac1 → PA5** (VDDA reference, ~3.3 V).

```
mcu dac list
mcu res grant dac0 analog_out
mcu dac value dac0 50
mcu dac enable dac0 1
mcu dac status dac0
```

## AWG (arbitrary waveform generator)

DAC-based generator: a sample table is pre-computed (or uploaded) into external
SDRAM and streamed to the DAC by **TIM7-triggered circular DMA**, so output runs
with zero CPU load. Output pins are the DAC pins (**dac0 → PA4**, **dac1 → PA5**).

**Frequency:** per channel `out = sample_rate / points`. With both channels
running, `sample_rate` is the **maximum** of `freq × points` across active
channels (shared TIM7). Use the same point count on both channels when you need
matched fundamentals for X-Y plots. `sample_rate` is capped at **1 MHz**.
Points range **2–8192**. Both **dac0** and **dac1** may run at once.

### Built-in waveforms

| Wave | Name(s) | Parameters |
|------|---------|------------|
| Sine | `sin`, `sine` | amplitude, offset |
| Square | `square` | amplitude, offset, **duty** |
| Triangle | `tri`, `triangle` | amplitude, offset |
| Sawtooth | `saw`, `sawtooth` | amplitude, offset |
| DC | `dc` | offset |
| Noise | `noise` | amplitude, offset |

`gen <ch> <wave> <points> [ampl%] [off%] [duty%]` — **amplitude** is peak-to-peak
as a percentage of full scale, **offset** is the DC midpoint (0–100%), **duty**
applies to `square` only. Defaults: `ampl=100 off=50 duty=50`.

### Generate + play

```
mcu res grant dac0 awg
mcu awg gen dac0 sin 256 100 50 50   # sine, 256 pts, 100% p-p, 50% offset
mcu awg enable dac0 1
mcu awg freq dac0 2000               # live frequency change
mcu awg status dac0
mcu awg enable dac0 0
```

### Dual-channel (2D analog output)

Grant and configure each DAC independently, then enable both:

```
mcu res grant dac0 awg
mcu res grant dac1 awg
mcu awg gen dac0 sin 256 100 50 50
mcu awg gen dac1 tri 256 100 50 50
mcu awg enable dac0 1
mcu awg enable dac1 1
mcu awg status dac0
mcu awg status dac1
```

`status` shows requested `freq`, achieved `out` (sample/points), and shared
`sample` clock. Stop either channel without affecting the other:
`mcu awg enable dac1 0`.

**Dual-channel timing note:** both channels share one TIM7 sample clock. The
firmware sets `sample_rate = max(freq × points)` over all running channels, so
only the channel with the highest product hits its requested `freq` exactly.
Other channels compute `out = sample_rate / points`, which can be **higher** than
the `freq` you passed to `gen` or `freq` when point counts differ.

Example: dac0 at 1 kHz × 256 pts and dac1 at 1 kHz × 128 pts both running —
`sample_rate` is driven by dac0 (256 kHz). dac0 `out` ≈ 1 kHz; dac1 `out`
≈ 2 kHz even though you asked for 1 kHz on both.

For **X-Y / Lissajous** use on a scope, set the **same `points` and `freq`** on
dac0 and dac1 so `(x[i], y[i])` pairs stay aligned and both axes share the same
fundamental. Check `out` in `mcu awg status` when tuning.

### Upload an arbitrary waveform

The host streams raw **little-endian uint16** DAC codes (0–4095) into the table:

```
mcu awg upload dac0 512              # device replies "upload: ready", then expects 512*2 bytes
```

Use the helper instead of raw bytes — it generates codes, uploads, and can grant
+ set frequency + enable in one shot:

```bash
# built-in function
./tools/awg-upload.py dac0 --func sin --points 512 --freq 1000 --grant awg --enable

# arbitrary math expression (vars: t in [0,1), i, n, plus math.*)
./tools/awg-upload.py dac0 --expr "sin(2*pi*t)+0.3*sin(6*pi*t)" --points 1024

# raw samples from a file (already DAC codes 0-4095)
./tools/awg-upload.py dac0 --file scope_trace.csv --codes
```

Values are normalized `[-1, 1]` and mapped with `--ampl`/`--offset` unless
`--codes` is given. Upload requires the channel to be stopped; no tenant grant
is needed to upload (grant + `enable` still gate the hardware).

## Pin map (CSV)

| Path | Role |
|------|------|
| `tools/pinmap/<board>.csv` | Source of truth |
| `generated/pinmap/<board>.c` | Auto-generated (gitignored) |

Sections: `[periph]`, `[gpio]`, `[pwm]`, `[dac]`. Tenants are runtime-only
(`mcu res grant`); persist with `mcu res save`.

## Lua API

```lua
tars.gpio_write("pg13", 0)
tars.pwm_duty("pwm0", 50.0)
tars.pwm_enable("pwm0", 1)

-- Node Bus (I²C slaves; see tars-io-mux docs/tars-node-bus.md §0 / §3.1)
n = tars.nodebus_scan()          -- enumerate; returns node count (or <0)
c = tars.nodebus_count()
node = tars.nodebus_get(0)       -- {addr, product_id, vendor_id, profile, …} or nil
st = tars.nodebus_mux(0x10, 3)   -- select channel; 0 = OK
```

Grant a tenant from the shell before Lua uses a resource (`mcu res grant`).

Infrequent board utilities can live as Lua under `tools/examples/` (source),
packed to `.tlua` and installed to LittleFS `/apps` when needed — e.g.
`mux_pin_walk.lua` walks io-mux channels for pin bring-up.
