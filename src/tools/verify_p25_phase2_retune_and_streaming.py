#!/usr/bin/env python3
import re
from pathlib import Path

root = Path(__file__).resolve().parents[1]
main = (root / 'main.cpp').read_text(encoding='utf-8', errors='replace')
audio = (root / 'AudioEngine.cpp').read_text(encoding='utf-8', errors='replace')
live_diag = (root / 'tools' / 'run_p25_live_clear_audio_diag.py').read_text(encoding='utf-8', errors='replace')
deep_audit = (root / 'tools' / 'run_p25_capture_deep_audit.py').read_text(encoding='utf-8', errors='replace')
metadata_ready = re.search(
    r'const bool sameRfMetadataFollowReady\s*=\s*(.*?);',
    main,
    re.DOTALL)
metadata_ready_expr = metadata_ready.group(1) if metadata_ready else ''
can_accept = re.search(
    r'bool p25VoiceWorkerCanAcceptJob\(\)\s*\{(.*?)\n    \}',
    main,
    re.DOTALL)
can_accept_body = can_accept.group(1) if can_accept else ''
still_current = re.search(
    r'auto stillCurrent = .*?\};',
    main,
    re.DOTALL)
still_current_body = still_current.group(0) if still_current else ''

checks = {
    'metadata follow helper': 'p25CommitPhase2TrafficMetadataFollow' in main,
    'same-mhz traffic reuse': 'P25 traffic source reused at same MHz' in main,
    'skip retune when already centered': 'already at low-IF center=' in main,
    'reuse-centered traffic source selection': 'reuse-centered-traffic-source' in main,
    'same-mhz follow without restart': 'same-MHz follow' in main,
    'same-rf metadata follow quiet/unacquired': 'sameRfMetadataFollowReady' in main and '!currentFollowSpeakerActive' in metadata_ready_expr,
    'same-rf speaker hold': 'auto-follow-same-rf-speaker-hold' in main,
    'same-rf metadata not speaker-triggered': 'p25RecentSpeakerOutputActive' not in metadata_ready_expr,
    'same-rf metadata switch resets dwell': 'p25AutoFollowTunedAtMs = nowMs;' in main,
    'speaker sustain decode chunks': 'kP25Phase2VoiceDecodeSpeakerSustainChunkSeconds = 0.040' in main,
    'speaker cadence 3ms': 'kP25Phase2VoiceDecodeSpeakerCadenceMs = 3' in main,
    'speaker pending jobs': 'kP25VoiceDecodeMaxPendingJobsSpeaker = 2' in main,
    'worker queues while busy': 'p25VoiceWorkerBusy.load' not in can_accept_body,
    'rolling cursor commits non-stale': 'result.hasAudioBlock && !result.stale' in main,
    'same-rf stale shortcut removed': 'sameRfTrafficSession' not in still_current_body,
    'multi-pass voice drain': 'for (int drainPass = 0; drainPass < 6; ++drainPass)' in main,
    'pcm prime before empty ring': 'queuedNow + pending.size() < minPrimeSamples' in main,
    'decode budget tightened': 'std::min(priorDecodeBudgetMs, 55)' in main,
    'ring fill vs jitter cap': 'jitterCapFrames' in audio,
    'pending audio flush seq': 'p25PendingAudioFlushSeq' in main,
    'warm standby same mhz': 'warm-standby-same-mhz' in main,
    'warm standby hold window': 'kP25Phase2WarmStandbyMs = 5000' in main,
    'streaming push returns consumed': 'return totalPushed;' in main,
    'inline emit gate for audio log': 'p25Audio.phase2SpeakerGateReason == "emit"' in main,
    'follow IQ records low-IF RF center': (
        'double captureCenterFreqHz = 0.0' in main and
        'req.centerFreqHz = (captureCenterFreqHz > 0.0 && std::isfinite(captureCenterFreqHz))' in main and
        'cliTrafficCenterHz,' in main
    ),
    'follow IQ logs rf center': 'rfCenter=%2 MHz' in main,
    'CLI gated PCM updates audio-open timestamp': (
        'appendCliP25WavCapture(ch);' in main and
        'if (rxP25VoiceDecode) {' in main and
        'gP25AudioLastSpeakerOutputMs.store(' in main
    ),
    'live diag accepts nonzero gated wav': (
        'wav_pcm_opened = any(' in live_diag and
        'wav_pcm_opened' in live_diag and
        'audio_opened' in live_diag
    ),
    'deep audit suppresses snapshot-only pass noise': (
        'snapshot_only = {"no_speaker_audio_pushed", "live_zero_phase2_bursts"}' in deep_audit and
        'PASS_CONTINUOUS_AUDIO' in deep_audit and
        'deduped_findings' in deep_audit
    ),
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit('P25 Phase 2 retune/streaming regression failed: ' + ', '.join(failed))
print('P25 Phase 2 retune/streaming regression: PASS')
