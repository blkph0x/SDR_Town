#!/usr/bin/env python3
"""Guard: block-channelize CQPSK hint that framed Phase 2 stops the grid (DEC-0019)."""
from pathlib import Path

root = Path(__file__).resolve().parents[2]
src = (root / "src" / "P25LiveDecoder.cpp").read_text(encoding="utf-8", errors="replace")

marker = "Block-channelize clears Costas/Gardner every hop"
if marker not in src:
    raise SystemExit(
        "P25 Phase 2 block CQPSK hint early-stop regression failed: missing DEC-0019 comment"
    )
region = src.split(marker, 1)[1][:1200]
checks = {
    "requires block hint": "m_blockCqpskHint.valid" in region,
    "block path only": "!m_config.enableStreamingChannelDdc" in region,
    "stops after framed evidence": "stopCqpskSearch = true" in region,
    "hard Phase 2 evidence": "hasCqpskHardLockEvidence(best)" in region,
    "soft evidence alone not enough": "hasPhase2SoftCqpskLockEvidence(best)" not in region,
    "cites 060221": "20260908_060221" in region,
}
failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit(
        "P25 Phase 2 block CQPSK hint early-stop regression failed: "
        + ", ".join(failed)
    )
print("P25 Phase 2 block CQPSK hint early-stop regression: PASS")
