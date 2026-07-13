#!/usr/bin/env python3
"""Regression guard for Phase 2 GUI voice scheduler/worker observability."""

from pathlib import Path

root = Path(__file__).resolve().parents[1]
main = (root / "main.cpp").read_text(encoding="utf-8", errors="ignore")

required = {
    "scheduler log helper": "P25 voice scheduler:" in main,
    "scheduler submitted": "P25 voice scheduler: %1 %2" in main and "submitted" in main,
    "worker start log": "P25 DSP VOICE WORKER START" in main,
    "worker stale log": "P25 voice worker stale/drop" in main,
    "stale reason carried": "std::string staleReason" in main,
    "queue snapshot": "P25VoiceWorkerQueueSnapshot" in main and "p25VoiceWorkerQueueSnapshot()" in main,
    "no fresh IQ reason": "waiting-fresh-iq" in main,
    "stream unavailable reason": "stream-not-ready" in main,
    "no spectrum fallback reason": "no-spectrum-or-fallback" in main,
    "empty IQ after pull reason": "empty-iq-after-pull" in main,
    "worker busy reason": "worker-busy" in main,
}

missing = [name for name, ok in required.items() if not ok]
if missing:
    raise SystemExit("P25 Phase 2 voice scheduler observability regression FAILED: " + ", ".join(missing))

print("P25 Phase 2 voice scheduler observability regression: PASS")
