#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]
main = (root / 'main.cpp').read_text(encoding='utf-8', errors='replace')
follow = (root / 'P25FollowStateMachine.cpp').read_text(encoding='utf-8', errors='replace')

checks = {
    'pending audio flush on return': 'p25PendingAudioFlushSeq.fetch_add(1' in main,
    'dsp flush seq drain': 'lastPendingAudioFlushSeq' in main,
    'warm standby window': 'kP25Phase2WarmStandbyMs = 5000' in main,
    'warm standby re-follow path': 'warm-standby-same-mhz' in main,
    'speaker follow hold bounded': 'kP25Phase2SpeakerFollowHoldMs = 2500' in main,
    'push returns consumed samples': 'return totalPushed;' in main,
    'emit gate required for inline audio log': 'p25Audio.phase2SpeakerGateReason == "emit"' in main,
    'phase2 acquisition progress hold': 'phase2AcquisitionProgress' in follow,
    'bounded speaker grace in follow sm': 'kSpeakerFollowGraceMs = 2500' in follow,
    'speaker alone not live voice': 'recentSpeakerOutput ||' not in follow,
    'extended hard timeout for phase2': 'hardTimeoutTuneMs = phase2Follow ? 45000 : 12000' in follow,
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit('P25 Phase 2 capture-fix regression failed: ' + ', '.join(failed))
print('P25 Phase 2 capture-fix regression: PASS')
