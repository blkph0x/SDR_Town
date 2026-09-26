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
)

SATCOM_MAINWINDOW_PATH = "src/MainWindow.cpp"
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
