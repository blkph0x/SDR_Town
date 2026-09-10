#!/usr/bin/env python3
"""Guard P25 Phase-2 speaker pacing against stale fill-percent flow control."""

from pathlib import Path


root = Path(__file__).resolve().parents[2]
from p25_orchestration_sources import orchestration_source_text
main = orchestration_source_text()

push = main.split("size_t pushP25LiveStreamingAudio", 1)[1].split(
    "size_t pushP25SpeakerAudio", 1
)[0]
bridge = main.split("size_t pushP25Phase2PlayoutBridge", 1)[1].split(
    "size_t p25TopUpSpeakerPlaybackRing", 1
)[0]

checks = {
    "live push uses exact queued samples": "engine->getRingQueuedSamples()" in push,
    "live push keeps fill percent diagnostic-only": "(void)ringFillPercent;" in push,
    "live push does not reconstruct samples from percent": "ringFillPercent / 100.0" not in push,
    "live push does not override queued samples from percent": "queuedNow = static_cast<size_t>" not in push,
    "bridge uses exact queued samples": "engine->getRingQueuedSamples()" in bridge,
    "bridge keeps fill percent diagnostic-only": "(void)ringFillPercent;" in bridge,
    "bridge does not reconstruct samples from percent": "ringFillPercent / 100.0" not in bridge,
    "bridge does not override queued samples from percent": "queuedNow = static_cast<size_t>" not in bridge,
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit(
        "P25 Phase 2 speaker sample-queue pacing regression failed: "
        + ", ".join(failed)
    )

print("P25 Phase 2 speaker sample-queue pacing regression: PASS")
