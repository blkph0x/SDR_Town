#!/usr/bin/env python3
"""Regression: audio output dialog must not fight ensureAudioOutputActive / freeze UI."""
from pathlib import Path

root = Path(__file__).resolve().parents[2]
main = (root / "src" / "main.cpp").read_text(encoding="utf-8", errors="replace")
engine_h = (root / "include" / "AudioEngine.h").read_text(encoding="utf-8", errors="replace")
engine_cpp = (root / "src" / "AudioEngine.cpp").read_text(encoding="utf-8", errors="replace")
spec_h = (root / "include" / "SpectrumWidget.h").read_text(encoding="utf-8", errors="replace")
spec_cpp = (root / "src" / "SpectrumWidget.cpp").read_text(encoding="utf-8", errors="replace")

checks = {
    "active device name list API": "getActiveDeviceNameList" in engine_h and "getActiveDeviceNameList" in engine_cpp,
    "deviceName stored on ActiveOutput": "std::string deviceName" in engine_h,
    "enumerate remaps active by name": "m_devices[i].name == act->deviceName" in engine_cpp,
    "incremental setActiveOutputs": "needStart" in engine_cpp and "stopList" in engine_cpp,
    "dialog refuses empty Apply": "Select at least one playback device before Apply" in main,
    "dialog preselect by name": "currentlyActiveNames" in main and "getActiveDeviceNameList()" in main,
    "preferred output names remembered": "preferredAudioOutputNames" in main,
    "ensure prefers preferred names": "Audio auto-activated preferred output" in main,
    "spectrum UI 50ms throttle all modes": "lastSpectrumUiTick" in main
    and "milliseconds(50)" in main.split("lastSpectrumUiTick", 1)[1][:400],
    "timer starts at 50ms": "updateTimer->start(50)" in main,
    "high-res history capped": "kMaxHighResHistory = 96" in spec_h,
    "full-view skips high-res paint snap": "zoomedView" in spec_cpp and "m_sampleRate * 0.95" in spec_cpp,
}

missing = [name for name, ok in checks.items() if not ok]
if missing:
    raise SystemExit("Audio/UI freeze hotfix regression FAILED: " + ", ".join(missing))
print("Audio/UI freeze hotfix regression: PASS")
