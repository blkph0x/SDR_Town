#!/usr/bin/env python3
"""DEC-0066: unknown/cold dead grants must not park ~45s with zero VCWs."""

from pathlib import Path

root = Path(__file__).resolve().parents[2]
fsm = (root / "src" / "P25FollowStateMachine.cpp").read_text(encoding="utf-8", errors="ignore")
test = (root / "tests" / "test_p25follow.cpp").read_text(encoding="utf-8", errors="ignore")

required = {
    "unknown-grant cold no-VCW is ~8s not 45s": (
        "kUnknownGrantColdNoVcwTunedMs = 8000" in fsm
        and "kUnknownGrantColdNoVcwSilenceMs = 6000" in fsm
        and "unknownGrantColdAcquire" in fsm
        and "waitingClearGrantWithEvidence" in fsm
    ),
    "coldDeadNoVcw shortens clear grant hang": (
        "coldDeadNoVcw" in fsm
        and "kClearColdNoVcwTunedMs = 10000" in fsm
    ),
    "unit test expects fast unknown-grant return": (
        "returns quickly from unknown clear-grant with no VCWs" in test
        and "snapshot.nowMs = 10'000" in test
    ),
}

failed = [name for name, ok in required.items() if not ok]
if failed:
    raise SystemExit(
        "P25 Phase 2 DEC-0066 dead-grant timeout regression failed: "
        + ", ".join(failed)
    )
print("P25 Phase 2 DEC-0066 dead-grant timeout regression: PASS")
