#!/usr/bin/env python3
from pathlib import Path

from p25_orchestration_sources import orchestration_source_text
root = Path(__file__).resolve().parents[2]
main = orchestration_source_text()
main_text = main
session_text = (root / 'include' / 'P25ReceiverSession.h').read_text(
    encoding='utf-8', errors='replace')

assert 'P25Phase2AudioTailState' in session_text, 'missing audio tail tracker state'
assert 'p25Phase2FinalizeAudioTailState' in main_text, 'missing audio tail finalize helper'
assert 'phase2-stale-audio-tail' in main_text, 'missing stale audio tail speaker gate'
assert 'phase2-no-fresh-target-voice' in main_text, 'missing fresh target voice speaker gate'
assert 'phase2-no-fresh-feed' in main_text, 'missing fresh feed speaker gate'
assert 'p25Phase2EstablishedClearNoiseFeedAllowed' in main_text, 'missing established-clear noise feed guard'
assert 'p25Phase2ShouldFlushAudioTail' in main_text, 'missing audio tail flush helper'
assert 'decodedFrames > 0 && p25Audio.phase2EmittedPcmFrames > 0' not in main_text, 'stale push fallback remains in GUI path'
assert 'result.audio.decodedFrames > 0 && result.audio.phase2EmittedPcmFrames > 0' not in main_text, 'stale push fallback remains in worker path'
assert 'phase2AudioTailGraceActive' in main_text, 'missing tail grace push gate'
print('P25 Phase 2 no stale audio tail regression: PASS')

