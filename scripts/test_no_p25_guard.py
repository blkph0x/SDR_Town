#!/usr/bin/env python3
"""Unit checks for verify_no_p25_changes.py."""

from __future__ import annotations

import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = ROOT / "scripts" / "verify_no_p25_changes.py"
SPEC = importlib.util.spec_from_file_location("verify_no_p25_changes", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def expect_blocked(path: str) -> None:
    reason = MODULE.protected_reason(path)
    assert reason is not None, f"expected protected path: {path}"


def expect_allowed(path: str) -> None:
    reason = MODULE.protected_reason(path)
    assert reason is None, f"expected allowed path: {path}; matched {reason}"


def main() -> int:
    for path in (
        "src/P25LiveDecoder.cpp",
        "include/P25Control.h",
        "src/dsp/P25Phase2Framer.cpp",
        "src/MainWindowP25Voice.cpp",
        "tests/test_p25live.cpp",
        "src/tools/verify_p25_phase2_slot_mapping.py",
        "external/mbelib",
        "_codex_refs/op25",
        "src/DeviceManager.cpp",
        "include/Receiver.h",
        "src/Demod.cpp",
        "src/AudioEngine.cpp",
        "src/MainWindow.cpp",
    ):
        expect_blocked(path)

    for path in (
        "src/SatcomScannerEngine.cpp",
        "include/SatcomScannerEngine.h",
        "src/SdrplayProfile.cpp",
        "src/Ax25AprsDecoder.cpp",
        "cmake/SstvBackend.cmake",
        ".github/workflows/windows-ci.yml",
        "docs/NON_P25_HARDENING.md",
    ):
        expect_allowed(path)

    blocked = MODULE.protected_paths(
        ["src/SatcomScannerEngine.cpp", "src/P25VoiceDecode.cpp", "src/ModeS.cpp"]
    )
    assert blocked == [("src/P25VoiceDecode.cpp", "src/P25*")]

    allowed_diff = """diff --git a/src/MainWindow.cpp b/src/MainWindow.cpp
--- a/src/MainWindow.cpp
+++ b/src/MainWindow.cpp
@@ -1,0 +2 @@
+#include "SatcomHostServices.h"
@@ -10,0 +12,3 @@
+// SATCOM_HOST_INTEGRATION_BEGIN
+SatcomHostServices::instance().install({});
+// SATCOM_HOST_INTEGRATION_END
"""
    allowed, detail = MODULE.mainwindow_satcom_diff_allowed(allowed_diff)
    assert allowed, detail

    outside_marker = """diff --git a/src/MainWindow.cpp b/src/MainWindow.cpp
--- a/src/MainWindow.cpp
+++ b/src/MainWindow.cpp
@@ -10,0 +11 @@
+currentMonitorFreq = 0.0;
"""
    allowed, _ = MODULE.mainwindow_satcom_diff_allowed(outside_marker)
    assert not allowed

    destructive = """diff --git a/src/MainWindow.cpp b/src/MainWindow.cpp
--- a/src/MainWindow.cpp
+++ b/src/MainWindow.cpp
@@ -10 +10 @@
-currentMonitorFreq = 100e6;
+// SATCOM_HOST_INTEGRATION_BEGIN
+currentMonitorFreq = 0.0;
+// SATCOM_HOST_INTEGRATION_END
"""
    allowed, _ = MODULE.mainwindow_satcom_diff_allowed(destructive)
    assert not allowed

    print("P25 guard self-test passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
