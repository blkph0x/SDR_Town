#!/usr/bin/env python3
"""Verify P25 Phase 2 sustain/playback robustness regressions."""
from pathlib import Path

root = Path(__file__).resolve().parents[2]
main = (root / 'src' / 'main.cpp').read_text(encoding='utf-8', errors='replace')
p25 = (root / 'src' / 'P25LiveDecoder.cpp').read_text(encoding='utf-8', errors='replace')
bootstrap_block = main.split('static bool p25Phase2BootstrappedMaskTargetVoiceEvidence', 1)[1][:500] if 'static bool p25Phase2BootstrappedMaskTargetVoiceEvidence' in main else ''

checks = {
    'fixed speaker sustain cadence': (
        'During active speaker sustain, keep a fixed near-live cadence' in main and
        'gLastDspMicros' not in main.split('p25Phase2AdaptiveVoiceDecodeCadenceMs', 1)[1].split('static bool p25Phase2SpeakerSustainDecodeActive', 1)[0]
    ),
    'speaker gate requires target vcw': 'phase2-no-target-slot-vcw' in main,
    'bootstrap requires target vcw': (
        'out.phase2TargetVoiceCodewords >= 2' in bootstrap_block and
        'out.phase2OppositeVoiceCodewords' not in bootstrap_block
    ),
    'skip offset probe after session lock': (
        'skipOffsetProbeAfterMaskLock' in main and
        'p25Phase2SessionHadBurstEye(rx) ||' in main.split('skipOffsetProbeAfterMaskLock', 1)[1][:500]
    ),
    'bounded p25 speaker push helper': (
        'pushP25SpeakerAudio' in main and
        'return pushP25LiveStreamingAudio(engine, pending, audio, activeOutputIndices, 240, ringFillPercent);' in
            main.split('pushP25SpeakerAudio', 1)[1][:900] and
        'pushAudioFrames(engine, pending, audio' not in main.split('pushP25SpeakerAudio', 1)[1][:900]
    ),
    'sustain decode on successful emit': 'sustain.hadSuccessfulEmit' in main.split('p25Phase2UseSustainDecodeWindowLocked', 1)[1][:3500],
    'deep acch when mask applied': 'mask != nullptr' in p25 or '&& mask' in p25,
    'soft cqpsk hold before mac ess': (
        'allowRealtimePhase2SoftDemodHold' in p25 and
        'allowSoftCqpskStop' in p25 and
        'phase2StandardsStateSeen ||\n        allowRealtimePhase2SoftDemodHold' in p25
    ),
    'locked soft cqpsk candidate selected': (
        'lockedSoftPhase2Evidence' in p25 and
        '(trust > 0 || lockedSoftPhase2Evidence) && betterLiveResult(candidate, best' in p25
    ),
    'bounded realtime mask rescue': (
        'maxRescueCandidates' in p25 and
        'm_config.realtimeVoiceSearch ? std::min<size_t>(phaseWindows.size(), 1u)' in p25 and
        'rescueScoreSlots = m_config.realtimeVoiceSearch ? 6u : 12u' in p25 and
        'rescueDeepBudget = m_config.realtimeVoiceSearch ? 4u : 8u' in p25 and
        'm_config.realtimeVoiceSearch ? 3u : 12u' in p25 and
        '? size_t{2}' in p25
    ),
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
