#!/usr/bin/env python3
"""DEC-0033: streaming HDQPSK must commit persistent framer bursts (not Cold-gated)."""
from __future__ import annotations

from pathlib import Path
from p25_orchestration_sources import orchestration_source_text

ROOT = Path(__file__).resolve().parents[2]
DECODER = (ROOT / "src" / "P25LiveDecoder.cpp").read_text(encoding="utf-8", errors="replace")
MAIN = orchestration_source_text()

checks = {
    "streaming queues framer bursts": "enableStreamingChannelDdc ||"
    in DECODER
    and "m_pendingFramerBursts.insert" in DECODER,
    "framer commit needs superframe anchor": "m_phase2SuperframeAnchorKnown)" in DECODER
    and "enableStreamingChannelDdc ||" in DECODER,
    "framer annotate gets source dibits": "annotatePhase2SessionCodewords(out, sourceDibits" in DECODER,
    "framer misalign does not clear anchor": "one misaligned framer burst must not wipe" in DECODER,
    "companion-only sticky fallthrough": "companionOnlySticky" in DECODER,
    "GUI replay zero streaming context": "plannedContextMs = streamingDdc" in MAIN,
    "voicetest streaming diag fields": "framerBurst=" in MAIN and "demodState=" in MAIN,
    # DEC-0038: sticky Gardner under streaming (no weak discrete lock freeze).
    "streaming preserves Gardner during search": (
        "!m_cqpskLock.valid && !m_config.enableStreamingChannelDdc" in DECODER
    ),
    "no streaming lock freeze": "sticky timing alone is the streaming continuity fix" in DECODER,
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit("FAIL: " + ", ".join(failed))
print("PASS: DEC-0033 streaming framer commit + sticky companion fallthrough")
