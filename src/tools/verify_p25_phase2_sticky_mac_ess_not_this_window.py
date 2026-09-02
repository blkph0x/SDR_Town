#!/usr/bin/env python3
"""Regression: sticky session PTT/ESS must not fake this-window MAC/ESS proof."""

from pathlib import Path

root = Path(__file__).resolve().parents[2]
decoder = (root / "src" / "P25LiveDecoder.cpp").read_text(encoding="utf-8", errors="ignore")
header = (root / "include" / "P25LiveDecoder.h").read_text(encoding="utf-8", errors="ignore")
main = (root / "src" / "main.cpp").read_text(encoding="utf-8", errors="ignore")

# Sticky paint region after session fields copied onto burst.
paint_anchor = "burst.essKnown = session->ess.known && session->essTrusted;"
paint = decoder.split(paint_anchor, 1)[1][:1800] if paint_anchor in decoder else ""

# This-window ESS aggregation in main.
ess_agg = ""
if "phase2ThisWindowTargetEssClear = true" in main:
    # Take the targetSlot ESS block that sets this-window clear.
    chunk = main.split("out.phase2ThisWindowTargetEssClear = true;", 1)[0]
    ess_agg = chunk[-900:] + "out.phase2ThisWindowTargetEssClear = true;"

dual_now = ""
if "const bool dualSlotUntrustedNow =" in main:
    dual_now = main.split("const bool dualSlotUntrustedNow =", 1)[1][:350].split(";", 1)[0]

epoch = ""
if "const bool epochTrusted =" in main:
    # Prefer the feed-loop epochTrusted near hardEpoch (post-080304).
    parts = main.split("const bool epochTrusted =")
    epoch = parts[-1][:500].split(";", 1)[0] if len(parts) > 1 else ""

mask_phase = ""
if "const bool maskPhaseTrusted =" in main:
    parts = main.split("const bool maskPhaseTrusted =")
    mask_phase = parts[-1][:450].split(";", 1)[0] if len(parts) > 1 else ""

pending = ""
if "p25Phase2DualSlotPendingDrainUnsafeWindow" in main:
    # canDrainPending lambda tail after live-mix gates.
    pending = main.split("p25Phase2DualSlotPendingDrainUnsafeWindow(out))", 1)[1][:500]

required = {
    "burst carries essObservedThisBurst": "essObservedThisBurst" in header,
    "burst carries trafficSecurityObservedThisBurst": (
        "trafficSecurityObservedThisBurst" in header
    ),
    "sticky ptt/active not OR'd into macCrcLock": (
        "do NOT OR sticky pttSeen/activeSeen into" in paint
        and "burst.macCrcLock =" not in paint
        and "burst.macCrcLock ||" not in paint
        and "080304" in paint
        and "phase2AudioLock" in paint
    ),
    "this-window ESS clear requires observed-this-burst": (
        "essObservedThisBurst" in ess_agg
        and "trafficSecurityObservedThisBurst" in main
        and "080304" in main
    ),
    "dualSlotUntrustedNow uses DualSlotUntrustedGarbleWindow": (
        "p25Phase2DualSlotUntrustedGarbleWindow(out)" in dual_now
    ),
    "epochTrusted excludes sessionAudioRelease": "sessionAudioRelease" not in epoch,
    "maskPhaseTrusted excludes sessionAudioRelease": (
        "sessionAudioRelease" not in mask_phase
    ),
    "pending dual-slot drain fail-closed (no sessionRelease escape)": (
        "return false" in pending[:80]
        and "TargetSessionAudioRelease" not in pending
    ),
}

missing = [name for name, ok in required.items() if not ok]
if missing:
    raise SystemExit(
        "P25 Phase 2 sticky MAC/ESS this-window regression FAILED: "
        + ", ".join(missing)
    )

print("P25 Phase 2 sticky MAC/ESS this-window regression: PASS")
