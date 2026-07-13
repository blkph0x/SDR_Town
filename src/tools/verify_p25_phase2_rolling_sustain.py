#!/usr/bin/env python3
"""Verify P25 Phase 2 rolling-buffer sustain fix (cursor sync + gap stitch)."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
main = (root / 'main.cpp').read_text(encoding='utf-8', errors='replace')
dm_h = (root / '../include/DeviceManager.h').resolve().read_text(encoding='utf-8', errors='replace')
dm_cpp = (root / 'DeviceManager.cpp').read_text(encoding='utf-8', errors='replace')

checks = {
    'cursor sync API': 'syncReceiverCursorToAbsolute' in dm_h and 'syncReceiverCursorToAbsolute' in dm_cpp,
    'prepare rolling pull helper': 'p25Phase2PrepareRollingIqPull' in main,
    'sync before pull': main.count('p25Phase2PrepareRollingIqPull(mgr') >= 2,
    'gap stitch not replace': 'Do not replace accumulated IQ/context' in main,
    'sustain overlap guard': 'never emit a sustain chunk without overlap' in main,
    'empty rolling recovery preroll': 'setReceiverCursorBeforeLiveEdge(devIndex, rx, recoveryPreRoll)' in main,
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit('P25 Phase 2 rolling sustain regression failed: ' + ', '.join(failed))
print('P25 Phase 2 rolling sustain regression: PASS')
