#!/usr/bin/env python3
"""PC-side client for the TARS probe (SCPI over UART, 115200 8N1).

Examples:
  # Identify the probe
  python3 tools/probe/probe-client.py -p /dev/cu.usbserial-XXXX --idn

  # Read all telemetry metrics once
  python3 tools/probe/probe-client.py -p /dev/cu.usbserial-XXXX --meas

  # Trigger a capture and save the raw uint16 samples to a .bin
  python3 tools/probe/probe-client.py -p /dev/cu.usbserial-XXXX \
      --capture --depth 65536 --rate 1000000 -o cap.bin

NOTE: the probe UART is USART1 (PA9/PA10). On boards whose on-board ST-LINK has
no VCP (PID 0x3748), connect an external USB-TTL adapter to PA9/PA10/GND.
"""

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


def drain(ser: serial.Serial, settle: float = 0.2) -> bytes:
    """Read whatever is pending until the line goes quiet."""
    buf = bytearray()
    deadline = time.time() + settle
    while time.time() < deadline:
        n = ser.in_waiting
        if n:
            buf += ser.read(n)
            deadline = time.time() + settle
        else:
            time.sleep(0.01)
    return bytes(buf)


def read_line(ser: serial.Serial, timeout: float = 2.0) -> str:
    """Read one CRLF/LF-terminated text line, skipping telemetry (TELM:)."""
    deadline = time.time() + timeout
    line = bytearray()
    while time.time() < deadline:
        b = ser.read(1)
        if not b:
            continue
        if b in (b"\n", b"\r"):
            if line:
                text = line.decode("ascii", errors="replace").strip()
                line = bytearray()
                if text.startswith("TELM:"):
                    continue  # ignore async telemetry while waiting for a reply
                if text:
                    return text
            continue
        line += b
    return ""


def send(ser: serial.Serial, cmd: str) -> None:
    ser.reset_input_buffer()
    ser.write((cmd + "\r\n").encode("ascii"))
    ser.flush()


def query(ser: serial.Serial, cmd: str, timeout: float = 2.0) -> str:
    send(ser, cmd)
    return read_line(ser, timeout)


def silence_telemetry(ser: serial.Serial) -> None:
    send(ser, "TELE:STAT OFF")
    drain(ser, 0.3)


def read_exact(ser: serial.Serial, n: int, timeout: float = 30.0) -> bytes:
    buf = bytearray()
    deadline = time.time() + timeout
    while len(buf) < n and time.time() < deadline:
        chunk = ser.read(min(4096, n - len(buf)))
        if chunk:
            buf += chunk
            deadline = time.time() + timeout
    return bytes(buf)


def read_scpi_block(ser: serial.Serial, timeout: float = 60.0) -> bytes:
    """Parse a SCPI definite-length block: #<ndigits><length><bytes>."""
    deadline = time.time() + timeout

    # Find the leading '#'
    while time.time() < deadline:
        b = ser.read(1)
        if b == b"#":
            break
    else:
        raise TimeoutError("no SCPI block header '#' received")

    nd = ser.read(1)
    if not nd.isdigit():
        raise ValueError(f"bad block header digit-count: {nd!r}")
    ndigits = int(nd)

    len_field = ser.read(ndigits)
    if len(len_field) != ndigits or not len_field.isdigit():
        raise ValueError(f"bad block length field: {len_field!r}")
    length = int(len_field)

    payload = read_exact(ser, length, timeout)
    if len(payload) != length:
        raise IOError(f"short capture: got {len(payload)} of {length} bytes")

    drain(ser, 0.2)  # trailing CRLF
    return payload


def cmd_idn(ser: serial.Serial) -> int:
    print(query(ser, "*IDN?"))
    return 0


def cmd_meas(ser: serial.Serial) -> int:
    silence_telemetry(ser)
    print(query(ser, "MEAS:ALL?"))
    return 0


def cmd_list(ser: serial.Serial) -> int:
    silence_telemetry(ser)
    send(ser, "METR:LIST?")
    print(drain(ser, 0.4).decode("ascii", errors="replace").strip())
    return 0


def cmd_capture(ser: serial.Serial, args: argparse.Namespace) -> int:
    silence_telemetry(ser)

    if args.depth is not None:
        print("depth:", query(ser, f"CAP:DEPT {args.depth}"))
    if args.rate is not None:
        print("rate :", query(ser, f"CAP:RATE {args.rate}"))

    depth = int(query(ser, "CAP:DEPT?") or "0")
    rate = int(query(ser, "CAP:RATE?") or "0")
    print(f"capturing depth={depth} bytes rate={rate} Hz ...")

    send(ser, "*TRG")
    payload = read_scpi_block(ser)
    print(f"received {len(payload)} bytes ({len(payload)//2} uint16 samples)")

    import struct
    samples = struct.unpack(f"<{len(payload)//2}H", payload)
    print("first 16 samples:", list(samples[:16]))

    if args.output:
        Path(args.output).write_bytes(payload)
        print(f"saved raw block -> {args.output}")

    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-p", "--port", required=True, help="serial device, e.g. /dev/cu.usbserial-XXXX")
    ap.add_argument("-b", "--baud", type=int, default=115200)
    ap.add_argument("--idn", action="store_true", help="query *IDN?")
    ap.add_argument("--meas", action="store_true", help="read MEAS:ALL? once")
    ap.add_argument("--list", action="store_true", help="read METR:LIST?")
    ap.add_argument("--capture", action="store_true", help="trigger one capture")
    ap.add_argument("--depth", type=int, help="capture depth in bytes")
    ap.add_argument("--rate", type=int, help="sample rate in Hz")
    ap.add_argument("-o", "--output", help="save capture payload to file")
    args = ap.parse_args()

    with serial.Serial(args.port, args.baud, timeout=0.2) as ser:
        time.sleep(0.1)
        drain(ser, 0.2)

        if args.idn:
            return cmd_idn(ser)
        if args.list:
            return cmd_list(ser)
        if args.meas:
            return cmd_meas(ser)
        if args.capture:
            return cmd_capture(ser, args)

        # Default: identify + one metrics read.
        cmd_idn(ser)
        return cmd_meas(ser)


if __name__ == "__main__":
    sys.exit(main())
