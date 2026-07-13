#!/usr/bin/env python3
from pathlib import Path
import re
root = Path(__file__).resolve().parents[1]
main = (root / 'main.cpp').read_text(errors='ignore')
fn = re.search(r'static bool p25AmbeDecodeFrameLooksUsable\([^)]*\)\s*\{(?P<body>.*?)\n\}', main, re.S)
assert fn, 'p25AmbeDecodeFrameLooksUsable not found'
body = fn.group('body')
assert 'decoded.totalErrors <=' not in body, 'AMBE totalErrors must not be a hard audio gate'
assert 'return true;' in body, 'usable finite mbelib PCM should be accepted'
assert 'rms < 1.0e-6' not in body, 'valid low-energy AMBE concealment/silence frames must preserve 20 ms cadence'
assert 'peak > kP25DecodedAudioSafeMaxPeak' in body and 'rms > kP25DecodedAudioSafeMaxRms' in body, 'runaway PCM safety gate should remain'
assert 'vocoder-produced-no-frames' in main, 'field symptom marker/comment should remain for regression context'
print('no-total-error AMBE audio gate: PASS')
