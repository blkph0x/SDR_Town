#!/usr/bin/env python3
"""Guard: DEC-0030 active rolling clamp honors 4.0s (not 4194304/2s)."""
from pathlib import Path

root = Path(__file__).resolve().parents[2]
from p25_orchestration_sources import orchestration_source_text
main = orchestration_source_text()

checks = {
    "helper present": "p25Phase2VoiceRollingMaxSamples" in main,
    "cites 060036": "20260909_060036" in main,
    "hard cap 16s": "kP25Phase2VoiceDecodeRollingHardCapSeconds = 16.000" in main,
    "GUI uses helper": main.count("p25Phase2VoiceRollingMaxSamples(sr, rollingWindowSeconds)") >= 2,
    "no 4194304 clamp on live rollingWindow": (
        "clamp(sr * rollingWindowSeconds, 48000.0, 4194304.0)" not in main
    ),
    "backlog uses active rolling seconds": (
        "maxBacklogSeconds = havePhase2Eye" in main
        and "kP25Phase2VoiceDecodeActiveRollingSeconds" in main.split("maxBacklogSeconds = havePhase2Eye", 1)[1][:400]
    ),
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit("DEC-0030 regression failed: " + ", ".join(failed))
print("DEC-0030 rolling 4s clamp regression: PASS")
