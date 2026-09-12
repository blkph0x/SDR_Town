#!/usr/bin/env python3
"""DEC-0052: sticky sustain must not force unbounded commit after budget."""
from __future__ import annotations

import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
DECODER = (ROOT / "src" / "P25LiveDecoder.cpp").read_text(encoding="utf-8", errors="replace").replace("\r\n", "\n")
VOICE = (ROOT / "src" / "MainWindowP25Voice.cpp").read_text(encoding="utf-8", errors="replace")
HEADER = (ROOT / "include" / "P25LiveDecoder.h").read_text(encoding="utf-8", errors="replace")
LOGSCAN = (ROOT / "src" / "tools" / "p25_logscan.py").read_text(encoding="utf-8", errors="replace")


def main() -> int:
    fail = False
    if "mustAnnotateCommit =" in DECODER and "phase2CqpskTrafficDemod ||" in DECODER:
        # Old hole: phase2CqpskTrafficDemod alone in mustAnnotateCommit.
        idx = DECODER.find("mustAnnotateCommit =")
        window = DECODER[idx : idx + 280]
        if "phase2CqpskTrafficDemod ||" in window:
            print("FAIL: mustAnnotateCommit still ORs phase2CqpskTrafficDemod alone")
            fail = True
    if "skip-commit sticky-sustain budget-exhausted" not in DECODER:
        print("FAIL: DEC-0052 sticky skip-commit path missing")
        fail = True
    if "cheap-commit cold-acquire budget-exhausted" not in DECODER:
        print("FAIL: DEC-0052 cheap cold commit path missing")
        fail = True
    if "leave commit headroom on live Phase-2" not in DECODER:
        print("FAIL: CQPSK commit headroom reserve missing")
        fail = True
    if "m_phase2ForceCheapRealtimeCommit" not in HEADER:
        print("FAIL: force-cheap flag missing from header")
        fail = True
    if "P25 budget trip:" not in VOICE:
        print("FAIL: budget trip must log into p25_log")
        fail = True
    if '"budget_trip"' not in LOGSCAN:
        print("FAIL: logscan budget_trip signature missing")
        fail = True
    if fail:
        return 1
    print("PASS: DEC-0052 sticky skip-commit + CQPSK headroom + budget trip log")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
