#!/usr/bin/env python3
"""DEC-0036: rolling decode must not hold+purge when VCWs were not queued."""
from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
from p25_orchestration_sources import orchestration_source_text
# DEC-0040: search all orchestration TUs
MAIN_TEXT = orchestration_source_text()


def main() -> int:
    text = MAIN_TEXT
    m = re.search(
        r"static bool p25Phase2RollingDecodeWindowConsumed\(.*?\n\}",
        text,
        re.S,
    )
    if not m:
        print("FAIL: p25Phase2RollingDecodeWindowConsumed not found")
        return 1
    body = m.group(0)
    if "DEC-0036" not in body:
        print("FAIL: DEC-0036 marker missing")
        return 1
    # Final return must be true (advance), not the old hold-false.
    tail = body.strip().splitlines()[-3:]
    if not any("return true;" in line for line in tail):
        print("FAIL: function must end by advancing (return true)")
        return 1
    if re.search(r"return false;\s*\}\s*$", body):
        print("FAIL: still returns false (hold) at end")
        return 1
    print("PASS: rolling cursor advances when selected VCWs were not queued/fed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
