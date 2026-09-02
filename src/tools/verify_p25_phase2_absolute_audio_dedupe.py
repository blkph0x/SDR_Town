#!/usr/bin/env python3
from pathlib import Path
root = Path(__file__).resolve().parents[1]
main = (root / 'main.cpp').read_text(errors='ignore')
session = (root.parent / 'include' / 'P25ReceiverSession.h').read_text(errors='ignore')
required = [
    'kPhase2DuplicateStartToleranceDibits = 12u',
    'not de-dupe by AMBE payload hash',
    'p25Phase2VoiceFrameKeyHasProtocolIdentity(frameKey)',
    'SDRTrunk never re-plays a stream position',
    'must not bypass abs de-dupe across overlapping GUI windows',
    'readyAbsStarts',
    'duplicateInReadyBatch',
    'kReadyBatchDuplicateToleranceDibits = 12u',
    'phase2AbsoluteDuplicateSuppressedVoiceCodewords',
    'phase2SequencerSuppressedVoiceCodewords',
    'kPhase2ForwardWobbleDibits = 12u',
    'if (!isForwardNew)',
    'Cursor resets are already handled by the IQ stream/session reset paths',
]
for r in required:
    assert r in main, f'missing AMBE absolute-position de-dupe marker: {r}'
assert 'std::vector<uint64_t> recentAbsDibits' in session, 'session-owned AMBE absolute-position de-dupe state missing'
assert 'p25Phase2VoiceFrameKeyHasProtocolIdentity' in session, 'protocol frame identity helper missing'
assert 'p25Phase2VoiceFrameNeedsAbsoluteDedupeFallback' in session, 'absolute de-dupe fallback helper missing'
fresh_anchor = main.find('contextAudioLockedOut')
fresh_start = main.rfind('for (const auto& codeword : burst.voiceCodewords)', 0, fresh_anchor)
fresh_end = main.find('P25Phase2SequencerSpeechInput seqInput;', fresh_start)
fresh_loop = main[fresh_start:fresh_end]
pending_loop = main.split('for (const auto& pending : pendingFrames)', 1)[1].split(
    'auto releasePendingRawVoiceFromEss', 1
)[0]
assert (
    'if (!p25Phase2ShouldEmitAmbeFrame(rx, codewordAbsDibit, codewordEndAbsDibit,' in fresh_loop
    and 'must not bypass abs de-dupe across overlapping GUI windows' in fresh_loop
), 'protocol-keyed fresh AMBE must still run cross-window absolute de-dupe before sequencer'
assert (
    'if (!p25Phase2ShouldEmitAmbeFrame(rx,' in pending_loop
    and 'pending.codewordAbsDibit' in pending_loop
    and 'SDRTrunk never re-plays a stream position' in pending_loop
), 'protocol-keyed pending AMBE must still run cross-window absolute de-dupe before sequencer'
assert (
    'duplicateInReadyBatch ||' in main
    and '!p25Phase2ShouldEmitAmbeFrame(rx,' in main
    and 'readyNeedsAbsoluteFallback' not in main
), 'ready speech must apply absolute de-dupe to every keyed frame, not fallback-only'
assert 'priorHash != hash' not in main, 'AMBE payload-hash de-dupe must not suppress valid repeated speech frames'
assert 'p25AmbeFrameHash' not in main, 'AMBE hash de-dupe helper should be removed'
assert 'rx.p25Phase2LastEmittedAbsDibit' not in main, 'Receiver-header-dependent absolute de-dupe field remains'
assert 'kPhase2CursorResetDibits' not in main, 'GUI 720ms/40ms overlapped windows must not reset AMBE de-dupe on a one-superframe rewind'
print('P25 Phase 2 absolute-position AMBE audio de-dupe regression: PASS')
