#!/usr/bin/env python3
"""Guard SDRTrunk-class Phase 2 framer slip handling at commit time."""
from pathlib import Path

root = Path(__file__).resolve().parents[2]
decoder = (root / "src" / "P25LiveDecoder.cpp").read_text(
    encoding="utf-8", errors="ignore"
).replace("\r\n", "\n")

marker = "P25Phase2DecodeResult P25LiveDecoder::processPhase2FromFramerBurstsInternal"
if marker not in decoder:
    raise SystemExit("framer commit slip guard failed: missing framer commit function")

body = decoder.split(marker, 1)[1].split(
    "P25Phase2DecodeResult P25LiveDecoder::processPhase2HardDibitsDetailedInternal", 1
)[0]

checks = {
    "uses SDRTrunk sync offset bound": (
        "kMaxFramerCommitSlipDibits = p25dsp::kSyncOffsetMax" in body
    ),
    "allows small negative stale slip only": (
        "delta < -kMaxFramerCommitSlipDibits" in body
    ),
    "nearest 180-dibit lattice math": (
        "const int64_t burstNum = (delta + (kBurstDibits / 2)) / kBurstDibits" in body
        and "const int64_t expectedDelta = burstNum * kBurstDibits" in body
    ),
    "rejects larger slot-mixing slip": (
        "std::llabs(slipDibits) > kMaxFramerCommitSlipDibits" in body
    ),
    "superframe index comes from nearest lattice": (
        "static_cast<uint64_t>(burstNum) % 12ull" in body
    ),
    "diagnostics stamp corrected slip": (
        "fb.dibitOffsetCorrection != 0 || slipDibits != 0" in body
        and "burst.syncOffsetDibits = slipDibits != 0" in body
    ),
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit(
        "framer commit slip guard failed: " + ", ".join(failed)
    )

print("verify_p25_phase2_framer_commit_slip: PASS")
