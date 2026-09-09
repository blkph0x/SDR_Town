#!/usr/bin/env python3
from pathlib import Path
root = Path(__file__).resolve().parents[1]
from p25_orchestration_sources import orchestration_source_text
main = orchestration_source_text()
required = [
    'if (rx.p25VoicePhase2) {',
    'return nominalFreqHz;',
    'P25LiveDecoder baselineDecoder = rx.p25VoiceLiveDecoder.createIndependentProbeCopy(true);',
    'p25Phase2NeedsTargetOffsetProbe(live)',
    'kOffsetProbeHz',
    'effectiveTargetFreqHz = candidateTarget;',
    'establishedClearCall &&',
    'burst.xorMaskApplied &&',
    'const qint64 grantAgeMs = nowMs - rx.p25VoiceGrantEpochMs;',
    'sdrtrunkLateEntryVoiceRelease &&',
    'grantAgeMs >= 0',
    'const bool targetTrafficClearEvidence =',
    'out.phase2TargetSessionAudioRelease = true;',
]
missing = [s for s in required if s not in main]
if missing:
    raise SystemExit('missing audio-first big-ticket markers: ' + ', '.join(missing))
print('P25 Phase 2 audio-first big-ticket regression: PASS')
