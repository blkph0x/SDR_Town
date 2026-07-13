#!/usr/bin/env python3
from pathlib import Path
root = Path(__file__).resolve().parents[1]
follow = (root / 'P25FollowStateMachine.cpp').read_text()
main = (root / 'main.cpp').read_text()
assert 'phase2RecentContinuation' in follow, 'watchdog must distinguish first acquisition from recent-activity reacquisition'
assert 'tdmaNoVcwSilenceMs = phase2RecentContinuation ? 10000 : 3500' in follow, 'no-VCW watchdog must hold longer after recent Phase-2 activity'
assert 'tdmaVcwNoSuperframeSilenceMs = phase2RecentContinuation ? 10000 : 3500' in follow, 'VCW/no-superframe watchdog must hold longer after recent Phase-2 activity'
assert 'silenceSinceSignalMs > tdmaNoVcwSilenceMs' in follow, 'no-VCW watchdog must use recent active/audio age before returning'
assert 'silenceSinceSignalMs > tdmaVcwNoSuperframeSilenceMs' in follow, 'VCW/no-superframe watchdog must use recent active/audio age before returning'
fn = main[main.index('static bool p25AmbeDecodeFrameLooksUsable'):main.index('static QString p25Phase2ValidationPath')]
assert 'decoded.totalErrors <=' not in fn, 'AMBE gate should not re-add hard total-error thresholds'
assert 'rms < 1.0e-6' not in fn, 'AMBE gate must not drop low-energy vocoder frames and break cadence'
assert 'peak > kP25DecodedAudioSafeMaxPeak' in fn and 'rms > kP25DecodedAudioSafeMaxRms' in fn, 'AMBE gate should only reject runaway PCM'
print('P25 Phase 2 recent-audio watchdog and relaxed AMBE quality regression: PASS')
