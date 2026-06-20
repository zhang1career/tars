#!/usr/bin/env python3
"""Pack TARS app images. MVP: lua only. Native: Phase 1 fixed-slot (post-MVP)."""

from __future__ import annotations

import argparse
import re
import struct
import subprocess
import sys
from pathlib import Path

TARS_APP_MAGIC = 0x54504150
TARS_LUA_MAGIC = 0x5450484C
TARS_API_VERSION = 1
TARS_NATIVE_SLOT_BASE = 0x080A0000
TARS_NATIVE_SLOT_STRIDE = 0x20000

HEADER_FMT = "<IHHII16sIBBH16sI6I"


def _load_tars_crc32_table() -> list[int]:
    crc_c = Path(__file__).resolve().parent.parent / "App" / "tars_crc.c"
    text = crc_c.read_text(encoding="utf-8")
    match = re.search(r"s_crc32_table\[256\]\s*=\s*\{(.*?)\};", text, re.S)
    if match is None:
        raise RuntimeError(f"CRC table not found in {crc_c}")

    return [int(value, 16) for value in re.findall(r"0x([0-9A-Fa-f]{8})", match.group(1))]


TARS_CRC32_TABLE = _load_tars_crc32_table()


def tars_crc32_update(data: bytes, crc: int = 0xFFFFFFFF) -> int:
    for byte in data:
        crc = TARS_CRC32_TABLE[(crc ^ byte) & 0xFF] ^ (crc >> 8)
    return crc & 0xFFFFFFFF


def tars_crc32_blob(hdr_prefix: bytes, payload: bytes) -> int:
    crc = tars_crc32_update(hdr_prefix)
    crc = tars_crc32_update(payload, crc)
    return crc ^ 0xFFFFFFFF


def read_elf_sections(elf: Path) -> tuple[bytes, bytes, int, int]:
    readelf = subprocess.check_output(["arm-none-eabi-readelf", "-S", str(elf)], text=True)

    def section_size(name: str) -> int:
        for line in readelf.splitlines():
            if f"] {name}" in line:
                parts = line.split()
                return int(parts[5], 16)
        return 0

    text_size = section_size(".text") + section_size(".rodata")
    data_size = section_size(".data")
    bss_size = section_size(".bss")

    text = subprocess.check_output(
        ["arm-none-eabi-objcopy", "-O", "binary", "--only-section=.text", str(elf), "/dev/stdout"],
        stderr=subprocess.DEVNULL,
    )
    rodata = subprocess.check_output(
        ["arm-none-eabi-objcopy", "-O", "binary", "--only-section=.rodata", str(elf), "/dev/stdout"],
        stderr=subprocess.DEVNULL,
    )
    data = subprocess.check_output(
        ["arm-none-eabi-objcopy", "-O", "binary", "--only-section=.data", str(elf), "/dev/stdout"],
        stderr=subprocess.DEVNULL,
    )

    entry_offset = 0
    nm = subprocess.run(["arm-none-eabi-nm", str(elf)], capture_output=True, text=True)
    for line in nm.stdout.splitlines():
        if line.endswith(" T app_entry"):
            entry_offset = int(line.split()[0], 16)
            break

    return text + rodata, data, bss_size, entry_offset


TARS_NATIVE_HDR_CRC_LEN = 92


def pack_native(args: argparse.Namespace) -> None:
    text, data, bss_size, entry_offset = read_elf_sections(Path(args.elf))

    flags = 0x0011  # TARS_LOAD_FIXED | TARS_EXEC_FLASH
    link_base = TARS_NATIVE_SLOT_BASE + args.slot * TARS_NATIVE_SLOT_STRIDE

    manifest_count = len(args.resource or [])
    manifest = bytes([manifest_count]) + bytes(15 * 4)

    header_size = 96
    payload = text + data
    payload_size = len(payload)

    header = struct.pack(
        HEADER_FMT,
        TARS_APP_MAGIC,
        header_size,
        flags,
        TARS_API_VERSION,
        args.name.encode("ascii")[:15].ljust(16, b"\0"),
        args.version,
        1,
        args.timeslice,
        0,
        manifest,
        link_base,
        len(text),
        len(data),
        bss_size,
        entry_offset,
        0,
        payload_size,
        0,
    )

    blob = bytearray(header)
    blob.extend(payload)
    crc = tars_crc32_blob(bytes(header[:TARS_NATIVE_HDR_CRC_LEN]), payload)
    blob[TARS_NATIVE_HDR_CRC_LEN : TARS_NATIVE_HDR_CRC_LEN + 4] = struct.pack("<I", crc)

    Path(args.output).write_bytes(blob)
    print(f"wrote {args.output} ({len(blob)} bytes, phase1 fixed slot={args.slot})")


TARS_LUA_HDR_SIZE = 108
TARS_LUA_HDR_CRC_LEN = 104


def pack_lua_manifest() -> bytes:
    return bytes(68)


def pack_lua(args: argparse.Namespace) -> None:
    lua = Path(args.lua).read_bytes()
    name = args.name.encode("ascii")[:15].ljust(16, b"\0")
    manifest = pack_lua_manifest()
    header_size = TARS_LUA_HDR_SIZE

    header = bytearray(header_size)
    struct.pack_into("<I", header, 0, TARS_LUA_MAGIC)
    struct.pack_into("<H", header, 4, header_size)
    struct.pack_into("<H", header, 6, 0)
    header[8:24] = name
    struct.pack_into("<I", header, 24, args.version)
    header[28] = args.timeslice & 0xFF
    header[29] = args.priority & 0xFF
    header[30:32] = b"\0\0"
    header[32:100] = manifest
    struct.pack_into("<I", header, 100, len(lua))

    crc = tars_crc32_blob(bytes(header[:TARS_LUA_HDR_CRC_LEN]), lua)
    struct.pack_into("<I", header, 104, crc)

    blob = bytes(header) + lua
    Path(args.output).write_bytes(blob)
    print(f"wrote {args.output} ({len(blob)} bytes, lua)")


def main() -> None:
    parser = argparse.ArgumentParser(description="Pack TARS app images (MVP: lua)")
    sub = parser.add_subparsers(dest="kind", required=True)

    native = sub.add_parser("native", help="Pack Phase 1 fixed-slot native ELF (post-MVP)")
    native.add_argument("elf")
    native.add_argument("-o", "--output", required=True)
    native.add_argument("--name", required=True)
    native.add_argument("--version", type=int, default=1)
    native.add_argument("--timeslice", type=int, default=1)
    native.add_argument("--slot", type=int, default=0)
    native.add_argument("--resource", action="append")
    native.set_defaults(func=pack_native)

    lua = sub.add_parser("lua", help="Pack Lua source app")
    lua.add_argument("lua")
    lua.add_argument("-o", "--output", required=True)
    lua.add_argument("--name", required=True)
    lua.add_argument("--version", type=int, default=1)
    lua.add_argument("--timeslice", type=int, default=1)
    lua.add_argument("--priority", type=int, default=10)
    lua.set_defaults(func=pack_lua)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
