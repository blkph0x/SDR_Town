#!/usr/bin/env python3
from pathlib import Path
root = Path(__file__).resolve().parents[2]
main = (root / 'src' / 'main.cpp').read_text(errors='ignore')
session = (root / 'include' / 'P25ReceiverSession.h').read_text(errors='ignore')
assert 'p25AmbeFrameHash' not in main, 'AMBE hash de-dupe was intentionally removed; it dropped valid repeated speech frames'
assert 'priorHash != hash' not in main, 'AMBE hash comparisons must not gate speech frames'
assert 'std::vector<uint64_t> recentAbsDibits' in session, 'Absolute-position AMBE de-dupe state missing'
assert 'kPhase2DuplicateStartToleranceDibits = 12u' in main, 'Duplicate start tolerance must remain below adjacent AMBE spacing'
print('P25 Phase 2 no-AMBE-hash de-dupe regression: PASS')
