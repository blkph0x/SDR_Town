#!/usr/bin/env python3
"""DEC-0051: cooperative mid-decode realtime budget abort (not post-hoc wall)."""
from __future__ import annotations

import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
DECODER = (ROOT / "src" / "P25LiveDecoder.cpp").read_text(encoding="utf-8", errors="replace").replace("\r\n", "\n")
HEADER = (ROOT / "include" / "P25LiveDecoder.h").read_text(encoding="utf-8", errors="replace")
TEST = (ROOT / "tests" / "test_p25live.cpp").read_text(encoding="utf-8", errors="replace")
TIMING = (ROOT / "include" / "P25VoiceTiming.h").read_text(encoding="utf-8", errors="replace")


def main() -> int:
    if "armRealtimeDecodeBudget" not in HEADER:
        print("FAIL: armRealtimeDecodeBudget missing from P25LiveDecoder.h")
        return 1
    if "realtimeDecodeBudgetExceeded" not in HEADER:
        print("FAIL: realtimeDecodeBudgetExceeded missing from header")
        return 1
    if "RealtimeBudgetScope" not in DECODER:
        print("FAIL: processIq RealtimeBudgetScope missing")
        return 1
    if "DEC-0051: abort sync scan" not in DECODER:
        print("FAIL: sync-scan cooperative abort missing")
        return 1
    if "DEC-0051: probe path already has sync telemetry" not in DECODER:
        print("FAIL: probe-path early abort missing")
        return 1
    if DECODER.count("realtimeDecodeBudgetExceeded()") < 8:
        print("FAIL: expected >=8 realtimeDecodeBudgetExceeded call sites")
        return 1
    if "ms < 350" not in TEST or "[p25][cqpsk][budget]" not in TEST:
        print("FAIL: Catch budget ceiling must be tightened to <350 (DEC-0051)")
        return 1
    if "[p25][budget][dec0051]" not in TEST:
        print("FAIL: DEC-0051 Catch tag missing")
        return 1
    if "kP25LiveHealthySustainWallMs = kP25VoiceWorkerMaxDecodeWallMs" not in TIMING:
        print("FAIL: healthy wall must stay global (DEC-0046) even with cooperative abort")
        return 1
    print("PASS: DEC-0051 cooperative mid-decode budget abort + Catch ceiling")
    return 0


if __name__ == "__main__":
    sys.exit(main())
