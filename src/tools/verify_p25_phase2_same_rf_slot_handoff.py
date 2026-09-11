#!/usr/bin/env python3
from pathlib import Path
from p25_orchestration_sources import orchestration_source_text
src_text = orchestration_source_text()
text = src_text
checks = {
    'same RF handoff flag': 'sameRfPhase2SlotHandoff' in text,
    'unacquired requires no decoded frames': 'activeDiag.decodedFrames == 0' in text,
    'unacquired requires no MAC': 'activeDiag.phase2MacCrcValid == 0' in text,
    'unacquired requires no ESS': 'activeDiag.phase2EssKnown == false' in text,
    'same RF carrier guard': 'sameRfPhase2Carrier' in text and 'std::abs(liveFollowCarrierHz - followTg.lastVoiceFreqHz) <= 50.0' in text,
    'same RF steal dwell is long enough for late clear voice': 'kP25Phase2SameRfUnacquiredSlotStealMs = 24000' in text,
    'same RF slot handoff does not preempt solely on incoming clear': 'incomingClearGrantForHandoff' not in text,
    'same RF clear preempt waits for long dwell or silence': (
        'currentVoiceSilent ||\n                     dwellMs >= kP25Phase2SameRfUnacquiredSlotStealMs' in text
    ),
    'same RF metadata switch requires silence or unacquired long dwell': (
        'currentVoiceSilent ||\n                     (currentVoiceUnacquired &&\n                      dwellMs >= kP25Phase2SameRfUnacquiredSlotStealMs)' in text
    ),
    'quiet call dwell steal': 'allowPhase2DwellSteal' in text,
    'handoff log': 'P25 Phase 2 same-RF slot handoff' in text,
    'phase2 post arm discard disabled': 'constexpr int kP25Phase2PostArmDiscardWindows = 0;' in text,
    'retune pre-arm discard disabled': 'constexpr int kP25RetunePreArmDiscardWindows = 0;' in text,
}
missing=[name for name,ok in checks.items() if not ok]
if missing:
    raise SystemExit('P25 Phase 2 same-RF slot handoff regression: FAIL missing '+', '.join(missing))
print('P25 Phase 2 same-RF slot handoff regression: PASS')
