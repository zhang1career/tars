#!/usr/bin/env python3
"""Query generated/config/board.json by dotted path (e.g. pwm0.freq)."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def default_board_json() -> Path:
    return repo_root() / "generated/config/board.json"


def load_doc(path: Path) -> dict[str, Any]:
    if not path.is_file():
        print(
            f"error: {path} not found — run a build or "
            f"python3 tools/config/config-gen.py ...",
            file=sys.stderr,
        )
        raise SystemExit(1)
    return json.loads(path.read_text(encoding="utf-8"))


def resolve_path(doc: dict[str, Any], dotted: str) -> Any:
    """Resolve dotted paths; supports pwm0.freq shorthand and freq -> freq_hz."""
    parts = dotted.split(".")
    if not parts or not parts[0]:
        return None

    pwm = doc.get("pwm")
    if isinstance(pwm, dict) and parts[0] in pwm:
        cur: Any = pwm[parts[0]]
        for part in parts[1:]:
            key = "freq_hz" if part in {"freq", "freq_hz"} else part
            if not isinstance(cur, dict) or key not in cur:
                return None
            cur = cur[key]
        return cur

    timers = doc.get("timers")
    if isinstance(timers, dict) and parts[0] in timers:
        cur = timers[parts[0]]
        for part in parts[1:]:
            key = part
            if part in {"freq", "freq_hz"}:
                if isinstance(cur, dict) and "freq_hz" in cur:
                    key = "freq_hz"
                elif isinstance(cur, dict) and "default_freq_hz" in cur:
                    key = "default_freq_hz"
            if not isinstance(cur, dict) or key not in cur:
                return None
            cur = cur[key]
        return cur

    cur = doc
    for part in parts:
        key = part
        if isinstance(cur, dict) and part == "freq" and "freq_hz" in cur:
            key = "freq_hz"
        if not isinstance(cur, dict) or key not in cur:
            return None
        cur = cur[key]
    return cur


def format_value(value: Any) -> str:
    if isinstance(value, (dict, list)):
        return json.dumps(value, indent=2, sort_keys=True)
    if isinstance(value, bool):
        return "1" if value else "0"
    return str(value)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "path",
        nargs="?",
        help="Dotted query (pwm0.freq, tim1, foc.FOC_PARAM_FPWM_HZ)",
    )
    parser.add_argument(
        "--json",
        action="store_true",
        help="Dump the full board.json document",
    )
    parser.add_argument(
        "--list",
        metavar="SECTION",
        help="List keys in a top-level section (pwm, gpio, timers, foc, ...)",
    )
    parser.add_argument(
        "--board-json",
        type=Path,
        default=None,
        help="Override path to board.json",
    )
    args = parser.parse_args()

    board_json = args.board_json or default_board_json()
    doc = load_doc(board_json)

    if args.json:
        print(json.dumps(doc, indent=2, sort_keys=False))
        return 0

    if args.list:
        section = doc.get(args.list)
        if not isinstance(section, dict):
            print(f"error: unknown section {args.list!r}", file=sys.stderr)
            return 1
        for key in sorted(section.keys()):
            print(key)
        return 0

    if not args.path:
        parser.print_help()
        return 1

    value = resolve_path(doc, args.path)
    if value is None:
        print(f"error: path not found: {args.path}", file=sys.stderr)
        return 1

    print(format_value(value))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
