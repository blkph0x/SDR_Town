#!/usr/bin/env python3
"""Fail when a non-P25 hardening change touches the frozen P25 pipeline.

The current clear P25 implementation is a release invariant.  This script is
intentionally conservative: besides P25-named implementation files, it protects
shared RF/audio/orchestration files whose behaviour directly feeds the P25
chain.  Run it from CI with a base and head ref, or pass explicit paths for a
fast local/pre-commit check.
"""

from __future__ import annotations

import argparse
import fnmatch
import hashlib
import os
import subprocess
import sys
from pathlib import Path
from typing import Iterable

PROTECTED_PATTERNS: tuple[str, ...] = (
    "src/P25*",
    "include/P25*",
    "src/dsp/P25*",
    "include/dsp/P25*",
    "src/MainWindowP25*.cpp",
    "tests/test_p25*",
    "src/tools/*p25*",
    "external/mbelib",
    "external/mbelib/**",
    "_codex_refs/op25",
    "_codex_refs/op25/**",
    "_codex_refs/sdrtrunk",
    "_codex_refs/sdrtrunk/**",
    # Shared pipeline files are frozen on this branch as well.  Even a change
    # made for another feature could alter sample timing, tune sequencing,
    # demod state, speaker buffering, or P25 orchestration.
    "src/DeviceManager.cpp",
    "include/DeviceManager.h",
    "src/Receiver.cpp",
    "include/Receiver.h",
    "src/Demod.cpp",
    "include/Demod.h",
    "src/AudioEngine.cpp",
    "include/AudioEngine.h",
    "src/MainWindow.cpp",
    "include/MainWindow.h",
    "src/CliApp.cpp",
)

SATCOM_MAINWINDOW_PATH = "src/MainWindow.cpp"
# DEC-0171 / T-0102: reviewed RX hardware-loss boundary and numeric counters,
# explicit fail-closed tone TX, and opt-in health observers only. No P25 DSP,
# vocoder, slot/security gate or AudioEngine changes are accepted by this pair.
INFRASTRUCTURE_DIGESTS = {
    "src/DeviceManager.cpp": (
        "568056eeb297ff32786255c78c51723c553ba11d1dd4940104d05a4bd3a77fd3",
        "78c94d69c87650c4e3b1469f826e3b0d4d309d5daec340da187533121ea01112"),
    "include/DeviceManager.h": (
        "e581e7d2f1103c5fc456576fc73d8df5d306889295013abecf8430f03c64e302",
        "1444bb34ee7da5d7bdaea0dd1d4de77d8491787704e1cbb65f92e46aaadac33b"),
    "src/MainWindow.cpp": (
        "fb2a5565a37718fad24a7f245ae31287d8906ebc10a3da3df1998a3176f36aa9",
        "7354c636fb731ac250c0aa9aa1a900cadc78baa465ff9e617d43d5d97f97b40d"),
    "include/MainWindow.h": (
        "5d8a753ed52b64ec793a51e0eca949158f3ce029a24fb14b6f1788b4d1c7439b",
        "e1b4224b500f961de282d6e0cfe779551a5e6a44ab3bf19e6320ff97d5439019"),
}

LOSS_ACK_DIGESTS = {
    # DEC-0171 follow-up: retain epoch notification across an empty consumer poll.
    "src/DeviceManager.cpp": (
        "2d24a948dfdd1bf57ef0a77d522c2f31d7b13b354e26635891a593ecec1887e8",
        "78c94d69c87650c4e3b1469f826e3b0d4d309d5daec340da187533121ea01112"),
}

# DEC-0181 / T-0103: exact reviewed ownership/assignment patch. Source selection
# may reject reserved radios; no demodulator, vocoder or speaker timing edits.
WORKFLOW_DIGESTS = {
    "src/CliApp.cpp": (
        "272b694b177a186ab62ddf38dade9e1def8d03c4a44636752debd5c0ca9e0839",
        "21f6522f5a61cd8dc3537b21c0fd2cc7f7e6ca36ecf9f9c5d6cd3cfc2f8a662d"),
    "src/DeviceManager.cpp": (
        "78c94d69c87650c4e3b1469f826e3b0d4d309d5daec340da187533121ea01112",
        "14a3753c9cd4f4b2d75cee02bee574ceb08c26748930a9ce1295cd55b938e612"),
    "include/DeviceManager.h": (
        "1444bb34ee7da5d7bdaea0dd1d4de77d8491787704e1cbb65f92e46aaadac33b",
        "c19c6dde380f1ee1cd8ca59208cfd0445d32da857437dc78c405b0cfe2dc85e2"),
    "src/MainWindow.cpp": (
        "7354c636fb731ac250c0aa9aa1a900cadc78baa465ff9e617d43d5d97f97b40d",
        "055f919232c2dc78e8be9628c031cdea70874bd382f1e5183598e5e551b9a219"),
}


# DEC-0182: exact GUI/CLI routing and token-scoped satellite/aircraft lifecycle
# follow-up to ba26fe3. No P25 DSP, receive cadence or audio implementation edits.
WORKFLOW_ROUTING_DIGESTS = {
    "src/CliApp.cpp": (
        "21f6522f5a61cd8dc3537b21c0fd2cc7f7e6ca36ecf9f9c5d6cd3cfc2f8a662d",
        "450dbf7743e23939e1c0385fcac82fb6a542f76b19c72c270936a2565c1fccee"),
    "src/DeviceManager.cpp": (
        "14a3753c9cd4f4b2d75cee02bee574ceb08c26748930a9ce1295cd55b938e612",
        "d7affe51d89a1d5b91f78963cab65aaba71b24168e03d040c65d0599b715a45e"),
    "include/DeviceManager.h": (
        "c19c6dde380f1ee1cd8ca59208cfd0445d32da857437dc78c405b0cfe2dc85e2",
        "5fba5178a13fe71b39e7b16eb713400d1a447f685412443a7e3d1edb0f61abcc"),
    "src/MainWindow.cpp": (
        "055f919232c2dc78e8be9628c031cdea70874bd382f1e5183598e5e551b9a219",
        "1bee21e1adb220ea826a02927cebe045639e925e8012b0b74062a6f55a1559d8"),
}


# DEC-0183: bind only the analog repeater controller to its logical receiver;
# preserve all P25 branches and active-list ordering in the shared worker.
REPEATER_ROUTING_DIGESTS = {
    "src/MainWindowP25Orchestration.cpp": (
        "713eeb72bf0e0e99647a4f8de31601da4133f83a892f61876bba3683e56f13e1",
        "f0bf8a6b2e34f57b29cd4a6e502678fd93c2fea3c2d3151ee414de16ed57e50e"),
}

# DEC-0184: exact reviewed hardware-command fencing, error propagation and
# legacy P25 ownership adapter. No decoder, vocoder or playout changes.
CONTROL_OWNERSHIP_DIGESTS = {
    "include/DeviceManager.h": (
        "5fba5178a13fe71b39e7b16eb713400d1a447f685412443a7e3d1edb0f61abcc",
        "09be7f885a929b124d36d824fbc7376d37e271871865c8500d3e442f7a92ccb1"),
    "src/DeviceManager.cpp": (
        "d7affe51d89a1d5b91f78963cab65aaba71b24168e03d040c65d0599b715a45e",
        "7b8ee8f740adee826ac669bd54fa90eaced4611310b4c3c2fbed3cadc7d4660c"),
    "src/MainWindow.cpp": (
        "1bee21e1adb220ea826a02927cebe045639e925e8012b0b74062a6f55a1559d8",
        "391cc6cc0f98c48243ca7c7716199f060d4ba5e4ceb21bec6bd1e71bdff4f810"),
    "src/CliApp.cpp": (
        "450dbf7743e23939e1c0385fcac82fb6a542f76b19c72c270936a2565c1fccee",
        "4e1094a11b6bf05ea974a9b0a6f250efd8aae0c28fb5af6120e357f9ec600ffb"),
    "src/P25VoiceDecode.cpp": (
        "e59af41048c18518a1003ec46575530d835ad8bcb5c3a35ef972ecc59b1fd2c1",
        "0c30057dd2225e997bfbfbc45cc52c8d80dba380ac33ab33c3c84a059fca5c18"),
}


# DEC-0185: only named decoder windows/status/API and independent observers.
# No P25, device ownership, tuner or speaker behavior changes in this pair.
WORKFLOW_WINDOW_DIGESTS = {
    "src/MainWindow.cpp": (
        "391cc6cc0f98c48243ca7c7716199f060d4ba5e4ceb21bec6bd1e71bdff4f810",
        "f310b5f986c86c51af4a9843c0c51d1874daa74b2491d46fcdcae6e133c5c773"),
    "include/MainWindow.h": (
        "e1b4224b500f961de282d6e0cfe779551a5e6a44ab3bf19e6320ff97d5439019",
        "7190137f5ae45d3501288af8cfcc547a01186a6cab0f5d7996e1ab07f21347e8"),
}


# DEC-0185 follow-up: explicit Inmarsat stop/join before shared host teardown.
WORKFLOW_SHUTDOWN_DIGESTS = {
    "src/MainWindow.cpp": (
        "f310b5f986c86c51af4a9843c0c51d1874daa74b2491d46fcdcae6e133c5c773",
        "fbb235ef625cd9b65c6f4454df845bf28b7e77c3461d051d2f7be48ca44394be"),
}

# DEC-0187: all-session shutdown and forwarding the authenticated session route.
INMARSAT_SESSION_DIGESTS = {
    "src/MainWindow.cpp": (
        "fbb235ef625cd9b65c6f4454df845bf28b7e77c3461d051d2f7be48ca44394be",
        "82f145c49ecdcb8582b3f2608c08f961bcf77763fa8dbabb210d67b335788c91"),
}

# DEC-0189: satellite/aircraft route forwarding and all-controller shutdown only.
SATELLITE_SESSION_DIGESTS = {
    "src/MainWindow.cpp": (
        "82f145c49ecdcb8582b3f2608c08f961bcf77763fa8dbabb210d67b335788c91",
        "b0181884f8d80d5c7370303bad751b5b812f553296d725326604881c263bc976"),
}

# DEC-0190: identical driver critical sections with FIFO mutex admission.
DRIVER_IO_ADMISSION_DIGESTS = {
    "src/DeviceManager.cpp": (
        "7b8ee8f740adee826ac669bd54fa90eaced4611310b4c3c2fbed3cadc7d4660c",
        "82b7115943eb9b015962d1bd315c0133f4e1f6b06a87e41fe660d6e32add692d"),
}


def infrastructure_text_allowed(path: str, before: str, after: str) -> bool:
    actual = (
        hashlib.sha256(before.encode("utf-8")).hexdigest(),
        hashlib.sha256(after.encode("utf-8")).hexdigest(),
    )
    return actual in (INFRASTRUCTURE_DIGESTS.get(path), LOSS_ACK_DIGESTS.get(path),
                      WORKFLOW_DIGESTS.get(path), WORKFLOW_ROUTING_DIGESTS.get(path), REPEATER_ROUTING_DIGESTS.get(path),
                      CONTROL_OWNERSHIP_DIGESTS.get(path), WORKFLOW_WINDOW_DIGESTS.get(path),
                      WORKFLOW_SHUTDOWN_DIGESTS.get(path), INMARSAT_SESSION_DIGESTS.get(path),
                      SATELLITE_SESSION_DIGESTS.get(path), DRIVER_IO_ADMISSION_DIGESTS.get(path))


# DEC-0160: read-only audio telemetry and consent checks; no DSP/follow edits.
DIAGNOSTICS_DIGESTS = {
    "src/MainWindow.cpp": (
        "7e309ca8c17c89c5dd181a7f40b4a594f4e952ea963a69d0dbea8a97ea640c91",
        "833602464b0c73e1752970a1f86340a4aff78ea4466d51d840011df4b8366997"),
}


WORKSPACE_UI_BLOCK = '''        // DEC-0166: reparent presentation only; keep the existing monitor and signals.
        rxLay->removeWidget(repeaterBox);
        workspaceLayout->addPanel("repeater", "Repeater Tones", repeaterBox);
        setMonBtn->setText("Tune and Receive");
        scanBtn->setText("Start Smart Scan");
'''


CW_INCLUDES = '#include "CwWindow.h"\n#include "CwRfSession.h"\n'
DTMF_INCLUDE = '#include "DtmfWindow.h"\n'
DTMF_MENU = '''        // DEC-0168: DTMF observer settings only; no tuning or audio controls.
        toolsMenu->addAction("DTMF Analysis...", this, [this] {
            auto* window = findChild<DtmfWindow*>("dtmfWindow");
            if (!window) window = new DtmfWindow([this](bool input) -> std::shared_ptr<DtmfDecoder> {
                std::lock_guard lock(receiversMutex);
                if (receivers.empty()) return {};
                auto receiver = receivers.front();
                return {receiver, input ? &receiver->inputWatchDtmf : &receiver->dtmf};
            }, this);
            window->show(); window->raise(); window->activateWindow();
        });
'''
DTMF_SKIP_BEFORE = '''                            // Rate-limit to every 4th block so NFM audio stays realtime; DTMF still
                            // catches keypad bursts which last tens of ms.
'''
DTMF_SKIP_AFTER = '''                            // DEC-0168: tone timing needs every chronological IQ block.
                            // Skipping blocks here loses bursts and resets all input-leg decoders.
'''
DTMF_CONDITION_BEFORE = '''                                ++rx.inputWatchSkipCounter;
                                const bool runInputWatch = (rx.inputWatchSkipCounter % 4u) == 1u;
                                if (runInputWatch) {
'''


def dtmf_text_allowed(path: str, before: str, after: str) -> bool:
    if path == "src/MainWindow.cpp":
        include = '#include "SstvWindow.h"\n'
        menu = '        QMenu* toolsMenu = menuBar()->addMenu("&Tools");\n'
        return (before.count(include) == 1 and before.count(menu) == 1
                and DTMF_INCLUDE not in before and DTMF_MENU not in before
                and after == before.replace(include, include + DTMF_INCLUDE, 1).replace(menu, menu + DTMF_MENU, 1))
    if path == "src/MainWindowP25Orchestration.cpp":
        return (before.count(DTMF_SKIP_BEFORE) == 1 and before.count(DTMF_CONDITION_BEFORE) == 1
                and after == before.replace(DTMF_SKIP_BEFORE, DTMF_SKIP_AFTER, 1)
                .replace(DTMF_CONDITION_BEFORE, "                                {\n", 1))
    return False


def without_dtmf_menu(text: str) -> str:
    if text.count(DTMF_INCLUDE) == 1 and text.count(DTMF_MENU) == 1:
        return text.replace(DTMF_INCLUDE, "", 1).replace(DTMF_MENU, "", 1)
    return text


CW_MENU = '''        // DEC-0167: read-only Morse observer; no radio/speaker state changes.
        toolsMenu->addAction("CW / Morse Decoder...", this, [this] {
            auto* window = findChild<CwWindow*>("cwWindow");
            if (!window) window = new CwWindow([this] {
                std::shared_ptr<Receiver> receiver;
                { std::lock_guard lock(receiversMutex); if (!receivers.empty()) receiver = receivers.front(); }
                return cwReceiverSource(receiver);
            }, this);
            window->show(); window->raise(); window->activateWindow();
        });
'''


def cw_window_text_allowed(path: str, before: str, after: str) -> bool:
    include = '#include "SstvWindow.h"\n'
    menu = '        QMenu* toolsMenu = menuBar()->addMenu("&Tools");\n'
    return (path == "src/MainWindow.cpp" and before.count(include) == 1
            and before.count(menu) == 1 and CW_INCLUDES not in before and CW_MENU not in before
            and after == before.replace(include, include + CW_INCLUDES, 1).replace(menu, menu + CW_MENU, 1))


def workspace_ui_text_allowed(path: str, before: str, after: str) -> bool:
    anchor = "        workspaceLayout = new WorkspaceLayout(this);\n"
    return (path == "src/MainWindow.cpp" and before.count(anchor) == 1
            and WORKSPACE_UI_BLOCK not in before
            and after == before.replace(anchor, anchor + WORKSPACE_UI_BLOCK, 1))


def diagnostics_text_allowed(path: str, before: str, after: str) -> bool:
    after = without_dtmf_menu(after)
    # Historical exact patch remains testable after independently reviewed CW UI.
    if after.count(CW_INCLUDES) == 1 and after.count(CW_MENU) == 1:
        after = after.replace(CW_INCLUDES, "", 1).replace(CW_MENU, "", 1)
    # Preserve the historical exact-patch test across the independently checked UI move.
    anchor = "        workspaceLayout = new WorkspaceLayout(this);\n"
    if after.count(anchor + WORKSPACE_UI_BLOCK) == 1:
        after = after.replace(anchor + WORKSPACE_UI_BLOCK, anchor, 1)
    pair = DIAGNOSTICS_DIGESTS.get(path)
    return pair is not None and pair == (
        hashlib.sha256(before.encode("utf-8")).hexdigest(),
        hashlib.sha256(after.encode("utf-8")).hexdigest(),
    )


# DEC-0148: exact NFM first-stage anti-alias change; other demods unchanged.
NFM_INPUT_DIGESTS = {
    "src/Demod.cpp": (
        "0737fc0ab93edc0f93fb99f89069b3718efe009458e938c11bf4777ac15762ba",
        "5459b7b840d1cdca572438f58143d3d3dfc88b1d6e58a76770dab2e8473126b8"),
    "include/Demod.h": (
        "090773e1eaa3b410361a6c52207587118cecc2d4e705544f78e6b91004c6a766",
        "ed678b546ef8c06c092b7f2c3d9795c0d447e455d11c3863af84e4094f604eb3"),
}


def nfm_input_text_allowed(path: str, before: str, after: str) -> bool:
    pair = NFM_INPUT_DIGESTS.get(path)
    return pair is not None and pair == (
        hashlib.sha256(before.encode("utf-8")).hexdigest(),
        hashlib.sha256(after.encode("utf-8")).hexdigest(),
    )


# DEC-0147: WFM FIR computation only; same full-rate outputs and power semantics.
WFM_FIR_DIGESTS = {
    "src/Demod.cpp": (
        "7610e29cbe34ac0f4d03ff896622fbb24f957e89882a28c3b5e8ab9a2bd53f9a",
        "0737fc0ab93edc0f93fb99f89069b3718efe009458e938c11bf4777ac15762ba"),
    "include/Demod.h": (
        "1d110e0fa0ea2d682fa0c5c02ed5e9ffb8f58da39949c8984da8db01ec5937a9",
        "090773e1eaa3b410361a6c52207587118cecc2d4e705544f78e6b91004c6a766"),
}


def wfm_fir_text_allowed(path: str, before: str, after: str) -> bool:
    pair = WFM_FIR_DIGESTS.get(path)
    return pair is not None and pair == (
        hashlib.sha256(before.encode("utf-8")).hexdigest(),
        hashlib.sha256(after.encode("utf-8")).hexdigest(),
    )


# DEC-0145: exact WFM speech-only continuity repair; RDS/NFM/P25 unchanged.
WFM_PCM_DIGESTS = {
    "src/Demod.cpp": (
        "ccb8a8770eb591736bcb8b7601748c56f46b323fc777c77e46f296b002b13fd0",
        "7610e29cbe34ac0f4d03ff896622fbb24f957e89882a28c3b5e8ab9a2bd53f9a"),
    "include/Demod.h": (
        "d01abee5dbf7fe5b1bc628ce889a947af73144951ec5e3e66eea69dafff6d19c",
        "1d110e0fa0ea2d682fa0c5c02ed5e9ffb8f58da39949c8984da8db01ec5937a9"),
}


def wfm_pcm_text_allowed(path: str, before: str, after: str) -> bool:
    pair = WFM_PCM_DIGESTS.get(path)
    return pair is not None and pair == (
        hashlib.sha256(before.encode("utf-8")).hexdigest(),
        hashlib.sha256(after.encode("utf-8")).hexdigest(),
    )


# DEC-0144: exact monitor UI/policy patch; no P25 setup or DSP changes.
AUTO_BW_DIGESTS = {
    "src/MainWindow.cpp": (
        "3ed596dedafc1052675755a944015e7cf3d9a01581fffe9a793b884ea6f026d1",
        "7e309ca8c17c89c5dd181a7f40b4a594f4e952ea963a69d0dbea8a97ea640c91"),
    "include/MainWindow.h": (
        "4cc2ca3b7ef25a89a90adb12c662f63257c1343d2210c26c8d090bba9439d8fb",
        "5d8a753ed52b64ec793a51e0eca949158f3ce029a24fb14b6f1788b4d1c7439b"),
}


def auto_bw_text_allowed(path: str, before: str, after: str) -> bool:
    pair = AUTO_BW_DIGESTS.get(path)
    return pair is not None and pair == (
        hashlib.sha256(before.encode("utf-8")).hexdigest(),
        hashlib.sha256(after.encode("utf-8")).hexdigest(),
    )


# DEC-0143: exact NFM PCM clock and sample-wise startup fade repair.
NFM_PCM_DIGESTS = {
    "src/Demod.cpp": (
        "34eadd76f7639c1ef49ad46c2f2b73bb8223b16bcac2aec47836a0bde7b9e134",
        "ccb8a8770eb591736bcb8b7601748c56f46b323fc777c77e46f296b002b13fd0"),
    "include/Demod.h": (
        "f2dd4a3665f8f5a2d290cb54ab296a50434a5eb00004055dd02554b2428c1379",
        "d01abee5dbf7fe5b1bc628ce889a947af73144951ec5e3e66eea69dafff6d19c"),
}


def nfm_pcm_text_allowed(path: str, before: str, after: str) -> bool:
    pair = NFM_PCM_DIGESTS.get(path)
    return pair is not None and pair == (
        hashlib.sha256(before.encode("utf-8")).hexdigest(),
        hashlib.sha256(after.encode("utf-8")).hexdigest(),
    )


# DEC-0142: exact reviewed NFM-only FIR/decimator repair and read-only FM counters.
# No path-wide exemption. Any subsequent shared-pipeline byte needs fresh review.
NFM_CONTINUITY_DIGESTS = {
    "src/Demod.cpp": (
        "28d8e1b490c6a48d7ac111beb59b369e969fe5c818fbf2b113fe83b738ab86a8",
        "34eadd76f7639c1ef49ad46c2f2b73bb8223b16bcac2aec47836a0bde7b9e134"),
    "include/Demod.h": (
        "c8f7a2fa628e9ecbac2951ce5e0ca719c6beef2ab8aabd0798edcf3e7a9608f5",
        "f2dd4a3665f8f5a2d290cb54ab296a50434a5eb00004055dd02554b2428c1379"),
}


def nfm_continuity_text_allowed(path: str, before: str, after: str) -> bool:
    pair = NFM_CONTINUITY_DIGESTS.get(path)
    return pair is not None and pair == (
        hashlib.sha256(before.encode("utf-8")).hexdigest(),
        hashlib.sha256(after.encode("utf-8")).hexdigest(),
    )


SATCOM_MARKER_BEGIN = "// SATCOM_HOST_INTEGRATION_BEGIN"
SATCOM_MARKER_END = "// SATCOM_HOST_INTEGRATION_END"
SATCOM_HOST_INCLUDE = '#include "SatcomHostServices.h"'

# DEC-0122: allow exactly the reviewed SDRplay loader-only patch, not a
# DeviceManager/function allowlist. Any extra RF/audio/tune edit changes the
# whole-file digest and fails. Text uses Git's LF-normalized UTF-8 content.
SDRPLAY_DEVICE_PATH = "src/DeviceManager.cpp"
SDRPLAY_DEVICE_BEFORE = "4e2ed89819473036ee1111825090fe41a013d7d20f97875396daabdeb2aca47a"
SDRPLAY_DEVICE_AFTER = "7aeee235a8b28a4ce57976129caa658233c4a007b8d251160030af199b535da7"

# DEC-0128: exact reviewed control-only changes from ccaf6ae. This is not a
# path/function exemption; changing one further byte requires fresh review.
SDRPLAY_CONTROL_DIGESTS = {
    "src/DeviceManager.cpp": (
        "7aeee235a8b28a4ce57976129caa658233c4a007b8d251160030af199b535da7",
        "505d49ca040385d0037ef66db76b529b3e45d0fe72e5dc720c2ac0d9b2c46b32"),
    "include/DeviceManager.h": (
        "ba90de292fe999ade828b6cabb19d714f6342e3bd6f716afa24e45eafe8aa0d3",
        "4384d7f156fbdc99442289befa2d40be9cd24e630dd68b625ae79027b17f324d"),
    "src/MainWindow.cpp": (
        "9b82e8f8c5740bc5abd2877eba3764419f6c5e8cd2c15b990c25c8c27294e510",
        "2c7d04447656899ecb907b0205a64d1e4723437e2a4ce3fd575df49af3992e12"),
}


# DEC-0129: reviewed RTL-only bias-T probe/control/open/cleanup and GUI wiring.
# Includes RX fault cleanup only, not the RX sample/tune loop or P25 algorithms.
RTL_BIAS_DIGESTS = {
    "src/DeviceManager.cpp": (
        "505d49ca040385d0037ef66db76b529b3e45d0fe72e5dc720c2ac0d9b2c46b32",
        "568056eeb297ff32786255c78c51723c553ba11d1dd4940104d05a4bd3a77fd3"),
    "include/DeviceManager.h": (
        "4384d7f156fbdc99442289befa2d40be9cd24e630dd68b625ae79027b17f324d",
        "e581e7d2f1103c5fc456576fc73d8df5d306889295013abecf8430f03c64e302"),
    "src/MainWindow.cpp": (
        "2c7d04447656899ecb907b0205a64d1e4723437e2a4ce3fd575df49af3992e12",
        "3ed596dedafc1052675755a944015e7cf3d9a01581fffe9a793b884ea6f026d1"),
}


def rtl_bias_text_allowed(path: str, before: str, after: str) -> bool:
    pair = RTL_BIAS_DIGESTS.get(path)
    return pair is not None and pair == (
        hashlib.sha256(before.encode("utf-8")).hexdigest(),
        hashlib.sha256(after.encode("utf-8")).hexdigest(),
    )


def sdrplay_control_text_allowed(path: str, before: str, after: str) -> bool:
    pair = SDRPLAY_CONTROL_DIGESTS.get(path)
    return pair is not None and pair == (
        hashlib.sha256(before.encode("utf-8")).hexdigest(),
        hashlib.sha256(after.encode("utf-8")).hexdigest(),
    )


def device_manager_sdrplay_text_allowed(before: str, after: str) -> bool:
    return (
        hashlib.sha256(before.encode("utf-8")).hexdigest() == SDRPLAY_DEVICE_BEFORE
        and hashlib.sha256(after.encode("utf-8")).hexdigest() == SDRPLAY_DEVICE_AFTER
    )


HF_DEMOD_PATH = "src/Demod.cpp"
HF_INCLUDE = '#include "HfDemod.h"\n'
HF_OLD_DESTRUCTOR = "Demodulator::~Demodulator() = default;\n"
HF_LIFECYCLE_BLOCK = '// HF_RECEIVE_LIFECYCLE_BEGIN\nDemodulator::~Demodulator() {\n    HfDemod::release(this);\n}\n// HF_RECEIVE_LIFECYCLE_END\n'
HF_RESET_BLOCK = '    // HF_RECEIVE_RESET_BEGIN\n    // reset() is a no-op when this Demodulator has never entered an HF mode.\n    // Do not rely on the legacy lastResetMode field: the isolated HF delegate\n    // returns before the legacy narrowband state machine updates that field.\n    HfDemod::reset(this);\n    // HF_RECEIVE_RESET_END\n'
HF_DELEGATE_BLOCK = '    // HF_RECEIVE_DELEGATE_BEGIN\n    if (HfDemod::supports(mode)) {\n        mpxContinuous = false;\n        return HfDemod::demodulate(\n            this, iq, sr, cf, target, mode, rmsOut, lpfHz, squelchDb,\n            gain, channelBwHz, target_audio_samples, outputRate,\n            externalSquelchLevelDb, audioLpfEnabled, multiplex,\n            dataIdentityHz);\n    }\n    // HF_RECEIVE_DELEGATE_END\n'


def normalize_path(path: str) -> str:
    return path.strip().replace("\\", "/").lstrip("./")


def protected_reason(path: str) -> str | None:
    normalized = normalize_path(path)
    for pattern in PROTECTED_PATTERNS:
        if fnmatch.fnmatchcase(normalized, pattern):
            return pattern
    return None


def protected_paths(paths: Iterable[str]) -> list[tuple[str, str]]:
    blocked: list[tuple[str, str]] = []
    for raw in paths:
        path = normalize_path(raw)
        if not path:
            continue
        reason = protected_reason(path)
        if reason is not None:
            blocked.append((path, reason))
    return blocked


def mainwindow_satcom_diff_allowed(diff_text: str) -> tuple[bool, str]:
    # Allow only additive, explicitly marked Satcom host wiring in MainWindow.
    marker_depth = 0
    saw_marker = False
    for line in diff_text.splitlines():
        if (line.startswith("diff --git ") or line.startswith("index ") or
                line.startswith("--- ") or line.startswith("+++ ") or
                line.startswith("@@")):
            continue
        if line.startswith("-"):
            return False, "MainWindow Satcom integration may not remove or replace existing lines"
        if not line.startswith("+"):
            continue

        content = line[1:]
        stripped = content.strip()
        if content == SATCOM_HOST_INCLUDE:
            continue
        if stripped == SATCOM_MARKER_BEGIN:
            marker_depth += 1
            saw_marker = True
            continue
        if stripped == SATCOM_MARKER_END:
            marker_depth -= 1
            if marker_depth < 0:
                return False, "Satcom integration marker order is invalid"
            continue
        if marker_depth > 0:
            continue
        return False, f"unmarked MainWindow addition: {content[:100]!r}"

    if marker_depth != 0:
        return False, "Satcom integration markers are unbalanced"
    if not saw_marker:
        return False, "no Satcom integration marker block was found"
    return True, ""


def git_file_text(ref: str, path: str) -> str:
    command = ["git", "show", f"{ref}:{path}"]
    try:
        result = subprocess.run(
            command,
            check=True,
            text=True,
            encoding="utf-8",
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
    except subprocess.CalledProcessError as exc:
        detail = exc.stderr.strip() or exc.stdout.strip() or str(exc)
        raise RuntimeError(
            f"could not read {path!r} from {ref!r}: {detail}"
        ) from exc
    return result.stdout


def demod_hf_change_allowed(base: str, head: str) -> tuple[bool, str]:
    base_text = git_file_text(base, HF_DEMOD_PATH)
    head_text = git_file_text(head, HF_DEMOD_PATH)
    transformed = head_text

    exact_blocks = (
        (HF_INCLUDE, ""),
        (HF_LIFECYCLE_BLOCK, HF_OLD_DESTRUCTOR),
        (HF_RESET_BLOCK, ""),
        (HF_DELEGATE_BLOCK, ""),
    )
    for block, replacement in exact_blocks:
        count = transformed.count(block)
        if count != 1:
            return False, (
                "isolated HF integration block is missing or duplicated: "
                f"{block.splitlines()[0]!r} count={count}"
            )
        transformed = transformed.replace(block, replacement, 1)

    if transformed != base_text:
        return False, (
            "Demod.cpp changed outside the exact AM/USB/LSB/CW "
            "integration blocks"
        )
    return True, ""


def git_path_diff(base: str, head: str, path: str) -> str:
    command = ["git", "diff", "--unified=0", f"{base}...{head}", "--", path]
    try:
        result = subprocess.run(
            command,
            check=True,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
    except subprocess.CalledProcessError as exc:
        detail = exc.stderr.strip() or exc.stdout.strip() or str(exc)
        raise RuntimeError(f"could not inspect {path!r}: {detail}") from exc
    return result.stdout


def git_changed_paths(base: str, head: str) -> list[str]:
    command = [
        "git",
        "diff",
        "--name-only",
        "--diff-filter=ACMRTUXB",
        f"{base}...{head}",
    ]
    try:
        result = subprocess.run(
            command,
            check=True,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
    except subprocess.CalledProcessError as exc:
        detail = exc.stderr.strip() or exc.stdout.strip() or str(exc)
        raise RuntimeError(
            f"could not inspect changes between {base!r} and {head!r}: {detail}"
        ) from exc
    return [line for line in result.stdout.splitlines() if line.strip()]


def default_base() -> str:
    explicit = os.environ.get("P25_GUARD_BASE", "").strip()
    if explicit:
        return explicit
    base_ref = os.environ.get("GITHUB_BASE_REF", "").strip()
    if base_ref:
        return f"origin/{base_ref}"
    before = os.environ.get("GITHUB_EVENT_BEFORE", "").strip()
    if before and set(before) != {"0"}:
        return before
    return "origin/master"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Reject modifications to the frozen P25 and shared RF/audio pipeline."
    )
    parser.add_argument("--base", default=default_base())
    parser.add_argument("--head", default="HEAD")
    parser.add_argument(
        "--paths",
        nargs="*",
        help="Check these paths directly instead of invoking git diff.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        changed = args.paths if args.paths is not None else git_changed_paths(args.base, args.head)
    except RuntimeError as exc:
        print(f"P25 guard error: {exc}", file=sys.stderr)
        return 2

    blocked = []
    for path, pattern in protected_paths(changed):
        if args.paths is None and path in (INFRASTRUCTURE_DIGESTS.keys() | WORKFLOW_DIGESTS.keys() | WORKFLOW_ROUTING_DIGESTS.keys() | REPEATER_ROUTING_DIGESTS.keys() | CONTROL_OWNERSHIP_DIGESTS.keys() | WORKFLOW_WINDOW_DIGESTS.keys() | WORKFLOW_SHUTDOWN_DIGESTS.keys()):
            if infrastructure_text_allowed(path, git_file_text(args.base, path), git_file_text(args.head, path)):
                print(f"P25 guard: accepted exact reviewed infrastructure/ownership patch: {path}")
                continue
        if args.paths is None and path in ("src/MainWindow.cpp", "src/MainWindowP25Orchestration.cpp"):
            if dtmf_text_allowed(path, git_file_text(args.base, path), git_file_text(args.head, path)):
                print(f"P25 guard: accepted exact DEC-0168 DTMF observer-only edit: {path}")
                continue
        if path == "src/MainWindow.cpp" and args.paths is None:
            if cw_window_text_allowed(path, git_file_text(args.base, path), git_file_text(args.head, path)):
                print(f"P25 guard: accepted exact DEC-0167 read-only Morse window hook: {path}")
                continue
            if workspace_ui_text_allowed(path, git_file_text(args.base, path), git_file_text(args.head, path)):
                print(f"P25 guard: accepted exact DEC-0166 widget reparenting: {path}")
                continue
        if path in DIAGNOSTICS_DIGESTS and args.paths is None:
            try:
                allowed = diagnostics_text_allowed(path, git_file_text(args.base,path), git_file_text(args.head,path))
            except RuntimeError as exc:
                print(f"P25 guard error: {exc}",file=sys.stderr)
                return 2
            if allowed:
                print(f"P25 guard: accepted exact DEC-0160 diagnostics-only change: {path}")
                continue
        if path in NFM_INPUT_DIGESTS and args.paths is None:
            try:
                allowed = nfm_input_text_allowed(
                    path, git_file_text(args.base, path), git_file_text(args.head, path)
                )
            except RuntimeError as exc:
                print(f"P25 guard error: {exc}", file=sys.stderr)
                return 2
            if allowed:
                print(f"P25 guard: accepted exact DEC-0148 NFM input filter: {path}")
                continue
        if path in WFM_FIR_DIGESTS and args.paths is None:
            try:
                allowed = wfm_fir_text_allowed(
                    path, git_file_text(args.base, path), git_file_text(args.head, path)
                )
            except RuntimeError as exc:
                print(f"P25 guard error: {exc}", file=sys.stderr)
                return 2
            if allowed:
                print(f"P25 guard: accepted exact DEC-0147 WFM FIR optimization: {path}")
                continue
        if path in WFM_PCM_DIGESTS and args.paths is None:
            try:
                allowed = wfm_pcm_text_allowed(
                    path, git_file_text(args.base, path), git_file_text(args.head, path)
                )
            except RuntimeError as exc:
                print(f"P25 guard error: {exc}", file=sys.stderr)
                return 2
            if allowed:
                print(f"P25 guard: accepted exact DEC-0145 WFM speech patch: {path}")
                continue
        if path in AUTO_BW_DIGESTS and args.paths is None:
            try:
                allowed = auto_bw_text_allowed(
                    path, git_file_text(args.base, path), git_file_text(args.head, path)
                )
            except RuntimeError as exc:
                print(f"P25 guard error: {exc}", file=sys.stderr)
                return 2
            if allowed:
                print(f"P25 guard: accepted exact DEC-0144 Auto BW policy patch: {path}")
                continue
        if path in NFM_PCM_DIGESTS and args.paths is None:
            try:
                allowed = nfm_pcm_text_allowed(
                    path, git_file_text(args.base, path), git_file_text(args.head, path)
                )
            except RuntimeError as exc:
                print(f"P25 guard error: {exc}", file=sys.stderr)
                return 2
            if allowed:
                print(f"P25 guard: accepted exact DEC-0143 NFM PCM patch: {path}")
                continue
        if path in NFM_CONTINUITY_DIGESTS and args.paths is None:
            try:
                allowed = nfm_continuity_text_allowed(
                    path, git_file_text(args.base, path), git_file_text(args.head, path)
                )
            except RuntimeError as exc:
                print(f"P25 guard error: {exc}", file=sys.stderr)
                return 2
            if allowed:
                print(f"P25 guard: accepted exact DEC-0142 NFM continuity/telemetry patch: {path}")
                continue
        if path in RTL_BIAS_DIGESTS and args.paths is None:
            try:
                allowed = rtl_bias_text_allowed(
                    path, git_file_text(args.base, path), git_file_text(args.head, path)
                )
            except RuntimeError as exc:
                print(f"P25 guard error: {exc}", file=sys.stderr)
                return 2
            if allowed:
                print(f"P25 guard: accepted exact DEC-0129 RTL bias-T patch: {path}")
                continue
        if path in SDRPLAY_CONTROL_DIGESTS and args.paths is None:
            try:
                allowed = sdrplay_control_text_allowed(
                    path, git_file_text(args.base, path), git_file_text(args.head, path)
                )
            except RuntimeError as exc:
                print(f"P25 guard error: {exc}", file=sys.stderr)
                return 2
            if allowed:
                print(f"P25 guard: accepted exact DEC-0128 SDRplay controls patch: {path}")
                continue
        if path == SDRPLAY_DEVICE_PATH and args.paths is None:
            try:
                allowed = device_manager_sdrplay_text_allowed(
                    git_file_text(args.base, path), git_file_text(args.head, path)
                )
            except RuntimeError as exc:
                print(f"P25 guard error: {exc}", file=sys.stderr)
                return 2
            if allowed:
                print("P25 guard: accepted exact DEC-0122 SDRplay loader-only patch; no other DeviceManager changes.")
                continue
        if path == HF_DEMOD_PATH and args.paths is None:
            try:
                allowed, detail = demod_hf_change_allowed(
                    args.base, args.head
                )
            except RuntimeError as exc:
                print(f"P25 guard error: {exc}", file=sys.stderr)
                return 2
            if allowed:
                print(
                    "P25 guard: accepted exact isolated AM/USB/LSB/CW "
                    "delegation; legacy Demod.cpp is otherwise byte-identical."
                )
                continue
            pattern = f"{pattern}; {detail}"
        if path == SATCOM_MAINWINDOW_PATH and args.paths is None:
            try:
                allowed, detail = mainwindow_satcom_diff_allowed(
                    git_path_diff(args.base, args.head, path)
                )
            except RuntimeError as exc:
                print(f"P25 guard error: {exc}", file=sys.stderr)
                return 2
            if allowed:
                print("P25 guard: accepted additive marked Satcom host wiring in MainWindow.")
                continue
            pattern = f"{pattern}; {detail}"
        blocked.append((path, pattern))

    if blocked:
        print("ERROR: this branch is not allowed to modify the frozen P25 pipeline.", file=sys.stderr)
        print("Protected changes detected:", file=sys.stderr)
        for path, pattern in blocked:
            print(f"  - {path}  (matched {pattern})", file=sys.stderr)
        print(
            "Move the change out of the protected path or use a separately reviewed P25 change process.",
            file=sys.stderr,
        )
        return 1

    print(f"P25 guard passed: {len(list(changed))} changed path(s), 0 protected.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
