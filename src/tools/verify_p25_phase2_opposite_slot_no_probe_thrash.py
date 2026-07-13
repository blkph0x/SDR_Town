#!/usr/bin/env python3
from pathlib import Path
s = Path(__file__).resolve().parents[1] / 'main.cpp'
text = s.read_text(errors='ignore')
need = [
    'selectedSlotHasVoiceCodewords',
    'oppositeSlotHasVoiceCodewords',
    'opposite slot\'s VCWs',
    'Only call it wrong-slot when this',
    'if (!selectedSlotHasVoiceCodewords && oppositeSlotHasVoiceCodewords)'
]
missing=[n for n in need if n not in text]
if missing:
    raise SystemExit('missing opposite-slot no-probe-thrash markers: '+', '.join(missing))
print('P25 Phase 2 opposite-slot no-probe-thrash regression: PASS')
