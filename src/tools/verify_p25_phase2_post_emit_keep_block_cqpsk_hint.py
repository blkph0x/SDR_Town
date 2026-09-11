#!/usr/bin/env python3
"""DEC-0034: post-emit empty-eye soft rehunt must not clearBlockCqpskHint."""
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
    # Locate the DEC-0032/0034 post-emit empty-eye block.
    m = re.search(
        r"p25Phase2PostEmitEmptyEyeWindows.*?p25Phase2PostEmitEmptyEyeWindows\s*=\s*0;",
        text,
        re.S,
    )
    if not m:
        print("FAIL: post-emit empty-eye window block not found")
        return 1
    block = m.group(0)
    if "clearBlockCqpskHint" in block:
        print("FAIL: post-emit empty-eye still calls clearBlockCqpskHint()")
        return 1
    if "ForceMaskEpochRehunt" not in block and "p25Phase2ForceMaskEpochRehunt" not in block:
        print("FAIL: post-emit empty-eye lost ForceMask soft rehunt")
        return 1
    # Companion-only sticky fallthrough must stay streaming-gated.
    dec = (ROOT / "src" / "P25LiveDecoder.cpp").read_text(encoding="utf-8", errors="replace")
    cm = re.search(
        r"companionOnlySticky\s*=\s*(.*?);",
        dec,
        re.S,
    )
    if not cm or "enableStreamingChannelDdc" not in cm.group(1):
        print("FAIL: companionOnlySticky is not gated to enableStreamingChannelDdc")
        return 1
    print("PASS: post-emit keeps block CQPSK hint; companion-only streaming-gated")
    return 0


if __name__ == "__main__":
    sys.exit(main())
