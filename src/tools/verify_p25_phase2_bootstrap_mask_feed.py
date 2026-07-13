#!/usr/bin/env python3
"""Static checks for bootstrap mask immediate AMBE feed (clear grants only)."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MAIN = ROOT / "src" / "main.cpp"
FOLLOW_H = ROOT / "include" / "P25FollowStateMachine.h"

main = MAIN.read_text(encoding="utf-8", errors="replace")
follow_h = FOLLOW_H.read_text(encoding="utf-8", errors="replace")

checks = [
    ("bootstrapped helper", "p25Phase2BootstrappedMaskTargetVoiceEvidence"),
    ("bootstrap immediate feed", "bootstrappedMaskImmediateFeed"),
    ("bootstrap late-entry evidence", "bootstrappedMaskEvidence"),
    ("bootstrap requires clear grant", "grantClearTrusted &&"),
    ("grant mask blocks early flip", "grantMaskParamsKnown"),
]

for label, needle in checks:
    assert needle in main or needle in follow_h, f"missing {label}: {needle}"

assert "bootstrappedMaskImmediateFeed" in main
assert "bootstrappedMaskImmediateFeed =\n            grantClearTrusted" in main or \
       "bootstrappedMaskImmediateFeed =\n            grantClearTrusted &&" in main
assert "!snapshot.grantMaskParamsKnown &&" in (ROOT / "src" / "P25FollowStateMachine.cpp").read_text(
    encoding="utf-8", errors="replace"
)

print("PASS bootstrap mask feed patch present")
