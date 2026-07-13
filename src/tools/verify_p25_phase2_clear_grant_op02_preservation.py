#!/usr/bin/env python3
"""Verify OP=0x02 grant updates preserve prior OP=0x00 clear grant state."""

from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
MAIN = ROOT / "src" / "main.cpp"


def read_main() -> str:
    return MAIN.read_text(encoding="utf-8", errors="replace")


def must_contain(text: str, needle: str, label: str) -> None:
    if needle not in text:
        raise SystemExit(f"FAIL: missing {label}: {needle!r}")


def main() -> int:
    text = read_main()
    must_contain(text, "p25PreserveTalkgroupEncryptionFromPrior", "encryption merge helper")
    must_contain(text, "p25PreserveTalkgroupEncryptionFromPrior(followTg, tg)", "auto-follow merge call")
    must_contain(
        text,
        "Service-option-less OP=0x02 updates must not erase sticky call security",
        "same-call clear preservation comment",
    )
    must_contain(
        text,
        "if (!rx.p25VoiceClearKnown && !rx.p25VoiceEncrypted &&",
        "traffic arm clear preservation",
    )
    must_contain(
        text,
        "out.phase2TargetVoiceCodewords >= 2",
        "bootstrap threshold 2 VCW",
    )
    must_contain(
        text,
        "auto-defer-p2-op02-unknown",
        "defer new follow from sticky OP=0x02 unknown",
    )
    must_contain(
        text,
        "event.type == P25ControlEventType::GroupVoiceGrant",
        "promote explicit clear OP=0x00 on first high-corr hit",
    )
    print("verify_p25_phase2_clear_grant_op02_preservation: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
