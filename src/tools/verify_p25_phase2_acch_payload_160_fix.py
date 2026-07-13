#!/usr/bin/env python3
from pathlib import Path
root = Path(__file__).resolve().parents[1]
text = (root / 'P25LiveDecoder.cpp').read_text()
assert 'payloadDibits.size() < 160' in text, 'ACCH must accept 160-dibit Phase-2 timeslot payloads'
assert 'payloadDibits.size() < 170' not in text, 'old 170-dibit ACCH reject must be removed'
assert 'appendBitRange(codedBits, 2, 72' in text, 'ACCH must extract from 320-bit timeslot bit field 2..73'
assert 'appendBitRange(codedBits, 246, 72' in text, 'ACCH must extract final parity range after DUID gap'
assert 'appendBitRange(codedBits, 136, 2' in text and 'appendBitRange(codedBits, 180, 4' in text, 'FACCH fragmented INFO_23 bits must be preserved'
print('P25 Phase 2 ACCH 160-dibit payload extraction fix: PASS')
