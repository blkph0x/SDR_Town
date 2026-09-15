#!/usr/bin/env python3
"""DEC-0055: dual-slot keep-selected PCM, tight epochTrusted, I-ISCH origin rebase."""

from pathlib import Path

root = Path(__file__).resolve().parents[2]
from p25_orchestration_sources import orchestration_source_text

main = orchestration_source_text()
decoder = (root / "src" / "P25LiveDecoder.cpp").read_text(encoding="utf-8", errors="ignore")

dual_gate = ""
if "if (dualSlotUntrustedGateEffective)" in main:
    dual_gate = main.split("if (dualSlotUntrustedGateEffective)", 1)[1][:2200]
elif "if (dualSlotUntrustedGate)" in main:
    dual_gate = main.split("if (dualSlotUntrustedGate)", 1)[1][:2200]

epoch = ""
if "const bool epochTrusted =" in main:
    parts = main.split("const bool epochTrusted =")
    epoch = parts[-1][:700].split(";", 1)[0] if len(parts) > 1 else ""

required = {
    "dual-slot keep path for Clear+selected": (
        "keepLabelledSelectedClearPcm" in dual_gate
        and "dual-slot-untrusted-keep-selected-pcm" in dual_gate
        and "dual-slot-untrusted-garble-drop" in dual_gate
        and "out.audio.clear()" in dual_gate
        and 'finishSecurityGate("dual-slot-untrusted-keep-selected-pcm")' in dual_gate
    ),
    "dual-slot keep requires latch Clear + target VCW + fed": (
        "latchClear" in dual_gate
        and "phase2TargetVoiceCodewords > 0" in dual_gate
        and "phase2FedToMbelib > 0" in dual_gate
        and "p25Phase2StrongSelectedSlotStructure(out)" in dual_gate
    ),
    "epochTrusted drops bare establishedClear+xor+grantSlot": (
        "establishedClearCall && burst.xorMaskApplied && burst.grantSlotKnown)" not in epoch.replace(
            "\n", " "
        )
        and "burst.superframeLock" in epoch
        and "burst.maskPhaseLock" in epoch
        and "forceEstablishedFeed" in main.split("const bool epochTrusted =", 1)[1][:900]
    ),
    "I-ISCH absolute origin helper present": (
        "phase2AbsoluteSuperframeBurstIndexFromIisch" in decoder
        and "location) * 4u + local" in decoder
        and "ischA.location != ischB.location" in decoder
    ),
    "decodePhase2BurstAt applies I-ISCH absolute to traffic index + grantSlot": (
        "phase2AbsoluteSuperframeBurstIndexFromIisch(dibits, pos)" in decoder
        and "trafficSuperframeBurstIndex = (*absoluteIndex) % 12u" in decoder
        and "phase2TrafficSlotFromSuperframeBurstIndex(trafficSuperframeBurstIndex)" in decoder
        and "do not flip an already-aligned grant slot from I-ISCH alone" in decoder
    ),
    "phase2TrafficSlotForSuperframeBurst still no dibit flip": (
        "(void)dibits;" in decoder.split(
            "uint8_t phase2TrafficSlotForSuperframeBurst", 1
        )[1][:900]
        and "(void)superframeOffset;" in decoder.split(
            "uint8_t phase2TrafficSlotForSuperframeBurst", 1
        )[1][:900]
    ),
}

failed = [name for name, ok in required.items() if not ok]
if failed:
    raise SystemExit(
        "P25 Phase 2 DEC-0055 epoch/dual-slot/origin regression failed: "
        + ", ".join(failed)
    )
print("P25 Phase 2 DEC-0055 epoch/dual-slot/origin regression: PASS")
