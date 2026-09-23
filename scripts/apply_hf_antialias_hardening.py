#!/usr/bin/env python3
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one match, found {count}")
    return text.replace(old, new, 1)


cpp_path = Path("src/HfDemod.cpp")
cpp = cpp_path.read_text(encoding="utf-8")
cpp = replace_once(
    cpp,
    "constexpr int kResamplerHalf = 24;",
    "constexpr int kMinimumResamplerHalf = 24;\n"
    "constexpr int kMaximumResamplerHalf = 1024;",
    "resampler constants",
)
cpp = replace_once(
    cpp,
    "    std::vector<std::complex<float>> resampleBuffer;\n"
    "    double resamplePosition = static_cast<double>(kResamplerHalf);",
    "    std::vector<std::complex<float>> resampleBuffer;\n"
    "    int resamplerHalf = kMinimumResamplerHalf;\n"
    "    double resamplePosition = static_cast<double>(kMinimumResamplerHalf);",
    "resampler state",
)
cpp = replace_once(
    cpp,
    "    state.resampleBuffer.assign(static_cast<size_t>(kResamplerHalf), {});\n"
    "    state.resamplePosition = static_cast<double>(kResamplerHalf);",
    "    const int half = std::clamp(\n"
    "        state.resamplerHalf, kMinimumResamplerHalf, kMaximumResamplerHalf);\n"
    "    state.resampleBuffer.assign(static_cast<size_t>(half), {});\n"
    "    state.resamplePosition = static_cast<double>(half);",
    "resampler reset",
)
cpp = replace_once(
    cpp,
    "    if (input.empty()) return {};\n"
    "    if (std::abs(inputRateHz - outputRateHz) < 0.5) return input;\n\n"
    "    state.resampleBuffer.insert(",
    "    if (input.empty()) return {};\n"
    "    if (std::abs(inputRateHz - outputRateHz) < 0.5) return input;\n\n"
    "    const int half = std::clamp(\n"
    "        state.resamplerHalf, kMinimumResamplerHalf, kMaximumResamplerHalf);\n"
    "    state.resampleBuffer.insert(",
    "resampler local half",
)
cpp = cpp.replace("kResamplerHalf", "half")
cpp = replace_once(
    cpp,
    "    const double cutoffCyclesPerInput =\n"
    "        0.45 * std::min(1.0, outputRateHz / inputRateHz);",
    "    // HF only needs the inner audio/data channel. Keeping the anti-alias\n"
    "    // cutoff below the final Nyquist edge creates a real transition band\n"
    "    // even when the device is running at several MS/s.\n"
    "    const double cutoffCyclesPerInput =\n"
    "        0.38 * std::min(1.0, outputRateHz / inputRateHz);",
    "anti-alias cutoff",
)
cpp = replace_once(
    cpp,
    "bool materiallyDifferent(double left, double right, double tolerance) noexcept {\n"
    "    return !std::isfinite(left) || !std::isfinite(right) ||\n"
    "           std::abs(left - right) > tolerance;\n"
    "}",
    "int adaptiveResamplerHalf(double inputRateHz, double outputRateHz) noexcept {\n"
    "    if (!(inputRateHz > 0.0) || !(outputRateHz > 0.0))\n"
    "        return kMinimumResamplerHalf;\n"
    "    const double ratio = std::max(1.0, inputRateHz / outputRateHz);\n"
    "    return std::clamp(\n"
    "        static_cast<int>(std::ceil(8.0 * ratio)),\n"
    "        kMinimumResamplerHalf, kMaximumResamplerHalf);\n"
    "}\n\n"
    "bool materiallyDifferent(double left, double right, double tolerance) noexcept {\n"
    "    return !std::isfinite(left) || !std::isfinite(right) ||\n"
    "           std::abs(left - right) > tolerance;\n"
    "}",
    "adaptive half helper",
)
cpp = replace_once(
    cpp,
    "        clearStreamingState(*state);",
    "        state->resamplerHalf = adaptiveResamplerHalf(inputRateHz, workRateHz);\n"
    "        clearStreamingState(*state);",
    "configuration resampler sizing",
)
cpp_path.write_text(cpp, encoding="utf-8")

catch_path = Path("tests/test_hf_demod.cpp")
catch = catch_path.read_text(encoding="utf-8")
catch += r'''

TEST_CASE("HF rate conversion rejects aliases from multi-megasample SDR streams",
          "[hf][resampler][alias]") {
    constexpr double wantedHz = 1500.0;
    constexpr double aliasHz = 48000.0 + wantedHz;

    for (const auto [inputRate, duration] :
         {std::pair{2.4e6, 0.12}, std::pair{10.0e6, 0.05}}) {
        const auto wantedIq = analyticTone(
            inputRate, duration, wantedHz, 0.15f);
        const auto aliasIq = analyticTone(
            inputRate, duration, aliasHz, 0.90f);

        Demodulator wantedDemod;
        Demodulator aliasDemod;
        const auto wanted = processHf(
            wantedDemod, wantedIq, inputRate,
            DemodMode::USB, 6000.0, 3000.0);
        const auto aliased = processHf(
            aliasDemod, aliasIq, inputRate,
            DemodMode::USB, 6000.0, 3000.0);

        INFO("input rate " << inputRate);
        REQUIRE(wanted.size() > 1000);
        REQUIRE(allFinite(wanted));
        REQUIRE(allFinite(aliased));
        REQUIRE(tailRms(wanted) >
                std::max(1.0e-9, tailRms(aliased)) * 20.0);
    }
}
'''
catch_path.write_text(catch.rstrip() + "\n", encoding="utf-8")

smoke_path = Path("tests/hf_smoke.cpp")
smoke = smoke_path.read_text(encoding="utf-8")
smoke = replace_once(
    smoke,
    "    require(rms(wanted) > rms(rejected) * 20.0,\n"
    "            \"USB opposite-sideband rejection is below 26 dB\");\n"
    "    require(HfDemod::supports(DemodMode::AM), \"AM ownership missing\");",
    "    require(rms(wanted) > rms(rejected) * 20.0,\n"
    "            \"USB opposite-sideband rejection is below 26 dB\");\n\n"
    "    int highRateWantedOwner = 3;\n"
    "    int highRateAliasOwner = 4;\n"
    "    constexpr double highRate = 2.4e6;\n"
    "    const auto highRateWantedIq = tone(\n"
    "        highRate, 0.12, 1500.0, 0.15f);\n"
    "    const auto highRateAliasIq = tone(\n"
    "        highRate, 0.12, 49500.0, 0.90f);\n"
    "    const auto highRateWanted = HfDemod::demodulate(\n"
    "        &highRateWantedOwner, highRateWantedIq, highRate, frequency, frequency,\n"
    "        DemodMode::USB, levelDb, 3000.0, -120.0, 1.0,\n"
    "        6000.0, 0, 48000.0);\n"
    "    const auto highRateAliased = HfDemod::demodulate(\n"
    "        &highRateAliasOwner, highRateAliasIq, highRate, frequency, frequency,\n"
    "        DemodMode::USB, levelDb, 3000.0, -120.0, 1.0,\n"
    "        6000.0, 0, 48000.0);\n"
    "    require(rms(highRateWanted) > rms(highRateAliased) * 20.0,\n"
    "            \"2.4 MS/s anti-alias rejection is below 26 dB\");\n\n"
    "    require(HfDemod::supports(DemodMode::AM), \"AM ownership missing\");",
    "standalone high-rate smoke",
)
smoke = replace_once(
    smoke,
    "    HfDemod::release(&wantedOwner);\n"
    "    HfDemod::release(&rejectedOwner);",
    "    HfDemod::release(&wantedOwner);\n"
    "    HfDemod::release(&rejectedOwner);\n"
    "    HfDemod::release(&highRateWantedOwner);\n"
    "    HfDemod::release(&highRateAliasOwner);",
    "standalone release",
)
smoke_path.write_text(smoke, encoding="utf-8")

verify_path = Path("scripts/verify_hf_integration.py")
verify = verify_path.read_text(encoding="utf-8")
verify = replace_once(
    verify,
    "    require(\"designComplexBandpass\" in hf_cpp,\n"
    "            \"complex sideband filter missing\")",
    "    require(\"designComplexBandpass\" in hf_cpp,\n"
    "            \"complex sideband filter missing\")\n"
    "    require(\"adaptiveResamplerHalf\" in hf_cpp and\n"
    "            \"kMaximumResamplerHalf = 1024\" in hf_cpp,\n"
    "            \"multi-MS/s anti-alias hardening missing\")\n"
    "    require(\"0.38 * std::min(1.0, outputRateHz / inputRateHz)\" in hf_cpp,\n"
    "            \"HF anti-alias transition band missing\")",
    "HF verifier anti-alias contract",
)
verify_path.write_text(verify, encoding="utf-8")

doc_path = Path("docs/HF_RECEIVE.md")
doc = doc_path.read_text(encoding="utf-8")
doc = replace_once(
    doc,
    "- Very short impulsive samples are replaced before the channel filter.\n",
    "- Very short impulsive samples are replaced before the channel filter.\n"
    "- The streaming rate converter expands its anti-alias kernel for common\n"
    "  multi-megasample SDR rates, preventing signals near 48 kHz multiples\n"
    "  from folding into the selected HF audio channel.\n",
    "HF documentation anti-alias property",
)
doc = replace_once(
    doc,
    "- safe fallback from stale wideband settings\n",
    "- safe fallback from stale wideband settings\n"
    "- multi-MS/s rate conversion and alias rejection at 2.4 and 10 MS/s\n",
    "HF validation list",
)
doc_path.write_text(doc, encoding="utf-8")
