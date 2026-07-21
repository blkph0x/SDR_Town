#!/usr/bin/env python3
"""Static regression for the Phase-2 unknown-grant vocoder gate.

Unknown Phase-2 grants may be followed, queued, and decoded through a throwaway
AMBE probe for diagnostics. Probe plausibility must never mark a call clear or
release speaker audio. Explicit clear control grants may select/follow a traffic
slot, but queued pending AMBE still waits for target-slot PTT/session, ESS, or
retained late-entry proof before speaker release. Encrypted evidence still wins
and mutes.
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
    "keepUnknownGrantProbeDiagnosticOnly",
    "unknown-grant-probe-diagnostic-only",
    "releasePendingRawVoiceFromExplicitClearTrafficProof",
    "explicit-clear-grant-traffic-clear-release",
    "const bool targetTrafficClearEvidence =",
    "p25Phase2TargetHardClearEvidence(out)",
    "p25Phase2TargetPttSessionClear",
    "p25Phase2SdrtrunkLateEntryVoiceReleaseEvidence(rx, out)",
    "late-entry-audio-probe-diagnostic-only",
    "Unknown-security AMBE is queued first",
]
missing = [r for r in required if r not in text]
if missing:
    print("P25 Phase 2 unknown-grant vocoder gate regression: FAIL")
    for item in missing:
        print("missing:", item)
    raise SystemExit(1)

forbidden_fragments = [
    "explicit-clear-grant-target-release",
    "explicit-clear-grant-validated-release",
    "unknown-grant-probe-validated-release",
    "releasePendingRawVoiceFromValidatedExplicitClearGrant",
    "releasePendingRawVoiceFromValidatedUnknownGrantProbe",
    "fieldProbeClear ||",
    "voiceReleaseTrusted || burstSdrtrunkLateEntryVoiceRelease || lateEntryAudioProbeAllowed",
    "late-entry-field-audio-probe-release",
    "(rx.p25VoiceClearKnown && !rx.p25VoiceEncrypted) ||",
    "const bool immediateAmbeDecodeAllowed =\n            grantClearTrusted",
    "clearGrantTargetReleaseAllowed",
    "bootstrappedMaskImmediateFeed",
    "validatedExplicitClearGrantSeed",
    "validatedControlClearSeed",
    "boundedProbeAccepted",
    "decodedExplicitClearPcm",
    "p25Phase2StrictLateEntryTargetVoiceEvidence",
    "rx.p25VoiceClearKnown = true;\n        rx.p25VoiceEncrypted = false;",
]
for fragment in forbidden_fragments:
    if fragment in text:
        print("P25 Phase 2 unknown-grant vocoder gate regression: FAIL")
        print("unsafe unknown/probe release marker remains:", fragment)
        raise SystemExit(1)

release_block = text.split("auto releasePendingRawVoiceFromExplicitClearTrafficProof = [&]() {", 1)
if len(release_block) != 2 or "p25Phase2StrongVoiceTimeslotPcm(out)" in release_block[1].split("};", 1)[0]:
    print("P25 Phase 2 unknown-grant vocoder gate regression: FAIL")
    print("explicit clear release must not use a PCM/probe-quality shortcut")
    raise SystemExit(1)

explicit_helper = text.split("static bool p25Phase2ExplicitClearGrantVoiceReleaseEvidence", 1)
if len(explicit_helper) != 2:
    print("P25 Phase 2 unknown-grant vocoder gate regression: FAIL")
    print("missing explicit-clear release evidence helper")
    raise SystemExit(1)
explicit_body = explicit_helper[1].split("static bool p25Phase2UnknownGrantProbeVoiceReleaseEvidence", 1)[0]
if "targetTrafficClearEvidence;" not in explicit_body:
    print("P25 Phase 2 unknown-grant vocoder gate regression: FAIL")
    print("explicit clear pending-drain helper must end on target traffic proof")
    raise SystemExit(1)
if ("p25Phase2TargetHardClearEvidence(out)" not in explicit_body or
        "p25Phase2SdrtrunkLateEntryVoiceReleaseEvidence(rx, out)" not in explicit_body or
        "out.phase2TargetSessionAudioRelease ||" in explicit_body):
    print("P25 Phase 2 unknown-grant vocoder gate regression: FAIL")
    print("explicit clear release must be target-slot PTT/ESS/late-entry keyed")
    raise SystemExit(1)
if ("phase2DiagnosticAmbeProbeAccepted" in explicit_body or
        "phase2CurrentProbePcmUsable" in explicit_body or
        "p25AudioSamplesLookSafe" in explicit_body):
    print("P25 Phase 2 unknown-grant vocoder gate regression: FAIL")
    print("explicit clear release still depends on diagnostic AMBE/PCM quality")
    raise SystemExit(1)

if ("out.phase2OppositeVoiceCodewords > 0 &&\n            !targetTrafficClearEvidence &&" not in text or
        "!explicitGrantTargetSlotSelected" not in text):
    print("P25 Phase 2 unknown-grant vocoder gate regression: FAIL")
    print("explicit clear probe must block ambiguous dual-slot voice but allow a selected target slot")
    raise SystemExit(1)

unknown_block = text.split("static bool p25Phase2UnknownGrantProbeVoiceReleaseEvidence", 1)
if len(unknown_block) != 2:
    print("P25 Phase 2 unknown-grant vocoder gate regression: FAIL")
    print("missing unknown-grant release evidence helper")
    raise SystemExit(1)
unknown_body = unknown_block[1].split("static bool p25Phase2WindowHasFreshTargetEvidence", 1)[0]
if "return false;" not in unknown_body:
    print("P25 Phase 2 unknown-grant vocoder gate regression: FAIL")
    print("unknown-grant probe helper must be diagnostic-only")
    raise SystemExit(1)
if "p25Phase2BootstrappedMaskTargetVoiceEvidence" in unknown_body:
    print("P25 Phase 2 unknown-grant vocoder gate regression: FAIL")
    print("unknown-grant speaker release still allows one-burst bootstrap evidence")
    raise SystemExit(1)

print("P25 Phase 2 unknown-grant vocoder gate regression: PASS")
