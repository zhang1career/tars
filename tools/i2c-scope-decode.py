#!/usr/bin/env python3
"""Capture Rigol DS1104Z CH1=SDA / CH2=SCL while TARS fires Node Bus I2C, decode 7-bit addr."""

from __future__ import annotations

import argparse
import subprocess
import sys
import time
from pathlib import Path

import numpy as np

SCOPE_ADDR = "USB0::0x1AB1::0x04CE::DS1ZC244706177::INSTR"
TARS_ROOT = Path(__file__).resolve().parents[1]


def open_scope():
    import pyvisa

    rm = pyvisa.ResourceManager()
    scope = rm.open_resource(SCOPE_ADDR)
    scope.timeout = 15000
    scope.write_termination = "\n"
    scope.read_termination = "\n"
    print(scope.query("*IDN?").strip())
    return scope


def setup_scope(scope, *, tdiv_s: float = 1e-3, trig_lev_v: float = 1.0) -> None:
    """CH1=SDA, CH2=SCL; single-shot on SDA fall."""
    scope.write(":STOP")
    scope.write(":CHAN1:DISP ON")
    scope.write(":CHAN2:DISP ON")
    scope.write(":CHAN1:COUP DC")
    scope.write(":CHAN2:COUP DC")
    scope.write(":CHAN1:SCAL 2")
    scope.write(":CHAN2:SCAL 2")
    scope.write(":CHAN1:OFFS -6")
    scope.write(":CHAN2:OFFS -6")
    scope.write(f":TIM:SCAL {tdiv_s}")
    scope.write(":TIM:OFFS 0")
    scope.write(":ACQ:MODE NORM")
    scope.write(":TRIG:MODE EDGE")
    scope.write(":TRIG:EDGE:SOUR CHAN1")
    scope.write(":TRIG:EDGE:SLOP FALL")
    scope.write(f":TRIG:EDGE:LEV {trig_lev_v}")
    scope.write(":TRIG:SWE SING")
    time.sleep(0.3)


def arm_single(scope) -> None:
    scope.write(":STOP")
    scope.write(":SING")
    time.sleep(0.05)


def wait_capture(scope, timeout_s: float = 3.0) -> bool:
    t0 = time.time()
    while time.time() - t0 < timeout_s:
        # TRIG status: wait until not waiting (Rigol returns "STOP" when acquired)
        st = scope.query(":TRIG:STAT?").strip()
        if st in ("STOP", "TD", "SAVE", "AUTO"):
            time.sleep(0.05)
            return True
        time.sleep(0.02)
    return False


def read_waveform(scope, channel: int) -> tuple[np.ndarray, np.ndarray]:
    ch = f"CHAN{channel}"
    scope.write(f":WAV:SOUR {ch}")
    scope.write(":WAV:FORM BYTE")
    scope.write(":WAV:MODE NORM")
    data = scope.query_binary_values(":WAV:DATA?", datatype="B", container=np.array)
    xinc = float(scope.query(":WAV:XINC?"))
    xorg = float(scope.query(":WAV:XOR?"))
    yinc = float(scope.query(":WAV:YINC?"))
    yorg = float(scope.query(":WAV:YOR?"))
    yref = float(scope.query(":WAV:YREF?"))
    v = (np.asarray(data, dtype=float) - yref) * yinc + yorg
    t = np.arange(len(v)) * xinc + xorg
    return t, v


def digitize(v: np.ndarray, thr: float | None = None) -> tuple[np.ndarray, float]:
    if thr is None:
        lo, hi = np.percentile(v, [10, 90])
        thr = float((lo + hi) / 2.0)
    return (v >= thr).astype(np.uint8), thr


def find_starts(sda: np.ndarray, scl: np.ndarray) -> list[int]:
    """Return sample indices where SDA falls while SCL is high (I2C START)."""
    out: list[int] = []
    for i in range(1, len(sda)):
        if scl[i] and sda[i - 1] and not sda[i]:
            out.append(i)
    return out


def decode_byte(sda: np.ndarray, scl: np.ndarray, pos: int) -> tuple[int | None, int | None]:
    """Decode one data byte starting near pos; return (byte, ack_sample) or (None, None)."""
    n = len(sda)
    i = pos
    # Wait for first SCL rising edge with SCL currently low.
    while i < n - 1 and scl[i]:
        i += 1
    byte = 0
    bits = 0
    while i < n - 1 and bits < 8:
        # rising edge on SCL
        if not scl[i] and scl[i + 1]:
            bit = 1 if sda[i + 1] else 0
            byte = (byte << 1) | bit
            bits += 1
            i += 1
        i += 1
    if bits != 8:
        return None, None
    # ACK bit: next SCL rising, SDA should be low for ACK
    ack = None
    while i < n - 1:
        if not scl[i] and scl[i + 1]:
            ack = 0 if not sda[i + 1] else 1
            break
        i += 1
    return byte, ack


def decode_first_frame(t_sda: np.ndarray, v_sda: np.ndarray, v_scl: np.ndarray) -> dict | None:
    """Return first well-formed 8-bit frame + ACK after an I2C START."""
    sda, _ = digitize(v_sda)
    scl, _ = digitize(v_scl)
    starts = find_starts(sda, scl)
    for start in starts:
        byte, ack = decode_byte(sda, scl, start)
        if byte is None or ack is None:
            continue
        return {
            "t_start_s": float(t_sda[start]),
            "addr_byte": byte,
            "addr7": byte >> 1,
            "rw": "R" if (byte & 1) else "W",
            "ack": "ACK" if ack == 0 else "NACK",
        }
    return None


def fire_tars_cmd(cmd: str) -> str:
    shell = TARS_ROOT / "tools" / "usb-shell.py"
    r = subprocess.run(
        [sys.executable, str(shell), cmd],
        check=False,
        capture_output=True,
        text=True,
        timeout=30,
    )
    return (r.stdout or "") + (r.stderr or "")


def capture_one(scope, tars_cmd: str, tag: str, out_dir: Path) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    # Try single-shot near trigger; fall back to RUN window if empty.
    arm_single(scope)
    time.sleep(0.05)
    out = fire_tars_cmd(tars_cmd)
    print(f"--- tars: {tars_cmd} ---")
    print(out.strip()[:400])
    wait_capture(scope)
    time.sleep(0.15)
    t1, v1 = read_waveform(scope, 1)
    t2, v2 = read_waveform(scope, 2)
    if len(t1) < 16:
        scope.write(":RUN")
        time.sleep(0.05)
        fire_tars_cmd(tars_cmd)
        time.sleep(0.3)
        scope.write(":STOP")
        time.sleep(0.1)
        t1, v1 = read_waveform(scope, 1)
        t2, v2 = read_waveform(scope, 2)
    np.savez(out_dir / f"capture_{tag}.npz", time_s=t1, sda_v=v1, scl_v=v2)
    return t1, v1, v2


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--addrs", default="0x13,0x14", help="hex addr list, e.g. 0x13,0x14")
    ap.add_argument("--no-tars", action="store_true", help="only capture/decode (manual trigger)")
    ap.add_argument("--out-dir", type=Path, default=TARS_ROOT / "models" / "captured" / "i2c_scope")
    args = ap.parse_args()
    addrs = [int(x.strip(), 0) for x in args.addrs.split(",") if x.strip()]
    args.out_dir.mkdir(parents=True, exist_ok=True)

    scope = open_scope()
    setup_scope(scope)

    all_events: list[dict] = []
    seen_ack: set[int] = set()

    if not args.no_tars:
        for a in addrs:
            tag = f"{a:02X}"
            exp_byte = (a << 1) | 0  # ADDR+W
            t1, v1, v2 = capture_one(scope, f"nodebus rd 0x{a:02X} 0x00 1", tag, args.out_dir)
            print(f"capture {tag}: samples={len(t1)} SDA {v1.min():.2f}..{v1.max():.2f}V  "
                  f"SCL {v2.min():.2f}..{v2.max():.2f}V")
            if len(t1) < 16:
                print("  skip: too few samples")
                continue
            ev = decode_first_frame(t1, v1, v2)
            if ev is None:
                print(f"  no I2C frame decoded")
                continue
            ev["probe_addr"] = a
            ev["expected_byte"] = exp_byte
            ev["match"] = ev["addr_byte"] == exp_byte
            all_events.append(ev)
            if ev["ack"] == "ACK" and ev["match"]:
                seen_ack.add(ev["addr7"])
            print(
                f"  frame byte=0x{ev['addr_byte']:02X} addr7=0x{ev['addr7']:02X} "
                f"{ev['rw']} {ev['ack']} expected=0x{exp_byte:02X} match={ev['match']}"
            )
    else:
        scope.write(":RUN")
        time.sleep(1.0)
        scope.write(":STOP")
        t1, v1 = read_waveform(scope, 1)
        t2, v2 = read_waveform(scope, 2)
        np.savez(args.out_dir / "capture.npz", time_s=t1, sda_v=v1, scl_v=v2)
        ev = decode_first_frame(t1, v1, v2)
        if ev:
            all_events = [ev]
            if ev["ack"] == "ACK":
                seen_ack.add(ev["addr7"])

    scope.close()

    if not all_events:
        print("\nNo I2C frames decoded.")
        return 2

    print("\n=== Result ===")
    for ev in all_events:
        pa = ev.get("probe_addr")
        mark = ""
        if ev.get("match") and ev["ack"] == "ACK":
            mark = "  ** ONLINE **"
        print(
            f"probe 0x{pa:02X}: wire byte=0x{ev['addr_byte']:02X} → addr7=0x{ev['addr7']:02X} "
            f"{ev['rw']} {ev['ack']}{mark}"
        )

    print("\nSummary:")
    if seen_ack:
        for a in sorted(seen_ack):
            print(f"  Slave ACK at 0x{a:02X}")
    else:
        print("  Neither target address ACK'd with matching ADDR+W byte")
    return 0 if seen_ack else 3


if __name__ == "__main__":
    raise SystemExit(main())
