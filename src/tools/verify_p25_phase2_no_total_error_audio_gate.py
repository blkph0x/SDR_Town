#!/usr/bin/env python3
from pathlib import Path
import re
root = Path(__file__).resolve().parents[1]
main = (root / 'main.cpp').read_text(errors='ignore')
fn = re.search(r'static bool p25AmbeDecodeFrameLooksUsable\([^)]*\)\s*\{(?P<body>.*?)\n\}', main, re.S)
assert fn, 'p25AmbeDecodeFrameLooksUsable not found'
body = fn.group('body')
assert 'decoded.totalErrors > 3' not in body, (
    'usable gate must not hard-cut on totalErrors '
    '(field: islands / sparse duty when invent-PLC is disabled)'
)
assert 'Do not hard-gate on totalErrors' in body, 'field-evidence comment should remain'
assert 'return true;' in body, 'usable finite mbelib PCM should be accepted'
assert 'rms < 1.0e-6' not in body, 'valid low-energy AMBE concealment/silence frames must preserve 20 ms cadence'
assert 'peak > kP25DecodedAudioSafeMaxPeak' in body and 'rms > kP25DecodedAudioSafeMaxRms' in body, 'runaway PCM safety gate should remain'
assert 'p25Phase2AppendOppositeSlotSustainPlc' in main and 'Intentionally disabled' in main, (
    'opposite-slot invent PLC must stay disabled (SDRTrunk per-timeslot silence)'
)
print('no-totalErrors usable gate + no invent-PLC: PASS')
