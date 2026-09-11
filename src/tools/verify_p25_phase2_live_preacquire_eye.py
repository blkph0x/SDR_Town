#!/usr/bin/env python3
from pathlib import Path
import re

root = Path(__file__).resolve().parents[2]
from p25_orchestration_sources import orchestration_source_text
main = orchestration_source_text()

checks = {
    "cold acquire waits for the full two-superframe eye in GUI and CLI": (
        main.count("sr * kP25Phase2VoiceDecodeFirstColdEyeSeconds") >= 2
        and "std::clamp(sr * 0.120" not in main
    ),
    "pre-acquired Phase 2 traffic is explicit in both schedulers": (
        main.count("const bool preAcquiredPhase2Traffic =") >= 2
    ),
    "pre-acquired traffic cannot be stolen by backlog catch-up": (
        len(re.findall(
            r"const bool backlogCatchUp\s*=\s*!preAcquiredPhase2Traffic\s*&&\s*undecodedBacklog\s*>\s*backlogCatchUpThreshold",
            main,
        )) >= 2
    ),
    "unacquired acquire uses the protected pre-acquire state": (
        len(re.findall(
            r"const bool unacquiredAcquireWindow\s*=\s*preAcquiredPhase2Traffic\s*&&\s*!speakerSustainDecode",
            main,
        )) >= 2
    ),
    "unacquired acquire retains full-window overlap context": (
        "kP25Phase2VoiceDecodeUnacquiredAcquireOverlapSeconds =" in main
        and "plan.overlapSeconds = kP25Phase2VoiceDecodeUnacquiredAcquireOverlapSeconds;" in main
    ),
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit(
        "verify_p25_phase2_live_preacquire_eye failed:\n- " + "\n- ".join(failed)
    )
print("verify_p25_phase2_live_preacquire_eye: PASS")
