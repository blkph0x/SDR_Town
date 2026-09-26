#!/usr/bin/env python3
"""Unit checks for verify_no_p25_changes.py."""

from __future__ import annotations

import importlib.util
from unittest.mock import patch
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

    base_demod = '''#include "Demod.h"
Demodulator::~Demodulator() = default;
void Demodulator::resetState() {
    resetMultiplexState();
}
void run() {
    if (mode == DemodMode::AUTO) mode = DemodMode::NFM;
    rmsOut = -100;
}
'''
    head_demod = base_demod.replace(
        '#include "Demod.h"\n',
        '#include "Demod.h"\n' + MODULE.HF_INCLUDE,
    ).replace(
        MODULE.HF_OLD_DESTRUCTOR,
        MODULE.HF_LIFECYCLE_BLOCK,
    ).replace(
        "void Demodulator::resetState() {\n",
        "void Demodulator::resetState() {\n" + MODULE.HF_RESET_BLOCK,
    ).replace(
        "    if (mode == DemodMode::AUTO) mode = DemodMode::NFM;\n",
        "    if (mode == DemodMode::AUTO) mode = DemodMode::NFM;\n"
        + MODULE.HF_DELEGATE_BLOCK,
    )

    transformed = head_demod
    for block, replacement in (
        (MODULE.HF_INCLUDE, ""),
        (MODULE.HF_LIFECYCLE_BLOCK, MODULE.HF_OLD_DESTRUCTOR),
        (MODULE.HF_RESET_BLOCK, ""),
        (MODULE.HF_DELEGATE_BLOCK, ""),
    ):
        assert transformed.count(block) == 1
        transformed = transformed.replace(block, replacement, 1)
    assert transformed == base_demod

    tampered = head_demod + "\n// unrelated P25-path change\n"
    transformed = tampered
    for block, replacement in (
        (MODULE.HF_INCLUDE, ""),
        (MODULE.HF_LIFECYCLE_BLOCK, MODULE.HF_OLD_DESTRUCTOR),
        (MODULE.HF_RESET_BLOCK, ""),
        (MODULE.HF_DELEGATE_BLOCK, ""),
    ):
        transformed = transformed.replace(block, replacement, 1)
    assert transformed != base_demod

    # Exercise the exact digest-pair gate independently of Git history; both
    # sides must match. Arbitrary setup-function or RF changes remain blocked.
    import hashlib
    before, after = "reviewed old loader", "reviewed new loader"
    with patch.object(MODULE, "SDRPLAY_DEVICE_BEFORE", hashlib.sha256(before.encode()).hexdigest()), \
         patch.object(MODULE, "SDRPLAY_DEVICE_AFTER", hashlib.sha256(after.encode()).hexdigest()):
        assert MODULE.device_manager_sdrplay_text_allowed(before, after)
        assert not MODULE.device_manager_sdrplay_text_allowed(before, after + "\nRF change")
        assert not MODULE.device_manager_sdrplay_text_allowed(before + "\n", after)
        assert not MODULE.device_manager_sdrplay_text_allowed(after, before)

    reviewed = {"src/MainWindow.cpp": (
        hashlib.sha256(before.encode()).hexdigest(),
        hashlib.sha256(after.encode()).hexdigest())}
    with patch.object(MODULE, "SDRPLAY_CONTROL_DIGESTS", reviewed):
        assert MODULE.sdrplay_control_text_allowed("src/MainWindow.cpp", before, after)
        assert not MODULE.sdrplay_control_text_allowed("src/MainWindow.cpp", before, after + "\nRF change")
        assert not MODULE.sdrplay_control_text_allowed("src/MainWindow.cpp", before + "\n", after)
        assert not MODULE.sdrplay_control_text_allowed("src/MainWindow.cpp", after, before)
        assert not MODULE.sdrplay_control_text_allowed("src/P25LiveDecoder.cpp", before, after)
        assert not MODULE.sdrplay_control_text_allowed("include/DeviceManager.h", before, after)

    with patch.object(MODULE, "RTL_BIAS_DIGESTS", reviewed):
        assert MODULE.rtl_bias_text_allowed("src/MainWindow.cpp", before, after)
        assert not MODULE.rtl_bias_text_allowed("src/MainWindow.cpp", before, after + "\nRF change")
        assert not MODULE.rtl_bias_text_allowed("src/MainWindow.cpp", before + "\n", after)
        assert not MODULE.rtl_bias_text_allowed("src/MainWindow.cpp", after, before)
        assert not MODULE.rtl_bias_text_allowed("src/P25LiveDecoder.cpp", before, after)
        assert not MODULE.rtl_bias_text_allowed("include/DeviceManager.h", before, after)

    print("P25 guard self-test passed")
    reviewed_nfm = {"src/Demod.cpp": (
        hashlib.sha256(before.encode()).hexdigest(),
        hashlib.sha256(after.encode()).hexdigest())}
    with patch.object(MODULE, "NFM_CONTINUITY_DIGESTS", reviewed_nfm):
        assert MODULE.nfm_continuity_text_allowed("src/Demod.cpp", before, after)
        assert not MODULE.nfm_continuity_text_allowed("src/Demod.cpp", before, after + "\nchange")
        assert not MODULE.nfm_continuity_text_allowed("src/Demod.cpp", before + "\n", after)
        assert not MODULE.nfm_continuity_text_allowed("src/Demod.cpp", after, before)
        assert not MODULE.nfm_continuity_text_allowed("src/P25LiveDecoder.cpp", before, after)
    print("NFM exact-patch negative mutation tests passed")
    with patch.object(MODULE, "NFM_PCM_DIGESTS", reviewed_nfm):
        assert MODULE.nfm_pcm_text_allowed("src/Demod.cpp", before, after)
        assert not MODULE.nfm_pcm_text_allowed("src/Demod.cpp", before, after + "\nchange")
        assert not MODULE.nfm_pcm_text_allowed("src/Demod.cpp", before + "\n", after)
        assert not MODULE.nfm_pcm_text_allowed("src/Demod.cpp", after, before)
        assert not MODULE.nfm_pcm_text_allowed("src/P25LiveDecoder.cpp", before, after)
    print("NFM PCM exact-patch negative mutation tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
