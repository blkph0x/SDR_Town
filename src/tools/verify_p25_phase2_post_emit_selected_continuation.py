#!/usr/bin/env python3
"""Guard the Phase 2 post-emit selected-slot continuation escape."""
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / "src" / "tools"))
from p25_orchestration_sources import orchestration_source_text

text = orchestration_source_text()


def after(marker: str, chars: int) -> str:
    if marker not in text:
        return ""
    return text.split(marker, 1)[1][:chars]


helper = after("bool p25Phase2PostEmitSelectedSlotContinuationSafe", 2600)
feed = after("const bool selectedPostEmitContinuationForBurst =", 1800)
security = after("const bool postEmitSelectedContinuationSafe =", 1200)
trusted = after("const bool trustedClear =", 900)

checks = {
    "helper exists": bool(helper),
    "helper only applies to post-emit mixed MAC-dead windows": (
        "p25Phase2PostEmitMixedMacDeadWindow(rx, out)" in helper
    ),
    "helper requires same-call selected continuation": (
        "p25Phase2SameCallSelectedTimeslotContinuationSafe(rx, out, key, nowMs, requireFedAudio)" in helper
    ),
    "helper requires current trusted selected burst": (
        "out.phase2CurrentFeedTrustedTargetBurst" in helper
    ),
    "helper forbids pending drain and encrypted/wrong-slot leakage": (
        "out.phase2PendingAmbeFramesReleased > 0" in helper
        and "out.phase2TargetEssEncrypted" in helper
        and "out.phase2WrongSlot" in helper
        and "out.skippedEncrypted" in helper
    ),
    "helper requires companion accounted and strong selected structure": (
        "p25Phase2CompanionSlotAccounted(out)" in helper
        and "p25Phase2StrongSelectedSlotStructure(out)" in helper
        and "p25Phase2WindowHasFreshTargetEvidence(out)" in helper
    ),
    "feed exempts only selected post-emit continuation": (
        "dualSlotSelectedContinuationForBurst" in feed
        and "p25Phase2PostEmitSelectedSlotContinuationSafe" in feed
        and "!selectedPostEmitContinuationForBurst" in feed
        and "p25Phase2PostEmitMixedMacDeadWindow(rx, out)" in feed
    ),
    "feed opens clear path with selected continuation": (
        "selectedPostEmitContinuationForBurst" in after("const bool securityProvedClearForFeed =", 1200)
        and "selectedPostEmitContinuationForBurst" in after("const bool continuousSelectedClearFeed =", 1400)
    ),
    "speaker gate keeps fail-close default": (
        "p25Phase2PostEmitSelectedSlotContinuationSafe" in security
        and "p25Phase2PostEmitMixedMacDeadWindow(rx, out)" in security
        and "!postEmitSelectedContinuationSafe" in security
        and "!postEmitMixedMacDeadGate" in trusted
    ),
    "trusted clear may pass selected safe continuation": (
        "postEmitSelectedContinuationSafe" in trusted
    ),
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit(
        "P25 Phase 2 post-emit selected-slot continuation regression failed: "
        + ", ".join(failed)
    )
print("P25 Phase 2 post-emit selected-slot continuation regression: PASS")
