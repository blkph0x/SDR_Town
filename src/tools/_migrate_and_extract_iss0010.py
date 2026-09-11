#!/usr/bin/env python3
"""One-shot: extract MainWindow::startP25LiveDecodePipeline from ctor (ISS-0010)."""
from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MW = ROOT / "src" / "MainWindow.cpp"
OUT = ROOT / "src" / "MainWindowP25Orchestration.cpp"
HDR = ROOT / "include" / "MainWindow.h"
CMAKE = ROOT / "CMakeLists.txt"
ORCH = ROOT / "src" / "tools" / "p25_orchestration_sources.py"

MARKER_START = "        // Dedicated background DSP worker thread for the GUI monitor path."
MARKER_END = "        // Ensure streams are stopped on GUI close / quit so rxThread + realInitThread"


def main() -> None:
    text = MW.read_text(encoding="utf-8")
    start = text.find(MARKER_START)
    end = text.find(MARKER_END)
    if start < 0 or end < 0 or end <= start:
        raise SystemExit(f"markers not found start={start} end={end}")

    block = text[start:end]
    # Trim trailing blank lines from extracted block (leave one newline)
    block = block.rstrip() + "\n"

    replacement = (
        "        // ISS-0010: rolling-IQ / chunk-plan / submit / CADENCE path lives in\n"
        "        // startP25LiveDecodePipeline() (MainWindowP25Orchestration.cpp).\n"
        "        // Remaining ctor timers: UI/update/diagnostics/updater (not voice DSP).\n"
        "        startP25LiveDecodePipeline();\n\n"
    )
    MW.write_text(text[:start] + replacement + text[end:], encoding="utf-8")

    method = (
        '#include "MainWindow.h"\n'
        "\n"
        "// AUTOMOC: Q_OBJECT lives in MainWindow.h.\n"
        "// ISS-0010: live GUI P25 decode pipeline (voice worker + guiDspWorker rolling IQ).\n"
        "// Mechanical extract from MainWindow::MainWindow — no hop/feed/CADENCE changes.\n"
        "\n"
        "void MainWindow::startP25LiveDecodePipeline()\n"
        "{\n"
        f"{block}"
        "}\n"
    )
    OUT.write_text(method, encoding="utf-8")

    hdr = HDR.read_text(encoding="utf-8")
    needle = "    void startP25VoiceWorker();\n"
    if "startP25LiveDecodePipeline" not in hdr:
        if needle not in hdr:
            raise SystemExit("header needle missing")
        HDR.write_text(
            hdr.replace(
                needle,
                "    void startP25LiveDecodePipeline();\n\n" + needle,
                1,
            ),
            encoding="utf-8",
        )
        # Also update header comment
        hdr2 = HDR.read_text(encoding="utf-8")
        hdr2 = hdr2.replace(
            "// Voice worker / submit / backpressure / publish: src/MainWindowP25Voice.cpp.",
            "// Voice worker / submit / backpressure / publish: src/MainWindowP25Voice.cpp.\n"
            "// Live decode pipeline (guiDspWorker): src/MainWindowP25Orchestration.cpp.",
            1,
        )
        HDR.write_text(hdr2, encoding="utf-8")

    cmake = CMAKE.read_text(encoding="utf-8")
    if "MainWindowP25Orchestration.cpp" not in cmake:
        cmake = cmake.replace(
            "    src/MainWindowP25Voice.cpp\n",
            "    src/MainWindowP25Voice.cpp\n"
            "    src/MainWindowP25Orchestration.cpp\n",
            1,
        )
        CMAKE.write_text(cmake, encoding="utf-8")

    orch = ORCH.read_text(encoding="utf-8")
    if "MainWindowP25Orchestration.cpp" not in orch:
        orch = orch.replace(
            '    ROOT / "src" / "MainWindowP25Voice.cpp",\n',
            '    ROOT / "src" / "MainWindowP25Voice.cpp",\n'
            '    ROOT / "src" / "MainWindowP25Orchestration.cpp",\n',
            1,
        )
        ORCH.write_text(orch, encoding="utf-8")

    print(f"extracted {len(block.splitlines())} lines -> {OUT.name}")
    print(f"MainWindow.cpp now {len(MW.read_text(encoding='utf-8').splitlines())} lines")


if __name__ == "__main__":
    main()
