#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[2]
main = (root / 'src' / 'main.cpp').read_text(encoding='utf-8')
receiver = (root / 'include' / 'Receiver.h').read_text(encoding='utf-8')

required_main = [
    'QCheckBox* p25IndependentTrafficCheck = new QCheckBox("Traffic Source")',
    'p25IndependentTrafficEnabled = p25IndependentTrafficCheck->isChecked();',
    'selectP25IndependentTrafficSource',
    'startP25IndependentTrafficSource',
    'same-wideband-control-source',
    'dedicated-sdr-traffic-source',
    'P25 traffic source unavailable',
    'single-rtl-retune-traffic-source',
    'retunesPrimary',
    '(!p25FollowEnabled || p25IndependentTrafficActive) && p25CcInPassband',
    '&& !p25IndependentTrafficActive',
]
required_receiver = [
    'bool p25IndependentTrafficSource = false;',
    'double p25TrafficControlFreqHz = 0.0;',
    'double p25TrafficVoiceFreqHz = 0.0;',
    'int64_t p25TrafficLastGrantMs = 0;',
]
missing = [x for x in required_main if x not in main] + [x for x in required_receiver if x not in receiver]
if missing:
    print('P25 independent traffic source regression: FAIL')
    for item in missing:
        print(' missing:', item)
    raise SystemExit(1)

for file in [root/'src'/'main.cpp', root/'include'/'Receiver.h']:
    text = file.read_text(encoding='utf-8')
    bal = 0
    for ch in text:
        if ch == '{':
            bal += 1
        elif ch == '}':
            bal -= 1
        if bal < 0:
            print(f'{file}: brace balance went negative')
            raise SystemExit(1)
    if bal != 0:
        print(f'{file}: brace balance {bal}')
        raise SystemExit(1)

print('P25 independent traffic source regression: PASS')
