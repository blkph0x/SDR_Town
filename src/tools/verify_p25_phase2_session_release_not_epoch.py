#!/usr/bin/env python3
"""Regression: sessionAudioRelease must not count as XOR/mask epoch for AMBE feed."""

from pathlib import Path

root = Path(__file__).resolve().parents[2]
main = (root / "src" / "main.cpp").read_text(encoding="utf-8", errors="ignore")


def expr_after(anchor: str, n: int = 400) -> str:
    chunk = main.split(anchor, 1)[1][:n]
    return chunk.split(";", 1)[0]


feed_fn = main.split("static bool p25Phase2CurrentSelectedBurstFeedTrusted", 1)[1][:1600]
hard_epoch = expr_after("const bool hardEpochOnBurst =")
dual_now = expr_after("const bool dualSlotUntrustedNow =")
dual_explicit = expr_after("const bool dualSlotUntrustedExplicitGrant =")
proof = expr_after("const bool thisWindowSelectedClearProof =")

required = {
    "feed trusted requires mask/MAC not sessionRelease": (
        "sessionAudioRelease" not in feed_fn.split("return false;", 1)[0]
        and "burst.maskPhaseLock" in feed_fn
        and "burst.xorMaskPhaseKnown && burst.superframeLock" in feed_fn
        and "20260811_072556" in feed_fn
    ),
    "hardEpoch excludes sessionAudioRelease": (
        "sessionAudioRelease" not in hard_epoch
        and "xorMaskPhaseKnown && burst.superframeLock" in hard_epoch
    ),
    "dualSlotUntrustedNow excludes session-release alone": (
        "SessionAudioRelease" not in dual_now
        and "p25Phase2DualSlotUntrustedGarbleWindow(out)" in dual_now
        and "p25Phase2PostEmitMixedMacDeadWindow(rx, out)" in dual_now
    ),
    "dualSlotUntrustedExplicitGrant excludes session-release alone": (
        "SessionAudioRelease" not in dual_explicit
        and "p25Phase2DualSlotUntrustedGarbleWindow(out)" in dual_explicit
        and "p25Phase2PostEmitMixedMacDeadWindow(rx, out)" in dual_explicit
    ),
    "dualSlot clear proof excludes session-release alone": (
        "SessionAudioRelease" not in proof
        and "ThisWindowTargetEssClear" in proof
    ),
    "capture evidence comments present": "20260811_072556" in main,
}

missing = [name for name, ok in required.items() if not ok]
if missing:
    raise SystemExit(
        "P25 Phase 2 session-release epoch gate regression FAILED: "
        + ", ".join(missing)
    )

print("P25 Phase 2 session-release epoch gate regression: PASS")
