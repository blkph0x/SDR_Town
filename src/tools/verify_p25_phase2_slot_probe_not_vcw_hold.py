from pathlib import Path
s = (Path(__file__).resolve().parents[1] / 'main.cpp').read_text(errors='ignore')
assert 'currentSlotHasUsefulAudio' in s
assert 'decoded > 0 && voiceDiag.audioSamples > 0' in s, 'GUI slot hold should use any current accepted PCM, not stale VCW activity'
assert 'diag.decodedFrames > 0 && diag.audioSamples > 0' in s, 'CLI slot hold should use any current accepted PCM'
assert 'p25AutoFollowLastActiveMs' in s, 'follow-active timestamp still exists for watchdog/status'
assert 'recentSlotAudio' not in s, 'slot probe must not use stale follow-active timestamp as audio proof'
assert 'cliLastPhase2AudioMs' not in s, 'CLI must not hold slot from stale audio timestamp'
assert 'mere VCW presence as proof' in s
print('P25 Phase 2 slot-probe no-VCW-only hold regression: PASS')
