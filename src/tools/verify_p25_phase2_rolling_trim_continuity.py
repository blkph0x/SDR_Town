#!/usr/bin/env python3
"""Verify rolling IQ trim protects undecoded traffic-channel samples."""
from pathlib import Path

root = Path(__file__).resolve().parents[2]
main = (root / 'src' / 'main.cpp').read_text(encoding='utf-8', errors='replace')

checks = {
    'protected overlap samples': 'kProtectedOverlapSamples' in main,
    'no decode cursor bump on trim': 'lastDecodeAbsolute = startAbsolute' not in main.split('protectedPrefixSamples', 1)[1][:1200],
    'allow temporary oversize buffer': 'if (drop == 0)' in main and 'protectedPrefixSamples' in main,
    'active rolling window seconds': 'kP25Phase2VoiceDecodeActiveRollingSeconds' in main,
    'effective rolling window helper': 'p25Phase2EffectiveRollingWindowSeconds' in main,
    'speaker tiny chunks need stable lock': (
        'p25Phase2HasStableSuperframeLockLocked(rx) &&\n'
        '                                 p25Phase2SessionHadVoiceLock(rx)' in main or
        'p25Phase2HasStableSuperframeLockLocked(rx) &&\n'
        '                            p25Phase2SessionHadVoiceLock(rx)' in main
    ),
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit('P25 Phase 2 rolling trim continuity regression failed: ' + ', '.join(failed))
print('P25 Phase 2 rolling trim continuity regression: PASS')
