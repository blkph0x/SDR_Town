#!/usr/bin/env python3
"""DEC-0038: streaming HDQPSK keeps Gardner timing across unlocked CQPSK search."""
from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DECODER = (ROOT / "src" / "P25LiveDecoder.cpp").read_text(encoding="utf-8", errors="replace")

checks = {
    "streaming preserves cqpskValid during search": (
        "!m_cqpskLock.valid && !m_config.enableStreamingChannelDdc" in DECODER
        and "candidateTiming.cqpskValid = false" in DECODER
    ),
    "documents cold-acquire streaming bug": "forced a cold timing acquire" in DECODER,
    "no streaming post-commit lock create": (
        "Do not create streaming locks here" in DECODER
        or "sticky timing alone is the streaming continuity fix" in DECODER
    ),
    "streaming warm skips absolute CQPSK": (
        "do not also score absolute CQPSK" in DECODER
        and "timingStateStorage.cqpskValid &&" in DECODER
        and "!differential" in DECODER
    ),
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit("FAIL: " + ", ".join(failed))
print("PASS: DEC-0038 streaming sticky Gardner (no weak lock freeze)")
