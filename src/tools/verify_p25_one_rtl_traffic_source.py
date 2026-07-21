from pathlib import Path
root = Path(__file__).resolve().parents[2]
main = (root / 'src' / 'main.cpp').read_text(errors='ignore')
checks = {
    'selection has primary-retune flag': 'bool retunesPrimary = false;' in main,
    'one rtl source kind': 'single-rtl-retune-traffic-source' in main,
    'primary low-if retune happens': 'primaryRetuneSeq = mgr.setCenterFreq(source.deviceIndex, desiredCenterHz);' in main,
    'phase2 low-if center helper': 'p25Phase2LowIfTrafficCenterHz' in main,
    'one rtl log': 'P25 one-RTL traffic source retuned primary tuner' in main,
    'tooltip mentions one rtl retune': 'temporarily retunes the single tuner to traffic' in main,
    'source log branches on retune': 'source.retunesPrimary' in main,
}
failed = [name for name, ok in checks.items() if not ok]
if failed:
    print('P25 one-RTL traffic source regression: FAIL')
    for name in failed:
        print(' -', name)
    raise SystemExit(1)
print('P25 one-RTL traffic source regression: PASS')
