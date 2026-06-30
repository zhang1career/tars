#!/usr/bin/env python3
"""Send one or more commands to the TARS USB shell."""

from __future__ import annotations

import argparse
import glob
import sys
import time

try:
    import serial
except ImportError:
    print("usb-shell: pyserial required (pip install pyserial)", file=sys.stderr)
    sys.exit(1)


def find_port(explicit: str | None) -> str:
    if explicit:
        return explicit
    ports = sorted(glob.glob("/dev/tty.usbmodem*"))
    if not ports:
        raise SystemExit("usb-shell: no /dev/tty.usbmodem* port found")
    return ports[-1]


def main() -> int:
    parser = argparse.ArgumentParser(description="Send commands to TARS USB shell")
    parser.add_argument("commands", nargs="*", help="shell commands (omit to read stdin)")
    parser.add_argument("-p", "--port", help="serial port (default: latest usbmodem)")
    parser.add_argument("-w", "--wait", type=float, default=0.4, help="post-command delay (s)")
    parser.add_argument("-b", "--baud", type=int, default=115200)
    args = parser.parse_args()

    commands = args.commands
    if not commands:
        commands = [line.strip() for line in sys.stdin if line.strip()]

    if not commands:
        parser.error("no commands given")

    port = find_port(args.port)
    with serial.Serial(port, args.baud, timeout=1) as ser:
        time.sleep(0.3)
        ser.reset_input_buffer()
        for cmd in commands:
            ser.write((cmd + "\r\n").encode())
            time.sleep(args.wait)
            chunk = ser.read(8192).decode("utf-8", errors="replace")
            sys.stdout.write(chunk)
            if chunk and not chunk.endswith("\n"):
                sys.stdout.write("\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
