#!/usr/bin/env python3
"""DEC-0057: immutable grant — companion dwell must not brand wrong-TDMA status."""

from pathlib import Path

root = Path(__file__).resolve().parents[2]
from p25_orchestration_sources import orchestration_source_text

main = orchestration_source_text()
decoder = (root / "src" / "P25LiveDecoder.cpp").read_text(encoding="utf-8", errors="ignore")

feed = ""
if "if (effectiveBurstSlot != followedGrantSlot)" in main:
    feed = main.split("if (effectiveBurstSlot != followedGrantSlot)", 1)[1][:1600]

# Pending opposite-slot drain: multiline compare on pending.grantSlot.
pending = ""
marker = "static_cast<uint8_t>(pending.grantSlot & 0x01u) !="
if marker in main:
    pending = main.split(marker, 1)[1][:700]

required = {
    "feed wrong-slot respects grantedSlotImmutable": (
        "!rx.p25Phase2GrantedSlotImmutable" in feed
        and "out.phase2WrongSlot = true" in feed
        and "DEC-0057" in feed
    ),
    "pending wrong-slot respects grantedSlotImmutable": (
        "!rx.p25Phase2GrantedSlotImmutable" in pending
        and "out.phase2WrongSlot = true" in pending
    ),
    "I-ISCH absolute still drives grantSlot (DEC-0055.3)": (
        "phase2AbsoluteSuperframeBurstIndexFromIisch(dibits, pos)" in decoder
        and "phase2TrafficSlotFromSuperframeBurstIndex(trafficSuperframeBurstIndex)" in decoder
        and "phase2TrafficSlotFromSuperframeBurstIndex(lockRelativeBurstIndex)" not in decoder
    ),
}

failed = [name for name, ok in required.items() if not ok]
if failed:
    raise SystemExit(
        "P25 Phase 2 DEC-0057 wrong-slot / grantSlot regression failed: "
        + ", ".join(failed)
    )
print("P25 Phase 2 DEC-0057 wrong-slot / grantSlot regression: PASS")
