#!/usr/bin/env python3
from pathlib import Path

main = Path(__file__).resolve().parents[1] / 'main.cpp'
text = main.read_text()

assert 'kP25Phase2SameRfSlotHandoffGraceMs = 8000' in text
assert 'kP25Phase2SameRfUnacquiredSlotStealMs = 8000' in text
assert 'decodeControlFromThisDevice' in text
assert 'p25IndependentTrafficRetunedPrimary' in text
assert 'rx.p25SessionState.ambeDedupe = {}' in text
assert '!(sameRfPhase2Carrier && phase2HandoffGraceActive)' in text
meta_idx = text.find('metadataSwitchCommitted = true')
assert meta_idx != -1
meta_block = text[meta_idx - 1200:meta_idx + 400]
assert 'p25Phase2ResetTrafficTargetOffset' not in meta_block
print('P25 Phase 2 same-RF acquisition regression: PASS')
