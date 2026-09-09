#!/usr/bin/env python3
from p25_orchestration_sources import orchestration_source_text

text = orchestration_source_text()
need = [
    'selectedSlotHasVoiceCodewords',
    'oppositeSlotHasVoiceCodewords',
    "opposite slot's VCWs",
    'Only call it wrong-slot when this',
    'if (!selectedSlotHasVoiceCodewords && oppositeSlotHasVoiceCodewords)'
]
missing = [n for n in need if n not in text]
if missing:
    raise SystemExit('missing opposite-slot no-probe-thrash markers: ' + ', '.join(missing))
print('P25 Phase 2 opposite-slot no-probe-thrash regression: PASS')
