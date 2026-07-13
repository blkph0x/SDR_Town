from pathlib import Path
root = Path(__file__).resolve().parents[2]
main = (root / 'src' / 'main.cpp').read_text()
follow_cpp = (root / 'src' / 'P25FollowStateMachine.cpp').read_text()
follow_h = (root / 'include' / 'P25FollowStateMachine.h').read_text()
recv_h = (root / 'include' / 'Receiver.h').read_text()
assert 'p25Phase2PendingAudioSampleCount' in main
assert 'kP25Phase2UnknownGrantAudioProbeMinSamples = 1920u' in main
assert 'kP25Phase2UnknownGrantAudioProbeMinFrames = 2' in main
assert 'out.decodedFrames = 0;\n        out.waitingForClearGrant = true' not in main
assert 'rx.p25VoiceDiagnostics.updatedMs' in main
assert 'updatedMs = 0' in recv_h
assert 'diagUpdatedMs = 0' in follow_h
assert 'diagnosticFresh' in follow_cpp
assert 'phase2SuperframeBursts >= 4' in follow_cpp
assert 'tdmaPartialEpochNoProgress' in follow_cpp
print('P25 Phase 2 unknown-audio queue/watchdog regression: PASS')
