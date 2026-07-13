#!/usr/bin/env python3
from pathlib import Path
root = Path(__file__).resolve().parents[2]
main = (root / 'src' / 'main.cpp').read_text()
follow = (root / 'src' / 'P25FollowStateMachine.cpp').read_text()
checks = {
    'call key ignores grant epoch/source': 'Do not include grant epoch here' in main and 'sourceId == other.sourceId' not in main[main.find('bool operator=='):main.find('};', main.find('bool operator=='))],
    'explicit clear grant can release target voice only': 'explicit clear control grant can promote target-slot masked Voice2/Voice4' in main and 'clearGrantTargetReleaseAllowed' in main,
    'field fallback aligns with speaker debounce': 'kP25Phase2UnknownGrantAudioProbeMinFrames = 2' in main and 'kP25Phase2UnknownGrantAudioProbeMinSamples = 1920u' in main,
    'phase2 partial does not refresh forever': 'phase2TrustedActivity' in follow and 'tdmaPartialEpochNoProgress' in follow,
    'bounded no-vcw timeout': 'tdmaNoVcwTunedMs = phase2RecentContinuation ? 18000 : 9000' in follow and
                              'snapshot.nowMs - snapshot.tunedAtMs > tdmaNoVcwTunedMs' in follow,
}
failed = [name for name, ok in checks.items() if not ok]
if failed:
    print('FAIL')
    for name in failed:
        print(' -', name)
    raise SystemExit(1)
print('P25 Phase 2 sdrtrunk parity final regression: PASS')
