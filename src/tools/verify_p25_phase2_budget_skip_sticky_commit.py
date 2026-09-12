#!/usr/bin/env python3
"""DEC-0054: restore cold full-commit; sticky cheap only; no CQPSK headroom."""
from __future__ import annotations

import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
DECODER = (ROOT / "src" / "P25LiveDecoder.cpp").read_text(encoding="utf-8", errors="replace").replace("\r\n", "\n")
TIMING = (ROOT / "include" / "P25VoiceTiming.h").read_text(encoding="utf-8", errors="replace")


def main() -> int:
    fail = False
    if "leave commit headroom on live Phase-2" in DECODER:
        print("FAIL: DEC-0052 CQPSK half-budget headroom must be removed (081416)")
        fail = True
    if "skip-commit sticky-sustain" in DECODER:
        print("FAIL: sticky skip-commit must stay removed")
        fail = True
    if "full-commit cold-acquire budget-rearm" not in DECODER:
        print("FAIL: DEC-0054 cold full-commit re-arm missing")
        fail = True
    if "cheap-commit sticky-sustain budget-rearm" not in DECODER:
        print("FAIL: DEC-0054 sticky cheap-commit re-arm missing")
        fail = True
    if "kP25LiveColdCommitAllowanceMs" not in TIMING:
        print("FAIL: cold commit allowance missing")
        fail = True
    if "kP25LiveStickyCheapCommitAllowanceMs" not in TIMING:
        print("FAIL: sticky cheap allowance missing")
        fail = True
    # Cold must not forceCheap via cold-acquire cheap-commit tag
    if "cheap-commit cold-acquire" in DECODER:
        print("FAIL: cold must not forceCheap (081416 SILENT scraps)")
        fail = True
    if "armRealtimeDecodeBudget(kP25LiveColdCommitAllowanceMs)" not in DECODER:
        print("FAIL: cold re-arm must use ColdCommitAllowance")
        fail = True
    if fail:
        return 1
    print("PASS: DEC-0054 cold full-commit + sticky cheap; no CQPSK headroom")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
