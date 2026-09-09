#!/usr/bin/env python3
"""Regression: pending AMBE must not mix with dual-slot / post-emit live VCWs."""

from pathlib import Path

root = Path(__file__).resolve().parents[2]
from p25_orchestration_sources import orchestration_source_text
main = orchestration_source_text()
receiver_h = (root / "include" / "Receiver.h").read_text(encoding="utf-8", errors="ignore")
receiver_cpp = (root / "src" / "Receiver.cpp").read_text(encoding="utf-8", errors="ignore")

required = {
    "sticky call-had-speaker flag": "p25Phase2CallHadSpeakerAudio" in receiver_h
    and "rx.p25Phase2CallHadSpeakerAudio = true;" in main
    and "rx.p25Phase2CallHadSpeakerAudio = false;" in receiver_cpp,
    "context lock uses sticky": "rx.p25Phase2CallHadSpeakerAudio ||" in main
    and "contextAudioLockedOut" in main,
    "pending drain blocks dual-slot live mix": (
        "out.phase2OppositeVoiceCodewords > 0 &&" in main
        and "out.phase2TargetVoiceCodewords > 0" in main
        and "canDrainPendingRawVoiceThisWindow" in main
    ),
    "pending drain blocks post-emit live mix": (
        "p25Phase2CallHadSpeakerAudio ||" in main
        and "hadSuccessfulEmit) &&" in main
        and "out.phase2TargetVoiceCodewords > 0" in main
    ),
    "live-stream-preferred pending clear": "LiveStreamPreferred" in main
    and "live-stream-preferred" in main
    and "discardStalePendingWhenLivePreferred" in main,
    "drain owns canDrain gate": (
        "if (!canDrainPendingRawVoiceThisWindow())" in main
        and "discardStalePendingWhenLivePreferred();" in main
    ),
}

missing = [name for name, ok in required.items() if not ok]
if missing:
    raise SystemExit(
        "P25 Phase 2 pending/live mix gate regression FAILED: " + ", ".join(missing)
    )

print("P25 Phase 2 pending/live mix gate regression: PASS")
