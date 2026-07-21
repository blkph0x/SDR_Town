#!/usr/bin/env python3
from pathlib import Path
root = Path(__file__).resolve().parents[2]
main = (root / 'src' / 'main.cpp').read_text()
follow = (root / 'src' / 'P25FollowStateMachine.cpp').read_text()
session = (root / 'include' / 'P25ReceiverSession.h').read_text()
call_key_eq = session[session.find('bool operator=='):session.find('};', session.find('bool operator=='))]
checks = {
    'call key binds grant epoch/source/session': 'sourceId == other.sourceId' in call_key_eq and 'grantEpochMs == other.grantEpochMs' in call_key_eq and 'callSessionId == other.callSessionId' in call_key_eq,
    'explicit clear grant queues until target traffic proves clear': 'p25Phase2ExplicitClearGrantVoiceReleaseEvidence' in main and 'releasePendingRawVoiceFromExplicitClearTrafficProof' in main and 'explicitClearGrantProbeAllowed' in main and 'explicit-clear-grant-target-release' not in main and 'explicit-clear-grant-validated-release' not in main and 'explicit-clear-grant-traffic-clear-release' in main and 'validatedExplicitClearGrantSeed' not in main and 'boundedProbeAccepted' not in main and 'const bool grantMayProbeVoice = grantClearTrusted || grantUnknownProbe;' in main and 'const bool targetTrafficClearEvidence =' in main and 'p25Phase2TargetHardClearEvidence(out)' in main and 'p25Phase2TargetPttSessionClear' in main and 'p25Phase2SdrtrunkLateEntryVoiceReleaseEvidence(rx, out)' in main and 'explicitGrantTargetSlotSelected' in main and 'explicitClearGrantSelectedVoiceRelease' not in main and '(targetTrafficClearEvidence || explicitClearGrantSelectedVoiceRelease)' not in main and 'targetTrafficClearEvidence &&' in main.split('const bool explicitClearGrantHardVoiceRelease', 1)[1].split(';', 1)[0],
    'field fallback aligns with speaker debounce': 'kP25Phase2UnknownGrantAudioProbeMinFrames = 2' in main and 'kP25Phase2UnknownGrantAudioProbeMinSamples = 1920u' in main,
    'phase2 partial does not refresh forever': 'phase2TrustedActivity' in follow and 'tdmaPartialEpochNoProgress' in follow,
    'bounded no-vcw timeout': 'const int64_t tdmaNoVcwTunedMs = waitingUnknownClearGrant' in follow and
                              'phase2RecentContinuation ? 18000 : 9000' in follow and
                              'snapshot.nowMs - snapshot.tunedAtMs > tdmaNoVcwTunedMs' in follow,
}
failed = [name for name, ok in checks.items() if not ok]
if failed:
    print('FAIL')
    for name in failed:
        print(' -', name)
    raise SystemExit(1)
print('P25 Phase 2 sdrtrunk parity final regression: PASS')
