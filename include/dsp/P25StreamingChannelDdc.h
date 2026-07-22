#pragma once

#include <complex>
#include <cstdint>
#include <vector>

struct P25LiveDecoderConfig;

namespace p25dsp {

class P25FilterCache;

struct P25ComplexNco {
    float real = 1.0f;
    float imag = 0.0f;
    float stepReal = 1.0f;
    float stepImag = 0.0f;
    uint32_t samplesSinceRenorm = 0;

    void setFrequencyHz(double frequencyHz, double sampleRateHz) noexcept;
    void mixSample(float inI, float inQ, float& outI, float& outQ) noexcept;
};

struct P25StreamingChannelDdcResult {
    std::vector<std::complex<float>> samples;
    double sampleRate = 0.0;
    uint64_t bytesCopied = 0;
    uint64_t heapAllocs = 0;
};

class P25StreamingChannelDdc {
public:
    P25StreamingChannelDdcResult process(const std::vector<std::complex<float>>& iq,
                                         double sampleRate,
                                         double centerFreqHz,
                                         double targetFreqHz,
                                         const P25LiveDecoderConfig& config,
                                         P25FilterCache& filterCache);

    void reset() noexcept;

private:
    P25ComplexNco m_nco;
    double m_lastOffsetHz = 0.0;
    double m_lastSampleRate = 0.0;
    std::vector<std::complex<float>> m_mixedScratch;
    std::vector<std::complex<float>> m_decimatedScratch;
    std::vector<std::complex<float>> m_channelScratch;
};

} // namespace p25dsp
