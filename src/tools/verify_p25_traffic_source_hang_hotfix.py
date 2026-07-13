#!/usr/bin/env python3
from pathlib import Path
root = Path(__file__).resolve().parents[2]
main = (root / 'src' / 'main.cpp').read_text(errors='ignore')
fsm = (root / 'src' / 'P25FollowStateMachine.cpp').read_text(errors='ignore')
checks = {
    'rolling IQ tiny-fragment guard': 'freshSamples < minFreshSamples' in main and 'iq=256' in main,
    'minimum fresh IQ passed at GUI decode': '&phase2FreshIqSamples, &phase2ContextIqSamples, minDecodeFresh' in main,
    'final stale traffic generation drop': 'dropStaleTrafficAudio' in main and 'rx.p25TrafficGeneration != liveGen' in main,
    'slot probe uses active traffic receiver': 'probeRx = candidate' in main and 'auto& rx = *probeRx' in main,
    'no full-lock opposite-slot immediate decode': 'phase2AutoInvertGrantSlot' not in main and 'std::swap(out.phase2TargetVoiceCodewords' not in main,
    'explicit clear preempts stuck unknown': 'allowExplicitClearPreempt' in main and 'auto-follow-clear-preempt' in main,
    'no-acquire watchdog handles idle': 'P25FollowDiagCode::Idle' in fsm and 'snapshot.nowMs - snapshot.tunedAtMs > 4500' in fsm,
}
failed = [name for name, ok in checks.items() if not ok]
for name, ok in checks.items():
    print(f'{name}: {"PASS" if ok else "FAIL"}')
if failed:
    raise SystemExit('failed: ' + ', '.join(failed))
print('P25 traffic source hang/stale/slot hotfix regression: PASS')
