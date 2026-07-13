#!/usr/bin/env python3
"""Regression: Phase 2 auto-follow must not use broken in-band DDC on the shared CC receiver."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
main = (root / 'main.cpp').read_text(errors='ignore')
checks = {
    'in-band shortcut removed': 'in-band auto-follow' not in main,
    'physical retune forced for in-passband': 'forcing physical retune' in main,
    'legacy retune path retained': 'if (!tuneP25Path(followTg.lastVoiceFreqHz)) return false;' in main,
    'traffic source path retained': 'startP25IndependentTrafficSource' in main,
    'traffic source disabled notice': 'p25-traffic-source-disabled' in main,
    'phase2 skips wideband ddc unless centered': 'phase2Traffic && !centeredOnVoice' in main,
    'clear-trusted dwell steal grace': 'kP25Phase2ClearTrustedSilentDwellStealGraceMs' in main,
    'symbol rate log uses 6000': 'cfgSymbolRate=6000Hz' in main,
}
failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit('P25 Phase 2 force-retune follow regression FAIL: ' + ', '.join(failed))
print('P25 Phase 2 force-retune follow regression: PASS')
