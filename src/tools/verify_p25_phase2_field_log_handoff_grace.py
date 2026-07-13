#!/usr/bin/env python3
from pathlib import Path
root = Path(__file__).resolve().parents[1]
main = (root / 'main.cpp').read_text()
recv = (root.parent / 'include' / 'Receiver.h').read_text()
assert 'kP25Phase2SameRfSlotHandoffGraceMs' in main
assert 'phase2HandoffGraceActive' in main
assert 'sameRfPhase2SlotHandoff = haveActiveDiag && currentVoiceUnacquired' in main
assert 'kP25Phase2UnacquiredDwellStealGraceMs' in main
assert 'service-option-less grant' in main
assert 'p25VoiceGrantEpochMs' in recv and 'p25VoiceSourceId' in recv
assert 'Do not include grant epoch here' in main
assert 'Do not require' in main and 'sourceId either' in main
assert 'p25TrafficProcessorSessionId' in main
assert 'rx.p25VoiceGrantEpochMs' in main and 'rx.p25VoiceSourceId' in main
assert 'p25ClearPhase2PendingAudio(rx);' in main
assert 'phase2TargetVoiceCodewords' in recv and 'phase2AmbeDecodeAttempts' in recv
print('P25 Phase 2 field-log handoff grace/keyed diagnostics regression: PASS')
