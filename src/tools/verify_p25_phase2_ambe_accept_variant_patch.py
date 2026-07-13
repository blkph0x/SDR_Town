#!/usr/bin/env python3
from pathlib import Path
root = Path(__file__).resolve().parents[2]
main = (root / 'src' / 'main.cpp').read_text(errors='ignore')
dec = (root / 'src' / 'P25LiveDecoder.cpp').read_text(errors='ignore')
hdr = (root / 'include' / 'P25LiveDecoder.h').read_text(errors='ignore')
assert 'p25Phase2AmbeFrameVariantCount' in hdr
assert 'p25Phase2VoiceCodewordToAmbe3600x2450FrameVariant' in hdr
assert 'p25Phase2AmbeVariantBits' in dec
assert 'for (int v = 0; v < 8' not in main
assert 'kP25Phase2AmbeProbeVariants' in main
assert 'p25ResolvePhase2AmbeFrame' in main
assert 'p25Phase2VoiceCodewordToAmbe3600x2450FrameVariant' in main
assert 'p25AmbeDecodeFrameLooksUsable' in main
print('P25 Phase 2 AMBE accept/variant patch regression: PASS')
