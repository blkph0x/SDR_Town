#!/usr/bin/env python3
"""Static contract checks for the isolated HF receive path."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def text(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(f"HF integration check failed: {message}")


def main() -> int:
    header = text("include/HfDemod.h")
    source = text("src/HfDemod.cpp")
    demod = text("src/Demod.cpp")
    cmake = text("CMakeLists.txt")
    tests = text("tests/test_hf_demod.cpp")
    docs = text("docs/HF_RECEIVE.md")
    satcom = text("src/SatcomScannerEngine.cpp")

    require("bool supports(DemodMode mode) noexcept;" in header,
            "mode ownership declaration missing")
    require("mode == DemodMode::AM || mode == DemodMode::USB" in source,
            "HF mode ownership changed")
    require("mode == DemodMode::LSB || mode == DemodMode::CW" in source,
            "HF mode ownership is incomplete")
    require("DemodMode::NFM" not in source.split(
                "bool supports(DemodMode mode) noexcept {", 1)[1].split("}", 1)[0],
            "NFM was routed into the HF module")
    require("DemodMode::WFM" not in source.split(
                "bool supports(DemodMode mode) noexcept {", 1)[1].split("}", 1)[0],
            "WFM was routed into the HF module")

    require('#include "HfDemod.h"' in demod,
            "legacy Demodulator does not include the HF module")
    require(demod.count("// HF_RECEIVE_LIFECYCLE_BEGIN") == 1,
            "HF lifecycle marker missing or duplicated")
    require(demod.count("// HF_RECEIVE_RESET_BEGIN") == 1,
            "HF reset marker missing or duplicated")
    require(demod.count("// HF_RECEIVE_DELEGATE_BEGIN") == 1,
            "HF delegate marker missing or duplicated")
    require("if (HfDemod::supports(mode))" in demod,
            "mode-gated HF delegate missing")
    require("HfDemod::reset(this);" in demod and
            "HfDemod::supports(lastResetMode)" not in demod,
            "HF stream-epoch reset is conditional on stale legacy mode state")
    require("HfDemod::release(this);" in demod,
            "per-demodulator HF state is not released")

    require(cmake.count("src/HfDemod.cpp") == 2,
            "HF source must be linked into app and unit-test targets")
    require(cmake.count("tests/test_hf_demod.cpp") == 1,
            "HF unit-test source is not linked")
    require("opposite sideband" in tests,
            "opposite-sideband regression coverage missing")
    require("stale wideband settings" in tests,
            "stale WFM-to-HF profile regression coverage missing")
    require("decoder tap is continuous" in tests,
            "HF SSTV continuity coverage missing")
    require("decoder sink publishes clean live SSTV audio" in tests,
            "main-receiver SSB SSTV sink coverage missing")
    require("RTL direct-sampling rate stays intelligible" in tests,
            "Q-branch AM realtime regression coverage missing")
    require("coarseDecimate" in source and "designCoarseDecimatorTaps" in source,
            "bounded staged high-rate HF decimator missing")
    require("coarseTaps" in source and "coarseDelay" in source,
            "streaming HF FIR decimator state missing")
    require("cicIntegrator" not in source,
            "unbounded CIC integrator state must not return")
    require("mixerOscillator *= state->mixerStep" in source,
            "recursive HF NCO missing; per-sample trig regression possible")
    require("adaptiveResamplerHalf" in source and
            "kMaximumResamplerHalf = 1024" in source,
            "fractional HF resampler hardening missing")
    require("0.38 * std::min(1.0, outputRateHz / inputRateHz)" in source,
            "HF anti-alias transition band missing")
    require("startupMuteSamples" in source and
            "state.mode == DemodMode::AM" in source,
            "HF startup settling is not limited to AM")
    require("Prime the carrier estimator from settled samples" in source and
            "carrierSum" in source,
            "AM carrier acquisition can still normalize against FIR startup zeros")
    require("(nfmSstv || ssbSstv) ? &multiplex : nullptr" in satcom,
            "Satcom sideband SSTV does not request the clean HF decoder block")
    require("if ((nfmSstv || ssbSstv) && !multiplex.samples.empty())" in satcom,
            "Satcom does not publish USB/LSB decoder provenance")
    require("block.samples.assign(audio.begin()" not in satcom,
            "Satcom still reconstructs SSTV input from speaker audio")
    require("multi-megasample SDR streams" in tests,
            "multi-MS/s alias regression coverage missing")
    require("NFM, WFM and AUTO never enter the HF module" in docs,
            "P25/non-HF isolation is not documented")

    protected_tokens = (
        "P25Control", "P25LiveDecoder", "P25Follow", "P25Voice",
        "P25Traffic", "mbelib",
    )
    for token in protected_tokens:
        require(token not in source and token not in header,
                f"isolated HF module unexpectedly references {token}")

    print("HF receive integration contract passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
