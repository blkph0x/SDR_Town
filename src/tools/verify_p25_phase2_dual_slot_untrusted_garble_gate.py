#!/usr/bin/env python3
"""Guard: dual-slot MAC-dead fail-close, with a narrow same-call continuation escape."""
from pathlib import Path

root = Path(__file__).resolve().parents[2]
from p25_orchestration_sources import orchestration_source_text
main = orchestration_source_text()


def region_after(marker: str, chars: int) -> str:
    if marker not in main:
        return ""
    return main.split(marker, 1)[1][:chars]


# Prefer the function body, not the forward declaration.
body_marker = (
    "static bool p25Phase2DualSlotUntrustedGarbleWindow(const P25VoiceAudioBlock& out) noexcept\n{"
)
if body_marker not in main:
    body_marker = (
        "static bool p25Phase2DualSlotUntrustedGarbleWindow(const P25VoiceAudioBlock& out) noexcept\r\n{"
    )
garble_fn = main.split(body_marker, 1)[1].split(
    "p25Phase2DualSlotPendingDrainUnsafeWindow", 1
)[0]
pending_fn = main.split("p25Phase2DualSlotPendingDrainUnsafeWindow", 1)[1].split(
    "p25Phase2CurrentSelectedBurstFeedTrusted", 1
)[0]
feed_region = main.split("hardEpochOnBurst =", 1)[1][:16000]
pending_drain_body = main.split("auto canDrainPendingRawVoiceThisWindow", 1)[1].split(
    "auto discardStalePendingWhenLivePreferred", 1
)[0]
pending_dual_branch = pending_drain_body.split(
    "if (out.phase2OppositeVoiceCodewords > 0) {", 2
)[-1].split("}", 1)[0]
continuation_fn = region_after(
    "static bool p25Phase2SameCallSelectedTimeslotContinuationSafe", 3200
)
unsafe_mixed_fn = region_after(
    "static bool p25Phase2UnsafeMixedSlotAudioWindow", 1200
)
security_gate_region = region_after(
    "const bool sameCallSelectedContinuation =", 1600
)
trusted_clear_region = region_after(
    "const bool trustedClear =",
    1800,
)
recent_continuation_region = region_after(
    "const bool recentClearContinuationEvidence =",
    700,
)
feed_region_after_now = (
    feed_region.split("const bool dualSlotUntrustedNow", 1)[1].split(";", 1)[0]
    if "const bool dualSlotUntrustedNow" in feed_region
    else ""
)

checks = {
    "garble uses this-window selected proof": (
        "phase2ThisWindowTargetMacCrcValid" in garble_fn
        and "phase2ThisWindowTargetEssClear" in garble_fn
        and "selectedSlotContinuityProof" not in garble_fn
        and "targetSlotClear" not in garble_fn
        # Sticky targetMac alone must not appear as the allow path.
        and "out.phase2TargetMacCrcValid" not in garble_fn
    ),
    "pending drain delegates to garble helper": (
        "return p25Phase2DualSlotUntrustedGarbleWindow(out);" in pending_fn
    ),
    "continuation helper is narrow": (
        "p25Phase2SameCallSelectedTimeslotContinuationSafe" in main
        and "p25Phase2RecentSecurityEvidenceUsable(rx, key, nowMs)" in continuation_fn
        and "p25Phase2CompanionSlotAccounted(out)" in continuation_fn
        and "p25Phase2StrongSelectedSlotStructure(out)" in continuation_fn
        and "out.phase2WrongSlot" in continuation_fn
        and "out.phase2FeedOrderIssues > 0" in continuation_fn
        and "out.phase2PendingAmbeFramesReleased > 0" in continuation_fn
        and "out.phase2CurrentFeedTrustedTargetBurst" in continuation_fn
        and "p25AudioSamplesLookSafe(out.audio)" in continuation_fn
        and "thisWindowSelectedSlotProof" in continuation_fn
        and "recentSelectedSlotProof" in continuation_fn
        and "out.phase2ThisWindowTargetMacCrcValid" in continuation_fn
        and "out.phase2ThisWindowTargetEssClear" in continuation_fn
    ),
    "speaker dual-slot gate has continuation escape": (
        "p25Phase2SameCallSelectedTimeslotContinuationSafe(rx, out, key, nowMs, true)"
        in security_gate_region
        and "p25Phase2DualSlotUntrustedGarbleWindow(out)" in security_gate_region
        and "!sameCallSelectedContinuation" in security_gate_region
        and "p25Phase2PostEmitMixedMacDeadWindow(rx, out)" in security_gate_region
        and "dual-slot-untrusted-garble-drop" in main.split(
            "if (dualSlotUntrustedGate)", 1
        )[1][:900]
        and "postEmitMixedMacDeadGate" not in main.split(
            "if (dualSlotUntrustedGate)", 1
        )[1][:900]
    ),
    "feed dual-slot gate has continuation escape": (
        "dualSlotSelectedContinuationProof" not in feed_region
        and "dualSlotUntrustedNow" in feed_region
        and "dualSlotSelectedContinuationForBurst" in feed_region
        and "p25Phase2SameCallSelectedTimeslotContinuationSafe(rx, out, audioKey, nowMs, false)"
        in feed_region
        and "!dualSlotSelectedContinuationForBurst" in feed_region_after_now
        and "p25Phase2PostEmitMixedMacDeadWindow(rx, out)" in feed_region_after_now
        and "hadSuccessfulEmit"
        not in feed_region_after_now
    ),
    "feed uses this-window selected proof": (
        # Capture 20260811_080304: feed dualSlotUntrustedNow delegates to the
        # same DualSlotUntrustedGarbleWindow helper (which owns this-window
        # MAC/ESS). Do not re-inline weaker this-window flags on the feed path.
        "p25Phase2DualSlotUntrustedGarbleWindow(out)"
        in feed_region_after_now
        and "phase2ThisWindowTargetMacCrcValid" in garble_fn
        and "phase2ThisWindowTargetEssClear" in garble_fn
    ),
    "unsafe mixed-slot audio honors same-call continuation": (
        "phase2SameCallSelectedTimeslotContinuation" in unsafe_mixed_fn
        and "phase2CurrentFeedTrustedTargetBurst" in unsafe_mixed_fn
        and "phase2PendingAmbeFramesReleased == 0" in unsafe_mixed_fn
        and "p25Phase2CompanionSlotAccounted(out)" in unsafe_mixed_fn
        and "return false;" in unsafe_mixed_fn
    ),
    "pending dual-slot drain requires target proof": (
        "out.phase2ThisWindowTargetMacCrcValid" in pending_dual_branch
        and "out.phase2ThisWindowTargetEssClear" in pending_dual_branch
        and "out.phase2MacCrcValid > 0" not in pending_dual_branch
        and "currentWindowHasFeedTrustedTargetBurst &&" not in pending_dual_branch
    ),
    "recent continuation uses same-call selected-slot proof": (
        "out.phase2OppositeVoiceCodewords == 0 ||" in recent_continuation_region
        and "out.phase2ThisWindowTargetMacCrcValid" in recent_continuation_region
        and "out.phase2ThisWindowTargetEssClear" in recent_continuation_region
        and "out.phase2SameCallSelectedTimeslotContinuation" in recent_continuation_region
    ),
    "trusted clear sustain cannot bypass dual-slot proof": (
        "out.phase2OppositeVoiceCodewords == 0 ||" in trusted_clear_region
        and "out.phase2ThisWindowTargetMacCrcValid" in trusted_clear_region
        and "out.phase2ThisWindowTargetEssClear" in trusted_clear_region
        and "sameCallSelectedContinuation" not in trusted_clear_region.split("unknownGrantProbeVoiceRelease", 1)[0]
    ),
    "speaker gate names garble": "phase2-dual-slot-untrusted-garble" in main,
    "explicit-clear evidence fails closed on dual-slot": (
        "p25Phase2DualSlotUntrustedGarbleWindow(out)"
        in main.split("p25Phase2ExplicitClearGrantVoiceReleaseEvidence", 1)[1][:1200]
        and "p25Phase2PostEmitMixedMacDeadWindow(rx, out)"
        in main.split("p25Phase2ExplicitClearGrantVoiceReleaseEvidence", 1)[1][:1200]
    ),
    "trusted clear cannot bypass post-emit mixed MAC-dead": (
        "!postEmitMixedMacDeadGate" in trusted_clear_region
    ),
    "gui sustain matches cli speakerMayEmit": (
        "Match CLI voicetest: sustain lattice" in main
        and "pushed > 0);"
        not in main.split("configureReceiver =", 1)[-1].split(
            "p25Phase2UpdateSessionSustainState", 1
        )[1][:350]
    ),
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit(
        "P25 Phase 2 dual-slot untrusted garble gate regression failed: "
        + ", ".join(failed)
    )
print("P25 Phase 2 dual-slot untrusted garble gate regression: PASS")
