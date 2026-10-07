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


# DEC-0192 / explicit P25 diagnosis request: exact call-observer repair only.
# Decoder, vocoder, playout, and hold constants remain frozen.
P25_OBSERVER_DIGESTS = {
    "src/P25TrafficChannelProcessor.cpp": (
        "bbd474be7ca114346deb0fdd13ef4254d39a477a8f6fc99971ba941cf3727972",
        "b68f79ab11e5e8e77d1f7b4a6e8825396c404dde0192a3daea67782f51c379f7"),
    "include/P25TrafficChannelProcessor.h": (
        "5361d78ff933cf60a9aa8e69d4dc6f08e0aa393d2eea1db2973a70be27030f9a",
        "cabe3cdb1aa312c074d2d9615f7021e57be7d8fe8271512284fa7d22a48196d2"),
    "tests/test_p25traffic_processor.cpp": (
        "7807987b05ec0365533bc8cd3a66db6fd12161cb879c2469f9945a1d2a08bfad",
        "dfc08bb5fc0ab769238b3562fbd2c0214a77d2e9d8fe837970fbad10dfcf1249"),
}


# DEC-0193: exact confirmed-teardown/allocation repair. DSP/audio remain frozen.
P25_FOLLOW_LIFECYCLE_DIGESTS = {
    "include/P25FollowStateMachine.h": ("9341590f05858f945d8c0c9d1612b60f869493b69b126b5453fb5a91fd5fdad4", "f0ad755d0f36d99d342053c7cc3e1b54b2bb5ba0946e9c7e15e2a25fffc14eed"),
    "include/P25TrafficChannelProcessor.h": ("cabe3cdb1aa312c074d2d9615f7021e57be7d8fe8271512284fa7d22a48196d2", "392128cee3bf0c455eb2e3fefe1f6176281644f1b855fbca09d1968d30da9bec"),
    "src/P25FollowStateMachine.cpp": ("a79220c876a99f3597f4e468aa6f67eadd7b0f8900de275054417685d7eb6911", "fff39a851d0252a7c60fe06a99bcb6350d32fd0529d60e4858123ea2e2fb78a8"),
    "src/P25TrafficChannelProcessor.cpp": ("b68f79ab11e5e8e77d1f7b4a6e8825396c404dde0192a3daea67782f51c379f7", "54f6ce8b9edb68dd48f08b0f5da65dd77eeb7a5986e08e131e911e8c933d4961"),
    "src/P25VoiceDecode.cpp": ("0c30057dd2225e997bfbfbc45cc52c8d80dba380ac33ab33c3c84a059fca5c18", "a045731d2e554bb3c19512bdf8c1b439220ed195de66e79718549fccc7e4bfaf"),
    "src/MainWindow.cpp": ("b0181884f8d80d5c7370303bad751b5b812f553296d725326604881c263bc976", "0ef4e88814b6ecdc6ce951e2c12c4659ee0adc79fe80d7432e9b220da71685df"),
    "src/CliApp.cpp": ("4e1094a11b6bf05ea974a9b0a6f250efd8aae0c28fb5af6120e357f9ec600ffb", "e56e546ff3cea066c01ab55c8b57dc29039e8a9ed113efafa27d7268f3ad9ed8"),
    "tests/test_p25follow.cpp": ("fb7ac4c6ccda5f8cd144124c185cdd2a862693cd4091098b3992541893364d78", "0a5df977916c127610189b7a2f8f98165617973b1ec03ead8e7e687fe69a4757"),
    "tests/test_p25traffic_processor.cpp": ("dfc08bb5fc0ab769238b3562fbd2c0214a77d2e9d8fe837970fbad10dfcf1249", "683d2b12366446956c5f5c965c71af74a43765fac8d13103af3fad066f05af16"),
}


# DEC-0194: exact passive trace, current-tuning CC view and diagnostic synthesis
# repair. No receive budgets, slot/encryption policy or speaker timings change.
P25_TRACE_CONTEXT_DIGESTS = {
    "include/DeviceManager.h": ("09be7f885a929b124d36d824fbc7376d37e271871865c8500d3e442f7a92ccb1", "cc568d765e1fed0759151f91c9f410b30fc4276baea4ecb84e546756bf559e94"),
    "include/MainWindow.h": ("7190137f5ae45d3501288af8cfcc547a01186a6cab0f5d7996e1ab07f21347e8", "f213ced64f4952bfef7e5b300835b9eff06d892f986b9b7df6b2b8afa3046091"),
    "include/P25VoiceTest.h": ("abbc7d87727704f82ea7e1abaf87b971792ee7650eef73e636a93f727ffe00c9", "90185ae59e90afb997a040370ba36263fff202251b2ccaa17831f7fbb0b86c90"),
    "include/P25PipelineTrace.h": ("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "3eda8d57020b0f467174ddf1235638f998a4943163985d2f27e707b783c2fdcd"),
    "src/DeviceManager.cpp": ("82b7115943eb9b015962d1bd315c0133f4e1f6b06a87e41fe660d6e32add692d", "99ae844bd9211f0009a9f2e780a7ffdeed47213accaeb1b011df2221d3d71438"),
    "src/CliApp.cpp": ("e56e546ff3cea066c01ab55c8b57dc29039e8a9ed113efafa27d7268f3ad9ed8", "009bbffa8a513a64eb05eab33a56297de78ba85ad353b6e96313cd91eec061b1"),
    "src/MainWindow.cpp": ("0ef4e88814b6ecdc6ce951e2c12c4659ee0adc79fe80d7432e9b220da71685df", "0d1ab8f8c40321869c8d2ff2621fac732ddba797d131519e01b75fdfb47698e3"),
    "src/MainWindowP25Voice.cpp": ("24e26a4b70eab89417e17c9a83310c195d3de86f52471f7817c5c37ba86977f1", "8727230a46abcbed425d6cc646410f629b8b26a9450e8d05c6fb73555bca0a6b"),
    "src/MainWindowP25Orchestration.cpp": ("f0bf8a6b2e34f57b29cd4a6e502678fd93c2fea3c2d3151ee414de16ed57e50e", "e2266e4f44a134ee3999cb084da4b2d94f44dc779726f0fee51eaf365897de59"),
    "src/P25VoiceDecode.cpp": ("a045731d2e554bb3c19512bdf8c1b439220ed195de66e79718549fccc7e4bfaf", "e40e23bc224d419272c3542cfd7fe8147e99824c52332c6b898bbd5619781e98"),
    "tests/test_p25_pipeline_trace.cpp": ("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "681fc72b256687a746363b9b6cbd9e1cce600356ca561f84ab73d2715ae27fd8"),
}


# DEC-0201: capture 20261006_081738_771 restores audio after DEC-0200 but
# measures residual bucket-D ring starvation on dominant 160+280 active-clear
# windows. Exact digests are 0a3fe41 (0.2.127) -> active-only 200 ms minFresh;
# 240 ms max / 280 ms overlap and non-active 160 ms stay unchanged.
P25_EMIT_GAP_DIGESTS = {
    "include/P25VoiceTiming.h": (
        "1f5b679283fd3034ffdc963fb6ef3017f79caa67bd3079d6c7ca5fe57a55577f",
        "efbcf36274de9795a48d8beebaee5ea24e5c03a75293e76ae29cd4c3514dff21"),
    "src/P25VoiceTiming.cpp": (
        "2e5f558aec52fb9d3d34cf21c6087203b81b21ea7db2cca3af778d0ee9635d58",
        "94ddc4ea4051e50a431b869614645e4cb573c41ed8644fdda70416cd6fb0fbd6"),
    "src/tools/verify_p25_phase2_realtime_catchup_geometry.py": (
        "000d8108bdaeba4c7d03dc25c8280c97c6555a4ee5a3e230d0e4462f304f3356",
        "982f03a6553d04420acba02073d3c7a8ea17d4b9a1ed46eaa42ab61a0e14739d"),
    "tests/test_p25_voice_timing.cpp": (
        "d287d67322171b8b1ff96702863fc85abe9c707c02646045c504e368ca274d41",
        "3a3e110fad20af490daaee832c7c8fdef5a439e3728d5a77e9e4eca62591a681"),
}


# DEC-0204: IQ-only encrypted grant/follow capture. Exact digests are
# b8e803c (0.2.129) -> holdEncryptedForIqCapture + GUI button.
P25_ENCRYPTED_GRANT_IQ_DIGESTS = {
    "include/P25FollowStateMachine.h": (
        "f0ad755d0f36d99d342053c7cc3e1b54b2bb5ba0946e9c7e15e2a25fffc14eed",
        "f6a5ee8732f30370d1d62410000c5167a66ef983f8d923ce80c3efa57dfac7da"),
    "src/P25FollowStateMachine.cpp": (
        "fff39a851d0252a7c60fe06a99bcb6350d32fd0529d60e4858123ea2e2fb78a8",
        "40e1d18a77629a7521021afa3ab3d00258069d5d2aac29de4962f7d3f61d9fe3"),
    "tests/test_p25follow.cpp": (
        "0a5df977916c127610189b7a2f8f98165617973b1ec03ead8e7e687fe69a4757",
        "6c97ae8fd86d8e6b3b58c253709aa374be1644c929b3ce00b1b3653a9abc870a"),
    "include/MainWindow.h": (
        "f213ced64f4952bfef7e5b300835b9eff06d892f986b9b7df6b2b8afa3046091",
        "870f4a524fe1065e455cebc21bfdc5627df24dbbca8cbc8ddae00b3de6f1216e"),
    "src/MainWindow.cpp": (
        "0d1ab8f8c40321869c8d2ff2621fac732ddba797d131519e01b75fdfb47698e3",
        "3639563e9f6899e2ec65d5d661660be7c5cc2209bfdd13b1781e2fa0ffff3454"),
}


# DEC-0203 / capture 20261006_093930: first locked-lattice empty hop stays
# healthy 80/4. Exact digests are 14e1090 (0.2.128) -> planner + live call.
P25_LOCKED_LATTICE_EMPTY_DIGESTS = {
    "include/P25VoiceTiming.h": (
        "3c25716efce3b52e32287bb2f37b10e093eb7b96a52aa41595463695b71342a4",
        "efbcf36274de9795a48d8beebaee5ea24e5c03a75293e76ae29cd4c3514dff21"),
    "src/P25VoiceTiming.cpp": (
        "a31b89088a261a12880f32c5a2ee6e3c1aefa5aaef26db4be0536e0631273148",
        "94ddc4ea4051e50a431b869614645e4cb573c41ed8644fdda70416cd6fb0fbd6"),
    "src/MainWindowP25Voice.cpp": (
        "8727230a46abcbed425d6cc646410f629b8b26a9450e8d05c6fb73555bca0a6b",
        "54956d949f0cbbc362269373818985a1089775220c8bb9e7938f9579723796b3"),
    "tests/test_p25_voice_timing.cpp": (
        "6d7517506ff346c0e97ca04d46c623cfec7e5e799671c70481d9c1adf329a5fc",
        "3a3e110fad20af490daaee832c7c8fdef5a439e3728d5a77e9e4eca62591a681"),
    "src/tools/verify_p25_phase2_live_eyelost_replay_caps.py": (
        "ce34dc79f8c96a58bc2db1a13208b02d529740029ffd1c073f55bf41ea1f5a03",
        "41e6ecfbbbf562b99462d337bda12714cb87b576b2defce278f232a5c3ed2efa"),
    "src/tools/verify_p25_phase2_no_post_emit_cold_escalate.py": (
        "0b2682dd94e4de318e2f55e9557cb84a34e1a00976618469c6db378d77723a17",
        "e9196fdaf5fe4cf84fa47907e3b77881b2508cb3c8f8453da9d6609010aec30b"),
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
                      SATELLITE_SESSION_DIGESTS.get(path), DRIVER_IO_ADMISSION_DIGESTS.get(path),
                      P25_OBSERVER_DIGESTS.get(path), P25_FOLLOW_LIFECYCLE_DIGESTS.get(path),
                      P25_TRACE_CONTEXT_DIGESTS.get(path), P25_EMIT_GAP_DIGESTS.get(path),
                      P25_LOCKED_LATTICE_EMPTY_DIGESTS.get(path),
                      P25_ENCRYPTED_GRANT_IQ_DIGESTS.get(path))


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


def digest_before_text(ref: str, path: str, digests: dict[str, tuple[str, str]]) -> str:
    try:
        return git_file_text(ref, path)
    except RuntimeError:
        pair = digests.get(path)
        if not pair or pair[0] != hashlib.sha256(b"").hexdigest():
            raise
        result = subprocess.run(["git", "ls-tree", "--name-only", ref, "--", path],
                                check=True, capture_output=True, text=True, encoding="utf-8")
        if result.stdout.strip():
            raise
        return ""


def trace_before_text(ref: str, path: str) -> str:
    return digest_before_text(ref, path, P25_TRACE_CONTEXT_DIGESTS)


def emit_gap_before_text(ref: str, path: str) -> str:
    return digest_before_text(ref, path, P25_EMIT_GAP_DIGESTS)


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
        if args.paths is None and path in P25_TRACE_CONTEXT_DIGESTS:
            if infrastructure_text_allowed(path, trace_before_text(args.base, path), git_file_text(args.head, path)):
                print(f"P25 guard: accepted exact DEC-0194 trace/context repair: {path}")
                continue
        if args.paths is None and path in P25_EMIT_GAP_DIGESTS:
            if infrastructure_text_allowed(path, emit_gap_before_text(args.base, path), git_file_text(args.head, path)):
                print(f"P25 guard: accepted exact DEC-0201 emit-gap repair: {path}")
                continue
        if args.paths is None and path in P25_LOCKED_LATTICE_EMPTY_DIGESTS:
            if infrastructure_text_allowed(path, git_file_text(args.base, path), git_file_text(args.head, path)):
                print(f"P25 guard: accepted exact DEC-0203 locked-lattice empty-hop repair: {path}")
                continue
        if args.paths is None and path in P25_ENCRYPTED_GRANT_IQ_DIGESTS:
            if infrastructure_text_allowed(path, git_file_text(args.base, path), git_file_text(args.head, path)):
                print(f"P25 guard: accepted exact DEC-0204 encrypted-grant IQ capture: {path}")
                continue
        if args.paths is None and path in P25_FOLLOW_LIFECYCLE_DIGESTS:
            if infrastructure_text_allowed(path, git_file_text(args.base, path), git_file_text(args.head, path)):
                print(f"P25 guard: accepted exact DEC-0193 follow-lifecycle repair: {path}")
                continue
        if args.paths is None and path in P25_OBSERVER_DIGESTS:
            if infrastructure_text_allowed(path, git_file_text(args.base, path), git_file_text(args.head, path)):
                print(f"P25 guard: accepted exact DEC-0192 traffic-observer repair: {path}")
                continue
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
