#!/usr/bin/env python3
"""DEC-0036 accounted-unqueued advance, tempered by DEC-0037 recoverable hold."""
from __future__ import annotations

import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
from p25_orchestration_sources import orchestration_source_text
# DEC-0040: search all orchestration TUs
MAIN_TEXT = orchestration_source_text()


def _brace_body(text: str, sig: str) -> str | None:
    start = text.find(sig)
    if start < 0:
        return None
    brace = text.find("{", start)
    if brace < 0:
        return None
    depth = 0
    for i in range(brace, len(text)):
        ch = text[i]
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return text[start : i + 1]
    return None


def main() -> int:
    text = MAIN_TEXT
    body = _brace_body(text, "static bool p25Phase2RollingDecodeWindowConsumed")
    if not body:
        print("FAIL: p25Phase2RollingDecodeWindowConsumed not found")
        return 1
    # DEC-0036: when every selected VCW is already accounted and nothing was
    # queued/fed, advance so a stale hold cannot purge newer live jobs.
    if "selectedVoiceAlreadyAccounted" not in body:
        print("FAIL: DEC-0036 accounted-unqueued advance path missing")
        return 1
    if "selectedVoiceAlreadyAccounted >= selectedVoiceNeedingDisposition" not in body:
        print("FAIL: accounted-unqueued advance condition missing")
        return 1
    accounted = body.split("selectedVoiceAlreadyAccounted >= selectedVoiceNeedingDisposition", 1)[1][:400]
    if "return true;" not in accounted:
        print("FAIL: accounted-unqueued path must advance (return true)")
        return 1
    # DEC-0037 restored hold for recoverable clear eyes after the advance path.
    if "DEC-0037" not in body or "return false;" not in body:
        print("FAIL: DEC-0037 recoverable hold missing")
        return 1
    print("PASS: rolling advances when selected VCWs were accounted unqueued; recoverable eyes hold")
    return 0


if __name__ == "__main__":
    sys.exit(main())
