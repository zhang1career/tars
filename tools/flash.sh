#!/usr/bin/env bash
# Flash tars.elf via OpenOCD + ST-LINK.
#
# Default (partition mode): erase/program firmware sectors 0-6 only (384 KiB).
# LittleFS (sectors 7-8) and native app slots (9+) are preserved.
#
# Full chip program (may wipe apps):
#   FLASH_MODE=full ./tools/flash.sh
#   ./tools/flash.sh --full
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build/Release}"
ELF="$BUILD_DIR/tars.elf"
CFG="$ROOT/openocd.cfg"
TCL="$ROOT/tools/openocd-flash-partition.tcl"

FW_BASE="${FW_BASE:-0x08000000}"
FW_FIRST_SECTOR="${FW_FIRST_SECTOR:-0}"
FW_LAST_SECTOR="${FW_LAST_SECTOR:-6}"
FLASH_MODE="${FLASH_MODE:-partition}"
OBJCOPY="${OBJCOPY:-arm-none-eabi-objcopy}"

usage() {
  cat <<EOF
Usage: $(basename "$0") [--full|--partition]

  --partition   Erase sectors ${FW_FIRST_SECTOR}-${FW_LAST_SECTOR} only (default)
  --full        OpenOCD program+verify (may mass-erase app partition)
  --help        Show this help

Environment:
  BUILD_DIR          CMake build dir (default: build/Release)
  FW_BASE            Firmware load address (default: 0x08000000)
  FW_FIRST_SECTOR    First sector to erase (default: 0)
  FW_LAST_SECTOR     Last sector to erase (default: 6)
  FLASH_MODE         partition|full
  OBJCOPY            arm-none-eabi-objcopy path
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --full)
      FLASH_MODE=full
      shift
      ;;
    --partition)
      FLASH_MODE=partition
      shift
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      echo "unknown option: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

if [[ ! -f "$CFG" ]]; then
  echo "missing openocd.cfg: $CFG" >&2
  exit 1
fi

if [[ ! -f "$TCL" ]]; then
  echo "missing openocd script: $TCL" >&2
  exit 1
fi

if [[ ! -f "$BUILD_DIR/build.ninja" && ! -f "$BUILD_DIR/Makefile" ]]; then
  if ! command -v cmake >/dev/null 2>&1; then
    echo "missing build dir $BUILD_DIR and cmake not found" >&2
    exit 1
  fi
  echo "==> configure (Release)"
  cmake --preset Release
fi

echo "==> build pass 1 (Release)"
if command -v cmake >/dev/null 2>&1; then
  cmake --build "$BUILD_DIR" --target tars
elif [[ -f "$BUILD_DIR/build.ninja" ]] && command -v ninja >/dev/null 2>&1; then
  (cd "$BUILD_DIR" && ninja tars)
else
  echo "need cmake or ninja in $BUILD_DIR" >&2
  exit 1
fi

if [[ ! -f "$ELF" ]]; then
  echo "missing ELF: $ELF" >&2
  exit 1
fi

if command -v "$OBJCOPY" >/dev/null 2>&1; then
  FW_BIN="$BUILD_DIR/tars.fw.bin"
  for pass in 1 2 3; do
    "$OBJCOPY" -O binary "$ELF" "$FW_BIN"
    python3 "$ROOT/tools/fw-identity-gen.py" "$FW_BIN" \
      -o "$ROOT/generated/fw/tars_fw_identity.c"
    echo "==> build pass $((pass + 1)) (embed firmware identity)"
    if command -v cmake >/dev/null 2>&1; then
      cmake --build "$BUILD_DIR" --target tars
    elif [[ -f "$BUILD_DIR/build.ninja" ]] && command -v ninja >/dev/null 2>&1; then
      (cd "$BUILD_DIR" && ninja tars)
    fi
  done
fi

if [[ ! -f "$ELF" ]]; then
  echo "missing ELF after identity pass: $ELF" >&2
  exit 1
fi

if [[ "$FLASH_MODE" == "full" ]]; then
  echo "==> flash FULL $ELF (warning: may erase LittleFS / app slots)"
  openocd -f "$CFG" \
    -c "program $ELF verify reset exit"
else
  if ! command -v "$OBJCOPY" >/dev/null 2>&1; then
    echo "missing objcopy: $OBJCOPY" >&2
    exit 1
  fi

  BIN="$(mktemp "${TMPDIR:-/tmp}/tars-firmware.XXXXXX.bin")"
  trap 'rm -f "$BIN"' EXIT

  echo "==> extract firmware binary from $ELF"
  "$OBJCOPY" -O binary "$ELF" "$BIN"

  echo "==> flash PARTITION $BIN (sectors ${FW_FIRST_SECTOR}-${FW_LAST_SECTOR}, base ${FW_BASE})"
  openocd -f "$CFG" \
    -c "set FW_BIN {$BIN}" \
    -c "set FW_BASE $FW_BASE" \
    -c "set FW_FIRST_SECTOR $FW_FIRST_SECTOR" \
    -c "set FW_LAST_SECTOR $FW_LAST_SECTOR" \
    -c "source {$TCL}"
fi

echo "==> done"
