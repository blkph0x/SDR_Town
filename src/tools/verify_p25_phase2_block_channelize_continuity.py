#!/usr/bin/env python3
"""Verify block-channelize CQPSK is not sticky across independent IQ windows."""
from pathlib import Path

root = Path(__file__).resolve().parents[2]
main = (root / "src" / "main.cpp").read_text(encoding="utf-8", errors="replace")
decoder = (root / "src" / "P25LiveDecoder.cpp").read_text(encoding="utf-8", errors="replace")

checks = {
    "clear cqpsk on block channelize": (
        "Carrying Gardner/Costas discrete lock + demod-state lock-only budgets" in decoder
        and "m_demodStateMachine.reset()" in decoder
        and "m_streamTimingState = {}" in decoder
    ),
    "freeze requires streaming ddc": (
        "enableStreamingChannelDdc" in main.split(
            "p25Phase2ShouldFreezeCqpskDiscrete", 1
        )[1][:500]
    ),
    "speaker sustain multi-burst hop": (
        "kP25Phase2VoiceDecodeSpeakerSustainChunkSeconds = 0.120" in main
        and "kP25Phase2VoiceDecodeSpeakerSustainOverlapSeconds = 0.030" in main
    ),
    "speaker catch-up constants present": (
        "kP25Phase2VoiceDecodeSpeakerCatchUpChunkSeconds = 0.200" in main
        and "kP25Phase2VoiceDecodeSpeakerCatchUpMinFreshSeconds = 0.080" in main
    ),
    "realtime phase2 enables streaming ddc": (
        "phase2 && profile == P25VoiceDecodeProfile::Realtime" in main
        and "cfg.enableStreamingChannelDdc =" in main
        and "Capture 20260807_232020" in main
    ),
    "empty-eye does not invalidate epoch": (
        "do NOT invalidate" in main.split("hadSuccessfulEmit &&", 1)[1][:600]
        or "Do not invalidate sticky epoch" in main
    ),
    "lock-only requires streaming ddc": (
        "enableStreamingChannelDdc" in main.split("streamingCqpskJob", 1)[1][:500]
        and "enableStreamingChannelDdc" in main.split("streamLockOnlyWindow", 1)[1][:400]
    ),
    "block channelize keeps cold cqpsk budget": (
        "blockChannelizeReacquire" in main
        and "hotPhase2TrafficJob &&" in main
        and "enableStreamingChannelDdc) {" in main.split("hotPhase2TrafficJob &&", 1)[1][:200]
        and "kP25VoiceWorkerColdRealtimeBudgetMs" in main.split(
            "Block channelize clears sticky CQPSK each window", 1
        )[1][:500]
    ),
    "carried ess alone is not cqpsk hard lock": (
        "thisWindowPhase2Structure" in decoder
        and "thisWindowPhase2Structure && r.stats.phase2MacCrcValid > 0" in decoder
        and "r.stats.phase2MacCrcValid > 0 || r.stats.phase2EssKnown" not in decoder.split(
            "bool hasCqpskHardLockEvidence", 1
        )[1].split("bool hasPhase2SoftCqpskLockEvidence", 1)[0]
    ),
    "block channelize clears framer and bit tail": (
        "m_phase2Framer.reset()" in decoder.split(
            "Block channelize produces an independent baseband eye", 1
        )[1][:1500]
        and "m_phase1BitTail.clear()" in decoder.split(
            "Block channelize produces an independent baseband eye", 1
        )[1][:1800]
    ),
    "block channelize clears mask phase": (
        "m_phase2MaskPhaseKnown = false" in decoder.split(
            "Block channelize produces an independent baseband eye", 1
        )[1][:1800]
        and "m_phase2SuperframeAnchorKnown = false" in decoder.split(
            "Block channelize produces an independent baseband eye", 1
        )[1][:1800]
    ),
    "standards soft-stop requires cqpsk lock": (
        "standardsStateMayHoldDemod" in decoder
    ),
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit(
        "P25 Phase 2 block-channelize continuity regression failed: "
        + ", ".join(failed)
    )
print("P25 Phase 2 block-channelize continuity regression: PASS")
