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

    blocked = protected_paths(changed)
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
