#!/usr/bin/env python3
from pathlib import Path
root = Path(__file__).resolve().parents[1]
audio = (root / 'AudioEngine.cpp').read_text()
checks = {
    'ring capacity increased for digital voice buffering': '1u << 19' in audio,
    'jitter buffer named': 'kDigitalVoiceJitterSeconds' in audio,
    'jitter buffer is not 250ms': '0.25f' not in audio,
    'uses bounded low-latency cap': '0.65' in audio and '1.50' not in audio,
    'producer never advances read cursor on overflow': 'producer must not advance' in audio and 'rb.readPos.store(newRead' not in audio,
    'oversized producer block keeps bounded recent speech': 'samples += drop' in audio and 'queued >= maxQueuedFrames' in audio,
    'documents 20 ms PCM voice timing': '20 ms PCM' in audio,
}
failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit('P25 Phase 2 audio jitter buffer regression failed: ' + ', '.join(failed))
print('P25 Phase 2 audio jitter buffer regression: PASS')
