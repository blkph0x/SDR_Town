#!/usr/bin/env python3
"""Guard speaker-sustain lattice: cold eye is large; locked speaker hops are near-live."""

from pathlib import Path

root = Path(__file__).resolve().parents[2]
main = (root / "src" / "main.cpp").read_text(encoding="utf-8", errors="ignore")

required = {
    "speaker sustain near-live hop": "kP25Phase2VoiceDecodeSpeakerSustainChunkSeconds = 0.080" in main,
    "speaker sustain frame-pair fresh": "kP25Phase2VoiceDecodeSpeakerSustainMinFreshSeconds = 0.040" in main,
    "speaker sustain bounded context": "kP25Phase2VoiceDecodeSpeakerSustainOverlapSeconds = 0.080" in main,
    "catch-up overlap 100ms restored": "kP25Phase2VoiceDecodeSpeakerCatchUpOverlapSeconds = 0.100" in main,
    "cold eye remains two superframes": "kP25Phase2VoiceDecodeFirstColdEyeSeconds = 0.720" in main,
    "worker lag rationale present": "fall behind live traffic" in main,
}

missing = [name for name, ok in required.items() if not ok]
if missing:
    raise SystemExit(
        "P25 Phase 2 sustain overlap revert regression FAILED: " + ", ".join(missing)
    )

print("P25 Phase 2 sustain overlap revert regression: PASS")
