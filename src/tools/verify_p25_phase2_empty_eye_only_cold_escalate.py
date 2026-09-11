#!/usr/bin/env python3
"""Guard: DEC-0027 emptyEye-only path superseded by DEC-0028 (no post-emit cold)."""
from pathlib import Path

root = Path(__file__).resolve().parents[2]
from p25_orchestration_sources import orchestration_source_text
main = orchestration_source_text()
checks = {
    "DEC-0028 present": "DEC-0028" in main and "20260908_115603" in main,
    "cites 112922 history": "20260908_112922" in main,
    "no emptyStreakReacq": "emptyStreakReacq" not in main,
}
failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit("DEC-0027 regression failed: " + ", ".join(failed))
print("DEC-0027 emptyEye-only cold-escalate regression: PASS (superseded by DEC-0028)")
