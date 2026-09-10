#!/usr/bin/env python3
"""Guard the 20260825 live GUI Phase-2 starvation and carried-epoch fixes."""

from pathlib import Path

root = Path(__file__).resolve().parents[2]
from p25_orchestration_sources import orchestration_source_text
main = orchestration_source_text()

checks = {
    # ISS-0003: SpeakerCatchUp* removed — speaker path must stay on sustain 80+280;
    # backlog catch-up uses BacklogCatchUp* (120/80/280), not 180 ms live-edge skip.
    "speaker path uses sustain not 180ms catch-up": (
        "kP25Phase2VoiceDecodeSpeakerSustainChunkSeconds = 0.080" in main
        and "kP25Phase2VoiceDecodeSpeakerCatchUpChunkSeconds" not in main
        and "kP25Phase2VoiceDecodeBacklogCatchUpChunkSeconds = 0.120" in main
        and "skip to live-edge with 180 ms catch-up" in main
    ),
    "queue depth accepts receiver sustain hint": (
        "p25VoiceDecodeMaxPendingJobsNow(bool speakerSustainHint)" in main
        and "speakerSustainHint || p25Phase2SpeakerSustainDecodeActive()" in main
        and "bool speakerSustainDecode = false;" in main
        and "p25VoiceDecodeMaxPendingJobsNow(job.speakerSustainDecode)" in main
        and "p25VoiceWorkerCanAcceptJobForDepth(phase2VoiceQueueSustainHint)" in main
        and "job.speakerSustainDecode = phase2VoiceQueueSustainHint;" in main
    ),
    "carried selected slot epoch exists": (
        "const bool carriedSelectedSlotEpoch" in main
        and "sameCallContinuationStructure" in main
        and "currentBurstFeedTrustedRaw || carriedSelectedSlotEpoch" in main
        and "selectedSlotEpochForFeed = hardEpochOnBurst || carriedSelectedSlotEpoch" in main
    ),
    "carried epoch remains selected-slot only": (
        "burst.grantSlotKnown" in main.split("const bool carriedSelectedSlotEpoch", 1)[1].split(
            "const bool currentBurstFeedTrusted", 1
        )[0]
        and "effectiveBurstSlot == followedGrantSlot" in main.split(
            "const bool carriedSelectedSlotEpoch", 1
        )[1].split("const bool currentBurstFeedTrusted", 1)[0]
        and "!burstEncryptedForFollowedCall" in main.split(
            "const bool carriedSelectedSlotEpoch", 1
        )[1].split("const bool currentBurstFeedTrusted", 1)[0]
    ),
    "dual-slot continuation uses carried epoch": (
        "sameCallContinuationStructure &&\n            currentBurstFeedTrusted" in main
        and "selectedSlotEpochForFeed &&" in main.split(
            "const bool dualSlotSelectedContinuationForBurst", 1
        )[1].split("if (dualSlotSelectedContinuationForBurst)", 1)[0]
        and "burst.xorMaskApplied &&\n            selectedSlotEpochForFeed" in main
    ),
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit(
        "P25 Phase 2 live starvation/carried-epoch regression FAILED: "
        + ", ".join(failed)
    )

print("P25 Phase 2 live starvation/carried-epoch regression: PASS")
