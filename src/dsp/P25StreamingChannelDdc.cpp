#include "dsp/P25StreamingChannelDdc.h"

#include "P25LiveDecoder.h"
#include "dsp/P25FilterCache.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace p25dsp {

namespace {
constexpr double kPi = std::numbers::pi;
constexpr int kResamplerRadius = 10;

std::complex<float> interpolateSinc(const std::vector<std::complex<float>>& samples,
                                    double pos,
                                    double cutoff)
{
    if (samples.empty()) return {};
    if (samples.size() == 1) return samples.front();

    const long center = static_cast<long>(std::floor(pos));
    double accI = 0.0;
    double accQ = 0.0;
    double wsum = 0.0;
    for (int n = -kResamplerRadius; n <= kResamplerRadius; ++n) {
        const long idx = center + n;
        if (idx < 0 || idx >= static_cast<long>(samples.size())) continue;
        const double d = pos - static_cast<double>(idx);
        const double sincArg = 2.0 * cutoff * d;
        const double sinc = std::abs(sincArg) < 1e-12
            ? 1.0
            : std::sin(kPi * sincArg) / (kPi * sincArg);
        const double winX = static_cast<double>(n + kResamplerRadius) /
            static_cast<double>(2 * kResamplerRadius);
        const double window = 0.42 - 0.5 * std::cos(2.0 * kPi * winX) +
            0.08 * std::cos(4.0 * kPi * winX);
        const double w = 2.0 * cutoff * sinc * window;
        accI += static_cast<double>(samples[static_cast<size_t>(idx)].real()) * w;
        accQ += static_cast<double>(samples[static_cast<size_t>(idx)].imag()) * w;
        wsum += w;
    }
    if (wsum <= 1e-12) return samples[static_cast<size_t>(std::clamp(center, 0L, static_cast<long>(samples.size()) - 1))];
    return {static_cast<float>(accI / wsum), static_cast<float>(accQ / wsum)};
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

void P25StreamingFirState::processInPlace(std::vector<std::complex<float>>& samples,
                                          const std::vector<double>& taps)
{
    if (samples.empty() || taps.empty()) return;

    const size_t tapCount = taps.size();
    std::vector<std::complex<float>> output;
    output.reserve(samples.size());

    for (const auto& sample : samples) {
        delay.push_back(sample);
        if (delay.size() > tapCount) {
            delay.erase(delay.begin());
        }
        if (delay.size() < tapCount) {
            output.push_back({0.0f, 0.0f});
            continue;
        }

        double accI = 0.0;
        double accQ = 0.0;
        for (size_t k = 0; k < tapCount; ++k) {
            const double w = taps[k];
            const auto& tapSample = delay[k];
            accI += static_cast<double>(tapSample.real()) * w;
            accQ += static_cast<double>(tapSample.imag()) * w;
        }
        output.emplace_back(static_cast<float>(accI), static_cast<float>(accQ));
    }

    samples = std::move(output);
}

void P25StreamingDecimatorState::process(const std::vector<std::complex<float>>& input,
                                         std::vector<std::complex<float>>& output,
                                         const std::vector<double>& taps,
                                         int decimationFactor)
{
    output.clear();
    if (input.empty() || taps.empty() || decimationFactor <= 1) {
        output = input;
        decimation = 1;
        return;
    }

    decimation = decimationFactor;
    const size_t tapCount = taps.size();

    for (const auto& sample : input) {
        fir.delay.push_back(sample);
        if (fir.delay.size() > tapCount) {
            fir.delay.erase(fir.delay.begin());
        }

        if (inputSamplesProcessed % static_cast<uint64_t>(decimation) == 0 &&
            fir.delay.size() >= tapCount) {
            double accI = 0.0;
            double accQ = 0.0;
            for (size_t k = 0; k < tapCount; ++k) {
                const double w = taps[k];
                const auto& tapSample = fir.delay[k];
                accI += static_cast<double>(tapSample.real()) * w;
                accQ += static_cast<double>(tapSample.imag()) * w;
            }
            output.emplace_back(static_cast<float>(accI), static_cast<float>(accQ));
        }
        ++inputSamplesProcessed;
    }
}

void P25StreamingResamplerState::resample(const std::vector<std::complex<float>>& input,
                                          std::vector<std::complex<float>>& output,
                                          double inputRateHz,
                                          double outputRateHz)
{
    output.clear();
    if (input.size() < 2 || !std::isfinite(inputRateHz) || !std::isfinite(outputRateHz) ||
        inputRateHz <= 0.0 || outputRateHz <= 0.0) {
        output = input;
        return;
    }

    const bool ratesChanged = std::abs(inputRate - inputRateHz) > 1e-6 ||
        std::abs(outputRate - outputRateHz) > 1e-6;
    if (ratesChanged) {
        prefix.clear();
        totalInputSamples = 0;
        nextOutAbsolutePos = 0.0;
        inputRate = inputRateHz;
        outputRate = outputRateHz;
    }

    const uint64_t blockStartAbsolute = totalInputSamples;
    std::vector<std::complex<float>> stream;
    stream.reserve(prefix.size() + input.size());
    stream.insert(stream.end(), prefix.begin(), prefix.end());
    stream.insert(stream.end(), input.begin(), input.end());
    const uint64_t streamStartAbsolute = blockStartAbsolute - prefix.size();
    const uint64_t streamEndAbsolute = blockStartAbsolute + input.size();

    const double step = inputRate / outputRate;
    const double cutoff = std::min(0.46, 0.46 * outputRate / inputRate);

    while (nextOutAbsolutePos + 1.0 < static_cast<double>(streamEndAbsolute)) {
        if (nextOutAbsolutePos + 1.0 < static_cast<double>(streamStartAbsolute)) {
            nextOutAbsolutePos += step;
            continue;
        }
        const double localPos = static_cast<double>(nextOutAbsolutePos) -
            static_cast<double>(streamStartAbsolute);
        if (localPos >= 0.0 && localPos < static_cast<double>(stream.size()) - 1.0) {
            output.push_back(interpolateSinc(stream, localPos, cutoff));
        }
        nextOutAbsolutePos += step;
    }

    const size_t keep = std::min(static_cast<size_t>(2 * kResamplerRadius + 2), stream.size());
    prefix.assign(stream.end() - static_cast<std::ptrdiff_t>(keep), stream.end());
    totalInputSamples += input.size();
}

void P25StreamingChannelDdc::resetFilterChain() noexcept
{
    m_decimator.reset();
    m_channelFir.reset();
    m_resampler.reset();
    m_lastIntermediateRate = 0.0;
    m_lastOutputRate = 0.0;
    m_lastDecimation = 1;
}

void P25StreamingChannelDdc::reset() noexcept
{
    m_nco = {};
    m_lastOffsetHz = 0.0;
    m_lastSampleRate = 0.0;
    resetFilterChain();
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
        resetFilterChain();
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

    if (decim != m_lastDecimation ||
        std::abs(intermediateRate - m_lastIntermediateRate) > 1e-3 ||
        std::abs(outputRate - m_lastOutputRate) > 1e-3) {
        resetFilterChain();
        m_lastDecimation = decim;
        m_lastIntermediateRate = intermediateRate;
        m_lastOutputRate = outputRate;
    }

    if (decim > 1) {
        const double antiAliasCutoffHz = std::clamp(intermediateRate * 0.42,
                                                    config.channelBandwidthHz * 2.5,
                                                    sampleRate * 0.45);
        const double antiAliasTransitionHz = std::clamp(intermediateRate * 0.18,
                                                        config.channelBandwidthHz,
                                                        sampleRate * 0.20);
        const auto& antiAliasTaps = filterCache.lowpassTaps(
            sampleRate, antiAliasCutoffHz, antiAliasTransitionHz, 161);
        m_decimator.process(m_mixedScratch, m_decimatedScratch, antiAliasTaps, decim);
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
    m_channelFir.processInPlace(m_channelScratch, channelTaps);

    double finalRate = intermediateRate;
    if (std::abs(intermediateRate - outputRate) > outputRate * 0.002) {
        m_resampler.resample(m_channelScratch, out.samples, intermediateRate, outputRate);
        finalRate = outputRate;
    } else {
        out.samples = std::move(m_channelScratch);
    }

    out.sampleRate = finalRate;
    out.heapAllocs = 0;
    return out;
}

} // namespace p25dsp
