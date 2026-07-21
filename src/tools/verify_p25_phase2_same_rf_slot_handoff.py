#!/usr/bin/env python3
from pathlib import Path
src = Path(__file__).resolve().parents[1] / 'main.cpp'
text = src.read_text(encoding='utf-8', errors='replace')
checks = {
    'same RF handoff flag': 'sameRfPhase2SlotHandoff' in text,
    'unacquired requires no decoded frames': 'activeDiag.decodedFrames == 0' in text,
    'unacquired requires no MAC': 'activeDiag.phase2MacCrcValid == 0' in text,
    'unacquired requires no ESS': 'activeDiag.phase2EssKnown == false' in text,
    'same RF carrier guard': 'sameRfPhase2Carrier' in text and 'std::abs(liveFollowCarrierHz - followTg.lastVoiceFreqHz) <= 50.0' in text,
    'quiet call dwell steal': 'allowPhase2DwellSteal' in text,
    'handoff log': 'P25 Phase 2 same-RF slot handoff' in text,
    'phase2 post arm discard disabled': 'static constexpr int kP25Phase2PostArmDiscardWindows = 0;' in text,
    'retune pre-arm discard disabled': 'static constexpr int kP25RetunePreArmDiscardWindows = 0;' in text,
}
missing=[name for name,ok in checks.items() if not ok]
if missing:
    raise SystemExit('P25 Phase 2 same-RF slot handoff regression: FAIL missing '+', '.join(missing))
print('P25 Phase 2 same-RF slot handoff regression: PASS')
