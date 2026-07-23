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

struct P25StreamingFirState {
    std::vector<std::complex<float>> delay;

    void reset() noexcept { delay.clear(); }
    void processInPlace(std::vector<std::complex<float>>& samples, const std::vector<double>& taps);
};

struct P25StreamingDecimatorState {
    P25StreamingFirState fir;
    int decimation = 1;
    uint64_t inputSamplesProcessed = 0;

    void reset() noexcept
    {
        fir.reset();
        decimation = 1;
        inputSamplesProcessed = 0;
    }

    void process(const std::vector<std::complex<float>>& input,
                 std::vector<std::complex<float>>& output,
                 const std::vector<double>& taps,
                 int decimationFactor);
};

struct P25StreamingResamplerState {
    std::vector<std::complex<float>> prefix;
    uint64_t totalInputSamples = 0;
    double nextOutAbsolutePos = 0.0;
    double inputRate = 0.0;
    double outputRate = 0.0;

    void reset() noexcept
    {
        prefix.clear();
        totalInputSamples = 0;
        nextOutAbsolutePos = 0.0;
        inputRate = 0.0;
        outputRate = 0.0;
    }

    void resample(const std::vector<std::complex<float>>& input,
                  std::vector<std::complex<float>>& output,
                  double inputRateHz,
                  double outputRateHz);
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
    void resetFilterChain() noexcept;

    P25ComplexNco m_nco;
    double m_lastOffsetHz = 0.0;
    double m_lastSampleRate = 0.0;
    double m_lastIntermediateRate = 0.0;
    double m_lastOutputRate = 0.0;
    int m_lastDecimation = 1;

    P25StreamingDecimatorState m_decimator;
    P25StreamingFirState m_channelFir;
    P25StreamingResamplerState m_resampler;

    std::vector<std::complex<float>> m_mixedScratch;
    std::vector<std::complex<float>> m_decimatedScratch;
    std::vector<std::complex<float>> m_channelScratch;
};

} // namespace p25dsp
