#!/usr/bin/env python3
"""Sweep tars-io-mux channels and correlate scope COM vs PA4 (dac0).

Fail-fast: do not write a success artifact if nodebus health/mux or scope
preflight fails. Intended for conda env ``hw`` (pyvisa + numpy).
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
USB_SHELL = ROOT / "tools" / "usb-shell.py"
DEFAULT_OUT = ROOT / "models" / "captured" / "mux_ch0_9_pa4_corr.npz"


class GateError(RuntimeError):
    """Experiment gate failed; no success artifact should be written."""


def _run_shell(port: str, wait: float, *cmds: str) -> str:
    proc = subprocess.run(
        [sys.executable, str(USB_SHELL), "-p", port, "-w", str(wait), *cmds],
        cwd=ROOT,
        capture_output=True,
        text=True,
        check=False,
    )
    out = (proc.stdout or "") + (proc.stderr or "")
    if proc.returncode != 0:
        raise GateError(f"usb-shell exit {proc.returncode}: {out[-500:]}")
    return out


def preflight_nodebus(port: str, addr: int, wait: float) -> dict:
    probe = _run_shell(port, wait, "nodebus probe")
    if f"ACK 0x{addr:02X}" not in probe and f"ACK 0x{addr:02x}" not in probe:
        raise GateError(f"nodebus probe: no ACK for 0x{addr:02X}\n{probe}")

    health = _run_shell(port, wait, f"nodebus health 0x{addr:02X}")
    if "read failed" in health or f"0x{addr:02X}:" not in health:
        raise GateError(f"nodebus health failed:\n{health}")
    m = re.search(
        r"0x[0-9A-Fa-f]+:\s+st=(0x[0-9A-Fa-f]+)\s+flt=(0x[0-9A-Fa-f]+)",
        health,
    )
    if not m:
        raise GateError(f"nodebus health: unparseable:\n{health}")
    return {"raw": health.strip(), "st": m.group(1), "flt": m.group(2)}


def ensure_awg(port: str, wait: float, ch: str) -> None:
    out = _run_shell(
        port,
        wait,
        f"mcu res grant {ch} awg",
        f"mcu awg gen {ch} sine",
        f"mcu awg freq {ch} 1000",
        f"mcu awg enable {ch} 1",
        f"mcu awg status {ch}",
    )
    if f"ch={ch}" not in out or "run=1" not in out:
        # grant may say err=active; status line is authoritative
        if "run=1" not in out:
            raise GateError(f"AWG not running on {ch}:\n{out}")


def mux_select(port: str, wait: float, addr: int, channel: int) -> None:
    out = _run_shell(port, wait, f"nodebus mux 0x{addr:02X} {channel}")
    m = re.search(
        rf"mux 0x{addr:02X} ch={channel} sel=(-?\d+) en=(-?\d+)",
        out,
        re.IGNORECASE,
    )
    if not m:
        raise GateError(f"mux parse failed for ch={channel}:\n{out}")
    sel, en = int(m.group(1)), int(m.group(2))
    if sel != 0 or en != 0:
        raise GateError(
            f"mux ch={channel} not OK (sel={sel} en={en}; want 0/0):\n{out}"
        )


def corr(a, b) -> float:
    import numpy as np

    n = min(len(a), len(b))
    a = a[:n] - a[:n].mean()
    b = b[:n] - b[:n].mean()
    d = float(np.linalg.norm(a) * np.linalg.norm(b))
    return float(np.dot(a, b) / d) if d > 0 else 0.0


def open_scope(resource: str):
    import pyvisa

    rm = pyvisa.ResourceManager()
    inst = rm.open_resource(resource)
    inst.timeout = 12000
    idn = inst.query("*IDN?").strip()
    if "RIGOL" not in idn.upper() and "DS1" not in idn.upper():
        inst.close()
        raise GateError(f"unexpected scope IDN: {idn}")
    return inst, idn


def setup_scope(inst) -> None:
    inst.write("*CLS")
    inst.write(":TRIGger:SWEep AUTO")
    inst.write(":TRIGger:EDGe:SOURce CHANnel1")
    inst.write(":TRIGger:EDGe:LEVel 1.5")
    for ch in (1, 3):
        inst.write(f":CHANnel{ch}:DISPlay ON")
        inst.write(f":CHANnel{ch}:COUPling DC")
        inst.write(f":CHANnel{ch}:PROBe 1")
        inst.write(f":CHANnel{ch}:SCALe 0.5")
        inst.write(f":CHANnel{ch}:OFFSet -1.0")
    inst.write(":CHANnel2:DISPlay OFF")
    inst.write(":TIMebase:SCALe 5e-4")
    inst.write(":RUN")
    time.sleep(0.5)


def capture_pair(
    inst,
    min_vpp: float,
    min_corr: float,
    retries: int,
):
    import numpy as np

    best = None
    for _ in range(retries):
        inst.write(":STOP")
        time.sleep(0.18)
        waves = {}
        for ch in (1, 3):
            inst.write(f":WAVeform:SOURce CHANnel{ch}")
            inst.write(":WAVeform:MODE NORMal")
            inst.write(":WAVeform:FORMat BYTE")
            inst.write(":WAVeform:POINts 1200")
            pre = [float(x) for x in inst.query(":WAVeform:PREamble?").strip().split(",")]
            xinc, yinc, yorig, yref = pre[4], pre[7], pre[8], pre[9]
            raw = np.array(
                inst.query_binary_values(":WAVeform:DATA?", datatype="B", container=list),
                dtype=float,
            )
            if len(raw) == 0:
                raise GateError(f"empty waveform on CH{ch}")
            waves[ch] = (raw - yref - yorig) * yinc
            waves["xinc"] = xinc
        inst.write(":RUN")
        time.sleep(0.1)
        v1 = float(waves[1].max() - waves[1].min())
        v3 = float(waves[3].max() - waves[3].min())
        c = corr(waves[3], waves[1])
        if best is None or c > best[0]:
            best = (c, waves, v1, v3)
        # Accept only when amplitude and correlation both look real.
        if v1 >= min_vpp and v3 >= min_vpp * 0.5 and c >= min_corr:
            return waves, v1, v3, c
        time.sleep(0.25)
    assert best is not None
    return best[1], best[2], best[3], best[0]


def write_failed(path: Path, payload: dict) -> None:
    fail_path = path.with_suffix(".failed.json")
    fail_path.parent.mkdir(parents=True, exist_ok=True)
    fail_path.write_text(json.dumps(payload, indent=2) + "\n")
    print(f"wrote diagnostic: {fail_path}", file=sys.stderr)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("-p", "--port", default="/dev/cu.usbmodem346D345933331")
    ap.add_argument("--addr", type=lambda s: int(s, 0), default=0x10)
    ap.add_argument("--channels", default="0-9", help="e.g. 0-9 or 0,1,4")
    ap.add_argument("--awg", default="dac0")
    ap.add_argument(
        "--scope",
        default="USB0::0x1AB1::0x04CE::DS1ZC244706177::INSTR",
    )
    ap.add_argument("-o", "--output", type=Path, default=DEFAULT_OUT)
    ap.add_argument("--wait", type=float, default=0.3)
    ap.add_argument("--settle", type=float, default=0.4)
    ap.add_argument("--min-corr", type=float, default=0.9)
    ap.add_argument("--min-gain", type=float, default=0.5)
    ap.add_argument("--min-vpp", type=float, default=3.5)
    ap.add_argument("--retries", type=int, default=5)
    args = ap.parse_args()

    if "-" in args.channels:
        a, b = args.channels.split("-", 1)
        channels = list(range(int(a), int(b) + 1))
    else:
        channels = [int(x) for x in args.channels.split(",") if x.strip() != ""]

    started = datetime.now(timezone.utc).isoformat()
    meta: dict = {
        "started_at": started,
        "addr": f"0x{args.addr:02X}",
        "channels": channels,
        "awg": args.awg,
        "criteria": {
            "min_corr": args.min_corr,
            "min_gain": args.min_gain,
            "min_vpp_pa4": args.min_vpp,
        },
    }

    try:
        print("==> preflight nodebus")
        meta["health"] = preflight_nodebus(args.port, args.addr, args.wait)
        for line in meta["health"]["raw"].splitlines():
            if line.startswith(f"0x{args.addr:02X}:") or line.startswith(
                f"0x{args.addr:02x}:"
            ):
                print(line)
                break
        else:
            print(meta["health"]["raw"].strip().splitlines()[-1])

        print("==> ensure AWG")
        ensure_awg(args.port, args.wait, args.awg)

        print("==> open scope")
        inst, idn = open_scope(args.scope)
        meta["scope_idn"] = idn
        print(idn)
        setup_scope(inst)
    except GateError as e:
        meta["error"] = str(e)
        write_failed(args.output, meta)
        print(f"GATE FAIL (preflight): {e}", file=sys.stderr)
        return 2
    except Exception as e:  # noqa: BLE001 — surface VISA/serial unexpectedly
        meta["error"] = f"{type(e).__name__}: {e}"
        write_failed(args.output, meta)
        print(f"GATE FAIL (preflight): {e}", file=sys.stderr)
        return 2

    import numpy as np

    rows = []
    store: dict = {}
    try:
        print(f'{"ch":>3} {"Vpp_PA4":>8} {"Vpp_COM":>8} {"gain":>7} {"corr":>7} {"pass":>5}')
        print("-" * 50)
        for ch in channels:
            mux_select(args.port, args.wait, args.addr, ch)
            time.sleep(args.settle)
            waves, vpp1, vpp3, c = capture_pair(
                inst, args.min_vpp, args.min_corr, args.retries
            )
            gain = vpp3 / vpp1 if vpp1 > 1e-6 else float("nan")
            ok = (c >= args.min_corr) and (gain > args.min_gain) and (vpp1 >= args.min_vpp)
            rows.append(
                {
                    "ch": ch,
                    "vpp_pa4": vpp1,
                    "vpp_com": vpp3,
                    "gain": gain,
                    "corr": c,
                    "pass": ok,
                }
            )
            store[f"ch{ch}_pa4"] = waves[1]
            store[f"ch{ch}_com"] = waves[3]
            print(
                f"{ch:3d} {vpp1:8.3f} {vpp3:8.3f} {gain:7.3f} {c:7.3f} {str(ok):>5}"
            )
            if not ok:
                raise GateError(
                    f"channel {ch} failed criteria "
                    f"(corr={c:.3f} gain={gain:.3f} vpp_pa4={vpp1:.3f})"
                )
        store["xinc"] = waves["xinc"]
        store["corr"] = np.array([r["corr"] for r in rows])
        store["gain"] = np.array([r["gain"] for r in rows])
    except GateError as e:
        meta["error"] = str(e)
        meta["rows"] = rows
        write_failed(args.output, meta)
        print(f"GATE FAIL: {e}", file=sys.stderr)
        print("success artifact NOT written", file=sys.stderr)
        return 3
    finally:
        try:
            inst.close()
        except Exception:
            pass

    args.output.parent.mkdir(parents=True, exist_ok=True)
    np.savez(args.output, **store)
    summary = {
        **meta,
        "finished_at": datetime.now(timezone.utc).isoformat(),
        "pass": True,
        "n_pass": len(rows),
        "n_channels": len(channels),
        "corr_min": min(r["corr"] for r in rows),
        "corr_mean": sum(r["corr"] for r in rows) / len(rows),
        "corr_max": max(r["corr"] for r in rows),
        "gain_min": min(r["gain"] for r in rows),
        "gain_mean": sum(r["gain"] for r in rows) / len(rows),
        "rows": rows,
        "npz": str(args.output),
    }
    summary_path = args.output.with_suffix(".summary.json")
    summary_path.write_text(json.dumps(summary, indent=2) + "\n")

    print("-" * 50)
    print(f"PASS {len(rows)}/{len(channels)}")
    print(
        f"corr min/mean/max = {summary['corr_min']:.3f} / "
        f"{summary['corr_mean']:.3f} / {summary['corr_max']:.3f}"
    )
    print(f"saved {args.output}")
    print(f"saved {summary_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
