#include "dsp/P25StreamingChannelDdc.h"

#include "P25LiveDecoder.h"
#include "dsp/P25FilterCache.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace p25dsp {

namespace {
constexpr double kPi = std::numbers::pi;

void applyFirInPlace(std::vector<std::complex<float>>& x, const std::vector<double>& taps)
{
    if (x.empty() || taps.empty()) return;
    const std::vector<std::complex<float>> input = x;
    const int half = static_cast<int>(taps.size() / 2);
    for (size_t n = 0; n < x.size(); ++n) {
        double accI = 0.0;
        double accQ = 0.0;
        for (size_t k = 0; k < taps.size(); ++k) {
            const int idx = static_cast<int>(n) + static_cast<int>(k) - half;
            if (idx < 0 || idx >= static_cast<int>(input.size())) continue;
            const double w = taps[k];
            accI += static_cast<double>(input[static_cast<size_t>(idx)].real()) * w;
            accQ += static_cast<double>(input[static_cast<size_t>(idx)].imag()) * w;
        }
        x[n] = {static_cast<float>(accI), static_cast<float>(accQ)};
    }
}

void decimateFirInPlace(const std::vector<std::complex<float>>& input,
                        std::vector<std::complex<float>>& output,
                        const std::vector<double>& taps,
                        int decimation)
{
    output.clear();
    if (input.empty() || taps.empty() || decimation <= 1) {
        output = input;
        return;
    }
    output.reserve(input.size() / static_cast<size_t>(decimation) + 1);
    const int half = static_cast<int>(taps.size() / 2);
    for (size_t n = 0; n < input.size(); n += static_cast<size_t>(decimation)) {
        double accI = 0.0;
        double accQ = 0.0;
        for (size_t k = 0; k < taps.size(); ++k) {
            const int idx = static_cast<int>(n) + static_cast<int>(k) - half;
            if (idx < 0 || idx >= static_cast<int>(input.size())) continue;
            const double w = taps[k];
            accI += static_cast<double>(input[static_cast<size_t>(idx)].real()) * w;
            accQ += static_cast<double>(input[static_cast<size_t>(idx)].imag()) * w;
        }
        output.emplace_back(static_cast<float>(accI), static_cast<float>(accQ));
    }
}

void resampleWindowedSincInPlace(const std::vector<std::complex<float>>& input,
                                 std::vector<std::complex<float>>& output,
                                 double inputRate,
                                 double outputRate)
{
    output.clear();
    if (input.size() < 2 || !std::isfinite(inputRate) || !std::isfinite(outputRate) ||
        inputRate <= 0.0 || outputRate <= 0.0) {
        output = input;
        return;
    }
    const double outCountExact = static_cast<double>(input.size()) * outputRate / inputRate;
    const size_t outCount = static_cast<size_t>(std::max(2.0, std::floor(outCountExact)));
    output.reserve(outCount);
    const double step = inputRate / outputRate;
    constexpr int radius = 10;
    const double cutoff = std::min(0.46, 0.46 * outputRate / inputRate);
    for (size_t i = 0; i < outCount; ++i) {
        const double pos = static_cast<double>(i) * step;
        if (pos > static_cast<double>(input.size() - 1)) break;
        const long center = static_cast<long>(std::floor(pos));
        double accI = 0.0;
        double accQ = 0.0;
        double wsum = 0.0;
        for (int n = -radius; n <= radius; ++n) {
            const long idx = center + n;
            if (idx < 0 || idx >= static_cast<long>(input.size())) continue;
            const double d = pos - static_cast<double>(idx);
            const double sincArg = 2.0 * cutoff * d;
            const double sinc = std::abs(sincArg) < 1e-12
                ? 1.0
                : std::sin(kPi * sincArg) / (kPi * sincArg);
            const double winX = static_cast<double>(n + radius) / static_cast<double>(2 * radius);
            const double window = 0.42 - 0.5 * std::cos(2.0 * kPi * winX) + 0.08 * std::cos(4.0 * kPi * winX);
            const double w = 2.0 * cutoff * sinc * window;
            accI += static_cast<double>(input[static_cast<size_t>(idx)].real()) * w;
            accQ += static_cast<double>(input[static_cast<size_t>(idx)].imag()) * w;
            wsum += w;
        }
        if (wsum > 1e-12) {
            accI /= wsum;
            accQ /= wsum;
        }
        output.emplace_back(static_cast<float>(accI), static_cast<float>(accQ));
    }
}

} // namespace

void P25ComplexNco::setFrequencyHz(double frequencyHz, double sampleRateHz) noexcept
{
    const double angle = -2.0 * kPi * frequencyHz / std::max(sampleRateHz, 1.0);
    stepReal = static_cast<float>(std::cos(angle));
    stepImag = static_cast<float>(std::sin(angle));
    real = 1.0f;
    imag = 0.0f;
    samplesSinceRenorm = 0;
}

void P25ComplexNco::mixSample(float inI, float inQ, float& outI, float& outQ) noexcept
{
    outI = inI * real - inQ * imag;
    outQ = inI * imag + inQ * real;
    const float nextReal = real * stepReal - imag * stepImag;
    const float nextImag = imag * stepReal + real * stepImag;
    real = nextReal;
    imag = nextImag;
    if (++samplesSinceRenorm >= 512) {
        const float invMag = 1.0f / std::sqrt(real * real + imag * imag + 1e-12f);
        real *= invMag;
        imag *= invMag;
        samplesSinceRenorm = 0;
    }
}

void P25StreamingChannelDdc::reset() noexcept
{
    m_nco = {};
    m_lastOffsetHz = 0.0;
    m_lastSampleRate = 0.0;
    m_mixedScratch.clear();
    m_decimatedScratch.clear();
    m_channelScratch.clear();
}

P25StreamingChannelDdcResult P25StreamingChannelDdc::process(
    const std::vector<std::complex<float>>& iq,
    double sampleRate,
    double centerFreqHz,
    double targetFreqHz,
    const P25LiveDecoderConfig& config,
    P25FilterCache& filterCache)
{
    P25StreamingChannelDdcResult out;
    if (iq.size() < 2 || !std::isfinite(sampleRate) || sampleRate <= 0.0) return out;

    const double offsetHz = targetFreqHz - centerFreqHz;
    if (std::abs(offsetHz) > sampleRate * 0.55) return out;

    if (std::abs(offsetHz - m_lastOffsetHz) > 0.5 || std::abs(sampleRate - m_lastSampleRate) > 1.0) {
        m_nco.setFrequencyHz(offsetHz, sampleRate);
        m_lastOffsetHz = offsetHz;
        m_lastSampleRate = sampleRate;
    }

    m_mixedScratch.resize(iq.size());
    for (size_t i = 0; i < iq.size(); ++i) {
        float oI = 0.0f;
        float oQ = 0.0f;
        m_nco.mixSample(iq[i].real(), iq[i].imag(), oI, oQ);
        m_mixedScratch[i] = {oI, oQ};
    }
    out.bytesCopied += iq.size() * sizeof(std::complex<float>) * 2;

    const double outputRate = std::clamp(
        std::max(config.workSampleRate, config.symbolRate * 10.0),
        config.symbolRate * 8.0,
        std::min(sampleRate, 96000.0));
    const double intermediateTarget = std::clamp(
        std::max({outputRate * 4.0, config.channelBandwidthHz * 14.0, config.symbolRate * 24.0}),
        outputRate,
        std::min(sampleRate, 240000.0));
    const int decim = sampleRate > intermediateTarget * 1.25
        ? std::max(1, static_cast<int>(std::llround(sampleRate / intermediateTarget)))
        : 1;
    const double intermediateRate = sampleRate / static_cast<double>(decim);

    if (decim > 1) {
        const double antiAliasCutoffHz = std::clamp(intermediateRate * 0.42,
                                                    config.channelBandwidthHz * 2.5,
                                                    sampleRate * 0.45);
        const double antiAliasTransitionHz = std::clamp(intermediateRate * 0.18,
                                                        config.channelBandwidthHz,
                                                        sampleRate * 0.20);
        const auto& antiAliasTaps = filterCache.lowpassTaps(
            sampleRate, antiAliasCutoffHz, antiAliasTransitionHz, 161);
        decimateFirInPlace(m_mixedScratch, m_decimatedScratch, antiAliasTaps, decim);
    } else {
        m_decimatedScratch = m_mixedScratch;
    }

    if (m_decimatedScratch.size() < 2) return out;

    m_channelScratch = m_decimatedScratch;
    const double channelCutoffHz = std::clamp(config.channelBandwidthHz * 0.58, config.symbolRate * 1.15,
                                             std::min(intermediateRate * 0.42, outputRate * 0.45));
    const double channelTransitionHz = std::clamp(config.channelBandwidthHz * 0.25, 1800.0, 6000.0);
    const auto& channelTaps = filterCache.lowpassTaps(
        intermediateRate, channelCutoffHz, channelTransitionHz, 161);
    applyFirInPlace(m_channelScratch, channelTaps);

    double finalRate = intermediateRate;
    if (std::abs(intermediateRate - outputRate) > outputRate * 0.002) {
        resampleWindowedSincInPlace(m_channelScratch, out.samples, intermediateRate, outputRate);
        finalRate = outputRate;
    } else {
        out.samples = std::move(m_channelScratch);
    }

    out.sampleRate = finalRate;
    out.heapAllocs = 0;
    return out;
}

} // namespace p25dsp
