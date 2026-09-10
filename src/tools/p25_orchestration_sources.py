#!/usr/bin/env python3
"""Concatenated P25 orchestration sources for string-lock verifiers (DEC-0040)."""
from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# Keep in sync with CMake SDR_TOWN_SOURCES orchestration TUs extracted from main.cpp.
# Headers are included when extracted constants live as inline constexpr (Phase 1+).
ORCHESTRATION_CPP = [
    ROOT / "src" / "main.cpp",
    ROOT / "src" / "P25AppGlobals.cpp",
    ROOT / "include" / "P25AppGlobals.h",
    ROOT / "src" / "P25TalkgroupRegistry.cpp",
    ROOT / "include" / "P25TalkgroupRegistry.h",
    ROOT / "include" / "P25VoiceTiming.h",
    ROOT / "src" / "P25VoiceTiming.cpp",
    ROOT / "src" / "P25RollingIq.cpp",
    ROOT / "include" / "P25RollingIq.h",
    ROOT / "src" / "P25VoiceSession.cpp",
    ROOT / "include" / "P25VoiceSession.h",
    ROOT / "src" / "P25DecodeConfig.cpp",
    ROOT / "include" / "P25DecodeConfig.h",
    ROOT / "src" / "DemodModeUtils.cpp",
    ROOT / "include" / "DemodModeUtils.h",
    ROOT / "src" / "SavedFrequencies.cpp",
    ROOT / "include" / "SavedFrequencies.h",
    ROOT / "src" / "P25VoiceDecode.cpp",
    ROOT / "include" / "P25VoiceDecode.h",
    ROOT / "src" / "P25VoiceTest.cpp",
    ROOT / "include" / "P25VoiceTest.h",
    ROOT / "src" / "CliApp.cpp",
    ROOT / "include" / "CliApp.h",
    ROOT / "src" / "MainWindow.cpp",
    ROOT / "src" / "MainWindowP25Voice.cpp",
    ROOT / "include" / "MainWindow.h",
    ROOT / "src" / "AppBootstrap.cpp",
    ROOT / "include" / "AppBootstrap.h",
]


def orchestration_source_paths() -> list[Path]:
    return [p for p in ORCHESTRATION_CPP if p.is_file()]


def orchestration_source_text() -> str:
    parts: list[str] = []
    for path in orchestration_source_paths():
        parts.append(f"\n/* ==== {path.relative_to(ROOT).as_posix()} ==== */\n")
        parts.append(path.read_text(encoding="utf-8", errors="replace"))
    return "".join(parts)


if __name__ == "__main__":
    paths = orchestration_source_paths()
    print(f"orchestration files: {len(paths)}")
    for p in paths:
        print(f"  {p.relative_to(ROOT).as_posix()}")
    print(f"total chars: {len(orchestration_source_text())}")
