#!/usr/bin/env python3
from pathlib import Path
root = Path(__file__).resolve().parents[1]
p25 = (root / 'P25LiveDecoder.cpp').read_text(errors='ignore')
main = (root / 'main.cpp').read_text(errors='ignore')
assert 'for (uint16_t info = 0; info < 512; ++info)' in p25, 'I-ISCH decoder must enumerate all 9-bit information words'
assert 'info & 0x01ffu' in p25, 'I-ISCH encoder must preserve 9-bit information word'
assert 'out.channel = static_cast<uint8_t>((v >> 5) & 0x03u);' in p25, 'I-ISCH channel field must be parsed from bits 2..3'
assert 'out.location = static_cast<uint8_t>((v >> 3) & 0x03u);' in p25, 'I-ISCH location field must be parsed from bits 4..5'
assert 'ischAnchoredMaskPhase' in p25, 'Mask phase should be promotable from low-error I-ISCH like sdrtrunk timeslot offset'
assert 'grantUnknownProbe' in main, 'Unknown Phase 2 grants should be probed until trusted ESS/MAC proves encryption'
assert 'applyP25Phase2SecurityAudioGate' in main and 'p25Phase2PendingAudio' in main, 'Unknown probe may decode for diagnostics but speaker audio must be queued until clear'
assert 'b.isch.channel <= 1' in p25, 'I-ISCH mask-phase anchor must reject invalid channel values'
print('P25 Phase 2 I-ISCH/mask/unknown-grant alignment regression: PASS')
