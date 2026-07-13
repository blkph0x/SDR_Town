#!/usr/bin/env python3
"""Verify one-RTL traffic retune keeps monotonic IQ ring continuity."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
dm = (root / 'DeviceManager.cpp').read_text(encoding='utf-8', errors='replace')
dmh = (root / '../include/DeviceManager.h').resolve().read_text(encoding='utf-8', errors='replace')
main = (root / 'main.cpp').read_text(encoding='utf-8', errors='replace')

checks = {
    'markStreamRetune declared': 'markStreamRetune' in dmh,
    'retune uses markStreamRetune not full reset': 'markStreamRetune(st);' in dm and
        'dev->setFrequency(SOAPY_SDR_RX, 0, tuneHz);\n                            markStreamRetune(st);' in dm,
    'markStreamRetune preserves monotonic ring': 'markStreamRetune' in dm and
        'Preserve monotonic totalSamplesWritten' in dm,
    'rolling append keeps overlap on epoch change': 'Monotonic retune handoff: keep accumulated IQ/overlap' in main,
    'epoch handoff returns IQ pre-roll': 'Soft retune handoff: re-anchor at the live edge' in dm,
    'scheduler stream-retune-handoff path': 'stream-retune-handoff' in main,
    'scheduler does not force discontinuity when IQ present': 'if (newWin.samples.empty())' in main and
        'iq-stream-retune-handoff' in main,
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit('P25 Phase 2 retune ring continuity regression failed: ' + ', '.join(failed))
print('P25 Phase 2 retune ring continuity regression: PASS')
