#!/usr/bin/env python3
from pathlib import Path
root = Path(__file__).resolve().parents[2]
main = (root / 'src' / 'main.cpp').read_text(errors='ignore')
fsm = (root / 'src' / 'P25FollowStateMachine.cpp').read_text(errors='ignore')
receiver = (root / 'include' / 'Receiver.h').read_text(errors='ignore')
voice_nominal_body = main.split('static double p25Phase2VoiceSchedulerNominalHz', 1)[1].split('static double p25Phase2TrafficSourceCenterHz', 1)[0]
checks = {
    'rolling IQ tiny-fragment guard': 'freshSamples < minFreshSamples' in main and 'p25Phase2EffectiveMinFreshSamples' in main,
    'minimum fresh IQ passed at GUI decode': '&phase2FreshIqSamples, &phase2ContextIqSamples, minDecodeFresh' in main,
    'final stale traffic generation drop': 'dropStaleTrafficAudio' in main and 'rx.p25TrafficGeneration != result.trafficGeneration' in main and 'liveGen != result.trafficGeneration' not in main,
    'slot probe uses active traffic receiver': 'probeRx = candidate' in main and 'auto& rx = *probeRx' in main,
    'no full-lock opposite-slot immediate decode': 'phase2AutoInvertGrantSlot' not in main and 'std::swap(out.phase2TargetVoiceCodewords' not in main,
    'explicit clear preempts stuck unknown': 'allowExplicitClearPreempt' in main and 'auto-follow-clear-preempt' in main,
    'no-acquire watchdog handles idle': 'P25FollowDiagCode::Idle' in fsm and 'untrustedClearAcquireLimitMs' in fsm and '6500' in fsm,
    'one-rtl low-if source center is tracked': 'p25TrafficSourceCenterFreqHz' in receiver and 'p25Phase2TrafficSourceCenterHz' in main,
    'wideband traffic scheduler uses selected voice carrier': 'p25TrafficVoiceFreqHz' in voice_nominal_body and 'p25TrafficRetunesPrimary' not in voice_nominal_body,
    'stream handoff reset is non-blocking': 'tryResetP25TrafficSession' in receiver and 'tryResetP25TrafficSessionNonBlocking' in main and 'stream-retune-reset-deferred' in main,
    'stream handoff clears rolling iq': 'clearing rolling IQ and restarting traffic decoder' in main and 'rolling.clear();' in main,
    'follow watchdog handles missing diagnostics': 'p25-follow-no-diag-watchdog' in main and 'P25VoiceDiagCode::NoSync' in main,
}
failed = [name for name, ok in checks.items() if not ok]
for name, ok in checks.items():
    print(f'{name}: {"PASS" if ok else "FAIL"}')
if failed:
    raise SystemExit('failed: ' + ', '.join(failed))
print('P25 traffic source hang/stale/slot hotfix regression: PASS')
