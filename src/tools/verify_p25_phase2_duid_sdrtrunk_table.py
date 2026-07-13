#!/usr/bin/env python3
from pathlib import Path
import re
src = Path(__file__).resolve().parents[1] / 'P25LiveDecoder.cpp'
text = src.read_text()
expected = {
    '0x00': 'VOICE_4',
    '0x39': 'SCRAMBLED_SACCH',
    '0x65': 'VOICE_2',
    '0x9A': 'SCRAMBLED_FACCH',
    '0xA3': 'SCRAMBLED_DATCH',
    '0xC6': 'UNSCRAMBLED_SACCH',
    '0xD1': 'UNSCRAMBLED_LCCH',
    '0xFF': 'UNSCRAMBLED_FACCH',
}
missing = []
for value, label in expected.items():
    if value not in text or label not in text:
        missing.append((value, label))
wrong = ['0x3A', '0x66', '0x97', '0xCB', '0xDC', '0xF1']
wrong_present = [v for v in wrong if re.search(r'\b' + re.escape(v) + r'\b', text)]
if missing or wrong_present:
    print('FAIL: DUID table mismatch')
    if missing:
        print('missing:', missing)
    if wrong_present:
        print('old wrong values present:', wrong_present)
    raise SystemExit(1)
print('P25 Phase 2 DUID sdrtrunk table regression: PASS')
