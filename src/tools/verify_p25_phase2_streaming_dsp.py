#!/usr/bin/env python3
"""Regression guard for v0.2.44 streaming DSP pipeline (OP25/SDRTrunk-aligned)."""

from pathlib import Path

root = Path(__file__).resolve().parents[2]
cmake = (root / "CMakeLists.txt").read_text(encoding="utf-8", errors="ignore")
decoder_h = (root / "include" / "P25LiveDecoder.h").read_text(encoding="utf-8", errors="ignore")
decoder_cpp = (root / "src" / "P25LiveDecoder.cpp").read_text(encoding="utf-8", errors="ignore")
main_cpp = (root / "src" / "main.cpp").read_text(encoding="utf-8", errors="ignore")

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
    "streaming ddc remains off in decoder default / CC / forensic": "enableStreamingChannelDdc = false" in decoder_h
    and "cfg.enableStreamingChannelDdc = false;" in main_cpp,
    "runtime streaming ddc env switch": "SDR_TOWN_P25_STREAMING_DDC" in main_cpp
    and "cfg.enableStreamingChannelDdc = true;" in main_cpp,
    "gui live path does not auto-enable via stickyReady": "setEnableStreamingChannelDdc(stickyReady)" not in main_cpp,
    "independent traffic streaming ddc stays opt-in": "p25Phase2StreamingDdcExperimentEnabled" in main_cpp
    and "duty 0.685" in main_cpp,
    "locked streaming hops are 80 ms": "kP25Phase2StreamingLiveSliceSeconds = 0.080" in main_cpp
    and "kP25Phase2StreamingLiveMinFreshSeconds = 0.040" in main_cpp,
    "processIq uses streaming ddc": "m_streamingDdc.process" in decoder_cpp,
    "streaming ddc does not jump lattice on fir lag": (
        "DEC-0018" in decoder_cpp
        and "jumping the lattice" in decoder_cpp.lower()
        and "!rx.p25VoiceLiveDecoder.config().enableStreamingChannelDdc" in main_cpp
    ),
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
