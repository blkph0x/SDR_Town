#!/usr/bin/env python3
"""Regression guard for v0.2.44 streaming DSP pipeline (OP25/SDRTrunk-aligned)."""

from pathlib import Path

root = Path(__file__).resolve().parents[2]
cmake = (root / "CMakeLists.txt").read_text(encoding="utf-8", errors="ignore")
decoder_h = (root / "include" / "P25LiveDecoder.h").read_text(encoding="utf-8", errors="ignore")
decoder_cpp = (root / "src" / "P25LiveDecoder.cpp").read_text(encoding="utf-8", errors="ignore")

required = {
    "sdr_town_dsp static library": "add_library(sdr_town_dsp STATIC" in cmake,
    "dsp linked to app": "sdr_town_dsp" in cmake.split("target_link_libraries(${PROJECT_NAME}", 1)[1][:400],
    "dsp uses EHsc not EHa": "/EHsc" in cmake.split("sdr_town_dsp", 1)[1][:400],
    "streaming ddc header": (root / "include" / "dsp" / "P25StreamingChannelDdc.h").is_file(),
    "persistent framer header": (root / "include" / "dsp" / "P25Phase2Framer.h").is_file(),
    "demod state machine": (root / "include" / "dsp" / "P25DemodStateMachine.h").is_file(),
    "staged cqpsk scorer": (root / "include" / "dsp" / "P25CqpskStagedScorer.h").is_file(),
    "180 dibit burst constant": "kPhase2BurstDibits = 180" in (root / "include" / "dsp" / "P25DspTypes.h").read_text(encoding="utf-8"),
    "720 dibit superframe constant": "kPhase2SuperframeDibits = 720" in (root / "include" / "dsp" / "P25DspTypes.h").read_text(encoding="utf-8"),
    "sdrtrunk sync threshold 7": "kSyncThresholdSynchronized = 7" in (root / "include" / "dsp" / "P25DspTypes.h").read_text(encoding="utf-8"),
    "config enables streaming ddc": "enableStreamingChannelDdc = true" in decoder_h,
    "processIq uses streaming ddc": "m_streamingDdc.process" in decoder_cpp,
    "staged sync gate before full decode": "passesStagedCqpskGate" in decoder_cpp,
    "quadrant lut mapping path": "mapQuadrantsToDibits" in decoder_cpp,
    "persistent framer feed": "m_phase2Framer.consumeDibits" in decoder_cpp,
    "extended decode profile": "stagedReject=" in decoder_cpp and "demodState=" in decoder_cpp,
    "dsp test file": (root / "tests" / "test_p25dsp.cpp").is_file(),
}

missing = [name for name, ok in required.items() if not ok]
if missing:
    raise SystemExit("P25 streaming DSP regression FAILED: " + ", ".join(missing))

print("P25 streaming DSP regression: PASS")
