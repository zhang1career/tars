#!/usr/bin/env python3
"""Merge pinmap, FOC params, probe metrics, and timer rules into board.json."""

from __future__ import annotations

import argparse
import csv
import json
import re
import sys
from dataclasses import dataclass, field
from io import StringIO
from pathlib import Path


@dataclass
class GpioRow:
    pin: str
    alias: str = ""
    default_tenant: str = "none"


@dataclass
class PwmRow:
    channel: str
    tim: str
    chan: int
    pin: str
    af: str
    default_tenant: str = "none"
    alias: str = ""


@dataclass
class PinMap:
    board: str = ""
    mcu: str = ""
    package: str = ""
    periph: list[tuple[str, str]] = field(default_factory=list)
    gpio: list[GpioRow] = field(default_factory=list)
    pwm: list[PwmRow] = field(default_factory=list)


def load_pinmap_csv(path: Path) -> PinMap:
    pm = PinMap()
    section = "meta"

    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue

        if line.startswith("@"):
            key, _, value = line.partition(",")
            key = key[1:].strip().lower()
            value = value.strip()
            if key == "board":
                pm.board = value
            elif key == "mcu":
                pm.mcu = value
            elif key == "package":
                pm.package = value
            continue

        if line.startswith("[") and line.endswith("]"):
            section = line[1:-1].strip().lower()
            continue

        row = next(csv.reader(StringIO(line)))
        if not row:
            continue

        if section == "periph":
            if row[0].lower() in {"signal", "name"}:
                continue
            if len(row) >= 2:
                pm.periph.append((row[0].strip(), row[1].strip()))
        elif section == "gpio":
            if row[0].lower() in {"pin", "name"}:
                continue
            pm.gpio.append(
                GpioRow(
                    pin=row[0].strip(),
                    alias=row[1].strip() if len(row) > 1 else "",
                    default_tenant=row[2].strip() if len(row) > 2 else "none",
                )
            )
        elif section == "pwm":
            if row[0].lower() in {"channel", "name"}:
                continue
            if len(row) >= 6:
                pm.pwm.append(
                    PwmRow(
                        channel=row[0].strip(),
                        tim=row[1].strip().lower(),
                        chan=int(row[2].strip()),
                        pin=row[3].strip().lower(),
                        af=row[4].strip(),
                        default_tenant=row[5].strip() or "none",
                        alias=row[6].strip() if len(row) > 6 else "",
                    )
                )

    return pm


def parse_foc_params(path: Path) -> dict[str, int | float | str]:
    foc: dict[str, int | float | str] = {}
    if not path.is_file():
        return foc

    for line in path.read_text(encoding="utf-8").splitlines():
        match = re.match(r"#define\s+(FOC_PARAM_\w+)\s+(.+)", line)
        if not match:
            continue
        raw = match.group(2).strip()
        if raw.endswith("f"):
            raw = raw[:-1]
            try:
                foc[match.group(1)] = float(raw)
            except ValueError:
                foc[match.group(1)] = raw
        else:
            raw = raw.rstrip("U")
            try:
                foc[match.group(1)] = int(raw, 0)
            except ValueError:
                foc[match.group(1)] = raw
    return foc


def parse_tim9_pwm_hz(tim_c: Path) -> int:
    if not tim_c.is_file():
        return 1000
    match = re.search(r"#define\s+TARS_TIM9_PWM_HZ\s+(\d+)U", tim_c.read_text(encoding="utf-8"))
    return int(match.group(1)) if match else 1000


def parse_probe_metrics(path: Path) -> list[dict[str, str]]:
    metrics: list[dict[str, str]] = []
    if not path.is_file():
        return metrics

    section = "meta"
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("@"):
            continue
        if line.startswith("[") and line.endswith("]"):
            section = line[1:-1].strip().lower()
            continue

        row = next(csv.reader(StringIO(line)))
        if not row:
            continue
        if section == "metrics":
            if row[0].lower() in {"name", "metric"}:
                continue
            if len(row) >= 4:
                metrics.append(
                    {
                        "name": row[0].strip(),
                        "unit": row[1].strip(),
                        "provider": row[2].strip(),
                        "arg": row[3].strip(),
                    }
                )
    return metrics


def timer_section(foc: dict[str, int | float | str], tim9_hz: int) -> dict:
    foc_fpwm = int(foc.get("FOC_PARAM_FPWM_HZ", 20000))
    tim1_clk = 72000000
    tim1_arr = tim1_clk // (2 * foc_fpwm)
    tim9_arr = (72000000 // tim9_hz) - 1

    return {
        "tim1": {
            "clk_hz": tim1_clk,
            "pwm_mode": "center_aligned",
            "freq_hz": foc_fpwm,
            "freq_source": "FOC_PARAM_FPWM_HZ",
            "arr": tim1_arr,
            "shell_freq_mutable": False,
            "notes": "FOC-owned; shell PWM adjusts compare only",
        },
        "tim9": {
            "clk_hz": 72000000,
            "pwm_mode": "edge_aligned",
            "default_freq_hz": tim9_hz,
            "freq_source": "TARS_TIM9_PWM_HZ",
            "arr": tim9_arr,
            "shell_freq_mutable": True,
        },
    }


def build_board_json(
    pinmap: PinMap,
    foc: dict[str, int | float | str],
    metrics: list[dict[str, str]],
    timers: dict,
) -> dict:
    pwm_out: dict[str, dict] = {}
    for row in pinmap.pwm:
        tim_meta = timers.get(row.tim, {})
        freq_hz = tim_meta.get("freq_hz", tim_meta.get("default_freq_hz", 1000))
        entry = {
            "tim": row.tim,
            "chan": row.chan,
            "pin": row.pin,
            "af": row.af,
            "default_tenant": row.default_tenant,
            "freq_hz": freq_hz,
            "freq_source": tim_meta.get("freq_source", ""),
            "shell_freq_mutable": tim_meta.get("shell_freq_mutable", True),
            "pwm_mode": tim_meta.get("pwm_mode", "edge_aligned"),
        }
        if row.alias:
            entry["alias"] = row.alias
        pwm_out[row.channel] = entry

    gpio_out = {
        row.pin: {
            "alias": row.alias,
            "default_tenant": row.default_tenant,
        }
        for row in pinmap.gpio
    }

    periph_out = {sig: pin for sig, pin in pinmap.periph}

    return {
        "schema_version": 1,
        "board": {
            "id": pinmap.board,
            "mcu": pinmap.mcu,
            "package": pinmap.package,
        },
        "timers": timers,
        "pwm": pwm_out,
        "gpio": gpio_out,
        "periph": periph_out,
        "foc": foc,
        "probe_metrics": metrics,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pinmap_csv", type=Path, help="tools/pinmap/<board>.csv")
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        required=True,
        help="Output path (e.g. generated/config/board.json)",
    )
    parser.add_argument(
        "--foc-params",
        type=Path,
        default=None,
        help="generated/foc/foc_params.h",
    )
    parser.add_argument(
        "--probe-csv",
        type=Path,
        default=None,
        help="tools/probe/metrics.csv",
    )
    parser.add_argument(
        "--tim-c",
        type=Path,
        default=None,
        help="Core/Src/tim.c (TARS_TIM9_PWM_HZ)",
    )
    args = parser.parse_args()

    repo = args.pinmap_csv.resolve().parents[2]
    foc_path = args.foc_params or (repo / "generated/foc/foc_params.h")
    probe_path = args.probe_csv or (repo / "tools/probe/metrics.csv")
    tim_c = args.tim_c or (repo / "Core/Src/tim.c")

    pinmap = load_pinmap_csv(args.pinmap_csv)
    foc = parse_foc_params(foc_path)
    metrics = parse_probe_metrics(probe_path)
    tim9_hz = parse_tim9_pwm_hz(tim_c)
    timers = timer_section(foc, tim9_hz)
    doc = build_board_json(pinmap, foc, metrics, timers)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(doc, indent=2, sort_keys=False) + "\n", encoding="utf-8")
    print(f"wrote {args.output}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
