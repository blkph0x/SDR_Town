#!/usr/bin/env python3
from pathlib import Path
main = (Path(__file__).resolve().parents[1] / 'main.cpp').read_text(errors='ignore')
assert 'p25AmbeFrameHash' not in main, 'AMBE hash de-dupe was intentionally removed; it dropped valid repeated speech frames'
assert 'priorHash != hash' not in main, 'AMBE hash comparisons must not gate speech frames'
assert 'std::vector<uint64_t> recentAbsDibits' in main, 'Absolute-position AMBE de-dupe state missing'
assert 'kPhase2DuplicateStartToleranceDibits = 18u' in main, 'Duplicate start tolerance must remain below adjacent AMBE spacing'
print('P25 Phase 2 no-AMBE-hash de-dupe regression: PASS')
