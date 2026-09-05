#!/usr/bin/env python3
from pathlib import Path
import re
root = Path(__file__).resolve().parents[1]
main = (root / 'main.cpp').read_text(errors='ignore')
fn = re.search(r'static bool p25AmbeDecodeFrameLooksUsable\([^)]*\)\s*\{(?P<body>.*?)\n\}', main, re.S)
assert fn, 'p25AmbeDecodeFrameLooksUsable not found'
body = fn.group('body')
assert 'decoded.totalErrors > 3' in body, (
    'fresh AMBE proof should reject erasure-grade mbelib frames; '
    'the decode path must advance those slots as concealment/silence'
)
assert "decoded.message.find('R')" in body and "decoded.message.find('E')" in body, (
    'fresh AMBE proof should not treat repeat/erasure markers as authoritative speech'
)
assert 'return true;' in body, 'usable finite mbelib PCM should be accepted'
assert 'rms < 1.0e-6' not in body, 'valid low-energy AMBE concealment/silence frames must preserve 20 ms cadence'
assert 'peak > kP25DecodedAudioSafeMaxPeak' in body and 'rms > kP25DecodedAudioSafeMaxRms' in body, 'runaway PCM safety gate should remain'
decode = main[main.index('static bool p25DecodePhase2AmbeFrameToAudio'):main.index('static bool p25ProbePhase2AmbeFrameForDiagnostics')]
assert 'const bool emitAsSpeaker = speakerSafe;' in decode and 'frame.accepted = emitAsSpeaker;' in decode, (
    'non-fresh clear-call AMBE slots must still emit safe mbelib PCM so the speaker cadence does not stutter'
)
assert 'if (codecConcealment && emitAsSpeaker)' in decode and '++out.phase2ConcealmentFrames;' in decode, (
    'repeat/erasure/near-silent mbelib frames should be counted as quality debt, not hard speaker gaps'
)
assert 'p25Phase2AppendOppositeSlotSustainPlc' in main and 'Intentionally disabled' in main, (
    'opposite-slot invent PLC must stay disabled (SDRTrunk per-timeslot silence)'
)
print('strict fresh AMBE gate + concealment cadence + no invent-PLC: PASS')
