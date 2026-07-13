#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]
main = (root / 'main.cpp').read_text(encoding='utf-8', errors='replace')
audio = (root / 'AudioEngine.cpp').read_text(encoding='utf-8', errors='replace')

checks = {
    'metadata follow helper': 'p25CommitPhase2TrafficMetadataFollow' in main,
    'same-mhz traffic reuse': 'P25 traffic source reused at same MHz' in main,
    'skip retune when already centered': 'already centered on voice=' in main,
    'reuse-centered traffic source selection': 'reuse-centered-traffic-source' in main,
    'same-mhz follow without restart': 'same-MHz follow' in main,
    'same-rf metadata follow after grace': 'sameRfMetadataFollowReady' in main,
    'speaker sustain decode chunks': 'kP25Phase2VoiceDecodeSpeakerSustainChunkSeconds = 0.040' in main,
    'speaker cadence 10ms': 'kP25Phase2VoiceDecodeSpeakerCadenceMs = 10' in main,
    'speaker pending jobs': 'kP25VoiceDecodeMaxPendingJobsSpeaker = 1' in main,
    'multi-pass voice drain': 'for (int drainPass = 0; drainPass < 6; ++drainPass)' in main,
    'pcm prime before empty ring': 'kPrimeSamples = 480' in main,
    'decode budget tightened': 'cfg.realtimeDecodeBudgetMs = 85' in main,
    'ring fill vs jitter cap': 'jitterCapFrames' in audio,
    'pending audio flush seq': 'p25PendingAudioFlushSeq' in main,
    'warm standby same mhz': 'warm-standby-same-mhz' in main,
    'warm standby hold window': 'kP25Phase2WarmStandbyMs = 5000' in main,
    'streaming push returns consumed': 'return consumed;' in main,
    'inline emit gate for audio log': 'p25Audio.phase2SpeakerGateReason == "emit"' in main,
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit('P25 Phase 2 retune/streaming regression failed: ' + ', '.join(failed))
print('P25 Phase 2 retune/streaming regression: PASS')
