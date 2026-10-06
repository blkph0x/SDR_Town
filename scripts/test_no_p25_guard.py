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
    import subprocess
    import re
    from types import SimpleNamespace
    assert MODULE.trace_before_text("dfd4c68", "include/P25PipelineTrace.h") == ""
    try:
        MODULE.trace_before_text("no-such-p25-test-ref", "include/P25PipelineTrace.h")
        raise AssertionError("Missing base ref must not become an added-file approval")
    except subprocess.CalledProcessError:
        pass
    trace_files = {}
    for path in MODULE.P25_TRACE_CONTEXT_DIGESTS:
        before = MODULE.trace_before_text("dfd4c68", path)
        after = subprocess.check_output(["git", "show", "7bc0fae:" + path], cwd=ROOT, text=True, encoding="utf-8")
        trace_files[("before", path)] = before
        trace_files[("after", path)] = after
        assert MODULE.infrastructure_text_allowed(path, before, after)
        assert not MODULE.infrastructure_text_allowed(path, before, after + "\nRF change")
        assert not MODULE.infrastructure_text_allowed(path, after, before)
        assert not MODULE.infrastructure_text_allowed("src/AudioEngine.cpp", before, after)
        if "false" in after:
            assert not MODULE.infrastructure_text_allowed(path, before, after.replace("false", "true", 1))
    with patch.object(MODULE, "parse_args", return_value=SimpleNamespace(base="before", head="after", paths=None)), \
         patch.object(MODULE, "git_changed_paths", return_value=list(MODULE.P25_TRACE_CONTEXT_DIGESTS)), \
         patch.object(MODULE, "git_file_text", side_effect=lambda ref, path: trace_files[(ref, path)]):
        assert MODULE.main() == 0
        trace_files[("after", "src/P25VoiceDecode.cpp")] += "\nRF change"
        assert MODULE.main() == 1
    emit_files = {}
    for path in MODULE.P25_EMIT_GAP_DIGESTS:
        before = MODULE.emit_gap_before_text("7bc0fae", path)
        after = subprocess.check_output(["git", "show", "3af218e:" + path], cwd=ROOT, text=True, encoding="utf-8")
        emit_files[("before", path)] = before
        emit_files[("after", path)] = after
        assert MODULE.infrastructure_text_allowed(path, before, after)
        assert not MODULE.infrastructure_text_allowed(path, before, after + "\nRF change")
        assert not MODULE.infrastructure_text_allowed(path, after, before)
        assert not MODULE.infrastructure_text_allowed("src/AudioEngine.cpp", before, after)
        if "false" in after:
            assert not MODULE.infrastructure_text_allowed(path, before, after.replace("false", "true", 1))
    with patch.object(MODULE, "parse_args", return_value=SimpleNamespace(base="before", head="after", paths=None)), \
         patch.object(MODULE, "git_changed_paths", return_value=list(MODULE.P25_EMIT_GAP_DIGESTS)), \
         patch.object(MODULE, "git_file_text", side_effect=lambda ref, path: emit_files[(ref, path)]), \
         patch.object(MODULE, "emit_gap_before_text", side_effect=lambda ref, path: emit_files[("before", path)]):
        assert MODULE.main() == 0
        emit_files[("after", "src/DeviceManager.cpp")] += "\nRF change"
        assert MODULE.main() == 1
    lifecycle_files = {}
    for path in MODULE.P25_FOLLOW_LIFECYCLE_DIGESTS:
        before = subprocess.check_output(["git", "show", "d3975a3:" + path], cwd=ROOT, text=True, encoding="utf-8")
        after = subprocess.check_output(["git", "show", "dfd4c68:" + path], cwd=ROOT, text=True, encoding="utf-8")
        lifecycle_files[("before", path)] = before
        lifecycle_files[("after", path)] = after
        assert MODULE.infrastructure_text_allowed(path, before, after)
        assert not MODULE.infrastructure_text_allowed(path, before, after + "\nRF change")
        assert not MODULE.infrastructure_text_allowed(path, after, before)
        assert not MODULE.infrastructure_text_allowed("src/P25LiveDecoder.cpp", before, after)
        assert not MODULE.infrastructure_text_allowed(path, before, after.replace("false", "true", 1))
    with patch.object(MODULE, "parse_args", return_value=SimpleNamespace(base="before", head="after", paths=None)), \
         patch.object(MODULE, "git_changed_paths", return_value=list(MODULE.P25_FOLLOW_LIFECYCLE_DIGESTS)), \
         patch.object(MODULE, "git_file_text", side_effect=lambda ref, path: lifecycle_files[(ref, path)]):
        assert MODULE.main() == 0
        lifecycle_files[("after", "src/P25FollowStateMachine.cpp")] += "\nRF change"
        assert MODULE.main() == 1
    observer_files = {}
    for path in MODULE.P25_OBSERVER_DIGESTS:
        before = subprocess.check_output(["git", "show", "6805bf6:" + path], cwd=ROOT, text=True, encoding="utf-8")
        after = subprocess.check_output(["git", "show", "d3975a3:" + path], cwd=ROOT, text=True, encoding="utf-8")
        observer_files[("before", path)] = before
        observer_files[("after", path)] = after
        assert MODULE.infrastructure_text_allowed(path, before, after)
        assert not MODULE.infrastructure_text_allowed(path, before, after + "\nRF change")
        assert not MODULE.infrastructure_text_allowed(path, after, before)
        assert not MODULE.infrastructure_text_allowed("src/P25VoiceDecode.cpp", before, after)
        if path.endswith(".cpp"):
            assert not MODULE.infrastructure_text_allowed(path, before, after.replace("false", "true", 1))
    with patch.object(MODULE, "parse_args", return_value=SimpleNamespace(base="before", head="after", paths=None)), \
         patch.object(MODULE, "git_changed_paths", return_value=list(MODULE.P25_OBSERVER_DIGESTS)), \
         patch.object(MODULE, "git_file_text", side_effect=lambda ref, path: observer_files[(ref, path)]):
        assert MODULE.main() == 0
        observer_files[("after", "src/P25TrafficChannelProcessor.cpp")] += "\nRF change"
        assert MODULE.main() == 1
    for path in MODULE.DRIVER_IO_ADMISSION_DIGESTS:
        before = subprocess.check_output(["git", "show", "26716b3:" + path], cwd=ROOT, text=True, encoding="utf-8")
        after = subprocess.check_output(["git", "show", "dfd4c68:" + path], cwd=ROOT, text=True, encoding="utf-8")
        expected = before.replace('#include "DeviceManager.h"', '#include "DeviceManager.h"\n#include "DriverIoMutex.h"', 1)
        expected = expected.replace("static std::mutex gSoapyLiveIoMutex;", "static DriverIoMutex gSoapyLiveIoMutex;", 1)
        expected, count = re.subn(r"std::(lock_guard|unique_lock)<std::mutex>(\s+\w+\(gSoapyLiveIoMutex)",
            r"std::\1<DriverIoMutex>\2", expected)
        assert count == 23 and after == expected
        assert MODULE.infrastructure_text_allowed(path, before, after)
        assert not MODULE.infrastructure_text_allowed(path, before, after.replace("timeNs, 100000)", "timeNs, 1000)"))
        assert not MODULE.infrastructure_text_allowed(path, after, before)
    for path in MODULE.SATELLITE_SESSION_DIGESTS:
        before = subprocess.check_output(["git", "show", "26716b3:" + path], cwd=ROOT, text=True, encoding="utf-8")
        after = subprocess.check_output(["git", "show", "d3975a3:" + path], cwd=ROOT, text=True, encoding="utf-8")
        expected = before.replace("        SatcomScannerEngine::instance().stop();", "        SatcomScannerEngine::stopAll();\n        AircraftMapWidget::stopAll();", 1)
        route = '''        if (path == "/v1/satcom/sessions" || path == "/v1/aircraft/sessions") {
            auto* hub = findChild<SatcomHubWidget*>(QStringLiteral("satcomHub"));
            if (!hub) return {{"ok",false},{"status",503},{"error","Satellite workspace unavailable"}};
            return hub->controlReceiverSessions(path == "/v1/satcom/sessions" ? "satcom" : "aircraft", method, body);
        }
'''
        marker = '        if (path == "/v1/inmarsat/bandplans" && method == "GET") {'
        assert after == expected.replace(marker, route + marker, 1)
        assert MODULE.infrastructure_text_allowed(path, before, after)
        assert not MODULE.infrastructure_text_allowed(path, before, after + "\nRF change")
        assert not MODULE.infrastructure_text_allowed(path, after, before)
        assert not MODULE.infrastructure_text_allowed("src/P25LiveDecoder.cpp", before, after)
    for path in MODULE.INMARSAT_SESSION_DIGESTS:
        before = subprocess.check_output(["git", "show", "63acf32:" + path], cwd=ROOT, text=True, encoding="utf-8")
        after = subprocess.check_output(["git", "show", "26716b3:" + path], cwd=ROOT, text=True, encoding="utf-8")
        expected = before.replace("    InmarsatEngine::instance().stop();", "    InmarsatEngine::stopAll();", 1)
        route = '''        if (path == "/v1/inmarsat/sessions") {
            auto* hub = findChild<SatcomHubWidget*>(QStringLiteral("satcomHub"));
            if (!hub) return {{"ok",false},{"status",503},{"error","Inmarsat workspace unavailable"}};
            return hub->controlInmarsatSessions(method, body);
        }
'''
        marker = '        if (path == "/v1/inmarsat/bandplans" && method == "GET") {'
        assert after == expected.replace(marker, route + marker, 1)
        assert MODULE.infrastructure_text_allowed(path, before, after)
        assert not MODULE.infrastructure_text_allowed(path, before, after + "\nRF change")
        assert not MODULE.infrastructure_text_allowed(path, after, before)
        assert not MODULE.infrastructure_text_allowed("src/P25LiveDecoder.cpp", before, after)
    for path in MODULE.WORKFLOW_SHUTDOWN_DIGESTS:
        before = subprocess.check_output(["git", "show", "a7624ee:" + path], cwd=ROOT, text=True, encoding="utf-8")
        after = subprocess.check_output(["git", "show", "63acf32:" + path], cwd=ROOT, text=True, encoding="utf-8")
        assert MODULE.infrastructure_text_allowed(path, before, after)
        assert not MODULE.infrastructure_text_allowed(path, before, after + "\nRF change")
        assert not MODULE.infrastructure_text_allowed(path, after, before)
        assert not MODULE.infrastructure_text_allowed("src/P25LiveDecoder.cpp", before, after)
        assert after.index("InmarsatEngine::instance().stop();", after.index("void MainWindow::stopAllStreaming()")) < after.index("SatcomHostServices::instance().clear();", after.index("void MainWindow::stopAllStreaming()"))
    for path in MODULE.WORKFLOW_WINDOW_DIGESTS:
        before = subprocess.check_output(["git", "show", "9371201:" + path], cwd=ROOT, text=True, encoding="utf-8")
        after = subprocess.check_output(["git", "show", "a7624ee:" + path], cwd=ROOT, text=True, encoding="utf-8")
        assert MODULE.infrastructure_text_allowed(path, before, after)
        assert not MODULE.infrastructure_text_allowed(path, after, before)
        assert not MODULE.infrastructure_text_allowed(path, before, after + "\nRF change")
        assert not MODULE.infrastructure_text_allowed("src/P25LiveDecoder.cpp", before, after)
        assert not MODULE.infrastructure_text_allowed(path, before, after.replace("false", "true", 1))
    for path in MODULE.CONTROL_OWNERSHIP_DIGESTS:
        before = subprocess.check_output(["git", "show", "d0f1633:" + path], cwd=ROOT, text=True, encoding="utf-8")
        after = subprocess.check_output(["git", "show", "9371201:" + path], cwd=ROOT, text=True, encoding="utf-8")
        assert MODULE.infrastructure_text_allowed(path, before, after)
        assert not MODULE.infrastructure_text_allowed(path, after, before)
        assert not MODULE.infrastructure_text_allowed(path, before, after + "\nRF change")
        assert not MODULE.infrastructure_text_allowed("src/P25LiveDecoder.cpp", before, after)
        assert "false" in after
        assert not MODULE.infrastructure_text_allowed(path, before, after.replace("false", "true", 1))
        if path == "src/DeviceManager.cpp":
            assert not MODULE.infrastructure_text_allowed(path, before, after.replace("if (!control) return false;", "if (false) return false;"))
        if path == "src/P25VoiceDecode.cpp":
            assert not MODULE.infrastructure_text_allowed(path, before, after.replace("DeviceLeaseOwner::P25", "DeviceLeaseOwner::Listen"))
    for path in MODULE.REPEATER_ROUTING_DIGESTS:
        before = subprocess.check_output(["git", "show", "53a518d:" + path], cwd=ROOT, text=True, encoding="utf-8")
        after = subprocess.check_output(["git", "show", "dfd4c68:" + path], cwd=ROOT, text=True, encoding="utf-8")
        assert MODULE.infrastructure_text_allowed(path, before, after)
        assert not MODULE.infrastructure_text_allowed(path, after, before)
        assert not MODULE.infrastructure_text_allowed(path, before, after + "\nRF change")
        assert not MODULE.infrastructure_text_allowed("src/P25LiveDecoder.cpp", before, after)
        assert not MODULE.infrastructure_text_allowed(path, before, after.replace("ownsRepeaterControls && repeaterDualWatchWanted", "repeaterDualWatchWanted"))
        assert not MODULE.infrastructure_text_allowed(path, before, after.replace("if (ownsRepeaterControls)", "if (true)"))
        assert not MODULE.infrastructure_text_allowed(path, before, after.replace("receivers.empty() ? nullptr : receivers.front()", "rxSnapshot.front()"))
    for path in MODULE.WORKFLOW_DIGESTS:
        before = subprocess.check_output(["git", "show", "10259ca:" + path], cwd=ROOT, text=True, encoding="utf-8")
        after = subprocess.check_output(["git", "show", "ba26fe3:" + path], cwd=ROOT, text=True, encoding="utf-8")
        assert MODULE.infrastructure_text_allowed(path, before, after)
        assert not MODULE.infrastructure_text_allowed(path, before, after + "\nRF change")
        assert not MODULE.infrastructure_text_allowed(path, after, before)
        assert not MODULE.infrastructure_text_allowed("src/P25LiveDecoder.cpp", before, after)
        assert not MODULE.infrastructure_text_allowed(path, before, after.replace("false", "true", 1))
    for path in MODULE.WORKFLOW_ROUTING_DIGESTS:
        before = subprocess.check_output(["git", "show", "ba26fe3:" + path], cwd=ROOT, text=True, encoding="utf-8")
        after = subprocess.check_output(["git", "show", "53a518d:" + path], cwd=ROOT, text=True, encoding="utf-8")
        assert MODULE.infrastructure_text_allowed(path, before, after)
        assert not MODULE.infrastructure_text_allowed(path, before, after + "\nRF change")
        assert not MODULE.infrastructure_text_allowed(path, after, before)
        assert not MODULE.infrastructure_text_allowed("src/P25LiveDecoder.cpp", before, after)
        assert not MODULE.infrastructure_text_allowed(path, before, after.replace("false", "true", 1))
    for path in MODULE.INFRASTRUCTURE_DIGESTS:
        before = subprocess.check_output(["git", "show", "d47f000:" + path], cwd=ROOT, text=True, encoding="utf-8")
        after = subprocess.check_output(["git", "show", "10259ca:" + path], cwd=ROOT, text=True, encoding="utf-8")
        assert MODULE.infrastructure_text_allowed(path, before, after)
        assert not MODULE.infrastructure_text_allowed(path, before, after + "\nRF change")
        assert not MODULE.infrastructure_text_allowed(path, before + "\n", after)
        assert not MODULE.infrastructure_text_allowed(path, after, before)
        assert not MODULE.infrastructure_text_allowed("src/P25LiveDecoder.cpp", before, after)
        assert not MODULE.infrastructure_text_allowed("src/AudioEngine.cpp", before, after)
        # Detect even a single substituted literal inside otherwise approved text.
        assert "false" in after
        assert not MODULE.infrastructure_text_allowed(path, before, after.replace("false", "true", 1))
    path = "src/DeviceManager.cpp"
    before = subprocess.check_output(["git", "show", "90edc4a:" + path], cwd=ROOT, text=True, encoding="utf-8")
    after = subprocess.check_output(["git", "show", "10259ca:" + path], cwd=ROOT, text=True, encoding="utf-8")
    assert MODULE.infrastructure_text_allowed(path, before, after)
    assert not MODULE.infrastructure_text_allowed(path, before, after + "\nchange")
    assert not MODULE.infrastructure_text_allowed(path, after, before)
    assert not MODULE.infrastructure_text_allowed("src/P25LiveDecoder.cpp", before, after)
    assert not MODULE.infrastructure_text_allowed(path, before, after.replace("available == 0", "available == 1", 1))
    path="src/MainWindow.cpp"
    before=subprocess.check_output(["git","show","127469d:"+path],cwd=ROOT,text=True,encoding="utf-8")
    # Historical exact-patch fixtures must not inherit later infrastructure work.
    after=subprocess.check_output(["git","show","d47f000:"+path],cwd=ROOT,text=True,encoding="utf-8")
    before_dtmf = MODULE.without_dtmf_menu(after)
    assert MODULE.dtmf_text_allowed(path, before_dtmf, after)
    assert not MODULE.dtmf_text_allowed(path, before_dtmf, after+"\nchange")
    assert not MODULE.dtmf_text_allowed(path, before_dtmf, after.replace("receiver = receivers.front()", "receiver = receivers.back()"))
    assert not MODULE.dtmf_text_allowed("src/P25LiveDecoder.cpp", before_dtmf, after)
    assert not MODULE.dtmf_text_allowed(path, after, before_dtmf)
    orchestration="src/MainWindowP25Orchestration.cpp"
    old=subprocess.check_output(["git","show","ab21a4f:"+orchestration],cwd=ROOT,text=True,encoding="utf-8")
    new=subprocess.check_output(["git","show","10259ca:"+orchestration],cwd=ROOT,text=True,encoding="utf-8")
    assert MODULE.dtmf_text_allowed(orchestration,old,new)
    assert not MODULE.dtmf_text_allowed(orchestration,old,new+"\nchange")
    assert not MODULE.dtmf_text_allowed(orchestration,old,new.replace("repEnabled && dualActive", "repEnabled || dualActive"))
    assert not MODULE.dtmf_text_allowed(orchestration,new,old)
    after = before_dtmf
    before_cw = after.replace(MODULE.CW_INCLUDES, "", 1).replace(MODULE.CW_MENU, "", 1)
    assert MODULE.cw_window_text_allowed(path, before_cw, after)
    assert not MODULE.cw_window_text_allowed(path, before_cw, after + "\nchange")
    assert not MODULE.cw_window_text_allowed(path, before_cw, after.replace("receiver = receivers.front()", "receiver = receivers.back()"))
    assert not MODULE.cw_window_text_allowed(path, after, before_cw)
    assert not MODULE.cw_window_text_allowed("src/P25LiveDecoder.cpp", before_cw, after)
    assert MODULE.diagnostics_text_allowed(path,before,after)
    assert not MODULE.diagnostics_text_allowed(path,before,after+"\nchange")
    assert not MODULE.diagnostics_text_allowed(path,before+"\n",after)
    assert not MODULE.diagnostics_text_allowed(path,after,before)
    assert not MODULE.diagnostics_text_allowed("src/P25LiveDecoder.cpp",before,after)
    prior_ui = after.replace(MODULE.WORKSPACE_UI_BLOCK, "", 1)
    assert MODULE.workspace_ui_text_allowed(path, prior_ui, after)
    assert not MODULE.workspace_ui_text_allowed(path, prior_ui, after + "\nchange")
    assert not MODULE.workspace_ui_text_allowed(path, after, prior_ui)
    assert not MODULE.workspace_ui_text_allowed("src/P25LiveDecoder.cpp", prior_ui, after)
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
    with patch.object(MODULE, "AUTO_BW_DIGESTS", reviewed):
        assert MODULE.auto_bw_text_allowed("src/MainWindow.cpp", before, after)
        assert not MODULE.auto_bw_text_allowed("src/MainWindow.cpp", before, after + "\nchange")
        assert not MODULE.auto_bw_text_allowed("src/MainWindow.cpp", before + "\n", after)
        assert not MODULE.auto_bw_text_allowed("src/MainWindow.cpp", after, before)
        assert not MODULE.auto_bw_text_allowed("src/P25LiveDecoder.cpp", before, after)
    print("Auto BW exact-patch negative mutation tests passed")
    with patch.object(MODULE, "WFM_PCM_DIGESTS", reviewed_nfm):
        assert MODULE.wfm_pcm_text_allowed("src/Demod.cpp", before, after)
        assert not MODULE.wfm_pcm_text_allowed("src/Demod.cpp", before, after + "\nchange")
        assert not MODULE.wfm_pcm_text_allowed("src/Demod.cpp", before + "\n", after)
        assert not MODULE.wfm_pcm_text_allowed("src/Demod.cpp", after, before)
        assert not MODULE.wfm_pcm_text_allowed("src/P25LiveDecoder.cpp", before, after)
    print("WFM exact-patch negative mutation tests passed")
    with patch.object(MODULE, "WFM_FIR_DIGESTS", reviewed_nfm):
        assert MODULE.wfm_fir_text_allowed("src/Demod.cpp", before, after)
        assert not MODULE.wfm_fir_text_allowed("src/Demod.cpp", before, after + "\nchange")
        assert not MODULE.wfm_fir_text_allowed("src/Demod.cpp", before + "\n", after)
        assert not MODULE.wfm_fir_text_allowed("src/Demod.cpp", after, before)
        assert not MODULE.wfm_fir_text_allowed("src/P25LiveDecoder.cpp", before, after)
    print("WFM FIR exact-patch negative mutation tests passed")
    with patch.object(MODULE, "NFM_INPUT_DIGESTS", reviewed_nfm):
        assert MODULE.nfm_input_text_allowed("src/Demod.cpp", before, after)
        assert not MODULE.nfm_input_text_allowed("src/Demod.cpp", before, after + "\nchange")
        assert not MODULE.nfm_input_text_allowed("src/Demod.cpp", before + "\n", after)
        assert not MODULE.nfm_input_text_allowed("src/Demod.cpp", after, before)
        assert not MODULE.nfm_input_text_allowed("src/P25LiveDecoder.cpp", before, after)
    print("NFM input exact-patch negative mutation tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
