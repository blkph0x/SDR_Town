#include "P25LiveDecoder.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <limits>
#include <numeric>
#include <optional>
#include <unordered_map>
#include <mutex>
#include <deque>
#include <utility>

#ifdef HAVE_MBELIB
extern "C" {
#include <mbelib.h>
}
#endif

namespace {

constexpr double kPi = 3.14159265358979323846;

double clampFinite(double v, double fallback)
{
    return std::isfinite(v) ? v : fallback;
}

double percentile(std::vector<double>& values, double p)
{
    if (values.empty()) return 0.0;
    values.erase(std::remove_if(values.begin(), values.end(), [](double v) {
        return !std::isfinite(v);
    }), values.end());
    if (values.empty()) return 0.0;
    const double pos = std::clamp(p, 0.0, 1.0) * static_cast<double>(values.size() - 1);
    const size_t lo = static_cast<size_t>(std::floor(pos));
    const size_t hi = static_cast<size_t>(std::ceil(pos));
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(lo), values.end());
    const double loVal = values[lo];
    if (hi == lo) return loVal;
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(hi), values.end());
    const double hiVal = values[hi];
    const double frac = pos - static_cast<double>(lo);
    return loVal * (1.0 - frac) + hiVal * frac;
}

struct SecondOrderTimingLoop {
    double omega = 0.0;
    double omegaMin = 0.0;
    double omegaMax = 0.0;
    double gainMu = 0.0;
    double gainOmega = 0.0;
    double mu = 0.0;

    SecondOrderTimingLoop(double samplesPerSymbol,
                          double symbolRate,
                          double loopBandwidth = 0.010,
                          double damping = 0.707)
    {
        reset(samplesPerSymbol, symbolRate, loopBandwidth, damping);
    }

    void reset(double samplesPerSymbol,
               double symbolRate,
               double loopBandwidth = 0.010,
               double damping = 0.707)
    {
        const double sps = std::max(samplesPerSymbol, 1e-6);
        omega = sps;
        omegaMin = sps * 0.985;
        omegaMax = sps * 1.015;
        mu = std::clamp(sps * 0.5, 0.0, std::max(0.0, sps - 1.0));

        const double normalized = std::clamp(loopBandwidth, 0.001, 0.060);
        const double denom = 1.0 + 2.0 * damping * normalized + normalized * normalized;
        // Type-II second-order timing PLL coefficients.  omega tracks samples/symbol,
        // while mu tracks the current fractional strobe position within this block.
        gainMu = (4.0 * damping * normalized / denom) * std::max(1.0, sps / std::max(symbolRate, 1.0));
        gainOmega = (4.0 * normalized * normalized / denom) * std::max(0.05, sps / 8.0);
        gainMu = std::clamp(gainMu, 0.004, 0.090);
        gainOmega = std::clamp(gainOmega, 0.00005, 0.0030);
    }

    double advance(double timingError)
    {
        const double e = std::clamp(timingError, -1.0, 1.0);
        omega = std::clamp(omega + gainOmega * e, omegaMin, omegaMax);
        const double step = std::max(1.0, omega + gainMu * e);
        mu += step;
        return step;
    }
};

struct StreamingBitCorrelator48 {
    static constexpr uint64_t kMask = (1ull << 48) - 1ull;
    uint64_t reg = 0;
    size_t seen = 0;

    int push(uint8_t bit, uint64_t syncWord) noexcept
    {
        reg = ((reg << 1) | static_cast<uint64_t>(bit & 1u)) & kMask;
        if (seen < 48) ++seen;
        if (seen < 48) return 49;
        return static_cast<int>(std::popcount((reg ^ syncWord) & kMask));
    }
};

struct StreamingDibitCorrelator40 {
    static constexpr uint64_t kMask = (1ull << 40) - 1ull;
    uint64_t reg = 0;
    size_t seen = 0;

    int push(int dibit, uint64_t syncWord) noexcept
    {
        reg = ((reg << 2) | static_cast<uint64_t>(dibit & 0x03)) & kMask;
        if (seen < 20) ++seen;
        if (seen < 20) return 41;
        return static_cast<int>(std::popcount((reg ^ syncWord) & kMask));
    }
};

struct StreamingSyncHit {
    bool matched = false;
    size_t offset = 0;
    int bitErrors = 0;
    bool inverted = false;
};

class P25Phase1StreamingSync {
public:
    StreamingSyncHit push(uint8_t bit, uint64_t offset, int maxErrors) noexcept
    {
        const uint64_t inverted = P25LiveDecoder::FrameSyncWord ^ StreamingBitCorrelator48::kMask;
        const int normalErrors = m_normal.push(bit, P25LiveDecoder::FrameSyncWord);
        const int invertedErrors = m_inverted.push(bit, inverted);
        StreamingSyncHit hit;
        if (normalErrors <= maxErrors || invertedErrors <= maxErrors) {
            hit.matched = true;
            hit.offset = offset >= 47 ? static_cast<size_t>(offset - 47) : 0;
            hit.inverted = invertedErrors < normalErrors;
            hit.bitErrors = std::min(normalErrors, invertedErrors);
        }
        return hit;
    }
private:
    StreamingBitCorrelator48 m_normal;
    StreamingBitCorrelator48 m_inverted;
};

class P25Phase2StreamingSync {
public:
    StreamingSyncHit push(int dibit, uint64_t dibitOffset, int maxErrors) noexcept
    {
        const int normalErrors = m_normal.push(dibit, P25LiveDecoder::Phase2FrameSyncWord);
        const int invertedErrors = m_inverted.push(dibit, P25LiveDecoder::Phase2FrameSyncWord ^ StreamingDibitCorrelator40::kMask);
        StreamingSyncHit hit;
        if (normalErrors <= maxErrors || invertedErrors <= maxErrors) {
            hit.matched = true;
            hit.offset = dibitOffset >= 19 ? static_cast<size_t>(dibitOffset - 19) : 0;
            hit.inverted = invertedErrors < normalErrors;
            hit.bitErrors = std::min(normalErrors, invertedErrors);
        }
        return hit;
    }
private:
    StreamingDibitCorrelator40 m_normal;
    StreamingDibitCorrelator40 m_inverted;
};

struct P25BlockTimingState {
    bool c4fmValid = false;
    bool cqpskValid = false;
    double c4fmOmega = 0.0;
    double c4fmMu = 0.0;
    double cqpskOmega = 0.0;
    double cqpskMu = 0.0;
    double c4fmSampleRate = 0.0;
    double cqpskSampleRate = 0.0;
    std::vector<float> c4fmTail;
    std::vector<std::complex<float>> cqpskTail;
};

struct P25StreamingDecoderState {
    P25BlockTimingState timing;
    std::deque<uint8_t> phase1BitTail;
};

static std::mutex gP25StreamingStateMutex;
static std::unordered_map<const P25LiveDecoder*, P25StreamingDecoderState> gP25StreamingState;

P25StreamingDecoderState loadStreamingState(const P25LiveDecoder* decoder)
{
    std::lock_guard<std::mutex> lk(gP25StreamingStateMutex);
    return gP25StreamingState[decoder];
}

void storeTimingState(const P25LiveDecoder* decoder, const P25BlockTimingState& timing)
{
    std::lock_guard<std::mutex> lk(gP25StreamingStateMutex);
    gP25StreamingState[decoder].timing = timing;
}

void eraseStreamingState(const P25LiveDecoder* decoder)
{
    std::lock_guard<std::mutex> lk(gP25StreamingStateMutex);
    gP25StreamingState.erase(decoder);
}

std::vector<uint8_t> buildPhase1BitStreamWithTail(const P25LiveDecoder* decoder,
                                                  const std::vector<uint8_t>& inputBits,
                                                  size_t& prefixBits)
{
    constexpr size_t kPhase1TailBits = P25LiveDecoder::FrameSyncBits + 512;
    std::vector<uint8_t> bits;
    {
        std::lock_guard<std::mutex> lk(gP25StreamingStateMutex);
        auto& state = gP25StreamingState[decoder];
        prefixBits = std::min(state.phase1BitTail.size(), kPhase1TailBits);
        bits.reserve(prefixBits + inputBits.size());
        if (prefixBits > 0) {
            bits.insert(bits.end(),
                        state.phase1BitTail.end() - static_cast<std::ptrdiff_t>(prefixBits),
                        state.phase1BitTail.end());
        }
    }
    for (uint8_t bit : inputBits) bits.push_back(bit ? 1u : 0u);
    return bits;
}

void storePhase1BitTail(const P25LiveDecoder* decoder, const std::vector<uint8_t>& bits)
{
    constexpr size_t kPhase1TailBits = P25LiveDecoder::FrameSyncBits + 512;
    std::lock_guard<std::mutex> lk(gP25StreamingStateMutex);
    auto& tail = gP25StreamingState[decoder].phase1BitTail;
    tail.clear();
    const size_t keep = std::min(bits.size(), kPhase1TailBits);
    if (keep > 0) {
        tail.insert(tail.end(), bits.end() - static_cast<std::ptrdiff_t>(keep), bits.end());
    }
}

template <typename T>
void appendTailPrefix(std::vector<T>& block, const std::vector<T>& tail, size_t maxTail)
{
    if (tail.empty() || maxTail == 0) return;
    const size_t keep = std::min(tail.size(), maxTail);
    std::vector<T> combined;
    combined.reserve(keep + block.size());
    combined.insert(combined.end(), tail.end() - static_cast<std::ptrdiff_t>(keep), tail.end());
    combined.insert(combined.end(), block.begin(), block.end());
    block.swap(combined);
}

template <typename T>
void storeTail(std::vector<T>& tail, const std::vector<T>& block, size_t maxTail)
{
    tail.clear();
    if (block.empty() || maxTail == 0) return;
    const size_t keep = std::min(block.size(), maxTail);
    tail.insert(tail.end(), block.end() - static_cast<std::ptrdiff_t>(keep), block.end());
}

double sampleLinear(const std::vector<float>& x, double index)
{
    if (x.empty()) return 0.0;
    if (index <= 0.0) return x.front();
    const double last = static_cast<double>(x.size() - 1);
    if (index >= last) return x.back();
    const size_t i = static_cast<size_t>(index);
    const double frac = index - static_cast<double>(i);
    return static_cast<double>(x[i]) * (1.0 - frac) + static_cast<double>(x[i + 1]) * frac;
}

std::complex<float> sampleLinearComplex(const std::vector<std::complex<float>>& x, double index)
{
    if (x.empty()) return {};
    if (index <= 0.0) return x.front();
    const double last = static_cast<double>(x.size() - 1);
    if (index >= last) return x.back();
    const size_t i = static_cast<size_t>(index);
    const double frac = index - static_cast<double>(i);
    const auto a = x[i];
    const auto b = x[i + 1];
    return {
        static_cast<float>(static_cast<double>(a.real()) * (1.0 - frac) + static_cast<double>(b.real()) * frac),
        static_cast<float>(static_cast<double>(a.imag()) * (1.0 - frac) + static_cast<double>(b.imag()) * frac),
    };
}

std::vector<double> designLowpassTaps(double sampleRate, double cutoffHz, double transitionHz, int maxTaps)
{
    if (!std::isfinite(sampleRate) || sampleRate <= 0.0) return {1.0};
    cutoffHz = std::clamp(cutoffHz, 10.0, sampleRate * 0.45);
    transitionHz = std::clamp(transitionHz, sampleRate * 0.005, sampleRate * 0.25);
    int taps = static_cast<int>(std::ceil(4.0 * sampleRate / transitionHz));
    taps = std::clamp(taps, 31, std::max(31, maxTaps));
    if ((taps & 1) == 0) ++taps;
    const int mid = taps / 2;
    const double fc = cutoffHz / sampleRate;

    std::vector<double> h(static_cast<size_t>(taps), 0.0);
    double sum = 0.0;
    for (int n = 0; n < taps; ++n) {
        const int m = n - mid;
        const double sinc = (m == 0)
            ? 2.0 * fc
            : std::sin(2.0 * kPi * fc * static_cast<double>(m)) / (kPi * static_cast<double>(m));
        const double x = taps == 1 ? 0.0 : (2.0 * kPi * static_cast<double>(n)) / static_cast<double>(taps - 1);
        const double window = 0.35875 - 0.48829 * std::cos(x) + 0.14128 * std::cos(2.0 * x) -
            0.01168 * std::cos(3.0 * x);
        h[static_cast<size_t>(n)] = sinc * window;
        sum += h[static_cast<size_t>(n)];
    }
    if (std::abs(sum) > 1e-12) {
        for (double& v : h) v /= sum;
    }
    return h;
}

std::vector<std::complex<float>> applyFirSame(const std::vector<std::complex<float>>& x,
                                              const std::vector<double>& taps)
{
    if (x.empty() || taps.empty()) return {};
    std::vector<std::complex<float>> out(x.size());
    const int half = static_cast<int>(taps.size() / 2);
    for (size_t n = 0; n < x.size(); ++n) {
        std::complex<double> acc(0.0, 0.0);
        for (size_t k = 0; k < taps.size(); ++k) {
            const int idx = static_cast<int>(n) + static_cast<int>(k) - half;
            if (idx < 0 || idx >= static_cast<int>(x.size())) continue;
            const double w = taps[k];
            acc += std::complex<double>(x[static_cast<size_t>(idx)].real(), x[static_cast<size_t>(idx)].imag()) * w;
        }
        out[n] = {static_cast<float>(acc.real()), static_cast<float>(acc.imag())};
    }
    return out;
}

std::vector<float> applyFirSame(const std::vector<float>& x, const std::vector<double>& taps)
{
    if (x.empty() || taps.empty()) return {};
    std::vector<float> out(x.size(), 0.0f);
    const int half = static_cast<int>(taps.size() / 2);
    for (size_t n = 0; n < x.size(); ++n) {
        double acc = 0.0;
        for (size_t k = 0; k < taps.size(); ++k) {
            const int idx = static_cast<int>(n) + static_cast<int>(k) - half;
            if (idx < 0 || idx >= static_cast<int>(x.size())) continue;
            const double w = taps[k];
            acc += static_cast<double>(x[static_cast<size_t>(idx)]) * w;
        }
        out[n] = static_cast<float>(acc);
    }
    return out;
}

std::vector<std::complex<float>> resampleWindowedSinc(const std::vector<std::complex<float>>& x,
                                                      double inputRate,
                                                      double outputRate)
{
    if (x.size() < 2 || !std::isfinite(inputRate) || !std::isfinite(outputRate) ||
        inputRate <= 0.0 || outputRate <= 0.0) {
        return x;
    }
    const double outCountExact = static_cast<double>(x.size()) * outputRate / inputRate;
    const size_t outCount = static_cast<size_t>(std::max(2.0, std::floor(outCountExact)));
    std::vector<std::complex<float>> out;
    out.reserve(outCount);
    const double step = inputRate / outputRate;
    const int radius = 10;
    const double cutoff = std::min(0.46, 0.46 * outputRate / inputRate);
    for (size_t i = 0; i < outCount; ++i) {
        const double pos = static_cast<double>(i) * step;
        if (pos > static_cast<double>(x.size() - 1)) break;
        const long center = static_cast<long>(std::floor(pos));
        std::complex<double> acc(0.0, 0.0);
        double wsum = 0.0;
        for (int n = -radius; n <= radius; ++n) {
            const long idx = center + n;
            if (idx < 0 || idx >= static_cast<long>(x.size())) continue;
            const double d = pos - static_cast<double>(idx);
            const double sincArg = 2.0 * cutoff * d;
            const double sinc = std::abs(sincArg) < 1e-12
                ? 1.0
                : std::sin(kPi * sincArg) / (kPi * sincArg);
            const double winX = static_cast<double>(n + radius) / static_cast<double>(2 * radius);
            const double window = 0.42 - 0.5 * std::cos(2.0 * kPi * winX) + 0.08 * std::cos(4.0 * kPi * winX);
            const double w = 2.0 * cutoff * sinc * window;
            acc += std::complex<double>(x[static_cast<size_t>(idx)].real(), x[static_cast<size_t>(idx)].imag()) * w;
            wsum += w;
        }
        if (std::abs(wsum) > 1e-9) acc /= wsum;
        out.push_back({static_cast<float>(acc.real()), static_cast<float>(acc.imag())});
    }
    return out;
}

void removeMean(std::vector<float>& x)
{
    double sum = 0.0;
    size_t count = 0;
    for (float v : x) {
        if (std::isfinite(v)) {
            sum += v;
            ++count;
        }
    }
    if (count == 0) return;
    const float mean = static_cast<float>(sum / static_cast<double>(count));
    for (float& v : x) v = std::isfinite(v) ? v - mean : 0.0f;
}

void limitComplexEnvelope(std::vector<std::complex<float>>& x)
{
    for (auto& v : x) {
        const float mag = std::abs(v);
        if (mag > 1e-8f && std::isfinite(mag)) {
            v /= mag;
        } else {
            v = {};
        }
    }
}

struct FmDiscriminatorBlock {
    std::vector<float> hz;
    double sampleRate = 0.0;
    double meanHz = 0.0;
};

struct ChannelizedIqBlock {
    std::vector<std::complex<float>> samples;
    double sampleRate = 0.0;
};

ChannelizedIqBlock channelizeP25Iq(const std::vector<std::complex<float>>& iq,
                                   double sampleRate,
                                   double centerFreqHz,
                                   double targetFreqHz,
                                   const P25LiveDecoderConfig& config)
{
    ChannelizedIqBlock out;
    if (iq.size() < 2 || !std::isfinite(sampleRate) || sampleRate <= 0.0) return out;

    const double offsetHz = targetFreqHz - centerFreqHz;
    if (std::abs(offsetHz) > sampleRate * 0.55) return out;

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

    std::vector<std::complex<float>> mixed;
    mixed.reserve(iq.size());
    const double phaseStep = -2.0 * kPi * offsetHz / sampleRate;
    double phase = 0.0;
    for (size_t i = 0; i < iq.size(); ++i) {
        if (i != 0) {
            phase += phaseStep;
            if (phase > kPi || phase < -kPi) {
                phase = std::remainder(phase, 2.0 * kPi);
            }
        }
        const std::complex<double> osc(std::cos(phase), std::sin(phase));
        const std::complex<double> rawSample(static_cast<double>(iq[i].real()), static_cast<double>(iq[i].imag()));
        const auto bb = rawSample * osc;
        mixed.emplace_back(static_cast<float>(bb.real()), static_cast<float>(bb.imag()));
    }

    if (mixed.size() < 2) return out;
    if (decim > 1) {
        const double antiAliasCutoffHz = std::clamp(intermediateRate * 0.42,
                                                    config.channelBandwidthHz * 2.5,
                                                    sampleRate * 0.45);
        const double antiAliasTransitionHz = std::clamp(intermediateRate * 0.18,
                                                        config.channelBandwidthHz,
                                                        sampleRate * 0.20);
        mixed = applyFirSame(mixed, designLowpassTaps(sampleRate, antiAliasCutoffHz, antiAliasTransitionHz, 241));
    }

    std::vector<std::complex<float>> channel;
    channel.reserve(mixed.size() / static_cast<size_t>(decim) + 1);
    for (size_t i = 0; i < mixed.size(); i += static_cast<size_t>(decim)) {
        channel.push_back(mixed[i]);
    }

    if (channel.size() < 2) return out;
    const double channelCutoffHz = std::clamp(config.channelBandwidthHz * 0.58, config.symbolRate * 1.15,
                                             std::min(intermediateRate * 0.42, outputRate * 0.45));
    const double channelTransitionHz = std::clamp(config.channelBandwidthHz * 0.25, 1800.0, 6000.0);
    const auto channelTaps = designLowpassTaps(intermediateRate, channelCutoffHz, channelTransitionHz, 161);
    channel = applyFirSame(channel, channelTaps);

    double finalRate = intermediateRate;
    if (std::abs(intermediateRate - outputRate) > outputRate * 0.002) {
        channel = resampleWindowedSinc(channel, intermediateRate, outputRate);
        finalRate = outputRate;
    }
    out.sampleRate = finalRate;
    if (channel.size() < 2) return out;

    out.samples = std::move(channel);
    return out;
}

FmDiscriminatorBlock fmDiscriminatorFromChannel(std::vector<std::complex<float>> channel,
                                                double sampleRate)
{
    FmDiscriminatorBlock out;
    out.sampleRate = sampleRate;
    if (channel.size() < 2 || !std::isfinite(sampleRate) || sampleRate <= 0.0) return out;

    limitComplexEnvelope(channel);
    out.hz.reserve(channel.size() - 1);
    std::complex<float> prev = channel.front();
    double sumHz = 0.0;
    for (size_t i = 1; i < channel.size(); ++i) {
        const std::complex<float> cur = channel[i];
        const std::complex<float> d = cur * std::conj(prev);
        const float mag2 = std::norm(d);
        float hz = 0.0f;
        if (mag2 > 1e-18f) {
            hz = static_cast<float>(std::atan2(d.imag(), d.real()) * out.sampleRate / (2.0 * kPi));
        }
        out.hz.push_back(hz);
        sumHz += hz;
        prev = cur;
    }
    if (!out.hz.empty()) out.meanHz = sumHz / static_cast<double>(out.hz.size());
    removeMean(out.hz);
    return out;
}

FmDiscriminatorBlock fmDiscriminatorFromIq(const std::vector<std::complex<float>>& iq,
                                           double sampleRate,
                                           double centerFreqHz,
                                           double targetFreqHz,
                                           const P25LiveDecoderConfig& config)
{
    auto channel = channelizeP25Iq(iq, sampleRate, centerFreqHz, targetFreqHz, config);
    return fmDiscriminatorFromChannel(std::move(channel.samples), channel.sampleRate);
}

struct SymbolRecovery {
    std::vector<double> symbolsHz;
    double confidence = 0.0;
    double recoveredSampleRate = 0.0;
    double averageTimingError = 0.0;
    std::string path = "C4FM";
};

struct ComplexSymbolRecovery {
    std::vector<std::complex<double>> symbols;
    double confidence = 0.0;
    double recoveredSampleRate = 0.0;
    double averageTimingError = 0.0;
};

std::vector<float> prepareC4fmDiscriminatorForSymbols(const std::vector<float>& discriminatorHz,
                                                      double sampleRate,
                                                      const P25LiveDecoderConfig& config)
{
    if (discriminatorHz.empty() || !std::isfinite(sampleRate) || sampleRate <= config.symbolRate) {
        return {};
    }

    std::vector<float> centered = discriminatorHz;
    removeMean(centered);

    const double filterCutoffHz = std::clamp(config.symbolRate * 0.78, 2600.0, sampleRate * 0.42);
    const double filterTransitionHz = std::clamp(config.symbolRate * 0.45, 1200.0, sampleRate * 0.2);
    centered = applyFirSame(centered, designLowpassTaps(sampleRate, filterCutoffHz, filterTransitionHz, 101));
    removeMean(centered);
    return centered;
}

double c4fmSymbolConfidence(const std::vector<double>& symbolsHz, double timingRms)
{
    if (symbolsHz.empty()) return 0.0;

    std::vector<double> absVals;
    absVals.reserve(symbolsHz.size());
    for (double v : symbolsHz) absVals.push_back(std::abs(v));
    const double p95 = percentile(absVals, 0.95);
    const double p50 = percentile(absVals, 0.50);
    if (p95 <= 1e-9) return 0.0;

    const double eyeMetric = (p95 - p50 * 0.25) / std::max(p95, 1.0);
    return std::clamp(eyeMetric * (1.0 - std::min(0.45, timingRms * 0.08)), 0.0, 1.0);
}

SymbolRecovery recoverSymbolsFromPreparedC4fm(const std::vector<float>& centered,
                                              double sampleRate,
                                              const P25LiveDecoderConfig& config,
                                              P25BlockTimingState* streamState = nullptr)
{
    SymbolRecovery out;
    out.path = "C4FM-Gardner";
    out.recoveredSampleRate = config.symbolRate;
    if (centered.empty() || !std::isfinite(sampleRate) || sampleRate <= config.symbolRate) {
        return out;
    }

    const double sps = sampleRate / config.symbolRate;
    const int phaseSteps = static_cast<int>(std::clamp(std::ceil(sps * 2.0), 8.0, 96.0));
    double bestPhase = sps * 0.5;
    double bestMetric = -std::numeric_limits<double>::infinity();

    for (int p = 0; p < phaseSteps; ++p) {
        const double phase = (static_cast<double>(p) + 0.5) * sps / static_cast<double>(phaseSteps);
        std::vector<double> absVals;
        absVals.reserve(static_cast<size_t>(std::max(1.0, centered.size() / sps)));
        for (double t = phase; t < static_cast<double>(centered.size() - 1); t += sps) {
            absVals.push_back(std::abs(sampleLinear(centered, t)));
            if (absVals.size() >= 1200) break;
        }
        if (absVals.size() < 16) continue;
        const double p85 = percentile(absVals, 0.85);
        const double p35 = percentile(absVals, 0.35);
        const double metric = p85 + 0.25 * (p85 - p35);
        if (metric > bestMetric) {
            bestMetric = metric;
            bestPhase = phase;
        }
    }

    std::vector<double> levelProbe;
    levelProbe.reserve(static_cast<size_t>(std::max(1.0, centered.size() / sps)));
    for (double t = bestPhase; t < static_cast<double>(centered.size() - 1); t += sps) {
        levelProbe.push_back(std::abs(sampleLinear(centered, t)));
        if (levelProbe.size() >= 1800) break;
    }
    const double levelScale = std::max(percentile(levelProbe, 0.95), config.c4fmInnerDeviationHz * 0.25);

    SecondOrderTimingLoop timingLoop(sps, config.symbolRate, 0.010, 0.707);
    double t = bestPhase;
    if (streamState && streamState->c4fmValid &&
        std::abs(streamState->c4fmSampleRate - sampleRate) <= sampleRate * 0.02 &&
        streamState->c4fmOmega > 0.0) {
        timingLoop.omega = std::clamp(streamState->c4fmOmega, timingLoop.omegaMin, timingLoop.omegaMax);
        t = std::clamp(streamState->c4fmMu, 0.0, std::max(0.0, timingLoop.omega - 1.0));
    }

    std::vector<double> timingErrors;
    timingErrors.reserve(static_cast<size_t>(std::max(1.0, centered.size() / sps)));
    while (t < static_cast<double>(centered.size() - 1)) {
        const double symbol = sampleLinear(centered, t);
        out.symbolsHz.push_back(symbol);

        const double early = sampleLinear(centered, t - timingLoop.omega * 0.5);
        const double late = sampleLinear(centered, t + timingLoop.omega * 0.5);
        double err = ((early - late) * symbol) / std::max(levelScale * levelScale, 1.0);
        err = std::clamp(err, -1.0, 1.0);
        timingErrors.push_back(err);

        t += timingLoop.advance(err);
    }
    if (out.symbolsHz.empty()) return out;

    double timingRms = 0.0;
    if (!timingErrors.empty()) {
        for (double e : timingErrors) timingRms += e * e;
        timingRms = std::sqrt(timingRms / static_cast<double>(timingErrors.size()));
        out.averageTimingError = timingRms;
    }
    out.confidence = c4fmSymbolConfidence(out.symbolsHz, timingRms);
    if (streamState && !out.symbolsHz.empty()) {
        streamState->c4fmValid = true;
        streamState->c4fmSampleRate = sampleRate;
        streamState->c4fmOmega = timingLoop.omega;
        streamState->c4fmMu = std::fmod(std::max(t, 0.0), std::max(timingLoop.omega, 1e-6));
    }
    return out;
}

SymbolRecovery recoverFixedPhaseC4fmSymbols(const std::vector<float>& centered,
                                            double sampleRate,
                                            const P25LiveDecoderConfig& config,
                                            double phaseFraction)
{
    SymbolRecovery out;
    out.path = "C4FM-fixed";
    out.recoveredSampleRate = config.symbolRate;
    if (centered.empty() || !std::isfinite(sampleRate) || sampleRate <= config.symbolRate) {
        return out;
    }

    const double sps = sampleRate / config.symbolRate;
    const double phase = std::clamp(phaseFraction, 0.0, 0.999) * sps;
    out.symbolsHz.reserve(static_cast<size_t>(std::max(1.0, centered.size() / sps)));
    for (double t = phase; t < static_cast<double>(centered.size() - 1); t += sps) {
        out.symbolsHz.push_back(sampleLinear(centered, t));
    }
    out.confidence = c4fmSymbolConfidence(out.symbolsHz, 0.0);
    return out;
}

SymbolRecovery recoverSymbols(const std::vector<float>& discriminatorHz,
                              double sampleRate,
                              const P25LiveDecoderConfig& config)
{
    return recoverSymbolsFromPreparedC4fm(
        prepareC4fmDiscriminatorForSymbols(discriminatorHz, sampleRate, config),
        sampleRate,
        config);
}

std::vector<SymbolRecovery> recoverC4fmSymbolCandidates(const std::vector<float>& discriminatorHz,
                                                        double sampleRate,
                                                        const P25LiveDecoderConfig& config,
                                                        P25BlockTimingState* streamState = nullptr)
{
    std::vector<SymbolRecovery> candidates;
    std::vector<float> block = discriminatorHz;
    const size_t tailSamples = static_cast<size_t>(std::ceil(std::max(2.0, sampleRate / std::max(config.symbolRate, 1.0)) * 4.0));
    if (streamState && streamState->c4fmValid &&
        std::abs(streamState->c4fmSampleRate - sampleRate) <= sampleRate * 0.02) {
        appendTailPrefix(block, streamState->c4fmTail, tailSamples);
    }
    if (streamState) storeTail(streamState->c4fmTail, discriminatorHz, tailSamples);
    auto centered = prepareC4fmDiscriminatorForSymbols(block, sampleRate, config);
    if (centered.empty()) return candidates;

    auto gardner = recoverSymbolsFromPreparedC4fm(centered, sampleRate, config, streamState);
    if (!gardner.symbolsHz.empty()) candidates.push_back(std::move(gardner));
    if (!config.enableC4fmFixedPhaseSearch || config.maxC4fmFixedPhaseCandidates == 0) return candidates;

    const double sps = sampleRate / config.symbolRate;
    const int phaseSteps = static_cast<int>(std::clamp(
        std::min<double>(std::ceil(sps), static_cast<double>(config.maxC4fmFixedPhaseCandidates)),
        1.0,
        16.0));
    for (int p = 0; p < phaseSteps; ++p) {
        const double phaseFraction = (static_cast<double>(p) + 0.5) / static_cast<double>(phaseSteps);
        auto fixed = recoverFixedPhaseC4fmSymbols(centered, sampleRate, config, phaseFraction);
        if (fixed.symbolsHz.size() >= 16) candidates.push_back(std::move(fixed));
    }
    return candidates;
}

void removeMean(std::vector<std::complex<float>>& x)
{
    std::complex<double> sum(0.0, 0.0);
    size_t count = 0;
    for (const auto& v : x) {
        if (std::isfinite(v.real()) && std::isfinite(v.imag())) {
            sum += std::complex<double>(v.real(), v.imag());
            ++count;
        }
    }
    if (count == 0) return;
    const auto mean = sum / static_cast<double>(count);
    for (auto& v : x) {
        if (std::isfinite(v.real()) && std::isfinite(v.imag())) {
            v -= std::complex<float>(static_cast<float>(mean.real()), static_cast<float>(mean.imag()));
        } else {
            v = {};
        }
    }
}

ComplexSymbolRecovery recoverComplexSymbols(const std::vector<std::complex<float>>& baseband,
                                            double sampleRate,
                                            const P25LiveDecoderConfig& config,
                                            double initialPhase,
                                            P25BlockTimingState* streamState = nullptr)
{
    ComplexSymbolRecovery out;
    out.recoveredSampleRate = config.symbolRate;
    if (baseband.empty() || !std::isfinite(sampleRate) || sampleRate <= config.symbolRate) return out;

    std::vector<std::complex<float>> filtered = baseband;
    const size_t tailSamples = static_cast<size_t>(std::ceil(std::max(2.0, sampleRate / std::max(config.symbolRate, 1.0)) * 4.0));
    if (streamState && streamState->cqpskValid &&
        std::abs(streamState->cqpskSampleRate - sampleRate) <= sampleRate * 0.02) {
        appendTailPrefix(filtered, streamState->cqpskTail, tailSamples);
    }
    if (streamState) storeTail(streamState->cqpskTail, baseband, tailSamples);
    removeMean(filtered);
    const double filterCutoffHz = std::clamp(config.symbolRate * 0.82, 3000.0, sampleRate * 0.42);
    const double filterTransitionHz = std::clamp(config.symbolRate * 0.50, 1400.0, sampleRate * 0.22);
    filtered = applyFirSame(filtered, designLowpassTaps(sampleRate, filterCutoffHz, filterTransitionHz, 121));
    removeMean(filtered);

    const double sps = sampleRate / config.symbolRate;
    double level = 0.0;
    size_t levelCount = 0;
    for (double t = std::clamp(initialPhase, 0.0, std::max(0.0, sps - 1.0));
         t < static_cast<double>(filtered.size() - 1); t += sps) {
        level += std::abs(sampleLinearComplex(filtered, t));
        ++levelCount;
        if (levelCount >= 1600) break;
    }
    level = levelCount > 0 ? level / static_cast<double>(levelCount) : 1.0;
    level = std::max(level, 1e-3);

    SecondOrderTimingLoop timingLoop(sps, config.symbolRate, 0.012, 0.707);
    double t = std::clamp(initialPhase, 0.0, std::max(0.0, sps - 1.0));
    if (streamState && streamState->cqpskValid &&
        std::abs(streamState->cqpskSampleRate - sampleRate) <= sampleRate * 0.02 &&
        streamState->cqpskOmega > 0.0) {
        timingLoop.omega = std::clamp(streamState->cqpskOmega, timingLoop.omegaMin, timingLoop.omegaMax);
        t = std::clamp(streamState->cqpskMu, 0.0, std::max(0.0, timingLoop.omega - 1.0));
    }
    double timingEnergy = 0.0;
    size_t timingCount = 0;

    while (t < static_cast<double>(filtered.size() - 1)) {
        const auto s = sampleLinearComplex(filtered, t);
        out.symbols.emplace_back(static_cast<double>(s.real()), static_cast<double>(s.imag()));

        const auto early = sampleLinearComplex(filtered, t - timingLoop.omega * 0.5);
        const auto late = sampleLinearComplex(filtered, t + timingLoop.omega * 0.5);
        const auto errComplex = (early - late) * std::conj(s);
        double err = static_cast<double>(errComplex.real()) / std::max(level * level, 1e-6);
        err = std::clamp(err, -1.0, 1.0);
        timingEnergy += err * err;
        ++timingCount;

        t += timingLoop.advance(err);
    }

    if (out.symbols.empty()) return out;
    out.averageTimingError = timingCount > 0 ? std::sqrt(timingEnergy / static_cast<double>(timingCount)) : 0.0;
    double avgMag = 0.0;
    double avgMag2 = 0.0;
    for (const auto& s : out.symbols) {
        const double mag = std::abs(s);
        avgMag += mag;
        avgMag2 += mag * mag;
    }
    avgMag /= static_cast<double>(out.symbols.size());
    avgMag2 /= static_cast<double>(out.symbols.size());
    const double magVar = std::max(0.0, avgMag2 - avgMag * avgMag);
    const double timingRms = timingCount > 0 ? std::sqrt(timingEnergy / static_cast<double>(timingCount)) : 1.0;
    out.confidence = std::clamp((avgMag / std::max(avgMag + std::sqrt(magVar), 1e-6)) *
                                (1.0 - std::min(0.45, timingRms * 0.08)),
                                0.0, 1.0);
    if (streamState && !out.symbols.empty()) {
        streamState->cqpskValid = true;
        streamState->cqpskSampleRate = sampleRate;
        streamState->cqpskOmega = timingLoop.omega;
        streamState->cqpskMu = std::fmod(std::max(t, 0.0), std::max(timingLoop.omega, 1e-6));
    }
    return out;
}

int sliceC4fmSymbol(double symbolHz, double scaleHz)
{
    const double scale = std::max(scaleHz, 1e-6);
    const double v = symbolHz / scale;
    if (v >= 2.0) return 0x1;  // 01, +3 nominal
    if (v >= 0.0) return 0x0;  // 00, +1 nominal
    if (v >= -2.0) return 0x2; // 10, -1 nominal
    return 0x3;                // 11, -3 nominal
}

int reverseDibitBitOrder(int dibit)
{
    const int d = dibit & 0x03;
    return ((d & 0x01) << 1) | ((d >> 1) & 0x01);
}

double c4fmNominalLevelForDibit(int dibit)
{
    switch (dibit & 0x03) {
        case 0x1: return 3.0;   // 01
        case 0x0: return 1.0;   // 00
        case 0x2: return -1.0;  // 10
        case 0x3: return -3.0;  // 11
        default: return 0.0;
    }
}

double logSumExpPair(double a, double b)
{
    const double m = std::max(a, b);
    if (!std::isfinite(m)) return m;
    return m + std::log(std::exp(a - m) + std::exp(b - m));
}

struct SoftDibitSequence {
    std::vector<int> dibits;
    double quality = 0.0;
    double meanAbsLlr = 0.0;
    double minAbsLlr = 0.0;
    size_t lowConfidenceSymbols = 0;
};

void finalizeSoftDibitStats(SoftDibitSequence& out,
                            const std::vector<std::array<double, 2>>& llrs,
                            double lowConfidenceThreshold = 1.0)
{
    if (llrs.empty()) {
        out.minAbsLlr = 0.0;
        return;
    }

    double sum = 0.0;
    double minAbs = std::numeric_limits<double>::infinity();
    size_t count = 0;
    for (const auto& pair : llrs) {
        const double a0 = std::abs(pair[0]);
        const double a1 = std::abs(pair[1]);
        if (!std::isfinite(a0) || !std::isfinite(a1)) continue;
        const double symbolMin = std::min(a0, a1);
        minAbs = std::min(minAbs, symbolMin);
        sum += a0 + a1;
        count += 2;
        if (symbolMin < lowConfidenceThreshold) ++out.lowConfidenceSymbols;
    }

    out.meanAbsLlr = count > 0 ? sum / static_cast<double>(count) : 0.0;
    out.minAbsLlr = std::isfinite(minAbs) ? minAbs : 0.0;
    out.quality = std::clamp(out.meanAbsLlr / (out.meanAbsLlr + 4.0), 0.0, 1.0);
}

void stampSoftDibitStats(P25LiveDecodeResult& result, const SoftDibitSequence& soft)
{
    result.stats.softDecisionSymbols = soft.dibits.size();
    result.stats.softDecisionQuality = soft.quality;
    result.stats.softBitLlrMean = soft.meanAbsLlr;
    result.stats.softBitLlrMinimum = soft.minAbsLlr;
    result.stats.softLowConfidenceSymbols = soft.lowConfidenceSymbols;
}

std::vector<int> symbolsToDibits(const std::vector<double>& symbolsHz,
                                  const P25LiveDecoderConfig& config,
                                  bool invertDeviation,
                                  bool reverseBitOrder,
                                  double scaleMultiplier = 1.0,
                                  double scaleOverrideHz = 0.0)
{
    std::vector<double> absVals;
    absVals.reserve(symbolsHz.size());
    for (double v : symbolsHz) absVals.push_back(std::abs(v));
    double high = percentile(absVals, 0.95);
    double scale = scaleOverrideHz > 1e-6 ? scaleOverrideHz : (high > 1e-6 ? high / 3.0 : config.c4fmInnerDeviationHz);
    scale *= std::clamp(scaleMultiplier, 0.45, 1.8);
    scale = std::max(scale, config.c4fmInnerDeviationHz * 0.15);

    std::vector<int> dibits;
    dibits.reserve(symbolsHz.size());
    for (double v : symbolsHz) {
        int dibit = sliceC4fmSymbol(invertDeviation ? -v : v, scale);
        if (reverseBitOrder) dibit = reverseDibitBitOrder(dibit);
        dibits.push_back(dibit);
    }
    return dibits;
}

SoftDibitSequence symbolsToSoftDibits(const std::vector<double>& symbolsHz,
                                      const P25LiveDecoderConfig& config,
                                      bool invertDeviation,
                                      bool reverseBitOrder,
                                      double scaleMultiplier = 1.0,
                                      double scaleOverrideHz = 0.0)
{
    SoftDibitSequence out;
    std::vector<double> absVals;
    absVals.reserve(symbolsHz.size());
    for (double v : symbolsHz) absVals.push_back(std::abs(v));
    double high = percentile(absVals, 0.95);
    double scale = scaleOverrideHz > 1e-6 ? scaleOverrideHz : (high > 1e-6 ? high / 3.0 : config.c4fmInnerDeviationHz);
    scale *= std::clamp(scaleMultiplier, 0.45, 1.8);
    scale = std::max(scale, config.c4fmInnerDeviationHz * 0.15);

    std::vector<double> residuals;
    residuals.reserve(symbolsHz.size());
    out.dibits.reserve(symbolsHz.size());
    for (double v : symbolsHz) {
        const double observed = invertDeviation ? -v : v;
        const int rawDibit = sliceC4fmSymbol(observed, scale);
        const int dibit = reverseBitOrder ? reverseDibitBitOrder(rawDibit) : rawDibit;
        const double ideal = c4fmNominalLevelForDibit(rawDibit) * scale;
        residuals.push_back((observed - ideal) * (observed - ideal));
        out.dibits.push_back(dibit & 0x03);
    }

    double sigma2 = 0.0;
    if (!residuals.empty()) {
        sigma2 = percentile(residuals, 0.65);
    }
    sigma2 = std::max(sigma2, (scale * 0.18) * (scale * 0.18));

    std::vector<std::array<double, 2>> llrs;
    llrs.reserve(symbolsHz.size());
    for (double v : symbolsHz) {
        const double observed = invertDeviation ? -v : v;
        std::array<double, 4> metric{};
        for (int outDibit = 0; outDibit < 4; ++outDibit) {
            const int rawDibit = reverseBitOrder ? reverseDibitBitOrder(outDibit) : outDibit;
            const double ideal = c4fmNominalLevelForDibit(rawDibit) * scale;
            const double dist2 = (observed - ideal) * (observed - ideal);
            metric[static_cast<size_t>(outDibit)] = -dist2 / (2.0 * sigma2);
        }

        std::array<double, 2> bits{};
        for (int bit = 0; bit < 2; ++bit) {
            const int shift = bit == 0 ? 1 : 0;
            double ones[2]{-std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()};
            double zeros[2]{-std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()};
            int oi = 0;
            int zi = 0;
            for (int d = 0; d < 4; ++d) {
                if ((d >> shift) & 1) ones[oi++] = metric[static_cast<size_t>(d)];
                else zeros[zi++] = metric[static_cast<size_t>(d)];
            }
            bits[static_cast<size_t>(bit)] = logSumExpPair(ones[0], ones[1]) - logSumExpPair(zeros[0], zeros[1]);
        }
        llrs.push_back(bits);
    }
    finalizeSoftDibitStats(out, llrs);
    return out;
}

double wrapPhase(double p)
{
    p = std::remainder(p, 2.0 * kPi);
    if (p <= -kPi) p += 2.0 * kPi;
    if (p > kPi) p -= 2.0 * kPi;
    return p;
}

int phaseToQuadrantBin(double phase, double rotation)
{
    const double shifted = wrapPhase(phase + rotation);
    int bin = static_cast<int>(std::floor((shifted + kPi + kPi * 0.25) / (kPi * 0.5)));
    return bin & 0x03;
}

double quadrantCenterForBin(int bin)
{
    return wrapPhase(-kPi + static_cast<double>(bin & 0x03) * (kPi * 0.5));
}

const std::array<std::array<int, 4>, 24>& cqpskDibitPermutations()
{
    static const auto perms = [] {
        std::array<std::array<int, 4>, 24> out{};
        std::array<int, 4> p{0, 1, 2, 3};
        size_t i = 0;
        do {
            out[i++] = p;
        } while (std::next_permutation(p.begin(), p.end()) && i < out.size());
        return out;
    }();
    return perms;
}

std::vector<int> cqpskSymbolsToDibits(const std::vector<std::complex<double>>& symbols,
                                      bool differential,
                                      bool conjugate,
                                      double rotation,
                                      const std::array<int, 4>& permutation)
{
    std::vector<int> dibits;
    if (symbols.size() < 2) return dibits;
    dibits.reserve(symbols.size());

    const size_t start = differential ? 1 : 0;
    for (size_t i = start; i < symbols.size(); ++i) {
        std::complex<double> z = differential ? symbols[i] * std::conj(symbols[i - 1]) : symbols[i];
        if (conjugate) z = std::conj(z);
        if (std::norm(z) < 1e-10 || !std::isfinite(z.real()) || !std::isfinite(z.imag())) {
            dibits.push_back(0);
            continue;
        }
        const int bin = phaseToQuadrantBin(std::atan2(z.imag(), z.real()), rotation);
        dibits.push_back(permutation[static_cast<size_t>(bin)] & 0x03);
    }
    return dibits;
}

SoftDibitSequence cqpskSymbolsToSoftDibits(const std::vector<std::complex<double>>& symbols,
                                           bool differential,
                                           bool conjugate,
                                           double rotation,
                                           const std::array<int, 4>& permutation)
{
    SoftDibitSequence out;
    if (symbols.size() < 2) return out;

    struct ObservedSymbol {
        std::complex<double> z;
        bool valid = false;
    };
    std::vector<ObservedSymbol> observed;
    observed.reserve(symbols.size());
    double level = 0.0;
    size_t levelCount = 0;

    const size_t start = differential ? 1 : 0;
    for (size_t i = start; i < symbols.size(); ++i) {
        std::complex<double> z = differential ? symbols[i] * std::conj(symbols[i - 1]) : symbols[i];
        if (conjugate) z = std::conj(z);
        const bool valid = std::norm(z) >= 1e-10 && std::isfinite(z.real()) && std::isfinite(z.imag());
        observed.push_back({z, valid});
        if (valid) {
            level += std::abs(z);
            ++levelCount;
        }
    }
    level = levelCount > 0 ? level / static_cast<double>(levelCount) : 1.0;
    level = std::max(level, 1e-4);

    auto idealForBin = [&](int bin) {
        return std::polar(level, quadrantCenterForBin(bin) - rotation);
    };

    std::vector<double> residuals;
    residuals.reserve(observed.size());
    out.dibits.reserve(observed.size());
    for (const auto& sym : observed) {
        if (!sym.valid) {
            out.dibits.push_back(0);
            residuals.push_back(level * level);
            continue;
        }
        const int bin = phaseToQuadrantBin(std::atan2(sym.z.imag(), sym.z.real()), rotation);
        out.dibits.push_back(permutation[static_cast<size_t>(bin)] & 0x03);
        residuals.push_back(std::norm(sym.z - idealForBin(bin)));
    }

    double sigma2 = 0.0;
    if (!residuals.empty()) sigma2 = percentile(residuals, 0.65);
    sigma2 = std::max(sigma2, (level * 0.18) * (level * 0.18));

    std::vector<std::array<double, 2>> llrs;
    llrs.reserve(observed.size());
    for (const auto& sym : observed) {
        if (!sym.valid) {
            llrs.push_back({0.0, 0.0});
            continue;
        }
        std::array<double, 4> metric{};
        for (int bin = 0; bin < 4; ++bin) {
            const double dist2 = std::norm(sym.z - idealForBin(bin));
            const int dibit = permutation[static_cast<size_t>(bin)] & 0x03;
            metric[static_cast<size_t>(dibit)] = -dist2 / (2.0 * sigma2);
        }

        std::array<double, 2> bits{};
        for (int bit = 0; bit < 2; ++bit) {
            const int shift = bit == 0 ? 1 : 0;
            double ones[2]{-std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()};
            double zeros[2]{-std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()};
            int oi = 0;
            int zi = 0;
            for (int d = 0; d < 4; ++d) {
                if ((d >> shift) & 1) ones[oi++] = metric[static_cast<size_t>(d)];
                else zeros[zi++] = metric[static_cast<size_t>(d)];
            }
            bits[static_cast<size_t>(bit)] = logSumExpPair(ones[0], ones[1]) - logSumExpPair(zeros[0], zeros[1]);
        }
        llrs.push_back(bits);
    }
    finalizeSoftDibitStats(out, llrs);
    return out;
}

struct CqpskCandidateParams {
    bool differential = true;
    bool conjugate = false;
    double rotation = 0.0;
    double fineRotation = 0.0;
    std::array<int, 4> permutation{0, 1, 2, 3};
    double symbolPhaseFraction = 0.5;
    bool fineCorrectionApplied = false;
    double residualCarrierHz = 0.0;
    double phaseErrorRmsRad = 0.0;
    size_t fineCorrectionSymbols = 0;
};

struct CqpskFineCorrection {
    bool valid = false;
    double rotationAdjustment = 0.0;
    double meanErrorRad = 0.0;
    double phaseErrorRmsRad = 0.0;
    double residualCarrierHz = 0.0;
    size_t symbols = 0;
};

CqpskFineCorrection estimateCqpskFineCorrection(const std::vector<std::complex<double>>& symbols,
                                                bool differential,
                                                bool conjugate,
                                                double rotation,
                                                double symbolRate)
{
    CqpskFineCorrection out;
    if (symbols.size() < 16 || !std::isfinite(symbolRate) || symbolRate <= 0.0) return out;

    double sumWeight = 0.0;
    double sumError = 0.0;
    double sumError2 = 0.0;
    const size_t start = differential ? 1 : 0;
    for (size_t i = start; i < symbols.size(); ++i) {
        std::complex<double> z = differential ? symbols[i] * std::conj(symbols[i - 1]) : symbols[i];
        if (conjugate) z = std::conj(z);
        const double mag = std::abs(z);
        if (mag < 1e-5 || !std::isfinite(z.real()) || !std::isfinite(z.imag())) continue;

        const double phase = std::atan2(z.imag(), z.real());
        const int bin = phaseToQuadrantBin(phase, rotation);
        const double shifted = wrapPhase(phase + rotation);
        const double error = wrapPhase(shifted - quadrantCenterForBin(bin));
        if (!std::isfinite(error)) continue;

        const double weight = std::clamp(mag, 0.05, 5.0);
        sumWeight += weight;
        sumError += weight * error;
        sumError2 += weight * error * error;
        ++out.symbols;
    }

    if (out.symbols < 16 || sumWeight <= 1e-9) return out;
    out.meanErrorRad = sumError / sumWeight;
    const double meanSquare = sumError2 / sumWeight;
    out.phaseErrorRmsRad = std::sqrt(std::max(0.0, meanSquare - out.meanErrorRad * out.meanErrorRad));
    if (!std::isfinite(out.meanErrorRad) || !std::isfinite(out.phaseErrorRmsRad)) return {};

    out.rotationAdjustment = -out.meanErrorRad;
    out.residualCarrierHz = differential ? out.meanErrorRad * symbolRate / (2.0 * kPi) : 0.0;
    out.valid = std::abs(out.meanErrorRad) <= 0.60 && out.phaseErrorRmsRad <= 0.50;
    return out;
}

bool isCqpskPath(const std::string& path)
{
    return path.find("CQPSK") != std::string::npos;
}

int liveResultTrustScore(const P25LiveDecodeResult& r)
{
    int score = 0;
    for (const auto& block : r.rawTsbkBlocks) {
        if (block.fecDecoded && block.crcValid) score += 200;
        else if (block.fecDecoded) score += 8;
    }
    for (const auto& nid : r.nids) {
        if (nid.fecValidated) score += 60;
    }
    for (const auto& frame : r.imbeFrames) {
        if (frame.valid) score += 40;
    }
    score += static_cast<int>(r.stats.phase2MacCrcValid) * 250;
    if (r.stats.phase2EssKnown) score += 220;
    const bool phase2MetadataTrusted = r.stats.phase2MacCrcValid > 0 || r.stats.phase2EssKnown;
    if (r.stats.phase2MaskPhaseKnown) score += phase2MetadataTrusted ? 160 : 20;
    if (phase2MetadataTrusted) {
        if (r.stats.phase2SuperframeBursts >= 6) score += 30;
        if (r.stats.phase2MaskedBursts >= 6) score += 20;
        score += static_cast<int>(std::min<size_t>(r.stats.phase2VoiceCodewords, 24));
    } else {
        // Superframe/mask/VCW counts are useful acquisition telemetry, but they
        // are not a trusted Phase 2 decode without MAC CRC or ESS confirmation.
        score += static_cast<int>(std::min<size_t>(r.stats.phase2VoiceCodewords, 6));
    }
    if (r.stats.bestFrameSyncBitErrors >= 0) score += std::max(0, 12 - r.stats.bestFrameSyncBitErrors);
    if (r.stats.bestPhase2SyncErrors >= 0) score += std::max(0, 8 - r.stats.bestPhase2SyncErrors);
    if (r.stats.softDecisionSymbols > 0) {
        score += static_cast<int>(std::clamp(r.stats.softDecisionQuality, 0.0, 1.0) * 12.0);
    }
    return score;
}

int liveResultHardEvidenceScore(const P25LiveDecodeResult& r)
{
    int score = 0;
    for (const auto& block : r.rawTsbkBlocks) {
        if (block.fecDecoded && block.crcValid) score += 40;
    }
    for (const auto& nid : r.nids) {
        if (nid.fecValidated) score += 12;
    }
    for (const auto& frame : r.imbeFrames) {
        if (frame.valid) score += 16;
    }
    score += static_cast<int>(r.stats.phase2MacCrcValid) * 60;
    if (r.stats.phase2EssKnown) score += 50;
    if (r.stats.phase2MaskPhaseKnown &&
        r.stats.phase2SuperframeBursts >= 6 &&
        r.stats.phase2MaskedBursts >= 6) {
        score += 10;
    }
    return score;
}

bool betterLiveResult(const P25LiveDecodeResult& a, const P25LiveDecodeResult& b)
{
    const auto validNids = [](const P25LiveDecodeResult& r) {
        return std::count_if(r.nids.begin(), r.nids.end(), [](const P25Nid& nid) {
            return nid.fecValidated;
        });
    };
    const auto trustedTsbks = [](const P25LiveDecodeResult& r) {
        return std::count_if(r.rawTsbkBlocks.begin(), r.rawTsbkBlocks.end(), [](const P25TsbkBlock& block) {
            return block.fecDecoded && block.crcValid;
        });
    };
    const auto aTrustedTsbks = trustedTsbks(a);
    const auto bTrustedTsbks = trustedTsbks(b);
    if (aTrustedTsbks != bTrustedTsbks) return aTrustedTsbks > bTrustedTsbks;

    const auto phase2VoiceCodewords = [](const P25LiveDecodeResult& r) {
        size_t out = 0;
        for (const auto& burst : r.phase2Bursts) out += burst.voiceCodewords.size();
        return out;
    };
    const auto phase2TelemetryScore = [&](const P25LiveDecodeResult& r) {
        int score = 0;
        // MAC/ESS evidence must dominate untrusted VCW counts.  Several wrong
        // CQPSK dibit permutations can still reproduce the repeated Phase-2 sync
        // pattern and produce voice-looking DUIDs, which caused the app to stop
        // searching at 12/12 p2sf/p2mask while sdrtrunk continued on a candidate
        // that actually yielded ACCH/MAC.
        score += static_cast<int>(r.stats.phase2MacCrcValid) * 1000;
        const size_t phase2FecMacCandidates = static_cast<size_t>(std::count_if(
            r.phase2MacPdus.begin(), r.phase2MacPdus.end(),
            [](const P25Phase2MacPdu& pdu) { return pdu.fecDecoded; }));
        // Non-CRC direct ACCH probes are useful diagnostics (p2mac=0/N), but
        // they are not sdrtrunk-like confidence evidence and must not steer the
        // CQPSK candidate arbiter.  Only RS/FEC-recovered candidates can break
        // ties before a valid MAC CRC or ESS appears.
        score += static_cast<int>(phase2FecMacCandidates) * 80;
        if (r.stats.phase2EssKnown) score += 900;
        score += static_cast<int>(phase2VoiceCodewords(r)) * 4;
        score += static_cast<int>(r.stats.phase2SuperframeBursts) * 2;
        score += static_cast<int>(r.stats.phase2MaskedBursts) * 2;
        score += static_cast<int>(r.stats.phase2IschDecoded) * 2;
        if (r.stats.phase2MaskPhaseKnown) score += 8;
        if (r.stats.bestPhase2SyncErrors >= 0) score += std::max(0, 8 - r.stats.bestPhase2SyncErrors);
        return score;
    };

    // sdrtrunk's Phase-2 traffic decoder is a dedicated Phase-2 pipeline: once
    // the traffic channel is allocated, SuperFrameFragment/TimeslotFactory
    // classify 320-bit TDMA timeslots and dispatch Voice2/Voice4 frames.  It
    // does not let a coincidental Phase-1 NID candidate outrank valid Phase-2
    // timeslot/voice telemetry on the traffic channel.  Our candidate arbiter
    // used to prefer NID count before Phase-2 telemetry unless MAC/ESS was
    // already CRC-trusted, which produced field logs with alternating
    // "decoded=4" and "NID not validated" even while p2vcw was present.  With
    // no trusted TSBK in the candidate, prefer clear Phase-2 traffic evidence
    // before Phase-1 NID evidence.
    if (aTrustedTsbks == 0 && bTrustedTsbks == 0) {
        const int aP2 = phase2TelemetryScore(a);
        const int bP2 = phase2TelemetryScore(b);
        if (aP2 != bP2 && std::max(aP2, bP2) >= 8) return aP2 > bP2;
    }

    const auto aValidNids = validNids(a);
    const auto bValidNids = validNids(b);
    if (aValidNids != bValidNids) return aValidNids > bValidNids;
    const auto phase2HardEvidence = [](const P25LiveDecodeResult& r) {
        int out = 0;
        out += static_cast<int>(r.stats.phase2MacCrcValid) * 8;
        if (r.stats.phase2EssKnown) out += 6;
        if (r.stats.phase2MaskPhaseKnown) out += 2;
        return out;
    };
    const int aPhase2Hard = phase2HardEvidence(a);
    const int bPhase2Hard = phase2HardEvidence(b);
    if (aPhase2Hard != bPhase2Hard) return aPhase2Hard > bPhase2Hard;
    const bool compareTrustedPhase2Telemetry = aPhase2Hard > 0 || bPhase2Hard > 0;
    if (compareTrustedPhase2Telemetry) {
        const auto aPhase2Voice = phase2VoiceCodewords(a);
        const auto bPhase2Voice = phase2VoiceCodewords(b);
        if (aPhase2Voice != bPhase2Voice) return aPhase2Voice > bPhase2Voice;
        if (a.phase2Bursts.size() != b.phase2Bursts.size()) return a.phase2Bursts.size() > b.phase2Bursts.size();
    }
    if (a.nids.size() != b.nids.size()) return a.nids.size() > b.nids.size();
    if (a.syncs.size() != b.syncs.size()) return a.syncs.size() > b.syncs.size();
    if (a.stats.bestFrameSyncBitErrors >= 0 && b.stats.bestFrameSyncBitErrors >= 0 &&
        a.stats.bestFrameSyncBitErrors != b.stats.bestFrameSyncBitErrors) {
        return a.stats.bestFrameSyncBitErrors < b.stats.bestFrameSyncBitErrors;
    }
    if (a.stats.bestNidBchDistance >= 0 && b.stats.bestNidBchDistance >= 0 &&
        a.stats.bestNidBchDistance != b.stats.bestNidBchDistance) {
        return a.stats.bestNidBchDistance < b.stats.bestNidBchDistance;
    }
    const int aTrust = liveResultTrustScore(a);
    const int bTrust = liveResultTrustScore(b);
    if (aTrust != bTrust) return aTrust > bTrust;
    const bool aCqpsk = isCqpskPath(a.stats.demodPath);
    const bool bCqpsk = isCqpskPath(b.stats.demodPath);
    if (aCqpsk != bCqpsk &&
        (aTrustedTsbks > 0 || aValidNids > 0) &&
        (bTrustedTsbks > 0 || bValidNids > 0)) {
        return aCqpsk;
    }
    if (std::abs(a.stats.softDecisionQuality - b.stats.softDecisionQuality) > 1e-6) {
        return a.stats.softDecisionQuality > b.stats.softDecisionQuality;
    }
    if (std::abs(a.stats.symbolConfidence - b.stats.symbolConfidence) > 1e-6) {
        return a.stats.symbolConfidence > b.stats.symbolConfidence;
    }
    return false;
}

bool hasCqpskHardLockEvidence(const P25LiveDecodeResult& r)
{
    const bool trustedTsbk = std::any_of(r.rawTsbkBlocks.begin(), r.rawTsbkBlocks.end(), [](const P25TsbkBlock& block) {
        return block.fecDecoded && block.crcValid;
    });
    if (trustedTsbk) return true;

    const bool trustedNid = std::any_of(r.nids.begin(), r.nids.end(), [](const P25Nid& nid) {
        return nid.fecValidated;
    });
    if (trustedNid) return true;

    if (r.stats.phase2MacCrcValid > 0 || r.stats.phase2EssKnown) return true;

    return std::any_of(r.imbeFrames.begin(), r.imbeFrames.end(), [](const P25ImbeFrame& frame) {
        return frame.valid;
    });
}

bool hasPhase2FastStopEvidence(const P25LiveDecodeResult& r)
{
    // Do not fast-stop candidate search on untrusted Phase-2 telemetry alone.
    // A wrong CQPSK permutation/mask phase can still yield 12/12 sync+mask and
    // voice-looking DUIDs while never producing MAC/ESS/audio.  sdrtrunk keeps
    // feeding its Phase-2 pipeline until TimeslotFactory/MAC/ESS confirms the
    // timeslot contents, so only hard Phase-2 metadata should stop our search.
    return r.stats.phase2MacCrcValid > 0 || r.stats.phase2EssKnown;
}

size_t phase2VoiceCodewordCount(const P25LiveDecodeResult& r)
{
    size_t out = 0;
    for (const auto& burst : r.phase2Bursts) out += burst.voiceCodewords.size();
    return out;
}

bool compiledVoiceBackendAvailable()
{
#ifdef HAVE_MBELIB
    return true;
#else
    return false;
#endif
}

uint64_t readBitsMsb(const std::vector<uint8_t>& bits, size_t startBit, size_t count, bool invert)
{
    if (count == 0 || count > 64 || startBit + count > bits.size()) return 0;
    uint64_t out = 0;
    for (size_t i = 0; i < count; ++i) {
        uint8_t bit = bits[startBit + i] ? 1u : 0u;
        if (invert) bit ^= 1u;
        out = (out << 1) | bit;
    }
    return out;
}

P25DataUnitId toDuid(uint8_t raw)
{
    switch (raw & 0x0f) {
        case 0x0: return P25DataUnitId::HDU;
        case 0x3: return P25DataUnitId::TDU;
        case 0x5: return P25DataUnitId::LDU1;
        case 0x7: return P25DataUnitId::TSDU;
        case 0xA: return P25DataUnitId::LDU2;
        case 0xC: return P25DataUnitId::PDU;
        case 0xF: return P25DataUnitId::TDULC;
        default: return P25DataUnitId::Unknown;
    }
}

constexpr std::array<uint16_t, 48> kP25BchGeneratorRows = {
    0b1110110001000111u,
    0b1001101001100100u,
    0b0100110100110010u,
    0b0010011010011001u,
    0b1111111100001011u,
    0b1001001111000010u,
    0b0100100111100001u,
    0b1100100010110111u,
    0b1000100000011100u,
    0b0100010000001110u,
    0b0010001000000111u,
    0b1111110101000100u,
    0b0111111010100010u,
    0b0011111101010001u,
    0b1111001111101111u,
    0b1001010110110000u,
    0b0100101011011000u,
    0b0010010101101100u,
    0b0001001010110110u,
    0b0000100101011011u,
    0b1110100011101010u,
    0b0111010001110101u,
    0b1101011001111101u,
    0b1000011101111001u,
    0b1010111111111011u,
    0b1011101110111010u,
    0b0101110111011101u,
    0b1100001010101001u,
    0b1000110100010011u,
    0b1010101011001110u,
    0b0101010101100111u,
    0b1100011011110100u,
    0b0110001101111010u,
    0b0011000110111101u,
    0b1111010010011001u,
    0b1001011000001011u,
    0b1010011101000010u,
    0b0101001110100001u,
    0b1100010110010111u,
    0b1000111010001100u,
    0b0100011101000110u,
    0b0010001110100011u,
    0b1111110110010110u,
    0b0111111011001011u,
    0b1101001100100010u,
    0b0110100110010001u,
    0b1101100010001111u,
    0b0000000000000011u,
};

uint64_t encodeP25Bch16(uint16_t data)
{
    uint64_t word = static_cast<uint64_t>(data) << 48;
    for (size_t i = 0; i < kP25BchGeneratorRows.size(); ++i) {
        const auto parity = static_cast<uint64_t>(
            std::popcount(static_cast<unsigned>(data & kP25BchGeneratorRows[i])) & 1u);
        word |= parity << (47 - i);
    }
    return word;
}

struct P25BchDecode {
    bool valid = false;
    uint16_t data = 0;
    uint64_t correctedWord = 0;
    int correctedErrors = 0;
};

const std::array<uint64_t, 65536>& p25BchCodewordTable()
{
    static const auto table = [] {
        std::array<uint64_t, 65536> t{};
        for (size_t i = 0; i < t.size(); ++i) {
            t[i] = encodeP25Bch16(static_cast<uint16_t>(i));
        }
        return t;
    }();
    return table;
}

P25BchDecode decodeP25BchNid(uint64_t raw)
{
    P25BchDecode out;
    const auto& table = p25BchCodewordTable();
    int bestDistance = 65;
    int secondDistance = 65;
    uint16_t bestData = 0;
    uint64_t bestWord = 0;

    for (size_t i = 0; i < table.size(); ++i) {
        const int distance = static_cast<int>(std::popcount(raw ^ table[i]));
        if (distance < bestDistance) {
            secondDistance = bestDistance;
            bestDistance = distance;
            bestData = static_cast<uint16_t>(i);
            bestWord = table[i];
        } else if (distance < secondDistance) {
            secondDistance = distance;
        }
    }

    out.data = bestData;
    out.correctedWord = bestWord;
    out.correctedErrors = bestDistance;
    if (bestDistance <= 11 && bestDistance < secondDistance) {
        out.valid = true;
    }
    return out;
}

int syncErrorsAt(const std::vector<uint8_t>& bits,
                 const std::array<uint8_t, P25LiveDecoder::FrameSyncBits>& sync,
                 size_t bitOffset,
                 bool inverted)
{
    int errors = 0;
    for (size_t i = 0; i < sync.size(); ++i) {
        const uint8_t expected = inverted ? static_cast<uint8_t>(sync[i] ^ 1u) : sync[i];
        if ((bits[bitOffset + i] ? 1u : 0u) != expected) ++errors;
    }
    return errors;
}

std::array<int, P25LiveDecoder::Phase2FrameSyncDibits> phase2DibitsFromWord(uint64_t word)
{
    std::array<int, P25LiveDecoder::Phase2FrameSyncDibits> out{};
    for (size_t i = 0; i < out.size(); ++i) {
        const size_t shift = (out.size() - 1 - i) * 2;
        out[i] = static_cast<int>((word >> shift) & 0x3ull);
    }
    return out;
}

int phase2SyncErrorsAt(const std::vector<int>& dibits,
                       const std::array<int, P25LiveDecoder::Phase2FrameSyncDibits>& sync,
                       size_t dibitOffset)
{
    int errors = 0;
    for (size_t i = 0; i < sync.size(); ++i) {
        if ((dibits[dibitOffset + i] & 0x03) != sync[i]) ++errors;
    }
    return errors;
}

uint64_t phase2IschWordAt(const std::vector<int>& dibits, size_t dibitOffset)
{
    if (dibitOffset + P25LiveDecoder::Phase2FrameSyncDibits > dibits.size()) return 0;
    uint64_t word = 0;
    for (size_t i = 0; i < P25LiveDecoder::Phase2FrameSyncDibits; ++i) {
        word = (word << 2) | static_cast<uint64_t>(dibits[dibitOffset + i] & 0x03);
    }
    return word & 0xffffffffffull;
}

uint64_t phase2EncodeIschInfo(uint16_t info)
{
    static constexpr uint64_t kOffset = 0x184229d461ull;
    static constexpr std::array<uint64_t, 9> kRows{
        0x8816ce36d7ull, 0x201dfd4f64ull, 0x100f4b1758ull,
        0x0c00ded18eull, 0x020807f7ffull, 0x09048d9b72ull,
        0x009da3a171ull, 0x0058cbaa4eull, 0x00343d8597ull,
    };

    // sdrtrunk's ISCHDecoder enumerates the full 9-bit I-ISCH information
    // word.  The previous implementation accidentally truncated this to 7
    // bits, forcing the two LCH-type bits to zero.  That makes many valid
    // I-ISCH words look like generic sync and prevents standards-based
    // scrambling segment anchoring, which in turn leaves TDMA follows with
    // p2sf/VCW evidence but no mask/MAC/audio.
    uint64_t word = kOffset;
    const uint16_t input = static_cast<uint16_t>(info & 0x01ffu);
    for (size_t row = 0; row < kRows.size(); ++row) {
        if ((input >> (8 - row)) & 1u) word ^= kRows[row];
    }
    return word & 0xffffffffffull;
}

P25Phase2IschState decodePhase2IschAt(const std::vector<int>& dibits, size_t dibitOffset)
{
    P25Phase2IschState out;
    out.dibitOffset = dibitOffset;
    if (dibitOffset + P25LiveDecoder::Phase2FrameSyncDibits > dibits.size()) return out;

    const uint64_t raw = phase2IschWordAt(dibits, dibitOffset);
    const int syncErrors = static_cast<int>(std::popcount(raw ^ P25LiveDecoder::Phase2FrameSyncWord));
    int bestErrors = 41;
    int secondErrors = 41;
    uint16_t bestInfo = 0;
    for (uint16_t info = 0; info < 512; ++info) {
        const int errors = static_cast<int>(std::popcount(raw ^ phase2EncodeIschInfo(info)));
        if (errors < bestErrors) {
            secondErrors = bestErrors;
            bestErrors = errors;
            bestInfo = static_cast<uint16_t>(info);
        } else if (errors < secondErrors) {
            secondErrors = errors;
        }
    }

    if (syncErrors <= 3 && syncErrors <= bestErrors) {
        out.valid = true;
        out.sync = true;
        out.errors = syncErrors;
        return out;
    }
    // sdrtrunk corrects I-ISCH up to 7 bits and later treats <3 corrected
    // bits as strong/valid for fragment timing.  Keep the same recovery range
    // so we can still derive the timeslot-offset hypothesis under field RF,
    // but downstream scoring weights low-error I-ISCH much higher.
    if (bestErrors > 7 || bestErrors >= secondErrors) return out;

    out.valid = true;
    out.errors = bestErrors;
    const uint16_t v = static_cast<uint16_t>(bestInfo & 0x01ffu);
    // I-ISCH bit layout, MSB first: LCH type[0..1], channel[2..3],
    // ISCH sequence/location[4..5], LCH flag[6], superframe sequence[7..8].
    // Preserve the existing struct's names: channel remains zero-based,
    // location is 0,1,2 for fragments 1/3,2/3,3/3.
    out.channel = static_cast<uint8_t>((v >> 5) & 0x03u);
    out.location = static_cast<uint8_t>((v >> 3) & 0x03u);
    out.freeAccess = ((v >> 2) & 0x01u) != 0;
    out.ultraframeCounter = static_cast<uint8_t>(v & 0x03u);
    return out;
}

uint8_t encodePhase2DuidCodeword(int duid)
{
    // Match sdrtrunk DataUnitID.getValueWithParity() exactly.  The Phase-2
    // DUID is an interleaved 8-bit (8,4,4) codeword at timeslot bits
    // 0,1,74,75,244,245,318,319.  The previous local generator produced
    // non-standard encoded values instead of the P25/sdrtrunk table,
    // causing FACCH/SACCH/LCCH bursts to be decoded as
    // the wrong kind and inflating VCW evidence while starving MAC/ESS.
    static constexpr std::array<uint8_t, 16> kEncodedDuid{
        0x00, // 0x0 VOICE_4
        0x17, // 0x1 RESERVED
        0x2E, // 0x2 RESERVED
        0x39, // 0x3 SCRAMBLED_SACCH
        0x4B, // 0x4 RESERVED
        0x5C, // 0x5 RESERVED
        0x65, // 0x6 VOICE_2
        0x72, // 0x7 RESERVED
        0x8D, // 0x8 RESERVED
        0x9A, // 0x9 SCRAMBLED_FACCH
        0xA3, // 0xA SCRAMBLED_DATCH
        0xB4, // 0xB RESERVED
        0xC6, // 0xC UNSCRAMBLED_SACCH
        0xD1, // 0xD UNSCRAMBLED_LCCH
        0xE8, // 0xE RESERVED
        0xFF, // 0xF UNSCRAMBLED_FACCH
    };
    if (duid < 0 || duid >= static_cast<int>(kEncodedDuid.size())) return 0x00;
    return kEncodedDuid[static_cast<size_t>(duid)];
}

struct Phase2DuidDecode {
    int duid = -1;
    int errors = 9;
};

Phase2DuidDecode decodePhase2Duid(uint8_t codeword)
{
    Phase2DuidDecode out;
    for (int duid = 0; duid < 16; ++duid) {
        const uint8_t expected = encodePhase2DuidCodeword(duid);
        const int distance = std::popcount(static_cast<unsigned>(codeword ^ expected));
        if (distance < out.errors) {
            out.errors = distance;
            out.duid = duid;
        }
    }
    if (out.errors > 2) out.duid = -1;
    return out;
}

P25Phase2BurstKind phase2BurstKindFromDuid(int duid)
{
    switch (duid) {
        case 0x0: return P25Phase2BurstKind::Voice4;
        case 0x3: return P25Phase2BurstKind::SacchScrambled;
        case 0x6: return P25Phase2BurstKind::Voice2;
        case 0x9: return P25Phase2BurstKind::FacchScrambled;
        case 0xC: return P25Phase2BurstKind::SacchClear;
        case 0xD: return P25Phase2BurstKind::LcchClear;
        case 0xF: return P25Phase2BurstKind::FacchClear;
        default: return P25Phase2BurstKind::Unknown;
    }
}

bool phase2BurstKindHasVoice(P25Phase2BurstKind kind)
{
    return kind == P25Phase2BurstKind::Voice4 || kind == P25Phase2BurstKind::Voice2;
}

bool phase2BurstKindUsesScrambling(P25Phase2BurstKind kind)
{
    return phase2BurstKindHasVoice(kind) ||
        kind == P25Phase2BurstKind::SacchScrambled ||
        kind == P25Phase2BurstKind::FacchScrambled;
}

uint64_t seedPhase2Scrambler(uint16_t nac, uint32_t wacn, uint16_t systemId)
{
    uint64_t reg = ((static_cast<uint64_t>(wacn) & 0xfffffull) << 24) |
        ((static_cast<uint64_t>(systemId) & 0x0fffull) << 12) |
        (static_cast<uint64_t>(nac) & 0x0fffull);
    if (reg == 0) reg = 0xfffffffffffULL;
    return reg & 0xfffffffffffULL;
}

uint8_t nextPhase2ScramblerBit(uint64_t& reg)
{
    const uint8_t out = static_cast<uint8_t>((reg >> 43) & 1ull);
    const uint8_t feedback = static_cast<uint8_t>(out ^
        ((reg >> 33) & 1ull) ^
        ((reg >> 19) & 1ull) ^
        ((reg >> 14) & 1ull) ^
        ((reg >> 8) & 1ull) ^
        ((reg >> 3) & 1ull));
    reg = ((reg << 1) & 0xfffffffffffULL) | static_cast<uint64_t>(feedback & 1u);
    return out;
}

std::array<int, P25LiveDecoder::Phase2BurstDibits * 12> makePhase2XorMaskDibits(uint16_t nac,
                                                                                 uint32_t wacn,
                                                                                 uint16_t systemId)
{
    std::array<int, P25LiveDecoder::Phase2BurstDibits * 12> mask{};
    std::array<uint8_t, 4320> bits{};
    uint64_t reg = seedPhase2Scrambler(nac, wacn, systemId);
    for (auto& bit : bits) bit = nextPhase2ScramblerBit(reg);

    for (size_t slot = 0; slot < 12; ++slot) {
        const size_t segmentStart = 20 + slot * 360;
        // sdrtrunk's ScramblingSequence returns one 320-bit sequence for the
        // 320-bit timeslot that follows the 40-bit I/S-ISCH word.  Our local
        // burst buffer is 20 dibits of ISCH/sync plus 160 dibits of timeslot.
        // Store the mask at payload/timeslot dibit indices 0..159; do not add
        // a second 10-dibit lead-in here.
        for (size_t payloadDibit = 0; payloadDibit < 160; ++payloadDibit) {
            const size_t segmentBit = payloadDibit * 2;
            const int dibit = (static_cast<int>(bits[segmentStart + segmentBit]) << 1) |
                static_cast<int>(bits[segmentStart + segmentBit + 1]);
            mask[slot * P25LiveDecoder::Phase2BurstDibits + payloadDibit] = dibit & 0x03;
        }
    }
    return mask;
}

uint8_t gf64Mul(uint8_t a, uint8_t b)
{
    a &= 0x3fu;
    b &= 0x3fu;
    uint16_t product = 0;
    for (int i = 0; i < 6; ++i) {
        if ((b >> i) & 1u) product ^= static_cast<uint16_t>(a) << i;
    }
    for (int i = 10; i >= 6; --i) {
        if ((product >> i) & 1u) product ^= static_cast<uint16_t>(0x43u) << (i - 6);
    }
    return static_cast<uint8_t>(product & 0x3fu);
}

uint8_t gf64Pow(uint8_t a, int power)
{
    uint8_t out = 1;
    while (power-- > 0) out = gf64Mul(out, a);
    return out;
}

uint8_t gf64Inv(uint8_t a)
{
    if ((a & 0x3fu) == 0) return 0;
    return gf64Pow(a, 62);
}

std::array<uint8_t, 28> rs63Remainder(const std::array<uint8_t, 63>& codeword)
{
    static constexpr std::array<uint8_t, 29> generator = {
        001, 026, 055, 072, 075, 010, 026, 027, 056, 036,
        004, 045, 073, 045, 016, 063, 033, 055, 007, 023,
        002, 031, 036, 040, 007, 022, 001, 027, 034,
    };
    std::array<uint8_t, 63> work{};
    for (size_t i = 0; i < work.size(); ++i) work[i] = static_cast<uint8_t>(codeword[i] & 0x3fu);

    for (size_t i = 0; i < 35; ++i) {
        const uint8_t coef = work[i] & 0x3fu;
        if (coef == 0) continue;
        work[i] = 0;
        for (size_t j = 1; j < generator.size(); ++j) {
            work[i + j] ^= gf64Mul(coef, generator[j]);
        }
    }

    std::array<uint8_t, 28> rem{};
    for (size_t i = 0; i < rem.size(); ++i) rem[i] = static_cast<uint8_t>(work[35 + i] & 0x3fu);
    return rem;
}

struct Phase2RsDecodeResult {
    bool ok = false;
    std::array<uint8_t, 63> symbols{};
    int correctedSymbols = 0;
};


struct Rs63P25Tables {
    std::array<int, 64> alphaTo{};
    std::array<int, 64> indexOf{};
};

const Rs63P25Tables& rs63P25Tables()
{
    static const Rs63P25Tables tables = [] {
        Rs63P25Tables t;
        t.indexOf.fill(-1);
        constexpr int MM = 6;
        constexpr int NN = 63;
        constexpr std::array<int, 7> generatorPolynomial{1, 1, 0, 0, 0, 0, 1};
        int mask = 1;
        t.alphaTo[MM] = 0;
        for (int i = 0; i < MM; ++i) {
            t.alphaTo[i] = mask;
            t.indexOf[static_cast<size_t>(t.alphaTo[i])] = i;
            if (generatorPolynomial[static_cast<size_t>(i)] != 0) t.alphaTo[MM] ^= mask;
            mask <<= 1;
        }
        t.indexOf[static_cast<size_t>(t.alphaTo[MM])] = MM;
        mask >>= 1;
        for (int i = MM + 1; i < NN; ++i) {
            if (t.alphaTo[static_cast<size_t>(i - 1)] >= mask) {
                t.alphaTo[static_cast<size_t>(i)] = t.alphaTo[MM] ^
                    ((t.alphaTo[static_cast<size_t>(i - 1)] ^ mask) << 1);
            } else {
                t.alphaTo[static_cast<size_t>(i)] = t.alphaTo[static_cast<size_t>(i - 1)] << 1;
            }
            t.indexOf[static_cast<size_t>(t.alphaTo[static_cast<size_t>(i)])] = i;
        }
        t.indexOf[0] = -1;
        return t;
    }();
    return tables;
}

Phase2RsDecodeResult rs63DecodeBerlekampMasseyP25(const std::array<uint8_t, 63>& input)
{
    // Direct port of sdrtrunk's ReedSolomon_63_P25/BerlekempMassey decoder for
    // RS(63,35,29) over GF(2^6).  sdrtrunk does not solve the ACCH shortened /
    // punctured symbols as explicit erasures; it leaves those array entries as
    // zero and lets the Berlekamp-Massey decoder correct unknown symbol errors.
    // The previous local decoder was erasure-oriented and then brute-forced only
    // one or two unknown symbols, so strong/simulcast captures could still show
    // p2sf=12/p2mask=12 while every MAC disappeared before CRC validation.
    constexpr int MM = 6;
    constexpr int NN = 63;
    constexpr int KK = 35;
    constexpr int NROOTS = NN - KK;
    constexpr int TT = NROOTS / 2;
    const auto& gf = rs63P25Tables();

    Phase2RsDecodeResult result;
    result.symbols = input;

    std::array<int, NN> outputIndex{};
    std::array<int, NN> outputPoly{};
    for (int i = 0; i < NN; ++i) {
        const int v = static_cast<int>(input[static_cast<size_t>(i)] & 0x3fu);
        outputIndex[static_cast<size_t>(i)] = gf.indexOf[static_cast<size_t>(v)];
    }

    std::array<int, NROOTS + 1> syndromes{};
    bool syndromeError = false;
    for (int i = 1; i <= NROOTS; ++i) {
        int s = 0;
        for (int j = 0; j < NN; ++j) {
            const int idx = outputIndex[static_cast<size_t>(j)];
            if (idx != -1) s ^= gf.alphaTo[static_cast<size_t>((idx + i * j) % NN)];
        }
        if (s != 0) syndromeError = true;
        syndromes[static_cast<size_t>(i)] = gf.indexOf[static_cast<size_t>(s)];
    }

    bool irrecoverable = false;
    if (syndromeError) {
        std::array<std::array<int, NROOTS>, NROOTS + 2> elp{};
        std::array<int, NROOTS + 2> d{};
        std::array<int, NROOTS + 2> l{};
        std::array<int, NROOTS + 2> uLu{};
        std::array<int, TT> root{};
        std::array<int, TT> loc{};
        std::array<int, TT + 1> z{};
        std::array<int, NN> err{};
        std::array<int, TT + 1> reg{};

        d[0] = 0;
        d[1] = syndromes[1];
        elp[0][0] = 0;
        elp[1][0] = 1;
        for (int i = 1; i < NROOTS; ++i) {
            elp[0][static_cast<size_t>(i)] = -1;
            elp[1][static_cast<size_t>(i)] = 0;
        }
        l[0] = 0;
        l[1] = 0;
        uLu[0] = -1;
        uLu[1] = 0;

        int u = 0;
        do {
            ++u;
            if (d[static_cast<size_t>(u)] == -1) {
                l[static_cast<size_t>(u + 1)] = l[static_cast<size_t>(u)];
                for (int i = 0; i <= l[static_cast<size_t>(u)]; ++i) {
                    elp[static_cast<size_t>(u + 1)][static_cast<size_t>(i)] =
                        elp[static_cast<size_t>(u)][static_cast<size_t>(i)];
                    elp[static_cast<size_t>(u)][static_cast<size_t>(i)] =
                        gf.indexOf[static_cast<size_t>(std::max(0, elp[static_cast<size_t>(u)][static_cast<size_t>(i)]))];
                }
            } else {
                int q = u - 1;
                while (q > 0 && d[static_cast<size_t>(q)] == -1) --q;
                if (q > 0) {
                    int j = q;
                    do {
                        --j;
                        if (d[static_cast<size_t>(j)] != -1 &&
                            uLu[static_cast<size_t>(q)] < uLu[static_cast<size_t>(j)]) {
                            q = j;
                        }
                    } while (j > 0);
                }

                l[static_cast<size_t>(u + 1)] = std::max(l[static_cast<size_t>(u)],
                                                         l[static_cast<size_t>(q)] + u - q);
                for (int i = 0; i < NROOTS; ++i) elp[static_cast<size_t>(u + 1)][static_cast<size_t>(i)] = 0;
                for (int i = 0; i <= l[static_cast<size_t>(q)]; ++i) {
                    if (elp[static_cast<size_t>(q)][static_cast<size_t>(i)] != -1) {
                        const int target = i + u - q;
                        if (0 <= target && target < NROOTS) {
                            elp[static_cast<size_t>(u + 1)][static_cast<size_t>(target)] =
                                gf.alphaTo[static_cast<size_t>((d[static_cast<size_t>(u)] + NN - d[static_cast<size_t>(q)] +
                                                                elp[static_cast<size_t>(q)][static_cast<size_t>(i)]) % NN)];
                        }
                    }
                }
                for (int i = 0; i <= l[static_cast<size_t>(u)]; ++i) {
                    elp[static_cast<size_t>(u + 1)][static_cast<size_t>(i)] ^=
                        elp[static_cast<size_t>(u)][static_cast<size_t>(i)];
                    elp[static_cast<size_t>(u)][static_cast<size_t>(i)] =
                        gf.indexOf[static_cast<size_t>(std::max(0, elp[static_cast<size_t>(u)][static_cast<size_t>(i)]))];
                }
            }

            uLu[static_cast<size_t>(u + 1)] = u - l[static_cast<size_t>(u + 1)];
            if (u < NROOTS) {
                d[static_cast<size_t>(u + 1)] = syndromes[static_cast<size_t>(u + 1)] != -1
                    ? gf.alphaTo[static_cast<size_t>(syndromes[static_cast<size_t>(u + 1)])]
                    : 0;
                for (int i = 1; i <= l[static_cast<size_t>(u + 1)]; ++i) {
                    if (syndromes[static_cast<size_t>(u + 1 - i)] != -1 &&
                        elp[static_cast<size_t>(u + 1)][static_cast<size_t>(i)] != 0) {
                        d[static_cast<size_t>(u + 1)] ^=
                            gf.alphaTo[static_cast<size_t>((syndromes[static_cast<size_t>(u + 1 - i)] +
                                                            gf.indexOf[static_cast<size_t>(elp[static_cast<size_t>(u + 1)][static_cast<size_t>(i)])]) % NN)];
                    }
                }
                d[static_cast<size_t>(u + 1)] = gf.indexOf[static_cast<size_t>(d[static_cast<size_t>(u + 1)])];
            }
        } while (u < NROOTS && l[static_cast<size_t>(u + 1)] <= TT);

        ++u;
        if (l[static_cast<size_t>(u)] <= TT) {
            for (int i = 0; i <= l[static_cast<size_t>(u)]; ++i) {
                elp[static_cast<size_t>(u)][static_cast<size_t>(i)] =
                    gf.indexOf[static_cast<size_t>(std::max(0, elp[static_cast<size_t>(u)][static_cast<size_t>(i)]))];
            }
            if (l[static_cast<size_t>(u)] >= 0) {
                for (int i = 1; i <= l[static_cast<size_t>(u)] && i <= TT; ++i) {
                    reg[static_cast<size_t>(i)] = elp[static_cast<size_t>(u)][static_cast<size_t>(i)];
                }
            }
            int count = 0;
            for (int i = 1; i <= NN; ++i) {
                int q = 1;
                for (int j = 1; j <= l[static_cast<size_t>(u)]; ++j) {
                    if (reg[static_cast<size_t>(j)] != -1) {
                        reg[static_cast<size_t>(j)] = (reg[static_cast<size_t>(j)] + j) % NN;
                        q ^= gf.alphaTo[static_cast<size_t>(reg[static_cast<size_t>(j)])];
                    }
                }
                if (q == 0) {
                    if (count >= TT) { irrecoverable = true; break; }
                    root[static_cast<size_t>(count)] = i;
                    loc[static_cast<size_t>(count)] = NN - i;
                    ++count;
                }
            }

            if (!irrecoverable && count == l[static_cast<size_t>(u)]) {
                for (int i = 1; i <= l[static_cast<size_t>(u)]; ++i) {
                    if (syndromes[static_cast<size_t>(i)] != -1 &&
                        elp[static_cast<size_t>(u)][static_cast<size_t>(i)] != -1) {
                        z[static_cast<size_t>(i)] = gf.alphaTo[static_cast<size_t>(syndromes[static_cast<size_t>(i)])] ^
                            gf.alphaTo[static_cast<size_t>(elp[static_cast<size_t>(u)][static_cast<size_t>(i)])];
                    } else if (syndromes[static_cast<size_t>(i)] != -1 &&
                               elp[static_cast<size_t>(u)][static_cast<size_t>(i)] == -1) {
                        z[static_cast<size_t>(i)] = gf.alphaTo[static_cast<size_t>(syndromes[static_cast<size_t>(i)])];
                    } else if (syndromes[static_cast<size_t>(i)] == -1 &&
                               elp[static_cast<size_t>(u)][static_cast<size_t>(i)] != -1) {
                        z[static_cast<size_t>(i)] = gf.alphaTo[static_cast<size_t>(elp[static_cast<size_t>(u)][static_cast<size_t>(i)])];
                    } else {
                        z[static_cast<size_t>(i)] = 0;
                    }
                    for (int j = 1; j < i; ++j) {
                        if (syndromes[static_cast<size_t>(j)] != -1 &&
                            elp[static_cast<size_t>(u)][static_cast<size_t>(i - j)] != -1) {
                            z[static_cast<size_t>(i)] ^=
                                gf.alphaTo[static_cast<size_t>((elp[static_cast<size_t>(u)][static_cast<size_t>(i - j)] +
                                                                syndromes[static_cast<size_t>(j)]) % NN)];
                        }
                    }
                    z[static_cast<size_t>(i)] = gf.indexOf[static_cast<size_t>(z[static_cast<size_t>(i)])];
                }

                for (int i = 0; i < NN; ++i) {
                    err[static_cast<size_t>(i)] = 0;
                    const int idx = outputIndex[static_cast<size_t>(i)];
                    outputPoly[static_cast<size_t>(i)] = idx != -1 ? gf.alphaTo[static_cast<size_t>(idx)] : 0;
                }
                for (int i = 0; i < l[static_cast<size_t>(u)]; ++i) {
                    const int location = loc[static_cast<size_t>(i)];
                    err[static_cast<size_t>(location)] = 1;
                    for (int j = 1; j <= l[static_cast<size_t>(u)]; ++j) {
                        if (z[static_cast<size_t>(j)] != -1) {
                            err[static_cast<size_t>(location)] ^=
                                gf.alphaTo[static_cast<size_t>((z[static_cast<size_t>(j)] + j * root[static_cast<size_t>(i)]) % NN)];
                        }
                    }
                    if (err[static_cast<size_t>(location)] != 0) {
                        err[static_cast<size_t>(location)] = gf.indexOf[static_cast<size_t>(err[static_cast<size_t>(location)])];
                        int q = 0;
                        for (int j = 0; j < l[static_cast<size_t>(u)]; ++j) {
                            if (j == i) continue;
                            const int denom = 1 ^ gf.alphaTo[static_cast<size_t>((loc[static_cast<size_t>(j)] + root[static_cast<size_t>(i)]) % NN)];
                            q += gf.indexOf[static_cast<size_t>(denom)];
                        }
                        q %= NN;
                        err[static_cast<size_t>(location)] =
                            gf.alphaTo[static_cast<size_t>((err[static_cast<size_t>(location)] - q + NN) % NN)];
                        outputPoly[static_cast<size_t>(location)] ^= err[static_cast<size_t>(location)];
                    }
                }
            } else if (!irrecoverable) {
                irrecoverable = true;
            }
        } else {
            irrecoverable = true;
        }
    } else {
        for (int i = 0; i < NN; ++i) {
            const int idx = outputIndex[static_cast<size_t>(i)];
            outputPoly[static_cast<size_t>(i)] = idx != -1 ? gf.alphaTo[static_cast<size_t>(idx)] : 0;
        }
    }

    if (irrecoverable) {
        for (int i = 0; i < NN; ++i) {
            const int idx = outputIndex[static_cast<size_t>(i)];
            outputPoly[static_cast<size_t>(i)] = idx != -1 ? gf.alphaTo[static_cast<size_t>(idx)] : 0;
        }
    }

    int changed = 0;
    for (int i = 0; i < NN; ++i) {
        const uint8_t v = static_cast<uint8_t>(outputPoly[static_cast<size_t>(i)] & 0x3f);
        if (v != (input[static_cast<size_t>(i)] & 0x3fu)) ++changed;
        result.symbols[static_cast<size_t>(i)] = v;
    }
    result.correctedSymbols = changed;
    result.ok = !irrecoverable;
    return result;
}

Phase2RsDecodeResult rs63DecodeErasures(std::array<uint8_t, 63> symbols,
                                        const std::vector<int>& erasures)
{
    Phase2RsDecodeResult out;
    out.symbols = symbols;
    if (erasures.size() > 28) return out;

    for (int pos : erasures) {
        if (pos < 0 || pos >= static_cast<int>(symbols.size())) return out;
        symbols[static_cast<size_t>(pos)] = 0;
    }

    const auto base = rs63Remainder(symbols);
    if (erasures.empty()) {
        out.ok = std::all_of(base.begin(), base.end(), [](uint8_t v) { return (v & 0x3fu) == 0; });
        out.symbols = symbols;
        return out;
    }

    const size_t vars = erasures.size();
    std::vector<std::vector<uint8_t>> matrix(28, std::vector<uint8_t>(vars + 1, 0));
    for (size_t c = 0; c < vars; ++c) {
        std::array<uint8_t, 63> unit{};
        unit[static_cast<size_t>(erasures[c])] = 1;
        const auto rem = rs63Remainder(unit);
        for (size_t r = 0; r < rem.size(); ++r) matrix[r][c] = rem[r] & 0x3fu;
    }
    for (size_t r = 0; r < base.size(); ++r) matrix[r][vars] = base[r] & 0x3fu;

    size_t rank = 0;
    std::vector<size_t> pivotCol;
    for (size_t col = 0; col < vars && rank < matrix.size(); ++col) {
        size_t pivot = rank;
        while (pivot < matrix.size() && matrix[pivot][col] == 0) ++pivot;
        if (pivot == matrix.size()) continue;
        if (pivot != rank) std::swap(matrix[pivot], matrix[rank]);
        const uint8_t inv = gf64Inv(matrix[rank][col]);
        if (inv == 0) return out;
        for (size_t j = col; j <= vars; ++j) matrix[rank][j] = gf64Mul(matrix[rank][j], inv);
        for (size_t r = 0; r < matrix.size(); ++r) {
            if (r == rank || matrix[r][col] == 0) continue;
            const uint8_t scale = matrix[r][col];
            for (size_t j = col; j <= vars; ++j) matrix[r][j] ^= gf64Mul(scale, matrix[rank][j]);
        }
        pivotCol.push_back(col);
        ++rank;
    }

    for (const auto& row : matrix) {
        bool anyCoeff = false;
        for (size_t c = 0; c < vars; ++c) anyCoeff = anyCoeff || row[c] != 0;
        if (!anyCoeff && row[vars] != 0) return out;
    }
    if (rank != vars) return out;

    for (size_t r = 0; r < pivotCol.size(); ++r) {
        const size_t c = pivotCol[r];
        symbols[static_cast<size_t>(erasures[c])] = matrix[r][vars] & 0x3fu;
    }
    const auto check = rs63Remainder(symbols);
    if (!std::all_of(check.begin(), check.end(), [](uint8_t v) { return (v & 0x3fu) == 0; })) return out;

    out.ok = true;
    out.symbols = symbols;
    out.correctedSymbols = static_cast<int>(erasures.size());
    return out;
}


Phase2RsDecodeResult rs63DecodeWithUnknownSymbolErrors(std::array<uint8_t, 63> symbols,
                                                       const std::vector<int>& erasures,
                                                       const std::vector<int>& transmittedPositions,
                                                       int maxUnknownSymbols)
{
    // sdrtrunk's ReedSolomon_63_35_29_P25 corrects unknown symbol errors in
    // addition to the standard punctured/shortened erasures.  The previous
    // local decoder solved only the known erasures; a single bad 6-bit ACCH
    // symbol made RS fail and field logs showed p2sf/p2mask/VCW but p2mac=0/0.
    // This bounded search recovers the common strong-signal/simulcast case of
    // one or two unknown RS symbol errors while preserving the existing erasure
    // solver and CRC as the final authority.
    // Realtime hot path guard: callers pass a negative value when they only
    // want the cheap direct/erasure path.  Full BM + unknown-symbol fallback is
    // useful for offline diagnostics, but it is too expensive to run on every
    // false ACCH hypothesis in the live GUI/DSP path.
    if (maxUnknownSymbols < 0) {
        return rs63DecodeErasures(symbols, erasures);
    }

    // First try the same full Berlekamp-Massey RS decoder that sdrtrunk uses.
    // It treats shortened/punctured positions as zero-valued symbols and can
    // correct unknown symbol errors across the full RS(63,35,29) word.
    auto bm = rs63DecodeBerlekampMasseyP25(symbols);
    if (bm.ok) return bm;

    auto base = rs63DecodeErasures(symbols, erasures);
    if (base.ok || maxUnknownSymbols == 0) return base;

    std::array<bool, 63> isErasure{};
    for (int e : erasures) {
        if (0 <= e && e < 63) isErasure[static_cast<size_t>(e)] = true;
    }

    std::vector<int> candidates;
    candidates.reserve(transmittedPositions.size());
    for (int pos : transmittedPositions) {
        if (pos < 0 || pos >= 63) continue;
        if (isErasure[static_cast<size_t>(pos)]) continue;
        if (std::find(candidates.begin(), candidates.end(), pos) == candidates.end()) {
            candidates.push_back(pos);
        }
    }

    auto tryUnknownSet = [&](const std::vector<int>& unknowns) -> Phase2RsDecodeResult {
        std::vector<int> combined = erasures;
        combined.insert(combined.end(), unknowns.begin(), unknowns.end());
        auto out = rs63DecodeErasures(symbols, combined);
        if (out.ok) out.correctedSymbols = static_cast<int>(combined.size());
        return out;
    };

    if (maxUnknownSymbols >= 1) {
        for (int a : candidates) {
            auto out = tryUnknownSet(std::vector<int>{a});
            if (out.ok) return out;
        }
    }

    if (maxUnknownSymbols >= 2) {
        for (size_t i = 0; i < candidates.size(); ++i) {
            for (size_t j = i + 1; j < candidates.size(); ++j) {
                auto out = tryUnknownSet(std::vector<int>{candidates[i], candidates[j]});
                if (out.ok) return out;
            }
        }
    }

    return base;
}

void appendBitsFromDibitRange(std::vector<uint8_t>& bits,
                              const std::vector<int>& dibits,
                              size_t start,
                              size_t count)
{
    if (start + count > dibits.size()) return;
    for (size_t i = 0; i < count; ++i) {
        const auto pair = P25LiveDecoder::bitsFromDibit(dibits[start + i] & 0x03);
        bits.push_back(pair[0]);
        bits.push_back(pair[1]);
    }
}

uint16_t p25Phase2Crc12(const std::vector<uint8_t>& bits, size_t len)
{
    static constexpr std::array<uint8_t, 13> poly{1, 1, 0, 0, 0, 1, 0, 0, 1, 0, 1, 1, 1};
    std::vector<uint8_t> work(len + 12, 0);
    for (size_t i = 0; i < len && i < bits.size(); ++i) work[i] = bits[i] ? 1u : 0u;
    for (size_t i = 0; i < len; ++i) {
        if (!work[i]) continue;
        for (size_t j = 0; j < poly.size(); ++j) work[i + j] ^= poly[j];
    }
    uint16_t crc = 0;
    for (size_t i = 0; i < 12; ++i) crc = static_cast<uint16_t>((crc << 1) | (work[len + i] & 1u));
    return static_cast<uint16_t>((crc ^ 0x0fffu) & 0x0fffu);
}

bool p25Phase2Crc12Ok(const std::vector<uint8_t>& bits, size_t len)
{
    if (bits.size() < len + 12) return false;
    uint16_t transmitted = 0;
    for (size_t i = 0; i < 12; ++i) transmitted = static_cast<uint16_t>((transmitted << 1) | (bits[len + i] & 1u));
    return transmitted == p25Phase2Crc12(bits, len);
}

uint16_t p25Phase2Crc16(const std::vector<uint8_t>& bits, size_t len)
{
    static constexpr std::array<uint8_t, 17> poly{1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    std::vector<uint8_t> work(len + 16, 0);
    for (size_t i = 0; i < len && i < bits.size(); ++i) work[i] = bits[i] ? 1u : 0u;
    for (size_t i = 0; i < len; ++i) {
        if (!work[i]) continue;
        for (size_t j = 0; j < poly.size(); ++j) work[i + j] ^= poly[j];
    }
    uint16_t crc = 0;
    for (size_t i = 0; i < 16; ++i) crc = static_cast<uint16_t>((crc << 1) | (work[len + i] & 1u));
    return static_cast<uint16_t>((crc ^ 0xffffu) & 0xffffu);
}

bool p25Phase2Crc16Ok(const std::vector<uint8_t>& bits, size_t len)
{
    if (bits.size() < len + 16) return false;
    uint16_t transmitted = 0;
    for (size_t i = 0; i < 16; ++i) transmitted = static_cast<uint16_t>((transmitted << 1) | (bits[len + i] & 1u));
    return transmitted == p25Phase2Crc16(bits, len);
}

std::vector<uint8_t> packBitsToBytes(const std::vector<uint8_t>& bits, size_t bitCount)
{
    std::vector<uint8_t> out((bitCount + 7) / 8, 0);
    for (size_t i = 0; i < bitCount; ++i) {
        out[i / 8] = static_cast<uint8_t>((out[i / 8] << 1) | (bits[i] ? 1u : 0u));
        if ((i % 8) == 7) continue;
    }
    const size_t rem = bitCount % 8;
    if (rem != 0 && !out.empty()) out.back() = static_cast<uint8_t>(out.back() << (8 - rem));
    return out;
}

struct Phase2SessionState {
    P25Phase2EssState ess;
    std::array<uint8_t, 16> essB{};
    std::array<bool, 4> essBSeen{};
    // Legacy retained flag now means current-call PTT anchor, not just "any
    // CRC-valid MAC existed".  Generic MAC CRC is useful for mask confidence,
    // but must not authorize ESS/security or audio release.
    bool macCrcSeen = false;
    bool pttSeen = false;
    bool activeSeen = false;
    bool endPttSeen = false;
    bool idleSeen = false;
    bool hangtimeSeen = false;
    bool maskPhaseMacCrcSeen = false;
    bool essTrusted = false;
    int first4vSlot = -1;

    // Late-entry encrypted ESS is intentionally fail-closed but not poisonable:
    // a single 12/12 superframe/mask window can be the wrong slot or wrong epoch.
    // Keep it tentative until it repeats, or until MAC CRC has anchored the call.
    P25Phase2EssState tentativeEss;
    int tentativeEssRepeats = 0;

    // Late-entry fallback: some captures show stable superframe/mask hypotheses but no
    // decoded MAC PTT PDU, so first4vSlot never becomes known. Keep five
    // independent ESS hypotheses keyed by possible first-4V slot. These are
    // internal decoder state only; audio remains gated until trusted ESS or MAC.
    std::array<P25Phase2EssState, 5> essHypotheses{};
    std::array<std::array<uint8_t, 16>, 5> essBHypotheses{};
    std::array<std::array<bool, 4>, 5> essBSeenHypotheses{};
};

bool phase2EssSameCore(const P25Phase2EssState& a, const P25Phase2EssState& b)
{
    return a.known && b.known &&
        a.algId == b.algId &&
        a.keyId == b.keyId &&
        a.messageIndicator == b.messageIndicator;
}

void phase2ResetEssFragments(Phase2SessionState& session)
{
    session.essB = {};
    session.essBSeen = {};
    session.essHypotheses = {};
    session.essBHypotheses = {};
    session.essBSeenHypotheses = {};
    session.tentativeEss = {};
    session.tentativeEssRepeats = 0;
}

void phase2ClearSessionEss(Phase2SessionState& session)
{
    session.ess = {};
    session.essTrusted = false;
    session.first4vSlot = -1;
    phase2ResetEssFragments(session);
}

void phase2ClearCallSession(Phase2SessionState& session)
{
    session.macCrcSeen = false;
    session.pttSeen = false;
    session.activeSeen = false;
    session.endPttSeen = false;
    session.idleSeen = false;
    session.hangtimeSeen = false;
    session.maskPhaseMacCrcSeen = false;
    phase2ClearSessionEss(session);
}

bool phase2AcceptVoiceEss(Phase2SessionState& session, const P25Phase2EssState& candidate)
{
    if (!candidate.known || !candidate.fecValidated) return false;

    // CRC-valid MAC PTT is authoritative.  A clear late-entry ESS is also safe
    // to promote because it can only open audio when the algorithm is the P25
    // clear algorithm (0x80).
    if (session.pttSeen || !candidate.encrypted) {
        session.ess = candidate;
        session.essTrusted = true;
        session.tentativeEss = {};
        session.tentativeEssRepeats = 0;
        return true;
    }

    // Do not let a single full 12/12 masked window mark the call encrypted.
    // Require the same encrypted ESS to repeat from independent voice windows.
    if (phase2EssSameCore(session.tentativeEss, candidate)) {
        ++session.tentativeEssRepeats;
    } else {
        session.tentativeEss = candidate;
        session.tentativeEssRepeats = 1;
    }

    if (session.tentativeEssRepeats >= 2) {
        session.ess = candidate;
        session.essTrusted = true;
        return true;
    }

    return false;
}

P25Phase2EssState phase2EssFromMacPtt(const std::vector<uint8_t>& bytes, bool fecValidated, int correctedSymbols)
{
    P25Phase2EssState ess;
    if (bytes.size() < 13) return ess;
    ess.known = true;
    ess.fecValidated = fecValidated;
    ess.correctedSymbols = correctedSymbols;
    for (size_t i = 0; i < ess.messageIndicator.size(); ++i) ess.messageIndicator[i] = bytes[i + 1];
    ess.algId = bytes[10];
    ess.keyId = static_cast<uint16_t>((static_cast<uint16_t>(bytes[11]) << 8) | bytes[12]);
    ess.encrypted = ess.algId != 0x80;
    return ess;
}

std::optional<P25Phase2MacPdu> decodePhase2Acch(const std::vector<int>& payloadDibits,
                                                P25Phase2BurstKind kind,
                                                size_t dibitOffset,
                                                bool deepSearch = true)
{
    const bool fast = kind == P25Phase2BurstKind::FacchScrambled ||
        kind == P25Phase2BurstKind::FacchClear;
    const bool lcch = kind == P25Phase2BurstKind::LcchClear;
    const bool acch = fast || lcch ||
        kind == P25Phase2BurstKind::SacchScrambled ||
        kind == P25Phase2BurstKind::SacchClear;

    // The Phase-2 timeslot payload is exactly 320 bits / 160 dibits after the
    // 40-bit ISCH word.  A previous implementation still expected the older
    // 170-dibit half-ISCH-included buffer and returned nullopt here, so even a
    // perfect RF capture could show p2sf=12/p2mask=12 forever with p2mac=0/0.
    if (!acch || payloadDibits.size() < 160) return std::nullopt;

    auto appendBitRange = [&](std::vector<uint8_t>& bits,
                              int startBit,
                              size_t countBits,
                              bool swapBitOrder,
                              bool invertDibit,
                              int acchSlipDibits) {
        const int slippedStartBit = startBit + acchSlipDibits * 2;
        for (size_t i = 0; i < countBits; ++i) {
            const int bitIndex = slippedStartBit + static_cast<int>(i);
            if (bitIndex < 0) return false;
            const size_t dibitIndex = static_cast<size_t>(bitIndex / 2);
            if (dibitIndex >= payloadDibits.size()) return false;
            int dibit = payloadDibits[dibitIndex] & 0x03;
            if (invertDibit) dibit ^= 0x03;
            const auto pair = P25LiveDecoder::bitsFromDibit(dibit);
            int bitInDibit = bitIndex & 0x01;
            if (swapBitOrder) bitInDibit ^= 0x01;
            bits.push_back(pair[static_cast<size_t>(bitInDibit)]);
        }
        return true;
    };

    auto makeCodedBits = [&](bool swapBitOrder, bool invertDibit, int acchSlipDibits) -> std::vector<uint8_t> {
        std::vector<uint8_t> codedBits;
        codedBits.reserve(fast ? 270 : 312);
        bool ok = true;

        if (fast) {
            // Match sdrtrunk FacchTimeslot field layout in the 320-bit timeslot:
            // INFO_1..12     bits 2..73
            // INFO_13..22    bits 76..135
            // INFO_23        bits 136,137,180,181,182,183
            // INFO_24..26    bits 184..201
            // PARITY_1..7    bits 202..243
            // PARITY_8..19   bits 246..317
            ok = ok && appendBitRange(codedBits, 2, 72, swapBitOrder, invertDibit, acchSlipDibits);
            ok = ok && appendBitRange(codedBits, 76, 60, swapBitOrder, invertDibit, acchSlipDibits);
            ok = ok && appendBitRange(codedBits, 136, 2, swapBitOrder, invertDibit, acchSlipDibits);
            ok = ok && appendBitRange(codedBits, 180, 4, swapBitOrder, invertDibit, acchSlipDibits);
            ok = ok && appendBitRange(codedBits, 184, 18, swapBitOrder, invertDibit, acchSlipDibits);
            ok = ok && appendBitRange(codedBits, 202, 42, swapBitOrder, invertDibit, acchSlipDibits);
            ok = ok && appendBitRange(codedBits, 246, 72, swapBitOrder, invertDibit, acchSlipDibits);
        } else {
            // Match sdrtrunk SacchTimeslot/LcchTimeslot field layout:
            // INFO_1..12     bits 2..73
            // INFO_13..30    bits 76..183
            // PARITY_1..10   bits 184..243
            // PARITY_11..22  bits 246..317
            ok = ok && appendBitRange(codedBits, 2, 72, swapBitOrder, invertDibit, acchSlipDibits);
            ok = ok && appendBitRange(codedBits, 76, 108, swapBitOrder, invertDibit, acchSlipDibits);
            ok = ok && appendBitRange(codedBits, 184, 60, swapBitOrder, invertDibit, acchSlipDibits);
            ok = ok && appendBitRange(codedBits, 246, 72, swapBitOrder, invertDibit, acchSlipDibits);
        }

        if (!ok) codedBits.clear();
        return codedBits;
    };

    auto decodeAttempt = [&](bool swapBitOrder, bool invertDibit, int acchSlipDibits) -> std::optional<P25Phase2MacPdu> {
        const auto codedBits = makeCodedBits(swapBitOrder, invertDibit, acchSlipDibits);
        const size_t expectedBits = fast ? 270u : 312u;
        if (codedBits.size() != expectedBits) return std::nullopt;

        const size_t macContentBits = fast ? 144u : (lcch ? 152u : 168u);
        const size_t crcProtectedBits = fast ? 144u : (lcch ? 164u : 168u);
        const size_t totalPayloadBits = fast ? 156u : 180u;

        auto makePduFromBits = [&](const std::vector<uint8_t>& messageBits,
                                   bool fecDecoded,
                                   bool crcOk,
                                   int correctedSymbols) {
            P25Phase2MacPdu pdu;
            pdu.valid = crcOk;
            pdu.dibitOffset = dibitOffset;
            pdu.detectedKind = kind;
            pdu.source = kind;
            pdu.fecDecoded = fecDecoded;
            pdu.crcValid = crcOk;
            pdu.correctedSymbols = correctedSymbols;
            pdu.acchHypothesisKnown = true;
            pdu.acchBitOrderSwapped = swapBitOrder;
            pdu.acchDibitInverted = invertDibit;
            pdu.acchSlipDibits = acchSlipDibits;
            pdu.bytes = packBitsToBytes(messageBits, macContentBits);
            if (!pdu.bytes.empty()) {
                pdu.opcode = static_cast<uint8_t>((pdu.bytes[0] >> 5) & 0x07u);
                pdu.offset = static_cast<uint8_t>((pdu.bytes[0] >> 2) & 0x07u);
            }
            if (crcOk && pdu.opcode == 1 && pdu.bytes.size() >= 18) {
                pdu.essPresent = true;
                // A direct ACCH information-field CRC hit is still a validated
                // MAC_PTT ESS.  Do not require Reed-Solomon recovery to mark the
                // clear/encrypted state trustworthy; otherwise valid direct CRC
                // PTTs keep Phase-2 audio muted forever.
                pdu.ess = phase2EssFromMacPtt(pdu.bytes, fecDecoded || crcOk, correctedSymbols);
            }
            return pdu;
        };

        // Field debugging showed a pathological strong-signal failure where the
        // timeslot layout/DUID looked correct, but p2mac stayed 0/0 forever.
        // If our local RS orientation/generator is wrong, an otherwise perfect
        // ACCH message would be discarded before the MAC CRC is ever tested.
        // sdrtrunk ultimately validates the reconstructed MAC message with the
        // P25 CRC; do that first on the raw information field as a fail-safe.
        std::vector<uint8_t> directBits;
        if (codedBits.size() >= totalPayloadBits) {
            directBits.assign(codedBits.begin(), codedBits.begin() + static_cast<std::ptrdiff_t>(totalPayloadBits));
            const bool directCrcOk = lcch ? p25Phase2Crc16Ok(directBits, crcProtectedBits)
                                          : p25Phase2Crc12Ok(directBits, crcProtectedBits);
            auto directPdu = makePduFromBits(directBits, false, directCrcOk, 0);
            if (directCrcOk) return directPdu;
        }

        // P25 Phase 2 ACCH Reed-Solomon words are transmitted as
        // INFO_1..INFO_N followed by PARITY_1..PARITY_M.  The shortened
        // RS(63,35,29) decoder indexes those symbols in the same reversed layout
        // used by sdrtrunk:
        //   FACCH:      parity symbols  9..27, information symbols 28..53
        //   SACCH/LCCH: parity symbols  6..27, information symbols 28..57
        std::vector<uint8_t> txSymbols;
        txSymbols.reserve(codedBits.size() / 6);
        for (size_t bit = 0; bit + 5 < codedBits.size(); bit += 6) {
            txSymbols.push_back(static_cast<uint8_t>((codedBits[bit] << 5) |
                                                     (codedBits[bit + 1] << 4) |
                                                     (codedBits[bit + 2] << 3) |
                                                     (codedBits[bit + 3] << 2) |
                                                     (codedBits[bit + 4] << 1) |
                                                     codedBits[bit + 5]));
        }

        std::array<uint8_t, 63> symbols{};
        std::vector<int> erasures;
        std::vector<int> transmittedPositions;
        int firstInfoSymbol = 0;
        int lastInfoSymbol = 0;

        if (fast) {
            if (txSymbols.size() != 45u) return std::nullopt;
            for (int v = 0; v <= 8; ++v) erasures.push_back(v);
            for (int v = 54; v <= 62; ++v) erasures.push_back(v);
            firstInfoSymbol = 28;
            lastInfoSymbol = 53;
            for (int v = 9; v <= 53; ++v) transmittedPositions.push_back(v);
            for (size_t i = 0; i < 26u; ++i) {
                symbols[static_cast<size_t>(lastInfoSymbol - static_cast<int>(i))] = txSymbols[i] & 0x3fu;
            }
            for (size_t i = 0; i < 19u; ++i) {
                symbols[static_cast<size_t>(27 - static_cast<int>(i))] = txSymbols[26u + i] & 0x3fu;
            }
        } else {
            if (txSymbols.size() != 52u) return std::nullopt;
            for (int v = 0; v <= 5; ++v) erasures.push_back(v);
            for (int v = 58; v <= 62; ++v) erasures.push_back(v);
            firstInfoSymbol = 28;
            lastInfoSymbol = 57;
            for (int v = 6; v <= 57; ++v) transmittedPositions.push_back(v);
            for (size_t i = 0; i < 30u; ++i) {
                symbols[static_cast<size_t>(lastInfoSymbol - static_cast<int>(i))] = txSymbols[i] & 0x3fu;
            }
            for (size_t i = 0; i < 22u; ++i) {
                symbols[static_cast<size_t>(27 - static_cast<int>(i))] = txSymbols[30u + i] & 0x3fu;
            }
        }

        const int maxUnknownSymbols = deepSearch ? 1 : -1;
        const auto rs = rs63DecodeWithUnknownSymbolErrors(symbols, erasures, transmittedPositions, maxUnknownSymbols);
        if (!rs.ok) {
            // Return a non-CRC diagnostic candidate instead of null so the field
            // log can distinguish "ACCH was extracted but neither direct CRC nor
            // RS recovered it" from "no ACCH extraction path was reached".
            if (!directBits.empty()) return makePduFromBits(directBits, false, false, 0);
            return std::nullopt;
        }

        std::vector<uint8_t> decodedBits;
        decodedBits.reserve(totalPayloadBits);
        for (int sym = lastInfoSymbol; sym >= firstInfoSymbol && decodedBits.size() < totalPayloadBits; --sym) {
            const uint8_t v = rs.symbols[static_cast<size_t>(sym)] & 0x3fu;
            for (int bit = 5; bit >= 0 && decodedBits.size() < totalPayloadBits; --bit) {
                decodedBits.push_back(static_cast<uint8_t>((v >> bit) & 1u));
            }
        }

        const bool crcOk = lcch ? p25Phase2Crc16Ok(decodedBits, crcProtectedBits)
                                : p25Phase2Crc12Ok(decodedBits, crcProtectedBits);
        return makePduFromBits(decodedBits,
                               true,
                               crcOk,
                               std::max(0, rs.correctedSymbols - static_cast<int>(erasures.size())));
    };

    // Decode the nominal ACCH layout first, then try conservative alternate
    // hypotheses when RS can decode but CRC never validates.  Only CRC-valid
    // MAC/ESS promotes audio; non-CRC attempts are returned for diagnostics/FEC
    // counts so field logs can distinguish "no ACCH extraction" from "FEC OK,
    // CRC bad".
    std::optional<P25Phase2MacPdu> bestNonCrc;
    auto consider = [&](bool swapBitOrder, bool invertDibit, int acchSlipDibits) -> std::optional<P25Phase2MacPdu> {
        auto pdu = decodeAttempt(swapBitOrder, invertDibit, acchSlipDibits);
        if (!pdu) return std::nullopt;
        if (pdu->crcValid) return pdu;
        if (!bestNonCrc || pdu->correctedSymbols < bestNonCrc->correctedSymbols) bestNonCrc = pdu;
        return std::nullopt;
    };

    if (auto pdu = consider(false, false, 0)) return pdu;
    if (!deepSearch) return bestNonCrc;
    if (auto pdu = consider(true, false, 0)) return pdu;

    for (int slip : {-2, -1, 1, 2}) {
        if (auto pdu = consider(false, false, slip)) return pdu;
        if (auto pdu = consider(true, false, slip)) return pdu;
    }

    for (int slip : {0, -2, -1, 1, 2}) {
        if (auto pdu = consider(false, true, slip)) return pdu;
        if (auto pdu = consider(true, true, slip)) return pdu;
    }

    return bestNonCrc;
}

std::optional<P25Phase2EssState> decodePhase2VoiceEss(const std::vector<int>& payloadDibits,
                                                       int burstId,
                                                       Phase2SessionState& session)
{
    if (payloadDibits.size() < 160 || burstId < 0 || burstId > 4) return std::nullopt;
    const size_t essStart = 74;
    if (essStart + 84 > payloadDibits.size()) return std::nullopt;

    // sdrtrunk maps Phase 2 ESS as:
    //   Voice4: 24-bit ESS-B fragment at timeslot bits 148..171
    //   Voice2: 168-bit ESS-A fragment at timeslot bits 148..243 and 246..317
    // In dibits within the 320-bit timeslot, ESS starts at bit 148 / dibit 74.
    if (burstId < 4) {
        for (int i = 0; i < 12; i += 3) {
            session.essB[static_cast<size_t>(burstId * 4 + (i / 3))] =
                static_cast<uint8_t>(((payloadDibits[essStart + static_cast<size_t>(i)] & 0x03) << 4) |
                                     ((payloadDibits[essStart + static_cast<size_t>(i + 1)] & 0x03) << 2) |
                                     (payloadDibits[essStart + static_cast<size_t>(i + 2)] & 0x03));
        }
        session.essBSeen[static_cast<size_t>(burstId)] = true;
        return std::nullopt;
    }

    std::array<uint8_t, 28> essA{};
    size_t cursor = essStart;
    for (size_t i = 0; i < essA.size(); ++i) {
        if (cursor + 2 >= payloadDibits.size()) return std::nullopt;
        essA[i] = static_cast<uint8_t>(((payloadDibits[cursor] & 0x03) << 4) |
                                       ((payloadDibits[cursor + 1] & 0x03) << 2) |
                                       (payloadDibits[cursor + 2] & 0x03));
        cursor += (i == 15) ? 4u : 3u;
    }

    // Match sdrtrunk's EncryptionSynchronizationSequenceProcessor exactly:
    //   input[0..27]  = ESS-A parity hexbits, reversed
    //   input[28..31] = ESS-B4 information hexbits, reversed
    //   input[32..35] = ESS-B3 information hexbits, reversed
    //   input[36..39] = ESS-B2 information hexbits, reversed
    //   input[40..43] = ESS-B1 information hexbits, reversed
    //   input[44..62] = shortened zero symbols for the embedded RS(63,35)
    // Missing ESS-B fragments are erasures.  A single B fragment plus ESS-A is
    // sufficient for RS(44,16,29) recovery; a single full 12/12 mask window is not.
    std::array<uint8_t, 63> codeword{};
    std::vector<int> erasures;

    for (size_t i = 0; i < essA.size(); ++i) {
        codeword[i] = essA[essA.size() - 1u - i] & 0x3fu;
    }

    auto placeEssB = [&](int burst, int baseSymbol) {
        const size_t src = static_cast<size_t>(burst * 4);
        if (session.essBSeen[static_cast<size_t>(burst)]) {
            for (int i = 0; i < 4; ++i) {
                codeword[static_cast<size_t>(baseSymbol + i)] = session.essB[src + static_cast<size_t>(3 - i)] & 0x3fu;
            }
        } else {
            for (int i = 0; i < 4; ++i) erasures.push_back(baseSymbol + i);
        }
    };

    placeEssB(3, 28); // ESS-B4
    placeEssB(2, 32); // ESS-B3
    placeEssB(1, 36); // ESS-B2
    placeEssB(0, 40); // ESS-B1

    if (erasures.size() == 16u) return std::nullopt; // ESS-A without any B fragment is not decodable.

    std::vector<int> essTransmittedPositions;
    essTransmittedPositions.reserve(44);
    for (int v = 0; v <= 43; ++v) essTransmittedPositions.push_back(v);
    const auto rs = rs63DecodeWithUnknownSymbolErrors(codeword, erasures, essTransmittedPositions, 2);
    if (!rs.ok) return std::nullopt;

    std::vector<uint8_t> messageBits;
    messageBits.reserve(96);
    for (int sym = 43; sym >= 28; --sym) {
        const uint8_t v = rs.symbols[static_cast<size_t>(sym)] & 0x3fu;
        for (int bit = 5; bit >= 0; --bit) messageBits.push_back(static_cast<uint8_t>((v >> bit) & 1u));
    }
    if (messageBits.size() != 96u) return std::nullopt;

    auto getBits = [&](size_t offset, size_t count) {
        uint32_t value = 0;
        for (size_t i = 0; i < count; ++i) value = (value << 1) | (messageBits[offset + i] & 1u);
        return value;
    };

    P25Phase2EssState ess;
    ess.known = true;
    ess.fecValidated = true;
    ess.correctedSymbols = std::max(0, rs.correctedSymbols);
    ess.algId = static_cast<uint8_t>(getBits(0, 8));
    ess.keyId = static_cast<uint16_t>(getBits(8, 16));
    for (size_t i = 0; i < ess.messageIndicator.size(); ++i) {
        ess.messageIndicator[i] = static_cast<uint8_t>(getBits(24 + i * 8, 8));
    }
    ess.encrypted = ess.algId != 0x80;
    return ess;
}

struct Phase2SyncHit {
    size_t dibitOffset = 0;
    int errors = 0;
};

struct Phase2SuperframeLock {
    size_t dibitOffset = 0;
    int syncScore = 0;
    int syncErrors = 0;
};

constexpr std::array<size_t, 6> kPhase2PreferredSuperframeSyncSlots{2, 3, 6, 7, 10, 11};
constexpr std::array<size_t, 12> kPhase2AllBurstSlots{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};

bool phase2OffsetInWindow(size_t offset, size_t start, size_t length)
{
    return offset >= start && offset < start + length;
}


// sdrtrunk Phase-2 superframe channel/timeslot order is not a simple
// even/odd parity for all 12 bursts.  Each 1/3 superframe fragment carries
// A,B,C,D timeslots; the final fragment swaps C/D when presenting logical
// traffic-channel order.  The physical scrambling mask still uses the raw
// 0..11 burst index, but grant-slot filtering and voice ESS sequencing must
// use this logical traffic slot mapping.  A wrong label here exactly produces
// the field symptom: p2sf=12/p2mask=12/p2vcw>0 followed by "wrong TDMA slot"
// and no audio even though the traffic channel was found.
uint8_t phase2TrafficSlotFromSuperframeBurstIndex(size_t superframeBurstIndex) noexcept
{
    switch (superframeBurstIndex % 12u) {
        case 0: case 2: case 4: case 6: case 8: case 11:
            return 0; // sdrtrunk TIMESLOT_1 / grant slot 0
        case 1: case 3: case 5: case 7: case 9: case 10:
        default:
            return 1; // sdrtrunk TIMESLOT_2 / grant slot 1
    }
}

// Per logical traffic slot, voice ESS fragments progress in channel order.
// This is the sequence order used by sdrtrunk's getChannel1Timeslots()/
// getChannel2Timeslots(), including the final-fragment C/D inversion.
int phase2LogicalVoiceSequenceIndex(size_t superframeBurstIndex) noexcept
{
    switch (superframeBurstIndex % 12u) {
        case 0: case 1: return 0;
        case 2: case 3: return 1;
        case 4: case 5: return 2;
        case 6: case 7: return 3;
        case 8: case 9: return 4;
        case 10: case 11: return 5;
        default: return 0;
    }
}

std::optional<Phase2SyncHit> phase2SyncHitAt(const std::vector<Phase2SyncHit>& hits, size_t offset)
{
    auto it = std::find_if(hits.begin(), hits.end(), [&](const Phase2SyncHit& hit) {
        return hit.dibitOffset == offset;
    });
    if (it == hits.end()) return std::nullopt;
    return *it;
}

std::optional<Phase2SyncHit> phase2SyncHitNear(const std::vector<Phase2SyncHit>& hits,
                                               size_t offset,
                                               size_t maxSlipDibits)
{
    const Phase2SyncHit* best = nullptr;
    size_t bestSlip = maxSlipDibits + 1;
    for (const auto& hit : hits) {
        const size_t slip = hit.dibitOffset > offset ? hit.dibitOffset - offset : offset - hit.dibitOffset;
        if (slip > maxSlipDibits) continue;
        if (!best || slip < bestSlip || (slip == bestSlip && hit.errors < best->errors)) {
            best = &hit;
            bestSlip = slip;
        }
    }
    if (!best) return std::nullopt;
    return *best;
}

int phase2SignedSyncSlipDibits(size_t actualOffset, size_t expectedOffset)
{
    if (actualOffset >= expectedOffset) {
        const size_t diff = actualOffset - expectedOffset;
        return diff > static_cast<size_t>(std::numeric_limits<int>::max())
            ? std::numeric_limits<int>::max()
            : static_cast<int>(diff);
    }
    const size_t diff = expectedOffset - actualOffset;
    return diff > static_cast<size_t>(std::numeric_limits<int>::max())
        ? std::numeric_limits<int>::min() + 1
        : -static_cast<int>(diff);
}

std::vector<Phase2SuperframeLock> findPhase2SuperframeLocks(const std::vector<Phase2SyncHit>& hits,
                                                            size_t dibitCount)
{
    std::vector<Phase2SuperframeLock> locks;
    if (hits.size() < 2 || dibitCount < P25LiveDecoder::Phase2BurstDibits * 4) return locks;

    // Production hardening: do not assume that the first sync hits observed in a
    // weak/late-entry voice capture land only on the nominal preferred TDMA
    // superframe slots.  Candidate voice captures from the field showed valid
    // Phase 2 VCW-looking bursts but zero superframe locks because all candidate
    // epochs were generated only from slots {2,3,6,7,10,11}.  Generate epochs
    // from every possible burst slot, then score preferred slots higher.  Audio
    // is still gated later by MAC/ESS, so this can improve acquisition without
    // opening noisy audio.
    std::vector<size_t> starts;
    for (const auto& hit : hits) {
        for (size_t slot : kPhase2AllBurstSlots) {
            const size_t back = slot * P25LiveDecoder::Phase2BurstDibits;
            if (hit.dibitOffset >= back) starts.push_back(hit.dibitOffset - back);
        }
    }
    std::sort(starts.begin(), starts.end());
    starts.erase(std::unique(starts.begin(), starts.end()), starts.end());

    constexpr size_t kMaxSyncSlipDibits = 2;
    for (size_t start : starts) {
        Phase2SuperframeLock lock;
        lock.dibitOffset = start;
        int preferredHits = 0;
        int totalHits = 0;

        for (size_t slot : kPhase2AllBurstSlots) {
            const size_t expected = start + slot * P25LiveDecoder::Phase2BurstDibits;
            if (expected + P25LiveDecoder::Phase2FrameSyncDibits > dibitCount) continue;
            if (auto hit = phase2SyncHitNear(hits, expected, kMaxSyncSlipDibits)) {
                ++totalHits;
                lock.syncErrors += hit->errors;
                if (std::find(kPhase2PreferredSuperframeSyncSlots.begin(),
                              kPhase2PreferredSuperframeSyncSlots.end(),
                              slot) != kPhase2PreferredSuperframeSyncSlots.end()) {
                    ++preferredHits;
                    ++lock.syncScore;
                }
            }
        }

        // Preferred-slot evidence is best.  As a fallback, allow a soft lock
        // when several burst-spaced syncs exist but they do not line up with
        // the old preferred slot assumption.  The score is intentionally lower
        // than a true preferred-slot lock, and MAC/ESS remains mandatory for
        // audio release.
        if (preferredHits >= 2) {
            locks.push_back(lock);
        } else if (totalHits >= 3) {
            lock.syncScore = 1;
            lock.syncErrors += 6; // demote soft locks behind preferred locks.
            locks.push_back(lock);
        }
    }

    std::sort(locks.begin(), locks.end(), [](const Phase2SuperframeLock& a, const Phase2SuperframeLock& b) {
        if (a.syncScore != b.syncScore) return a.syncScore > b.syncScore;
        if (a.syncErrors != b.syncErrors) return a.syncErrors < b.syncErrors;
        return a.dibitOffset < b.dibitOffset;
    });

    std::vector<Phase2SuperframeLock> filtered;
    for (const auto& lock : locks) {
        const bool overlaps = std::any_of(filtered.begin(), filtered.end(), [&](const Phase2SuperframeLock& kept) {
            const size_t a = lock.dibitOffset;
            const size_t b = kept.dibitOffset;
            const size_t distance = a > b ? a - b : b - a;
            return distance < P25LiveDecoder::Phase2BurstDibits * 12;
        });
        if (!overlaps) filtered.push_back(lock);
    }
    std::sort(filtered.begin(), filtered.end(), [](const Phase2SuperframeLock& a, const Phase2SuperframeLock& b) {
        return a.dibitOffset < b.dibitOffset;
    });
    return filtered;
}

P25Phase2Burst decodePhase2BurstAt(const std::vector<int>& dibits,
                                   size_t pos,
                                   int syncErrors,
                                   bool superframeLocked,
                                   size_t superframeOffset,
                                   size_t superframeBurstIndex,
                                   int superframeSyncScore,
                                   int superframeSyncErrors,
                                   const std::array<int, P25LiveDecoder::Phase2BurstDibits * 12>* xorMask,
                                   uint8_t xorMaskPhase,
                                   int xorMaskPhaseScore,
                                   Phase2SessionState* session,
                                   std::vector<P25Phase2MacPdu>* macPdus,
                                   bool deepAcchSearch = true)
{
    P25Phase2Burst burst;
    if (pos + P25LiveDecoder::Phase2BurstDibits > dibits.size()) return burst;

    burst.valid = true;
    burst.dibitOffset = pos;
    burst.syncErrors = syncErrors;
    burst.superframeLocked = superframeLocked;
    burst.superframeDibitOffset = superframeOffset;
    burst.superframeSyncScore = superframeSyncScore;
    burst.superframeSyncErrors = superframeSyncErrors;
    // Superframe/mask lock is not enough to release audio. Audio lock is
    // promoted only when this burst's own ACCH/MAC CRC validates below.
    burst.phase2AudioLock = false;
    burst.tdmaSyncLock = syncErrors >= 0 && syncErrors <= 2;
    burst.superframeLock = superframeLocked && superframeSyncScore >= 4;
    burst.superframeBurstIndexKnown = superframeLocked;
    burst.superframeBurstIndex = static_cast<uint8_t>(superframeBurstIndex & 0x0fu);
    burst.grantSlotKnown = superframeLocked;
    burst.grantSlot = phase2TrafficSlotFromSuperframeBurstIndex(superframeBurstIndex);
    burst.isch = decodePhase2IschAt(dibits, pos);

    // sdrtrunk SuperFrameFragment separates each 360-bit Phase-2 burst into a
    // 40-bit I/S-ISCH word followed by a 320-bit timeslot.  This code works in
    // dibits, so payload/timeslot starts after 20 dibits and is 160 dibits
    // long.  A previous implementation used a 10-dibit offset and 170-dibit
    // payload, which shifted DUID, ESS and AMBE extraction by one half-ISCH.
    const size_t payload = pos + P25LiveDecoder::Phase2FrameSyncDibits;
    constexpr size_t kPhase2TimeslotPayloadDibits = 160;
    if (payload + kPhase2TimeslotPayloadDibits > dibits.size()) return burst;

    // sdrtrunk Timeslot uses DUID field bits 0,1,74,75,244,245,318,319,
    // i.e. dibits 0,37,122,159 of the 320-bit timeslot.
    burst.rawDuidCodeword = static_cast<uint8_t>(((dibits[payload + 0] & 0x03) << 6) |
                                                 ((dibits[payload + 37] & 0x03) << 4) |
                                                 ((dibits[payload + 122] & 0x03) << 2) |
                                                 (dibits[payload + 159] & 0x03));
    const auto duid = decodePhase2Duid(burst.rawDuidCodeword);
    burst.duid = duid.duid;
    burst.duidErrors = duid.errors;
    burst.kind = phase2BurstKindFromDuid(duid.duid);

    std::vector<int> rawPayloadDibits(kPhase2TimeslotPayloadDibits, 0);
    std::vector<int> descrambledPayloadDibits(kPhase2TimeslotPayloadDibits, 0);
    const bool canApplyMask = xorMask && superframeLocked && superframeBurstIndex < 12;
    const size_t maskBurstIndex = (superframeBurstIndex + static_cast<size_t>(xorMaskPhase)) % 12u;
    burst.rawPayloadDibits.reserve(rawPayloadDibits.size());
    burst.maskedPayloadDibits.reserve(descrambledPayloadDibits.size());
    for (size_t i = 0; i < rawPayloadDibits.size(); ++i) {
        int d = dibits[payload + i] & 0x03;
        burst.rawPayloadDibits.push_back(d & 0x03);
        rawPayloadDibits[i] = d & 0x03;
        if (canApplyMask) {
            const size_t maskIndex = maskBurstIndex * P25LiveDecoder::Phase2BurstDibits + i;
            d ^= ((*xorMask)[maskIndex] & 0x03);
        }
        descrambledPayloadDibits[i] = d & 0x03;
        burst.maskedPayloadDibits.push_back(descrambledPayloadDibits[i]);
    }
    burst.xorMaskApplied = canApplyMask;
    burst.xorMaskPhaseKnown = canApplyMask;
    burst.xorMaskPhase = static_cast<uint8_t>(xorMaskPhase % 12u);
    burst.xorMaskPhaseScore = canApplyMask ? xorMaskPhaseScore : 0;
    burst.maskPhaseLock = canApplyMask && xorMaskPhaseScore > 0;

    {
        auto selectMacPdu = [&]() -> std::optional<P25Phase2MacPdu> {
            // Prefer the decoded DUID/kind, but do not let a single DUID error
            // suppress MAC acquisition.  Different vendors and weak simulcast
            // conditions can produce stable TDMA superframe/mask hypotheses with noisy
            // DUIDs.  Try alternate ACCH interpretations and accept only a
            // CRC-valid MAC for release; non-CRC decodes are kept only for FEC
            // diagnostics.
            std::optional<P25Phase2MacPdu> bestNonCrc;
            std::array<P25Phase2BurstKind, 6> candidates{
                burst.kind,
                P25Phase2BurstKind::SacchScrambled,
                P25Phase2BurstKind::SacchClear,
                P25Phase2BurstKind::FacchScrambled,
                P25Phase2BurstKind::FacchClear,
                P25Phase2BurstKind::LcchClear,
            };
            std::array<int, 16> seen{};
            size_t seenCount = 0;
            for (P25Phase2BurstKind k : candidates) {
                if (phase2BurstKindUsesScrambling(k) && !canApplyMask) continue;
                const int key = static_cast<int>(k);
                bool duplicate = false;
                for (size_t i = 0; i < seenCount; ++i) duplicate = duplicate || seen[i] == key;
                if (duplicate) continue;
                if (seenCount < seen.size()) seen[seenCount++] = key;
                const auto& candidatePayload = phase2BurstKindUsesScrambling(k) ? descrambledPayloadDibits : rawPayloadDibits;
                auto candidate = decodePhase2Acch(candidatePayload, k, payload, deepAcchSearch);
                if (!candidate) continue;
                candidate->detectedKind = burst.kind;
                if (candidate->crcValid) return candidate;
                if (!bestNonCrc || candidate->correctedSymbols < bestNonCrc->correctedSymbols) {
                    bestNonCrc = candidate;
                }
            }
            return bestNonCrc;
        };

        if (auto pdu = selectMacPdu()) {
            burst.macFecDecoded = pdu->fecDecoded;
            burst.macCrcValid = pdu->crcValid;
            burst.macCrcLock = burst.macCrcValid;
            if (burst.macCrcValid) burst.phase2AudioLock = true;
            if (pdu->crcValid && session) {
                // Match sdrtrunk's Phase 2 message processor session semantics:
                // MAC_PTT starts a current-call ESS context; END_PTT, IDLE, and
                // HANGTIME terminate/clear it.  Keeping old ESS across MAC_IDLE or
                // MAC_HANGTIME leaves stale clear/encrypted state attached to the
                // next talkspurt and was one source of late, random Phase-2 audio.
                const uint8_t macType = static_cast<uint8_t>(pdu->opcode & 0x07u);
                session->maskPhaseMacCrcSeen = true;
                switch (macType) {
                    case 1: // MAC_PTT
                        session->macCrcSeen = true;
                        session->pttSeen = true;
                        session->activeSeen = false;
                        session->endPttSeen = false;
                        session->idleSeen = false;
                        session->hangtimeSeen = false;
                        session->ess = pdu->ess;
                        session->essTrusted = pdu->ess.known && pdu->ess.fecValidated;
                        session->first4vSlot = static_cast<int>((phase2LogicalVoiceSequenceIndex(superframeBurstIndex) + pdu->offset + 1) % 5);
                        phase2ResetEssFragments(*session);
                        break;
                    case 2: // MAC_END_PTT
                        phase2ClearCallSession(*session);
                        session->endPttSeen = true;
                        break;
                    case 3: // MAC_IDLE
                        phase2ClearCallSession(*session);
                        session->idleSeen = true;
                        break;
                    case 6: // MAC_HANGTIME
                        phase2ClearCallSession(*session);
                        session->hangtimeSeen = true;
                        break;
                    case 4: // MAC_ACTIVE
                        session->activeSeen = true;
                        session->first4vSlot = pdu->offset > 4 ? 0 : pdu->offset;
                        break;
                    default:
                        break;
                }
            }
            if (macPdus) macPdus->push_back(std::move(*pdu));
        }
    }

    if (phase2BurstKindHasVoice(burst.kind)) {
        const auto& voicePayloadDibits = canApplyMask ? descrambledPayloadDibits : rawPayloadDibits;
        if (session && burst.xorMaskApplied && superframeLocked) {
            int currentSlot = phase2LogicalVoiceSequenceIndex(superframeBurstIndex);
            if (session->first4vSlot >= 0) {
                if (currentSlot < session->first4vSlot) currentSlot += 5;
                const int burstId = currentSlot - session->first4vSlot;
                if (auto ess = decodePhase2VoiceEss(voicePayloadDibits, burstId, *session)) {
                    phase2AcceptVoiceEss(*session, *ess);
                }
            } else {
                // Late-entry soft ESS acquisition. When no MAC PTT PDU is available
                // in the current capture window, the first 4V slot is unknown. Try
                // all legal first-4V hypotheses using persistent per-hypothesis B
                // fragments. A validated clear ESS is strong enough to release
                // late-entry audio even if the MAC PTT was missed in this window.
                for (int candidateFirst4v = 0; candidateFirst4v < 5; ++candidateFirst4v) {
                    int hypothesisSlot = currentSlot;
                    if (hypothesisSlot < candidateFirst4v) hypothesisSlot += 5;
                    const int burstId = hypothesisSlot - candidateFirst4v;
                    if (burstId < 0 || burstId > 4) continue;

                    Phase2SessionState hyp;
                    hyp.ess = session->essHypotheses[static_cast<size_t>(candidateFirst4v)];
                    hyp.essB = session->essBHypotheses[static_cast<size_t>(candidateFirst4v)];
                    hyp.essBSeen = session->essBSeenHypotheses[static_cast<size_t>(candidateFirst4v)];
                    hyp.first4vSlot = candidateFirst4v;

                    bool acceptedEss = false;
                    if (auto ess = decodePhase2VoiceEss(voicePayloadDibits, burstId, hyp)) {
                        acceptedEss = phase2AcceptVoiceEss(hyp, *ess);
                    }

                    session->essHypotheses[static_cast<size_t>(candidateFirst4v)] = hyp.ess;
                    session->essBHypotheses[static_cast<size_t>(candidateFirst4v)] = hyp.essB;
                    session->essBSeenHypotheses[static_cast<size_t>(candidateFirst4v)] = hyp.essBSeen;
                    if (acceptedEss && hyp.essTrusted) {
                        session->ess = hyp.ess;
                        session->essTrusted = true;
                        session->essB = hyp.essB;
                        session->essBSeen = hyp.essBSeen;
                        session->tentativeEss = {};
                        session->tentativeEssRepeats = 0;
                        session->first4vSlot = candidateFirst4v;
                        break;
                    }
                }
            }
        }

        // sdrtrunk Voice4Timeslot/Voice2Timeslot extracts AMBE from timeslot
        // bit starts 2, 76, 172, 246.  In dibits within the 320-bit timeslot
        // that is 1, 38, 86, 123.  Do not add the old half-ISCH +10 offset.
        const std::array<size_t, 4> starts{1, 38, 86, 123};
        const size_t count = burst.kind == P25Phase2BurstKind::Voice4 ? 4u : 2u;
        for (size_t i = 0; i < count; ++i) {
            const size_t start = starts[i];
            if (start + 36 > voicePayloadDibits.size()) continue;
            P25Phase2VoiceCodeword cw;
            cw.dibitOffset = payload + start;
            cw.voiceIndex = static_cast<uint8_t>(i);
            for (size_t d = 0; d < 36; ++d) {
                const auto bits = P25LiveDecoder::bitsFromDibit(voicePayloadDibits[start + d] & 0x03);
                cw.bits[d * 2] = bits[0];
                cw.bits[d * 2 + 1] = bits[1];
            }
            burst.voiceCodewords.push_back(cw);
        }
    }

    if (session) {
        burst.essKnown = session->ess.known && session->essTrusted;
        burst.encrypted = burst.essKnown && session->ess.encrypted;
        burst.macCrcLock = session->pttSeen || session->activeSeen || burst.macCrcValid;
        burst.phase2AudioLock = session->pttSeen || session->activeSeen ||
            (burst.essKnown && session->ess.fecValidated);
    }
    burst.sessionAudioRelease =
        burst.xorMaskApplied &&
        burst.essKnown &&
        !burst.encrypted &&
        (session && (session->pttSeen || (session->essTrusted && session->ess.fecValidated)));

    return burst;
}

struct Phase2MaskPhaseWindow {
    uint8_t phase = 0;
    int score = 0;
    size_t macCrcValid = 0;
    size_t macFecDecoded = 0;
    bool essKnown = false;
    std::array<Phase2SessionState, 2> sessions{};
    std::vector<P25Phase2Burst> bursts;
    std::vector<P25Phase2MacPdu> macPdus;
};

Phase2MaskPhaseWindow scorePhase2MaskPhaseWindow(const std::vector<int>& dibits,
                                                 const std::vector<Phase2SyncHit>& hits,
                                                 const Phase2SuperframeLock& lock,
                                                 const std::array<int, P25LiveDecoder::Phase2BurstDibits * 12>* mask,
                                                 uint8_t phase,
                                                 const std::array<Phase2SessionState, 2>& seedSessions,
                                                 bool stickyPhase)
{
    Phase2MaskPhaseWindow window;
    window.phase = static_cast<uint8_t>(phase % 12u);
    window.sessions = seedSessions;
    window.bursts.reserve(12);

    for (size_t slot = 0; slot < 12; ++slot) {
        const size_t expectedPos = lock.dibitOffset + slot * P25LiveDecoder::Phase2BurstDibits;
        if (expectedPos + P25LiveDecoder::Phase2BurstDibits > dibits.size()) break;
        const auto hit = phase2SyncHitNear(hits, expectedPos, 2);
        const size_t pos = hit ? hit->dibitOffset : expectedPos;
        if (pos + P25LiveDecoder::Phase2BurstDibits > dibits.size()) continue;
        auto& slotSession = window.sessions[phase2TrafficSlotFromSuperframeBurstIndex(slot) & 0x01u];
        auto burst = decodePhase2BurstAt(dibits, pos, hit ? hit->errors : -1,
                                         true, lock.dibitOffset, slot,
                                         lock.syncScore, lock.syncErrors,
                                         mask, window.phase, 0,
                                         &slotSession, &window.macPdus,
                                         false);
        if (!burst.valid) continue;
        if (hit && hit->dibitOffset != expectedPos) {
            burst.syncOffsetAdjusted = true;
            burst.syncOffsetDibits = phase2SignedSyncSlipDibits(hit->dibitOffset, expectedPos);
        }
        window.bursts.push_back(std::move(burst));
    }

    window.essKnown = std::any_of(window.sessions.begin(), window.sessions.end(), [](const Phase2SessionState& session) {
        return session.ess.known && session.essTrusted;
    });
    for (const auto& pdu : window.macPdus) {
        if (pdu.fecDecoded) ++window.macFecDecoded;
        if (pdu.crcValid) ++window.macCrcValid;
    }

    // Hard evidence dominates: valid MAC CRC/ESS means the XOR phase and superframe epoch agree.
    // Soft evidence keeps stable prior phases from bouncing when a block has voice-only bursts.
    window.score = lock.syncScore * 20 - lock.syncErrors * 2;
    if (stickyPhase) window.score += 35;
    window.score += static_cast<int>(window.macFecDecoded) * 8;
    window.score += static_cast<int>(window.macCrcValid) * 500;
    if (window.essKnown) window.score += 900;

    for (const auto& burst : window.bursts) {
        if (burst.duidErrors == 0) window.score += 2;
        else if (burst.duidErrors > 1) window.score -= 8;
        if (burst.isch.valid) {
            window.score += burst.isch.sync ? 6 : 2;

            // sdrtrunk anchors the scrambling segment to the decoded I-ISCH
            // fragment location: ISCH_1 => mask slots 0..3, ISCH_2 => 4..7,
            // ISCH_3 => 8..11.  When traffic MAC/ESS is absent, the previous
            // implementation could pick an arbitrary xorMaskPhase because all
            // voice-only phases produced the same VCW count.  That gives the
            // exact field symptom of strong p2sf/p2mask counters but AMBE
            // frames that never decode.  Use I-ISCH as the standards-level soft
            // phase anchor, while still allowing MAC CRC/ESS to dominate above.
            if (!burst.isch.sync && burst.isch.location <= 2 && burst.superframeBurstIndexKnown) {
                const size_t burstIndex = static_cast<size_t>(burst.superframeBurstIndex % 12u);
                const size_t expectedMaskIndex = static_cast<size_t>(burst.isch.location) * 4u + (burstIndex % 4u);
                const size_t actualMaskIndex = (burstIndex + static_cast<size_t>(window.phase)) % 12u;
                const bool lowErrorIsch = burst.isch.errors >= 0 && burst.isch.errors < 3;
                const bool channelMatchesGrantSlot = !burst.grantSlotKnown ||
                    static_cast<uint8_t>(burst.isch.channel & 0x01u) == static_cast<uint8_t>(burst.grantSlot & 0x01u);
                window.score += (actualMaskIndex == expectedMaskIndex && channelMatchesGrantSlot)
                    ? (lowErrorIsch ? 140 : 55)
                    : (lowErrorIsch ? -120 : -40);
            }
        }
        if (burst.macCrcValid) window.score += 120;
        if (burst.essKnown) window.score += 80;
        window.score += static_cast<int>(std::min<size_t>(burst.voiceCodewords.size(), 4));
    }

    return window;
}

bool betterPhase2MaskPhaseWindow(const Phase2MaskPhaseWindow& a, const Phase2MaskPhaseWindow& b)
{
    if (a.macCrcValid != b.macCrcValid) return a.macCrcValid > b.macCrcValid;
    if (a.essKnown != b.essKnown) return a.essKnown;
    if (a.score != b.score) return a.score > b.score;
    if (a.macFecDecoded != b.macFecDecoded) return a.macFecDecoded > b.macFecDecoded;
    return a.phase < b.phase;
}

void appendBitsFromDibits(std::vector<uint8_t>& bits, const std::vector<int>& dibits)
{
    bits.reserve(bits.size() + dibits.size() * 2);
    for (int d : dibits) {
        const auto pair = P25LiveDecoder::bitsFromDibit(d);
        bits.push_back(pair[0]);
        bits.push_back(pair[1]);
    }
}

std::vector<int> bitsToDibits(const std::vector<uint8_t>& bits, bool invert)
{
    std::vector<int> dibits;
    dibits.reserve(bits.size() / 2);
    for (size_t i = 0; i + 1 < bits.size(); i += 2) {
        uint8_t hi = bits[i] ? 1u : 0u;
        uint8_t lo = bits[i + 1] ? 1u : 0u;
        if (invert) {
            hi ^= 1u;
            lo ^= 1u;
        }
        dibits.push_back(P25LiveDecoder::dibitFromBits(hi, lo));
    }
    return dibits;
}

std::vector<uint8_t> dibitsToBytes(const std::vector<int>& dibits)
{
    if (dibits.size() % 4 != 0) return {};
    std::vector<uint8_t> out(dibits.size() / 4, 0);
    for (size_t i = 0; i < dibits.size(); ++i) {
        out[i / 4] = static_cast<uint8_t>((out[i / 4] << 2) | (dibits[i] & 0x03));
    }
    return out;
}

uint32_t degree64(uint64_t x)
{
    uint32_t degree = 0;
    while (x >>= 1u) ++degree;
    return degree;
}

void crcDivide(uint64_t& word)
{
    constexpr uint64_t gen = 0b10001000000100001ull;
    while (word != 0) {
        const int diff = static_cast<int>(degree64(word)) - static_cast<int>(degree64(gen));
        if (diff < 0) break;
        word ^= gen << diff;
    }
}

uint16_t p25Crc16(const std::vector<uint8_t>& bytes, size_t count)
{
    uint64_t word = 0;
    const size_t n = std::min(count, bytes.size());
    for (size_t i = 0; i < n; ++i) {
        word = (word << 8) | bytes[i];
        crcDivide(word);
    }
    for (int i = 0; i < 16; ++i) {
        word <<= 1;
        crcDivide(word);
    }
    return static_cast<uint16_t>((word ^ 0xffffu) & 0xffffu);
}

bool p25TsbkCrcValid(const std::vector<uint8_t>& bytes)
{
    if (bytes.size() != 12) return false;
    const uint16_t transmitted = static_cast<uint16_t>((static_cast<uint16_t>(bytes[10]) << 8) | bytes[11]);
    return transmitted == p25Crc16(bytes, 10);
}

struct StatusSplit {
    std::vector<int> dataDibits;
    std::vector<int> statusDibits;
};

StatusSplit splitStatusDibitsAfterFrameSync(const std::vector<int>& dibits, size_t firstPayloadDibit)
{
    StatusSplit split;
    if (firstPayloadDibit >= dibits.size()) return split;

    int pos = 24; // frame sync is 24 symbols and counts toward the first status period.
    for (size_t i = firstPayloadDibit; i < dibits.size(); ++i) {
        pos = (pos + 1) % 36; // 35 data dibits, then one status dibit.
        if (pos == 0) split.statusDibits.push_back(dibits[i] & 0x03);
        else split.dataDibits.push_back(dibits[i] & 0x03);
    }
    return split;
}

uint64_t dibitsToWordMsb(const std::vector<int>& dibits, size_t startDibit, size_t count)
{
    if (count == 0 || count > 32 || startDibit + count > dibits.size()) return 0;
    uint64_t out = 0;
    for (size_t i = 0; i < count; ++i) {
        out = (out << 2) | static_cast<uint64_t>(dibits[startDibit + i] & 0x03);
    }
    return out;
}

constexpr std::array<size_t, 98> kP25TsbkDeinterleave = {
    0, 1, 26, 27, 50, 51, 74, 75,
    2, 3, 28, 29, 52, 53, 76, 77,
    4, 5, 30, 31, 54, 55, 78, 79,
    6, 7, 32, 33, 56, 57, 80, 81,
    8, 9, 34, 35, 58, 59, 82, 83,
    10, 11, 36, 37, 60, 61, 84, 85,
    12, 13, 38, 39, 62, 63, 86, 87,
    14, 15, 40, 41, 64, 65, 88, 89,
    16, 17, 42, 43, 66, 67, 90, 91,
    18, 19, 44, 45, 68, 69, 92, 93,
    20, 21, 46, 47, 70, 71, 94, 95,
    22, 23, 48, 49, 72, 73, 96, 97,
    24, 25
};

std::vector<int> deinterleaveTsbkCodedDibits(const std::vector<int>& coded)
{
    if (coded.size() != kP25TsbkDeinterleave.size()) return {};
    std::vector<int> out;
    out.reserve(kP25TsbkDeinterleave.size());
    for (size_t idx : kP25TsbkDeinterleave) out.push_back(coded[idx] & 0x03);
    return out;
}

std::array<int, 2> p25TrellisPair(int cur, int next)
{
    static constexpr int pairIndexes[4][4] = {
        {0, 15, 12, 3},
        {4, 11, 8, 7},
        {13, 2, 1, 14},
        {9, 6, 5, 10},
    };
    static constexpr int pairs[16][2] = {
        {0b00, 0b10}, {0b10, 0b10}, {0b01, 0b11}, {0b11, 0b11},
        {0b11, 0b10}, {0b01, 0b10}, {0b10, 0b11}, {0b00, 0b11},
        {0b11, 0b01}, {0b01, 0b01}, {0b10, 0b00}, {0b00, 0b00},
        {0b00, 0b01}, {0b10, 0b01}, {0b01, 0b00}, {0b11, 0b00},
    };
    const int idx = pairIndexes[cur & 0x03][next & 0x03];
    return {pairs[idx][0], pairs[idx][1]};
}

int dibitDistance(int a, int b)
{
    int x = (a ^ b) & 0x03;
    return (x & 0x01) + ((x >> 1) & 0x01);
}

struct TrellisDecodeResult {
    bool ok = false;
    std::vector<int> dibits;
    int correctedDibitErrors = 0;
};

TrellisDecodeResult decodeP25HalfRateTrellis(const std::vector<int>& encoded)
{
    TrellisDecodeResult result;
    if (encoded.size() != 98) return result;

    struct Path {
        int cost = 1000000;
        std::array<int, 49> states{};
    };

    std::array<Path, 4> paths{};
    paths[0].cost = 0;
    for (int s = 1; s < 4; ++s) paths[s].cost = 1000000;

    for (size_t edge = 0; edge < 49; ++edge) {
        std::array<Path, 4> nextPaths{};
        for (auto& p : nextPaths) p.cost = 1000000;

        const int rxHi = encoded[edge * 2] & 0x03;
        const int rxLo = encoded[edge * 2 + 1] & 0x03;
        for (int cur = 0; cur < 4; ++cur) {
            if (paths[cur].cost >= 1000000) continue;
            for (int next = 0; next < 4; ++next) {
                const auto expected = p25TrellisPair(cur, next);
                const int cost = paths[cur].cost
                    + dibitDistance(rxHi, expected[0])
                    + dibitDistance(rxLo, expected[1]);
                if (cost < nextPaths[next].cost) {
                    nextPaths[next] = paths[cur];
                    nextPaths[next].cost = cost;
                    nextPaths[next].states[edge] = next;
                }
            }
        }
        paths = nextPaths;
    }

    int bestState = 0;
    if (paths[0].cost >= 1000000) {
        for (int s = 1; s < 4; ++s) {
            if (paths[s].cost < paths[bestState].cost) bestState = s;
        }
    }
    if (paths[bestState].cost >= 1000000) return result;

    result.ok = true;
    result.correctedDibitErrors = paths[bestState].cost;
    result.dibits.reserve(48);
    for (size_t i = 0; i < 48; ++i) result.dibits.push_back(paths[bestState].states[i] & 0x03);
    return result;
}

P25TsbkBlock decodeCodedTsbkBlock(const std::vector<int>& onAirCodedDibits, size_t bitOffset)
{
    P25TsbkBlock block;
    block.bitOffset = bitOffset;
    block.crcPresent = true;

    auto deinterleaved = deinterleaveTsbkCodedDibits(onAirCodedDibits);
    auto decoded = decodeP25HalfRateTrellis(deinterleaved);
    if (!decoded.ok || decoded.dibits.size() != 48) return block;

    block.bytes = dibitsToBytes(decoded.dibits);
    block.fecDecoded = block.bytes.size() == 12;
    block.correctedDibitErrors = decoded.correctedDibitErrors;
    block.crcValid = p25TsbkCrcValid(block.bytes);
    return block;
}

struct VoiceZigZag {
    int start = 0;
    int count = 0;
    bool high = true;
};

int dibitBit(int dibit, bool high)
{
    return high ? ((dibit >> 1) & 1) : (dibit & 1);
}

uint32_t descrambleVoiceWord(const std::vector<int>& dibits, int wordIndex)
{
    static const std::array<std::vector<VoiceZigZag>, 8> paths = {
        std::vector<VoiceZigZag>{{0, 23, true}},
        std::vector<VoiceZigZag>{{69, 1, false}, {0, 22, false}},
        std::vector<VoiceZigZag>{{66, 2, false}, {1, 21, true}},
        std::vector<VoiceZigZag>{{64, 3, false}, {1, 20, false}},
        std::vector<VoiceZigZag>{{61, 4, false}, {2, 11, true}},
        std::vector<VoiceZigZag>{{35, 13, false}, {2, 2, false}},
        std::vector<VoiceZigZag>{{8, 15, false}},
        std::vector<VoiceZigZag>{{53, 7, true}},
    };

    if (wordIndex < 0 || wordIndex >= static_cast<int>(paths.size())) return 0;
    uint32_t out = 0;
    for (const auto& segment : paths[static_cast<size_t>(wordIndex)]) {
        int index = segment.start;
        bool high = segment.high;
        for (int i = 0; i < segment.count; ++i) {
            if (index < 0 || index >= static_cast<int>(dibits.size())) return 0;
            out = (out << 1) | static_cast<uint32_t>(dibitBit(dibits[static_cast<size_t>(index)], high));
            index += 3;
            high = !high;
        }
    }
    return out;
}

class P25PseudoRandom {
public:
    explicit P25PseudoRandom(uint16_t seed)
        : m_state(static_cast<uint16_t>((seed & 0x0fffu) << 4))
    {
    }

    uint32_t nextBits(int bits)
    {
        uint32_t out = 0;
        for (int i = 0; i < bits; ++i) {
            m_state = static_cast<uint16_t>(m_state * 173u + 13849u);
            out = (out << 1) | static_cast<uint32_t>((m_state >> 15) & 1u);
        }
        return out;
    }

private:
    uint16_t m_state = 0;
};

void appendWordBits(std::vector<uint8_t>& bits, uint32_t word, int count)
{
    for (int i = count - 1; i >= 0; --i) {
        bits.push_back(static_cast<uint8_t>((word >> i) & 1u));
    }
}

std::array<uint8_t, 11> bitsToPackedImbeBytes(const std::vector<uint8_t>& bits)
{
    std::array<uint8_t, 11> out{};
    for (size_t i = 0; i < bits.size() && i < 88; ++i) {
        out[i / 8] = static_cast<uint8_t>((out[i / 8] << 1) | (bits[i] ? 1u : 0u));
    }
    return out;
}

#ifdef HAVE_MBELIB
uint32_t decodeGolay2312Data(uint32_t word, int& correctedErrors)
{
    char in[23]{};
    char out[23]{};
    for (int j = 0; j < 23; ++j) in[j] = static_cast<char>((word >> j) & 1u);
    correctedErrors += mbe_golay2312(in, out);

    uint32_t data = 0;
    for (int j = 22; j > 10; --j) data = (data << 1) | static_cast<uint32_t>(out[j] & 1);
    return data & 0x0fffu;
}

uint32_t decodeHamming1511Data(uint32_t word, int& correctedErrors)
{
    char in[15]{};
    char out[15]{};
    for (int j = 0; j < 15; ++j) in[j] = static_cast<char>((word >> j) & 1u);
    correctedErrors += mbe_hamming1511(in, out);

    uint32_t data = 0;
    for (int j = 14; j >= 4; --j) data = (data << 1) | static_cast<uint32_t>(out[j] & 1);
    return data & 0x07ffu;
}
#endif

} // namespace

P25ImbeFrame p25DecodeImbeFrameFromVoiceDibits(const std::vector<int>& voiceFrameDibits)
{
    P25ImbeFrame frame;
    if (voiceFrameDibits.size() != 72) {
        frame.message = "P25 voice frame must contain exactly 72 dibits.";
        return frame;
    }

#ifndef HAVE_MBELIB
    frame.message = "P25 voice frame FEC currently requires mbelib helpers.";
    return frame;
#else
    int correctedErrors = 0;
    const uint32_t u0 = decodeGolay2312Data(descrambleVoiceWord(voiceFrameDibits, 0), correctedErrors);
    P25PseudoRandom prng(static_cast<uint16_t>(u0));

    std::vector<uint8_t> bits;
    bits.reserve(88);
    appendWordBits(bits, u0, 12);

    for (int i = 1; i <= 3; ++i) {
        const uint32_t descrambled = descrambleVoiceWord(voiceFrameDibits, i) ^ prng.nextBits(23);
        appendWordBits(bits, decodeGolay2312Data(descrambled, correctedErrors), 12);
    }
    for (int i = 4; i <= 6; ++i) {
        const uint32_t descrambled = descrambleVoiceWord(voiceFrameDibits, i) ^ prng.nextBits(15);
        appendWordBits(bits, decodeHamming1511Data(descrambled, correctedErrors), 11);
    }
    appendWordBits(bits, descrambleVoiceWord(voiceFrameDibits, 7) & 0x7fu, 7);

    if (bits.size() != 88) {
        frame.message = "P25 voice frame produced an invalid IMBE bit count.";
        return frame;
    }

    frame.valid = true;
    frame.correctedErrors = correctedErrors;
    frame.imbe88 = bitsToPackedImbeBytes(bits);
    frame.message = "decoded";
    return frame;
#endif
}

namespace {
std::array<uint8_t, 72> p25Phase2AmbeVariantBits(const P25Phase2VoiceCodeword& codeword, int variant)
{
    std::array<uint8_t, 72> bits{};
    for (size_t i = 0; i < bits.size(); ++i) bits[i] = codeword.bits[i] ? 1u : 0u;

    switch (variant) {
        case 1:
            // Some demod/dibit paths expose the two TDMA bits in a dibit reversed.
            // ACCH can still work through its own bit-order hypotheses, while AMBE
            // voice extraction previously only tried one ordering.
            for (size_t i = 0; i + 1 < bits.size(); i += 2) std::swap(bits[i], bits[i + 1]);
            break;
        case 2:
            // Whole-frame reversal guard.  Kept as a live-safe fallback for field
            // bring-up; the scorer in main.cpp only keeps it if mbelib produces a
            // sane finite frame.
            std::reverse(bits.begin(), bits.end());
            break;
        case 3:
            // Bit polarity guard.  A wrong hard-decision polarity normally should
            // lose at the DUID/MAC level, but this costs little and helps prove the
            // AMBE side when MAC is still marginal.
            for (auto& b : bits) b ^= 1u;
            break;
        case 4:
            for (size_t i = 0; i + 1 < bits.size(); i += 2) std::swap(bits[i], bits[i + 1]);
            for (auto& b : bits) b ^= 1u;
            break;
        case 5:
            std::reverse(bits.begin(), bits.end());
            for (auto& b : bits) b ^= 1u;
            break;
        default:
            break;
    }
    return bits;
}

std::array<uint8_t, 96> p25Phase2AmbeBitsToMbelibFrame(const std::array<uint8_t, 72>& bits)
{
    // This is the standard AMBE 3600x2450 interleave used by mbelib/OP25-style
    // decoders.  It is equivalent to the rW/rX/rY/rZ schedule used by public
    // AMBE 72-bit deinterleave examples: each pair of TDMA bits is placed into
    // the C0/C1/C2/C3 frame matrix, then mbelib performs the inner AMBE Golay and
    // C1 pseudo-random demodulation.
    static constexpr std::array<int, 72> kRows = {
        0,0,1,2,0,0,1,2,0,0,1,2,0,0,1,2,
        0,0,1,3,0,0,1,3,0,1,1,3,0,1,1,3,
        0,1,1,3,0,1,1,3,0,1,1,3,0,1,2,3,
        0,1,2,3,0,1,2,3,0,1,2,3,0,1,2,3,
        0,1,2,3,0,1,2,3,
    };
    static constexpr std::array<int, 24> kCols0 = {
        23,5,22,4,21,3,20,2,19,1,18,0,
        17,16,15,14,13,12,11,10,9,8,7,6,
    };
    static constexpr std::array<int, 23> kCols1 = {
        10,9,8,7,6,5,22,4,21,3,20,2,
        19,1,18,0,17,16,15,14,13,12,11,
    };
    static constexpr std::array<int, 11> kCols2 = {
        3,2,1,0,10,9,8,7,6,5,4,
    };
    static constexpr std::array<int, 14> kCols3 = {
        13,12,11,10,9,8,7,6,5,4,3,2,1,0,
    };

    std::array<uint8_t, 96> out{};
    std::array<size_t, 4> nextCol{};
    for (size_t i = 0; i < bits.size(); ++i) {
        const int row = kRows[i];
        int col = 0;
        switch (row) {
            case 0: col = kCols0[nextCol[0]++]; break;
            case 1: col = kCols1[nextCol[1]++]; break;
            case 2: col = kCols2[nextCol[2]++]; break;
            case 3: col = kCols3[nextCol[3]++]; break;
            default: continue;
        }
        out[static_cast<size_t>(row) * 24u + static_cast<size_t>(col)] = bits[i] ? 1u : 0u;
    }
    return out;
}
} // namespace

int p25Phase2AmbeFrameVariantCount()
{
    return 6;
}

std::array<uint8_t, 96> p25Phase2VoiceCodewordToAmbe3600x2450FrameVariant(const P25Phase2VoiceCodeword& codeword, int variant)
{
    return p25Phase2AmbeBitsToMbelibFrame(p25Phase2AmbeVariantBits(codeword, variant));
}

std::array<uint8_t, 96> p25Phase2VoiceCodewordToAmbe3600x2450Frame(const P25Phase2VoiceCodeword& codeword)
{
    return p25Phase2VoiceCodewordToAmbe3600x2450FrameVariant(codeword, 0);
}


P25LiveDecoder::P25LiveDecoder(P25LiveDecoderConfig config)
    : m_config(config)
{
    if (!std::isfinite(m_config.symbolRate) || m_config.symbolRate <= 0.0) {
        m_config.symbolRate = SymbolRate;
    }
    if (!std::isfinite(m_config.c4fmInnerDeviationHz) || m_config.c4fmInnerDeviationHz <= 0.0) {
        m_config.c4fmInnerDeviationHz = 600.0;
    }
    if (!std::isfinite(m_config.workSampleRate) || m_config.workSampleRate < m_config.symbolRate * 8.0) {
        m_config.workSampleRate = 48000.0;
    }
    if (!std::isfinite(m_config.channelBandwidthHz) || m_config.channelBandwidthHz <= 0.0) {
        m_config.channelBandwidthHz = 12500.0;
    }
    m_config.maxC4fmFixedPhaseCandidates = std::min<size_t>(m_config.maxC4fmFixedPhaseCandidates, 16);
}

P25LiveDecoder::~P25LiveDecoder()
{
    eraseStreamingState(this);
}

P25LiveDecoder::P25LiveDecoder(const P25LiveDecoder& other)
    : m_config(other.m_config),
      m_phase2MaskParams(other.m_phase2MaskParams),
      m_phase2XorMask(other.m_phase2XorMask),
      m_phase2Ess(other.m_phase2Ess),
      m_phase2EssB(other.m_phase2EssB),
      m_phase2EssBSeen(other.m_phase2EssBSeen),
      m_phase2SessionMacCrcSeen(other.m_phase2SessionMacCrcSeen),
      m_phase2First4vSlot(other.m_phase2First4vSlot),
      m_phase2EssHypotheses(other.m_phase2EssHypotheses),
      m_phase2EssBHypotheses(other.m_phase2EssBHypotheses),
      m_phase2EssBSeenHypotheses(other.m_phase2EssBSeenHypotheses),
      m_phase2SlotEss(other.m_phase2SlotEss),
      m_phase2SlotEssB(other.m_phase2SlotEssB),
      m_phase2SlotEssBSeen(other.m_phase2SlotEssBSeen),
      m_phase2SlotSessionMacCrcSeen(other.m_phase2SlotSessionMacCrcSeen),
      m_phase2SlotFirst4vSlot(other.m_phase2SlotFirst4vSlot),
      m_phase2SlotEssHypotheses(other.m_phase2SlotEssHypotheses),
      m_phase2SlotEssBHypotheses(other.m_phase2SlotEssBHypotheses),
      m_phase2SlotEssBSeenHypotheses(other.m_phase2SlotEssBSeenHypotheses),
      m_phase2MaskPhaseKnown(other.m_phase2MaskPhaseKnown),
      m_phase2MaskPhase(other.m_phase2MaskPhase),
      m_phase2MaskPhaseScore(other.m_phase2MaskPhaseScore),
      m_phase2SuperframeAnchorKnown(other.m_phase2SuperframeAnchorKnown),
      m_phase2SuperframeAnchorDibit(other.m_phase2SuperframeAnchorDibit),
      m_phase2SuperframeAnchorGeneration(other.m_phase2SuperframeAnchorGeneration),
      m_phase2RecentCodewords(other.m_phase2RecentCodewords),
      m_phase2DibitTail(other.m_phase2DibitTail),
      m_phase2NextCodewordId(other.m_phase2NextCodewordId),
      m_phase2DecodeGeneration(other.m_phase2DecodeGeneration),
      m_phase2StreamDibits(other.m_phase2StreamDibits),
      m_cqpskLock(other.m_cqpskLock)
{
}

P25LiveDecoder& P25LiveDecoder::operator=(const P25LiveDecoder& other)
{
    if (this == &other) return *this;
    eraseStreamingState(this);
    m_config = other.m_config;
    m_phase2MaskParams = other.m_phase2MaskParams;
    m_phase2XorMask = other.m_phase2XorMask;
    m_phase2Ess = other.m_phase2Ess;
    m_phase2EssB = other.m_phase2EssB;
    m_phase2EssBSeen = other.m_phase2EssBSeen;
    m_phase2SessionMacCrcSeen = other.m_phase2SessionMacCrcSeen;
    m_phase2First4vSlot = other.m_phase2First4vSlot;
    m_phase2EssHypotheses = other.m_phase2EssHypotheses;
    m_phase2EssBHypotheses = other.m_phase2EssBHypotheses;
    m_phase2EssBSeenHypotheses = other.m_phase2EssBSeenHypotheses;
    m_phase2SlotEss = other.m_phase2SlotEss;
    m_phase2SlotEssB = other.m_phase2SlotEssB;
    m_phase2SlotEssBSeen = other.m_phase2SlotEssBSeen;
    m_phase2SlotSessionMacCrcSeen = other.m_phase2SlotSessionMacCrcSeen;
    m_phase2SlotFirst4vSlot = other.m_phase2SlotFirst4vSlot;
    m_phase2SlotEssHypotheses = other.m_phase2SlotEssHypotheses;
    m_phase2SlotEssBHypotheses = other.m_phase2SlotEssBHypotheses;
    m_phase2SlotEssBSeenHypotheses = other.m_phase2SlotEssBSeenHypotheses;
    m_phase2MaskPhaseKnown = other.m_phase2MaskPhaseKnown;
    m_phase2MaskPhase = other.m_phase2MaskPhase;
    m_phase2MaskPhaseScore = other.m_phase2MaskPhaseScore;
    m_phase2SuperframeAnchorKnown = other.m_phase2SuperframeAnchorKnown;
    m_phase2SuperframeAnchorDibit = other.m_phase2SuperframeAnchorDibit;
    m_phase2SuperframeAnchorGeneration = other.m_phase2SuperframeAnchorGeneration;
    m_phase2RecentCodewords = other.m_phase2RecentCodewords;
    m_phase2DibitTail = other.m_phase2DibitTail;
    m_phase2NextCodewordId = other.m_phase2NextCodewordId;
    m_phase2DecodeGeneration = other.m_phase2DecodeGeneration;
    m_phase2StreamDibits = other.m_phase2StreamDibits;
    m_cqpskLock = other.m_cqpskLock;
    return *this;
}

P25LiveDecoder::P25LiveDecoder(P25LiveDecoder&& other) noexcept
    : m_config(other.m_config),
      m_phase2MaskParams(other.m_phase2MaskParams),
      m_phase2XorMask(other.m_phase2XorMask),
      m_phase2Ess(other.m_phase2Ess),
      m_phase2EssB(other.m_phase2EssB),
      m_phase2EssBSeen(other.m_phase2EssBSeen),
      m_phase2SessionMacCrcSeen(other.m_phase2SessionMacCrcSeen),
      m_phase2First4vSlot(other.m_phase2First4vSlot),
      m_phase2EssHypotheses(other.m_phase2EssHypotheses),
      m_phase2EssBHypotheses(other.m_phase2EssBHypotheses),
      m_phase2EssBSeenHypotheses(other.m_phase2EssBSeenHypotheses),
      m_phase2SlotEss(other.m_phase2SlotEss),
      m_phase2SlotEssB(other.m_phase2SlotEssB),
      m_phase2SlotEssBSeen(other.m_phase2SlotEssBSeen),
      m_phase2SlotSessionMacCrcSeen(other.m_phase2SlotSessionMacCrcSeen),
      m_phase2SlotFirst4vSlot(other.m_phase2SlotFirst4vSlot),
      m_phase2SlotEssHypotheses(other.m_phase2SlotEssHypotheses),
      m_phase2SlotEssBHypotheses(other.m_phase2SlotEssBHypotheses),
      m_phase2SlotEssBSeenHypotheses(other.m_phase2SlotEssBSeenHypotheses),
      m_phase2MaskPhaseKnown(other.m_phase2MaskPhaseKnown),
      m_phase2MaskPhase(other.m_phase2MaskPhase),
      m_phase2MaskPhaseScore(other.m_phase2MaskPhaseScore),
      m_phase2SuperframeAnchorKnown(other.m_phase2SuperframeAnchorKnown),
      m_phase2SuperframeAnchorDibit(other.m_phase2SuperframeAnchorDibit),
      m_phase2SuperframeAnchorGeneration(other.m_phase2SuperframeAnchorGeneration),
      m_phase2RecentCodewords(std::move(other.m_phase2RecentCodewords)),
      m_phase2DibitTail(std::move(other.m_phase2DibitTail)),
      m_phase2NextCodewordId(other.m_phase2NextCodewordId),
      m_phase2DecodeGeneration(other.m_phase2DecodeGeneration),
      m_phase2StreamDibits(other.m_phase2StreamDibits),
      m_cqpskLock(other.m_cqpskLock)
{
    eraseStreamingState(&other);
}

P25LiveDecoder& P25LiveDecoder::operator=(P25LiveDecoder&& other) noexcept
{
    if (this == &other) return *this;
    eraseStreamingState(this);
    m_config = other.m_config;
    m_phase2MaskParams = other.m_phase2MaskParams;
    m_phase2XorMask = other.m_phase2XorMask;
    m_phase2Ess = other.m_phase2Ess;
    m_phase2EssB = other.m_phase2EssB;
    m_phase2EssBSeen = other.m_phase2EssBSeen;
    m_phase2SessionMacCrcSeen = other.m_phase2SessionMacCrcSeen;
    m_phase2First4vSlot = other.m_phase2First4vSlot;
    m_phase2EssHypotheses = other.m_phase2EssHypotheses;
    m_phase2EssBHypotheses = other.m_phase2EssBHypotheses;
    m_phase2EssBSeenHypotheses = other.m_phase2EssBSeenHypotheses;
    m_phase2SlotEss = other.m_phase2SlotEss;
    m_phase2SlotEssB = other.m_phase2SlotEssB;
    m_phase2SlotEssBSeen = other.m_phase2SlotEssBSeen;
    m_phase2SlotSessionMacCrcSeen = other.m_phase2SlotSessionMacCrcSeen;
    m_phase2SlotFirst4vSlot = other.m_phase2SlotFirst4vSlot;
    m_phase2SlotEssHypotheses = other.m_phase2SlotEssHypotheses;
    m_phase2SlotEssBHypotheses = other.m_phase2SlotEssBHypotheses;
    m_phase2SlotEssBSeenHypotheses = other.m_phase2SlotEssBSeenHypotheses;
    m_phase2MaskPhaseKnown = other.m_phase2MaskPhaseKnown;
    m_phase2MaskPhase = other.m_phase2MaskPhase;
    m_phase2MaskPhaseScore = other.m_phase2MaskPhaseScore;
    m_phase2SuperframeAnchorKnown = other.m_phase2SuperframeAnchorKnown;
    m_phase2SuperframeAnchorDibit = other.m_phase2SuperframeAnchorDibit;
    m_phase2SuperframeAnchorGeneration = other.m_phase2SuperframeAnchorGeneration;
    m_phase2RecentCodewords = std::move(other.m_phase2RecentCodewords);
    m_phase2DibitTail = std::move(other.m_phase2DibitTail);
    m_phase2NextCodewordId = other.m_phase2NextCodewordId;
    m_phase2DecodeGeneration = other.m_phase2DecodeGeneration;
    m_phase2StreamDibits = other.m_phase2StreamDibits;
    m_cqpskLock = other.m_cqpskLock;
    eraseStreamingState(&other);
    return *this;
}

uint64_t p25EncodeNidBch(uint16_t nac, P25DataUnitId duid)
{
    const uint16_t data = static_cast<uint16_t>(((nac & 0x0fffu) << 4) |
        (static_cast<uint8_t>(duid) & 0x0fu));
    return encodeP25Bch16(data);
}

void P25LiveDecoder::reset()
{
    m_phase2Ess = {};
    m_phase2EssB = {};
    m_phase2EssBSeen = {};
    m_phase2SessionMacCrcSeen = false;
    m_phase2First4vSlot = -1;
    m_phase2EssHypotheses = {};
    m_phase2EssBHypotheses = {};
    m_phase2EssBSeenHypotheses = {};
    m_phase2SlotEss = {};
    m_phase2SlotEssB = {};
    m_phase2SlotEssBSeen = {};
    m_phase2SlotSessionMacCrcSeen = {};
    m_phase2SlotFirst4vSlot = {-1, -1};
    m_phase2SlotEssHypotheses = {};
    m_phase2SlotEssBHypotheses = {};
    m_phase2SlotEssBSeenHypotheses = {};
    m_phase2MaskPhaseKnown = false;
    m_phase2MaskPhase = 0;
    m_phase2MaskPhaseScore = 0;
    m_phase2SuperframeAnchorKnown = false;
    m_phase2SuperframeAnchorDibit = 0;
    m_phase2SuperframeAnchorGeneration = 0;
    m_phase2RecentCodewords.clear();
    m_phase2DibitTail.clear();
    m_phase2NextCodewordId = 1;
    m_phase2DecodeGeneration = 0;
    m_phase2StreamDibits = 0;
    m_cqpskLock = {};
    eraseStreamingState(this);
}

void P25LiveDecoder::setPhase2MaskParameters(uint16_t nac, uint32_t wacn, uint16_t systemId)
{
    P25Phase2MaskParameters params;
    params.valid = true;
    params.nac = static_cast<uint16_t>(nac & 0x0fffu);
    params.wacn = wacn & 0x000fffffu;
    params.systemId = static_cast<uint16_t>(systemId & 0x0fffu);
    if (m_phase2MaskParams.valid &&
        m_phase2MaskParams.nac == params.nac &&
        m_phase2MaskParams.wacn == params.wacn &&
        m_phase2MaskParams.systemId == params.systemId) {
        return;
    }
    m_phase2MaskParams = params;
    m_phase2XorMask = makePhase2XorMaskDibits(params.nac, params.wacn, params.systemId);
    reset();
}

void P25LiveDecoder::clearPhase2MaskParameters()
{
    m_phase2MaskParams = {};
    m_phase2XorMask = {};
    reset();
}

std::array<int, P25LiveDecoder::Phase2BurstDibits * 12> P25LiveDecoder::phase2XorMaskDibits(uint16_t nac,
                                                                                            uint32_t wacn,
                                                                                            uint16_t systemId)
{
    return makePhase2XorMaskDibits(nac, wacn, systemId);
}

static void restoreSelectedDemodStats(P25LiveDecodeResult& result, const P25LiveDecoderStats& selected)
{
    result.stats.inputSamples = selected.inputSamples;
    result.stats.discriminatorSamples = selected.discriminatorSamples;
    result.stats.sampleRate = selected.sampleRate;
    result.stats.channelSampleRate = selected.channelSampleRate;
    result.stats.inputTargetOffsetHz = selected.inputTargetOffsetHz;
    result.stats.discriminatorMeanHz = selected.discriminatorMeanHz;
    result.stats.symbolRate = selected.symbolRate;
    result.stats.symbolConfidence = selected.symbolConfidence;
    result.stats.softDecisionSymbols = selected.softDecisionSymbols;
    result.stats.softDecisionQuality = selected.softDecisionQuality;
    result.stats.softBitLlrMean = selected.softBitLlrMean;
    result.stats.softBitLlrMinimum = selected.softBitLlrMinimum;
    result.stats.softLowConfidenceSymbols = selected.softLowConfidenceSymbols;
    result.stats.demodPath = selected.demodPath;
    result.stats.cqpskLockActive = selected.cqpskLockActive;
    result.stats.cqpskLockUsed = selected.cqpskLockUsed;
    result.stats.cqpskLockUpdated = selected.cqpskLockUpdated;
    result.stats.cqpskSymbolPhaseFraction = selected.cqpskSymbolPhaseFraction;
    result.stats.cqpskFineCorrectionApplied = selected.cqpskFineCorrectionApplied;
    result.stats.cqpskFineRotationRad = selected.cqpskFineRotationRad;
    result.stats.cqpskResidualCarrierHz = selected.cqpskResidualCarrierHz;
    result.stats.cqpskPhaseErrorRmsRad = selected.cqpskPhaseErrorRmsRad;
    result.stats.cqpskFineCorrectionSymbols = selected.cqpskFineCorrectionSymbols;
    result.stats.cqpskLockTrustScore = selected.cqpskLockTrustScore;
    result.stats.cqpskLockMisses = selected.cqpskLockMisses;
    result.stats.cqpskStickyOverride = selected.cqpskStickyOverride;
}

P25LiveDecodeResult P25LiveDecoder::processIq(const std::vector<std::complex<float>>& iq,
                                              double sampleRate,
                                              double centerFreqHz,
                                              double targetFreqHz)
{
    P25LiveDecodeResult best;
    const double inputTargetOffsetHz = targetFreqHz - centerFreqHz;
    auto channel = channelizeP25Iq(iq, sampleRate, centerFreqHz, targetFreqHz, m_config);
    if (channel.samples.empty() && !iq.empty()) {
        best.stats.inputSamples = iq.size();
        best.stats.inputTargetOffsetHz = inputTargetOffsetHz;
        best.warnings.push_back("P25 target is outside the sampled RF passband or IQ block is too short.");
        return best;
    }
    auto stampCommonStats = [&](P25LiveDecodeResult& result) {
        result.stats.inputSamples = iq.size();
        result.stats.channelSampleRate = channel.sampleRate;
        result.stats.inputTargetOffsetHz = inputTargetOffsetHz;
    };

    auto streamState = loadStreamingState(this);
    auto timingStateStorage = streamState.timing;

    auto fm = fmDiscriminatorFromChannel(channel.samples, channel.sampleRate);
    auto c4fm = processFmDiscriminatorInternal(fm.hz, fm.sampleRate, false);
    stampCommonStats(c4fm);
    c4fm.stats.discriminatorMeanHz = fm.meanHz;
    if (c4fm.stats.demodPath.empty()) c4fm.stats.demodPath = "C4FM";
    best = std::move(c4fm);

    // processFmDiscriminatorInternal updates the same persistent timing state for C4FM.
    // Reload before CQPSK recovery so the final save below merges with those updates
    // instead of overwriting them with a stale pre-C4FM snapshot.
    streamState = loadStreamingState(this);
    timingStateStorage = streamState.timing;

    const double sps = channel.sampleRate / m_config.symbolRate;
    const int phaseSteps = static_cast<int>(std::clamp(std::ceil(sps * 0.5), 4.0, 6.0));
    const std::array<double, 2> rotations{0.0, kPi * 0.25};
    std::optional<CqpskCandidateParams> selectedCqpskParams;
    std::optional<P25BlockTimingState> selectedCqpskTiming;
    int selectedCqpskTrust = 0;
    std::optional<P25LiveDecodeResult> lockedCqpskCandidate;
    std::optional<CqpskCandidateParams> lockedCqpskParams;
    std::optional<P25BlockTimingState> lockedCqpskTimingState;
    int lockedCqpskTrust = 0;

    auto evaluateCqpsk = [&](const CqpskCandidateParams& params,
                             bool fromLock,
                             P25BlockTimingState& candidateTiming) -> P25LiveDecodeResult {
        P25LiveDecodeResult candidate;
        const double phase = std::clamp(params.symbolPhaseFraction, 0.0, 0.999) * sps;
        auto complexSymbols = recoverComplexSymbols(channel.samples, channel.sampleRate, m_config, phase, &candidateTiming);
        if (complexSymbols.symbols.empty()) return candidate;
        CqpskCandidateParams effectiveParams = params;
        const auto correction = estimateCqpskFineCorrection(complexSymbols.symbols,
                                                            params.differential,
                                                            params.conjugate,
                                                            params.rotation,
                                                            m_config.symbolRate);
        if (correction.valid) {
            effectiveParams.fineRotation = correction.rotationAdjustment;
            effectiveParams.fineCorrectionApplied = std::abs(correction.rotationAdjustment) > 0.01;
            effectiveParams.residualCarrierHz = correction.residualCarrierHz;
            effectiveParams.phaseErrorRmsRad = correction.phaseErrorRmsRad;
            effectiveParams.fineCorrectionSymbols = correction.symbols;
        } else if (fromLock && m_cqpskLock.fineCorrectionSymbols >= 16) {
            effectiveParams.fineRotation = m_cqpskLock.fineRotation;
            effectiveParams.fineCorrectionApplied = std::abs(m_cqpskLock.fineRotation) > 0.01;
            effectiveParams.residualCarrierHz = m_cqpskLock.residualCarrierHz;
            effectiveParams.phaseErrorRmsRad = m_cqpskLock.phaseErrorRmsRad;
            effectiveParams.fineCorrectionSymbols = m_cqpskLock.fineCorrectionSymbols;
        }
        const auto soft = cqpskSymbolsToSoftDibits(
            complexSymbols.symbols,
            effectiveParams.differential,
            effectiveParams.conjugate,
            wrapPhase(effectiveParams.rotation + effectiveParams.fineRotation),
            effectiveParams.permutation);
        candidate = processHardDibitsInternal(soft.dibits, false);
        stampSoftDibitStats(candidate, soft);
        stampCommonStats(candidate);
        candidate.stats.sampleRate = channel.sampleRate;
        candidate.stats.symbolRate = m_config.symbolRate;
        candidate.stats.symbolConfidence = complexSymbols.confidence;
        candidate.stats.demodPath = effectiveParams.differential ? "CQPSK-diff" : "CQPSK-abs";
        candidate.stats.cqpskLockActive = m_cqpskLock.valid;
        candidate.stats.cqpskLockUsed = fromLock;
        candidate.stats.cqpskSymbolPhaseFraction = effectiveParams.symbolPhaseFraction;
        candidate.stats.cqpskFineCorrectionApplied = effectiveParams.fineCorrectionApplied;
        candidate.stats.cqpskFineRotationRad = effectiveParams.fineRotation;
        candidate.stats.cqpskResidualCarrierHz = effectiveParams.residualCarrierHz;
        candidate.stats.cqpskPhaseErrorRmsRad = effectiveParams.phaseErrorRmsRad;
        candidate.stats.cqpskFineCorrectionSymbols = effectiveParams.fineCorrectionSymbols;
        return candidate;
    };

    if (m_cqpskLock.valid) {
        CqpskCandidateParams locked;
        locked.differential = m_cqpskLock.differential;
        locked.conjugate = m_cqpskLock.conjugate;
        locked.rotation = m_cqpskLock.rotation;
        locked.permutation = m_cqpskLock.permutation;
        locked.symbolPhaseFraction = m_cqpskLock.symbolPhaseFraction;
        locked.fineRotation = m_cqpskLock.fineRotation;
        locked.residualCarrierHz = m_cqpskLock.residualCarrierHz;
        locked.phaseErrorRmsRad = m_cqpskLock.phaseErrorRmsRad;
        locked.fineCorrectionSymbols = m_cqpskLock.fineCorrectionSymbols;
        auto lockedTiming = timingStateStorage;
        auto candidate = evaluateCqpsk(locked, true, lockedTiming);
        const int trust = liveResultTrustScore(candidate);
        if (trust > 0) {
            locked.fineRotation = candidate.stats.cqpskFineRotationRad;
            locked.fineCorrectionApplied = candidate.stats.cqpskFineCorrectionApplied;
            locked.residualCarrierHz = candidate.stats.cqpskResidualCarrierHz;
            locked.phaseErrorRmsRad = candidate.stats.cqpskPhaseErrorRmsRad;
            locked.fineCorrectionSymbols = candidate.stats.cqpskFineCorrectionSymbols;
            lockedCqpskParams = locked;
            lockedCqpskTimingState = lockedTiming;
            lockedCqpskTrust = trust;
            lockedCqpskCandidate = candidate;
        }
        if (trust > 0 && betterLiveResult(candidate, best)) {
            selectedCqpskParams = locked;
            selectedCqpskTiming = lockedTiming;
            selectedCqpskTrust = trust;
            best = std::move(candidate);
        }
    }

    bool stopCqpskSearch = m_config.stopCqpskSearchOnHardLock &&
        ((isCqpskPath(best.stats.demodPath) && hasCqpskHardLockEvidence(best)) ||
         hasPhase2FastStopEvidence(best));
    for (int p = 0; p < phaseSteps && !stopCqpskSearch; ++p) {
        const double phase = (static_cast<double>(p) + 0.5) * sps / static_cast<double>(phaseSteps);
        const double phaseFraction = std::clamp(phase / std::max(sps, 1e-9), 0.0, 0.999);
        auto candidateTiming = timingStateStorage;
        if (!m_cqpskLock.valid) candidateTiming.cqpskValid = false;
        auto complexSymbols = recoverComplexSymbols(channel.samples, channel.sampleRate, m_config, phase, &candidateTiming);
        if (complexSymbols.symbols.empty()) continue;

        for (bool differential : {true, false}) {
            if (stopCqpskSearch) break;
            for (bool conjugate : {false, true}) {
                if (stopCqpskSearch) break;
                for (double rotation : rotations) {
                    if (stopCqpskSearch) break;
                    const auto correction = estimateCqpskFineCorrection(complexSymbols.symbols,
                                                                         differential,
                                                                         conjugate,
                                                                         rotation,
                                                                         m_config.symbolRate);
                    for (const auto& perm : cqpskDibitPermutations()) {
                        CqpskCandidateParams params;
                        params.differential = differential;
                        params.conjugate = conjugate;
                        params.rotation = rotation;
                        params.permutation = perm;
                        params.symbolPhaseFraction = phaseFraction;
                        if (correction.valid) {
                            params.fineRotation = correction.rotationAdjustment;
                            params.fineCorrectionApplied = std::abs(correction.rotationAdjustment) > 0.01;
                            params.residualCarrierHz = correction.residualCarrierHz;
                            params.phaseErrorRmsRad = correction.phaseErrorRmsRad;
                            params.fineCorrectionSymbols = correction.symbols;
                        }
                        const auto soft = cqpskSymbolsToSoftDibits(
                            complexSymbols.symbols,
                            differential,
                            conjugate,
                            wrapPhase(rotation + params.fineRotation),
                            perm);
                        auto candidate = processHardDibitsInternal(soft.dibits, false);
                        stampSoftDibitStats(candidate, soft);
                        stampCommonStats(candidate);
                        candidate.stats.sampleRate = channel.sampleRate;
                        candidate.stats.symbolRate = m_config.symbolRate;
                        candidate.stats.symbolConfidence = complexSymbols.confidence;
                        candidate.stats.demodPath = differential ? "CQPSK-diff" : "CQPSK-abs";
                        candidate.stats.cqpskLockActive = m_cqpskLock.valid;
                        candidate.stats.cqpskSymbolPhaseFraction = phaseFraction;
                        candidate.stats.cqpskFineCorrectionApplied = params.fineCorrectionApplied;
                        candidate.stats.cqpskFineRotationRad = params.fineRotation;
                        candidate.stats.cqpskResidualCarrierHz = params.residualCarrierHz;
                        candidate.stats.cqpskPhaseErrorRmsRad = params.phaseErrorRmsRad;
                        candidate.stats.cqpskFineCorrectionSymbols = params.fineCorrectionSymbols;
                        if (betterLiveResult(candidate, best)) {
                            selectedCqpskParams = params;
                            selectedCqpskTiming = candidateTiming;
                            selectedCqpskTrust = liveResultTrustScore(candidate);
                            best = std::move(candidate);
                            if (m_config.stopCqpskSearchOnHardLock && hasCqpskHardLockEvidence(best)) {
                                stopCqpskSearch = true;
                                break;
                            }
                        }
                    }
                }
            }
        }
    }
    const int cqpskMissLimit = static_cast<int>(std::clamp<size_t>(
        m_config.cqpskLockMissTolerance,
        8,
        96));
    if (m_cqpskLock.valid &&
        lockedCqpskCandidate &&
        lockedCqpskParams &&
        lockedCqpskTimingState &&
        isCqpskPath(lockedCqpskCandidate->stats.demodPath) &&
        !isCqpskPath(best.stats.demodPath)) {
        const int lockedHard = liveResultHardEvidenceScore(*lockedCqpskCandidate);
        const int competingHard = liveResultHardEvidenceScore(best);
        const bool lockedStillProven = lockedHard > 0 && hasCqpskHardLockEvidence(*lockedCqpskCandidate);
        const bool competingClearlyStronger = competingHard >= lockedHard + 40;
        if (lockedStillProven && (!competingClearlyStronger || m_cqpskLock.misses + 1 < cqpskMissLimit)) {
            selectedCqpskParams = *lockedCqpskParams;
            selectedCqpskTiming = *lockedCqpskTimingState;
            selectedCqpskTrust = lockedCqpskTrust;
            best = *lockedCqpskCandidate;
            best.stats.cqpskStickyOverride = true;
        }
    }
    if (selectedCqpskParams && isCqpskPath(best.stats.demodPath) &&
        selectedCqpskTrust >= 40 && hasCqpskHardLockEvidence(best)) {
        m_cqpskLock.valid = true;
        m_cqpskLock.differential = selectedCqpskParams->differential;
        m_cqpskLock.conjugate = selectedCqpskParams->conjugate;
        m_cqpskLock.rotation = selectedCqpskParams->rotation;
        m_cqpskLock.permutation = selectedCqpskParams->permutation;
        m_cqpskLock.symbolPhaseFraction = selectedCqpskParams->symbolPhaseFraction;
        m_cqpskLock.fineRotation = selectedCqpskParams->fineRotation;
        m_cqpskLock.residualCarrierHz = selectedCqpskParams->residualCarrierHz;
        m_cqpskLock.phaseErrorRmsRad = selectedCqpskParams->phaseErrorRmsRad;
        m_cqpskLock.fineCorrectionSymbols = selectedCqpskParams->fineCorrectionSymbols;
        m_cqpskLock.trustScore = selectedCqpskTrust;
        m_cqpskLock.misses = 0;
        best.stats.cqpskLockUpdated = true;
        best.stats.cqpskLockActive = true;
        best.stats.cqpskLockTrustScore = m_cqpskLock.trustScore;
        best.stats.cqpskLockMisses = m_cqpskLock.misses;
        best.stats.cqpskSymbolPhaseFraction = m_cqpskLock.symbolPhaseFraction;
        best.stats.cqpskFineCorrectionApplied = std::abs(m_cqpskLock.fineRotation) > 0.01;
        best.stats.cqpskFineRotationRad = m_cqpskLock.fineRotation;
        best.stats.cqpskResidualCarrierHz = m_cqpskLock.residualCarrierHz;
        best.stats.cqpskPhaseErrorRmsRad = m_cqpskLock.phaseErrorRmsRad;
        best.stats.cqpskFineCorrectionSymbols = m_cqpskLock.fineCorrectionSymbols;
    } else if (m_cqpskLock.valid && !isCqpskPath(best.stats.demodPath)) {
        best.stats.cqpskLockActive = true;

        // Do not burn down a proven CQPSK/LSM lock just because a C4FM search
        // window had weak sync telemetry. Require hard non-CQPSK evidence
        // before aging out the lock, otherwise local LSM control channels can
        // bounce between demod paths and lose otherwise valid TSBKs.
        if (hasCqpskHardLockEvidence(best)) {
            if (++m_cqpskLock.misses >= cqpskMissLimit) m_cqpskLock = {};
        }
        best.stats.cqpskLockTrustScore = m_cqpskLock.trustScore;
        best.stats.cqpskLockMisses = m_cqpskLock.misses;
    }
    if (!best.dibits.empty()) {
        const auto selectedStats = best.stats;
        auto committed = processHardDibitsInternal(best.dibits, true);
        restoreSelectedDemodStats(committed, selectedStats);
        best = std::move(committed);
    }
    if (selectedCqpskTiming && isCqpskPath(best.stats.demodPath)) {
        timingStateStorage.cqpskValid = selectedCqpskTiming->cqpskValid;
        timingStateStorage.cqpskOmega = selectedCqpskTiming->cqpskOmega;
        timingStateStorage.cqpskMu = selectedCqpskTiming->cqpskMu;
        timingStateStorage.cqpskSampleRate = selectedCqpskTiming->cqpskSampleRate;
        timingStateStorage.cqpskTail = selectedCqpskTiming->cqpskTail;
    }
    storeTimingState(this, timingStateStorage);
    return best;
}

P25LiveDecodeResult P25LiveDecoder::processFmDiscriminator(const std::vector<float>& discriminatorHz,
                                                           double sampleRate)
{
    return processFmDiscriminatorInternal(discriminatorHz, sampleRate, true);
}

P25LiveDecodeResult P25LiveDecoder::processFmDiscriminatorInternal(const std::vector<float>& discriminatorHz,
                                                                   double sampleRate,
                                                                   bool annotateSessionCodewords)
{
    P25LiveDecodeResult best;
    best.stats.discriminatorSamples = discriminatorHz.size();
    best.stats.sampleRate = sampleRate;
    best.stats.symbolRate = m_config.symbolRate;

    auto streamState = loadStreamingState(this);
    auto timingStateStorage = streamState.timing;
    P25BlockTimingState* timingState = &timingStateStorage;

    const auto symbolCandidates = recoverC4fmSymbolCandidates(discriminatorHz, sampleRate, m_config, timingState);
    if (symbolCandidates.empty()) {
        storeTimingState(this, timingStateStorage);
        return best;
    }

    bool haveCandidate = false;
    double selectedSymbolConfidence = 0.0;
    std::string selectedPath = "C4FM";
    bool stopCandidateSearch = false;
    for (const auto& symbols : symbolCandidates) {
        if (stopCandidateSearch) break;
        for (bool invertDeviation : {false, true}) {
            if (stopCandidateSearch) break;
            for (bool reverseBitOrder : {false, true}) {
                if (stopCandidateSearch) break;
                const std::array<double, 5> wideScaleMultipliers{1.0, 0.85, 1.15, 0.70, 1.30};
                const std::array<double, 3> realtimeScaleMultipliers{1.0, 0.85, 1.15};
                const size_t scaleCount = m_config.realtimeVoiceSearch
                    ? realtimeScaleMultipliers.size()
                    : wideScaleMultipliers.size();
                for (size_t scaleIndex = 0; scaleIndex < scaleCount; ++scaleIndex) {
                    const double scaleMultiplier = m_config.realtimeVoiceSearch
                        ? realtimeScaleMultipliers[scaleIndex]
                        : wideScaleMultipliers[scaleIndex];
                    const auto soft = symbolsToSoftDibits(symbols.symbolsHz,
                                                          m_config,
                                                          invertDeviation,
                                                          reverseBitOrder,
                                                          scaleMultiplier);
                    auto candidate = processHardDibitsInternal(soft.dibits, false);
                    stampSoftDibitStats(candidate, soft);
                    candidate.stats.symbolConfidence = symbols.confidence;
                    if (!haveCandidate || betterLiveResult(candidate, best)) {
                        best = std::move(candidate);
                        selectedSymbolConfidence = symbols.confidence;
                        selectedPath = symbols.path;
                        haveCandidate = true;
                        if (m_config.stopCqpskSearchOnHardLock && hasPhase2FastStopEvidence(best)) {
                            stopCandidateSearch = true;
                            break;
                        }
                    }
                }
                if (stopCandidateSearch) break;
                const std::array<double, 3> wideFixedScales{
                    m_config.c4fmInnerDeviationHz,
                    m_config.c4fmInnerDeviationHz * 0.80,
                    m_config.c4fmInnerDeviationHz * 1.20,
                };
                const std::array<double, 1> realtimeFixedScales{m_config.c4fmInnerDeviationHz};
                const size_t fixedScaleCount = m_config.realtimeVoiceSearch
                    ? realtimeFixedScales.size()
                    : wideFixedScales.size();
                for (size_t fixedScaleIndex = 0; fixedScaleIndex < fixedScaleCount; ++fixedScaleIndex) {
                    const double fixedScale = m_config.realtimeVoiceSearch
                        ? realtimeFixedScales[fixedScaleIndex]
                        : wideFixedScales[fixedScaleIndex];
                    const auto soft = symbolsToSoftDibits(symbols.symbolsHz,
                                                          m_config,
                                                          invertDeviation,
                                                          reverseBitOrder,
                                                          1.0,
                                                          fixedScale);
                    auto candidate = processHardDibitsInternal(soft.dibits, false);
                    stampSoftDibitStats(candidate, soft);
                    candidate.stats.symbolConfidence = symbols.confidence;
                    if (!haveCandidate || betterLiveResult(candidate, best)) {
                        best = std::move(candidate);
                        selectedSymbolConfidence = symbols.confidence;
                        selectedPath = symbols.path;
                        haveCandidate = true;
                        if (m_config.stopCqpskSearchOnHardLock && hasPhase2FastStopEvidence(best)) {
                            stopCandidateSearch = true;
                            break;
                        }
                    }
                }
            }
        }
    }
    best.stats.discriminatorSamples = discriminatorHz.size();
    best.stats.sampleRate = sampleRate;
    best.stats.symbolRate = m_config.symbolRate;
    best.stats.symbolConfidence = selectedSymbolConfidence;
    best.stats.demodPath = selectedPath;
    if (annotateSessionCodewords && !best.dibits.empty()) {
        const auto selectedStats = best.stats;
        auto committed = processHardDibitsInternal(best.dibits, true);
        restoreSelectedDemodStats(committed, selectedStats);
        best = std::move(committed);
    }
    storeTimingState(this, timingStateStorage);
    return best;
}

void P25LiveDecoder::annotatePhase2SessionCodewords(P25Phase2DecodeResult& out,
                                                    const std::vector<int>& dibits)
{
    const uint64_t generation = ++m_phase2DecodeGeneration;
    constexpr uint64_t kRetentionGenerations = 8;
    constexpr size_t kMaxRecentCodewords = 512;
    constexpr size_t kMaxDibitTail = 8192;
    constexpr size_t kMinTrustedOverlap = Phase2BurstDibits / 2;
    constexpr uint64_t kDibitTolerance = 8;

    auto longestTailPrefixOverlap = [&]() -> size_t {
        const size_t n = std::min({kMaxDibitTail, m_phase2DibitTail.size(), dibits.size()});
        if (n == 0) return 0;

        std::vector<int> combined;
        combined.reserve(n * 2 + 1);
        combined.insert(combined.end(), dibits.begin(), dibits.begin() + static_cast<std::ptrdiff_t>(n));
        combined.push_back(-1);
        combined.insert(combined.end(), m_phase2DibitTail.end() - static_cast<std::ptrdiff_t>(n), m_phase2DibitTail.end());

        std::vector<size_t> pi(combined.size(), 0);
        for (size_t i = 1; i < combined.size(); ++i) {
            size_t j = pi[i - 1];
            while (j > 0 && combined[i] != combined[j]) j = pi[j - 1];
            if (combined[i] == combined[j]) ++j;
            pi[i] = j;
        }
        return std::min(pi.back(), n);
    };

    auto fingerprintFor = [](const P25Phase2Burst& burst, const P25Phase2VoiceCodeword& codeword) {
        uint64_t h = 1469598103934665603ull; // FNV-1a
        auto mix = [&](uint64_t v) {
            for (int i = 0; i < 8; ++i) {
                h ^= (v >> (i * 8)) & 0xffu;
                h *= 1099511628211ull;
            }
        };
        mix(static_cast<uint64_t>(burst.kind));
        mix(burst.superframeBurstIndexKnown ? burst.superframeBurstIndex : 0xffu);
        mix(burst.grantSlotKnown ? burst.grantSlot : 0xffu);
        mix(codeword.voiceIndex);
        for (uint8_t bit : codeword.bits) {
            h ^= bit ? 1u : 0u;
            h *= 1099511628211ull;
        }
        return h;
    };

    const size_t overlap = longestTailPrefixOverlap();
    const bool trustedOverlap = overlap >= kMinTrustedOverlap &&
                                static_cast<uint64_t>(overlap) <= m_phase2StreamDibits;
    const uint64_t streamStart = trustedOverlap
        ? m_phase2StreamDibits - static_cast<uint64_t>(overlap)
        : m_phase2StreamDibits;

    for (auto& burst : out.bursts) {
        for (auto& codeword : burst.voiceCodewords) {
            const uint64_t fp = fingerprintFor(burst, codeword);
            // codeword.dibitOffset is already an absolute dibit offset within
            // the decoder input window.  Do not add burst.dibitOffset again; doing
            // so double-counts the burst position and makes the overlap de-dupe
            // randomly drop or replay AMBE frames.
            const uint64_t streamDibit = streamStart +
                static_cast<uint64_t>(codeword.dibitOffset);
            auto it = std::find_if(m_phase2RecentCodewords.begin(), m_phase2RecentCodewords.end(),
                [&](const RecentPhase2Codeword& seen) {
                    const uint64_t distance = streamDibit > seen.streamDibit
                        ? streamDibit - seen.streamDibit
                        : seen.streamDibit - streamDibit;
                    return seen.fingerprint == fp &&
                           distance <= kDibitTolerance &&
                           generation >= seen.generation &&
                           generation - seen.generation <= kRetentionGenerations;
                });
            codeword.sessionCodewordIdKnown = true;
            if (it != m_phase2RecentCodewords.end()) {
                codeword.sessionCodewordId = it->id;
                codeword.duplicateInSession = true;
                it->generation = generation;
            } else {
                codeword.sessionCodewordId = m_phase2NextCodewordId++;
                codeword.duplicateInSession = false;
                m_phase2RecentCodewords.push_back({streamDibit, fp, codeword.sessionCodewordId, generation});
            }
        }
    }

    const uint64_t streamEnd = streamStart + static_cast<uint64_t>(dibits.size());
    if (streamEnd > m_phase2StreamDibits) {
        const uint64_t alreadyCovered = m_phase2StreamDibits > streamStart
            ? m_phase2StreamDibits - streamStart
            : 0;
        const size_t appendFrom = static_cast<size_t>(std::min<uint64_t>(alreadyCovered, dibits.size()));
        for (size_t i = appendFrom; i < dibits.size(); ++i) {
            m_phase2DibitTail.push_back(dibits[i] & 0x03);
        }
        m_phase2StreamDibits = streamEnd;
    }
    while (m_phase2DibitTail.size() > kMaxDibitTail) {
        m_phase2DibitTail.pop_front();
    }

    while (!m_phase2RecentCodewords.empty() &&
           (generation > m_phase2RecentCodewords.front().generation + kRetentionGenerations ||
            m_phase2RecentCodewords.size() > kMaxRecentCodewords)) {
        m_phase2RecentCodewords.pop_front();
    }
}

P25LiveDecodeResult P25LiveDecoder::processHardDibits(const std::vector<int>& dibits)
{
    return processHardDibitsInternal(dibits, true);
}

P25LiveDecodeResult P25LiveDecoder::processHardDibitsInternal(const std::vector<int>& dibits,
                                                              bool annotateSessionCodewords)
{
    P25LiveDecodeResult result;
    result.dibits = dibits;
    appendBitsFromDibits(result.bits, dibits);
    result = processHardBits(result.bits);
    result.dibits = dibits;
    result.stats.symbols = dibits.size();
    if (m_config.enablePhase2Decode) {
        auto phase2 = processPhase2HardDibitsDetailedInternal(dibits, annotateSessionCodewords);
        result.phase2Bursts = std::move(phase2.bursts);
        result.phase2MacPdus = std::move(phase2.macPdus);
        result.phase2Ess = phase2.ess;
    }
    result.stats.phase2Bursts = result.phase2Bursts.size();
    result.stats.phase2VoiceCodewords = phase2VoiceCodewordCount(result);
    result.stats.phase2MacPdus = result.phase2MacPdus.size();
    result.stats.phase2MacCrcValid = static_cast<size_t>(std::count_if(result.phase2MacPdus.begin(), result.phase2MacPdus.end(), [](const P25Phase2MacPdu& pdu) {
        return pdu.crcValid;
    }));
    for (const auto& pdu : result.phase2MacPdus) {
        if (!pdu.crcValid) continue;
        const bool bitPathNominal = !pdu.acchBitOrderSwapped &&
            !pdu.acchDibitInverted &&
            pdu.acchSlipDibits == 0;
        const bool kindNominal = pdu.detectedKind == P25Phase2BurstKind::Unknown ||
            pdu.detectedKind == pdu.source;
        if (bitPathNominal && kindNominal) ++result.stats.phase2MacNominalCrcValid;
        if (!kindNominal) ++result.stats.phase2MacAltKindCrcValid;
        if (pdu.acchBitOrderSwapped) ++result.stats.phase2MacBitSwapCrcValid;
        if (pdu.acchSlipDibits != 0) ++result.stats.phase2MacSlipCrcValid;
        if (pdu.acchDibitInverted) ++result.stats.phase2MacInvertCrcValid;
    }
    result.stats.phase2EssKnown = result.phase2Ess.known;
    result.stats.phase2EssEncrypted = result.phase2Ess.encrypted;
    result.stats.phase2MaskPhaseKnown = m_phase2MaskPhaseKnown;
    result.stats.phase2MaskPhase = m_phase2MaskPhase;
    result.stats.phase2MaskPhaseScore = m_phase2MaskPhaseScore;
    result.stats.phase2MaskPhaseMacCrcValid = result.stats.phase2MacCrcValid;
    for (const auto& burst : result.phase2Bursts) {
        if (burst.superframeLocked) ++result.stats.phase2SuperframeBursts;
        if (burst.xorMaskApplied) ++result.stats.phase2MaskedBursts;
        if (burst.isch.valid) {
            ++result.stats.phase2IschDecoded;
            if (burst.isch.sync) ++result.stats.phase2IschSync;
        }
        if (burst.syncOffsetAdjusted) {
            ++result.stats.phase2SyncOffsetCorrections;
            result.stats.phase2SyncOffsetCorrectionDibits += burst.syncOffsetDibits;
        }
        if (burst.syncErrors >= 0 &&
            (result.stats.bestPhase2SyncErrors < 0 || burst.syncErrors < result.stats.bestPhase2SyncErrors)) {
            result.stats.bestPhase2SyncErrors = burst.syncErrors;
            result.stats.bestPhase2SyncDibitOffset = burst.dibitOffset;
        }
    }
    return result;
}

std::vector<P25Phase2Burst> P25LiveDecoder::processPhase2HardDibits(const std::vector<int>& dibits)
{
    return processPhase2HardDibitsDetailed(dibits).bursts;
}

P25Phase2DecodeResult P25LiveDecoder::processPhase2HardDibitsDetailed(const std::vector<int>& dibits)
{
    return processPhase2HardDibitsDetailedInternal(dibits, true);
}

P25Phase2DecodeResult P25LiveDecoder::processPhase2HardDibitsDetailedInternal(const std::vector<int>& dibits,
                                                                              bool annotateSessionCodewords)
{
    P25Phase2DecodeResult out;

    constexpr size_t kPhase2SyncTailDibits = Phase2BurstDibits * 12;
    std::vector<int> scanDibits;
    size_t phase2PrefixDibits = std::min(m_phase2DibitTail.size(), kPhase2SyncTailDibits);
    if (phase2PrefixDibits > 0) {
        scanDibits.reserve(phase2PrefixDibits + dibits.size());
        scanDibits.insert(scanDibits.end(),
                          m_phase2DibitTail.end() - static_cast<std::ptrdiff_t>(phase2PrefixDibits),
                          m_phase2DibitTail.end());
        for (int d : dibits) scanDibits.push_back(d & 0x03);
    }
    const auto& workingDibits = scanDibits.empty() ? dibits : scanDibits;
    const uint64_t phase2WorkingStreamStart = m_phase2StreamDibits >= static_cast<uint64_t>(phase2PrefixDibits)
        ? m_phase2StreamDibits - static_cast<uint64_t>(phase2PrefixDibits)
        : 0;

    if (workingDibits.size() < Phase2BurstDibits) {
        out.ess = m_phase2Ess;
        return out;
    }

    static constexpr std::array<uint64_t, 5> kSyncWords = {
        Phase2FrameSyncWord,
        Phase2FrameSyncWord ^ 0xAAAAAAAAAAull,
        0x0104015155ull,
        0xA8A2A8D800ull,
        0xFEFBFEAEAAull,
    };
    const int phase2SyncMaxErrors = std::clamp(std::max(3, static_cast<int>(m_config.maxFrameSyncBitErrors)), 0, 6);
    std::vector<Phase2SyncHit> hits;
    std::array<StreamingDibitCorrelator40, kSyncWords.size()> correlators{};
    for (size_t i = 0; i < workingDibits.size(); ++i) {
        int bestErrors = 41;
        for (size_t j = 0; j < kSyncWords.size(); ++j) {
            bestErrors = std::min(bestErrors, correlators[j].push(workingDibits[i], kSyncWords[j]));
        }
        if (i + 1 < Phase2FrameSyncDibits) continue;
        const size_t pos = i + 1 - Phase2FrameSyncDibits;
        if (pos + Phase2BurstDibits > workingDibits.size()) continue;
        if (pos + Phase2BurstDibits <= phase2PrefixDibits) continue;
        if (bestErrors > phase2SyncMaxErrors) continue;
        hits.push_back({pos, bestErrors});
    }

    const auto locks = findPhase2SuperframeLocks(hits, workingDibits.size());
    std::vector<std::pair<size_t, size_t>> lockedWindows;
    auto normalizePhase2BurstOffsets = [&](P25Phase2Burst& burst) {
        if (phase2PrefixDibits == 0) return;
        for (auto& cw : burst.voiceCodewords) {
            cw.duplicateInSession = cw.duplicateInSession || cw.dibitOffset < phase2PrefixDibits;
            cw.dibitOffset = cw.dibitOffset >= phase2PrefixDibits
                ? cw.dibitOffset - phase2PrefixDibits
                : 0;
        }
        burst.voiceCodewords.erase(
            std::remove_if(burst.voiceCodewords.begin(), burst.voiceCodewords.end(),
                [](const P25Phase2VoiceCodeword& cw) { return cw.duplicateInSession && cw.dibitOffset == 0; }),
            burst.voiceCodewords.end());
        burst.dibitOffset = burst.dibitOffset >= phase2PrefixDibits
            ? burst.dibitOffset - phase2PrefixDibits
            : 0;
        if (burst.superframeDibitOffset >= phase2PrefixDibits) burst.superframeDibitOffset -= phase2PrefixDibits;
        else burst.superframeDibitOffset = 0;
        if (burst.isch.dibitOffset >= phase2PrefixDibits) burst.isch.dibitOffset -= phase2PrefixDibits;
        else burst.isch.dibitOffset = 0;
    };
    // Keep Phase-2 call/ESS state per logical TDMA traffic slot during a
    // decode pass.  A single RF carrier contains both slots; sharing MAC/ESS
    // state lets TS1 and TS2 poison each other, unlike sdrtrunk's per-timeslot
    // processors.  Seed both slots from the retained late-entry state for
    // backward compatibility, then process each burst through its own slot
    // session.
    std::array<Phase2SessionState, 2> slotSessions{};
    for (size_t ts = 0; ts < slotSessions.size(); ++ts) {
        auto& session = slotSessions[ts];
        const bool slotHasRetainedState = m_phase2SlotEss[ts].known ||
            m_phase2SlotSessionMacCrcSeen[ts] || m_phase2SlotFirst4vSlot[ts] >= 0;
        session.ess = slotHasRetainedState ? m_phase2SlotEss[ts] : m_phase2Ess;
        session.essTrusted = session.ess.known && session.ess.fecValidated;
        session.essB = slotHasRetainedState ? m_phase2SlotEssB[ts] : m_phase2EssB;
        session.essBSeen = slotHasRetainedState ? m_phase2SlotEssBSeen[ts] : m_phase2EssBSeen;
        session.macCrcSeen = slotHasRetainedState ? m_phase2SlotSessionMacCrcSeen[ts] : m_phase2SessionMacCrcSeen;
        session.pttSeen = session.macCrcSeen;
        session.first4vSlot = slotHasRetainedState ? m_phase2SlotFirst4vSlot[ts] : m_phase2First4vSlot;
        session.essHypotheses = slotHasRetainedState ? m_phase2SlotEssHypotheses[ts] : m_phase2EssHypotheses;
        session.essBHypotheses = slotHasRetainedState ? m_phase2SlotEssBHypotheses[ts] : m_phase2EssBHypotheses;
        session.essBSeenHypotheses = slotHasRetainedState ? m_phase2SlotEssBSeenHypotheses[ts] : m_phase2EssBSeenHypotheses;
    }
    Phase2SessionState& session = slotSessions[0];
    const auto* mask = m_phase2MaskParams.valid ? &m_phase2XorMask : nullptr;
    for (const auto& lock : locks) {
        lockedWindows.push_back({lock.dibitOffset, P25LiveDecoder::Phase2BurstDibits * 12});

        uint8_t selectedMaskPhase = m_phase2MaskPhaseKnown ? m_phase2MaskPhase : 0;
        int selectedMaskScore = m_phase2MaskPhaseKnown ? m_phase2MaskPhaseScore : 0;
        size_t selectedMacCrc = 0;
        if (mask) {
            Phase2MaskPhaseWindow bestWindow;
            bool haveWindow = false;
            for (uint8_t phase = 0; phase < 12; ++phase) {
                const bool sticky = m_phase2MaskPhaseKnown && phase == m_phase2MaskPhase;
                auto window = scorePhase2MaskPhaseWindow(workingDibits, hits, lock, mask, phase, slotSessions, sticky);
                if (!haveWindow || betterPhase2MaskPhaseWindow(window, bestWindow)) {
                    bestWindow = std::move(window);
                    haveWindow = true;
                }
            }
            if (haveWindow) {
                selectedMaskPhase = bestWindow.phase;
                selectedMaskScore = bestWindow.score;
                selectedMacCrc = bestWindow.macCrcValid;
                const bool ischAnchoredMaskPhase = std::any_of(bestWindow.bursts.begin(), bestWindow.bursts.end(), [](const P25Phase2Burst& b) {
                    return b.isch.valid && !b.isch.sync &&
                        b.isch.channel <= 1 && b.isch.location <= 2 &&
                        b.superframeBurstIndexKnown && b.grantSlotKnown &&
                        static_cast<uint8_t>(b.isch.channel & 0x01u) == static_cast<uint8_t>(b.grantSlot & 0x01u) &&
                        b.isch.errors >= 0 && b.isch.errors < 3;
                }) && selectedMaskScore > 0;
                if (annotateSessionCodewords &&
                    (bestWindow.macCrcValid > 0 || bestWindow.essKnown || ischAnchoredMaskPhase)) {
                    m_phase2MaskPhaseKnown = true;
                    m_phase2MaskPhase = selectedMaskPhase;
                    m_phase2MaskPhaseScore = selectedMaskScore;
                    m_phase2SuperframeAnchorKnown = true;
                    m_phase2SuperframeAnchorDibit = phase2WorkingStreamStart + static_cast<uint64_t>(lock.dibitOffset);
                    m_phase2SuperframeAnchorGeneration = m_phase2DecodeGeneration;
                }
            }
        }

        for (size_t slot = 0; slot < 12; ++slot) {
            const size_t expectedPos = lock.dibitOffset + slot * Phase2BurstDibits;
            if (expectedPos + Phase2BurstDibits > workingDibits.size()) break;
            const auto hit = phase2SyncHitNear(hits, expectedPos, 2);
            const size_t pos = hit ? hit->dibitOffset : expectedPos;
            if (pos + Phase2BurstDibits > workingDibits.size()) continue;
            auto& burstSession = slotSessions[phase2TrafficSlotFromSuperframeBurstIndex(slot) & 0x01u];
            auto burst = decodePhase2BurstAt(workingDibits, pos, hit ? hit->errors : -1,
                                             true, lock.dibitOffset, slot,
                                             lock.syncScore, lock.syncErrors,
                                             mask, selectedMaskPhase, selectedMaskScore,
                                             &burstSession, &out.macPdus,
                                             false);
            if (burst.valid) {
                if (hit && hit->dibitOffset != expectedPos) {
                    burst.syncOffsetAdjusted = true;
                    burst.syncOffsetDibits = phase2SignedSyncSlipDibits(hit->dibitOffset, expectedPos);
                }
                // Normalize offsets back to the caller's fresh input. workingDibits
                // can include a Phase-2 tail prefix for overlap; exposing prefix-relative
                // offsets to main.cpp makes absolute AMBE de-dupe/audio cursors lag and
                // replay stale codewords after the waterfall has already gone quiet.
                normalizePhase2BurstOffsets(burst);
                // Do not promote all bursts in a 12-burst window just because some
                // other burst had a valid MAC CRC. The release gate is per-burst.
                out.bursts.push_back(std::move(burst));
            }
        }
    }

    for (const auto& hit : hits) {
        const bool alreadyCovered = std::any_of(lockedWindows.begin(), lockedWindows.end(), [&](const auto& window) {
            return phase2OffsetInWindow(hit.dibitOffset, window.first, window.second);
        });
        if (alreadyCovered) continue;

        bool stickyDecoded = false;
        if (m_phase2SuperframeAnchorKnown && m_phase2MaskPhaseKnown && mask) {
            const uint64_t hitStreamDibit = phase2WorkingStreamStart + static_cast<uint64_t>(hit.dibitOffset);
            constexpr uint64_t kPhase2SuperframeSpanDibits = static_cast<uint64_t>(P25LiveDecoder::Phase2BurstDibits) * 12ull;
            constexpr uint64_t kPhase2StickyAnchorMaxAgeDibits = kPhase2SuperframeSpanDibits * 10ull;
            if (hitStreamDibit >= m_phase2SuperframeAnchorDibit &&
                hitStreamDibit - m_phase2SuperframeAnchorDibit <= kPhase2StickyAnchorMaxAgeDibits) {
                const uint64_t delta = hitStreamDibit - m_phase2SuperframeAnchorDibit;
                const uint64_t mod = delta % kPhase2SuperframeSpanDibits;
                const uint64_t nearest = (mod + static_cast<uint64_t>(P25LiveDecoder::Phase2BurstDibits / 2)) /
                    static_cast<uint64_t>(P25LiveDecoder::Phase2BurstDibits);
                const size_t superframeIndex = static_cast<size_t>(nearest % 12ull);
                const uint64_t expectedMod = static_cast<uint64_t>(superframeIndex) *
                    static_cast<uint64_t>(P25LiveDecoder::Phase2BurstDibits);
                const uint64_t err = mod > expectedMod ? mod - expectedMod : expectedMod - mod;
                const uint64_t wrappedErr = std::min(err, kPhase2SuperframeSpanDibits - err);
                if (wrappedErr <= 2ull) {
                    const size_t syntheticLockOffset = hit.dibitOffset >= superframeIndex * Phase2BurstDibits
                        ? hit.dibitOffset - superframeIndex * Phase2BurstDibits
                        : 0;
                    auto& stickySession = slotSessions[phase2TrafficSlotFromSuperframeBurstIndex(superframeIndex) & 0x01u];
                    auto stickyBurst = decodePhase2BurstAt(workingDibits, hit.dibitOffset, hit.errors,
                                                           true, syntheticLockOffset, superframeIndex,
                                                           0, hit.errors,
                                                           mask, m_phase2MaskPhase, m_phase2MaskPhaseScore,
                                                           &stickySession, &out.macPdus,
                                                           false);
                    if (stickyBurst.valid) {
                        stickyBurst.stickySuperframe = true;
                        normalizePhase2BurstOffsets(stickyBurst);
                        out.bursts.push_back(std::move(stickyBurst));
                        stickyDecoded = true;
                    }
                }
            }
        }
        if (stickyDecoded) continue;

        auto burst = decodePhase2BurstAt(workingDibits, hit.dibitOffset, hit.errors,
                                         false, 0, 0,
                                         0, 0,
                                         nullptr, 0, 0,
                                         &session, &out.macPdus,
                                         false);
        if (burst.valid) {
            normalizePhase2BurstOffsets(burst);
            out.bursts.push_back(std::move(burst));
        }
    }
    Phase2SessionState* retainedSession = &slotSessions[0];
    for (auto& candidate : slotSessions) {
        if (candidate.ess.known && candidate.essTrusted && (!retainedSession->ess.known || candidate.ess.fecValidated)) {
            retainedSession = &candidate;
        }
    }
    out.ess = retainedSession->ess;
    if (annotateSessionCodewords) {
        for (size_t ts = 0; ts < slotSessions.size(); ++ts) {
            m_phase2SlotEss[ts] = slotSessions[ts].ess;
            m_phase2SlotEssB[ts] = slotSessions[ts].essB;
            m_phase2SlotEssBSeen[ts] = slotSessions[ts].essBSeen;
            m_phase2SlotSessionMacCrcSeen[ts] = slotSessions[ts].pttSeen;
            m_phase2SlotFirst4vSlot[ts] = slotSessions[ts].first4vSlot;
            m_phase2SlotEssHypotheses[ts] = slotSessions[ts].essHypotheses;
            m_phase2SlotEssBHypotheses[ts] = slotSessions[ts].essBHypotheses;
            m_phase2SlotEssBSeenHypotheses[ts] = slotSessions[ts].essBSeenHypotheses;
        }
        m_phase2Ess = retainedSession->ess;
        m_phase2EssB = retainedSession->essB;
        m_phase2EssBSeen = retainedSession->essBSeen;
        m_phase2SessionMacCrcSeen = retainedSession->pttSeen;
        m_phase2First4vSlot = retainedSession->first4vSlot;
        m_phase2EssHypotheses = retainedSession->essHypotheses;
        m_phase2EssBHypotheses = retainedSession->essBHypotheses;
        m_phase2EssBSeenHypotheses = retainedSession->essBSeenHypotheses;
        annotatePhase2SessionCodewords(out, dibits);
    }
    return out;
}

P25LiveDecodeResult P25LiveDecoder::processHardBits(const std::vector<uint8_t>& inputBits)
{
    P25LiveDecodeResult result;
    size_t prefixBits = 0;
    result.bits = buildPhase1BitStreamWithTail(this, inputBits, prefixBits);
    result.stats.bits = inputBits.size();
    result.stats.symbolRate = m_config.symbolRate;

    if (result.bits.size() < FrameSyncBits) {
        storePhase1BitTail(this, result.bits);
        return result;
    }

    int bestErrors = static_cast<int>(FrameSyncBits) + 1;
    size_t bestOffset = 0;
    bool bestInverted = false;

    P25Phase1StreamingSync streamingSync;
    StreamingBitCorrelator48 bestNormalSync;
    StreamingBitCorrelator48 bestInvertedSync;
    const uint64_t invertedFrameSyncWord = P25LiveDecoder::FrameSyncWord ^ StreamingBitCorrelator48::kMask;
    size_t suppressUntil = 0;
    for (size_t i = 0; i < result.bits.size(); ++i) {
        const int normalErrors = bestNormalSync.push(result.bits[i], P25LiveDecoder::FrameSyncWord);
        const int invertedErrors = bestInvertedSync.push(result.bits[i], invertedFrameSyncWord);
        auto hit = streamingSync.push(result.bits[i], static_cast<uint64_t>(i), m_config.maxFrameSyncBitErrors);
        if (i + 1 < FrameSyncBits) continue;
        const size_t pos = i + 1 - FrameSyncBits;
        const bool windowReachesCurrentBlock = (pos + FrameSyncBits) > prefixBits;
        if (!windowReachesCurrentBlock) continue;
        if (normalErrors < bestErrors) {
            bestErrors = normalErrors;
            bestOffset = pos;
            bestInverted = false;
        }
        if (invertedErrors < bestErrors) {
            bestErrors = invertedErrors;
            bestOffset = pos;
            bestInverted = true;
        }
        if (pos < suppressUntil || !hit.matched) continue;
        P25FrameSyncEvent ev;
        ev.bitOffset = hit.offset;
        ev.inverted = hit.inverted;
        ev.bitErrors = hit.bitErrors;
        ev.confidence = 1.0 - static_cast<double>(hit.bitErrors) / static_cast<double>(FrameSyncBits);
        result.syncs.push_back(ev);
        if (result.syncs.size() >= m_config.maxFrameSyncs) break;
        suppressUntil = hit.offset + FrameSyncBits;
    }

    result.stats.frameSyncs = result.syncs.size();
    if (bestErrors <= static_cast<int>(FrameSyncBits)) {
        result.stats.bestFrameSyncBitErrors = bestErrors;
        result.stats.bestFrameSyncBitOffset = bestOffset;
        result.stats.bestFrameSyncInverted = bestInverted;
        result.stats.bestFrameSyncBitAligned = (bestOffset % 2) == 0;
        if ((bestOffset % 2) == 0) {
            const auto streamDibits = bitsToDibits(result.bits, bestInverted);
            const size_t syncDibitOffset = bestOffset / 2;
            const size_t payloadDibitOffset = syncDibitOffset + (FrameSyncBits / 2);
            if (payloadDibitOffset < streamDibits.size()) {
                auto split = splitStatusDibitsAfterFrameSync(streamDibits, payloadDibitOffset);
                if (split.dataDibits.size() >= 32) {
                    const uint64_t rawNid = dibitsToWordMsb(split.dataDibits, 0, 32);
                    const auto decodedNid = decodeP25BchNid(rawNid);
                    result.stats.bestNidBchDistance = decodedNid.correctedErrors;
                    result.stats.bestNidValid = decodedNid.valid;
                    if (decodedNid.valid) {
                        result.stats.bestNidNac = static_cast<uint16_t>((decodedNid.data >> 4) & 0x0fffu);
                        result.stats.bestNidRawDuid = static_cast<uint8_t>(decodedNid.data & 0x0fu);
                    } else {
                        result.stats.bestNidNac = static_cast<uint16_t>((rawNid >> 52) & 0x0fffu);
                        result.stats.bestNidRawDuid = static_cast<uint8_t>((rawNid >> 48) & 0x0fu);
                    }
                }
            }
        }
    }

    for (const auto& syncEvent : result.syncs) {
        if (syncEvent.bitOffset % 2 != 0) continue;
        const auto streamDibits = bitsToDibits(result.bits, syncEvent.inverted);
        const size_t syncDibitOffset = syncEvent.bitOffset / 2;
        const size_t payloadDibitOffset = syncDibitOffset + (FrameSyncBits / 2);
        if (payloadDibitOffset >= streamDibits.size()) continue;

        auto split = splitStatusDibitsAfterFrameSync(streamDibits, payloadDibitOffset);
        if (split.dataDibits.size() < 32) continue;

        const size_t nidStartBit = syncEvent.bitOffset + FrameSyncBits;
        P25Nid nid;
        nid.bitOffset = nidStartBit;
        nid.raw = dibitsToWordMsb(split.dataDibits, 0, 32);
        const auto decodedNid = decodeP25BchNid(nid.raw);
        nid.correctedBitErrors = decodedNid.correctedErrors;
        if (decodedNid.valid) {
            nid.valid = true;
            nid.fecValidated = true;
            nid.nac = static_cast<uint16_t>((decodedNid.data >> 4) & 0x0fffu);
            nid.rawDuid = static_cast<uint8_t>(decodedNid.data & 0x0fu);
        } else {
            nid.valid = false;
            nid.fecValidated = false;
            nid.nac = static_cast<uint16_t>((nid.raw >> 52) & 0x0fffu);
            nid.rawDuid = static_cast<uint8_t>((nid.raw >> 48) & 0x0fu);
        }
        nid.duid = toDuid(nid.rawDuid);
        result.nids.push_back(nid);
        if (!nid.fecValidated) {
            result.warnings.push_back("P25 frame sync found but NID BCH validation/correction failed.");
            continue;
        }

        if (nid.duid == P25DataUnitId::TSDU) {
            size_t blockStartDibit = 32;
            size_t blocks = 0;
            while (blockStartDibit + 98 <= split.dataDibits.size() && blocks < m_config.maxRawTsbkBlocksPerFrame) {
                std::vector<int> coded(split.dataDibits.begin() + static_cast<std::ptrdiff_t>(blockStartDibit),
                                       split.dataDibits.begin() + static_cast<std::ptrdiff_t>(blockStartDibit + 98));
                auto block = decodeCodedTsbkBlock(coded, (payloadDibitOffset + blockStartDibit) * 2);
                if (!block.bytes.empty()) {
                    result.rawTsbkBlocks.push_back(std::move(block));
                }
                blockStartDibit += 98;
                ++blocks;
            }
            if (!result.rawTsbkBlocks.empty()) {
                const bool anyValid = std::any_of(result.rawTsbkBlocks.begin(), result.rawTsbkBlocks.end(), [](const P25TsbkBlock& b) {
                    return b.fecDecoded && b.crcValid;
                });
                if (!anyValid) {
                    result.warnings.push_back("TSDU block candidates were trellis-decoded but failed CRC validation.");
                }
            }
        } else if (nid.duid == P25DataUnitId::LDU1 || nid.duid == P25DataUnitId::LDU2) {
            size_t cursor = 32;
            size_t decodedFrames = 0;
            for (int frameIndex = 0; frameIndex < 9; ++frameIndex) {
                if (cursor + 72 > split.dataDibits.size()) break;
                std::vector<int> voiceFrame(split.dataDibits.begin() + static_cast<std::ptrdiff_t>(cursor),
                                            split.dataDibits.begin() + static_cast<std::ptrdiff_t>(cursor + 72));
                auto imbe = p25DecodeImbeFrameFromVoiceDibits(voiceFrame);
                imbe.bitOffset = (payloadDibitOffset + cursor) * 2;
                if (imbe.valid) ++decodedFrames;
                result.imbeFrames.push_back(std::move(imbe));

                cursor += 72;
                if (frameIndex >= 1 && frameIndex <= 6) cursor += 20; // LC/CC extra piece after frames 2..7.
                else if (frameIndex == 7) cursor += 8;               // Low-speed data fragment after frame 8.
            }
            if (decodedFrames == 0) {
                result.warnings.push_back("P25 LDU voice frame detected but no IMBE frames could be decoded.");
            }
        }
    }

    std::stable_sort(result.nids.begin(), result.nids.end(), [](const P25Nid& a, const P25Nid& b) {
        if (a.fecValidated != b.fecValidated) return a.fecValidated;
        return a.bitOffset < b.bitOffset;
    });

    result.stats.voiceBackendAvailable = compiledVoiceBackendAvailable();
    storePhase1BitTail(this, result.bits);
    return result;
}

std::array<uint8_t, P25LiveDecoder::FrameSyncBits> P25LiveDecoder::frameSyncBits()
{
    std::array<uint8_t, FrameSyncBits> out{};
    for (size_t i = 0; i < FrameSyncBits; ++i) {
        const size_t shift = FrameSyncBits - 1 - i;
        out[i] = static_cast<uint8_t>((FrameSyncWord >> shift) & 1ull);
    }
    return out;
}

std::array<int, P25LiveDecoder::Phase2FrameSyncDibits> P25LiveDecoder::phase2FrameSyncDibits()
{
    return phase2DibitsFromWord(Phase2FrameSyncWord);
}

int P25LiveDecoder::dibitFromBits(uint8_t first, uint8_t second)
{
    return ((first ? 1 : 0) << 1) | (second ? 1 : 0);
}

std::array<uint8_t, 2> P25LiveDecoder::bitsFromDibit(int dibit)
{
    const uint8_t d = static_cast<uint8_t>(dibit) & 0x03u;
    return {static_cast<uint8_t>((d >> 1) & 1u), static_cast<uint8_t>(d & 1u)};
}

double P25LiveDecoder::nominalC4fmLevelForDibit(int dibit)
{
    switch (dibit & 0x03) {
        case 0x1: return 3.0;   // 01
        case 0x0: return 1.0;   // 00
        case 0x2: return -1.0;  // 10
        case 0x3: return -3.0;  // 11
        default: return 0.0;
    }
}

std::string P25LiveDecoder::dataUnitIdToString(P25DataUnitId duid)
{
    switch (duid) {
        case P25DataUnitId::HDU: return "HDU";
        case P25DataUnitId::TDU: return "TDU";
        case P25DataUnitId::LDU1: return "LDU1";
        case P25DataUnitId::TSDU: return "TSDU";
        case P25DataUnitId::LDU2: return "LDU2";
        case P25DataUnitId::PDU: return "PDU";
        case P25DataUnitId::TDULC: return "TDULC";
        case P25DataUnitId::Unknown:
        default: return "Unknown";
    }
}

std::string P25LiveDecoder::phase2BurstKindToString(P25Phase2BurstKind kind)
{
    switch (kind) {
        case P25Phase2BurstKind::Voice4: return "4V";
        case P25Phase2BurstKind::Voice2: return "2V";
        case P25Phase2BurstKind::SacchScrambled: return "SACCH scrambled";
        case P25Phase2BurstKind::FacchScrambled: return "FACCH scrambled";
        case P25Phase2BurstKind::SacchClear: return "SACCH clear";
        case P25Phase2BurstKind::FacchClear: return "FACCH clear";
        case P25Phase2BurstKind::LcchClear: return "LCCH clear";
        case P25Phase2BurstKind::Unknown:
        default: return "Unknown";
    }
}

struct P25ImbeVoiceDecoder::Impl {
#ifdef HAVE_MBELIB
    mbe_parms current{};
    mbe_parms previous{};
    mbe_parms enhanced{};

    Impl()
    {
        mbe_initMbeParms(&current, &previous, &enhanced);
    }
#endif
};

P25ImbeVoiceDecoder::P25ImbeVoiceDecoder()
{
#ifdef HAVE_MBELIB
    m_impl = std::make_unique<Impl>();
#endif
}

P25ImbeVoiceDecoder::~P25ImbeVoiceDecoder() = default;
P25ImbeVoiceDecoder::P25ImbeVoiceDecoder(P25ImbeVoiceDecoder&&) noexcept = default;
P25ImbeVoiceDecoder& P25ImbeVoiceDecoder::operator=(P25ImbeVoiceDecoder&&) noexcept = default;

bool P25ImbeVoiceDecoder::backendAvailable() const
{
#ifdef HAVE_MBELIB
    return static_cast<bool>(m_impl);
#else
    return false;
#endif
}

P25VoiceDecodeResult P25ImbeVoiceDecoder::decodeImbe4400Frame(const std::array<uint8_t, 11>& imbe88)
{
    P25VoiceDecodeResult result;
    result.sampleRate = 8000.0;
#ifdef HAVE_MBELIB
    if (!m_impl) {
        result.status = P25VoiceDecodeStatus::BackendUnavailable;
        result.message = "mbelib IMBE backend is not initialized.";
        return result;
    }

    char imbeBits[88]{};
    for (size_t i = 0; i < imbe88.size(); ++i) {
        for (int b = 0; b < 8; ++b) {
            imbeBits[i * 8 + static_cast<size_t>(b)] = static_cast<char>((imbe88[i] >> (7 - b)) & 1u);
        }
    }

    float audio[160]{};
    int errs = 0;
    int errs2 = 0;
    char errText[64]{};
    mbe_processImbe4400Dataf(audio, &errs, &errs2, errText, imbeBits,
                             &m_impl->current, &m_impl->previous, &m_impl->enhanced, 3);
    result.status = P25VoiceDecodeStatus::Decoded;
    result.errors = errs;
    result.totalErrors = errs2;
    result.pcm.assign(std::begin(audio), std::end(audio));
    result.message = errText[0] ? errText : "decoded";
    return result;
#else
    (void)imbe88;
    result.status = P25VoiceDecodeStatus::BackendUnavailable;
    result.message = "Clear P25 Phase 1 IMBE voice requires an mbelib-compatible backend built with SDR_TOWN_ENABLE_MBELIB=ON.";
    return result;
#endif
}

struct P25AmbeVoiceDecoder::Impl {
#ifdef HAVE_MBELIB
    mbe_parms current{};
    mbe_parms previous{};
    mbe_parms enhanced{};

    Impl()
    {
        mbe_initMbeParms(&current, &previous, &enhanced);
    }
#endif
};

P25AmbeVoiceDecoder::P25AmbeVoiceDecoder()
{
#ifdef HAVE_MBELIB
    m_impl = std::make_unique<Impl>();
#endif
}

P25AmbeVoiceDecoder::~P25AmbeVoiceDecoder() = default;
P25AmbeVoiceDecoder::P25AmbeVoiceDecoder(P25AmbeVoiceDecoder&&) noexcept = default;
P25AmbeVoiceDecoder& P25AmbeVoiceDecoder::operator=(P25AmbeVoiceDecoder&&) noexcept = default;

bool P25AmbeVoiceDecoder::backendAvailable() const
{
#ifdef HAVE_MBELIB
    return static_cast<bool>(m_impl);
#else
    return false;
#endif
}

P25VoiceDecodeResult P25AmbeVoiceDecoder::decodeAmbe2450Data(const std::array<uint8_t, 49>& ambe49)
{
    P25VoiceDecodeResult result;
    result.sampleRate = 8000.0;
#ifdef HAVE_MBELIB
    if (!m_impl) {
        result.status = P25VoiceDecodeStatus::BackendUnavailable;
        result.message = "mbelib AMBE backend is not initialized.";
        return result;
    }

    char ambeBits[49]{};
    for (size_t i = 0; i < ambe49.size(); ++i) {
        ambeBits[i] = static_cast<char>(ambe49[i] ? 1 : 0);
    }

    float audio[160]{};
    int errs = 0;
    int errs2 = 0;
    char errText[64]{};
    mbe_processAmbe2450Dataf(audio, &errs, &errs2, errText, ambeBits,
                             &m_impl->current, &m_impl->previous, &m_impl->enhanced, 3);
    result.status = P25VoiceDecodeStatus::Decoded;
    result.errors = errs;
    result.totalErrors = errs2;
    result.pcm.assign(std::begin(audio), std::end(audio));
    result.message = errText[0] ? errText : "decoded";
    return result;
#else
    (void)ambe49;
    result.status = P25VoiceDecodeStatus::BackendUnavailable;
    result.message = "Clear P25 Phase 2 AMBE voice requires an mbelib-compatible backend built with SDR_TOWN_ENABLE_MBELIB=ON.";
    return result;
#endif
}


bool validateUnpackedAmbe96(const std::array<uint8_t, 96>& ambe96)
{
    return std::all_of(ambe96.begin(), ambe96.end(), [](uint8_t bit) {
        return bit == 0u || bit == 1u;
    });
}

P25VoiceDecodeResult P25AmbeVoiceDecoder::decodeAmbe3600x2450Frame(const std::array<uint8_t, 96>& ambe96)
{
    P25VoiceDecodeResult result;
    result.sampleRate = 8000.0;
#ifdef HAVE_MBELIB
    if (!m_impl) {
        result.status = P25VoiceDecodeStatus::BackendUnavailable;
        result.message = "mbelib AMBE backend is not initialized.";
        return result;
    }

    if (!validateUnpackedAmbe96(ambe96)) {
        result.status = P25VoiceDecodeStatus::InvalidFrame;
        result.message = "AMBE 3600x2450 input must be post-FEC unpacked bits (0/1 per byte).";
        return result;
    }

    // The stream parser supplies the AMBE C0/C1/C2/C3 matrix produced by
    // p25Phase2VoiceCodewordToAmbe3600x2450Frame().  Each char is an explicit
    // hard bit.  mbelib performs the inner AMBE Golay C0/C1 correction, C1
    // pseudo-random demodulation and 49-bit AMBE parameter extraction here.
    char ambeFrame[4][24]{};
    for (size_t row = 0; row < 4; ++row) {
        for (size_t col = 0; col < 24; ++col) {
            ambeFrame[row][col] = static_cast<char>(ambe96[row * 24 + col] & 0x01u);
        }
    }

    char ambeData[49]{};
    float audio[160]{};
    int errs = 0;
    int errs2 = 0;
    char errText[64]{};
    mbe_processAmbe3600x2450Framef(audio, &errs, &errs2, errText, ambeFrame, ambeData,
                                   &m_impl->current, &m_impl->previous, &m_impl->enhanced, 3);
    result.errors = errs;
    result.totalErrors = errs2;

    // sdrtrunk hands each 72-bit Phase-2 AMBE voice frame to JMBE and lets the
    // AMBE codec's own Golay/erasure/repeat logic decide whether to synthesize
    // speech, repeat, tone, erasure or silence.  Do not add a second hard
    // post-FEC error gate here: field logs showed healthy TDMA/VCW acquisition
    // followed by zero emitted audio because frames with correctable/erasured
    // AMBE state were being discarded after mbelib had already produced a valid
    // 160-sample frame.  The caller still records errs/errs2 for diagnostics and
    // performs the upstream P25 TDMA/slot/mask/clear-grant gates before samples
    // reach the speaker.
    result.status = P25VoiceDecodeStatus::Decoded;
    result.pcm.assign(std::begin(audio), std::end(audio));
    result.message = errText[0] ? errText : "decoded";
    return result;
#else
    (void)ambe96;
    result.status = P25VoiceDecodeStatus::BackendUnavailable;
    result.message = "Clear P25 Phase 2 AMBE voice requires an mbelib-compatible backend built with SDR_TOWN_ENABLE_MBELIB=ON.";
    return result;
#endif
}
