#!/usr/bin/env bash
# Flash tars.elf via OpenOCD + ST-LINK (same flow as CLion "OpenOCD Download & Run.stlink").
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/cmake-build-debug}"
ELF="$BUILD_DIR/tars.elf"
CFG="$ROOT/openocd.cfg"

if [[ ! -f "$CFG" ]]; then
  echo "missing openocd.cfg: $CFG" >&2
  exit 1
fi

echo "==> build (Debug)"
cmake --build "$BUILD_DIR" --target tars

if [[ ! -f "$ELF" ]]; then
  echo "missing ELF: $ELF" >&2
  exit 1
fi

echo "==> flash $ELF"
openocd -f "$CFG" \
  -c "program $ELF verify reset exit"

echo "==> done"
