#!/usr/bin/env python3
from pathlib import Path
src = Path(__file__).resolve().parents[1] / 'P25LiveDecoder.cpp'
text = src.read_text()
required = 'if (!burst.isch.sync && burst.isch.location <= 2 && burst.superframeBurstIndexKnown)'
forbidden = 'if (burst.isch.valid && burst.isch.sync && burst.isch.location <= 2 && burst.superframeBurstIndexKnown)'
if required not in text:
    raise SystemExit('FAIL: I-ISCH mask phase anchor does not require decoded I-ISCH information')
if forbidden in text:
    raise SystemExit('FAIL: stale plain-sync mask phase anchor remains')
if 'expectedMaskIndex = static_cast<size_t>(burst.isch.location) * 4u + (burstIndex % 4u)' not in text:
    raise SystemExit('FAIL: expected I-ISCH location-to-mask segment mapping missing')
print('PASS: Phase 2 decoded I-ISCH now anchors XOR mask phase scoring')
