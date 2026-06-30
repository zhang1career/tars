# MCU shell and pin mapping

TARS exposes on-chip hardware through the **`mcu`** shell command. Peripheral
names are MCU-generic; physical pins come from the board pin map CSV.

## Resource ownership

Resources (GPIO pins, PWM channels) are listed in the pin map. At boot, each
entry gets a **default owner** from CSV (`none|gpio|pwm|foc|system`). Runtime
ownership lives in **RAM only** — it resets on power cycle / reset.

Change ownership when idle (not **active**):

```
mcu res list
mcu res grant pwm0 pwm
mcu res status pwm0
```

Conflict checks when using a resource:

1. Request is within the pin map catalog (**scope**)
2. Target is not **active** (held by another function)
3. Caller matches **owner** (grant via `mcu res grant` first)

## GPIO

```
mcu gpio write pg13 0      # LD3 on (active low)
mcu gpio read pg13
mcu gpio list
```

## PWM

Grant ownership, set duty/frequency, then enable:

```
mcu pwm list
mcu res grant pwm0 pwm
mcu pwm duty pwm0 50
mcu pwm freq tim9 1000
mcu pwm enable pwm0 1
mcu pwm status pwm0
mcu pwm enable pwm0 0
```

TIM1 channels (`tim1_ch1` …) default to **foc**. Shell PWM on TIM1 is allowed
after granting ownership away from FOC and while the motor bridge is off.

Shared timers: all channels on the same TIM share one frequency (`mcu pwm freq tim9 …`).

## Pin map (CSV)

| Path | Role |
|------|------|
| `tools/pinmap/<board>.csv` | Source of truth |
| `generated/pinmap/<board>.c` | Auto-generated (gitignored) |

Sections: `[periph]`, `[gpio]`, `[pwm]`.

## Lua API

```lua
tars.gpio_write("pg13", 0)
tars.pwm_duty("pwm0", 50.0)
tars.pwm_enable("pwm0", 1)
```

Grant ownership from the shell before Lua uses PWM on a channel whose default
owner is not `pwm`.
