#!/usr/bin/env python3
"""Upload an arbitrary waveform to the TARS AWG over the USB shell.

The device streams `points` raw little-endian uint16 DAC codes (0-4095) into a
channel's SDRAM table, then plays them out via TIM7-triggered circular DMA.

Sample source (pick one):
  --expr "sin(2*pi*t)"   Python expression; vars: t in [0,1), i, n, plus math.*
  --func NAME            sin | square | triangle | sawtooth | noise
  --file PATH            text file of numbers (whitespace/comma separated)

Values are treated as normalized [-1, 1] and mapped with --ampl/--offset,
unless --codes says they are already 0-4095 DAC codes.

Examples:
  ./tools/awg-upload.py dac0 --func sin --points 512 --freq 1000 --grant awg --enable
  ./tools/awg-upload.py dac0 --expr "sin(2*pi*t)+0.3*sin(6*pi*t)" --points 1024
  ./tools/awg-upload.py dac0 --file scope_trace.csv --codes
"""

from __future__ import annotations

import argparse
import glob
import math
import struct
import sys
import time

try:
    import serial
except ImportError:
    print("awg-upload: pyserial required (pip install pyserial)", file=sys.stderr)
    sys.exit(1)

DAC_MAX = 4095
MAX_POINTS = 8192  # keep in sync with TARS_AWG_CH_MAX_POINTS


def find_port(explicit: str | None) -> str:
    if explicit:
        return explicit
    ports = sorted(glob.glob("/dev/tty.usbmodem*"))
    if not ports:
        raise SystemExit("awg-upload: no /dev/tty.usbmodem* port found")
    return ports[-1]


def read_until(ser: serial.Serial, needles: tuple[str, ...], timeout: float) -> tuple[bool, str]:
    buf = ""
    deadline = time.time() + timeout
    while time.time() < deadline:
        n = ser.in_waiting
        if n:
            buf += ser.read(n).decode("ascii", errors="replace")
            if any(x in buf for x in needles):
                return True, buf
        time.sleep(0.02)
    return False, buf


def gen_samples(args) -> list[float]:
    n = args.points
    if args.file:
        raw = open(args.file, "r", encoding="utf-8").read().replace(",", " ")
        vals = [float(x) for x in raw.split()]
        if not vals:
            raise SystemExit("awg-upload: file has no numbers")
        return vals

    if args.expr:
        env = {k: getattr(math, k) for k in dir(math) if not k.startswith("_")}
        out = []
        for i in range(n):
            env.update({"i": i, "n": n, "t": i / n})
            out.append(float(eval(args.expr, {"__builtins__": {}}, env)))
        return out

    func = args.func or "sin"
    out = []
    for i in range(n):
        t = i / n
        if func in ("sin", "sine"):
            v = math.sin(2 * math.pi * t)
        elif func == "square":
            v = 1.0 if t < 0.5 else -1.0
        elif func in ("tri", "triangle"):
            v = 4 * abs(t - 0.5) - 1.0  # -1..1 triangle
        elif func in ("saw", "sawtooth"):
            v = 2 * t - 1.0
        elif func == "noise":
            import random
            v = random.uniform(-1.0, 1.0)
        else:
            raise SystemExit(f"awg-upload: unknown func {func}")
        out.append(v)
    return out


def to_codes(vals: list[float], args) -> list[int]:
    if args.codes:
        return [max(0, min(DAC_MAX, int(round(v)))) for v in vals]
    center = args.offset / 100.0 * DAC_MAX
    peak = args.ampl / 100.0 * (DAC_MAX / 2.0)
    return [max(0, min(DAC_MAX, int(round(center + peak * v)))) for v in vals]


def main() -> int:
    p = argparse.ArgumentParser(description="Upload arbitrary waveform to TARS AWG")
    p.add_argument("channel", help="dac0 or dac1")
    src = p.add_mutually_exclusive_group()
    src.add_argument("--expr", help="Python expression of t/i/n (math.* available)")
    src.add_argument("--func", help="sin|square|triangle|sawtooth|noise")
    src.add_argument("--file", help="text file of numbers")
    p.add_argument("--points", type=int, default=256, help="points for expr/func (default 256)")
    p.add_argument("--codes", action="store_true", help="values are DAC codes 0-4095, not normalized")
    p.add_argument("--ampl", type=float, default=100.0, help="peak-to-peak %% of full scale (default 100)")
    p.add_argument("--offset", type=float, default=50.0, help="DC midpoint %% (default 50)")
    p.add_argument("--freq", type=int, help="set output frequency (Hz) after upload")
    p.add_argument("--grant", help="grant this tenant to the channel before enable")
    p.add_argument("--enable", action="store_true", help="enable output after upload")
    p.add_argument("-p", "--port", help="serial port (default: latest usbmodem)")
    p.add_argument("-b", "--baud", type=int, default=115200)
    args = p.parse_args()

    vals = gen_samples(args)
    codes = to_codes(vals, args)
    points = len(codes)
    if points < 2 or points > MAX_POINTS:
        raise SystemExit(f"awg-upload: points {points} out of range 2..{MAX_POINTS}")

    payload = struct.pack(f"<{points}H", *codes)
    port = find_port(args.port)

    with serial.Serial(port, args.baud, timeout=0.2) as ser:
        time.sleep(0.3)
        ser.reset_input_buffer()

        ser.write(f"mcu awg upload {args.channel} {points}\r\n".encode())
        ser.flush()
        ok, resp = read_until(ser, ("upload: ready", "upload: rejected", "upload: use"), 5.0)
        if "upload: ready" not in resp:
            print(resp.strip() or "device did not enter upload mode", file=sys.stderr)
            return 1

        ser.write(payload)
        ser.flush()
        ok, done = read_until(ser, ("upload:",), 10.0)
        for line in done.splitlines():
            if line.strip().startswith("upload:"):
                print(line.strip())

        if args.grant:
            ser.write(f"mcu res grant {args.channel} {args.grant}\r\n".encode())
            read_until(ser, ("tars>",), 3.0)
        if args.freq:
            ser.write(f"mcu awg freq {args.channel} {args.freq}\r\n".encode())
            read_until(ser, ("tars>",), 3.0)
        if args.enable:
            ser.write(f"mcu awg enable {args.channel} 1\r\n".encode())
            read_until(ser, ("tars>",), 3.0)

        ser.write(f"mcu awg status {args.channel}\r\n".encode())
        _, st = read_until(ser, ("tars>",), 3.0)
        for line in st.splitlines():
            if line.strip().startswith("awg:"):
                print(line.strip())

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
