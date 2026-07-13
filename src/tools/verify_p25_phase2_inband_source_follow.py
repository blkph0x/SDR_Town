#!/usr/bin/env python3
"""Static regression for Phase 2 traffic sourcing.

sdrtrunk allocates an independent TRAFFIC channel source whose source
configuration is set to the granted downlink frequency.  This single-receiver
build uses either an independent traffic receiver (preferred) or a physical
RF retune when the traffic source is disabled.  In-band DDC on the shared
control receiver is intentionally not used because field captures show zero
CQPSK acquisition on that path.
"""
from pathlib import Path
root = Path(__file__).resolve().parents[1]
main = (root / 'main.cpp').read_text(errors='ignore')
checks = {
    'inband predicate retained for diagnostics': 'p25TrafficInCurrentSamplePassband' in main and 'sampleRateHz * 0.42' in main,
    'single receiver retune note': 'forcing physical retune' in main,
    'traffic in passband variable': 'trafficInCurrentPassband' in main,
    'always retunes granted voice frequency': 'if (!tuneP25Path(followTg.lastVoiceFreqHz)) return false;' in main,
    'in-band shortcut removed': 'in-band auto-follow' not in main,
    'force retune log present': 'forcing physical retune' in main,
    'arm pins voice target': 'rx.freqHz = tg.lastVoiceFreqHz' in main,
}
failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit('P25 Phase 2 traffic-source follow regression FAIL: ' + ', '.join(failed))
print('P25 Phase 2 traffic-source follow regression: PASS')
