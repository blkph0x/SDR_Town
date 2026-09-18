#!/usr/bin/env python3
"""DEC-0058: latched selected-dominant dual-slot (catch-up sizes → DEC-0060)."""

from pathlib import Path

root = Path(__file__).resolve().parents[2]
timing_cpp = (root / "src" / "P25VoiceTiming.cpp").read_text(encoding="utf-8", errors="ignore")
voice = (root / "src" / "P25VoiceDecode.cpp").read_text(encoding="utf-8", errors="ignore")

required = {
    "planner uses backlog catch-up on speaker path": (
        "kP25Phase2VoiceDecodeSpeakerBacklogCatchUpChunkSeconds" in timing_cpp
        and "if (backlogCatchUp)" in timing_cpp
        and timing_cpp.find("kP25Phase2VoiceDecodeSpeakerBacklogCatchUpChunkSeconds")
        < timing_cpp.find("kP25Phase2VoiceDecodeSpeakerSustainChunkSeconds",
                          timing_cpp.find("Speaker-sustain"))
    ),
    "latched selected-dominant security escape": (
        "latchedSelectedDominantClearContinuation" in voice
        and "dualSlotUntrustedGateEffective" in voice
    ),
    "latched selected-dominant feed escape": (
        "latchedSelectedDominantClearFeed" in voice
        and "dualSlotUntrustedNowEffective" in voice
    ),
}

failed = [name for name, ok in required.items() if not ok]
if failed:
    raise SystemExit("DEC-0058 regression failed: " + ", ".join(failed))
print("P25 Phase 2 DEC-0058 latched dual-slot continuation: PASS")
