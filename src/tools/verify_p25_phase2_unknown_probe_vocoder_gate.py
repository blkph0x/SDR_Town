#!/usr/bin/env python3
"""Static regression for Phase 2 unknown-grant vocoder gate.

Unknown Phase 2 grants may be followed and queued into a bounded pending
queue. Unknown AMBE plausibility must not feed the live vocoder, mark the call
trusted-clear, or emit speaker audio. Target-slot PTT/ESS or target MAC/ESS may
release audio; explicit clear grants may follow and queue but do not open the
Phase-2 speaker by themselves.
"""
from pathlib import Path
src = Path(__file__).resolve().parents[1] / "main.cpp"
text = src.read_text(encoding="utf-8", errors="replace")
required = [
    "const bool grantUnknownProbe =",
    "const bool grantMayReleaseVoice =",
    "const bool voiceReleaseTrusted = burst.sessionAudioRelease || grantMayReleaseVoice;",
    "explicit clear control grant can follow and queue target-slot voice",
    "establishedClearCall",
    "if (!burst.essKnown && !burst.sessionAudioRelease) {",
    "queueUnknownAmbe",
    "applyP25Phase2SecurityAudioGate",
    "P25P2CallAudioKey",
    "sdrtrunkLateEntryVoiceRelease",
    "!out.phase2TargetEssEncrypted",
    "late-entry-audio-probe-diagnostic-only",
    "raw scrambled AMBE must",
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
    "fieldProbeClear ||",
    "voiceReleaseTrusted || burstSdrtrunkLateEntryVoiceRelease || lateEntryAudioProbeAllowed",
    "late-entry-field-audio-probe-release",
    "(rx.p25VoiceClearKnown && !rx.p25VoiceEncrypted) ||",
    "const bool immediateAmbeDecodeAllowed =\n            grantClearTrusted",
    "clearGrantTargetReleaseAllowed",
    "bootstrappedMaskImmediateFeed",
]
for fragment in forbidden_fragments:
    if fragment in text:
        print("P25 Phase 2 unknown-grant vocoder gate regression: FAIL")
        print("unsafe unknown-probe release marker remains:", fragment)
        raise SystemExit(1)
print("P25 Phase 2 unknown-grant vocoder gate regression: PASS")
