#!/usr/bin/env python3
"""DEC-0035/0039/0041: live post-emit eye-lost uses replay cand width inside hot wall."""
from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
from p25_orchestration_sources import orchestration_source_text
# DEC-0040: search all orchestration TUs
MAIN_TEXT = orchestration_source_text()


def main() -> int:
    text = MAIN_TEXT
    if "kP25ReplayHotCqpskCandidates = 16" not in text:
        print("FAIL: replay hot cand=16 constant missing")
        return 1
    if "kP25VoiceWorkerHotMaxCqpskCandidates = 8" not in text:
        print("FAIL: live hot cand=8 constant missing")
        return 1
    if "kP25LiveEyeLostReplayBudgetMs" not in text:
        print("FAIL: DEC-0041 live eye-lost budget constant missing")
        return 1
    if "kP25LiveEyeLostReplayCandStreak = 1" not in text:
        print("FAIL: DEC-0048 eye-lost cand streak must be 1")
        return 1
    if "kP25LiveEyeLostReplayCandStreak = 2" in text:
        print("FAIL: DEC-0048 forbids eye-lost streak=2")
        return 1
    if "kP25LiveHealthySustainBudgetMs = 80" not in text:
        print("FAIL: DEC-0042 healthy sustain budget constant missing")
        return 1
    if "kP25LiveHealthySustainCqpskCandidates = 4" not in text:
        print("FAIL: DEC-0042 healthy sustain cand=4 constant missing")
        return 1
    m = re.search(
        r"else if \(hotPhase2TrafficJob\) \{(.*?)rx\.p25VoiceLiveDecoder\.setCqpskDiscreteFrozen",
        text,
        re.S,
    )
    if not m:
        print("FAIL: hotPhase2TrafficJob block not found")
        return 1
    block = m.group(1)
    if "DEC-0035" not in block:
        print("FAIL: DEC-0035 comment missing in live hot path")
        return 1
    if "DEC-0039" not in block:
        print("FAIL: DEC-0039 no-target eye-lost refinement missing")
        return 1
    if "DEC-0041" not in block:
        print("FAIL: DEC-0041 live eye-lost budget/streak missing")
        return 1
    if "DEC-0042" not in block:
        print("FAIL: DEC-0042 healthy sustain path missing")
        return 1
    if "noTargetEye" not in block or "eyeLost" not in block:
        print("FAIL: noTargetEye / eyeLost branch missing")
        return 1
    if "hadSuccessfulEmit" not in block:
        print("FAIL: post-emit no-target must consider hadSuccessfulEmit")
        return 1
    if "postEmitEyeLostStreak" not in block:
        print("FAIL: postEmitEyeLostStreak debounce missing")
        return 1
    if "escalateReplayCands" not in block:
        print("FAIL: escalateReplayCands gate missing")
        return 1
    if "kP25ReplayHotCqpskCandidates" not in block:
        print("FAIL: eye-lost path does not use kP25ReplayHotCqpskCandidates")
        return 1
    if "kP25LiveEyeLostReplayBudgetMs" not in block:
        print("FAIL: eye-lost path must use kP25LiveEyeLostReplayBudgetMs (not 240)")
        return 1
    if "hotBudgetMs = kP25ReplayHotBudgetMs" in block:
        print("FAIL: live eye-lost must not assign kP25ReplayHotBudgetMs (240)")
        return 1
    if "kP25LiveHealthySustainBudgetMs" not in block:
        print("FAIL: healthy path must use kP25LiveHealthySustainBudgetMs")
        return 1
    if "kP25LiveHealthySustainCqpskCandidates" not in block:
        print("FAIL: healthy path must use kP25LiveHealthySustainCqpskCandidates")
        return 1
    # With streak=1, first eye-lost hop escalates to cand=16; cand=8 branch remains
    # as the non-escalate fallback if streak logic changes.
    if "kP25ReplayHotCqpskCandidates" not in block:
        print("FAIL: eye-lost escalate must still reference kP25ReplayHotCqpskCandidates")
        return 1
    # DEC-0046: do not clamp decodeWallMs to a tight healthy/eye-lost wall.
    if "decodeWallMs = std::min" in block:
        print("FAIL: DEC-0046 forbids clamping decodeWallMs inside healthy/eye-lost path")
        return 1
    print("PASS: live healthy sustain cand=4/80; eye-lost streak>=1->cand=16/120; no wall clamp")
    return 0


if __name__ == "__main__":
    sys.exit(main())
