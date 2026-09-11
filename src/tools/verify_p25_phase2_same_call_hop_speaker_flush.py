#!/usr/bin/env python3
"""Verify same-call hops flush speaker pending+ring (20260810_221028 doubles/O-O-O)."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[2]
from p25_orchestration_sources import orchestration_source_text
main = orchestration_source_text()
receiver = (root / "include" / "Receiver.h").read_text(encoding="utf-8", errors="replace")

def voice_reset_clears_ring(text: str) -> bool:
    # After multi-TU split, the first p25VoiceResetPending site is not the ring-clear path.
    # Require a reset-pending block that both applies the reset and peeks the engine.
    for match in re.finditer(r"if \(rx\.p25VoiceResetPending\)", text):
        window = text[match.start() : match.start() + 900]
        if "tryApplyP25VoiceResetLocked(rx)" in window and "peekAudioEngineIfReady()" in window:
            return True
    return False

checks = {
    "speaker clear pending flag": "p25Phase2SpeakerPlaybackClearPending" in receiver
    and "p25Phase2SpeakerPlaybackClearPending" in main,
    "in-source hop sets speaker clear": (
        "trafficCarrierChanged && commitSameCallMetadataInPlace" in main
        and main.find("p25Phase2SpeakerPlaybackClearPending = true")
        > main.find("trafficCarrierChanged && commitSameCallMetadataInPlace")
    ),
    "MHz hop sets speaker clear": "Same-call MHz hop: flush speaker jitter/ring" in main,
    "DSP drains speaker clear pending": (
        "if (rx.p25Phase2SpeakerPlaybackClearPending)" in main
        and "P25 speaker playback cleared after same-call hop" in main
    ),
    "hop log mentions speaker playback": "speaker playback queue were reset" in main,
    "voice-reset also clears ring": voice_reset_clears_ring(main),
    "context lock-out after first emit": (
        "contextAudioLockedOut" in main
        and "p25Phase2ShouldEmitAmbeFrame" in main.split("contextAudioLockedOut", 1)[1][:400]
    ),
    "bounded pending stash 650ms": (
        "outRate * 0.650" in main.split("size_t pushP25LiveStreamingAudio", 1)[1][:4000]
        and "outRate * 1.200" not in main.split("size_t pushP25LiveStreamingAudio", 1)[1][:4000]
    ),
    "real push ceiling follows jitter cap": "std::max(jitterSoftCap, jitterCap)"
    in main.split("size_t pushP25LiveStreamingAudio", 1)[1][:4000],
}

missing = [name for name, ok in checks.items() if not ok]
if missing:
    raise SystemExit(
        "P25 Phase 2 same-call hop speaker flush regression FAILED: " + ", ".join(missing)
    )
print("P25 Phase 2 same-call hop speaker flush regression: PASS")
