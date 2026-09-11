#!/usr/bin/env python3
"""Guard: DEC-0029 clear-trusted follow hold + structure exits coldAcquire."""
from pathlib import Path

root = Path(__file__).resolve().parents[2]
from p25_orchestration_sources import orchestration_source_text
main = orchestration_source_text()
follow = (root / "src" / "P25FollowStateMachine.cpp").read_text(
    encoding="utf-8", errors="replace"
)
tests = (root / "tests" / "test_p25follow.cpp").read_text(
    encoding="utf-8", errors="replace"
)

checks = {
    "cites 053448 in follow SM": "20260909_053448" in follow,
    "clearTrustedHold in follow SM": "clearTrustedHold" in follow,
    "15s clear-trusted activity silence": "kClearTrustedActivitySilenceMs = 15000" in follow,
    "clearTrusted extends speaker grace": (
        "clearTrustedHold);" in follow.replace(" ", "")
        or "clearTrustedHold)" in follow
    ),
    "sustainStructureAcquired cold exit": "sustainStructureAcquired" in main,
    "cites 053448 in cold acquire": "20260909_053448" in main,
    "structure bar sf+mask >=4": (
        "peakPhase2MaskedBursts >= 4" in main
        and "peakPhase2SuperframeBursts >= 4" in main
    ),
    "sameCall preserves traffic clearKnown": (
        "sameCall &&" in main
        and "rx.p25VoiceClearKnown &&" in main
        and "mirrors same-call in-place follow" in main
    ),
    "unit test for clear-trusted hold": (
        "holds clear-trusted call across empty-eye gaps after emit" in tests
    ),
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit("DEC-0029 regression failed: " + ", ".join(failed))
print("DEC-0029 clear-trusted hold + structure cold-exit regression: PASS")
