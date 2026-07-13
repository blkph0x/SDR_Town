#!/usr/bin/env python3
from pathlib import Path
src = Path(__file__).resolve().parents[1] / 'P25LiveDecoder.cpp'
text = src.read_text(errors='ignore')
checks = {
    'sdrtrunk comment': "sdrtrunk's Phase-2 traffic decoder is a dedicated Phase-2 pipeline" in text,
    'phase2 score before nid': text.find('phase2TelemetryScore') < text.find('const auto aValidNids = validNids(a);'),
    'voice weighted': 'phase2VoiceCodewords(r)) * 4' in text,
    'superframe weighted': 'phase2SuperframeBursts) * 2' in text,
    'masked weighted': 'phase2MaskedBursts) * 2' in text,
    'isch weighted': 'phase2IschDecoded) * 2' in text,
    'minimum p2 threshold': 'std::max(aP2, bP2) >= 8' in text,
}
failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit('P25 Phase 2 sdrtrunk candidate arbitration regression FAILED: ' + ', '.join(failed))
print('P25 Phase 2 sdrtrunk candidate arbitration regression: PASS')
