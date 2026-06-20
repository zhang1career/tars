#!/usr/bin/env python3
"""Send a .tapp or .tlua blob to TARS via CDC virtual serial."""

from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

try:
    import serial
except ImportError:
    print("pip install pyserial", file=sys.stderr)
    raise


SHELL_MARKERS = ("tars>", "shell ready")
INSTALL_MARKERS = ("install: ready", "install: bad")


INSTALL_OK_MARKERS = ("install: 0", "install: 0 (ok)")


def drain_serial(ser: serial.Serial, settle: float = 0.3) -> str:
    buf = ""
    deadline = time.time() + settle

    while time.time() < deadline:
        waiting = ser.in_waiting
        if waiting:
            buf += ser.read(waiting).decode("ascii", errors="replace")
            deadline = time.time() + 0.15
        time.sleep(0.02)

    return buf


def read_until(ser: serial.Serial, needles: tuple[str, ...], timeout: float) -> tuple[bool, str]:
    buf = ""
    deadline = time.time() + timeout

    while time.time() < deadline:
        waiting = ser.in_waiting
        if waiting:
            buf += ser.read(waiting).decode("ascii", errors="replace")
            for needle in needles:
                if needle in buf:
                    return True, buf

        time.sleep(0.02)

    return False, buf


def wait_for_shell(ser: serial.Serial, timeout: float) -> tuple[bool, str]:
    buf = drain_serial(ser, 0.4)
    lower = buf.lower()

    for marker in SHELL_MARKERS:
        if marker in lower:
            return True, buf

    ser.write(b"\r\n")
    ser.flush()
    ok, buf = read_until(ser, SHELL_MARKERS, timeout)
    return ok, buf


def main() -> None:
    parser = argparse.ArgumentParser(description="Install TARS app over CDC")
    parser.add_argument("blob", type=Path, help=".tapp or .tlua file")
    parser.add_argument("-p", "--port", required=True, help="Serial port e.g. /dev/tty.usbmodem*")
    parser.add_argument("-s", "--slot", type=int, default=-1, help="Optional native slot hint")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--wait", type=float, default=12.0, help="Seconds to wait for shell prompt")
    args = parser.parse_args()

    data = args.blob.read_bytes()
    size = len(data)

    with serial.Serial(args.port, args.baud, timeout=0.2) as ser:
        ser.dtr = False
        ser.rts = False
        time.sleep(0.05)
        ser.dtr = True
        time.sleep(0.5)

        ok, banner = wait_for_shell(ser, args.wait)
        if not ok:
            print(
                "warning: shell prompt not seen; attempting install anyway",
                file=sys.stderr,
            )

        drain_serial(ser, 0.5)

        cmd = f"app install begin {size}"
        if args.slot >= 0:
            cmd += f" {args.slot}"
        ser.write((cmd + "\r\n").encode("ascii"))
        ser.flush()

        ready, resp = read_until(ser, INSTALL_MARKERS, 10.0)
        if not ready:
            print("device did not enter install mode", file=sys.stderr)
            if resp.strip():
                print(f"response: {resp!r}", file=sys.stderr)
            elif banner.strip():
                print(f"banner: {banner!r}", file=sys.stderr)
            sys.exit(1)

        if "install: bad" in resp:
            print(resp.strip())
            sys.exit(1)

        ser.write(data)
        ser.flush()

        ok, done = read_until(ser, ("install:",), 30.0)
        if done.strip():
            for line in done.splitlines():
                if line.strip():
                    print(line.strip())

        install_line = ""
        for line in done.splitlines():
            if line.strip().startswith("install:"):
                install_line = line.strip()
                break

        if any(marker in install_line for marker in INSTALL_OK_MARKERS):
            ser.write(b"app catalog\r\n")
            ser.flush()
            _, catalog_resp = read_until(ser, ("catalog:",), 5.0)
            for line in catalog_resp.splitlines():
                if line.strip().startswith("catalog:"):
                    print(line.strip())

            ser.write(b"app list\r\n")
            ser.flush()
            _, list_resp = read_until(ser, ("tars>",), 5.0)
            for line in list_resp.splitlines():
                stripped = line.strip()
                if stripped and stripped != "tars>" and not stripped.startswith("catalog:"):
                    print(stripped)
            return

        if "install: incomplete" in done:
            print("install incomplete (size mismatch)", file=sys.stderr)
            sys.exit(1)

        if install_line:
            print(f"install failed: {install_line}", file=sys.stderr)
            sys.exit(1)

        if not ok:
            print("install timed out waiting for result", file=sys.stderr)
            sys.exit(1)

        print("install result not confirmed", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
