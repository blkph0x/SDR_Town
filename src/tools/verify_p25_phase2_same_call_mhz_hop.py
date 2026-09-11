#!/usr/bin/env python3
from pathlib import Path
from p25_orchestration_sources import orchestration_source_text

src_text = orchestration_source_text()
text = src_text
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
    'resolved grant wide MHz hop limit': 'kP25SameCallResolvedGrantMaxMHzHopHz' in text,
    'out-of-source retune guard': 'sameCallCarrierNeedsRetune' in text,
    'defer target until retune commit log': 'not updating traffic target until retune commits' in text,
    'channel hop decoder reset log': 'decoder/cursor reset for TDMA re-acquisition' in text,
    'MHz hop debounce dwell': 'kP25SameCallMinMHzHopDwellMs' in text,
}
missing = [name for name, ok in checks.items() if not ok]
if missing:
    raise SystemExit('P25 Phase 2 same-call MHz hop regression: FAIL missing ' + ', '.join(missing))
print('P25 Phase 2 same-call MHz hop regression: PASS')
