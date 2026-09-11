#!/usr/bin/env python3
from pathlib import Path
from p25_orchestration_sources import orchestration_source_text
s = orchestration_source_text()
checks = [
    'gP25RecentExplicitEncryptedPhase2Grants',
    'auto-skip-p2-unknown-after-current-encrypted',
    'explicit encrypted grant for the same TG/channel/frequency',
    'auto-follow-blocked-manual',
]
missing = [c for c in checks if c not in s]
if missing:
    raise SystemExit('missing: ' + ', '.join(missing))
print('P25 Phase 2 current-call encrypted hold/manual-block diagnostic regression: PASS')

