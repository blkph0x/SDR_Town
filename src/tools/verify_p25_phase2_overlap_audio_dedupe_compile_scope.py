#!/usr/bin/env python3
from pathlib import Path
main = (Path(__file__).resolve().parents[1] / 'main.cpp').read_text(errors='ignore')
for n in ['phase2FreshIqSamples', 'phase2ContextIqSamples', 'takeUndecoded(maxDecodeChunk, decodeOverlap']:
    assert n in main, f'missing overlap compile-scope marker: {n}'
assert 'rx.p25Phase2LastEmittedAbsDibit' not in main, 'Receiver-header-dependent de-dupe field remains'
print('P25 Phase 2 overlap audio de-dupe compile-scope regression: PASS')
