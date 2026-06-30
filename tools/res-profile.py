#!/usr/bin/env python3
"""Pack/unpack TARS resource profile (/sys/res_profile.bin)."""

from __future__ import annotations

import argparse
import importlib.util
import json
import struct
import sys
from pathlib import Path

_GEN = Path(__file__).resolve().parent / "fw-identity-gen.py"
_spec = importlib.util.spec_from_file_location("fw_identity_gen", _GEN)
_mod = importlib.util.module_from_spec(_spec)
assert _spec.loader is not None
_spec.loader.exec_module(_mod)
tars_crc32 = _mod.tars_crc32

TRSP_MAGIC = 0x54525350
TRSP_VERSION = 2
TFWK_MAGIC = 0x5446574B
BOARD_ID_LEN = 24
ID_LEN = 16
TIM_LEN = 8

HDR_FMT = "<IHH24sIIIIII"
GRANT_FMT = "<16s4s"
PWM_FMT = "<16s4s"
TIM_FMT = "<8sI"


def parse_elf_identity(elf_path: Path) -> tuple[int, int]:
    data = elf_path.read_bytes()
    magic = struct.unpack_from("<I", data, 0)[0]
    if magic != 0x464C457F:
        raise SystemExit(f"not an ELF: {elf_path}")

    # Scan .rodata for embedded g_tars_fw_identity (magic TFWK).
    off = 0
    while off + 12 <= len(data):
        val = struct.unpack_from("<I", data, off)[0]
        if val == TFWK_MAGIC:
            crc, size = struct.unpack_from("<II", data, off + 4)
            return crc, size
        off += 4

    raise SystemExit(f"TFWK identity not found in {elf_path} (flash/build pass 2?)")


def identity_from_bin(bin_path: Path) -> tuple[int, int]:
    data = bin_path.read_bytes()
    return tars_crc32(data), len(data)


def pack_profile(doc: dict, fw_crc32: int, fw_size: int) -> bytes:
    board = doc.get("board", "stm32f429i-disc1")[: BOARD_ID_LEN - 1]
    grants = doc.get("grants", [])
    pwm = doc.get("pwm", [])
    tim = doc.get("tim", [])

    body = b""
    for g in grants:
        body += struct.pack(GRANT_FMT, g["id"].encode(), bytes([g["owner"], 0, 0, 0]))
    for p in pwm:
        body += struct.pack(
            PWM_FMT,
            p["channel"].encode(),
            bytes([p["duty"], p.get("boot_enable", 0), 0, 0]),
        )
    for t in tim:
        body += struct.pack(TIM_FMT, t["tim_id"].encode(), t["freq_hz"])

    hdr_prefix = struct.pack("<IHH", TRSP_MAGIC, TRSP_VERSION, 0)
    board_field = board.encode().ljust(BOARD_ID_LEN, b"\0")
    hdr_mid = struct.pack(
        "<IIIII",
        fw_crc32,
        fw_size,
        len(grants),
        len(pwm),
        len(tim),
    )
    partial = hdr_prefix + board_field + hdr_mid
    body_crc = tars_crc32(partial + body)
    hdr = partial + struct.pack("<I", body_crc)
    return hdr + body


def unpack_profile(data: bytes) -> dict:
    if len(data) < struct.calcsize(HDR_FMT):
        raise ValueError("truncated header")

    magic, version, _flags, board_raw, fw_crc, fw_size, ng, np, nt, body_crc = struct.unpack(
        HDR_FMT, data[: struct.calcsize(HDR_FMT)]
    )
    if magic != TRSP_MAGIC:
        raise ValueError("bad magic")
    if version != TRSP_VERSION:
        raise ValueError(f"unsupported version {version}")

    board = board_raw.split(b"\0", 1)[0].decode()
    offset = struct.calcsize(HDR_FMT)
    grants = []
    for _ in range(ng):
        chunk = data[offset : offset + struct.calcsize(GRANT_FMT)]
        offset += struct.calcsize(GRANT_FMT)
        gid, own = struct.unpack(GRANT_FMT, chunk)
        grants.append({"id": gid.split(b"\0", 1)[0].decode(), "owner": own[0]})

    pwm = []
    for _ in range(np):
        chunk = data[offset : offset + struct.calcsize(PWM_FMT)]
        offset += struct.calcsize(PWM_FMT)
        ch, rest = struct.unpack(PWM_FMT, chunk)
        pwm.append(
            {
                "channel": ch.split(b"\0", 1)[0].decode(),
                "duty": rest[0],
                "boot_enable": rest[1],
            }
        )

    tim = []
    for _ in range(nt):
        chunk = data[offset : offset + struct.calcsize(TIM_FMT)]
        offset += struct.calcsize(TIM_FMT)
        tid, freq = struct.unpack(TIM_FMT, chunk)
        tim.append({"tim_id": tid.split(b"\0", 1)[0].decode(), "freq_hz": freq})

    partial = data[: struct.calcsize(HDR_FMT) - 4]
    expect = tars_crc32(partial + data[struct.calcsize(HDR_FMT) :])
    if expect != body_crc:
        raise ValueError("body CRC mismatch")

    return {
        "board": board,
        "fw_crc32": fw_crc,
        "fw_image_size": fw_size,
        "grants": grants,
        "pwm": pwm,
        "tim": tim,
    }


def owner_name(code: int) -> str:
    return ["none", "gpio", "pwm", "foc", "system"][code] if code < 5 else str(code)


def cmd_pack(args: argparse.Namespace) -> int:
    doc = json.loads(args.json.read_text())
    if args.elf:
        fw_crc, fw_size = parse_elf_identity(args.elf)
    elif args.bin:
        fw_crc, fw_size = identity_from_bin(args.bin)
    else:
        raise SystemExit("pack requires --elf or --bin")

    blob = pack_profile(doc, fw_crc, fw_size)
    args.output.write_bytes(blob)
    print(f"packed {len(blob)} bytes -> {args.output} (fw_crc=0x{fw_crc:08X} size={fw_size})")
    return 0


def cmd_unpack(args: argparse.Namespace) -> int:
    doc = unpack_profile(args.profile.read_bytes())
    if args.json:
        args.json.write_text(json.dumps(doc, indent=2) + "\n")
    else:
        print(json.dumps(doc, indent=2))
    return 0


def cmd_dump(args: argparse.Namespace) -> int:
    doc = unpack_profile(args.profile.read_bytes())
    print(f"board={doc['board']} fw_crc=0x{doc['fw_crc32']:08X} fw_size={doc['fw_image_size']}")
    for g in doc["grants"]:
        print(f"  grant {g['id']} -> {owner_name(g['owner'])}")
    for p in doc["pwm"]:
        print(f"  pwm {p['channel']} duty={p['duty']} boot={p['boot_enable']}")
    for t in doc["tim"]:
        print(f"  tim {t['tim_id']} freq={t['freq_hz']}")
    return 0


def cmd_identity(args: argparse.Namespace) -> int:
    if args.elf:
        crc, size = parse_elf_identity(args.elf)
    else:
        crc, size = identity_from_bin(args.bin)
    print(f"image_crc32=0x{crc:08X}")
    print(f"image_size={size}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="TARS resource profile tool")
    sub = parser.add_subparsers(dest="cmd", required=True)

    p_pack = sub.add_parser("pack", help="JSON -> binary profile")
    p_pack.add_argument("json", type=Path)
    p_pack.add_argument("-o", "--output", type=Path, required=True)
    p_pack.add_argument("--elf", type=Path, help="tars.elf with embedded identity")
    p_pack.add_argument("--bin", type=Path, help="firmware .bin (pass-1 image)")
    p_pack.set_defaults(func=cmd_pack)

    p_unpack = sub.add_parser("unpack", help="binary profile -> JSON")
    p_unpack.add_argument("profile", type=Path)
    p_unpack.add_argument("-o", "--json", type=Path)
    p_unpack.set_defaults(func=cmd_unpack)

    p_dump = sub.add_parser("dump", help="human-readable profile")
    p_dump.add_argument("profile", type=Path)
    p_dump.set_defaults(func=cmd_dump)

    p_id = sub.add_parser("identity", help="show firmware CRC/size from elf or bin")
    p_id.add_argument("--elf", type=Path)
    p_id.add_argument("--bin", type=Path)
    p_id.set_defaults(func=cmd_identity)

    args = parser.parse_args()
    if args.cmd == "identity" and not args.elf and not args.bin:
        parser.error("identity requires --elf or --bin")
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
