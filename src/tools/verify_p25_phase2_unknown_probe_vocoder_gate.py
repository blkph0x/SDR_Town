#!/usr/bin/env python3
"""Static regression for Phase 2 unknown-grant vocoder gate.

Unknown Phase 2 grants may be followed and queued into a bounded pending
queue. Plain AMBE plausibility must not mark the call permanently clear.
Target-slot PTT/ESS or target MAC/ESS may release audio; explicit clear control
grants may release after target-slot hard voice is XOR-masked and diagnostic
AMBE validation succeeds. Unknown-grant field probing is a default-off lab
diagnostic: it can be enabled for capture analysis, but normal GUI/CLI audio
release remains fail-closed until MAC/ESS/PTT proves clear. Encrypted evidence
still wins and mutes.
"""
from pathlib import Path
src = Path(__file__).resolve().parents[1] / "main.cpp"
text = src.read_text(encoding="utf-8", errors="replace")
required = [
    "const bool grantUnknownProbe =",
    "const bool grantMayReleaseVoice =",
    "const bool voiceReleaseTrusted =",
    "const bool grantMayProbeVoice = grantClearTrusted || grantUnknownProbe;",
    "kP25Phase2AllowUnknownGrantFieldAudioProbe = false",
    "args.fieldAudioProbe = true;",
    "args.fieldAudioProbe = false;",
    "probe|noprobe",
    "establishedClearCall",
    "if (!burst.essKnown && !burst.sessionAudioRelease) {",
    "queueUnknownAmbe",
    "applyP25Phase2SecurityAudioGate",
    "P25P2CallAudioKey",
    "sdrtrunkLateEntryVoiceRelease",
    "p25Phase2ExplicitClearGrantVoiceReleaseEvidence",
    "p25Phase2UnknownGrantProbeVoiceReleaseEvidence",
    "p25Phase2StrictLateEntryTargetVoiceEvidence",
    "const bool strictLateEntryEvidence =",
    "p25Phase2StrictLateEntryTargetVoiceEvidence(rx, out)",
    "releasePendingRawVoiceFromValidatedUnknownGrantProbe",
    "unknown-grant-probe-validated-release",
    "out.phase2DiagnosticAmbeProbeAccepted >= kP25Phase2UnknownGrantAudioProbeMinFrames",
    "out.decodedFrames >= static_cast<long long>(kP25Phase2ExplicitClearGrantProbeMinFrames)",
    "releasePendingRawVoiceFromValidatedExplicitClearGrant",
    "const bool validatedExplicitClearGrant =",
    "const bool decodedExplicitClearPcm =",
    "out.decodedFrames >= static_cast<long long>(kP25Phase2ExplicitClearGrantProbeMinFrames)",
    "!out.phase2TargetEssEncrypted",
    "late-entry-audio-probe-diagnostic-only",
    "Unknown-security AMBE is queued first",
]
missing = [r for r in required if r not in text]
if missing:
    print("P25 Phase 2 unknown-grant vocoder gate regression: FAIL")
    for item in missing:
        print("missing:", item)
    raise SystemExit(1)
forbidden = "const bool voiceReleaseTrusted = burst.sessionAudioRelease || grantClearTrusted || grantUnknownProbe;"
if forbidden in text:
    print("P25 Phase 2 unknown-grant vocoder gate regression: FAIL")
    print("unknown probe still releases voice")
    raise SystemExit(1)
forbidden_fragments = [
    "explicit-clear-grant-target-release",
    "fieldProbeClear ||",
    "voiceReleaseTrusted || burstSdrtrunkLateEntryVoiceRelease || lateEntryAudioProbeAllowed",
    "late-entry-field-audio-probe-release",
    "(rx.p25VoiceClearKnown && !rx.p25VoiceEncrypted) ||",
    "const bool immediateAmbeDecodeAllowed =\n            grantClearTrusted",
    "clearGrantTargetReleaseAllowed",
    "bootstrappedMaskImmediateFeed",
    "rx.p25VoiceClearKnown = true;\n        rx.p25VoiceEncrypted = false;",
]
for fragment in forbidden_fragments:
    if fragment in text:
        print("P25 Phase 2 unknown-grant vocoder gate regression: FAIL")
        print("unsafe unknown-probe release marker remains:", fragment)
        raise SystemExit(1)
release_block = text.split("auto releasePendingRawVoiceFromValidatedExplicitClearGrant = [&]() {", 1)
if len(release_block) != 2 or "p25Phase2StrongVoiceTimeslotPcm(out)" in release_block[1].split("};", 1)[0]:
    print("P25 Phase 2 unknown-grant vocoder gate regression: FAIL")
    print("explicit clear release must not require a full six-burst strong PCM window")
    raise SystemExit(1)
unknown_block = text.split("static bool p25Phase2UnknownGrantProbeVoiceReleaseEvidence", 1)
if len(unknown_block) != 2:
    print("P25 Phase 2 unknown-grant vocoder gate regression: FAIL")
    print("missing unknown-grant release evidence helper")
    raise SystemExit(1)
unknown_body = unknown_block[1].split("static bool p25Phase2WindowHasFreshTargetEvidence", 1)[0]
if "p25Phase2BootstrappedMaskTargetVoiceEvidence" in unknown_body:
    print("P25 Phase 2 unknown-grant vocoder gate regression: FAIL")
    print("unknown-grant speaker release still allows one-burst bootstrap evidence")
    raise SystemExit(1)
print("P25 Phase 2 unknown-grant vocoder gate regression: PASS")
