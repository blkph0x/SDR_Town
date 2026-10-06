#!/usr/bin/env python3
"""Verify DEC-0199 active-clear realtime catch-up geometry (360/160/280)."""
from pathlib import Path

root = Path(__file__).resolve().parents[2]
timing_h = (root / "include" / "P25VoiceTiming.h").read_text(encoding="utf-8", errors="replace")
timing_cpp = (root / "src" / "P25VoiceTiming.cpp").read_text(encoding="utf-8", errors="replace")
dm_cpp = (root / "src" / "DeviceManager.cpp").read_text(encoding="utf-8", errors="replace")

checks = {
    "realtime catch-up chunk 360ms":
        "kP25Phase2VoiceDecodeSpeakerRealtimeCatchUpChunkSeconds = 0.360" in timing_h,
    "realtime catch-up minFresh 160ms":
        "kP25Phase2VoiceDecodeSpeakerRealtimeCatchUpMinFreshSeconds = 0.160" in timing_h,
    "realtime catch-up does not use 280ms minFresh":
        "kP25Phase2VoiceDecodeSpeakerRealtimeCatchUpMinFreshSeconds = 0.280" not in timing_h,
    "backlog overlap stays DEC-0009 280ms":
        "kP25Phase2VoiceDecodeSpeakerBacklogCatchUpOverlapSeconds = 0.280" in timing_h,
    "planner selects realtime constants on active clear":
        "kP25Phase2VoiceDecodeSpeakerRealtimeCatchUpChunkSeconds" in timing_cpp
        and "kP25Phase2VoiceDecodeSpeakerRealtimeCatchUpMinFreshSeconds" in timing_cpp,
    "spectrum worker exists":
        "void DeviceManager::spectrumThreadFunc" in dm_cpp,
    "readStream path does not compute FFT inline":
        "localPower = computeRealFFTPower(samples, fftN" not in dm_cpp.split(
            "if (dev && stream)", 1
        )[1].split("Stub/no-hardware fallback", 1)[0],
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit(
        "P25 Phase 2 realtime catch-up geometry regression failed: " + ", ".join(failed)
    )
print("P25 Phase 2 realtime catch-up geometry regression: PASS")
