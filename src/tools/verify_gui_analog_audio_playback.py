#!/usr/bin/env python3
"""Verify GUI analog demod audio reaches the playback engine."""
from pathlib import Path

root = Path(__file__).resolve().parents[2]
main = (root / 'src' / 'main.cpp').read_text(encoding='utf-8', errors='replace')

checks = {
    'gui demodulates analog modes': 'rx.demod.demodulateToAudio(iq, sr, cf, demodFreq, monMode' in main,
    'gui pushes analog audio frames': (
        'pushAudioFrames(audioOutputEngine' in main and
        'Analog demod (WFM/NFM/AM/etc)' in main
    ),
    'p25 path stays gated': 'mayPushSpeakerAudio' in main and 'pushP25LiveStreamingAudio' in main,
    'mode change flushes pending audio': (
        'p25PendingAudioFlushSeq.fetch_add(1, std::memory_order_release)' in main.split('rfOrModeChanged', 1)[1][:1200]
    ),
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit('GUI analog audio playback regression failed: ' + ', '.join(failed))
print('GUI analog audio playback regression: PASS')
