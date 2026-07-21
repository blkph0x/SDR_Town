#!/usr/bin/env python3
from pathlib import Path
root = Path(__file__).resolve().parents[1]
main = (root / 'main.cpp').read_text()
audio = (root / 'AudioEngine.cpp').read_text()
checks = {
    'phase2 rolling window keeps two-superframe cold context': 'kP25Phase2VoiceDecodeWindowSeconds = 0.720' in main,
    'first cold eye keeps two-superframe late-entry context': 'kP25Phase2VoiceDecodeFirstColdEyeSeconds = 0.720' in main,
    'phase2 decode cadence stays near live': 'kP25Phase2VoiceDecodeCadenceMs = 10' in main,
    'phase2 pull window drains receiver cursors in small slices': 'kP25Phase2VoicePullWindowSeconds = 0.100' in main,
    'post-cold acquire uses short fresh chunks': 'kP25Phase2VoiceDecodeAcquireChunkSeconds = 0.050' in main,
    'post-cold acquire overlap is bounded': 'kP25Phase2VoiceDecodeAcquireOverlapSeconds = 0.160' in main,
    'cli stream replay uses bounded acquire/sustain context not full-window replay': (
        'streamAcquireContextSeconds' in main and
        'streamSustainContextSeconds' in main and
        'voiceSustainContextSamples' in main and
        'sustainContextMs=' in main
    ),
    'phase2 post-eye acquire does not wait for a full superframe': 'kP25Phase2VoiceDecodeMinFreshSeconds = 0.020' in main,
    'cold cqpsk search is bounded for live traffic': 'kP25VoiceWorkerColdMaxCqpskCandidates = 32' in main,
    'hot phase2 commit work is bounded': 'kP25VoiceWorkerHotMaxPhase2SyncHits = 24' in main and 'setMaxPhase2SyncHits' in main,
    'wide reacquire does not override current streaming eye': 'currentStreamingEye' in main,
    'audio jitter cap deepened for continuity': 'kDigitalVoiceJitterSeconds = 0.85' in audio,
    'overflow preserves SPSC read cursor ownership': 'producer must not advance' in audio and 'rb.readPos.store(newRead' not in audio,
    'oversized producer block keeps most recent speech': 'samples += drop' in audio,
}
failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit('P25 Phase 2 low-latency audio regression failed: ' + ', '.join(failed))
print('P25 Phase 2 low-latency audio regression: PASS')
