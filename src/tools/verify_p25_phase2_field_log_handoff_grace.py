#!/usr/bin/env python3
from pathlib import Path
root = Path(__file__).resolve().parents[1]
main = (root / 'main.cpp').read_text(encoding='utf-8', errors='replace')
recv = (root.parent / 'include' / 'Receiver.h').read_text(encoding='utf-8', errors='replace')
assert 'kP25Phase2SameRfSlotHandoffGraceMs' in main
assert 'phase2HandoffGraceActive' in main
assert 'sameRfPhase2SlotHandoff = haveActiveDiag && currentVoiceUnacquired' in main
assert 'kP25Phase2UnacquiredDwellStealGraceMs' in main
assert 'service-option-less grant' in main
assert 'p25VoiceGrantEpochMs' in recv and 'p25VoiceSourceId' in recv
assert 'key.sourceId = rx.p25VoiceSourceId;' in main
assert 'key.grantEpochMs = rx.p25VoiceGrantEpochMs;' in main
session = (root.parent / 'include' / 'P25ReceiverSession.h').read_text(encoding='utf-8', errors='replace')
call_key_eq = session[session.find('bool operator=='):session.find('};', session.find('bool operator=='))]
assert 'sourceId == other.sourceId' not in call_key_eq
assert 'grantEpochMs == other.grantEpochMs' in (root.parent / 'include' / 'P25ReceiverSession.h').read_text(encoding='utf-8', errors='replace')
assert 'p25TrafficProcessorSessionId' in main
assert 'rx.p25VoiceGrantEpochMs' in main and 'rx.p25VoiceSourceId' in main
assert 'p25ClearPhase2PendingAudio(rx);' in main
assert 'phase2TargetVoiceCodewords' in recv and 'phase2AmbeDecodeAttempts' in recv
print('P25 Phase 2 field-log handoff grace/keyed diagnostics regression: PASS')

