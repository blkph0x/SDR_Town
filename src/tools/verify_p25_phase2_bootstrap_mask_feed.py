#!/usr/bin/env python3
"""Static checks that bootstrap mask/probe evidence alone does not release Phase-2 audio."""

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
    ("explicit clear grant probes/queues before validation", "const bool grantMayProbeVoice = grantClearTrusted || grantUnknownProbe;"),
    ("explicit clear release helper", "p25Phase2ExplicitClearGrantVoiceReleaseEvidence"),
    ("explicit clear release keeps target traffic proof path", "const bool targetTrafficClearEvidence ="),
    ("explicit clear release accepts target PTT/ESS only", "p25Phase2TargetHardClearEvidence(out)"),
    ("explicit clear release keeps PTT-specific helper", "p25Phase2TargetPttSessionClear"),
    ("explicit clear release accepts SDRTrunk late-entry proof", "p25Phase2SdrtrunkLateEntryVoiceReleaseEvidence(rx, out)"),
    ("explicit clear grant runs diagnostic AMBE probe", "explicitClearGrantProbeAllowed"),
    ("explicit clear release drains queued AMBE", "releasePendingRawVoiceFromExplicitClearTrafficProof"),
]

for label, needle in checks:
    assert needle in main or needle in follow_h, f"missing {label}: {needle}"

assert "bootstrappedMaskImmediateFeed" not in main
assert "clearGrantTargetReleaseAllowed" not in main
assert "explicit-clear-grant-target-release" not in main
assert "explicit-clear-grant-validated-release" not in main
assert "validatedExplicitClearGrantSeed" not in main
assert "boundedProbeAccepted" not in main
assert "!snapshot.grantMaskParamsKnown &&" in (ROOT / "src" / "P25FollowStateMachine.cpp").read_text(
    encoding="utf-8", errors="replace"
)

print("PASS bootstrap/probe evidence remains queue-only; explicit clear release stays target-slot gated")
