#!/usr/bin/env python3
from pathlib import Path
main = (Path(__file__).resolve().parents[1] / 'main.cpp').read_text(errors='ignore')
needles = [
    'absolute recovered AMBE start position, not payload hash',
    'p25Phase2ShouldEmitAmbeFrame',
    'kPhase2DuplicateStartToleranceDibits = 18u',
    'std::vector<uint64_t> recentAbsDibits',
]
for n in needles:
    assert n in main, f'missing Phase 2 overlap AMBE de-dupe marker: {n}'
assert 'p25Audio.audio.erase' not in main, 'stale PCM tail trim remains'
assert 'p25AmbeFrameHash' not in main, 'AMBE payload hash de-dupe should stay removed'
print('P25 Phase 2 overlap audio de-dupe regression: PASS')
