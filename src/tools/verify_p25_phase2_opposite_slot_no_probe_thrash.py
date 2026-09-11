#!/usr/bin/env python3
from p25_orchestration_sources import orchestration_source_text

text = orchestration_source_text()
need = [
    'selectedSlotHasVoiceCodewords',
    'oppositeSlotHasVoiceCodewords',
    # Comment wording drifted; lock the still-present companion-observe + wrong-slot path.
    'never feeds',
    'Observe/decode the companion',
    'if (!selectedSlotHasVoiceCodewords && oppositeSlotHasVoiceCodewords)'
]
missing = [n for n in need if n not in text]
if missing:
    raise SystemExit('missing opposite-slot no-probe-thrash markers: ' + ', '.join(missing))
print('P25 Phase 2 opposite-slot no-probe-thrash regression: PASS')
