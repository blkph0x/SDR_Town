#!/usr/bin/env python3
from pathlib import Path
import re
root = Path(__file__).resolve().parents[1]
main = (root / 'main.cpp').read_text(errors='ignore')
session = (root.parent / 'include' / 'P25ReceiverSession.h').read_text(errors='ignore')
required = [
    'kPhase2DuplicateStartToleranceDibits = 12u',
    'not de-dupe by AMBE payload hash',
    'payload hash/fingerprint',
    'equality is the normal contiguous next AMBE frame, not a duplicate',
    'if (!isForwardNew)',
    'Cursor resets are already handled by the IQ stream/session reset paths',
]
for r in required:
    assert r in main, f'missing AMBE absolute-position de-dupe marker: {r}'
assert 'std::vector<uint64_t> recentAbsDibits' in session, 'session-owned AMBE absolute-position de-dupe state missing'
assert re.search(
    r'p25Phase2ShouldEmitAmbeFrame\s*\(\s*rx\s*,\s*codewordAbsDibit\s*,\s*codewordEndAbsDibit\s*,\s*haveAbsoluteDibits\s*,\s*false\s*\)',
    main,
    re.S,
), 'AMBE absolute-position de-dupe must happen before live mbelib decode without immediately remembering the frame'
assert 'priorHash != hash' not in main, 'AMBE payload-hash de-dupe must not suppress valid repeated speech frames'
assert 'p25AmbeFrameHash' not in main, 'AMBE hash de-dupe helper should be removed'
assert 'rx.p25Phase2LastEmittedAbsDibit' not in main, 'Receiver-header-dependent absolute de-dupe field remains'
assert 'kPhase2CursorResetDibits' not in main, 'GUI 720ms/40ms overlapped windows must not reset AMBE de-dupe on a one-superframe rewind'
print('P25 Phase 2 absolute-position AMBE audio de-dupe regression: PASS')
