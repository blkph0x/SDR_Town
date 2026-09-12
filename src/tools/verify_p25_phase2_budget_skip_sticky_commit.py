#!/usr/bin/env python3
"""DEC-0053: sticky+budgetGone must cheap-commit (not skip-commit)."""
from __future__ import annotations

import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
DECODER = (ROOT / "src" / "P25LiveDecoder.cpp").read_text(encoding="utf-8", errors="replace").replace("\r\n", "\n")
VOICE = (ROOT / "src" / "MainWindowP25Voice.cpp").read_text(encoding="utf-8", errors="replace")
TIMING = (ROOT / "include" / "P25VoiceTiming.h").read_text(encoding="utf-8", errors="replace")
HEADER = (ROOT / "include" / "P25LiveDecoder.h").read_text(encoding="utf-8", errors="replace")


def main() -> int:
    fail = False
    if "skip-commit sticky-sustain" in DECODER:
        print("FAIL: DEC-0052 skip-commit sticky path must be removed (064509 silence)")
        fail = True
    if "cheap-commit sticky-sustain budget-exhausted" not in DECODER:
        print("FAIL: DEC-0053 sticky cheap-commit path missing")
        fail = True
    if "kP25LiveCheapCommitAllowanceMs" not in TIMING:
        print("FAIL: cheap-commit allowance constant missing")
        fail = True
    if "armRealtimeDecodeBudget(kP25LiveCheapCommitAllowanceMs)" not in DECODER:
        print("FAIL: must re-arm cheap-commit allowance after CQPSK deadline")
        fail = True
    if "m_phase2ForceCheapRealtimeCommit" not in HEADER:
        print("FAIL: force-cheap flag missing")
        fail = True
    if "leave commit headroom on live Phase-2" not in DECODER:
        print("FAIL: CQPSK headroom reserve missing")
        fail = True
    if "cheap-commit" not in VOICE:
        print("FAIL: p25_log budget logger must prefer cheap-commit tags")
        fail = True
    if fail:
        return 1
    print("PASS: DEC-0053 sticky cheap-commit (no skip) + allowance re-arm")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
