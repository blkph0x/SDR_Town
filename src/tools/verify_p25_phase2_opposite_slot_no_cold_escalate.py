#!/usr/bin/env python3
"""Guard: DEC-0026 opposite-slot evidence retained under DEC-0028 supersession."""
from pathlib import Path

root = Path(__file__).resolve().parents[2]
from p25_orchestration_sources import orchestration_source_text
main = orchestration_source_text()
# DEC-0028 supersedes the emptyStreakReacq predicate; keep capture citations.
checks = {
    "cites 110146": "20260908_110146" in main,
    "cites DEC-0026 history or 115603 DEC-0028": (
        "DEC-0026" in main and "20260908_115603" in main
    ),
    "no post-emit emptyStreakReacq": "emptyStreakReacq" not in main,
}
failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit("DEC-0026 regression failed: " + ", ".join(failed))
print("DEC-0026 opposite-slot no-cold-escalate regression: PASS (superseded by DEC-0028)")
