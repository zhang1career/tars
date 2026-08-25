#!/usr/bin/env python3
"""Flash incoming-stm32f429i-disco and dump the SRAM mailbox."""

from __future__ import annotations

import argparse
import struct
import subprocess
import sys
import time
from pathlib import Path

MAGIC = 0x54494E43
REPORT_ADDR = 0x2002F000
REPORT_WORDS = 48
TARS_ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parents[1]
ELF = HERE / "build" / "Release" / "incoming-stm32f429i-disco.elf"
OOCD_CFG = TARS_ROOT / "openocd.cfg"

CHECKS = [
    (0, "MCU ID"),
    (1, "Flash size"),
    (2, "SRAM"),
    (3, "CCM"),
    (4, "VDD"),
    (5, "SDRAM"),
    (6, "Gyro"),
    (7, "Touch"),
    (8, "LCD"),
]

CLK_SRC = {0: "HSI+PLL", 1: "HSE crystal+PLL", 2: "HSE bypass+PLL"}


def run(cmd: list[str], check: bool = True) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, check=check, text=True, capture_output=True)


def flash(elf: Path) -> None:
    cmd = [
        "openocd",
        "-f",
        str(OOCD_CFG),
        "-c",
        f"program {elf} verify reset exit",
    ]
    print("flash:", " ".join(cmd))
    proc = run(cmd, check=False)
    if proc.returncode != 0:
        sys.stderr.write(proc.stdout + proc.stderr)
        raise SystemExit(f"openocd program failed ({proc.returncode})")


def dump_words(addr: int, count: int) -> list[int]:
    # Halt only — a reset here would wipe the mailbox.
    proc = run(
        [
            "openocd",
            "-f",
            str(OOCD_CFG),
            "-c",
            "init",
            "-c",
            "halt",
            "-c",
            f"mdw 0x{addr:08x} {count}",
            "-c",
            "exit",
        ],
        check=False,
    )
    text = proc.stdout + proc.stderr
    words: list[int] = []
    for line in text.splitlines():
        stripped = line.strip()
        if not stripped.lower().startswith("0x"):
            continue
        if ":" not in stripped:
            continue
        payload = stripped.split(":", 1)[1]
        for tok in payload.split():
            try:
                words.append(int(tok, 16))
            except ValueError:
                continue
    if len(words) < 20:
        sys.stderr.write(text)
        raise SystemExit("failed to dump incoming report")
    return words


def decode(words: list[int]) -> None:
    blob = b"".join(struct.pack("<I", w) for w in words[:REPORT_WORDS])
    magic, version, done, pass_m, fail_m, warn_m, skip_m = struct.unpack_from(
        "<7I", blob, 0
    )
    clock_hz, clock_src, idcode, flash_kb = struct.unpack_from("<4I", blob, 28)
    uid = struct.unpack_from("<3I", blob, 44)
    vdd_mv, vref_raw, vref_cal = struct.unpack_from("<3I", blob, 56)
    sdram_fail, sdram_exp, sdram_got, sdram_bytes = struct.unpack_from("<4I", blob, 68)
    gyro, touch_addr, touch_id, lcd_id = struct.unpack_from("<4I", blob, 84)
    i2c = struct.unpack_from("<4I", blob, 100)
    button, fatal = struct.unpack_from("<2I", blob, 116)
    summary = blob[124:156].split(b"\x00", 1)[0].decode("ascii", errors="replace")

    if magic != MAGIC:
        raise SystemExit(f"bad magic 0x{magic:08x} (expected TINC)")

    print(f"summary     {summary}")
    print(f"done        {done}  fatal={fatal}")
    print(f"clock       {clock_hz / 1e6:.2f} MHz  ({CLK_SRC.get(clock_src, clock_src)})")
    print(f"idcode      0x{idcode:08x}  flash={flash_kb} KiB")
    print(f"uid         {uid[0]:08x} {uid[1]:08x} {uid[2]:08x}")
    print(f"vdd         {vdd_mv} mV  (VREFINT raw={vref_raw} cal={vref_cal})")
    print(f"gyro        WHO_AM_I=0x{gyro:02x}")
    print(f"touch       addr=0x{touch_addr:02x} id=0x{touch_id:04x} i2c={list(i2c)}")
    print(f"lcd         id=0x{lcd_id:04x}")
    print(f"button      {button}")
    if sdram_fail:
        print(
            f"sdram fail  @{sdram_fail:08x} expect={sdram_exp:08x} got={sdram_got:08x}"
        )
    else:
        print(f"sdram       {sdram_bytes} bytes covered")

    print()
    rc = 0
    for bit, name in CHECKS:
        mask = 1 << bit
        if fail_m & mask:
            state = "FAIL"
            rc = 2
        elif warn_m & mask:
            state = "WARN"
            rc = max(rc, 1)
        elif skip_m & mask:
            state = "SKIP"
        elif pass_m & mask:
            state = "PASS"
        else:
            state = "?"
        print(f"  [{state}] {name}")
    sys.exit(rc)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--elf", type=Path, default=ELF)
    ap.add_argument("--no-flash", action="store_true")
    ap.add_argument("--wait", type=float, default=3.0, help="seconds after reset")
    args = ap.parse_args()

    if not args.no_flash:
        if not args.elf.is_file():
            raise SystemExit(f"missing ELF: {args.elf} (cmake --preset Release)")
        flash(args.elf)
        time.sleep(args.wait)

    decode(dump_words(REPORT_ADDR, REPORT_WORDS))


if __name__ == "__main__":
    main()
