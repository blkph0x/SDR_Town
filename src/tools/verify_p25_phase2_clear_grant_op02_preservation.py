#!/usr/bin/env python3
"""Verify OP=0x02 grant-update follow policy (retune, no sticky-clear speaker)."""

from __future__ import annotations

import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
MAIN = ROOT / "src" / "main.cpp"


def must_contain(text: str, needle: str, label: str) -> None:
    if needle not in text:
        raise SystemExit(f"FAIL: missing {label}: {needle!r}")


def must_not_contain(text: str, needle: str, label: str) -> None:
    if needle in text:
        raise SystemExit(f"FAIL: unexpected {label}: {needle!r}")


def main() -> int:
    text = MAIN.read_text(encoding="utf-8", errors="replace")
    must_contain(text, "p25PreserveTalkgroupEncryptionFromPrior", "encryption merge helper")
    must_contain(text, "p25PreserveTalkgroupEncryptionFromPrior(followTg, tg)", "auto-follow merge call")
    must_contain(
        text,
        "Service-option-less OP=0x02 updates must not erase sticky call security",
        "same-call clear preservation comment",
    )
    # New policy: OP=0x02 unknown may retune; do not hard-defer new follows forever.
    must_not_contain(
        text,
        "auto-defer-p2-op02-unknown",
        "old hard-defer key that blocked all OP=0x02 new follows",
    )
    must_contain(
        text,
        "auto-follow-p2-op02-unknown",
        "OP=0x02 unknown retune log key",
    )
    must_contain(
        text,
        "retuning, speaker waits for traffic-channel PTT/ESS",
        "OP=0x02 audio-gated follow message",
    )
    must_contain(
        text,
        "followTg.encryptionKnown = false",
        "strip sticky clear before OP=0x02 new RF arm",
    )
    must_contain(
        text,
        "!phase2Voice",
        "Phase 2 arm does not inherit sticky clear alone",
    )
    must_contain(
        text,
        "event.type == P25ControlEventType::GroupVoiceGrant",
        "explicit clear OP=0x00 path still present",
    )
    must_contain(text, "P25 CADENCE 1s:", "cadence rollup log line")
    print("verify_p25_phase2_clear_grant_op02_preservation: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
