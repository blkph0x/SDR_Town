#!/usr/bin/env python3
"""Verify MAC/ESS starve soft-repairs sticky mask/epoch instead of hard CQPSK reset."""
from pathlib import Path

root = Path(__file__).resolve().parents[2]
main = (root / "src" / "main.cpp").read_text(encoding="utf-8", errors="replace")
decoder_h = (root / "include" / "P25LiveDecoder.h").read_text(encoding="utf-8", errors="replace")
decoder_cpp = (root / "src" / "P25LiveDecoder.cpp").read_text(encoding="utf-8", errors="replace")
receiver_h = (root / "include" / "Receiver.h").read_text(encoding="utf-8", errors="replace")

checks = {
    "invalidate API": "invalidatePhase2StickyMaskEpoch" in decoder_h
    and "void P25LiveDecoder::invalidatePhase2StickyMaskEpoch()" in decoder_cpp,
    "receiver soft-repair flags": (
        "p25Phase2ForceMaskEpochRehunt" in receiver_h
        and "p25Phase2MaskEpochRepairHoldWindows" in receiver_h
    ),
    "mac starve sets soft rehunt": (
        "p25Phase2ForceMaskEpochRehunt = true" in main
        and "p25Phase2MaskEpochRepairHoldWindows" in main
    ),
    "mac starve does not arm wide reacquire": (
        "p25Phase2MacEssStarvedVoiceWindow(out)" in main
        and "WideReacquireHoldWindows =\n                    std::max(rx.p25Phase2WideReacquireHoldWindows, 3)"
        not in main.split("p25Phase2MacEssStarvedVoiceWindow(out)", 1)[1][:800]
    ),
    "hard reset requires empty eye": (
        "rx.p25VoiceDiagnostics.phase2Bursts == 0" in main
        and "rx.p25VoiceDiagnostics.phase2MaskedBursts == 0" in main
        and "rx.p25VoiceDiagnostics.phase2TargetVoiceCodewords == 0" in main
    ),
    "worker calls soft invalidate": "invalidatePhase2StickyMaskEpoch()" in main,
    "repair uses cold-eye windows": (
        "maskEpochRepairWindow" in main
        and "(wideReacquireWindow || maskEpochRepairWindow)" in main
        and "kP25Phase2VoiceDecodeFirstColdEyeSeconds" in main
    ),
    "invalidate clears hunt throttle": (
        "m_phase2LastFullMaskPhaseHuntGeneration = 0" in decoder_cpp.split(
            "invalidatePhase2StickyMaskEpoch", 1
        )[1][:1200]
    ),
    "wide reacquire ignores stale cqpsk/recent-only": (
        "cqpskLockValid()" not in main.split(
            "p25Phase2NeedsWideReacquireWindowLocked", 1
        )[1][:1800].split("currentStreamingEye", 1)[1][:600]
    ),
    "empty-eye soft repair after emit": (
        "sustain.hadSuccessfulEmit &&" in main
        or "rx.p25SessionState.sustain.hadSuccessfulEmit &&" in main
    ),
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit(
        "P25 Phase 2 MAC/ESS starve soft-repair regression failed: " + ", ".join(failed)
    )
print("P25 Phase 2 MAC/ESS starve soft-repair regression: PASS")
