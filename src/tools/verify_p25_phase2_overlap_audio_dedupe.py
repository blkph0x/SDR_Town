#!/usr/bin/env python3
from pathlib import Path
root = Path(__file__).resolve().parents[1]
main = (root / 'main.cpp').read_text(errors='ignore')
session = (root.parent / 'include' / 'P25ReceiverSession.h').read_text(errors='ignore')
needles = [
    'Absolute recovered dibit position is what stops',
    'p25Phase2ShouldEmitAmbeFrame',
    'kPhase2DuplicateStartToleranceDibits = 12u',
    'p25Phase2VoiceFrameKeyHasProtocolIdentity(frameKey)',
    'SDRTrunk never re-plays a stream position',
    'phase2AbsoluteDuplicateSuppressedVoiceCodewords',
    'phase2SequencerSuppressedVoiceCodewords',
]
for n in needles:
    assert n in main, f'missing Phase 2 overlap AMBE de-dupe marker: {n}'
assert 'std::vector<uint64_t> recentAbsDibits' in session, 'session-owned AMBE absolute-position de-dupe state missing'
assert 'p25Phase2VoiceFrameKeyHasProtocolIdentity' in session, 'protocol-key helper missing'
assert 'p25Audio.audio.erase' not in main, 'stale PCM tail trim remains'
assert 'p25AmbeFrameHash' not in main, 'AMBE payload hash de-dupe should stay removed'
print('P25 Phase 2 overlap audio de-dupe regression: PASS')
