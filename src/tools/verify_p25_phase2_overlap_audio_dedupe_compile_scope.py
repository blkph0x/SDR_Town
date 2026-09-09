#!/usr/bin/env python3
from pathlib import Path
from p25_orchestration_sources import orchestration_source_text
main = orchestration_source_text()
for n in ['phase2FreshIqSamples', 'phase2ContextIqSamples', 'takeUndecoded(maxDecodeChunk, decodeOverlap']:
    assert n in main, f'missing overlap compile-scope marker: {n}'
assert 'rx.p25Phase2LastEmittedAbsDibit' not in main, 'Receiver-header-dependent de-dupe field remains'
print('P25 Phase 2 overlap audio de-dupe compile-scope regression: PASS')
