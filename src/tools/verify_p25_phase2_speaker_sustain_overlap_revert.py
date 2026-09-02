#!/usr/bin/env python3
"""Guard speaker-sustain lattice: 120 ms eye, 40 ms overlap, 80 ms unique."""

from pathlib import Path

root = Path(__file__).resolve().parents[2]
main = (root / "src" / "main.cpp").read_text(encoding="utf-8", errors="ignore")

required = {
    "sustain chunk 120ms": "kP25Phase2VoiceDecodeSpeakerSustainChunkSeconds = 0.120" in main,
    "sustain minFresh 80ms unique": "kP25Phase2VoiceDecodeSpeakerSustainMinFreshSeconds = 0.080" in main,
    "sustain overlap 40ms": "kP25Phase2VoiceDecodeSpeakerSustainOverlapSeconds = 0.040" in main,
    "catch-up overlap 100ms restored": "kP25Phase2VoiceDecodeSpeakerCatchUpOverlapSeconds = 0.100" in main,
    "013000 chip evidence present": "20260829_013000" in main,
}

missing = [name for name, ok in required.items() if not ok]
if missing:
    raise SystemExit(
        "P25 Phase 2 sustain overlap revert regression FAILED: " + ", ".join(missing)
    )

print("P25 Phase 2 sustain overlap revert regression: PASS")
