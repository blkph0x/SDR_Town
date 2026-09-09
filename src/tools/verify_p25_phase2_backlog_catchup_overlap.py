#!/usr/bin/env python3
"""Guard: DEC-0024 backlog catch-up keeps DEC-0009 280 ms overlap (not 40 ms)."""
from pathlib import Path

root = Path(__file__).resolve().parents[2]
from p25_orchestration_sources import orchestration_source_text
main = orchestration_source_text()
marker = "DEC-0024 / capture 20260908_101644"
if marker not in main:
    raise SystemExit("DEC-0024 regression failed: missing catch-up comment")
checks = {
    "catch-up overlap 280ms": (
        "kP25Phase2VoiceDecodeBacklogCatchUpOverlapSeconds = 0.280" in main
    ),
    "not still 40ms": (
        "kP25Phase2VoiceDecodeBacklogCatchUpOverlapSeconds = 0.040" not in main
    ),
    "cites 101644": "20260908_101644" in main,
    "plan still uses catch-up overlap constant": (
        "plan.overlapSeconds = kP25Phase2VoiceDecodeBacklogCatchUpOverlapSeconds"
        in main
    ),
}
failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit("DEC-0024 regression failed: " + ", ".join(failed))
print("DEC-0024 backlog catch-up overlap regression: PASS")
