#!/usr/bin/env python3
from pathlib import Path
root = Path(__file__).resolve().parents[1]
main = (root / 'main.cpp').read_text()
audio = (root / 'AudioEngine.cpp').read_text()
checks = {
    'phase2 rolling window covers a full TDMA superframe': 'kP25Phase2VoiceDecodeWindowSeconds = 0.480' in main,
    'phase2 decode cadence bounded without starving IQ': 'kP25Phase2VoiceDecodeCadenceMs = 120' in main,
    'phase2 pull window drains late receiver cursors': 'kP25Phase2VoicePullWindowSeconds = kP25Phase2VoiceDecodeWindowSeconds' in main and 'sr * 0.040' not in main,
    'phase2 decode chunk spans superframe context': 'kP25Phase2VoiceDecodeAcquireChunkSeconds = 0.420' in main,
    'phase2 overlap preserves timing context': 'kP25Phase2VoiceDecodeAcquireOverlapSeconds = 0.160' in main,
    'phase2 waits for superframe-sized fresh IQ': 'kP25Phase2VoiceDecodeMinFreshSeconds = 0.300' in main,
    'audio jitter cap reduced': 'kDigitalVoiceJitterSeconds = 0.65' in audio,
    'overflow preserves SPSC read cursor ownership': 'producer must not advance' in audio and 'rb.readPos.store(newRead' not in audio,
    'oversized producer block keeps most recent speech': 'samples += drop' in audio,
}
failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit('P25 Phase 2 low-latency audio regression failed: ' + ', '.join(failed))
print('P25 Phase 2 low-latency audio regression: PASS')
