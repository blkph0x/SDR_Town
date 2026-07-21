#!/usr/bin/env python3
"""
Static/regression checks for the sdrtrunk-aligned P25 Phase 2 chain.
"""
from pathlib import Path
root = Path(__file__).resolve().parents[1]
p25 = (root / "P25LiveDecoder.cpp").read_text(errors='ignore')
main = (root / "main.cpp").read_text(errors='ignore')
recv = (root.parent / "include" / "Receiver.h").read_text(errors='ignore')
for idx in range(12):
    assert f"case {idx}:" in p25, f"missing slot case {idx}"
assert "case 10:" in p25 and "return 1; // sdrtrunk TIMESLOT_2" in p25
assert "case 11:" in p25 and "return 0; // sdrtrunk TIMESLOT_1" in p25
for needle in [
    "placeEssB(3, 28); // ESS-B4",
    "placeEssB(2, 32); // ESS-B3",
    "placeEssB(1, 36); // ESS-B2",
    "placeEssB(0, 40); // ESS-B1",
    "tentativeEssRepeats >= 2",
]:
    assert needle in p25, f"missing ESS alignment/gate marker: {needle}"
for needle in [
    "const size_t burstIndex = static_cast<size_t>(burst.superframeBurstIndex % 12u)",
    "expectedMaskIndex = static_cast<size_t>(burst.isch.location) * 4u + (burstIndex % 4u)",
    "actualMaskIndex = (burstIndex + static_cast<size_t>(window.phase)) % 12u",
    "b.isch.channel <= 1",
]:
    assert needle in p25, f"missing I-ISCH mask phase anchor: {needle}"
fn = main[main.index('static bool p25AmbeDecodeFrameLooksUsable'):main.index('static QString p25Phase2ValidationPath')]
assert "decoded.totalErrors > 3" in fn, "fresh AMBE speech must reject mbelib repeat/erasure-grade frames"
assert "decoded.message.find('R')" in fn and "decoded.message.find('E')" in fn, "fresh AMBE speech must reject mbelib repeat/erasure markers"
assert "rms < 1.0e-6" not in fn, "AMBE gate must preserve valid low-energy/silence frames for cadence"
assert "peak > kP25DecodedAudioSafeMaxPeak" in fn and "rms > kP25DecodedAudioSafeMaxRms" in fn
assert "p25Phase2PendingAudio" in recv and "applyP25Phase2SecurityAudioGate" in main
print("P25 Phase 2 sdrtrunk end-to-end alignment regression: PASS")
