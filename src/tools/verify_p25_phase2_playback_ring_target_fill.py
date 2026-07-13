#!/usr/bin/env python3
"""Verify P25 speaker playback keeps a target jitter fill instead of starving the ring."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
main = (root / 'main.cpp').read_text(encoding='utf-8', errors='replace')
engine_h = (root / '../include/AudioEngine.h').resolve().read_text(encoding='utf-8', errors='replace')
engine_cpp = (root / 'AudioEngine.cpp').read_text(encoding='utf-8', errors='replace')

checks = {
    'ring queued samples API': 'getRingQueuedSamples' in engine_h and 'getRingQueuedSamples' in engine_cpp,
    'jitter cap helper': 'getJitterQueueCapFrames' in engine_h and 'getJitterQueueCapFrames' in engine_cpp,
    '60ms playback prime': '0.060' in main.split('pushP25LiveStreamingAudio', 1)[1][:1200],
    '200ms target fill': '0.200' in main.split('pushP25LiveStreamingAudio', 1)[1][:1600],
    '500ms pending stash': '0.500' in main.split('pushP25LiveStreamingAudio', 1)[1][:1800],
    'batch top-up loop': 'targetQueuedSamples' in main and 'while (pending.size() - totalPushed >= frameSize)' in main,
    'speaker top-up helper': 'p25TopUpSpeakerPlaybackRing' in main,
    'dsp worker top-up call': 'speaker playback top-up' in main,
    'ring already primed bypass': 'ringAlreadyPrimed' in main,
    'speaker queue depth bounded': 'kP25VoiceDecodeMaxPendingJobsSpeaker = 1' in main,
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit('P25 Phase 2 playback ring target-fill regression failed: ' + ', '.join(failed))
print('P25 Phase 2 playback ring target-fill regression: PASS')
