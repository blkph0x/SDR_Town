#!/usr/bin/env python3
"""Verify session-level Phase-2 sustain state survives per-window diagnostic resets."""
from pathlib import Path

root = Path(__file__).resolve().parents[2]
main = (root / 'src' / 'main.cpp').read_text(encoding='utf-8', errors='replace')
session_h = (root / 'include' / 'P25ReceiverSession.h').read_text(encoding='utf-8', errors='replace')

checks = {
    'session sustain struct': 'P25Phase2SessionSustainState' in session_h,
    'session sustain member': 'P25Phase2SessionSustainState sustain' in session_h,
    'session voice lock helper': 'p25Phase2SessionHadVoiceLock' in main,
    'session sustain update': 'p25Phase2UpdateSessionSustainState' in main,
    'offset probe uses session lock': 'p25Phase2SessionHadVoiceLock(rx)' in main,
    'skip offset probe after emit': 'sustain.hadSuccessfulEmit' in main,
    'speaker sustain uses session lock': (
        'p25Phase2SessionHadVoiceLock(rx)' in main and
        'speakerSustainDecode' in main
    ),
    'wide reacquire disabled during session sustain': 'p25Phase2SessionSpeakerSustainActive' in main,
    'extended speaker tail grace': 'kP25Phase2SpeakerAudioTailGraceMs' in main,
    'force prime fresh audio': 'forcePrimeFromFreshAudio' in main,
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit('P25 Phase 2 session sustain regression failed: ' + ', '.join(failed))
print('P25 Phase 2 session sustain regression: PASS')
