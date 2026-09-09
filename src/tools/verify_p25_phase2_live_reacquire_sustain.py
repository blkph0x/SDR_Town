#!/usr/bin/env python3
"""Guard live Phase 2 sustain against reacquire loops after valid audio."""

from pathlib import Path
import re

root = Path(__file__).resolve().parents[2]
from p25_orchestration_sources import orchestration_source_text
main = orchestration_source_text()

hard_acquire = main.split(
    "bool p25Phase2SessionHasHardTargetAcquire", 1
)[1].split(
    "bool p25Phase2SessionHadVoiceLock", 1
)[0]
burst_eye = main.split(
    "bool p25Phase2SessionHadBurstEye", 1
)[1].split(
    "bool p25Phase2SessionSpeakerSustainActive", 1
)[0]

checks = {
    "hard acquire computes acquired before wide-reacquire gate": (
        "const bool acquired =" in hard_acquire
        and "p25Phase2WideReacquireHoldWindows > 0 && !acquired" in hard_acquire
        and "return acquired;" in hard_acquire
    ),
    "wide reacquire cannot erase post-emit burst eye": (
        "const bool voiceLock = p25Phase2SessionHadVoiceLock(rx);" in burst_eye
        and "!voiceLock &&" in burst_eye
        and "!sustain.hadSuccessfulEmit" in burst_eye
    ),
    "only true cold-eye chunks are forced context-free": (
        main.count("if (chunkPlan.treatAsContextFreeFresh) {") >= 2
        and "chunkPlan.treatAsContextFreeFresh || unacquiredAcquireWindow" not in main
    ),
    "unacquired acquire still requests overlap context": (
        "plan.overlapSeconds = kP25Phase2VoiceDecodeUnacquiredAcquireOverlapSeconds;" in main
        and "kP25Phase2VoiceDecodeUnacquiredAcquireOverlapSeconds =" in main
    ),
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit(
        "verify_p25_phase2_live_reacquire_sustain failed:\n- " + "\n- ".join(failed)
    )

print("verify_p25_phase2_live_reacquire_sustain: PASS")
