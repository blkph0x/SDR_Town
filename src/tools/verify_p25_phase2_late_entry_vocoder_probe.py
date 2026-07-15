#!/usr/bin/env python3
from pathlib import Path
root = Path(__file__).resolve().parents[1]
text = (root / 'main.cpp').read_text(errors='ignore')
needles = [
    'const bool grantMayProbeVoice = grantClearTrusted || grantUnknownProbe;',
    'const bool grantMayReleaseVoice =',
    'sdrtrunkLateEntryVoiceRelease',
    'if (!burst.xorMaskApplied)',
    'raw scrambled AMBE must',
    'if (!maskPhaseTrusted && !grantMayProbeVoice)',
    'late-entry-audio-probe-diagnostic-only',
    'targetVoiceForLateEntryProbe',
    'macEvidenceForLateEntryProbe',
    'superframeMaskEvidenceForLateEntryProbe',
    'kP25Phase2AllowUnknownGrantFieldAudioProbe = false',
    'args.fieldAudioProbe = true;',
    'args.fieldAudioProbe = false;',
    'late-entry-audio-probe-diagnostic-only',
]
missing = [n for n in needles if n not in text]
if missing:
    raise SystemExit('missing late-entry vocoder probe markers: ' + ', '.join(missing))
forbidden = [
    'fieldProbeClear ||',
    'voiceReleaseTrusted || burstSdrtrunkLateEntryVoiceRelease || lateEntryAudioProbeAllowed',
    'late-entry-field-audio-probe-release',
]
present = [n for n in forbidden if n in text]
if present:
    raise SystemExit('unsafe late-entry probe speaker-release markers remain: ' + ', '.join(present))
print('P25 Phase 2 late-entry vocoder probe regression: PASS')
