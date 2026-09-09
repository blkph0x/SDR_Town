#!/usr/bin/env python3
"""Verify post-emit Phase-2 TDMA re-lock decode window fixes."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
from p25_orchestration_sources import orchestration_source_text
main = orchestration_source_text()

checks = {
    'no one-rtl auto sustain': 'p25IndependentTrafficSource && rx.p25TrafficRetunesPrimary && rx.p25VoiceMaskParamsKnown' not in main,
    'stable superframe gate helper': 'p25Phase2HasStableSuperframeLockLocked' in main,
    'wide reacquire helper': 'p25Phase2NeedsWideReacquireWindowLocked' in main,
    'speaker sustain gated on superframe or session': (
        'p25Phase2HasStableSuperframeLockLocked(rx)' in main and
        'p25Phase2SessionHadVoiceLock(rx)' in main
    ),
    'wide reacquire uses acquire chunk': (
        'if (wideReacquireWindow || maskEpochRepairWindow)' in main and
        (
            'plan.maxChunkSeconds = kP25Phase2VoiceDecodeUnacquiredAcquireFreshSeconds' in main
            or 'plan.maxChunkSeconds = kP25Phase2VoiceDecodeFirstColdEyeSeconds' in main
        ) and
        'plan.maxChunkSeconds = kP25Phase2VoiceDecodeAcquireChunkSeconds' in main
    ),
    'takeUndecoded preserves overlap context': 'firstNew + maxSamples' in main,
    'overlap context comment': 'context=0 windows after' in main,
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit('P25 Phase 2 post-emit reacquire regression failed: ' + ', '.join(failed))
print('P25 Phase 2 post-emit reacquire regression: PASS')
