#!/usr/bin/env python3
"""DEC-0035/0039: live post-emit eye-lost (incl. no-target) uses replay CQPSK caps."""
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
    if "noTargetEye" not in block or "eyeLost" not in block:
        print("FAIL: noTargetEye / eyeLost branch missing")
        return 1
    if "hadSuccessfulEmit" not in block:
        print("FAIL: post-emit no-target must consider hadSuccessfulEmit")
        return 1
    if "kP25ReplayHotCqpskCandidates" not in block:
        print("FAIL: eye-lost path does not use kP25ReplayHotCqpskCandidates")
        return 1
    if "kP25VoiceWorkerHotMaxCqpskCandidates" not in block:
        print("FAIL: healthy-eye path must keep kP25VoiceWorkerHotMaxCqpskCandidates")
        return 1
    print("PASS: live eye-lost (incl. post-emit no-target) uses replay cand=16/240")
    return 0


if __name__ == "__main__":
    sys.exit(main())
