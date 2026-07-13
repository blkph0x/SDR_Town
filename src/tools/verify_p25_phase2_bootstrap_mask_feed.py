#!/usr/bin/env python3
"""Static checks that bootstrap mask evidence does not release Phase-2 audio."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MAIN = ROOT / "src" / "main.cpp"
FOLLOW_H = ROOT / "include" / "P25FollowStateMachine.h"

main = MAIN.read_text(encoding="utf-8", errors="replace")
follow_h = FOLLOW_H.read_text(encoding="utf-8", errors="replace")

checks = [
    ("bootstrapped helper", "p25Phase2BootstrappedMaskTargetVoiceEvidence"),
    ("bootstrap late-entry evidence", "bootstrappedMaskEvidence"),
    ("speaker proof uses established clear state", "establishedClearCall"),
    ("grant mask blocks early flip", "grantMaskParamsKnown"),
    ("explicit clear grant only probes/queues", "const bool grantMayProbeVoice = grantClearTrusted || grantUnknownProbe;"),
]

for label, needle in checks:
    assert needle in main or needle in follow_h, f"missing {label}: {needle}"

assert "bootstrappedMaskImmediateFeed" not in main
assert "clearGrantTargetReleaseAllowed" not in main
assert "p25Phase2ExplicitClearGrantVoiceReleaseEvidence" not in main
assert "explicit-clear-grant-target-release" not in main
assert "!snapshot.grantMaskParamsKnown &&" in (ROOT / "src" / "P25FollowStateMachine.cpp").read_text(
    encoding="utf-8", errors="replace"
)

print("PASS bootstrap mask and explicit clear grants remain queue-only until traffic PTT/ESS")
