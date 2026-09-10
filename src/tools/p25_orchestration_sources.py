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
    ROOT / "src" / "MainWindowP25Orchestration.cpp",
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


def _definition_start(text: str, signature: str) -> int | None:
    """Index of signature for a definition ({ before ;), else None."""
    start = 0
    while True:
        idx = text.find(signature, start)
        if idx < 0:
            return None
        after = text[idx + len(signature) : idx + len(signature) + 8192]
        brace = after.find("{")
        semi = after.find(";")
        if brace >= 0 and (semi < 0 or brace < semi):
            return idx
        start = idx + 1


def definition_body(
    text: str,
    signature: str,
    next_signatures: list[str] | None = None,
) -> str:
    """Return text from a definition signature through the next signature (or reasonable end).

    Skips prototypes/calls where ``;`` appears before ``{``. Prefer ``Class::method``
    or free-function signatures that appear on .cpp definitions (ISS-0009).
    """
    idx = _definition_start(text, signature)
    if idx is None:
        raise ValueError(f"definition not found for signature: {signature!r}")
    body = text[idx + len(signature) :]
    if next_signatures:
        end = len(body)
        for nxt in next_signatures:
            pos = body.find(nxt)
            if pos >= 0:
                end = min(end, pos)
        return body[:end]
    # No next marker: return through the matching closing brace of the definition.
    brace = body.find("{")
    if brace < 0:
        return body[:4096]
    depth = 0
    for i in range(brace, len(body)):
        ch = body[i]
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return body[: i + 1]
        if i - brace > 200000:
            break
    return body[: max(brace + 4096, 8192)]


def require_definition(text: str, signature: str) -> str:
    """Like definition_body without next markers; raises a clear error if missing."""
    try:
        return definition_body(text, signature)
    except ValueError as exc:
        raise RuntimeError(
            f"ISS-0009: required definition not found: {signature!r}"
        ) from exc


if __name__ == "__main__":
    paths = orchestration_source_paths()
    print(f"orchestration files: {len(paths)}")
    for p in paths:
        print(f"  {p.relative_to(ROOT).as_posix()}")
    print(f"total chars: {len(orchestration_source_text())}")
