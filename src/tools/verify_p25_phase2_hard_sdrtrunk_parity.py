#!/usr/bin/env python3
from pathlib import Path
root = Path(__file__).resolve().parents[1]
main = (root / 'main.cpp').read_text()
dec = (root / 'P25LiveDecoder.cpp').read_text()
hdr = (root.parent / 'include' / 'P25LiveDecoder.h').read_text()
assert 'm_phase2SlotEss' in hdr
assert 'm_phase2SlotSessionMacCrcSeen' in hdr
assert 'm_phase2SlotFirst4vSlot' in hdr
assert 'm_phase2SlotEssHypotheses' in hdr
assert 'm_phase2SlotEss[ts]' in dec
assert 'm_phase2SlotSessionMacCrcSeen[ts]' in dec
assert 'm_phase2SlotFirst4vSlot[ts]' in dec
assert 'const bool grantMayReleaseVoice =' in main
assert 'sdrtrunkLateEntryVoiceRelease' in main
assert 'targetVoiceForLateEntryProbe' in main
assert 'macEvidenceForLateEntryProbe' in main
assert 'superframeMaskEvidenceForLateEntryProbe' in main
assert 'kP25Phase2AllowUnknownGrantFieldAudioProbe = true' in main
assert 'burst.sessionAudioRelease || grantMayReleaseVoice' in main
assert 'raw scrambled AMBE must' in main
assert 'if (!burst.xorMaskApplied)' in main
assert 'acceptedReleaseVoice' in main
assert 'late-entry-audio-probe-diagnostic-only' in main
assert 'rx.p25VoiceClearKnown = false;' in main
assert 'rx.p25VoiceEncrypted = true;' in main
assert 'fieldProbeClear ||' not in main
assert 'voiceReleaseTrusted || burstSdrtrunkLateEntryVoiceRelease || lateEntryAudioProbeAllowed' not in main
assert 'late-entry-field-audio-probe-release' not in main
print('P25 Phase 2 hard sdrtrunk parity regression: PASS')
