#!/usr/bin/env python3
from pathlib import Path
main = (Path(__file__).resolve().parents[1] / 'main.cpp').read_text(errors='ignore')
assert 'currentSlotHasUsefulAudio' in main, 'GUI slot probe should hold slot after current accepted PCM'
assert 'decoded > 0 && voiceDiag.audioSamples > 0' in main, 'GUI hold must suppress flips after any accepted PCM, not wait for a full speaker block'
assert 'cliCurrentSlotHasUsefulAudio' in main, 'CLI slot probe should use the same current-audio guard'
assert 'diag.decodedFrames > 0 && diag.audioSamples > 0' in main, 'CLI hold must suppress flips after any accepted PCM'
assert 'recentSlotAudio' not in main, 'stale recent-active audio hold must not suppress wrong-slot probes'
assert 'cliLastPhase2AudioMs' not in main, 'CLI must not hold slot from stale audio timestamp'
print('P25 Phase 2 current-audio slot-probe hold regression: PASS')
