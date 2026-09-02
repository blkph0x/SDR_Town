#!/usr/bin/env python3
"""Verify rolling IQ trim protects undecoded traffic-channel samples."""
from pathlib import Path

root = Path(__file__).resolve().parents[2]
main = (root / 'src' / 'main.cpp').read_text(encoding='utf-8', errors='replace')

checks = {
    'protected overlap samples': 'kProtectedOverlapSamples' in main,
    'soft trim uses effective decode cursor': (
        'const uint64_t decodeCursor = effectiveDecodeAbsolute();' in main
        and 'decodeCursor - startAbsolute' in main
    ),
    'soft trim preserves pre-roll before cursor': (
        'decodeHeadSamples > kProtectedOverlapSamples' in main
        and 'decodeHeadSamples - kProtectedOverlapSamples' in main
    ),
    'old inverted trim math is gone': 'samples.size() - protectedPrefixSamples' not in main,
    'allow temporary oversize buffer': 'if (drop == 0)' in main and 'const size_t hardCap = maxSamples + (maxSamples / 2);' in main,
    'active rolling window seconds': 'kP25Phase2VoiceDecodeActiveRollingSeconds' in main,
    'effective rolling window helper': 'p25Phase2EffectiveRollingWindowSeconds' in main,
    'speaker tiny chunks need stable lock': (
        'phase2StableSuperframeLock && phase2SessionHadVoiceLock' in main and
        'const bool speakerSustainEligible =' in main
    ),
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit('P25 Phase 2 rolling trim continuity regression failed: ' + ', '.join(failed))
print('P25 Phase 2 rolling trim continuity regression: PASS')
