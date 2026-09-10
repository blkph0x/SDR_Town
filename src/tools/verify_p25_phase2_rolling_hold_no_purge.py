#!/usr/bin/env python3
"""DEC-0037: restore clear-eye hold; advance only waiting-clear; no purge on hold."""
from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
from p25_orchestration_sources import orchestration_source_text
# DEC-0040: search all orchestration TUs
MAIN_TEXT = orchestration_source_text()


def main() -> int:
    text = MAIN_TEXT
    m = re.search(
        r"bool p25Phase2RollingDecodeWindowConsumed\(.*?\n\}",
        text,
        re.S,
    )
    if not m:
        print("FAIL: p25Phase2RollingDecodeWindowConsumed not found")
        return 1
    body = m.group(0)
    if "DEC-0037" not in body:
        print("FAIL: DEC-0037 marker missing in Consumed()")
        return 1
    if "WaitingForClearGrant" not in body:
        print("FAIL: waiting-clear advance branch missing")
        return 1
    if not re.search(r"return false;\s*\}\s*$", body):
        print("FAIL: clear-eye hold (return false) not restored")
        return 1
    # Hold path must not purge newer work.
    hold = re.search(
        r"!consumedRollingWindow\) \{(.*?)(?:\} else if|\}\s*\}\s*\})",
        text,
        re.S,
    )
    if not hold:
        print("FAIL: hold publish branch not found")
        return 1
    hold_body = hold.group(1)
    if "DEC-0037" not in hold_body:
        print("FAIL: DEC-0037 marker missing in hold publish path")
        return 1
    if "purgeLocalPublishResultsForSession" in hold_body or "purgeP25VoiceDecodeWorkForSession" in hold_body:
        print("FAIL: hold path still purges newer jobs")
        return 1
    if "no purge" not in hold_body:
        print("FAIL: hold log should note no purge")
        return 1
    print("PASS: clear hold restored; waiting-clear advances; hold does not purge")
    return 0


if __name__ == "__main__":
    sys.exit(main())
