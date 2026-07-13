#!/usr/bin/env python3
from pathlib import Path
root = Path(__file__).resolve().parents[1]
main = (root / 'main.cpp').read_text(errors='ignore')
required = [
    'if (rx.p25VoicePhase2) {',
    'return nominalFreqHz;',
    'P25LiveDecoder baselineDecoder = rx.p25VoiceLiveDecoder;',
    'p25Phase2NeedsTargetOffsetProbe(live)',
    'kOffsetProbeHz',
    'effectiveTargetFreqHz = candidateTarget;',
    'grantClearTrusted &&',
    'burst.xorMaskApplied &&',
    'grantAgeMs >= (sdrtrunkLateEntryVoiceRelease ? 0 : 500)',
    'out.phase2TargetSessionAudioRelease = true;',
]
missing = [s for s in required if s not in main]
if missing:
    raise SystemExit('missing audio-first big-ticket markers: ' + ', '.join(missing))
print('P25 Phase 2 audio-first big-ticket regression: PASS')
