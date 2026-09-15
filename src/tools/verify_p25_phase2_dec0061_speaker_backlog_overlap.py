#!/usr/bin/env python3
"""DEC-0061: speaker backlog catch-up 240+280 (pace RF without thinning DEC-0024 overlap)."""

from pathlib import Path

root = Path(__file__).resolve().parents[2]
timing_h = (root / "include" / "P25VoiceTiming.h").read_text(encoding="utf-8", errors="ignore")
timing_cpp = (root / "src" / "P25VoiceTiming.cpp").read_text(encoding="utf-8", errors="ignore")

required = {
    "catch-up fresh 240 ms": (
        "kP25Phase2VoiceDecodeSpeakerBacklogCatchUpChunkSeconds = 0.240" in timing_h
    ),
    "catch-up minFresh 160 ms": (
        "kP25Phase2VoiceDecodeSpeakerBacklogCatchUpMinFreshSeconds = 0.160" in timing_h
    ),
    "catch-up overlap 280 ms (DEC-0024)": (
        "kP25Phase2VoiceDecodeSpeakerBacklogCatchUpOverlapSeconds = 0.280" in timing_h
    ),
    "not still thin 80 ms overlap": (
        "kP25Phase2VoiceDecodeSpeakerBacklogCatchUpOverlapSeconds = 0.080" not in timing_h
    ),
    "planner still branches backlogCatchUp on speaker path": (
        "kP25Phase2VoiceDecodeSpeakerBacklogCatchUpChunkSeconds" in timing_cpp
        and "if (backlogCatchUp)" in timing_cpp
    ),
    "DEC-0061 cited": "DEC-0061" in timing_h and "DEC-0061" in timing_cpp,
    "idle sustain still 80+280": (
        "kP25Phase2VoiceDecodeSpeakerSustainChunkSeconds = 0.080" in timing_h
        and "kP25Phase2VoiceDecodeSpeakerSustainOverlapSeconds = 0.280" in timing_h
    ),
}

failed = [name for name, ok in required.items() if not ok]
if failed:
    raise SystemExit("DEC-0061 regression failed: " + ", ".join(failed))
print("P25 Phase 2 DEC-0061 speaker backlog catch-up 240+280: PASS")
