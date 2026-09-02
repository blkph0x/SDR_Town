#!/usr/bin/env python3
from pathlib import Path

main = (Path(__file__).resolve().parents[1] / "main.cpp").read_text(
    encoding="utf-8",
    errors="replace",
)

assert "post-acquire hop a cold reacquire" in main
assert "cliWideReacquireHoldWindows > 0" in main
assert "state->wideReacquireHoldWindows > 0" in main
assert "(voiceWindows == 0 || !cliHardTargetAcquire || blockChannelizeReacquire)" not in main
assert "(state->windows == 0 || !state->hardTargetAcquire || blockChannelizeReacquire)" not in main

print("P25 Phase 2 replay hot-window cadence regression: PASS")
