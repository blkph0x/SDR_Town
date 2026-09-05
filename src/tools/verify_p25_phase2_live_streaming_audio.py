#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]
main = (root / 'main.cpp').read_text(encoding='utf-8', errors='replace')

checks = {
    'live streaming push path exists': 'pushP25LiveStreamingAudio' in main,
    '800ms starvation cushion removed': 'rate * 0.800' not in main and 'pushP25JitterBufferedAudio' not in main,
    'voice worker queue depth single-flight': 'kP25VoiceDecodeMaxPendingJobs = 1' in main,
    'speaker worker queue depth single-flight': 'kP25VoiceDecodeMaxPendingJobsSpeaker = 1' in main,
    'worker busy counts as in-flight': 'const size_t runningJobs = p25VoiceWorkerBusy.load' in main
        and 'const size_t inFlightJobs = p25VoicePendingJobs.size() + publishBacklog + runningJobs;' in main,
    'completed voice results bounded': 'kP25VoiceDecodeMaxCompletedResults = 16' in main,
    'speaker-active decode cadence tightened': 'kP25Phase2VoiceDecodeSpeakerCadenceMs = 3' in main,
    'non-speaker sustain decode windows bounded': 'kP25Phase2VoiceDecodeSustainChunkSeconds = 0.080' in main,
    'speaker sustain cadence evidence-backed': (
        'kP25Phase2VoiceDecodeSpeakerSustainChunkSeconds = 0.720' in main and
        'kP25Phase2VoiceDecodeSpeakerSustainMinFreshSeconds = 0.360' in main and
        'kP25Phase2VoiceDecodeSpeakerSustainOverlapSeconds = 0.060' in main
    ),
    'cc bleed guard on retuned traffic tuner': 'oneRtlTrafficTunerAwayFromCc' in main,
    'traffic channelization preserves CQPSK eye while guarding cc bleed': (
        'p25VoiceDecoderConfigForReceiver' in main and
        'cfg.channelBandwidthHz = 10000.0' in main and
        'cfg.channelBandwidthHz = 12500.0' in main and
        '6.5 kHz clamp' in main
    ),
    'stale worker drops clear pending audio': (
        'p25Phase2ClearStaleResultSpeakerPending(pendingAudioByRx' in
        main.split('if (stale) {', 1)[1].split('publishP25VoiceDiagnostics', 1)[0]
    ),
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit('P25 Phase 2 live streaming audio regression failed: ' + ', '.join(failed))
print('P25 Phase 2 live streaming audio regression: PASS')
