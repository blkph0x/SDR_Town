#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]
main = (root / 'main.cpp').read_text(encoding='utf-8', errors='replace')

checks = [
    ('RollingIqWindow has decode cursor', 'lastDecodeAbsolute' in main and 'decodeAbsoluteKnown' in main),
    ('RollingIqWindow exposes takeUndecoded', 'takeUndecoded(size_t maxSamples' in main),
    ('GUI worker decodes undecoded tail with overlap', 'iq = rolling.takeUndecoded(maxDecodeChunk, decodeOverlap, iqStartAbsolute, iqStartAbsoluteKnown' in main),
    ('No Phase 2 path re-decodes entire rolling buffer', 'iq = rolling.samples;' not in main),
    ('Phase 2 cadence stays near-live for continuous audio', 'kP25Phase2VoiceDecodeCadenceMs = 10' in main),
]
failed = [name for name, ok in checks if not ok]
if failed:
    raise SystemExit('P25 Phase 2 incremental rolling decode regression failed: ' + ', '.join(failed))
print('P25 Phase 2 incremental rolling decode regression: PASS')

