#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]
main = (root / 'main.cpp').read_text(encoding='utf-8', errors='replace')

checks = {
    'live streaming push path exists': 'pushP25LiveStreamingAudio' in main,
    '800ms starvation cushion removed': 'rate * 0.800' not in main and 'pushP25JitterBufferedAudio' not in main,
    'voice worker queue depth bounded': 'kP25VoiceDecodeMaxPendingJobs = 1' in main,
    'speaker worker queue depth bounded': 'kP25VoiceDecodeMaxPendingJobsSpeaker = 1' in main,
    'completed voice results bounded': 'kMaxCompletedVoiceResults = 2' in main,
    'speaker-active decode cadence tightened': 'kP25Phase2VoiceDecodeSpeakerCadenceMs = 10' in main,
    'sustain decode windows shortened': 'kP25Phase2VoiceDecodeSustainChunkSeconds = 0.080' in main,
    'cc bleed guard on retuned traffic tuner': 'oneRtlTrafficTunerAwayFromCc' in main,
    'narrow traffic channelization for cc bleed': 'p25VoiceDecoderConfigForReceiver' in main and 'cfg.channelBandwidthHz = 6500.0' in main,
    'stale worker drops clear pending audio': 'pendingAudioByRx[p25ReceiverSessionKey(rx)].clear();' in main.split('if (stale) {', 1)[1].split('publishP25VoiceDiagnostics', 1)[0],
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit('P25 Phase 2 live streaming audio regression failed: ' + ', '.join(failed))
print('P25 Phase 2 live streaming audio regression: PASS')
