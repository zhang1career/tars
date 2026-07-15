# Board config manifest

Build-time merge of pin map, FOC params, timer rules, and probe metrics into a single JSON file for humans, scripts, and AI assistants.

## Output

`generated/config/board.json` (gitignored; produced by CMake or manual run)

## Generate

```bash
python3 tools/config/config-gen.py tools/pinmap/stm32f429i-disc1.csv \
  -o generated/config/board.json
```

CMake runs this automatically after pinmap / FOC / probe inputs change.

## Query

```bash
python3 tools/config/config-query.py pwm0.freq          # -> 20000
python3 tools/config/config-query.py pwm0              # JSON object
python3 tools/config/config-query.py --list pwm
python3 tools/config/config-query.py --json            # full manifest
python3 tools/config/config-query.py foc.FOC_PARAM_FPWM_HZ
```

## On-device

USB shell mirrors design-time values plus live hardware state:

```text
mcu spec list
mcu spec pwm0
mcu spec tim1
```

## Sources merged

| Input | Keys in board.json |
|-------|-------------------|
| `tools/pinmap/<board>.csv` | `board`, `pwm`, `gpio`, `periph` |
| `generated/foc/foc_params.h` | `foc`, TIM1 `freq_hz` |
| `Core/Src/tim.c` | TIM9 `default_freq_hz` |
| `tools/probe/metrics.csv` | `probe_metrics` |
| Timer rules in `config-gen.py` | `timers`, PWM `shell_freq_mutable` |
