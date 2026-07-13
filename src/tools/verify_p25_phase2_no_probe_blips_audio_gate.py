#!/usr/bin/env python3
from pathlib import Path
text = Path('src/main.cpp').read_text(errors='ignore')
checks = {
    'phase2 min decoded frames': 'out.decodedFrames >= kP25Phase2UnknownGrantAudioProbeMinFrames' in text,
    'phase2 min pcm samples': 'out.audio.size() >= kP25Phase2UnknownGrantAudioProbeMinSamples' in text,
    'probe fragment comment': 'late-entry/probe fragments' in text,
    'keeps safety gate': 'p25AudioSamplesLookSafe(out.audio)' in text,
    'unknown probe is diagnostic only': 'late-entry-audio-probe-diagnostic-only' in text,
    'unknown clears audio before speaker': 'out.audio.clear();\n        out.decodedFrames = 0;' in text,
}
missing = [name for name, ok in checks.items() if not ok]
if missing:
    raise SystemExit('missing Phase 2 no-probe-blips audio gate markers: ' + ', '.join(missing))
print('P25 Phase 2 no-probe-blips audio gate regression: PASS')
