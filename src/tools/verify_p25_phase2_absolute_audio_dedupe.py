#!/usr/bin/env python3
from pathlib import Path
import re
main = (Path(__file__).resolve().parents[1] / 'main.cpp').read_text(errors='ignore')
required = [
    'std::vector<uint64_t> recentAbsDibits',
    'kPhase2DuplicateStartToleranceDibits = 18u',
    'not de-dupe by AMBE payload hash',
    'payload hash/fingerprint',
]
for r in required:
    assert r in main, f'missing AMBE absolute-position de-dupe marker: {r}'
assert re.search(
    r'p25Phase2ShouldEmitAmbeFrame\s*\(\s*rx\s*,\s*codewordAbsDibit\s*,\s*codewordEndAbsDibit\s*,\s*haveAbsoluteDibits\s*,\s*false\s*\)',
    main,
    re.S,
), 'AMBE absolute-position de-dupe must happen before live mbelib decode without immediately remembering the frame'
assert 'priorHash != hash' not in main, 'AMBE payload-hash de-dupe must not suppress valid repeated speech frames'
assert 'p25AmbeFrameHash' not in main, 'AMBE hash de-dupe helper should be removed'
assert 'rx.p25Phase2LastEmittedAbsDibit' not in main, 'Receiver-header-dependent absolute de-dupe field remains'
print('P25 Phase 2 absolute-position AMBE audio de-dupe regression: PASS')
