#!/usr/bin/env python3
from pathlib import Path

text = Path("src/main.cpp").read_text(errors="ignore")

checks = {
    "bounded speaker tail grace": "static constexpr qint64 kP25Phase2SpeakerAudioTailGraceMs = 2500;" in text,
    "trusted clear helper": "static bool p25Phase2BlockHasTrustedClearContext" in text,
    "plc helper": "static bool p25Phase2MayAppendPlcBlock" in text,
    "plc requires trusted clear": "if (!trustedClear) return false;" in text,
    "plc uses bounded clear tail": "return p25Phase2AudioTailGraceActive(rx);" in text,
    "concealment dominant muted": 'return "phase2-concealment-dominant";' in text,
    "concealment without target muted": 'return "phase2-concealment-without-target";' in text,
    "fragmented tail muted": 'return "phase2-fragmented-tail-muted";' in text,
    "fragmented tail no longer mutes on feed gap alone": "out.phase2FeedGaps > 0 ||" not in text,
    "plc no longer sustain-only": "!rx.p25Phase2LastGoodPcm.empty() && rx.p25SessionState.sustain.hadSuccessfulEmit" not in text,
    "phase2 empty-audio buffer preserve requires trusted clear context": "hasTrustedPhase2ClearContext" in text and "rx.p25VoiceMaskParamsKnown ||" not in text,
}

missing = [name for name, ok in checks.items() if not ok]
if missing:
    raise SystemExit("P25 Phase 2 inter-speech garbage gate regression: FAIL: " + ", ".join(missing))
print("P25 Phase 2 inter-speech garbage gate regression: PASS")
