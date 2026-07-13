#!/usr/bin/env python3
from pathlib import Path
root = Path(__file__).resolve().parents[2]
main = (root / 'src' / 'main.cpp').read_text()
recv = (root / 'include' / 'Receiver.h').read_text()
checks = {
    'app_retuned_primary_flag': 'bool p25IndependentTrafficRetunedPrimary = false;' in main,
    'receiver_retuned_primary_flag': 'bool p25TrafficRetunesPrimary = false;' in recv,
    'set_receiver_flag': 'trafficRx->p25TrafficRetunesPrimary = source.retunesPrimary;' in main,
    'set_app_flag': 'p25IndependentTrafficRetunedPrimary = source.retunesPrimary;' in main,
    'capture_flag_before_clear': 'const bool trafficRetunedPrimary = p25IndependentTrafficRetunedPrimary;' in main,
    'retune_on_release': 'if (trafficRetunedPrimary)' in main and 'tuneP25Path(ccHz)' in main,
    'dont_claim_if_retune_fails': 'not claiming CC monitor is active' in main,
    'manual_monitor_clears_flag': 'Monitoring muted P25 control channel target=' in main and 'p25IndependentTrafficRetunedPrimary = false;' in main,
}
failed = [k for k,v in checks.items() if not v]
if failed:
    raise SystemExit('FAILED: ' + ', '.join(failed))
print('P25 one-RTL return-to-control regression: PASS')
