#!/usr/bin/env python3
"""Generate board pin map C source from tools/pinmap/<board>.csv."""

from __future__ import annotations

import argparse
import csv
import sys
from dataclasses import dataclass, field
from io import StringIO
from pathlib import Path

OWNER_MAP = {
    "none": "TARS_OWNER_NONE",
    "gpio": "TARS_OWNER_GPIO",
    "pwm": "TARS_OWNER_PWM",
    "foc": "TARS_OWNER_FOC",
    "system": "TARS_OWNER_SYSTEM",
}

TIM_INSTANCE = {
    "tim1": "TIM1",
    "tim2": "TIM2",
    "tim3": "TIM3",
    "tim4": "TIM4",
    "tim5": "TIM5",
    "tim8": "TIM8",
    "tim9": "TIM9",
    "tim10": "TIM10",
    "tim11": "TIM11",
    "tim12": "TIM12",
    "tim13": "TIM13",
    "tim14": "TIM14",
}


@dataclass
class GpioRow:
    pin: str
    alias: str = ""
    default_owner: str = "gpio"


@dataclass
class PwmRow:
    channel: str
    tim: str
    chan: int
    pin: str
    af: str
    default_owner: str = "none"
    alias: str = ""


@dataclass
class PinMap:
    board: str = ""
    mcu: str = ""
    package: str = ""
    periph: list[tuple[str, str]] = field(default_factory=list)
    gpio: list[GpioRow] = field(default_factory=list)
    pwm: list[PwmRow] = field(default_factory=list)


def parse_pin_name(pin_name: str) -> tuple[str, int]:
    name = pin_name.strip().lower()
    if name.startswith("p"):
        name = name[1:]
    if len(name) < 2 or name[0] not in "abcdefghi":
        raise ValueError(f"invalid pin name: {pin_name!r}")
    num = int(name[1:])
    if num < 0 or num > 15:
        raise ValueError(f"pin number out of range: {pin_name!r}")
    return name[0], num


def pin_to_hal(pin_name: str) -> tuple[str, str]:
    bank, num = parse_pin_name(pin_name)
    return f"GPIO{bank.upper()}", f"GPIO_PIN_{num}"


def parse_owner(value: str) -> str:
    key = value.strip().lower() or "none"
    if key not in OWNER_MAP:
        raise ValueError(f"unknown owner {value!r}; use none|gpio|pwm|foc|system")
    return OWNER_MAP[key]


def af_const(tim: str, af: str) -> str:
    tim_key = tim.strip().lower()
    if tim_key not in TIM_INSTANCE:
        raise ValueError(f"unknown timer {tim!r}")
    af_num = af.strip().upper().replace("AF", "")
    return f"GPIO_AF{af_num}_{TIM_INSTANCE[tim_key]}"


def tim_const(tim: str) -> str:
    tim_key = tim.strip().lower()
    if tim_key not in TIM_INSTANCE:
        raise ValueError(f"unknown timer {tim!r}")
    return TIM_INSTANCE[tim_key]


def hal_channel(chan: int) -> str:
    if chan < 1 or chan > 4:
        raise ValueError(f"pwm channel number must be 1..4, got {chan}")
    return f"TIM_CHANNEL_{chan}"


def load_csv(path: Path) -> PinMap:
    text = path.read_text(encoding="utf-8")
    pm = PinMap()
    section = "meta"

    for raw in text.splitlines():
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
            if len(row) < 2:
                raise ValueError(f"periph row needs signal,pin: {line!r}")
            pm.periph.append((row[0].strip(), row[1].strip()))
        elif section == "gpio":
            if row[0].lower() in {"pin", "name"}:
                continue
            pin = row[0].strip()
            alias = row[1].strip() if len(row) > 1 else ""
            owner = row[2].strip() if len(row) > 2 else "gpio"
            pm.gpio.append(GpioRow(pin=pin, alias=alias, default_owner=owner))
        elif section == "pwm":
            if row[0].lower() in {"channel", "name"}:
                continue
            if len(row) < 5:
                raise ValueError(f"pwm row needs channel,tim,chan,pin,af[,owner][,alias]: {line!r}")
            pm.pwm.append(
                PwmRow(
                    channel=row[0].strip(),
                    tim=row[1].strip().lower(),
                    chan=int(row[2].strip()),
                    pin=row[3].strip(),
                    af=row[4].strip(),
                    default_owner=row[5].strip() if len(row) > 5 else "none",
                    alias=row[6].strip() if len(row) > 6 else "",
                )
            )

    if not pm.board:
        pm.board = path.stem
    if not pm.mcu:
        raise ValueError(f"missing @mcu in {path}")
    if not pm.package:
        raise ValueError(f"missing @package in {path}")
    if not pm.gpio and not pm.pwm:
        raise ValueError(f"no [gpio] or [pwm] entries in {path}")

    return pm


def c_string(value: str | None) -> str:
    if value is None or value == "":
        return "NULL"
    return f"\"{value}\""


def render_c(pm: PinMap, source: Path) -> str:
    gpio_rows: list[str] = []
    for row in pm.gpio:
        port, hal_pin = pin_to_hal(row.pin)
        owner = parse_owner(row.default_owner)
        gpio_rows.append(
            f"  {{ {c_string(row.pin)}, {c_string(row.alias or None)}, "
            f"{port}, {hal_pin}, {owner} }},"
        )

    pwm_rows: list[str] = []
    for row in pm.pwm:
        port, hal_pin = pin_to_hal(row.pin)
        owner = parse_owner(row.default_owner)
        tim_inst = tim_const(row.tim)
        advanced = "1U" if row.tim.lower() == "tim1" else "0U"
        pwm_rows.append(
            f"  {{ {c_string(row.channel)}, {c_string(row.alias or None)}, "
            f"{tim_inst}, {c_string(row.tim.lower())}, {hal_channel(row.chan)}, "
            f"{port}, {hal_pin}, {af_const(row.tim, row.af)}, "
            f"{owner}, {advanced}, {c_string(row.pin)} }},"
        )

    catalog_rows: list[str] = []
    order = 0
    for i, row in enumerate(pm.gpio):
        catalog_rows.append(
            f"  {{ {c_string(row.pin)}, TARS_RES_KIND_GPIO, {order}U, "
            f"{parse_owner(row.default_owner)}, -1, {i} }},"
        )
        order += 1

    for i, row in enumerate(pm.pwm):
        catalog_rows.append(
            f"  {{ {c_string(row.channel)}, TARS_RES_KIND_PWM, {order}U, "
            f"{parse_owner(row.default_owner)}, -1, {i} }},"
        )
        order += 1

    periph_rows = [
        f"  {{ {c_string(signal)}, {c_string(pin)} }},"
        for signal, pin in pm.periph
    ]
    periph_body = "\n".join(periph_rows)

    gpio_body = "\n".join(gpio_rows) if gpio_rows else ""
    pwm_body = "\n".join(pwm_rows) if pwm_rows else ""
    catalog_body = "\n".join(catalog_rows) if catalog_rows else ""

    return f"""/* AUTO-GENERATED by tools/pinmap/pinmap-gen.py — do not edit */
/* Source: {source.as_posix()} */

#include "tars_mcu_pinmap.h"
#include "main.h"

static const tars_mcu_gpio_entry_t s_gpio[] = {{
{gpio_body}
}};

static const tars_mcu_pwm_entry_t s_pwm[] = {{
{pwm_body}
}};

static const tars_res_catalog_entry_t s_res_catalog[] = {{
{catalog_body}
}};

static const tars_mcu_periph_entry_t s_periph[] = {{
{periph_body}
}};

const char *TarsMcuPinmap_BoardId(void)
{{
  return "{pm.board}";
}}

const char *TarsMcuPinmap_McuId(void)
{{
  return "{pm.mcu}";
}}

const char *TarsMcuPinmap_PackageId(void)
{{
  return "{pm.package}";
}}

const tars_mcu_gpio_entry_t *TarsMcuPinmap_GetGpioTable(uint32_t *count_out)
{{
  if (count_out != NULL)
  {{
    *count_out = (uint32_t)(sizeof(s_gpio) / sizeof(s_gpio[0]));
  }}

  return s_gpio;
}}

const tars_mcu_pwm_entry_t *TarsMcuPinmap_GetPwmTable(uint32_t *count_out)
{{
  if (count_out != NULL)
  {{
    *count_out = (uint32_t)(sizeof(s_pwm) / sizeof(s_pwm[0]));
  }}

  return s_pwm;
}}

const tars_res_catalog_entry_t *TarsMcuPinmap_GetResCatalog(uint32_t *count_out)
{{
  if (count_out != NULL)
  {{
    *count_out = (uint32_t)(sizeof(s_res_catalog) / sizeof(s_res_catalog[0]));
  }}

  return s_res_catalog;
}}

const tars_mcu_periph_entry_t *TarsMcuPinmap_GetPeriphTable(uint32_t *count_out)
{{
  if (count_out != NULL)
  {{
    *count_out = (uint32_t)(sizeof(s_periph) / sizeof(s_periph[0]));
  }}

  return s_periph;
}}
"""


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv", type=Path, help="Board pin map CSV")
    parser.add_argument("-o", "--output", type=Path, required=True, help="Output .c path")
    args = parser.parse_args()

    pm = load_csv(args.csv)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(render_c(pm, args.csv), encoding="utf-8")
    print(
        f"generated {args.output} ({len(pm.gpio)} gpio, {len(pm.pwm)} pwm, "
        f"{len(pm.gpio) + len(pm.pwm)} res)"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
