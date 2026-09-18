#!/usr/bin/env python3
"""DEC-0043: post-speak opp-dominant sticky invalidate is debounced (twin rescue reverted)."""
from pathlib import Path

root = Path(__file__).resolve().parents[2]
voice = (root / "src" / "P25VoiceDecode.cpp").read_text(encoding="utf-8", errors="replace")
decoder = (root / "src" / "P25LiveDecoder.cpp").read_text(encoding="utf-8", errors="replace")
receiver = (root / "include" / "Receiver.h").read_text(encoding="utf-8", errors="replace")

checks = {
    "receiver opp-dominant streak counter": "p25Phase2OppDominantEpochWindows" in receiver,
    "voice debounce after speak": (
        "p25Phase2OppDominantEpochWindows" in voice
        and "OppDominantEpochWindows >= 3" in voice
        and "callHasSpoken" in voice
    ),
    "pre-speak still immediate invalidate": (
        "if (!callHasSpoken)" in voice
        and "invalidatePhase2StickyMaskEpoch()" in voice.split("if (!callHasSpoken)", 1)[1][:400]
    ),
    "twin rescue reverted": "preferPhase2LockTwinForPreferredSlot" not in decoder,
    "revert note present": "twin rescue" in decoder.lower() and "024000" in decoder,
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit(
        "P25 Phase 2 opp-dominant sticky debounce regression failed: "
        + ", ".join(failed)
    )
print("P25 Phase 2 opp-dominant sticky debounce regression: PASS")
