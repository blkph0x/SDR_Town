#!/usr/bin/env python3
"""Verify P25 Phase 2 sustain/playback robustness regressions."""
from pathlib import Path

root = Path(__file__).resolve().parents[2]
main = (root / 'src' / 'main.cpp').read_text(encoding='utf-8', errors='replace')
p25 = (root / 'src' / 'P25LiveDecoder.cpp').read_text(encoding='utf-8', errors='replace')

checks = {
    'fixed speaker sustain cadence': (
        'During active speaker sustain, keep a fixed near-live cadence' in main and
        'gLastDspMicros' not in main.split('p25Phase2AdaptiveVoiceDecodeCadenceMs', 1)[1].split('static bool p25Phase2SpeakerSustainDecodeActive', 1)[0]
    ),
    'speaker gate requires target vcw': 'phase2-no-target-slot-vcw' in main,
    'bootstrap requires target vcw': 'out.phase2OppositeVoiceCodewords >= 4' not in main.split('strongTargetVcwMask', 1)[1][:900],
    'skip offset probe after session lock': 'p25Phase2SessionHadVoiceLock(rx) ||' in main.split('skipOffsetProbeAfterMaskLock', 1)[1][:300],
    'low latency speaker push helper': 'pushP25SpeakerAudio' in main and 'pushAudioFrames(engine, pending, audio' in main.split('pushP25SpeakerAudio', 1)[1][:800],
    'sustain decode on successful emit': 'sustain.hadSuccessfulEmit' in main.split('p25Phase2UseSustainDecodeWindowLocked', 1)[1][:2500],
    'deep acch when mask applied': 'mask != nullptr' in p25,
    'contiguous phase2 timeslot payload': (
        'Phase 2 TDMA does not' in p25 and
        'payloadDibits[i] = dibits[payload + i] & 0x03;' in p25 and
        'stripPhase2TimeslotPayload' not in p25
    ),
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit('P25 Phase 2 sustain robustness regression failed: ' + ', '.join(failed))
print('P25 Phase 2 sustain robustness regression: PASS')
