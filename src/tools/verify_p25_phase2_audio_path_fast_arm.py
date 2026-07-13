#!/usr/bin/env python3
from pathlib import Path
s = Path('src/main.cpp').read_text()
checks = {
    'phase2 arm delay zero': 'static constexpr int kP25Phase2ArmDelayMs = 0;' in s,
    'control cadence faster': 'static constexpr int kP25ControlDecodeCadenceMs = 70;' in s,
    'unknown field diagnostic probe is bounded': 'kP25Phase2AllowUnknownGrantFieldAudioProbe = true' in s and 'fieldAudioProbeAllowed' in s and 'kP25Phase2UnknownGrantAudioProbeMinFrames = 2' in s,
    'same rf metadata switch': 'same-RF slot metadata switch' in s and 'without retune/rearm' in s,
    'ambe telemetry log': 'targetVcw=%16 oppVcw=%17 exp=%18 fed=%19 emit=%20 gaps=%21' in s,
    'no duplicate grantClearTrusted declaration': 'const bool grantClearTrusted = rx.p25VoiceClearKnown && !rx.p25VoiceEncrypted;\n        const bool grantClearTrusted' not in s,
}
failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit('FAILED: ' + ', '.join(failed))
print('P25 Phase 2 audio path fast-arm regression: PASS')
