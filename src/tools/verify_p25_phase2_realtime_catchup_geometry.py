#!/usr/bin/env python3
"""Verify DEC-0200 removed the regressing realtime catch-up geometry."""
from pathlib import Path

root = Path(__file__).resolve().parents[2]
timing_h = (root / "include" / "P25VoiceTiming.h").read_text(encoding="utf-8", errors="replace")
timing_cpp = (root / "src" / "P25VoiceTiming.cpp").read_text(encoding="utf-8", errors="replace")
dm_cpp = (root / "src" / "DeviceManager.cpp").read_text(encoding="utf-8", errors="replace")

checks = {
    "no realtime catch-up chunk constant":
        "kP25Phase2VoiceDecodeSpeakerRealtimeCatchUpChunkSeconds" not in timing_h,
    "no realtime catch-up minFresh constant":
        "kP25Phase2VoiceDecodeSpeakerRealtimeCatchUpMinFreshSeconds" not in timing_h,
    "backlog catch-up stays 240/160/280":
        "kP25Phase2VoiceDecodeSpeakerBacklogCatchUpChunkSeconds = 0.240" in timing_h
        and "kP25Phase2VoiceDecodeSpeakerBacklogCatchUpMinFreshSeconds = 0.160" in timing_h
        and "kP25Phase2VoiceDecodeSpeakerBacklogCatchUpOverlapSeconds = 0.280" in timing_h,
    "planner uses backlog constants only":
        "kP25Phase2VoiceDecodeSpeakerBacklogCatchUpChunkSeconds" in timing_cpp
        and "kP25Phase2VoiceDecodeSpeakerRealtimeCatchUpChunkSeconds" not in timing_cpp,
    "spectrum worker remains":
        "void DeviceManager::spectrumThreadFunc" in dm_cpp,
    "readStream path does not compute FFT inline":
        "localPower = computeRealFFTPower(samples, fftN" not in dm_cpp.split(
            "if (dev && stream)", 1
        )[1].split("Stub/no-hardware fallback", 1)[0],
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit(
        "P25 Phase 2 realtime catch-up revert regression failed: " + ", ".join(failed)
    )
print("P25 Phase 2 realtime catch-up revert regression: PASS")
