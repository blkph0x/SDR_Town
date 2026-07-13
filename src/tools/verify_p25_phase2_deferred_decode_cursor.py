#!/usr/bin/env python3
"""Verify rolling IQ decode cursor advances only after worker/inline decode."""
from pathlib import Path

root = Path(__file__).resolve().parents[2]
main = (root / 'src' / 'main.cpp').read_text(encoding='utf-8', errors='replace')
take = main[main.find('std::vector<std::complex<float>> takeUndecoded'):main.find('static size_t p25Phase2UndecodedBacklogSamples')]

checks = {
    'takeUndecoded exposes decode end absolute': 'outDecodeEndAbsolute' in take,
    'takeUndecoded does not advance lastDecodeAbsolute': 'lastDecodeAbsolute =' not in take,
    'rolling window commits decode absolute': 'commitDecodeAbsolute' in main,
    'rolling window rolls back submitted decode': 'rollbackSubmittedDecode' in main,
    'rolling window tracks submitted decode end': 'markDecodeSubmitted' in main,
    'worker result commits rolling cursor': 'commitDecodeAbsolute(result.iqDecodeEndAbsolute)' in main,
    'single pending voice job': 'kP25VoiceDecodeMaxPendingJobs = 1' in main,
    'single speaker pending voice job': 'kP25VoiceDecodeMaxPendingJobsSpeaker = 1' in main,
    'backlog catch-up disables tiny speaker chunks': 'backlogCatchUp' in main and 'speakerSustainDecode' in main,
    'iq pull pauses during decode backlog': 'syncAbsolute = rolling.lastDecodeAbsolute' in main,
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit('P25 Phase 2 deferred decode cursor regression failed: ' + ', '.join(failed))
print('P25 Phase 2 deferred decode cursor regression: PASS')
