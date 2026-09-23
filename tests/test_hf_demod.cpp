#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Demod.h"
#include "HfDemod.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;

std::vector<std::complex<float>> analyticTone(
    double sampleRate, double duration, double frequencyHz, float amplitude)
{
    const size_t count = static_cast<size_t>(
        std::llround(sampleRate * duration));
    std::vector<std::complex<float>> samples(count);
    for (size_t index = 0; index < count; ++index) {
        const double phase =
            2.0 * kPi * frequencyHz *
            static_cast<double>(index) / sampleRate;
        samples[index] = std::polar(amplitude, static_cast<float>(phase));
    }
    return samples;
}

std::vector<std::complex<float>> addSignals(
    std::vector<std::complex<float>> left,
    const std::vector<std::complex<float>>& right)
{
    const size_t count = std::min(left.size(), right.size());
    for (size_t index = 0; index < count; ++index) left[index] += right[index];
    return left;
}

std::vector<std::complex<float>> amTone(
    double sampleRate, double duration, double audioHz,
    float carrierAmplitude, float depth)
{
    const size_t count = static_cast<size_t>(
        std::llround(sampleRate * duration));
    std::vector<std::complex<float>> samples(count);
    for (size_t index = 0; index < count; ++index) {
        const double t = static_cast<double>(index) / sampleRate;
        const float envelope = carrierAmplitude *
            (1.0f + depth * static_cast<float>(
                std::sin(2.0 * kPi * audioHz * t)));
        samples[index] = {envelope, 0.0f};
    }
    return samples;
}

double tailRms(const std::vector<float>& samples, double keep = 0.65) {
    if (samples.empty()) return 0.0;
    const size_t begin = std::min(
        samples.size() - 1,
        static_cast<size_t>(samples.size() * (1.0 - keep)));
    double power = 0.0;
    for (size_t index = begin; index < samples.size(); ++index)
        power += static_cast<double>(samples[index]) * samples[index];
    return std::sqrt(power /
        static_cast<double>(samples.size() - begin));
}

double toneAmplitude(
    const std::vector<float>& samples, double sampleRate, double toneHz)
{
    if (samples.size() < 32) return 0.0;
    const size_t begin = samples.size() / 3;
    double inPhase = 0.0;
    double quadrature = 0.0;
    for (size_t index = begin; index < samples.size(); ++index) {
        const double phase =
            2.0 * kPi * toneHz * static_cast<double>(index) / sampleRate;
        inPhase += samples[index] * std::cos(phase);
        quadrature += samples[index] * std::sin(phase);
    }
    const double count = static_cast<double>(samples.size() - begin);
    return 2.0 * std::hypot(inPhase, quadrature) / count;
}

bool allFinite(const std::vector<float>& samples) {
    return std::all_of(samples.begin(), samples.end(), [](float value) {
        return std::isfinite(value);
    });
}

float peak(const std::vector<float>& samples) {
    float value = 0.0f;
    for (float sample : samples)
        value = std::max(value, std::abs(sample));
    return value;
}

std::vector<float> processHf(
    Demodulator& demod,
    const std::vector<std::complex<float>>& iq,
    double sampleRate,
    DemodMode mode,
    double channelBandwidth,
    double audioLowPass,
    FmMultiplexBlock* decoderAudio = nullptr)
{
    double levelDb = -140.0;
    return demod.demodulateToAudio(
        iq, sampleRate, 14.2e6, 14.2e6, mode, levelDb,
        audioLowPass, -120.0, 1.0, 75.0, 0.96,
        channelBandwidth, 0, 48000.0,
        std::numeric_limits<double>::quiet_NaN(), true,
        decoderAudio);
}

} // namespace

TEST_CASE("HF USB and LSB reject the opposite sideband", "[hf][ssb]") {
    constexpr double inputRate = 192000.0;
    constexpr double duration = 0.50;
    constexpr double toneHz = 1500.0;

    const auto upper = analyticTone(
        inputRate, duration, toneHz, 0.16f);
    const auto lower = analyticTone(
        inputRate, duration, -toneHz, 0.16f);

    Demodulator usbWantedDemod;
    Demodulator usbRejectedDemod;
    const auto usbWanted = processHf(
        usbWantedDemod, upper, inputRate, DemodMode::USB, 6000.0, 3000.0);
    const auto usbRejected = processHf(
        usbRejectedDemod, lower, inputRate, DemodMode::USB, 6000.0, 3000.0);

    Demodulator lsbWantedDemod;
    Demodulator lsbRejectedDemod;
    const auto lsbWanted = processHf(
        lsbWantedDemod, lower, inputRate, DemodMode::LSB, 6000.0, 3000.0);
    const auto lsbRejected = processHf(
        lsbRejectedDemod, upper, inputRate, DemodMode::LSB, 6000.0, 3000.0);

    REQUIRE(usbWanted.size() > 10000);
    REQUIRE(lsbWanted.size() > 10000);
    REQUIRE(allFinite(usbWanted));
    REQUIRE(allFinite(lsbWanted));

    const double usbRejectionDb = 20.0 * std::log10(
        tailRms(usbWanted) / std::max(1.0e-12, tailRms(usbRejected)));
    const double lsbRejectionDb = 20.0 * std::log10(
        tailRms(lsbWanted) / std::max(1.0e-12, tailRms(lsbRejected)));
    REQUIRE(usbRejectionDb > 30.0);
    REQUIRE(lsbRejectionDb > 30.0);
    REQUIRE(toneAmplitude(usbWanted, 48000.0, toneHz) > 0.08);
    REQUIRE(toneAmplitude(lsbWanted, 48000.0, toneHz) > 0.08);
}

TEST_CASE("HF SSB remains clean with a stronger opposite-sideband interferer",
          "[hf][ssb][adjacent]") {
    constexpr double inputRate = 192000.0;
    const auto wanted = analyticTone(inputRate, 0.50, 1200.0, 0.08f);
    const auto interferer = analyticTone(inputRate, 0.50, -2100.0, 0.65f);
    const auto combined = addSignals(wanted, interferer);

    Demodulator demod;
    const auto audio = processHf(
        demod, combined, inputRate, DemodMode::USB, 6000.0, 3000.0);

    REQUIRE(allFinite(audio));
    REQUIRE(peak(audio) <= 0.981f);
    const double wantedAmplitude =
        toneAmplitude(audio, 48000.0, 1200.0);
    const double rejectedAmplitude =
        toneAmplitude(audio, 48000.0, 2100.0);
    REQUIRE(wantedAmplitude > 0.08);
    REQUIRE(wantedAmplitude >
            std::max(1.0e-6, rejectedAmplitude) * 20.0);
}

TEST_CASE("HF AGC reduces overload quickly without clipping weak recovery",
          "[hf][agc]") {
    constexpr double inputRate = 192000.0;
    auto weakBefore = analyticTone(inputRate, 0.20, 1400.0, 0.025f);
    auto overload = analyticTone(inputRate, 0.08, 1400.0, 0.95f);
    auto weakAfter = analyticTone(inputRate, 0.35, 1400.0, 0.035f);
    weakBefore.insert(weakBefore.end(), overload.begin(), overload.end());
    weakBefore.insert(weakBefore.end(), weakAfter.begin(), weakAfter.end());

    Demodulator demod;
    const auto audio = processHf(
        demod, weakBefore, inputRate, DemodMode::USB, 6000.0, 3000.0);

    REQUIRE(allFinite(audio));
    REQUIRE(peak(audio) <= 0.981f);
    REQUIRE(tailRms(audio, 0.30) > 0.04);
    REQUIRE(tailRms(audio, 0.30) < 0.45);
}

TEST_CASE("HF impulse blanker preserves the wanted SSB tone", "[hf][noise]") {
    constexpr double inputRate = 192000.0;
    auto iq = analyticTone(inputRate, 0.50, 1700.0, 0.12f);
    for (size_t index = 2400; index < iq.size(); index += 4800)
        iq[index] += std::complex<float>(8.0f, -6.0f);

    Demodulator demod;
    const auto audio = processHf(
        demod, iq, inputRate, DemodMode::USB, 6000.0, 3000.0);

    REQUIRE(allFinite(audio));
    REQUIRE(peak(audio) <= 0.981f);
    REQUIRE(toneAmplitude(audio, 48000.0, 1700.0) > 0.08);
}

TEST_CASE("HF AM carrier normalization is stable across RF amplitude",
          "[hf][am]") {
    constexpr double inputRate = 192000.0;
    Demodulator lowDemod;
    Demodulator highDemod;
    const auto lowAudio = processHf(
        lowDemod, amTone(inputRate, 0.50, 1000.0, 0.18f, 0.55f),
        inputRate, DemodMode::AM, 10000.0, 5000.0);
    const auto highAudio = processHf(
        highDemod, amTone(inputRate, 0.50, 1000.0, 0.80f, 0.55f),
        inputRate, DemodMode::AM, 10000.0, 5000.0);

    REQUIRE(allFinite(lowAudio));
    REQUIRE(allFinite(highAudio));
    REQUIRE(toneAmplitude(lowAudio, 48000.0, 1000.0) > 0.10);
    REQUIRE(toneAmplitude(highAudio, 48000.0, 1000.0) > 0.10);
    const double ratio =
        tailRms(highAudio) / std::max(1.0e-9, tailRms(lowAudio));
    REQUIRE(ratio > 0.70);
    REQUIRE(ratio < 1.40);
}

TEST_CASE("HF CW produces a clean 700 Hz beat note and rejects offset carriers",
          "[hf][cw]") {
    constexpr double inputRate = 192000.0;
    const auto tunedCarrier = analyticTone(inputRate, 0.50, 0.0, 0.12f);
    const auto offsetCarrier = analyticTone(inputRate, 0.50, 2200.0, 0.12f);

    Demodulator wantedDemod;
    Demodulator rejectedDemod;
    const auto wanted = processHf(
        wantedDemod, tunedCarrier, inputRate,
        DemodMode::CW, 700.0, 1100.0);
    const auto rejected = processHf(
        rejectedDemod, offsetCarrier, inputRate,
        DemodMode::CW, 700.0, 1100.0);

    REQUIRE(allFinite(wanted));
    REQUIRE(toneAmplitude(wanted, 48000.0, 700.0) > 0.08);
    REQUIRE(tailRms(wanted) >
            std::max(1.0e-9, tailRms(rejected)) * 15.0);
}

TEST_CASE("HF stale wideband settings fall back to clean mode profiles",
          "[hf][profiles]") {
    constexpr double inputRate = 192000.0;
    const auto upper = analyticTone(inputRate, 0.40, 1600.0, 0.15f);
    const auto lower = analyticTone(inputRate, 0.40, -1600.0, 0.15f);

    Demodulator cleanDemod;
    Demodulator staleDemod;
    const auto clean = processHf(
        cleanDemod, upper, inputRate, DemodMode::USB, 6000.0, 3000.0);

    double levelDb = -140.0;
    const auto stale = staleDemod.demodulateToAudio(
        upper, inputRate, 14.2e6, 14.2e6, DemodMode::USB, levelDb,
        15000.0, -120.0, 1.0, 75.0, 0.96,
        180000.0, 0, 48000.0);

    Demodulator staleRejectDemod;
    const auto staleRejected = staleRejectDemod.demodulateToAudio(
        lower, inputRate, 14.2e6, 14.2e6, DemodMode::USB, levelDb,
        15000.0, -120.0, 1.0, 75.0, 0.96,
        180000.0, 0, 48000.0);

    REQUIRE(tailRms(stale) > tailRms(clean) * 0.70);
    REQUIRE(tailRms(stale) < tailRms(clean) * 1.40);
    REQUIRE(tailRms(stale) >
            std::max(1.0e-9, tailRms(staleRejected)) * 20.0);
}

TEST_CASE("HF SSB decoder tap is continuous for live SSTV", "[hf][sstv]") {
    constexpr double inputRate = 192000.0;
    const auto first = analyticTone(inputRate, 0.25, 1900.0, 0.15f);
    const auto second = analyticTone(inputRate, 0.25, 1900.0, 0.15f);

    Demodulator demod;
    FmMultiplexBlock firstTap;
    FmMultiplexBlock secondTap;
    const auto firstAudio = processHf(
        demod, first, inputRate, DemodMode::USB, 6000.0, 3000.0,
        &firstTap);
    const auto secondAudio = processHf(
        demod, second, inputRate, DemodMode::USB, 6000.0, 3000.0,
        &secondTap);

    REQUIRE_FALSE(firstAudio.empty());
    REQUIRE_FALSE(secondAudio.empty());
    REQUIRE(firstTap.discontinuity);
    REQUIRE_FALSE(secondTap.discontinuity);
    REQUIRE(firstTap.sampleRate == 48000.0);
    REQUIRE(secondTap.epoch == firstTap.epoch);
    REQUIRE(secondTap.firstSample ==
            firstTap.firstSample + firstTap.samples.size());
    REQUIRE(toneAmplitude(secondTap.samples, 48000.0, 1900.0) > 0.08);
}

TEST_CASE("HF explicit reset starts a new decoder epoch", "[hf][sstv][reset]") {
    constexpr double inputRate = 192000.0;
    const auto iq = analyticTone(inputRate, 0.25, 1900.0, 0.15f);

    Demodulator demod;
    FmMultiplexBlock firstTap;
    FmMultiplexBlock continuousTap;
    FmMultiplexBlock resetTap;
    processHf(demod, iq, inputRate, DemodMode::USB, 6000.0, 3000.0,
              &firstTap);
    processHf(demod, iq, inputRate, DemodMode::USB, 6000.0, 3000.0,
              &continuousTap);

    REQUIRE_FALSE(firstTap.samples.empty());
    REQUIRE_FALSE(continuousTap.discontinuity);
    demod.resetState();
    processHf(demod, iq, inputRate, DemodMode::USB, 6000.0, 3000.0,
              &resetTap);

    REQUIRE_FALSE(resetTap.samples.empty());
    REQUIRE(resetTap.discontinuity);
    REQUIRE(resetTap.epoch > continuousTap.epoch);
    REQUIRE(resetTap.firstSample == 0);
}

TEST_CASE("HF dispatch is limited to AM USB LSB and CW", "[hf][guard]") {
    REQUIRE(HfDemod::supports(DemodMode::AM));
    REQUIRE(HfDemod::supports(DemodMode::USB));
    REQUIRE(HfDemod::supports(DemodMode::LSB));
    REQUIRE(HfDemod::supports(DemodMode::CW));
    REQUIRE_FALSE(HfDemod::supports(DemodMode::NFM));
    REQUIRE_FALSE(HfDemod::supports(DemodMode::WFM));
    REQUIRE_FALSE(HfDemod::supports(DemodMode::AUTO));
}


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
