#!/usr/bin/env python3
"""DEC-0044 auto PPM kept; DEC-0045 wall clamp rejected (DEC-0046)."""
from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
from p25_orchestration_sources import orchestration_source_text

MAIN_TEXT = orchestration_source_text()


def main() -> int:
    text = MAIN_TEXT
    if "kP25AutoPpmMinAbsAfcHz = 200.0" not in text:
        print("FAIL: DEC-0044 auto PPM AFC floor missing")
        return 1
    if "kP25AutoPpmCooldownMs = 120000" not in text:
        print("FAIL: DEC-0049 auto PPM cooldown 120s missing")
        return 1
    if "kP25AutoPpmMaxStep = 1.50" not in text:
        print("FAIL: DEC-0049 max step 1.5 missing")
        return 1
    if "kP25AutoPpmSoftProbeRailHz = 1250.0" not in text:
        print("FAIL: DEC-0049 soft-probe rail guard missing")
        return 1
    if "p25AutoPpmAfcSampleAcceptable" not in text:
        print("FAIL: DEC-0049 AFC sample gate missing")
        return 1
    if "p25MaybeAutoApplyPpmFromControlAfc" not in text:
        print("FAIL: auto PPM helper missing")
        return 1
    if "maybeAutoPpmOnControlReturn" not in text:
        print("FAIL: return-to-control auto PPM wire missing")
        return 1
    if "p25Phase2WallTimeoutMayClearSpeakerPending" not in text:
        print("FAIL: DEC-0046 wall-timeout pending guard missing")
        return 1
    if "wall stamp must not wipe playout" not in text:
        print("FAIL: empty wall-timeout keep-pending log missing")
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
    if "kP25LiveHealthySustainWallMs" in block and "decodeWallMs = std::min" in block:
        print("FAIL: DEC-0045 wall clamp must stay rejected (no decodeWallMs min to healthy wall)")
        return 1
    if "kP25LiveHealthySustainBudgetMs" not in block:
        print("FAIL: healthy budget path missing")
        return 1
    if "DEC-0046" not in block and "DEC-0046" not in text:
        print("FAIL: DEC-0046 rejection note missing")
        return 1
    print("PASS: DEC-0044 auto PPM; DEC-0046 no wall-clamp / no pending wipe on wall stamp")
    return 0


if __name__ == "__main__":
    sys.exit(main())
