#!/usr/bin/env python3
from pathlib import Path

text = Path("src/main.cpp").read_text(errors="ignore")

checks = {
    "short speaker tail grace": "static constexpr qint64 kP25Phase2SpeakerAudioTailGraceMs = 450;" in text,
    "trusted clear helper": "static bool p25Phase2BlockHasTrustedClearContext" in text,
    "plc helper": "static bool p25Phase2MayAppendPlcBlock" in text,
    "plc requires fresh target": "if (!p25Phase2WindowHasFreshTargetEvidence(out)) return false;" in text,
    "concealment dominant muted": 'return "phase2-concealment-dominant";' in text,
    "concealment without target muted": 'return "phase2-concealment-without-target";' in text,
    "fragmented tail muted": 'return "phase2-fragmented-tail-muted";' in text,
    "plc no longer sustain-only": "!rx.p25Phase2LastGoodPcm.empty() && rx.p25SessionState.sustain.hadSuccessfulEmit" not in text,
}

missing = [name for name, ok in checks.items() if not ok]
if missing:
    raise SystemExit("P25 Phase 2 inter-speech garbage gate regression: FAIL: " + ", ".join(missing))
print("P25 Phase 2 inter-speech garbage gate regression: PASS")
