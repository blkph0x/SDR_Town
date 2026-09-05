#!/usr/bin/env python3
"""Guard speaker-sustain lattice: full eye, short context, half-window fresh."""

from pathlib import Path

root = Path(__file__).resolve().parents[2]
main = (root / "src" / "main.cpp").read_text(encoding="utf-8", errors="ignore")

required = {
    "speaker sustain full eye": "kP25Phase2VoiceDecodeSpeakerSustainChunkSeconds = 0.720" in main,
    "speaker sustain half-window fresh": "kP25Phase2VoiceDecodeSpeakerSustainMinFreshSeconds = 0.360" in main,
    "speaker sustain short context": "kP25Phase2VoiceDecodeSpeakerSustainOverlapSeconds = 0.060" in main,
    "catch-up overlap 100ms restored": "kP25Phase2VoiceDecodeSpeakerCatchUpOverlapSeconds = 0.100" in main,
    "223158 clear-audio evidence present": "20260904_223158" in main,
}

missing = [name for name, ok in required.items() if not ok]
if missing:
    raise SystemExit(
        "P25 Phase 2 sustain overlap revert regression FAILED: " + ", ".join(missing)
    )

print("P25 Phase 2 sustain overlap revert regression: PASS")
