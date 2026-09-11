#!/usr/bin/env python3
"""Guard speaker-sustain lattice: cold eye is large; locked speaker hops are near-live."""

from pathlib import Path

root = Path(__file__).resolve().parents[2]
from p25_orchestration_sources import orchestration_source_text
main = orchestration_source_text()

required = {
    "speaker sustain near-live hop": "kP25Phase2VoiceDecodeSpeakerSustainChunkSeconds = 0.080" in main,
    "speaker sustain frame-pair fresh": "kP25Phase2VoiceDecodeSpeakerSustainMinFreshSeconds = 0.040" in main,
    "speaker sustain bounded context": "kP25Phase2VoiceDecodeSpeakerSustainOverlapSeconds = 0.280" in main,
    "no 180ms speaker catch-up constants (ISS-0003)": (
        "kP25Phase2VoiceDecodeSpeakerCatchUpChunkSeconds" not in main
        and "kP25Phase2VoiceDecodeSpeakerCatchUpMinFreshSeconds" not in main
        and "kP25Phase2VoiceDecodeSpeakerCatchUpOverlapSeconds" not in main
    ),
    "backlog catch-up overlap stays DEC-0009 280ms": "kP25Phase2VoiceDecodeBacklogCatchUpOverlapSeconds = 0.280" in main,
    "cold eye remains two superframes": "kP25Phase2VoiceDecodeFirstColdEyeSeconds = 0.720" in main,
    "worker lag rationale present": "fall behind live traffic" in main,
}

missing = [name for name, ok in required.items() if not ok]
if missing:
    raise SystemExit(
        "P25 Phase 2 sustain overlap revert regression FAILED: " + ", ".join(missing)
    )

print("P25 Phase 2 sustain overlap revert regression: PASS")
