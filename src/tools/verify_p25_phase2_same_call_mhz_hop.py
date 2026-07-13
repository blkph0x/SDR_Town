#!/usr/bin/env python3
from pathlib import Path

src = Path(__file__).resolve().parents[1] / 'main.cpp'
text = src.read_text(encoding='utf-8', errors='replace')
checks = {
    'same-call MHz hop fallthrough flag': 'sameCallVoiceMHzHop' in text,
    'MHz hop pending log': 'P25 auto-follow same-call MHz hop pending' in text,
    'scheduler voice nominal helper': 'p25Phase2VoiceSchedulerNominalHz' in text,
    'scheduler uses traffic voice nominal': 'p25VoiceAfcTargetHz(rx, p25Phase2VoiceSchedulerNominalHz(rx)' in text,
    'traffic offset clamp constant': 'kP25Phase2TrafficTargetOffsetMaxHz = 7500.0' in text,
    'offset reset helper': 'p25Phase2ResetTrafficTargetOffset' in text,
    'grant update MHz hop clamp': 'kP25SameCallGrantUpdateMaxMHzHopHz' in text,
    'sanitized same-call voice Hz helper': 'p25SanitizedSameCallFollowVoiceHz' in text,
    'MHz hop authorization helper': 'p25GrantAuthorizesSameCallVoiceMHzHop' in text,
    'ignore bogus grant MHz jump log': 'P25 auto-follow ignored same-call grant MHz jump' in text,
    'channel hop decoder reset log': 'with decoder/cursor reset for TDMA re-acquisition' in text,
    'MHz hop debounce dwell': 'kP25SameCallMinMHzHopDwellMs' in text,
}
missing = [name for name, ok in checks.items() if not ok]
if missing:
    raise SystemExit('P25 Phase 2 same-call MHz hop regression: FAIL missing ' + ', '.join(missing))
print('P25 Phase 2 same-call MHz hop regression: PASS')
