#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]
main = (root / "main.cpp").read_text(encoding="utf-8", errors="replace")
source = (root / "P25TranscriptSource.cpp").read_text(encoding="utf-8", errors="replace")
header = (root.parent / "include" / "P25TranscriptSource.h").read_text(encoding="utf-8", errors="replace")

checks = {
    "transcript voice label helper": "p25TranscriptVoiceLabelHz" in main,
    "helper prefers granted traffic voice": "rx.p25TrafficVoiceFreqHz > 0.0" in main.split("p25TranscriptVoiceLabelHz", 1)[1].split("static bool p25Phase2PendingAudioMatches", 1)[0],
    "live tap uses label helper": "p25TranscriptVoiceLabelHz(\n                                        rx, p25Audio.effectiveTargetFreqHz, demodFreq)" in main,
    "worker tap uses label helper": "p25TranscriptVoiceLabelHz(\n                        rx, result.audio.effectiveTargetFreqHz, result.targetFreqHz)" in main,
    "tap carries decoder target metadata": "decoderTargetFreqHz" in header and "decoderTargetFreqHz" in source,
    "metadata preserves canonical voice frequency": "voiceFreqHz" in source,
}

forbidden = [
    "const double sttFreq = (p25Audio.effectiveTargetFreqHz > 0.0)",
    "const double sttFreq = (result.audio.effectiveTargetFreqHz > 0.0)",
]

missing = [name for name, ok in checks.items() if not ok]
bad = [snippet for snippet in forbidden if snippet in main]
if missing or bad:
    parts = []
    if missing:
        parts.append("missing " + ", ".join(missing))
    if bad:
        parts.append("forbidden stale STT frequency path " + ", ".join(bad))
    raise SystemExit("P25 transcript grant voice frequency regression: FAIL " + "; ".join(parts))

print("P25 transcript grant voice frequency regression: PASS")
