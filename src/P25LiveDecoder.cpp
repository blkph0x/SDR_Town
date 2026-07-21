#include "P25LiveDecoder.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <numeric>
#include <optional>
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

bool p25DecoderTraceEnabled()
{
    static const bool enabled = []() {
        const char* v = std::getenv("SDR_TOWN_P25_DECODER_TRACE");
        return v && *v && std::strcmp(v, "0") != 0;
    }();
    return enabled;
}

void p25DecoderTrace(const char* stage, const char* detail = "")
{
    if (!p25DecoderTraceEnabled()) return;
    static std::mutex traceMutex;
    std::lock_guard<std::mutex> lock(traceMutex);
    std::ofstream out("p25_decoder_trace.log", std::ios::app);
    if (!out) return;
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    out << ms << ' ' << stage;
    if (detail && *detail) out << ' ' << detail;
    out << '\n';
}

struct P25DecoderTraceScope {
    const char* name = "";

    explicit P25DecoderTraceScope(const char* n) : name(n)
    {
        p25DecoderTrace(name, "enter");
    }

    ~P25DecoderTraceScope()
    {
        p25DecoderTrace(name, "leave");
    }
};

double clampFinite(double v, double fallback)
{
    return std::isfinite(v) ? v : fallback;
}


int popcount64(uint64_t v) noexcept
{
#if defined(__GNUG__) || defined(__clang__)
    return __builtin_popcountll(static_cast<unsigned long long>(v));
#else
    int count = 0;
    while (v != 0) {
        v &= (v - 1);
        ++count;
    }
    return count;
#endif
}

std::vector<float> normalizedMbelibPcm(const float* audio, size_t count)
{
    // mbelib's float synthesizer uses the same internal scale that its short
    // wrapper later multiplies by seven and clamps into int16.  Normalize that
    // contract at the codec boundary so the rest of SDR Town consistently sees
    // native float PCM in roughly [-1, 1].
    static constexpr float kMbelibFloatToShortGain = 7.0f;
    static constexpr float kInt16FullScale = 32768.0f;
    std::vector<float> out;
    out.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const float sample = std::isfinite(audio[i]) ? audio[i] : 0.0f;
        out.push_back(std::clamp(sample * kMbelibFloatToShortGain / kInt16FullScale,
                                 -1.0f, 1.0f));
    }
    return out;
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
        return static_cast<int>(popcount64((reg ^ syncWord) & kMask));
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
        return static_cast<int>(popcount64((reg ^ syncWord) & kMask));
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

std::vector<double> designRrcTaps(double sampleRate, double symbolRate, double alpha, int maxTaps)
{
    if (!std::isfinite(sampleRate) || sampleRate <= 0.0 ||
        !std::isfinite(symbolRate) || symbolRate <= 0.0) {
        return {1.0};
    }
    alpha = std::clamp(alpha, 0.05, 0.99);
    const double symbolPeriod = 1.0 / symbolRate;
    constexpr int kSpans = 4;
    int taps = static_cast<int>(std::ceil(static_cast<double>(kSpans) * symbolPeriod * sampleRate));
    taps = std::clamp(taps, 31, std::max(31, maxTaps));
    if ((taps & 1) == 0) ++taps;
    const int mid = taps / 2;
    const double dt = 1.0 / sampleRate;

    std::vector<double> h(static_cast<size_t>(taps), 0.0);
    double sum = 0.0;
    for (int n = 0; n < taps; ++n) {
        const int m = n - mid;
        const double t = static_cast<double>(m) * dt;
        double v = 0.0;
        if (std::abs(t) < 1e-12) {
            v = (1.0 / symbolPeriod) * (1.0 + alpha * (4.0 / kPi - 1.0));
        } else if (alpha > 0.0 && std::abs(std::abs(t) - symbolPeriod / (4.0 * alpha)) < dt * 0.5) {
            v = (alpha / symbolPeriod) *
                (std::sin(kPi / (4.0 * alpha)) + (1.0 - alpha) * kPi / (4.0 * alpha) * std::cos(kPi / (4.0 * alpha)));
        } else {
            const double arg = kPi * t / symbolPeriod;
            const double num = std::sin(arg * (1.0 - alpha)) +
                4.0 * alpha * t / symbolPeriod * std::cos(arg * (1.0 + alpha));
            const double den = kPi * t / symbolPeriod *
                (1.0 - std::pow(4.0 * alpha * t / symbolPeriod, 2.0));
            v = std::abs(den) > 1e-12 ? num / den : 0.0;
        }
        const double x = taps == 1 ? 0.0 : (2.0 * kPi * static_cast<double>(n)) / static_cast<double>(taps - 1);
        const double window = 0.35875 - 0.48829 * std::cos(x) + 0.14128 * std::cos(2.0 * x) -
            0.01168 * std::cos(3.0 * x);
        h[static_cast<size_t>(n)] = v * window;
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

std::vector<std::complex<float>> decimateFirSame(const std::vector<std::complex<float>>& x,
                                                 const std::vector<double>& taps,
                                                 int decimation)
{
    if (x.empty() || taps.empty() || decimation <= 1) return applyFirSame(x, taps);
    std::vector<std::complex<float>> out;
    out.reserve(x.size() / static_cast<size_t>(decimation) + 1);
    const int half = static_cast<int>(taps.size() / 2);
    for (size_t n = 0; n < x.size(); n += static_cast<size_t>(decimation)) {
        std::complex<double> acc(0.0, 0.0);
        for (size_t k = 0; k < taps.size(); ++k) {
            const int idx = static_cast<int>(n) + static_cast<int>(k) - half;
            if (idx < 0 || idx >= static_cast<int>(x.size())) continue;
            const double w = taps[k];
            acc += std::complex<double>(x[static_cast<size_t>(idx)].real(), x[static_cast<size_t>(idx)].imag()) * w;
        }
        out.emplace_back(static_cast<float>(acc.real()), static_cast<float>(acc.imag()));
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

double wrapRadiansLocal(double p)
{
    if (!std::isfinite(p)) return 0.0;
    p = std::remainder(p, 2.0 * kPi);
    if (p <= -kPi) p += 2.0 * kPi;
    if (p > kPi) p -= 2.0 * kPi;
    return p;
}

double nearestPiOverFourPhase(double phase)
{
    const double step = kPi * 0.25;
    return wrapRadiansLocal(std::round(phase / step) * step);
}

struct FrontEndDcBlockResult {
    bool applied = false;
    double estimateMagnitude = 0.0;
};

FrontEndDcBlockResult applyPersistentFrontEndDcBlock(std::vector<std::complex<float>>& iq,
                                                     double sampleRate,
                                                     double alpha,
                                                     bool& estimateValid,
                                                     double& estimateSampleRate,
                                                     std::complex<double>& estimate)
{
    FrontEndDcBlockResult out;
    if (iq.empty() || !std::isfinite(sampleRate) || sampleRate <= 0.0) return out;

    const bool sampleRateChanged = estimateValid &&
        (!std::isfinite(estimateSampleRate) ||
         std::abs(estimateSampleRate - sampleRate) > std::max(1.0, sampleRate * 0.02));
    if (!estimateValid || sampleRateChanged) {
        std::complex<double> sum(0.0, 0.0);
        size_t count = 0;
        const size_t probe = std::min<size_t>(iq.size(), 4096);
        for (size_t i = 0; i < probe; ++i) {
            const auto& v = iq[i];
            if (std::isfinite(v.real()) && std::isfinite(v.imag())) {
                sum += std::complex<double>(v.real(), v.imag());
                ++count;
            }
        }
        estimate = count > 0 ? sum / static_cast<double>(count) : std::complex<double>(0.0, 0.0);
        estimateSampleRate = sampleRate;
        estimateValid = true;
    }

    const double a = std::clamp(alpha, 1e-6, 0.02);
    for (auto& v : iq) {
        if (!std::isfinite(v.real()) || !std::isfinite(v.imag())) {
            v = {};
            continue;
        }
        const std::complex<double> x(v.real(), v.imag());
        estimate += (x - estimate) * a;
        const std::complex<double> y = x - estimate;
        v = {static_cast<float>(y.real()), static_cast<float>(y.imag())};
    }
    out.applied = true;
    out.estimateMagnitude = std::abs(estimate);
    return out;
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
    P25DecoderTraceScope trace("channelizeP25Iq");
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
    std::vector<std::complex<float>> channel;
    if (decim > 1) {
        const double antiAliasCutoffHz = std::clamp(intermediateRate * 0.42,
                                                    config.channelBandwidthHz * 2.5,
                                                    sampleRate * 0.45);
        const double antiAliasTransitionHz = std::clamp(intermediateRate * 0.18,
                                                        config.channelBandwidthHz,
                                                        sampleRate * 0.20);
        channel = decimateFirSame(
            mixed,
            designLowpassTaps(sampleRate, antiAliasCutoffHz, antiAliasTransitionHz, 161),
            decim);
    } else {
        channel = std::move(mixed);
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
    P25DecoderTraceScope trace("fmDiscriminatorFromChannel");
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
    bool carrierLoopApplied = false;
    double carrierLoopCorrectionHz = 0.0;
    double carrierLoopPhaseErrorRmsRad = 0.0;
    size_t carrierLoopSymbols = 0;
    bool fineCorrectionApplied = false;
    double fineRotationRad = 0.0;
    double finePhaseErrorRmsRad = 0.0;
    size_t fineCorrectionSymbols = 0;
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

    // Slightly tighter post-discriminator LPF for C4FM to reduce noise/ISI while passing the
    // main lobes of the 4-level FSK (closer to typical P25 C4FM receiver filtering + SDRTrunk
    // processing for cleaner symbol decisions leading to clearer voice frames).
    const double filterCutoffHz = std::clamp(config.symbolRate * 0.62, 2400.0, sampleRate * 0.38);
    const double filterTransitionHz = std::clamp(config.symbolRate * 0.38, 1100.0, sampleRate * 0.18);
    centered = applyFirSame(centered, designLowpassTaps(sampleRate, filterCutoffHz, filterTransitionHz, 121));
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

    // Lightweight running level normalization (AGC-like) to improve slice stability across fades/offsets.
    // Matches spirit of SDRTrunk C4FM equalizer/gain compensation for cleaner dibits -> clearer voice.
    if (out.symbolsHz.size() >= 8) {
        std::vector<double> absS;
        absS.reserve(out.symbolsHz.size());
        for (double s : out.symbolsHz) absS.push_back(std::abs(s));
        double ref = percentile(absS, 0.80);
        if (ref > 50.0) {
            const double target = config.c4fmInnerDeviationHz * 1.0;
            const double g = std::clamp(target / ref, 0.6, 1.8);
            for (double& s : out.symbolsHz) s *= g;
        }
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
    P25DecoderTraceScope trace("recoverC4fmSymbolCandidates");
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
    if (config.phase2CqpskTrafficDemod && config.cqpskUseMatchedRrcFilter) {
        const double alpha = std::clamp(config.cqpskRrcAlpha, 0.10, 0.50);
        filtered = applyFirSame(filtered,
            designRrcTaps(sampleRate, config.symbolRate, alpha, 121));
    } else if (config.phase2CqpskTrafficDemod) {
        // SDRTrunk P25P2DecoderHDQPSK baseband: pass ~6500 Hz, stop ~7200 Hz
        // (designed at 50 kHz).  No RRC on the Phase-2 HDQPSK path.
        const double filterCutoffHz = std::clamp(6500.0,
                                                 config.symbolRate * 0.90,
                                                 sampleRate * 0.42);
        const double filterTransitionHz = std::clamp(700.0, 400.0, sampleRate * 0.10);
        filtered = applyFirSame(filtered, designLowpassTaps(sampleRate, filterCutoffHz, filterTransitionHz, 121));
    } else {
        const double filterCutoffHz = std::clamp(config.symbolRate * 0.82, 3000.0, sampleRate * 0.42);
        const double filterTransitionHz = std::clamp(config.symbolRate * 0.50, 1400.0, sampleRate * 0.22);
        filtered = applyFirSame(filtered, designLowpassTaps(sampleRate, filterCutoffHz, filterTransitionHz, 121));
    }
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
    const bool carrierLoopEnabled = config.enableCqpskCarrierLoop &&
        std::isfinite(config.cqpskCarrierLoopBandwidth) &&
        config.cqpskCarrierLoopBandwidth > 0.0;
    const double carrierLoopBandwidth = std::clamp(config.cqpskCarrierLoopBandwidth, 0.002, 0.120);
    const double carrierLoopDamping = 0.707;
    const double carrierLoopDenom = 1.0 +
        2.0 * carrierLoopDamping * carrierLoopBandwidth +
        carrierLoopBandwidth * carrierLoopBandwidth;
    const double carrierLoopAlpha = (4.0 * carrierLoopDamping * carrierLoopBandwidth) / carrierLoopDenom;
    const double carrierLoopBeta = (4.0 * carrierLoopBandwidth * carrierLoopBandwidth) / carrierLoopDenom;
    const double maxCarrierCorrectionHz = std::clamp(config.cqpskCarrierLoopMaxCorrectionHz,
                                                     50.0,
                                                     std::max(50.0, config.symbolRate * 0.45));
    const double maxCarrierOmega = 2.0 * kPi * maxCarrierCorrectionHz / std::max(config.symbolRate, 1.0);
    double carrierPhase = 0.0;
    double carrierOmega = 0.0;
    double t = std::clamp(initialPhase, 0.0, std::max(0.0, sps - 1.0));
    if (streamState && streamState->cqpskValid &&
        std::abs(streamState->cqpskSampleRate - sampleRate) <= sampleRate * 0.02 &&
        streamState->cqpskOmega > 0.0) {
        timingLoop.omega = std::clamp(streamState->cqpskOmega, timingLoop.omegaMin, timingLoop.omegaMax);
        t = std::clamp(streamState->cqpskMu, 0.0, std::max(0.0, timingLoop.omega - 1.0));
    }
    if (carrierLoopEnabled &&
        streamState &&
        streamState->cqpskCarrierLoopValid &&
        streamState->cqpskValid &&
        std::abs(streamState->cqpskSampleRate - sampleRate) <= sampleRate * 0.02) {
        carrierPhase = wrapRadiansLocal(streamState->cqpskCarrierLoopPhase);
        carrierOmega = std::clamp(streamState->cqpskCarrierLoopOmega, -maxCarrierOmega, maxCarrierOmega);
    }
    double timingEnergy = 0.0;
    size_t timingCount = 0;
    double carrierErrorEnergy = 0.0;
    size_t carrierErrorCount = 0;

    std::complex<double> previousMiddleSample{0.0, 0.0};
    std::complex<double> previousCurrentSample{0.0, 0.0};
    std::complex<double> previousDecisionSymbol{0.0, 0.0};
    bool havePreviousSamples = false;
    bool havePreviousDecision = false;

    auto normalizedComplex = [](std::complex<double> z) {
        const double mag = std::abs(z);
        if (!std::isfinite(mag) || mag < 1e-9 ||
            !std::isfinite(z.real()) || !std::isfinite(z.imag())) {
            return std::complex<double>{0.0, 0.0};
        }
        return z / mag;
    };
    auto idealPhaseForDqpsk = [](const std::complex<double>& z) {
        if (z.imag() > 0.0) {
            return z.real() > 0.0 ? kPi * 0.25 : kPi * 0.75;
        }
        return z.real() > 0.0 ? -kPi * 0.25 : -kPi * 0.75;
    };

    while (t < static_cast<double>(filtered.size() - 1)) {
        const double halfSymbol = timingLoop.omega * 0.5;
        const auto rawMiddle = sampleLinearComplex(filtered, t - halfSymbol);
        const auto rawSample = sampleLinearComplex(filtered, t);
        std::complex<double> middle(static_cast<double>(rawMiddle.real()), static_cast<double>(rawMiddle.imag()));
        std::complex<double> s(static_cast<double>(rawSample.real()), static_cast<double>(rawSample.imag()));
        if (carrierLoopEnabled) {
            const std::complex<double> nco(std::cos(-carrierPhase), std::sin(-carrierPhase));
            middle *= nco;
            s *= nco;
        }
        out.symbols.push_back(s);

        double err = 0.0;
        double phaseError = 0.0;
        bool haveDecision = false;
        std::complex<double> currentDecisionSymbol{0.0, 0.0};
        if (havePreviousSamples) {
            const auto middleSymbol = normalizedComplex(middle * std::conj(previousMiddleSample));
            currentDecisionSymbol = normalizedComplex(s * std::conj(previousCurrentSample));
            haveDecision = std::abs(middleSymbol) > 0.0 && std::abs(currentDecisionSymbol) > 0.0;
            if (haveDecision) {
                if (havePreviousDecision) {
                    err = ((previousDecisionSymbol.real() - currentDecisionSymbol.real()) * middleSymbol.real()) +
                          ((previousDecisionSymbol.imag() - currentDecisionSymbol.imag()) * middleSymbol.imag());
                    err = std::clamp(err / 0.3, -1.0, 1.0);
                }
                const double observedPhase = std::atan2(currentDecisionSymbol.imag(), currentDecisionSymbol.real());
                phaseError = -wrapRadiansLocal(observedPhase - idealPhaseForDqpsk(currentDecisionSymbol));
                phaseError = std::clamp(phaseError, -0.3, 0.3);
            }
        }
        timingEnergy += err * err;
        ++timingCount;

        if (carrierLoopEnabled) {
            if (haveDecision) {
                carrierOmega = std::clamp(carrierOmega + carrierLoopBeta * phaseError,
                                           -maxCarrierOmega,
                                           maxCarrierOmega);
                carrierPhase = wrapRadiansLocal(carrierPhase + carrierOmega + carrierLoopAlpha * phaseError);
                carrierErrorEnergy += phaseError * phaseError;
                ++carrierErrorCount;
            } else {
                carrierPhase = wrapRadiansLocal(carrierPhase + carrierOmega);
            }
        }
        previousMiddleSample = middle;
        previousCurrentSample = s;
        havePreviousSamples = true;
        if (haveDecision) {
            previousDecisionSymbol = currentDecisionSymbol;
            havePreviousDecision = true;
        }
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
    if (carrierLoopEnabled && carrierErrorCount >= 16) {
        out.carrierLoopApplied = true;
        out.carrierLoopCorrectionHz = carrierOmega * config.symbolRate / (2.0 * kPi);
        out.carrierLoopPhaseErrorRmsRad =
            std::sqrt(carrierErrorEnergy / static_cast<double>(carrierErrorCount));
        out.carrierLoopSymbols = carrierErrorCount;
    }

    // Percentile-based magnitude normalization on CQPSK symbols (matches C4FM AGC spirit).
    // p80 resists brief fades better than mean normalization and tracks SDRTrunk-style
    // equalizer/gain compensation for cleaner dibits and clearer voice audio.
    if (out.symbols.size() >= 8) {
        std::vector<double> absS;
        absS.reserve(out.symbols.size());
        for (const auto& s : out.symbols) {
            absS.push_back(std::abs(s));
        }
        const double ref = percentile(absS, 0.80);
        if (ref > 1e-6) {
            const double target = 1.0;
            // SDRTrunk applies ~1.2x constellation imbalance correction on C4FM;
            // use a milder fixed bias on CQPSK after percentile normalization.
            const double g = std::clamp((target / ref) * 1.15, 0.5, 2.0);
            for (auto& s : out.symbols) {
                s *= g;
            }
        }
    }

    if (streamState && !out.symbols.empty()) {
        streamState->cqpskValid = true;
        streamState->cqpskSampleRate = sampleRate;
        streamState->cqpskOmega = timingLoop.omega;
        streamState->cqpskMu = std::fmod(std::max(t, 0.0), std::max(timingLoop.omega, 1e-6));
        streamState->cqpskCarrierLoopValid = out.carrierLoopApplied;
        streamState->cqpskCarrierLoopPhase = carrierPhase;
        streamState->cqpskCarrierLoopOmega = carrierOmega;
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
    for (const auto& pdu : r.phase1Pdus) {
        if (pdu.headerFecDecoded && pdu.headerCrcValid) score += pdu.format == 23 ? 220 : 160;
        else if (pdu.headerFecDecoded) score += 8;
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
    for (const auto& pdu : r.phase1Pdus) {
        if (pdu.headerFecDecoded && pdu.headerCrcValid) score += pdu.format == 23 ? 42 : 30;
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

bool betterLiveResult(const P25LiveDecodeResult& a,
                      const P25LiveDecodeResult& b,
                      const P25LiveDecoderConfig* config = nullptr)
{
    const auto validNids = [](const P25LiveDecodeResult& r) {
        return std::count_if(r.nids.begin(), r.nids.end(), [](const P25Nid& nid) {
            return nid.fecValidated;
        });
    };
    const auto bestValidatedNidCorrections = [](const P25LiveDecodeResult& r) {
        int best = std::numeric_limits<int>::max();
        for (const auto& nid : r.nids) {
            if (nid.fecValidated) best = std::min(best, nid.correctedBitErrors);
        }
        return best;
    };
    const auto trustedTsbks = [](const P25LiveDecodeResult& r) {
        return std::count_if(r.rawTsbkBlocks.begin(), r.rawTsbkBlocks.end(), [](const P25TsbkBlock& block) {
            return block.fecDecoded && block.crcValid;
        });
    };
    const auto trustedPhase1Pdus = [](const P25LiveDecodeResult& r) {
        return std::count_if(r.phase1Pdus.begin(), r.phase1Pdus.end(), [](const P25Phase1PduMessage& pdu) {
            return pdu.headerFecDecoded && pdu.headerCrcValid;
        });
    };
    const auto aTrustedTsbks = trustedTsbks(a);
    const auto bTrustedTsbks = trustedTsbks(b);
    const auto aTrustedPdus = trustedPhase1Pdus(a);
    const auto bTrustedPdus = trustedPhase1Pdus(b);
    const auto aTrustedPhase1Control = aTrustedTsbks + aTrustedPdus;
    const auto bTrustedPhase1Control = bTrustedTsbks + bTrustedPdus;
    if (aTrustedPhase1Control != bTrustedPhase1Control) return aTrustedPhase1Control > bTrustedPhase1Control;
    const auto phase2VoiceCodewords = [](const P25LiveDecodeResult& r) {
        size_t out = 0;
        for (const auto& burst : r.phase2Bursts) out += burst.voiceCodewords.size();
        return out;
    };
    const auto phase2SlotVoiceCodewords = [](const P25LiveDecodeResult& r, uint8_t slot, bool matching) {
        size_t out = 0;
        for (const auto& burst : r.phase2Bursts) {
            if (!burst.grantSlotKnown || burst.voiceCodewords.empty()) continue;
            const bool slotMatches = static_cast<uint8_t>(burst.grantSlot & 0x01u) == static_cast<uint8_t>(slot & 0x01u);
            if (slotMatches == matching) out += burst.voiceCodewords.size();
        }
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
        if (config && config->phase2PreferredTdmaSlotKnown) {
            const uint8_t slot = static_cast<uint8_t>(config->phase2PreferredTdmaSlot & 0x01u);
            const size_t targetVoice = phase2SlotVoiceCodewords(r, slot, true);
            const size_t oppositeVoice = phase2SlotVoiceCodewords(r, slot, false);
            score += static_cast<int>(targetVoice) * 36;
            score -= static_cast<int>(oppositeVoice) * 12;
            if (targetVoice > 0 && targetVoice >= oppositeVoice) score += 80;
            if (targetVoice == 0 && oppositeVoice > 0) score -= 80;
        }
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
    const bool phase2HardEvidencePresent =
        a.stats.phase2MacCrcValid > 0 || b.stats.phase2MacCrcValid > 0 ||
        a.stats.phase2EssKnown || b.stats.phase2EssKnown;
    const auto aValidNids = validNids(a);
    const auto bValidNids = validNids(b);
    if (aTrustedPhase1Control == 0 && bTrustedPhase1Control == 0) {
        const int aP2 = phase2TelemetryScore(a);
        const int bP2 = phase2TelemetryScore(b);
        if (aP2 != bP2 &&
            (phase2HardEvidencePresent || (aValidNids == bValidNids && std::max(aP2, bP2) >= 8))) {
            return aP2 > bP2;
        }
    }

    if (aValidNids != bValidNids) return aValidNids > bValidNids;
    if (aValidNids > 0 && bValidNids > 0) {
        const int aBestNidCorrections = bestValidatedNidCorrections(a);
        const int bBestNidCorrections = bestValidatedNidCorrections(b);
        if (aBestNidCorrections != bBestNidCorrections) {
            return aBestNidCorrections < bBestNidCorrections;
        }
    }
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
    if (a.stats.bestNidBchDistance >= 0 && b.stats.bestNidBchDistance >= 0 &&
        a.stats.bestNidBchDistance != b.stats.bestNidBchDistance) {
        return a.stats.bestNidBchDistance < b.stats.bestNidBchDistance;
    }
    if (a.stats.bestFrameSyncBitErrors >= 0 && b.stats.bestFrameSyncBitErrors >= 0 &&
        a.stats.bestFrameSyncBitErrors != b.stats.bestFrameSyncBitErrors) {
        return a.stats.bestFrameSyncBitErrors < b.stats.bestFrameSyncBitErrors;
    }
    const int aTrust = liveResultTrustScore(a);
    const int bTrust = liveResultTrustScore(b);
    if (aTrust != bTrust) return aTrust > bTrust;
    const bool aCqpsk = isCqpskPath(a.stats.demodPath);
    const bool bCqpsk = isCqpskPath(b.stats.demodPath);
    if (aCqpsk != bCqpsk &&
        (aTrustedPhase1Control > 0 || aValidNids > 0) &&
        (bTrustedPhase1Control > 0 || bValidNids > 0)) {
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

    const bool trustedPhase1Pdu = std::any_of(r.phase1Pdus.begin(), r.phase1Pdus.end(), [](const P25Phase1PduMessage& pdu) {
        return pdu.headerFecDecoded && pdu.headerCrcValid;
    });
    if (trustedPhase1Pdu) return true;

    const bool trustedNid = std::any_of(r.nids.begin(), r.nids.end(), [](const P25Nid& nid) {
        return nid.fecValidated;
    });
    if (trustedNid) return true;

    if (r.stats.phase2MacCrcValid > 0 || r.stats.phase2EssKnown) return true;

    return std::any_of(r.imbeFrames.begin(), r.imbeFrames.end(), [](const P25ImbeFrame& frame) {
        return frame.valid;
    });
}

bool hasPhase2SoftCqpskLockEvidence(const P25LiveDecodeResult& r)
{
    if (!isCqpskPath(r.stats.demodPath)) return false;
    if (hasCqpskHardLockEvidence(r)) return true;

    const bool goodBurstSync =
        r.stats.bestPhase2SyncErrors >= 0 &&
        r.stats.bestPhase2SyncErrors <= 3;

    // Promote soft CQPSK lock once TDMA structure is real enough to stream
    // (SDRTrunk keeps demod locked and consumes).  Do NOT use raw macPdus —
    // field eyes often show mac=0/145 junk which froze a wrong permutation and
    // blocked later mask/VCW (TG10609 PASS regression).
    if (r.stats.phase2Bursts >= 1 && goodBurstSync &&
        (r.stats.phase2SuperframeBursts >= 1 ||
         r.stats.phase2MaskedBursts >= 1 ||
         r.stats.phase2IschDecoded >= 1 ||
         r.stats.phase2VoiceCodewords >= 1 ||
         r.stats.phase2MacCrcValid > 0)) {
        return true;
    }
    // Sync-only CQPSK probe path (no mask decode yet) still proves a TDMA eye
    // once several low-error Phase-2 syncs land — enough to stop the permutation
    // grid and commit one full annotate pass (SDRTrunk locks demod on sync).
    if (r.stats.phase2Bursts >= 3 && goodBurstSync) {
        return true;
    }
    if (r.stats.phase2Bursts >= 3 && goodBurstSync &&
        (r.stats.phase2SuperframeBursts >= 1 || r.stats.phase2VoiceCodewords >= 1)) {
        return true;
    }
    if (r.stats.phase2SuperframeBursts >= 2 && r.stats.phase2MaskedBursts >= 1) return true;
    if (r.stats.phase2VoiceCodewords >= 2 && goodBurstSync) return true;

    const bool superframeMask =
        r.stats.phase2SuperframeBursts >= 6 &&
        r.stats.phase2MaskedBursts >= 6;
    const bool targetLikeVoice =
        r.stats.phase2VoiceCodewords >= 4 &&
        r.stats.phase2MacCrcValid > 0;

    // Still weaker than the MAC/ESS speaker gate: soft lock only holds demod.
    return superframeMask && targetLikeVoice && goodBurstSync;
}

bool hasCqpskSoftContinuityEvidence(const P25LiveDecodeResult& r)
{
    if (!isCqpskPath(r.stats.demodPath)) return false;
    if (hasCqpskHardLockEvidence(r)) return true;
    if (r.stats.phase2VoiceCodewords > 0 ||
        r.stats.phase2SuperframeBursts > 0 ||
        r.stats.phase2MaskedBursts > 0 ||
        r.stats.phase2MacPdus > 0 ||
        r.stats.phase2IschDecoded > 0 ||
        r.stats.bestPhase2SyncErrors >= 0 ||
        !r.phase2Bursts.empty()) {
        return true;
    }
    if (r.stats.bestFrameSyncBitErrors >= 0 &&
        r.stats.bestFrameSyncBitErrors <= 3) {
        return true;
    }
    if (r.stats.softDecisionSymbols >= 96 &&
        r.stats.softDecisionQuality >= 0.08) {
        return true;
    }
    return r.stats.symbols >= 96 &&
        r.stats.symbolConfidence >= 0.02 &&
        r.stats.softDecisionSymbols >= 96;
}

bool hasPhase2TrafficTelemetry(const P25LiveDecodeResult& r)
{
    return r.stats.phase2VoiceCodewords > 0 ||
        r.stats.phase2SuperframeBursts > 0 ||
        r.stats.phase2MaskedBursts > 0 ||
        r.stats.bestPhase2SyncErrors >= 0 ||
        !r.phase2Bursts.empty();
}

bool hasTrustedPhase1ControlPayload(const P25LiveDecodeResult& r)
{
    const bool trustedTsbk = std::any_of(r.rawTsbkBlocks.begin(), r.rawTsbkBlocks.end(),
        [](const P25TsbkBlock& block) {
            return block.fecDecoded && block.crcValid;
        });
    if (trustedTsbk) return true;

    return std::any_of(r.phase1Pdus.begin(), r.phase1Pdus.end(),
        [](const P25Phase1PduMessage& pdu) {
            return pdu.headerFecDecoded && pdu.headerCrcValid;
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
            popcount64(static_cast<unsigned>(data & kP25BchGeneratorRows[i])) & 1u);
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
        const int distance = static_cast<int>(popcount64(raw ^ table[i]));
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
    // Relaxed slightly to 12 to reduce persistent "NID BCH-fail" on real CC (high symbol errors on simulcast/LSM);
    // still requires unique best. TSBK/ grant parsing needs valid NID; follow still gated on low-corr TSBK + repeat for high.
    if (bestDistance <= 12 && bestDistance < secondDistance) {
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
    const int syncErrors = static_cast<int>(popcount64(raw ^ P25LiveDecoder::Phase2FrameSyncWord));
    int bestErrors = 41;
    int secondErrors = 41;
    uint16_t bestInfo = 0;
    for (uint16_t info = 0; info < 512; ++info) {
        const int errors = static_cast<int>(popcount64(raw ^ phase2EncodeIschInfo(info)));
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
        const int distance = popcount64(static_cast<unsigned>(codeword ^ expected));
        if (distance < out.errors) {
            out.errors = distance;
            out.duid = duid;
        }
    }
    // Match sdrtrunk DataUnitID.fromEncodedValue: exact table lookup first, then
    // nearest encoded DUID at Hamming distance <= 2. Equal-distance ties retain
    // the earlier enum/codeword because the comparison is strictly-less-than.
    // Unknown results become UnknownTimeslot-equivalent and never produce AMBE.
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

bool phase2BurstKindCarriesAcch(P25Phase2BurstKind kind)
{
    return kind == P25Phase2BurstKind::SacchScrambled ||
        kind == P25Phase2BurstKind::FacchScrambled ||
        kind == P25Phase2BurstKind::SacchClear ||
        kind == P25Phase2BurstKind::FacchClear ||
        kind == P25Phase2BurstKind::LcchClear;
}

bool phase2MacPduIsNominalCrc(const P25Phase2MacPdu& pdu)
{
    const bool nominalKind =
        pdu.detectedKind == P25Phase2BurstKind::Unknown ||
        pdu.detectedKind == pdu.source;
    return pdu.crcValid &&
        nominalKind &&
        !pdu.acchBitOrderSwapped &&
        !pdu.acchDibitInverted &&
        pdu.acchSlipDibits == 0;
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

struct Rs63P25Tables {
    std::array<int, 64> alphaTo{};
    std::array<int, 64> indexOf{};
};

const Rs63P25Tables& rs63P25Tables();

std::array<uint8_t, 28> rs63Remainder(const std::array<uint8_t, 63>& codeword)
{
    const auto& gf = rs63P25Tables();
    std::array<uint8_t, 28> rem{};
    for (int root = 1; root <= 28; ++root) {
        int syndrome = 0;
        for (int symbol = 0; symbol < 63; ++symbol) {
            const int value = static_cast<int>(codeword[static_cast<size_t>(symbol)] & 0x3fu);
            const int index = gf.indexOf[static_cast<size_t>(value)];
            if (index != -1) {
                syndrome ^= gf.alphaTo[static_cast<size_t>((index + root * symbol) % 63)];
            }
        }
        rem[static_cast<size_t>(root - 1)] = static_cast<uint8_t>(syndrome & 0x3f);
    }
    return rem;
}

struct Phase2RsDecodeResult {
    bool ok = false;
    std::array<uint8_t, 63> symbols{};
    int correctedSymbols = 0;
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
    P25DecoderTraceScope trace("rs63DecodeBerlekampMasseyP25");
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
    P25DecoderTraceScope trace("rs63DecodeWithUnknownSymbolErrors");
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
    if (bm.ok) {
        const auto check = rs63Remainder(bm.symbols);
        if (std::all_of(check.begin(), check.end(), [](uint8_t v) { return (v & 0x3fu) == 0; })) {
            return bm;
        }
    }

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
        // Full C(n,2) over ~45 FACCH symbols is hundreds of RS erasure solves and
        // was the multi-second / ~10s hang once 6000 baud produced real sticky hits.
        // Keep a bounded pair budget for rare late-entry MAC recovery only.
        constexpr size_t kMaxPairTrials = 48;
        size_t pairTrials = 0;
        for (size_t i = 0; i < candidates.size(); ++i) {
            for (size_t j = i + 1; j < candidates.size(); ++j) {
                if (++pairTrials > kMaxPairTrials) return base;
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
    uint8_t essBNext = 0;
    // Legacy retained flag now means current-call PTT anchor, not just "any
    // CRC-valid MAC existed".  Generic MAC CRC is useful for mask confidence,
    // but must not authorize ESS/security or audio release.
    bool macCrcSeen = false;
    bool pttSeen = false;
    bool activeSeen = false;
    bool endPttSeen = false;
    bool idleSeen = false;
    bool hangtimeSeen = false;
    bool securityStateFromPtt = false;
    bool maskPhaseMacCrcSeen = false;
    bool essTrusted = false;
    int first4vSlot = -1;
    bool trafficSecurityKnown = false;
    bool trafficEncrypted = false;
    bool trafficTalkgroupKnown = false;
    uint32_t trafficTalkgroupId = 0;

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
    session.essBNext = 0;
    session.essHypotheses = {};
    session.essBHypotheses = {};
    session.essBSeenHypotheses = {};
    session.tentativeEss = {};
    session.tentativeEssRepeats = 0;
}

void phase2ResetEssAssembly(Phase2SessionState& session)
{
    session.essB = {};
    session.essBSeen = {};
    session.essBNext = 0;
    session.essHypotheses = {};
    session.essBHypotheses = {};
    session.essBSeenHypotheses = {};
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
    session.securityStateFromPtt = false;
    session.maskPhaseMacCrcSeen = false;
    session.trafficSecurityKnown = false;
    session.trafficEncrypted = false;
    session.trafficTalkgroupKnown = false;
    session.trafficTalkgroupId = 0;
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
        session.securityStateFromPtt = false;
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

size_t phase2MacStructureLengthForLiveValidation(const std::vector<uint8_t>& bytes, size_t offset)
{
    if (offset >= bytes.size()) return 0;
    const uint8_t op = bytes[offset];
    switch (op) {
        case 0x00: return 1;   // Null / pad.
        case 0x01: return 7;
        case 0x02: return 8;
        case 0x03: return 7;
        case 0x05: return 16;
        case 0x21: return 14;
        case 0x22: return 15;
        case 0x25: return 15;
        case 0x30: return 5;
        case 0x31: return 7;
        case 0x40: return 9;
        case 0x41: return 7;
        case 0x42: return 9;
        case 0x44: return 9;
        case 0x45: return 10;
        case 0x46: return 9;
        case 0x48: return 10;
        case 0x49: return 10;
        case 0x4a: return 7;
        case 0x4c: return 10;
        case 0x52: return 8;
        case 0x53: return 9;
        case 0x54: return 9;
        case 0x55: return 7;
        case 0x58: return 10;
        case 0x5a: return 7;
        case 0x5c: return 10;
        case 0x5d: return 8;
        case 0x5e: return 14;
        case 0x5f: return 7;
        case 0x60: return 9;
        case 0x61: return 9;
        case 0x64: return 9;
        case 0x67: return 9;
        case 0x68: return 10;
        case 0x6a: return 7;
        case 0x6b: return 10;
        case 0x6c: return 10;
        case 0x6d: return 7;
        case 0x6f: return 9;
        case 0x70: return 9;
        case 0x71: return 18;
        case 0x72: return 9;
        case 0x73: return 9;
        case 0x74: return 9;
        case 0x75: return 9;
        case 0x76: return 10;
        case 0x77: return 13;
        case 0x78: return 9;
        case 0x79: return 9;
        case 0x7a: return 9;
        case 0x7b: return 11;
        case 0x7c: return 9;
        case 0x7d: return 9;
        case 0x88: return 5;
        case 0x90: return 7;
        case 0xc0: return 11;
        case 0xc3: return 8;
        case 0xc4: return 15;
        case 0xc5: return 14;
        case 0xc6: return 15;
        case 0xc7: return 18;
        case 0xc8: return 12;
        case 0xc9: return 12;
        case 0xcb: return 18;
        case 0xcc: return 14;
        case 0xcd: return 18;
        case 0xce: return 18;
        case 0xcf: return 18;
        case 0xd6: return 9;
        case 0xd8: return 14;
        case 0xd9: return 18;
        case 0xda: return 11;
        case 0xdb: return 18;
        case 0xdc: return 14;
        case 0xde: return 18;
        case 0xdf: return 11;
        case 0xe0: return 18;
        case 0xe4: return 17;
        case 0xe5: return 14;
        case 0xe8: return 16;
        case 0xe9: return 8;
        case 0xea: return 11;
        case 0xec: return 13;
        case 0xf2: return 16;
        case 0xf3: return 14;
        case 0xfa: return 11;
        case 0xfb: return 13;
        case 0xfc: return 11;
        case 0xfe: return 15;
        default:
            break;
    }

    if ((op == 0x08 || op == 0x10 || op == 0x11 || op == 0x12) &&
        offset + 1 < bytes.size()) {
        const size_t len = bytes[offset + 1] & 0x3fu;
        if (len >= 2) return len;
    }

    if ((op & 0xc0u) == 0x80u && offset + 1 < bytes.size()) {
        const uint8_t mfid = bytes[offset + 1];
        if (mfid == 0x90) {
            switch (op) {
                case 0x80: return 8;
                case 0x81: return 17;
                case 0x83: return 7;
                case 0x84: return 11;
                case 0x89: return 17;
                case 0x91: return 17;
                case 0x95: return 17;
                case 0xa0: return 16;
                case 0xa3: return 11;
                case 0xa4: return 13;
                case 0xa5: return 11;
                case 0xa6: return 11;
                case 0xa7: return 11;
                case 0xa8: return 10;
                default: break;
            }
        } else if (mfid == 0xa4) {
            switch (op) {
                case 0xa0: return 9;
                case 0xaa: return 17;
                case 0xac: return 12;
                default: break;
            }
        }
    }

    if ((op & 0xc0u) == 0x80u && offset + 2 < bytes.size()) {
        const size_t len = bytes[offset + 2] & 0x3fu;
        if (len >= 3) return len;
    }
    return 0;
}

bool phase2AllRemainingZero(const std::vector<uint8_t>& bytes, size_t offset)
{
    return std::all_of(bytes.begin() + static_cast<std::ptrdiff_t>(std::min(offset, bytes.size())),
                       bytes.end(),
                       [](uint8_t b) { return b == 0; });
}

uint16_t phase2ReadU16Msb(const std::vector<uint8_t>& bytes, size_t offset)
{
    if (offset + 1 >= bytes.size()) return 0;
    return static_cast<uint16_t>((static_cast<uint16_t>(bytes[offset]) << 8) |
                                 static_cast<uint16_t>(bytes[offset + 1]));
}

struct Phase2TrafficMacSecurity {
    bool known = false;
    bool encrypted = false;
    bool talkgroupKnown = false;
    uint32_t talkgroupId = 0;
};

Phase2TrafficMacSecurity phase2TrafficMacSecurityFromCrcPdu(const P25Phase2MacPdu& pdu)
{
    Phase2TrafficMacSecurity out;
    if (!pdu.crcValid || pdu.bytes.empty()) return out;

    const uint8_t pduType = static_cast<uint8_t>((pdu.bytes[0] >> 5) & 0x07u);
    // MAC_ACTIVE and MAC_HANGTIME may both carry MAC structures such as Group
    // Voice Channel User.  The latter is not an audio-release event, but its
    // service options are authoritative encryption evidence for the target TG.
    if (pduType != 4 && pduType != 6) return out; // PTT/ESS is handled separately.

    const size_t maxStartBit = pdu.macStructureMaxBits > 0
        ? std::min(pdu.macStructureMaxBits, pdu.bytes.size() * 8u)
        : pdu.bytes.size() * 8u;
    size_t pos = 1;
    while (pos < pdu.bytes.size() && pos * 8u < maxStartBit) {
        if (phase2AllRemainingZero(pdu.bytes, pos)) break;
        const uint8_t op = pdu.bytes[pos];
        if (op == 0x00) break;
        const size_t len = phase2MacStructureLengthForLiveValidation(pdu.bytes, pos);
        if (len == 0 || pos + len > pdu.bytes.size()) break;

        if ((op == 0x01 || op == 0x21) && len >= 7 && pos + 4 < pdu.bytes.size()) {
            out.known = true;
            out.encrypted = (pdu.bytes[pos + 1] & 0x40u) != 0;
            out.talkgroupKnown = true;
            out.talkgroupId = phase2ReadU16Msb(pdu.bytes, pos + 2);
            return out;
        }

        if ((op == 0x80 || op == 0xa0) && pos + 4 < pdu.bytes.size() && pdu.bytes[pos + 1] == 0x90) {
            const size_t groupOffset = op == 0x80 ? pos + 3 : pos + 4;
            if (groupOffset + 1 < pdu.bytes.size()) {
                out.known = true;
                out.encrypted = (pdu.bytes[pos + 2] & 0x40u) != 0;
                out.talkgroupKnown = true;
                out.talkgroupId = phase2ReadU16Msb(pdu.bytes, groupOffset);
                return out;
            }
        }

        pos += len;
    }

    return out;
}

bool phase2DirectCrcMacPduLooksSdrtrunkParseable(const P25Phase2MacPdu& pdu, size_t macStructureMaxBits)
{
    if (pdu.bytes.empty()) return false;
    const uint8_t pduType = static_cast<uint8_t>((pdu.bytes[0] >> 5) & 0x07u);
    if (pduType == 5 || pduType == 7) return false;

    switch (pduType) {
        case 1: // MAC_PTT has fixed ESS/current-call fields instead of MAC structures.
            return pdu.bytes.size() >= 13u;
        case 2: // MAC_END_PTT.
            return pdu.bytes.size() >= 3u;
        case 0:
        case 3:
        case 4:
        case 6:
            break;
        default:
            return false;
    }

    const size_t maxStartBit = macStructureMaxBits > 0
        ? std::min(macStructureMaxBits, pdu.bytes.size() * 8u)
        : pdu.bytes.size() * 8u;
    size_t pos = 1;
    size_t structures = 0;
    while (pos < pdu.bytes.size() && pos * 8u < maxStartBit) {
        if (phase2AllRemainingZero(pdu.bytes, pos)) return true;
        const uint8_t op = pdu.bytes[pos];
        if (op == 0x00) return true;
        const size_t len = phase2MacStructureLengthForLiveValidation(pdu.bytes, pos);
        if (len == 0 || pos + len > pdu.bytes.size()) return false;
        ++structures;
        if (structures >= 3) return true;
        pos += len;
    }
    return structures > 0;
}

std::optional<P25Phase2MacPdu> decodePhase2Acch(const std::vector<int>& payloadDibits,
                                                P25Phase2BurstKind kind,
                                                size_t dibitOffset,
                                                bool deepSearch = true,
                                                bool alternateHypotheses = true)
{
    P25DecoderTraceScope trace("decodePhase2Acch");
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
        const size_t macStructureMaxBits = fast ? 144u : (lcch ? 152u : 99u);
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
            pdu.macStructureMaxBits = macStructureMaxBits;
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
        // Keep a direct information-field CRC probe as a recovery path, but only
        // promote it when the resulting MAC PDU has a structure that SDRTrunk's
        // MAC factory would actually parse.  CRC-12 is too small to let an
        // otherwise unknown MAC_ACTIVE/opcode pattern become hard mask/audio
        // evidence by itself.
        std::vector<uint8_t> directBits;
        std::optional<P25Phase2MacPdu> directDiagnosticPdu;
        if (codedBits.size() >= totalPayloadBits) {
            directBits.assign(codedBits.begin(), codedBits.begin() + static_cast<std::ptrdiff_t>(totalPayloadBits));
            const bool directCrcOk = lcch ? p25Phase2Crc16Ok(directBits, crcProtectedBits)
                                          : p25Phase2Crc12Ok(directBits, crcProtectedBits);
            auto directPdu = makePduFromBits(directBits, false, directCrcOk, 0);
            directPdu.directCrcOk = directCrcOk;
            directPdu.directCrcParseable =
                directCrcOk && phase2DirectCrcMacPduLooksSdrtrunkParseable(directPdu, macStructureMaxBits);
            directDiagnosticPdu = directPdu;
            if (directPdu.directCrcParseable) {
                directPdu.valid = true;
                directPdu.crcValid = true;
                return directPdu;
            }
            if (directDiagnosticPdu) {
                directDiagnosticPdu->valid = false;
                directDiagnosticPdu->crcValid = false;
                directDiagnosticPdu->essPresent = false;
                directDiagnosticPdu->ess = {};
            }
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

        // Keep the live path close to sdrtrunk's Reed-Solomon behavior instead
        // of accepting p2sf/p2mask/VCW telemetry without a recoverable MAC.
        // Shallow hot path: BM + known erasures only (maxUnknown=0).  The old
        // maxUnknown=1 single-symbol hunt × 5 ACCH kinds × 12 slots was ~4–7s
        // per sustain window after 6000 baud lock.
        // Deep committed/sticky repair: maxUnknown=2 on nominal layout only
        // (swap/slip/invert off), still pair-capped.  Alt layouts stay at 1.
        const bool nominalLayout = !swapBitOrder && !invertDibit && acchSlipDibits == 0;
        const int maxUnknownSymbols = deepSearch ? (nominalLayout ? 2 : 1) : 0;
        const auto rs = rs63DecodeWithUnknownSymbolErrors(symbols, erasures, transmittedPositions, maxUnknownSymbols);
        if (!rs.ok) {
            // Return a non-CRC diagnostic candidate instead of null so the field
            // log can distinguish "ACCH was extracted but neither direct CRC nor
            // RS recovered it" from "no ACCH extraction path was reached".
            if (directDiagnosticPdu) return directDiagnosticPdu;
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
        auto pdu = makePduFromBits(decodedBits,
                                   true,
                                   crcOk,
                                   std::max(0, rs.correctedSymbols - static_cast<int>(erasures.size())));
        pdu.rsDecoded = true;
        return pdu;
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
    if (!alternateHypotheses) return bestNonCrc;
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

void phase2StoreVoiceEssB(const std::vector<int>& payloadDibits, int burstId, Phase2SessionState& session)
{
    const size_t essStart = 74;
    if (payloadDibits.size() < 160 || burstId < 0 || burstId > 3 || essStart + 12 > payloadDibits.size()) return;

    // sdrtrunk maps Phase 2 ESS as:
    //   Voice4: 24-bit ESS-B fragment at timeslot bits 148..171
    //   Voice2: 168-bit ESS-A fragment at timeslot bits 148..243 and 246..317
    // In dibits within the 320-bit timeslot, ESS starts at bit 148 / dibit 74.
    for (int i = 0; i < 12; i += 3) {
        session.essB[static_cast<size_t>(burstId * 4 + (i / 3))] =
            static_cast<uint8_t>(((payloadDibits[essStart + static_cast<size_t>(i)] & 0x03) << 4) |
                                 ((payloadDibits[essStart + static_cast<size_t>(i + 1)] & 0x03) << 2) |
                                 (payloadDibits[essStart + static_cast<size_t>(i + 2)] & 0x03));
    }
    session.essBSeen[static_cast<size_t>(burstId)] = true;
}

std::optional<P25Phase2EssState> decodePhase2VoiceEssA(const std::vector<int>& payloadDibits,
                                                       Phase2SessionState& session)
{
    P25DecoderTraceScope trace("decodePhase2VoiceEssA");
    if (payloadDibits.size() < 160) return std::nullopt;
    const size_t essStart = 74;
    if (essStart + 84 > payloadDibits.size()) return std::nullopt;

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
    // ESS runs in the same realtime traffic path as AMBE.  Allow one unknown
    // transmitted symbol (same bound as shallow ACCH) so late-entry clear ESS
    // can recover when a single hexbit is wrong; do not open C(n,2) here.
    const auto rs = rs63DecodeWithUnknownSymbolErrors(codeword, erasures, essTransmittedPositions, 1);
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

std::optional<P25Phase2EssState> decodePhase2VoiceEss(const std::vector<int>& payloadDibits,
                                                       int burstId,
                                                       Phase2SessionState& session)
{
    if (burstId < 0 || burstId > 4) return std::nullopt;
    if (burstId < 4) {
        phase2StoreVoiceEssB(payloadDibits, burstId, session);
        return std::nullopt;
    }
    return decodePhase2VoiceEssA(payloadDibits, session);
}

std::optional<P25Phase2EssState> decodePhase2VoiceEssSdrtrunkOrder(const std::vector<int>& payloadDibits,
                                                                   P25Phase2BurstKind kind,
                                                                   Phase2SessionState& session)
{
    if (kind == P25Phase2BurstKind::Voice4) {
        // sdrtrunk does not derive ESS-B1..B4 from a superframe index.  Each
        // traffic-slot ESS processor simply consumes Voice4 timeslots in order,
        // then Voice2 supplies ESS-A and triggers an RS decode/reset.
        const int burstId = static_cast<int>(session.essBNext % 4u);
        phase2StoreVoiceEssB(payloadDibits, burstId, session);
        session.essBNext = static_cast<uint8_t>((session.essBNext + 1u) % 4u);
        return std::nullopt;
    }
    if (kind == P25Phase2BurstKind::Voice2) {
        auto ess = decodePhase2VoiceEssA(payloadDibits, session);
        phase2ResetEssAssembly(session);
        return ess;
    }
    return std::nullopt;
}

std::optional<P25Phase2EssState> decodePhase2VoiceEssWithCallEpoch(const std::vector<int>& payloadDibits,
                                                                   P25Phase2BurstKind kind,
                                                                   int logicalVoiceIndex,
                                                                   Phase2SessionState& session)
{
    if (logicalVoiceIndex < 0) return std::nullopt;

    auto tryEpoch = [&](Phase2SessionState& target, int first4vSlot) -> std::optional<P25Phase2EssState> {
        int epochSlot = logicalVoiceIndex;
        if (epochSlot < first4vSlot) epochSlot += 5;
        const int burstId = epochSlot - first4vSlot;
        if (burstId < 0 || burstId > 4) return std::nullopt;
        if (kind == P25Phase2BurstKind::Voice4 && burstId >= 0 && burstId < 4) {
            return decodePhase2VoiceEss(payloadDibits, burstId, target);
        }
        if (kind == P25Phase2BurstKind::Voice2 && burstId == 4) {
            auto ess = decodePhase2VoiceEss(payloadDibits, burstId, target);
            phase2ResetEssAssembly(target);
            return ess;
        }
        return std::nullopt;
    };

    if (session.first4vSlot >= 0 && session.first4vSlot < 5) {
        return tryEpoch(session, session.first4vSlot);
    }

    // Late-entry voice channels often have no immediately visible MAC_PTT.  A
    // single rolling ESS-B counter can be off by one full fragment cycle, so
    // keep all legal first-4V hypotheses alive and accept only an RS-validated
    // ESS.  This mirrors SDRTrunk's fragment model while avoiding indefinite
    // "ESS unknown" when we joined after the call setup MAC.
    for (int first4vSlot = 0; first4vSlot < 5; ++first4vSlot) {
        Phase2SessionState hypothesis;
        const auto index = static_cast<size_t>(first4vSlot);
        hypothesis.ess = session.essHypotheses[index];
        hypothesis.essB = session.essBHypotheses[index];
        hypothesis.essBSeen = session.essBSeenHypotheses[index];
        hypothesis.first4vSlot = first4vSlot;

        bool accepted = false;
        if (auto ess = tryEpoch(hypothesis, first4vSlot)) {
            accepted = phase2AcceptVoiceEss(hypothesis, *ess);
        }

        session.essHypotheses[index] = hypothesis.ess;
        session.essBHypotheses[index] = hypothesis.essB;
        session.essBSeenHypotheses[index] = hypothesis.essBSeen;

        if (accepted && hypothesis.essTrusted) {
            session.ess = hypothesis.ess;
            session.essTrusted = true;
            session.securityStateFromPtt = false;
            session.tentativeEss = {};
            session.tentativeEssRepeats = 0;
            session.essB = hypothesis.essB;
            session.essBSeen = hypothesis.essBSeen;
            session.first4vSlot = first4vSlot;
            return session.ess;
        }
    }

    return std::nullopt;
}

struct Phase2SyncHit {
    size_t dibitOffset = 0;
    int errors = 0;
};

struct Phase2SuperframeLock {
    size_t dibitOffset = 0;
    int syncScore = 0;
    int syncErrors = 0;
    int ischScore = 0;
    int ischHits = 0;
    int ischMisses = 0;
};

constexpr std::array<size_t, 6> kPhase2PreferredSuperframeSyncSlots{2, 3, 6, 7, 10, 11};
constexpr std::array<size_t, 12> kPhase2AllBurstSlots{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};

bool phase2OffsetInWindow(size_t offset, size_t start, size_t length)
{
    return offset >= start && offset < start + length;
}

int phase2LockTotalScore(const Phase2SuperframeLock& lock) noexcept
{
    return lock.syncScore * 40 - lock.syncErrors * 2 + lock.ischScore;
}

void phase2ScoreLockIschEpoch(const std::vector<int>& dibits,
                              Phase2SuperframeLock& lock,
                              size_t dibitCount)
{
    // SDRTrunk's Phase-2 detector emits 720-dibit superframe fragments whose
    // A/B I-ISCH words identify fragment location 1/3, 2/3, or 3/3.  A rolling
    // live window can contain several sync-only epoch hypotheses with similar
    // S-ISCH scores; selecting one burst early/late flips TS1/TS2 ownership.
    // Use decoded I-ISCH location as standards-level epoch evidence.
    static constexpr std::array<size_t, 6> kIischSlots{0, 1, 4, 5, 8, 9};
    int score = 0;
    int hits = 0;
    int misses = 0;
    for (size_t slot : kIischSlots) {
        const size_t expected = lock.dibitOffset + slot * P25LiveDecoder::Phase2BurstDibits;
        if (expected + P25LiveDecoder::Phase2FrameSyncDibits > dibitCount ||
            expected + P25LiveDecoder::Phase2FrameSyncDibits > dibits.size()) {
            continue;
        }

        const auto isch = decodePhase2IschAt(dibits, expected);
        if (!isch.valid || isch.sync) continue;

        const bool lowError = isch.errors >= 0 && isch.errors < 3;
        const bool usableLocation = isch.location <= 2;
        const size_t expectedLocation = slot / 4u;
        const bool locationMatches = usableLocation &&
            static_cast<size_t>(isch.location) == expectedLocation;

        if (locationMatches) {
            ++hits;
            score += lowError ? 260 : 90;
        } else {
            ++misses;
            score -= lowError ? 420 : 140;
        }

        // The channel-number field is noisy on some captures, so it is a small
        // tie-break only.  The fragment location above is the hard epoch anchor.
        if (locationMatches && lowError && isch.channel <= 1) {
            const uint8_t expectedChannel = static_cast<uint8_t>(slot & 0x01u);
            score += (isch.channel == expectedChannel) ? 40 : -40;
        }
    }

    // One isolated I-ISCH word can be a synthetic unit-test injection or a bad
    // correction.  Require at least two decoded I-ISCH words before applying a
    // hard negative epoch penalty.  A single match remains a useful tie-break.
    const int evidence = hits + misses;
    if (evidence >= 2 || hits > 0) {
        lock.ischScore += score;
        lock.ischHits += hits;
        lock.ischMisses += misses;
    }
}


// sdrtrunk Phase-2 superframe channel/timeslot order is not a simple
// even/odd parity for all 12 bursts.  Each 1/3 superframe fragment carries
// A,B,C,D timeslots; the final fragment swaps C/D when presenting logical
// traffic-channel order.  The physical scrambling mask still uses the raw
// 0..11 burst index, but grant-slot filtering and voice ESS sequencing must
// use this logical traffic slot mapping.
//
// sdrtrunk SuperFrameFragment (3 fragments x 4 timeslots A/B/C/D):
//   non-final: A=TS1, B=TS2, C=TS1, D=TS2
//   final frag (bursts 8..11): A=TS1, B=TS2, C=TS2, D=TS1  (C/D channel swap)
// Grant slot 0 = TIMESLOT_1, grant slot 1 = TIMESLOT_2.
// Dual-slot RF carries two independent calls; never mix them.
//
// Important: XOR mask phase is a scrambling-sequence index, not a timeslot
// relabel.  SDRTrunk applies the ISCH-derived offset when selecting the 320-bit
// scrambling segment, while the A/B/C/D timeslot owner remains bound to the
// physical fragment position.  Adding mask phase to the slot label makes a TS2
// grant look like TS1 on some captures and breaks continuous audio.
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

std::optional<uint8_t> phase2FragmentLocationFromIisch(const std::vector<int>& dibits,
                                                       size_t fragmentStartDibit)
{
    std::optional<uint8_t> location;

    // A and B are the I-ISCH-bearing bursts in each 720-dibit fragment.
    // C/D carry S-ISCH, so use the A/B location when the whole fragment is in
    // the rolling buffer. If neither A nor B is usable, fall back to the
    // physical 0..11 mapping below.
    for (size_t local = 0; local < 2; ++local) {
        const size_t ischDibit = fragmentStartDibit + local * P25LiveDecoder::Phase2BurstDibits;
        if (ischDibit + P25LiveDecoder::Phase2FrameSyncDibits > dibits.size()) continue;

        const auto isch = decodePhase2IschAt(dibits, ischDibit);
        if (!isch.valid || isch.sync || isch.location > 2) continue;
        if (isch.errors >= 0 && isch.errors >= 3) continue;

        if (location && *location != isch.location) {
            return std::nullopt;
        }
        location = isch.location;
    }

    return location;
}

uint8_t phase2TrafficSlotFromFragmentLocalIndex(size_t fragmentLocalIndex,
                                                std::optional<uint8_t> fragmentLocation) noexcept
{
    const bool finalFragment = fragmentLocation && *fragmentLocation == 2u;
    switch (fragmentLocalIndex & 0x03u) {
        case 0: return 0;                  // A
        case 1: return 1;                  // B
        case 2: return finalFragment ? 1u : 0u; // C
        case 3:
        default: return finalFragment ? 0u : 1u; // D
    }
}

uint8_t phase2TrafficSlotForSuperframeBurst(const std::vector<int>& dibits,
                                            size_t superframeOffset,
                                            size_t superframeBurstIndex)
{
    // SDRTrunk binds A/B/C/D timeslot ownership to the physical fragment position.
    // I-ISCH fragment location is used for epoch scoring / scrambling-segment
    // selection — not for re-labeling grant slots.  Field 20260720_063846 showed
    // I-ISCH override flipping C/D ownership on a clear slot-0 call: target VCWs
    // were counted as oppVcw/wrongSlot, invent-PLC was correctly disabled, and
    // the speaker got empty-audio holes.  Keep physical mapping here.
    (void)dibits;
    (void)superframeOffset;
    return phase2TrafficSlotFromSuperframeBurstIndex(superframeBurstIndex);
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

int phase2LogicalVoiceSequenceIndexForSuperframeBurst(const std::vector<int>& dibits,
                                                      size_t superframeOffset,
                                                      size_t superframeBurstIndex)
{
    // Same rule as grant-slot labeling: ESS voice sequencing follows physical
    // channel order (including final-fragment C/D inversion in the 0..11 map).
    // Do not let a noisy I-ISCH location rewrite ESS assembly order mid-call.
    (void)dibits;
    (void)superframeOffset;
    return phase2LogicalVoiceSequenceIndex(superframeBurstIndex);
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

std::vector<Phase2SuperframeLock> findPhase2SuperframeLocks(const std::vector<int>& dibits,
                                                            const std::vector<Phase2SyncHit>& hits,
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
            phase2ScoreLockIschEpoch(dibits, lock, dibitCount);
            locks.push_back(lock);
        } else if (totalHits >= 3) {
            lock.syncScore = 1;
            lock.syncErrors += 6; // demote soft locks behind preferred locks.
            phase2ScoreLockIschEpoch(dibits, lock, dibitCount);
            locks.push_back(lock);
        }
    }

    std::sort(locks.begin(), locks.end(), [](const Phase2SuperframeLock& a, const Phase2SuperframeLock& b) {
        const int aTotal = phase2LockTotalScore(a);
        const int bTotal = phase2LockTotalScore(b);
        if (aTotal != bTotal) return aTotal > bTotal;
        if (a.ischHits != b.ischHits) return a.ischHits > b.ischHits;
        if (a.ischMisses != b.ischMisses) return a.ischMisses < b.ischMisses;
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

std::vector<Phase2SuperframeLock> findPhase2AnchorAlignedSuperframeLocks(const std::vector<Phase2SyncHit>& hits,
                                                                         const std::vector<int>& dibits,
                                                                         size_t dibitCount,
                                                                         uint64_t workingStreamStart,
                                                                         uint64_t anchorStreamDibit)
{
    std::vector<Phase2SuperframeLock> locks;
    if (hits.size() < 2 || dibitCount < P25LiveDecoder::Phase2BurstDibits * 4) return locks;

    constexpr uint64_t kSpan = static_cast<uint64_t>(P25LiveDecoder::Phase2BurstDibits) * 12ull;
    constexpr size_t kMaxSyncSlipDibits = 2;
    const uint64_t workingStreamEnd = workingStreamStart + static_cast<uint64_t>(dibitCount);

    const long double span = static_cast<long double>(kSpan);
    const long double firstF = std::floor((static_cast<long double>(workingStreamStart) -
                                           static_cast<long double>(anchorStreamDibit)) / span) - 1.0L;
    const long double lastF = std::ceil((static_cast<long double>(workingStreamEnd) -
                                         static_cast<long double>(anchorStreamDibit)) / span) + 1.0L;
    const long long first = static_cast<long long>(firstF);
    const long long last = static_cast<long long>(lastF);

    for (long long k = first; k <= last; ++k) {
        const long double startF = static_cast<long double>(anchorStreamDibit) +
                                   static_cast<long double>(k) * span;
        if (startF < 0.0L) continue;
        const uint64_t startStream = static_cast<uint64_t>(startF);
        if (startStream + kSpan <= workingStreamStart || startStream >= workingStreamEnd) continue;
        if (startStream < workingStreamStart) continue;
        const uint64_t startWorking64 = startStream - workingStreamStart;
        if (startWorking64 > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) continue;

        Phase2SuperframeLock lock;
        lock.dibitOffset = static_cast<size_t>(startWorking64);
        int preferredHits = 0;
        int totalHits = 0;
        for (size_t slot : kPhase2AllBurstSlots) {
            const size_t expected = lock.dibitOffset + slot * P25LiveDecoder::Phase2BurstDibits;
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
        if (preferredHits >= 2 || totalHits >= 3) {
            if (preferredHits == 0) lock.syncScore = std::max(lock.syncScore, 1);
            phase2ScoreLockIschEpoch(dibits, lock, dibitCount);
            locks.push_back(lock);
        }
    }

    std::sort(locks.begin(), locks.end(), [](const Phase2SuperframeLock& a, const Phase2SuperframeLock& b) {
        const int aTotal = phase2LockTotalScore(a);
        const int bTotal = phase2LockTotalScore(b);
        if (aTotal != bTotal) return aTotal > bTotal;
        if (a.ischHits != b.ischHits) return a.ischHits > b.ischHits;
        if (a.ischMisses != b.ischMisses) return a.ischMisses < b.ischMisses;
        if (a.syncScore != b.syncScore) return a.syncScore > b.syncScore;
        if (a.syncErrors != b.syncErrors) return a.syncErrors < b.syncErrors;
        return a.dibitOffset < b.dibitOffset;
    });
    return locks;
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
                                   bool deepAcchSearch = true,
                                   bool alternateAcchHypotheses = true,
                                   bool decodeAcch = true)
{
    P25DecoderTraceScope trace("decodePhase2BurstAt");
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
    const bool canApplyMask = (xorMask != nullptr);
    const size_t trafficSuperframeBurstIndex = superframeBurstIndex % 12u;
    const size_t maskSuperframeBurstIndex =
        (superframeBurstIndex + (canApplyMask ? static_cast<size_t>(xorMaskPhase) : 0u)) % 12u;
    burst.isch = decodePhase2IschAt(dibits, pos);
    burst.grantSlotKnown = superframeLocked;
    // Physical A/B/C/D map only (see phase2TrafficSlotForSuperframeBurst).
    burst.grantSlot = phase2TrafficSlotFromSuperframeBurstIndex(trafficSuperframeBurstIndex);

    // sdrtrunk SuperFrameFragment separates each 360-bit Phase-2 burst into a
    // 40-bit I/S-ISCH word followed by a contiguous 320-bit timeslot.  This
    // code works in dibits, so payload/timeslot starts after 20 dibits and is
    // exactly 160 dibits.  Phase 1 FDMA interleaves status symbols every 36
    // dibits; Phase 2 TDMA does not.  A prior "status strip" path here removed
    // every 36th dibit and, when the working buffer held later bursts, pulled
    // ISCH/payload dibits from the next burst into this timeslot.  That left
    // field logs at p2sf/p2mask/p2vcw high with p2mac=0/N forever.
    const size_t payload = pos + P25LiveDecoder::Phase2FrameSyncDibits;
    constexpr size_t kPhase2TimeslotPayloadDibits = 160;
    if (payload + kPhase2TimeslotPayloadDibits > dibits.size()) return burst;

    std::vector<int> payloadDibits(kPhase2TimeslotPayloadDibits, 0);
    std::vector<size_t> payloadSrcIndex(kPhase2TimeslotPayloadDibits, 0);
    for (size_t i = 0; i < kPhase2TimeslotPayloadDibits; ++i) {
        payloadDibits[i] = dibits[payload + i] & 0x03;
        payloadSrcIndex[i] = payload + i;
    }

    // sdrtrunk Timeslot uses DUID field bits 0,1,74,75,244,245,318,319,
    // i.e. dibits 0,37,122,159 of the 320-bit (clean) timeslot.
    burst.rawDuidCodeword = static_cast<uint8_t>(((payloadDibits[0] & 0x03) << 6) |
                                                 ((payloadDibits[37] & 0x03) << 4) |
                                                 ((payloadDibits[122] & 0x03) << 2) |
                                                 (payloadDibits[159] & 0x03));
    const auto duid = decodePhase2Duid(burst.rawDuidCodeword);
    burst.duid = duid.duid;
    burst.duidErrors = duid.errors;
    burst.kind = phase2BurstKindFromDuid(duid.duid);

    std::vector<int> rawPayloadDibits(kPhase2TimeslotPayloadDibits, 0);
    std::vector<int> descrambledPayloadDibits(kPhase2TimeslotPayloadDibits, 0);
    // If mask params known (from grant or set), always apply using the selected/current phase.
    // This makes descrambling consistent for followed target voice even if superframe lock is marginal in some windows.
    // Phase comes from sticky/previous lock or the per-window best search.
    // Note: do not recompute mask index from I-ISCH alone using (location*4 + index%4)
    // unless superframeBurstIndex is already absolute 0..11 — a lock that starts
    // mid-fragment would pick the wrong segment. I-ISCH is used in phase scoring
    // to choose xorMaskPhase instead (sdrtrunk SuperFrameFragment style).
    const size_t maskBurstIndex = maskSuperframeBurstIndex;
    burst.rawPayloadDibits.reserve(rawPayloadDibits.size());
    burst.maskedPayloadDibits.reserve(descrambledPayloadDibits.size());
    for (size_t i = 0; i < rawPayloadDibits.size(); ++i) {
        int d = payloadDibits[i] & 0x03;
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
            if (!decodeAcch) return std::nullopt;

            // Prefer the decoded DUID/kind, but do not let a single DUID error
            // suppress MAC acquisition.  Different vendors and weak simulcast
            // conditions can produce stable TDMA superframe/mask hypotheses with noisy
            // DUIDs.  Try alternate ACCH interpretations and accept only a
            // CRC-valid MAC for release; non-CRC decodes are kept only for FEC
            // diagnostics.
            //
            // Hot-path guard: always try nominal-layout ACCH across alternate
            // kinds first (maxUnknown=0, no bit-order/slip/invert fan-out).
            // Deep search (alt layouts) runs only on the rare sticky repair budget.
            // Scoring/bootstrap pass deepAcchSearch=false so they never explore
            // alt layouts.
            //
            // Voice DUID bursts on the shallow path skip ACCH fan-out entirely —
            // sdrtrunk extracts AMBE/ESS from voice timeslots and does not run
            // SACCH/FACCH RS on every Voice4/2.  Field sustain was spending
            // seconds re-trying 5 ACCH kinds per voice burst with p2mac=0/N.
            if (phase2BurstKindHasVoice(burst.kind) && !deepAcchSearch) {
                return std::nullopt;
            }
            std::optional<P25Phase2MacPdu> bestNonCrc;
            std::array<P25Phase2BurstKind, 6> candidates{
                burst.kind,
                P25Phase2BurstKind::SacchScrambled,
                P25Phase2BurstKind::SacchClear,
                P25Phase2BurstKind::FacchScrambled,
                P25Phase2BurstKind::FacchClear,
                P25Phase2BurstKind::LcchClear,
            };
            const bool allowAlternateKindFanout =
                alternateAcchHypotheses &&
                (deepAcchSearch || !superframeLocked || xorMask == nullptr);
            std::array<int, 16> seen{};
            size_t seenCount = 0;
            auto tryKind = [&](P25Phase2BurstKind k, bool deep) -> std::optional<P25Phase2MacPdu> {
                // Voice kinds have no ACCH body; skip them so deep search does
                // not waste a pass on the primary DUID when it is Voice4/2.
                if (phase2BurstKindHasVoice(k) || k == P25Phase2BurstKind::Unknown) {
                    return std::nullopt;
                }
                if (phase2BurstKindUsesScrambling(k) && !canApplyMask) return std::nullopt;
                const auto& candidatePayload = phase2BurstKindUsesScrambling(k) ? descrambledPayloadDibits : rawPayloadDibits;
                auto candidate = decodePhase2Acch(candidatePayload, k, payload, deep, alternateAcchHypotheses);
                if (!candidate) return std::nullopt;
                candidate->detectedKind = burst.kind;
                if (candidate->crcValid) return candidate;
                if (!bestNonCrc || candidate->correctedSymbols < bestNonCrc->correctedSymbols) {
                    bestNonCrc = candidate;
                }
                return std::nullopt;
            };
            for (P25Phase2BurstKind k : candidates) {
                // Match SDRTrunk/OP25 on the realtime hot path: the decoded
                // DUID selects the timeslot type. Alternate-kind ACCH fanout is
                // a bounded rescue/forensic operation only; doing it for every
                // rolling GUI window can burn hundreds of ms and starve audio.
                if (!allowAlternateKindFanout && k != burst.kind) break;
                const int key = static_cast<int>(k);
                bool duplicate = false;
                for (size_t i = 0; i < seenCount; ++i) duplicate = duplicate || seen[i] == key;
                if (duplicate) continue;
                if (seenCount < seen.size()) seen[seenCount++] = key;
                if (auto crcHit = tryKind(k, false)) return crcHit;
            }
            if (deepAcchSearch) {
                seenCount = 0;
                for (P25Phase2BurstKind k : candidates) {
                    const int key = static_cast<int>(k);
                    bool duplicate = false;
                    for (size_t i = 0; i < seenCount; ++i) duplicate = duplicate || seen[i] == key;
                    if (duplicate) continue;
                    if (seenCount < seen.size()) seen[seenCount++] = key;
                    if (auto crcHit = tryKind(k, true)) return crcHit;
                }
            }
            return bestNonCrc;
        };

        if (auto pdu = selectMacPdu()) {
            burst.macFecDecoded = pdu->fecDecoded;
            burst.macCrcValid = pdu->crcValid;
            burst.macCrcLock = burst.macCrcValid || (burst.macFecDecoded && pdu->correctedSymbols < 10);
            if (burst.macCrcValid || (burst.macFecDecoded && pdu->correctedSymbols < 10)) burst.phase2AudioLock = true;
            if (session && (pdu->crcValid || (pdu->fecDecoded && pdu->correctedSymbols < 10))) {
                // Match sdrtrunk's Phase 2 message processor session semantics:
                // MAC_PTT starts a current-call ESS context; END_PTT, IDLE, and
                // HANGTIME terminate/clear it.  Keeping old ESS across MAC_IDLE or
                // MAC_HANGTIME leaves stale clear/encrypted state attached to the
                // next talkspurt and was one source of late, random Phase-2 audio.
                // Use FEC-decoded MAC (low corrected symbols) for state on traffic to align SDRTrunk (MAC provides the clear/enc proof even if not full CRC).
                const uint8_t macType = static_cast<uint8_t>(pdu->opcode & 0x07u);
                const auto trafficSecurity = phase2TrafficMacSecurityFromCrcPdu(*pdu);
                session->maskPhaseMacCrcSeen = true;
                switch (macType) {
                    case 1: // MAC_PTT
                        burst.macPttSeen = true;
                        session->macCrcSeen = true;
                        session->pttSeen = true;
                        session->activeSeen = false;
                        session->endPttSeen = false;
                        session->idleSeen = false;
                        session->hangtimeSeen = false;
                        session->securityStateFromPtt = true;
                        session->trafficSecurityKnown = false;
                        session->trafficEncrypted = false;
                        session->trafficTalkgroupKnown = false;
                        session->trafficTalkgroupId = 0;
                        session->ess = pdu->ess;
                        session->essTrusted = pdu->ess.known && pdu->ess.fecValidated;
                        session->first4vSlot = static_cast<int>(
                            (phase2LogicalVoiceSequenceIndexForSuperframeBurst(
                                 dibits, superframeOffset, trafficSuperframeBurstIndex) +
                             pdu->offset + 1) % 5);
                        phase2ResetEssFragments(*session);
                        break;
                    case 2: // MAC_END_PTT
                        burst.macEndPttSeen = true;
                        phase2ClearCallSession(*session);
                        session->endPttSeen = true;
                        break;
                    case 3: // MAC_IDLE
                        burst.macIdleSeen = true;
                        phase2ClearCallSession(*session);
                        session->idleSeen = true;
                        break;
                    case 6: // MAC_HANGTIME
                        burst.macHangtimeSeen = true;
                        phase2ClearCallSession(*session);
                        session->hangtimeSeen = true;
                        if (trafficSecurity.known) {
                            session->trafficSecurityKnown = true;
                            session->trafficEncrypted = trafficSecurity.encrypted;
                            session->trafficTalkgroupKnown = trafficSecurity.talkgroupKnown;
                            session->trafficTalkgroupId = trafficSecurity.talkgroupId;
                        }
                        break;
                    case 4: // MAC_ACTIVE
                        burst.macActiveSeen = true;
                        session->activeSeen = true;
                        session->first4vSlot = pdu->offset > 4 ? 0 : pdu->offset;
                        if (trafficSecurity.known) {
                            session->trafficSecurityKnown = true;
                            session->trafficEncrypted = trafficSecurity.encrypted;
                            session->trafficTalkgroupKnown = trafficSecurity.talkgroupKnown;
                            session->trafficTalkgroupId = trafficSecurity.talkgroupId;
                        }
                        break;
                    default:
                        break;
                }
            }
            if (macPdus) macPdus->push_back(std::move(*pdu));
        }
    }

    // Extract AMBE for hard Voice2/Voice4 only (sdrtrunk Voice*Timeslot).
    // Unknown DUID maps to UnknownTimeslot in sdrtrunk and must not manufacture
    // AMBE; mbelib can synthesize plausible garbage from signaling bits.
    const bool hardVoice = phase2BurstKindHasVoice(burst.kind);
    if (hardVoice) {
        const auto& voicePayloadDibits = canApplyMask ? descrambledPayloadDibits : rawPayloadDibits;
        if (session && burst.xorMaskApplied && superframeLocked) {
            const int logicalVoiceIndex = phase2LogicalVoiceSequenceIndexForSuperframeBurst(
                dibits, superframeOffset, trafficSuperframeBurstIndex);
            std::optional<P25Phase2EssState> ess;
            if (session->first4vSlot >= 0) {
                ess = decodePhase2VoiceEssWithCallEpoch(voicePayloadDibits, burst.kind, logicalVoiceIndex, *session);
            } else {
                ess = decodePhase2VoiceEssWithCallEpoch(voicePayloadDibits, burst.kind, logicalVoiceIndex, *session);
                if (!ess) {
                    ess = decodePhase2VoiceEssSdrtrunkOrder(voicePayloadDibits, burst.kind, *session);
                }
            }
            if (ess) {
                phase2AcceptVoiceEss(*session, *ess);
            }
        }

        // sdrtrunk Voice4Timeslot/Voice2Timeslot uses bit offsets
        // 2/76/172/246 inside the 320-bit timeslot.  In dibits, relative to
        // the 160-dibit descrambled payload, those are 1/38/86/123.  OP25's
        // p25p2_tdma.cc reports 11/48/96/133 from its xored_burst pointer,
        // but that pointer is already shifted 10 dibits from the raw 180-dibit
        // burst; the same absolute positions are pos+21/58/106/143.
        const std::array<size_t, 4> starts{1, 38, 86, 123};
        const size_t count = burst.kind == P25Phase2BurstKind::Voice2 ? 2u : 4u;
        for (size_t i = 0; i < count; ++i) {
            const size_t start = starts[i];
            // Use mapped original stream dibit index so abs positions for dedupe / validation match the input dibit stream.
            const size_t absPos = (start < payloadSrcIndex.size() ? payloadSrcIndex[start] : (payload + start));
            if (start + 36 > voicePayloadDibits.size()) continue;
            P25Phase2VoiceCodeword cw;
            cw.dibitOffset = absPos;
            cw.voiceIndex = static_cast<uint8_t>(i);
            for (size_t d = 0; d < 36; ++d) {
                const int dval = voicePayloadDibits[start + d] & 0x03;
                const auto bits = P25LiveDecoder::bitsFromDibit(dval);
                // MSB-first within each dibit matches SDRTrunk submessage bit order.
                cw.bits[d * 2] = bits[0];
                cw.bits[d * 2 + 1] = bits[1];
            }
            // No reverse: the bit packing cw[0..] follows increasing bit position in the timeslot (matching SDRTrunk submessage order for the 72-bit voice frame at the dibit starts). Brute variants cover any residual order/polarity/phase issues for edge cases.
            burst.voiceCodewords.push_back(cw);
        }
    }

    if (session) {
        burst.essKnown = session->ess.known && session->essTrusted;
        burst.trafficSecurityKnown = session->trafficSecurityKnown;
        burst.trafficEncrypted = session->trafficEncrypted;
        burst.trafficTalkgroupKnown = session->trafficTalkgroupKnown;
        burst.trafficTalkgroupId = session->trafficTalkgroupId;
        burst.encrypted =
            (burst.essKnown && session->ess.encrypted) ||
            (session->trafficSecurityKnown && session->trafficEncrypted);
        burst.macCrcLock = session->pttSeen || session->activeSeen || burst.macCrcValid;
        const bool trafficClearRelease =
            session->trafficSecurityKnown &&
            !session->trafficEncrypted &&
            session->activeSeen;
        burst.phase2AudioLock = session->pttSeen || session->activeSeen ||
            trafficClearRelease ||
            (burst.essKnown && session->ess.fecValidated);
        burst.securityStateFromPtt = session->securityStateFromPtt;
    }
    const bool trafficClearRelease =
        session &&
        session->trafficSecurityKnown &&
        !session->trafficEncrypted &&
        session->activeSeen;
    burst.sessionAudioRelease =
        burst.xorMaskApplied &&
        !burst.encrypted &&
        (session &&
         (burst.essKnown &&
          (session->pttSeen || (session->essTrusted && session->ess.fecValidated))));

    return burst;
}

struct Phase2MaskPhaseWindow {
    uint8_t phase = 0;
    int score = 0;
    size_t macCrcValid = 0;
    size_t macFecDecoded = 0;
    int ambeSamples = 0;
    int ambeLowError = 0;
    int ambeErrorSum = 0;
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
                                                 bool stickyPhase,
                                                 size_t maxSlots = 12,
                                                 bool deepAcchRescue = false,
                                                 size_t deepAcchBudget = 0)
{
    P25DecoderTraceScope trace("scorePhase2MaskPhaseWindow");
    Phase2MaskPhaseWindow window;
    window.phase = static_cast<uint8_t>(phase % 12u);
    window.sessions = seedSessions;
    window.bursts.reserve(12);
    const size_t slotLimit = std::min<size_t>(12, std::max<size_t>(1, maxSlots));

    for (size_t slot = 0; slot < slotLimit; ++slot) {
        const size_t expectedPos = lock.dibitOffset + slot * P25LiveDecoder::Phase2BurstDibits;
        if (expectedPos + P25LiveDecoder::Phase2BurstDibits > dibits.size()) break;
        const auto hit = phase2SyncHitNear(hits, expectedPos, 2);
        const size_t pos = hit ? hit->dibitOffset : expectedPos;
        if (pos + P25LiveDecoder::Phase2BurstDibits > dibits.size()) continue;
        const uint8_t trafficSlot =
            phase2TrafficSlotForSuperframeBurst(dibits, lock.dibitOffset, slot);
        auto& slotSession = window.sessions[trafficSlot & 0x01u];
        // Mask-phase scoring normally stays shallow.  Realtime late-entry can
        // optionally enable a bounded standards-only ACCH rescue pass when soft
        // AMBE evidence exists but no MAC/ESS phase has been proven yet.
        auto burst = decodePhase2BurstAt(dibits, pos, hit ? hit->errors : -1,
                                         true, lock.dibitOffset, slot,
                                         lock.syncScore, lock.syncErrors,
                                         mask, window.phase, 0,
                                         &slotSession, &window.macPdus,
                                         false);
        if (burst.valid &&
            !burst.macCrcValid &&
            deepAcchRescue &&
            deepAcchBudget > 0 &&
            phase2BurstKindCarriesAcch(burst.kind) &&
            burst.voiceCodewords.empty()) {
            --deepAcchBudget;
            Phase2SessionState rescueSession = slotSession;
            std::vector<P25Phase2MacPdu> rescueMacPdus;
            auto rescueBurst = decodePhase2BurstAt(dibits, pos, hit ? hit->errors : -1,
                                                   true, lock.dibitOffset, slot,
                                                   lock.syncScore, lock.syncErrors,
                                                   mask, window.phase, 0,
                                                   &rescueSession, &rescueMacPdus,
                                                   true,
                                                   false);
            const bool nominalRescueCrc = std::any_of(
                rescueMacPdus.begin(),
                rescueMacPdus.end(),
                phase2MacPduIsNominalCrc);
            if (rescueBurst.valid && nominalRescueCrc &&
                (rescueBurst.macCrcValid ||
                 rescueBurst.sessionAudioRelease ||
                 rescueBurst.essKnown ||
                 rescueBurst.trafficSecurityKnown)) {
                slotSession = rescueSession;
                window.macPdus.insert(window.macPdus.end(),
                                      std::make_move_iterator(rescueMacPdus.begin()),
                                      std::make_move_iterator(rescueMacPdus.end()));
                burst = std::move(rescueBurst);
            }
        }
        if (!burst.valid) continue;
        if (hit && hit->dibitOffset != expectedPos) {
            burst.syncOffsetAdjusted = true;
            burst.syncOffsetDibits = phase2SignedSyncSlipDibits(hit->dibitOffset, expectedPos);
        }
        window.bursts.push_back(std::move(burst));
        // Once a phase produces CRC-valid MAC, remaining slots only inflate
        // scoring cost; commit re-decodes the chosen phase fully.
        if (window.macPdus.size() > 0 &&
            std::any_of(window.macPdus.begin(), window.macPdus.end(),
                        [](const P25Phase2MacPdu& pdu) { return pdu.crcValid; })) {
            break;
        }
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
    // Do NOT add a large sticky VCW bonus: every mask phase extracts the same
    // Voice2/4 DUID count (bits just differ), so +300 VCW made wrong phases stick
    // forever (field: p2sf/p2mask high, p2mac=0, continuous scrambled audio).
    window.score = lock.syncScore * 20 - lock.syncErrors * 2;
    if (stickyPhase) window.score += 15;
    window.score += static_cast<int>(window.macFecDecoded) * 8;
    window.score += static_cast<int>(window.macCrcValid) * 500;
    if (window.essKnown) window.score += 900;

    for (const auto& burst : window.bursts) {
        if (burst.duidErrors == 0) window.score += 2;
        else if (burst.duidErrors > 1) window.score -= 8;
        // Prefer exact DUID matches for hard voice — soft Hamming=2 Voice is weaker.
        if (phase2BurstKindHasVoice(burst.kind) && burst.duidErrors == 0) window.score += 6;
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
                    ? (lowErrorIsch ? 220 : 80)
                    : (lowErrorIsch ? -200 : -60);
            }
        }
        if (burst.macCrcValid) window.score += 120;
        if (burst.essKnown) window.score += 80;
        // Hard voice only, tiny tie-break — never enough to beat wrong I-ISCH.
        if (phase2BurstKindHasVoice(burst.kind)) {
            window.score += static_cast<int>(std::min<size_t>(burst.voiceCodewords.size(), 4));
        }
    }

    // When MAC/ESS are silent, prefer phases whose hard Voice AMBE frames have
    // low Golay errors.  Cap at 2 throwaway decodes so cold 12-phase hunt stays
    // within the live DSP budget (full 4×12 AMBE scoring caused ~1s workers).
    if (window.macCrcValid == 0 && !window.essKnown && !stickyPhase) {
        P25AmbeVoiceDecoder throwaway;
        for (const auto& burst : window.bursts) {
            if (!phase2BurstKindHasVoice(burst.kind) || burst.voiceCodewords.empty()) continue;
            const auto& cw = burst.voiceCodewords.front();
            const auto frame = p25Phase2VoiceCodewordToAmbe3600x2450Frame(cw);
            const auto decoded = throwaway.decodeAmbe3600x2450Frame(frame);
            if (decoded.status != P25VoiceDecodeStatus::Decoded) continue;
            ++window.ambeSamples;
            window.ambeErrorSum += decoded.totalErrors;
            if (decoded.totalErrors <= 2) ++window.ambeLowError;
            if (window.ambeSamples >= 2) break;
        }
        if (window.ambeSamples > 0) {
            const int meanErr = window.ambeErrorSum / window.ambeSamples;
            window.score += window.ambeLowError * 160;
            window.score += std::max(0, 6 - meanErr) * 40;
            window.score -= meanErr * 35;
        }
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

int scoreBootstrappedPhase2MaskedBurst(const P25Phase2Burst& burst)
{
    // Only hard Voice2/4 or CRC/ESS evidence scores. VCW count alone previously
    // locked a random mask phase because every phase extracts the same DUID count.
    if (!burst.valid || !burst.xorMaskApplied) return 0;
    const bool hardVoice = phase2BurstKindHasVoice(burst.kind);
    if (!hardVoice && !burst.macCrcValid && !burst.essKnown) return 0;
    int score = 0;
    if (hardVoice) {
        score += static_cast<int>(burst.voiceCodewords.size()) * 4;
        if (burst.duidErrors == 0) score += 20;
        else if (burst.duidErrors == 1) score += 8;
        else score -= 10;
    }
    if (burst.macCrcValid) score += 800;
    if (burst.essKnown) score += 200;
    if (burst.isch.valid && !burst.isch.sync && burst.isch.location <= 2) {
        if (burst.isch.errors >= 0 && burst.isch.errors < 3) score += 100;
        if (burst.superframeBurstIndexKnown) {
            const size_t burstIndex = static_cast<size_t>(burst.superframeBurstIndex % 12u);
            const size_t expectedMaskIndex = static_cast<size_t>(burst.isch.location) * 4u + (burstIndex % 4u);
            const size_t actualMaskIndex = (burstIndex + static_cast<size_t>(burst.xorMaskPhase)) % 12u;
            if (actualMaskIndex == expectedMaskIndex) score += 180;
            else score -= 80;
        }
    }
    if (burst.syncErrors >= 0 && burst.syncErrors <= 2) score += 20;
    return score;
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

    static constexpr std::array<uint16_t, 96> kCcitt80Checksums = {
        0x1bcb, 0x8de5, 0xc6f2, 0x6b69, 0xb5b4, 0x52ca, 0x2175, 0x90ba,
        0x404d, 0xa026, 0x5803, 0xac01, 0xd600, 0x6310, 0x3998, 0x14dc,
        0x027e, 0x092f, 0x8497, 0xc24b, 0xe125, 0xf092, 0x7059, 0xb82c,
        0x5406, 0x2213, 0x9109, 0xc884, 0x6c52, 0x3e39, 0x9f1c, 0x479e,
        0x2bdf, 0x95ef, 0xcaf7, 0xe57b, 0xf2bd, 0xf95e, 0x74bf, 0xba5f,
        0xdd2f, 0xee97, 0xf74b, 0xfba5, 0xfdd2, 0x76f9, 0xbb7c, 0x55ae,
        0x22c7, 0x9163, 0xc8b1, 0xe458, 0x7a3c, 0x350e, 0x1297, 0x894b,
        0xc4a5, 0xe252, 0x7939, 0xbc9c, 0x565e, 0x233f, 0x919f, 0xc8cf,
        0xe467, 0xf233, 0xf919, 0xfc8c, 0x7656, 0x333b, 0x999d, 0xccce,
        0x6e77, 0xb73b, 0xdb9d, 0xedce, 0x7ef7, 0xbf7b, 0xdfbd, 0xefde,
        0x0001, 0x0002, 0x0004, 0x0008, 0x0010, 0x0020, 0x0040, 0x0080,
        0x0100, 0x0200, 0x0400, 0x0800, 0x1000, 0x2000, 0x4000, 0x8000,
    };

    uint16_t calculated = 0xffffu;
    for (size_t bit = 0; bit < 80; ++bit) {
        const size_t byteIndex = bit / 8;
        const int bitInByte = 7 - static_cast<int>(bit % 8);
        if (((bytes[byteIndex] >> bitInByte) & 0x01u) != 0) {
            calculated ^= kCcitt80Checksums[bit];
        }
    }
    const uint16_t transmitted = static_cast<uint16_t>((static_cast<uint16_t>(bytes[10]) << 8) | bytes[11]);
    const uint16_t residual = static_cast<uint16_t>(calculated ^ transmitted);
    return residual == 0u || residual == 0xffffu;
}

struct P25Ccitt80Correction {
    bool valid = false;
    bool corrected = false;
    bool invertedResidual = false;
    int correctedBits = 0;
};

P25Ccitt80Correction p25CorrectCcitt80(std::vector<uint8_t>& bytes)
{
    P25Ccitt80Correction out;
    if (bytes.size() != 12) return out;

    static constexpr std::array<uint16_t, 96> kCcitt80Checksums = {
        0x1bcb, 0x8de5, 0xc6f2, 0x6b69, 0xb5b4, 0x52ca, 0x2175, 0x90ba,
        0x404d, 0xa026, 0x5803, 0xac01, 0xd600, 0x6310, 0x3998, 0x14dc,
        0x027e, 0x092f, 0x8497, 0xc24b, 0xe125, 0xf092, 0x7059, 0xb82c,
        0x5406, 0x2213, 0x9109, 0xc884, 0x6c52, 0x3e39, 0x9f1c, 0x479e,
        0x2bdf, 0x95ef, 0xcaf7, 0xe57b, 0xf2bd, 0xf95e, 0x74bf, 0xba5f,
        0xdd2f, 0xee97, 0xf74b, 0xfba5, 0xfdd2, 0x76f9, 0xbb7c, 0x55ae,
        0x22c7, 0x9163, 0xc8b1, 0xe458, 0x7a3c, 0x350e, 0x1297, 0x894b,
        0xc4a5, 0xe252, 0x7939, 0xbc9c, 0x565e, 0x233f, 0x919f, 0xc8cf,
        0xe467, 0xf233, 0xf919, 0xfc8c, 0x7656, 0x333b, 0x999d, 0xccce,
        0x6e77, 0xb73b, 0xdb9d, 0xedce, 0x7ef7, 0xbf7b, 0xdfbd, 0xefde,
        0x0001, 0x0002, 0x0004, 0x0008, 0x0010, 0x0020, 0x0040, 0x0080,
        0x0100, 0x0200, 0x0400, 0x0800, 0x1000, 0x2000, 0x4000, 0x8000,
    };

    auto bitIsSet = [&bytes](size_t bit) -> bool {
        const size_t byteIndex = bit / 8;
        const int bitInByte = 7 - static_cast<int>(bit % 8);
        return ((bytes[byteIndex] >> bitInByte) & 0x01u) != 0;
    };
    auto flipBit = [&bytes](size_t bit) {
        const size_t byteIndex = bit / 8;
        const int bitInByte = 7 - static_cast<int>(bit % 8);
        bytes[byteIndex] ^= static_cast<uint8_t>(1u << bitInByte);
    };
    auto residualFor = [&]() -> uint16_t {
        uint16_t calculated = 0xffffu;
        for (size_t bit = 0; bit < 80; ++bit) {
            if (bitIsSet(bit)) calculated ^= kCcitt80Checksums[bit];
        }
        const uint16_t transmitted = static_cast<uint16_t>((static_cast<uint16_t>(bytes[10]) << 8) | bytes[11]);
        return static_cast<uint16_t>(calculated ^ transmitted);
    };

    uint16_t residual = residualFor();
    if (residual == 0u || residual == 0xffffu) {
        out.valid = true;
        out.invertedResidual = residual == 0xffffu;
        return out;
    }

    auto it = std::find(kCcitt80Checksums.begin(), kCcitt80Checksums.end(), residual);
    if (it != kCcitt80Checksums.end()) {
        const size_t bit = static_cast<size_t>(std::distance(kCcitt80Checksums.begin(), it));
        flipBit(bit);
        residual = residualFor();
        if (residual == 0u || residual == 0xffffu) {
            out.valid = true;
            out.corrected = true;
            out.correctedBits = 1;
            out.invertedResidual = residual == 0xffffu;
            return out;
        }
        flipBit(bit);
    }

    out.correctedBits = 2;
    return out;
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

std::vector<int> deinterleaveP25DataCodedDibits(const std::vector<int>& coded)
{
    if (coded.size() != 98) return {};

    std::array<uint8_t, 196> interleavedBits{};
    for (size_t i = 0; i < coded.size(); ++i) {
        const auto pair = P25LiveDecoder::bitsFromDibit(coded[i] & 0x03);
        interleavedBits[i * 2] = pair[0];
        interleavedBits[i * 2 + 1] = pair[1];
    }

    std::array<uint8_t, 196> deinterleavedBits{};
    for (size_t i = 0; i < interleavedBits.size(); ++i) {
        const size_t nibble = i / 4;
        const size_t bit = i % 4;
        size_t row = 0;
        size_t column = 0;
        if (nibble < 13) {
            row = nibble;
            column = 0;
        } else if (nibble < 25) {
            row = nibble - 13;
            column = 1;
        } else if (nibble < 37) {
            row = nibble - 25;
            column = 2;
        } else {
            row = nibble - 37;
            column = 3;
        }
        const size_t out = ((row * 4) + column) * 4 + bit;
        if (out < deinterleavedBits.size()) deinterleavedBits[out] = interleavedBits[i];
    }

    std::vector<int> out;
    out.reserve(98);
    for (size_t i = 0; i + 1 < deinterleavedBits.size(); i += 2) {
        out.push_back(P25LiveDecoder::dibitFromBits(deinterleavedBits[i], deinterleavedBits[i + 1]));
    }
    return out;
}

uint64_t readBitsFromBytesMsb(const std::vector<uint8_t>& bytes, int startBit, int count)
{
    if (count <= 0 || count > 64 || startBit < 0) return 0;
    uint64_t out = 0;
    for (int i = 0; i < count; ++i) {
        const int bit = startBit + i;
        const int byteIndex = bit / 8;
        if (byteIndex < 0 || byteIndex >= static_cast<int>(bytes.size())) return 0;
        const int bitInByte = 7 - (bit % 8);
        out = (out << 1) | ((bytes[static_cast<size_t>(byteIndex)] >> bitInByte) & 0x01u);
    }
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
    auto crc = p25CorrectCcitt80(block.bytes);
    block.crcValid = crc.valid;
    block.crcCorrected = crc.corrected;
    block.crcCorrectedBits = crc.correctedBits;
    block.crcPassedInvertedResidual = crc.invertedResidual;
    return block;
}

P25Phase1PduDataBlock decodeCodedP25DataBlock(const std::vector<int>& onAirCodedDibits,
                                              size_t bitOffset)
{
    P25Phase1PduDataBlock block;
    block.bitOffset = bitOffset;

    auto deinterleaved = deinterleaveP25DataCodedDibits(onAirCodedDibits);
    auto decoded = decodeP25HalfRateTrellis(deinterleaved);
    if (!decoded.ok || decoded.dibits.size() != 48) return block;

    block.bytes = dibitsToBytes(decoded.dibits);
    block.fecDecoded = block.bytes.size() == 12;
    block.correctedDibitErrors = decoded.correctedDibitErrors;
    return block;
}

P25Phase1PduMessage decodeP25PduSequence(const StatusSplit& split,
                                         size_t payloadDibitOffset,
                                         size_t maxBlocks)
{
    P25Phase1PduMessage pdu;
    size_t blockStartDibit = 32;
    if (blockStartDibit + 98 > split.dataDibits.size()) return pdu;

    std::vector<int> codedHeader(split.dataDibits.begin() + static_cast<std::ptrdiff_t>(blockStartDibit),
                                 split.dataDibits.begin() + static_cast<std::ptrdiff_t>(blockStartDibit + 98));
    auto header = decodeCodedP25DataBlock(codedHeader, (payloadDibitOffset + blockStartDibit) * 2);
    pdu.bitOffset = header.bitOffset;
    pdu.headerBytes = std::move(header.bytes);
    pdu.headerFecDecoded = header.fecDecoded;
    pdu.headerCorrectedDibitErrors = header.correctedDibitErrors;
    auto crc = p25CorrectCcitt80(pdu.headerBytes);
    pdu.headerCrcValid = crc.valid;
    pdu.headerCrcCorrected = crc.corrected;
    pdu.headerCrcCorrectedBits = crc.correctedBits;
    pdu.headerCrcPassedInvertedResidual = crc.invertedResidual;
    if (!pdu.headerFecDecoded) return pdu;

    pdu.outbound = readBitsFromBytesMsb(pdu.headerBytes, 2, 1) != 0;
    pdu.format = static_cast<uint8_t>(readBitsFromBytesMsb(pdu.headerBytes, 3, 5));
    pdu.vendor = static_cast<uint8_t>(readBitsFromBytesMsb(pdu.headerBytes, 16, 8));
    pdu.logicalLinkId = static_cast<uint32_t>(readBitsFromBytesMsb(pdu.headerBytes, 24, 24));
    pdu.blocksToFollow = static_cast<uint8_t>(readBitsFromBytesMsb(pdu.headerBytes, 49, 7));
    pdu.opcode = static_cast<uint8_t>(readBitsFromBytesMsb(pdu.headerBytes, 58, 6));

    blockStartDibit += 98;
    const size_t blocksToDecode = std::min<size_t>(pdu.blocksToFollow, maxBlocks);
    for (size_t blockIndex = 0; blockIndex < blocksToDecode; ++blockIndex) {
        if (blockStartDibit + 98 > split.dataDibits.size()) break;
        std::vector<int> coded(split.dataDibits.begin() + static_cast<std::ptrdiff_t>(blockStartDibit),
                               split.dataDibits.begin() + static_cast<std::ptrdiff_t>(blockStartDibit + 98));
        auto data = decodeCodedP25DataBlock(coded, (payloadDibitOffset + blockStartDibit) * 2);
        if (!data.bytes.empty()) pdu.dataBlocks.push_back(std::move(data));
        blockStartDibit += 98;
    }
    return pdu;
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
        case 6:
            // Simulate small mask phase slip (rotate bitstream by 2 bits)
            std::rotate(bits.begin(), bits.begin() + 2, bits.end());
            break;
        case 7:
            // Simulate larger slip
            std::rotate(bits.begin(), bits.begin() + 4, bits.end());
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
    // Live-safe AMBE layout probes.  Variants 0/1/3/4 cover canonical,
    // dibit-pair swap, polarity invert, and swap+invert without whole-frame
    // reversal probes that often false-lock on noise.  Selection always uses
    // throwaway decoders; the live mbelib instance stays stateful.
    return 4;
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
    if (!std::isfinite(m_config.frontEndDcBlockAlpha) || m_config.frontEndDcBlockAlpha <= 0.0) {
        m_config.frontEndDcBlockAlpha = 0.00025;
    }
    m_config.frontEndDcBlockAlpha = std::clamp(m_config.frontEndDcBlockAlpha, 1e-6, 0.02);
    if (!std::isfinite(m_config.cqpskCarrierLoopBandwidth) || m_config.cqpskCarrierLoopBandwidth <= 0.0) {
        // SDRTrunk CostasLoop BW_300 → 2π/300.  Slightly wider 0.040 remains OK for P1 LSM.
        m_config.cqpskCarrierLoopBandwidth = m_config.phase2CqpskTrafficDemod
            ? ((2.0 * 3.14159265358979323846) / 300.0)
            : 0.040;
    }
    m_config.cqpskCarrierLoopBandwidth = std::clamp(m_config.cqpskCarrierLoopBandwidth, 0.002, 0.120);
    if (!std::isfinite(m_config.cqpskCarrierLoopMaxCorrectionHz) ||
        m_config.cqpskCarrierLoopMaxCorrectionHz <= 0.0) {
        m_config.cqpskCarrierLoopMaxCorrectionHz = m_config.phase2CqpskTrafficDemod
            ? std::max(1800.0, m_config.symbolRate * 0.5)
            : 1800.0;
    }
    m_config.maxC4fmFixedPhaseCandidates = std::min<size_t>(m_config.maxC4fmFixedPhaseCandidates, 16);
}


P25BlockTimingState P25LiveDecoder::streamTimingStateSnapshot() const
{
    std::lock_guard<std::mutex> lock(m_streamingStateMutex);
    return m_streamTimingState;
}

void P25LiveDecoder::storeStreamTimingState(const P25BlockTimingState& timing)
{
    std::lock_guard<std::mutex> lock(m_streamingStateMutex);
    m_streamTimingState = timing;
}

std::vector<uint8_t> P25LiveDecoder::buildPhase1BitStreamWithTail(const std::vector<uint8_t>& inputBits,
                                                                  size_t& prefixBits) const
{
    constexpr size_t kPhase1TailBits = P25LiveDecoder::FrameSyncBits + 512;
    std::vector<uint8_t> bits;
    {
        std::lock_guard<std::mutex> lock(m_streamingStateMutex);
        prefixBits = std::min(m_phase1BitTail.size(), kPhase1TailBits);
        bits.reserve(prefixBits + inputBits.size());
        if (prefixBits > 0) {
            bits.insert(bits.end(),
                        m_phase1BitTail.end() - static_cast<std::ptrdiff_t>(prefixBits),
                        m_phase1BitTail.end());
        }
    }
    for (uint8_t bit : inputBits) bits.push_back(bit ? 1u : 0u);
    return bits;
}

void P25LiveDecoder::storePhase1BitTail(const std::vector<uint8_t>& bits)
{
    constexpr size_t kPhase1TailBits = P25LiveDecoder::FrameSyncBits + 512;
    std::lock_guard<std::mutex> lock(m_streamingStateMutex);
    m_phase1BitTail.clear();
    const size_t keep = std::min(bits.size(), kPhase1TailBits);
    if (keep > 0) {
        m_phase1BitTail.insert(m_phase1BitTail.end(),
                               bits.end() - static_cast<std::ptrdiff_t>(keep),
                               bits.end());
    }
}

std::deque<uint8_t> P25LiveDecoder::snapshotPhase1BitTail() const
{
    std::lock_guard<std::mutex> lock(m_streamingStateMutex);
    return m_phase1BitTail;
}

void P25LiveDecoder::restorePhase1BitTail(const std::deque<uint8_t>& snapshot)
{
    std::lock_guard<std::mutex> lock(m_streamingStateMutex);
    m_phase1BitTail = snapshot;
}

P25LiveDecoder::~P25LiveDecoder() = default;

P25LiveDecoder::P25LiveDecoder(const P25LiveDecoder& other)
    : m_config(other.m_config),
      m_phase2MaskParams(other.m_phase2MaskParams),
      m_phase2XorMask(other.m_phase2XorMask),
      m_phase2Ess(other.m_phase2Ess),
      m_phase2EssB(other.m_phase2EssB),
      m_phase2EssBSeen(other.m_phase2EssBSeen),
      m_phase2EssBNext(other.m_phase2EssBNext),
      m_phase2SessionMacCrcSeen(other.m_phase2SessionMacCrcSeen),
      m_phase2First4vSlot(other.m_phase2First4vSlot),
      m_phase2EssHypotheses(other.m_phase2EssHypotheses),
      m_phase2EssBHypotheses(other.m_phase2EssBHypotheses),
      m_phase2EssBSeenHypotheses(other.m_phase2EssBSeenHypotheses),
      m_phase2SlotEss(other.m_phase2SlotEss),
      m_phase2SlotEssB(other.m_phase2SlotEssB),
      m_phase2SlotEssBSeen(other.m_phase2SlotEssBSeen),
      m_phase2SlotEssBNext(other.m_phase2SlotEssBNext),
      m_phase2SlotSessionMacCrcSeen(other.m_phase2SlotSessionMacCrcSeen),
      m_phase2SlotFirst4vSlot(other.m_phase2SlotFirst4vSlot),
      m_phase2SlotEssHypotheses(other.m_phase2SlotEssHypotheses),
      m_phase2SlotEssBHypotheses(other.m_phase2SlotEssBHypotheses),
      m_phase2SlotEssBSeenHypotheses(other.m_phase2SlotEssBSeenHypotheses),
      m_phase2MaskPhaseKnown(other.m_phase2MaskPhaseKnown),
      m_phase2MaskPhase(other.m_phase2MaskPhase),
      m_phase2MaskPhaseScore(other.m_phase2MaskPhaseScore),
      m_phase2MaskPhaseStarveWindows(other.m_phase2MaskPhaseStarveWindows),
      m_phase2LastFullMaskPhaseHuntGeneration(other.m_phase2LastFullMaskPhaseHuntGeneration),
      m_phase2SuperframeAnchorKnown(other.m_phase2SuperframeAnchorKnown),
      m_phase2SuperframeAnchorDibit(other.m_phase2SuperframeAnchorDibit),
      m_phase2SuperframeAnchorGeneration(other.m_phase2SuperframeAnchorGeneration),
      m_phase2SuperframeAnchorMaskParams(other.m_phase2SuperframeAnchorMaskParams),
      m_phase2SuperframeAnchorMaskPhase(other.m_phase2SuperframeAnchorMaskPhase),
      m_phase2RecentCodewords(other.m_phase2RecentCodewords),
      m_phase2RecentAcchDecodeBurstDibits(other.m_phase2RecentAcchDecodeBurstDibits),
      m_phase2DibitTail(other.m_phase2DibitTail),
      m_phase2NextCodewordId(other.m_phase2NextCodewordId),
      m_phase2DecodeGeneration(other.m_phase2DecodeGeneration),
      m_phase2StreamDibits(other.m_phase2StreamDibits),
      m_cqpskLock(other.m_cqpskLock),
      m_frontEndDcEstimateValid(other.m_frontEndDcEstimateValid),
      m_frontEndDcSampleRate(other.m_frontEndDcSampleRate),
      m_frontEndDcEstimate(other.m_frontEndDcEstimate)
{
    std::lock_guard<std::mutex> lock(other.m_streamingStateMutex);
    m_streamTimingState = other.m_streamTimingState;
    m_phase1BitTail = other.m_phase1BitTail;
}

P25LiveDecoder& P25LiveDecoder::operator=(const P25LiveDecoder& other)
{
    if (this == &other) return *this;
    m_config = other.m_config;
    m_phase2MaskParams = other.m_phase2MaskParams;
    m_phase2XorMask = other.m_phase2XorMask;
    m_phase2Ess = other.m_phase2Ess;
    m_phase2EssB = other.m_phase2EssB;
    m_phase2EssBSeen = other.m_phase2EssBSeen;
    m_phase2EssBNext = other.m_phase2EssBNext;
    m_phase2SessionMacCrcSeen = other.m_phase2SessionMacCrcSeen;
    m_phase2First4vSlot = other.m_phase2First4vSlot;
    m_phase2EssHypotheses = other.m_phase2EssHypotheses;
    m_phase2EssBHypotheses = other.m_phase2EssBHypotheses;
    m_phase2EssBSeenHypotheses = other.m_phase2EssBSeenHypotheses;
    m_phase2SlotEss = other.m_phase2SlotEss;
    m_phase2SlotEssB = other.m_phase2SlotEssB;
    m_phase2SlotEssBSeen = other.m_phase2SlotEssBSeen;
    m_phase2SlotEssBNext = other.m_phase2SlotEssBNext;
    m_phase2SlotSessionMacCrcSeen = other.m_phase2SlotSessionMacCrcSeen;
    m_phase2SlotFirst4vSlot = other.m_phase2SlotFirst4vSlot;
    m_phase2SlotEssHypotheses = other.m_phase2SlotEssHypotheses;
    m_phase2SlotEssBHypotheses = other.m_phase2SlotEssBHypotheses;
    m_phase2SlotEssBSeenHypotheses = other.m_phase2SlotEssBSeenHypotheses;
    m_phase2MaskPhaseKnown = other.m_phase2MaskPhaseKnown;
    m_phase2MaskPhase = other.m_phase2MaskPhase;
    m_phase2MaskPhaseScore = other.m_phase2MaskPhaseScore;
    m_phase2MaskPhaseStarveWindows = other.m_phase2MaskPhaseStarveWindows;
    m_phase2LastFullMaskPhaseHuntGeneration = other.m_phase2LastFullMaskPhaseHuntGeneration;
    m_phase2SuperframeAnchorKnown = other.m_phase2SuperframeAnchorKnown;
    m_phase2SuperframeAnchorDibit = other.m_phase2SuperframeAnchorDibit;
    m_phase2SuperframeAnchorGeneration = other.m_phase2SuperframeAnchorGeneration;
    m_phase2SuperframeAnchorMaskParams = other.m_phase2SuperframeAnchorMaskParams;
    m_phase2SuperframeAnchorMaskPhase = other.m_phase2SuperframeAnchorMaskPhase;
    m_phase2RecentCodewords = other.m_phase2RecentCodewords;
    m_phase2RecentAcchDecodeBurstDibits = other.m_phase2RecentAcchDecodeBurstDibits;
    m_phase2DibitTail = other.m_phase2DibitTail;
    m_phase2NextCodewordId = other.m_phase2NextCodewordId;
    m_phase2DecodeGeneration = other.m_phase2DecodeGeneration;
    m_phase2StreamDibits = other.m_phase2StreamDibits;
    m_cqpskLock = other.m_cqpskLock;
    m_frontEndDcEstimateValid = other.m_frontEndDcEstimateValid;
    m_frontEndDcSampleRate = other.m_frontEndDcSampleRate;
    m_frontEndDcEstimate = other.m_frontEndDcEstimate;
    {
        std::scoped_lock lock(m_streamingStateMutex, other.m_streamingStateMutex);
        m_streamTimingState = other.m_streamTimingState;
        m_phase1BitTail = other.m_phase1BitTail;
    }
    return *this;
}

P25LiveDecoder::P25LiveDecoder(P25LiveDecoder&& other) noexcept
    : m_config(other.m_config),
      m_phase2MaskParams(other.m_phase2MaskParams),
      m_phase2XorMask(other.m_phase2XorMask),
      m_phase2Ess(other.m_phase2Ess),
      m_phase2EssB(other.m_phase2EssB),
      m_phase2EssBSeen(other.m_phase2EssBSeen),
      m_phase2EssBNext(other.m_phase2EssBNext),
      m_phase2SessionMacCrcSeen(other.m_phase2SessionMacCrcSeen),
      m_phase2First4vSlot(other.m_phase2First4vSlot),
      m_phase2EssHypotheses(other.m_phase2EssHypotheses),
      m_phase2EssBHypotheses(other.m_phase2EssBHypotheses),
      m_phase2EssBSeenHypotheses(other.m_phase2EssBSeenHypotheses),
      m_phase2SlotEss(other.m_phase2SlotEss),
      m_phase2SlotEssB(other.m_phase2SlotEssB),
      m_phase2SlotEssBSeen(other.m_phase2SlotEssBSeen),
      m_phase2SlotEssBNext(other.m_phase2SlotEssBNext),
      m_phase2SlotSessionMacCrcSeen(other.m_phase2SlotSessionMacCrcSeen),
      m_phase2SlotFirst4vSlot(other.m_phase2SlotFirst4vSlot),
      m_phase2SlotEssHypotheses(other.m_phase2SlotEssHypotheses),
      m_phase2SlotEssBHypotheses(other.m_phase2SlotEssBHypotheses),
      m_phase2SlotEssBSeenHypotheses(other.m_phase2SlotEssBSeenHypotheses),
      m_phase2MaskPhaseKnown(other.m_phase2MaskPhaseKnown),
      m_phase2MaskPhase(other.m_phase2MaskPhase),
      m_phase2MaskPhaseScore(other.m_phase2MaskPhaseScore),
      m_phase2MaskPhaseStarveWindows(other.m_phase2MaskPhaseStarveWindows),
      m_phase2LastFullMaskPhaseHuntGeneration(other.m_phase2LastFullMaskPhaseHuntGeneration),
      m_phase2SuperframeAnchorKnown(other.m_phase2SuperframeAnchorKnown),
      m_phase2SuperframeAnchorDibit(other.m_phase2SuperframeAnchorDibit),
      m_phase2SuperframeAnchorGeneration(other.m_phase2SuperframeAnchorGeneration),
      m_phase2SuperframeAnchorMaskParams(other.m_phase2SuperframeAnchorMaskParams),
      m_phase2SuperframeAnchorMaskPhase(other.m_phase2SuperframeAnchorMaskPhase),
      m_phase2RecentCodewords(std::move(other.m_phase2RecentCodewords)),
      m_phase2RecentAcchDecodeBurstDibits(std::move(other.m_phase2RecentAcchDecodeBurstDibits)),
      m_phase2DibitTail(std::move(other.m_phase2DibitTail)),
      m_phase2NextCodewordId(other.m_phase2NextCodewordId),
      m_phase2DecodeGeneration(other.m_phase2DecodeGeneration),
      m_phase2StreamDibits(other.m_phase2StreamDibits),
      m_cqpskLock(other.m_cqpskLock),
      m_frontEndDcEstimateValid(other.m_frontEndDcEstimateValid),
      m_frontEndDcSampleRate(other.m_frontEndDcSampleRate),
      m_frontEndDcEstimate(other.m_frontEndDcEstimate)
{
    std::lock_guard<std::mutex> lock(other.m_streamingStateMutex);
    m_streamTimingState = std::move(other.m_streamTimingState);
    m_phase1BitTail = std::move(other.m_phase1BitTail);
    other.m_streamTimingState = {};
    other.m_phase1BitTail.clear();
    other.m_phase2MaskPhaseStarveWindows = 0;
    other.m_phase2LastFullMaskPhaseHuntGeneration = 0;
}

P25LiveDecoder& P25LiveDecoder::operator=(P25LiveDecoder&& other) noexcept
{
    if (this == &other) return *this;
    m_config = other.m_config;
    m_phase2MaskParams = other.m_phase2MaskParams;
    m_phase2XorMask = other.m_phase2XorMask;
    m_phase2Ess = other.m_phase2Ess;
    m_phase2EssB = other.m_phase2EssB;
    m_phase2EssBSeen = other.m_phase2EssBSeen;
    m_phase2EssBNext = other.m_phase2EssBNext;
    m_phase2SessionMacCrcSeen = other.m_phase2SessionMacCrcSeen;
    m_phase2First4vSlot = other.m_phase2First4vSlot;
    m_phase2EssHypotheses = other.m_phase2EssHypotheses;
    m_phase2EssBHypotheses = other.m_phase2EssBHypotheses;
    m_phase2EssBSeenHypotheses = other.m_phase2EssBSeenHypotheses;
    m_phase2SlotEss = other.m_phase2SlotEss;
    m_phase2SlotEssB = other.m_phase2SlotEssB;
    m_phase2SlotEssBSeen = other.m_phase2SlotEssBSeen;
    m_phase2SlotEssBNext = other.m_phase2SlotEssBNext;
    m_phase2SlotSessionMacCrcSeen = other.m_phase2SlotSessionMacCrcSeen;
    m_phase2SlotFirst4vSlot = other.m_phase2SlotFirst4vSlot;
    m_phase2SlotEssHypotheses = other.m_phase2SlotEssHypotheses;
    m_phase2SlotEssBHypotheses = other.m_phase2SlotEssBHypotheses;
    m_phase2SlotEssBSeenHypotheses = other.m_phase2SlotEssBSeenHypotheses;
    m_phase2MaskPhaseKnown = other.m_phase2MaskPhaseKnown;
    m_phase2MaskPhase = other.m_phase2MaskPhase;
    m_phase2MaskPhaseScore = other.m_phase2MaskPhaseScore;
    m_phase2MaskPhaseStarveWindows = other.m_phase2MaskPhaseStarveWindows;
    m_phase2LastFullMaskPhaseHuntGeneration = other.m_phase2LastFullMaskPhaseHuntGeneration;
    m_phase2SuperframeAnchorKnown = other.m_phase2SuperframeAnchorKnown;
    m_phase2SuperframeAnchorDibit = other.m_phase2SuperframeAnchorDibit;
    m_phase2SuperframeAnchorGeneration = other.m_phase2SuperframeAnchorGeneration;
    m_phase2SuperframeAnchorMaskParams = other.m_phase2SuperframeAnchorMaskParams;
    m_phase2SuperframeAnchorMaskPhase = other.m_phase2SuperframeAnchorMaskPhase;
    m_phase2RecentCodewords = std::move(other.m_phase2RecentCodewords);
    m_phase2RecentAcchDecodeBurstDibits = std::move(other.m_phase2RecentAcchDecodeBurstDibits);
    m_phase2DibitTail = std::move(other.m_phase2DibitTail);
    m_phase2NextCodewordId = other.m_phase2NextCodewordId;
    m_phase2DecodeGeneration = other.m_phase2DecodeGeneration;
    m_phase2StreamDibits = other.m_phase2StreamDibits;
    m_cqpskLock = other.m_cqpskLock;
    other.m_phase2MaskPhaseStarveWindows = 0;
    other.m_phase2LastFullMaskPhaseHuntGeneration = 0;
    m_frontEndDcEstimateValid = other.m_frontEndDcEstimateValid;
    m_frontEndDcSampleRate = other.m_frontEndDcSampleRate;
    m_frontEndDcEstimate = other.m_frontEndDcEstimate;
    {
        std::scoped_lock lock(m_streamingStateMutex, other.m_streamingStateMutex);
        m_streamTimingState = std::move(other.m_streamTimingState);
        m_phase1BitTail = std::move(other.m_phase1BitTail);
        other.m_streamTimingState = {};
        other.m_phase1BitTail.clear();
    }
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
    m_phase2EssBNext = 0;
    m_phase2SessionMacCrcSeen = false;
    m_phase2First4vSlot = -1;
    m_phase2EssHypotheses = {};
    m_phase2EssBHypotheses = {};
    m_phase2EssBSeenHypotheses = {};
    m_phase2SlotEss = {};
    m_phase2SlotEssB = {};
    m_phase2SlotEssBSeen = {};
    m_phase2SlotEssBNext = {};
    m_phase2SlotSessionMacCrcSeen = {};
    m_phase2SlotFirst4vSlot = {-1, -1};
    m_phase2SlotEssHypotheses = {};
    m_phase2SlotEssBHypotheses = {};
    m_phase2SlotEssBSeenHypotheses = {};
    m_phase2MaskPhaseKnown = false;
    m_phase2MaskPhase = 0;
    m_phase2MaskPhaseScore = 0;
    m_phase2MaskPhaseStarveWindows = 0;
    m_phase2LastFullMaskPhaseHuntGeneration = 0;
    m_phase2SuperframeAnchorKnown = false;
    m_phase2SuperframeAnchorDibit = 0;
    m_phase2SuperframeAnchorGeneration = 0;
    m_phase2SuperframeAnchorMaskParams = {};
    m_phase2SuperframeAnchorMaskPhase = 0;
    m_phase2RecentCodewords.clear();
    m_phase2RecentAcchDecodeBurstDibits.clear();
    m_phase2DibitTail.clear();
    m_phase2NextCodewordId = 1;
    m_phase2DecodeGeneration = 0;
    m_phase2StreamDibits = 0;
    m_cqpskLock = {};
    m_frontEndDcEstimateValid = false;
    m_frontEndDcSampleRate = 0.0;
    m_frontEndDcEstimate = {};
    {
        std::lock_guard<std::mutex> lock(m_streamingStateMutex);
        m_streamTimingState = {};
        m_phase1BitTail.clear();
    }
}

P25LiveDecoder P25LiveDecoder::createIndependentProbeCopy(bool retainPhase2MaskParameters) const
{
    P25LiveDecoder copy;
    copy.m_config = m_config;
    if (retainPhase2MaskParameters && m_phase2MaskParams.valid) {
        copy.m_phase2MaskParams = m_phase2MaskParams;
        copy.m_phase2XorMask = m_phase2XorMask;
    }
    return copy;
}

void P25LiveDecoder::alignPhase2AbsoluteDibitCursor(uint64_t chunkStartAbsolute, size_t chunkDibitCount)
{
    if (chunkDibitCount == 0) return;
    const uint64_t chunkEnd = chunkStartAbsolute + static_cast<uint64_t>(chunkDibitCount);

    auto clearTrafficContinuity = [&]() {
        m_phase2DibitTail.clear();
        m_phase2RecentAcchDecodeBurstDibits.clear();
        m_phase2SuperframeAnchorKnown = false;
    };

    // This method is called before processHardDibits().  m_phase2StreamDibits
    // must therefore be aligned to the start of the chunk; annotateSessionCodewords()
    // advances it to the end after the chunk is actually consumed.
    if (chunkStartAbsolute > m_phase2StreamDibits) {
        // Gap in the traffic feed: drop sticky tail so the next lock re-acquires cleanly.
        clearTrafficContinuity();
        m_phase2StreamDibits = chunkStartAbsolute;
    } else if (chunkEnd < m_phase2StreamDibits &&
               m_phase2StreamDibits - chunkEnd > Phase2BurstDibits) {
        // Cursor moved backward (ring reset / retune): force stream discontinuity.
        clearTrafficContinuity();
        m_phase2RecentCodewords.clear();
        m_phase2DecodeGeneration++;
        m_phase2StreamDibits = chunkStartAbsolute;
    }
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

bool P25LiveDecoder::phase2MaskParametersKnown() const
{
    return m_phase2MaskParams.valid;
}

bool P25LiveDecoder::phase2MaskParametersMatch(uint16_t nac, uint32_t wacn, uint16_t systemId) const
{
    return m_phase2MaskParams.valid &&
        m_phase2MaskParams.nac == static_cast<uint16_t>(nac & 0x0fffu) &&
        m_phase2MaskParams.wacn == (wacn & 0x000fffffu) &&
        m_phase2MaskParams.systemId == static_cast<uint16_t>(systemId & 0x0fffu);
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
    result.stats.phase2MaskParametersKnown = selected.phase2MaskParametersKnown;
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
    result.stats.frontEndDcBlockApplied = selected.frontEndDcBlockApplied;
    result.stats.frontEndDcEstimateMagnitude = selected.frontEndDcEstimateMagnitude;
    result.stats.cqpskCarrierLoopApplied = selected.cqpskCarrierLoopApplied;
    result.stats.cqpskCarrierLoopCorrectionHz = selected.cqpskCarrierLoopCorrectionHz;
    result.stats.cqpskCarrierLoopPhaseErrorRmsRad = selected.cqpskCarrierLoopPhaseErrorRmsRad;
    result.stats.cqpskCarrierLoopSymbols = selected.cqpskCarrierLoopSymbols;
}

P25LiveDecodeResult P25LiveDecoder::processIq(const std::vector<std::complex<float>>& iq,
                                              double sampleRate,
                                              double centerFreqHz,
                                              double targetFreqHz)
{
    P25DecoderTraceScope trace("P25LiveDecoder::processIq");
    const auto realtimeStarted = std::chrono::steady_clock::now();
    const bool realtimeBudgetActive =
        m_config.realtimeVoiceSearch && m_config.realtimeDecodeBudgetMs > 0;
    auto realtimeBudgetExceeded = [&]() {
        if (!realtimeBudgetActive) return false;
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - realtimeStarted).count();
        return elapsed >= m_config.realtimeDecodeBudgetMs;
    };
    auto noteRealtimeBudget = [&](P25LiveDecodeResult& result) {
        result.warnings.push_back("Realtime P25 voice decode budget exhausted; using best bounded result for this window.");
    };
    auto elapsedMsSince = [&](std::chrono::steady_clock::time_point t0) {
        return static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count());
    };
    P25LiveDecodeResult best;
    const double inputTargetOffsetHz = targetFreqHz - centerFreqHz;
    FrontEndDcBlockResult dcBlock;
    std::vector<std::complex<float>> dcBlockedIq;
    const std::vector<std::complex<float>>* channelInput = &iq;
    if (m_config.enableFrontEndDcBlock && !iq.empty()) {
        dcBlockedIq = iq;
        dcBlock = applyPersistentFrontEndDcBlock(dcBlockedIq,
                                                 sampleRate,
                                                 m_config.frontEndDcBlockAlpha,
                                                 m_frontEndDcEstimateValid,
                                                 m_frontEndDcSampleRate,
                                                 m_frontEndDcEstimate);
        channelInput = &dcBlockedIq;
    }
    const auto channelizeStarted = std::chrono::steady_clock::now();
    auto channel = channelizeP25Iq(*channelInput, sampleRate, centerFreqHz, targetFreqHz, m_config);
    const long long channelizeMs = elapsedMsSince(channelizeStarted);
    if (channel.samples.empty() && !iq.empty()) {
        best.stats.inputSamples = iq.size();
        best.stats.inputTargetOffsetHz = inputTargetOffsetHz;
        best.stats.frontEndDcBlockApplied = dcBlock.applied;
        best.stats.frontEndDcEstimateMagnitude = dcBlock.estimateMagnitude;
        best.warnings.push_back("P25 target is outside the sampled RF passband or IQ block is too short.");
        return best;
    }
    auto stampCommonStats = [&](P25LiveDecodeResult& result) {
        result.stats.inputSamples = iq.size();
        result.stats.channelSampleRate = channel.sampleRate;
        result.stats.inputTargetOffsetHz = inputTargetOffsetHz;
        result.stats.frontEndDcBlockApplied = dcBlock.applied;
        result.stats.frontEndDcEstimateMagnitude = dcBlock.estimateMagnitude;
    };

    auto timingStateStorage = streamTimingStateSnapshot();
    const auto phase1TailSnapshot = snapshotPhase1BitTail();

    const bool phase2TrafficDecoder = m_config.phase2CqpskTrafficDemod && m_config.enablePhase2Decode;
    if (!phase2TrafficDecoder || !m_config.enableCqpskSearch) {
        auto fm = fmDiscriminatorFromChannel(channel.samples, channel.sampleRate);
        auto c4fm = processFmDiscriminatorInternal(fm.hz, fm.sampleRate, false);
        stampCommonStats(c4fm);
        c4fm.stats.discriminatorMeanHz = fm.meanHz;
        if (c4fm.stats.demodPath.empty()) c4fm.stats.demodPath = "C4FM";
        best = std::move(c4fm);
    } else {
        stampCommonStats(best);
        best.stats.sampleRate = channel.sampleRate;
        best.stats.symbolRate = m_config.symbolRate;
        best.stats.demodPath = "CQPSK-search";
    }

    const bool c4fmControlHardLock =
        !phase2TrafficDecoder &&
        m_config.stopC4fmSearchOnHardLock &&
        hasTrustedPhase1ControlPayload(best);
    if (!m_config.enableCqpskSearch || c4fmControlHardLock) {
        if (!best.dibits.empty()) {
            const auto selectedStats = best.stats;
            restorePhase1BitTail(phase1TailSnapshot);
            auto committed = processHardDibitsInternal(best.dibits, true);
            restoreSelectedDemodStats(committed, selectedStats);
            best = std::move(committed);
        } else {
            restorePhase1BitTail(phase1TailSnapshot);
        }
        best.stats.cqpskCandidatesEvaluated = 0;
        best.stats.c4fmHardLockSkippedCqpsk = c4fmControlHardLock;
        return best;
    }

    // processFmDiscriminatorInternal updates the same persistent timing state for C4FM.
    // Reload before CQPSK recovery so the final save below merges with those updates
    // instead of overwriting them with a stale pre-C4FM snapshot.
    timingStateStorage = streamTimingStateSnapshot();

    const double sps = channel.sampleRate / m_config.symbolRate;
    const int phaseSteps = static_cast<int>(std::clamp(std::ceil(sps * 0.5), 4.0, 6.0));
    std::vector<int> cqpskPhaseOrder;
    cqpskPhaseOrder.reserve(static_cast<size_t>(phaseSteps));
    auto addCqpskPhase = [&](int phaseIndex) {
        if (phaseIndex < 0 || phaseIndex >= phaseSteps) return;
        if (std::find(cqpskPhaseOrder.begin(), cqpskPhaseOrder.end(), phaseIndex) ==
            cqpskPhaseOrder.end()) {
            cqpskPhaseOrder.push_back(phaseIndex);
        }
    };
    if (m_config.realtimeVoiceSearch && m_config.phase2CqpskTrafficDemod) {
        // Field captures show the standards-valid Phase-2 eye often sits late
        // in the Gardner symbol period (for example phase 0.9), while phase 0.1
        // can still produce plausible sync/VCW but no MAC/ESS.  Realtime used
        // to exhaust its whole candidate budget inside phase 0.1.  Try the late
        // and alternate eyes first, then fall back to the original order.
        addCqpskPhase(phaseSteps - 1);
        addCqpskPhase(1);
        addCqpskPhase(phaseSteps - 2);
        addCqpskPhase(phaseSteps / 2);
        addCqpskPhase(0);
    }
    for (int p = 0; p < phaseSteps; ++p) addCqpskPhase(p);
    const std::array<double, 2> rotations{0.0, kPi * 0.25};
    std::optional<CqpskCandidateParams> selectedCqpskParams;
    std::optional<P25BlockTimingState> selectedCqpskTiming;
    int selectedCqpskTrust = 0;
    std::optional<P25LiveDecodeResult> lockedCqpskCandidate;
    std::optional<CqpskCandidateParams> lockedCqpskParams;
    std::optional<P25BlockTimingState> lockedCqpskTimingState;
    int lockedCqpskTrust = 0;
    std::optional<P25LiveDecodeResult> bestCqpskCandidate;
    std::optional<CqpskCandidateParams> bestCqpskParams;
    std::optional<P25BlockTimingState> bestCqpskTimingState;
    int bestCqpskTrust = 0;

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
        restorePhase1BitTail(phase1TailSnapshot);
        candidate = processHardDibitsInternal(soft.dibits, false);
        restorePhase1BitTail(phase1TailSnapshot);
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
        candidate.stats.cqpskCarrierLoopApplied = complexSymbols.carrierLoopApplied;
        candidate.stats.cqpskCarrierLoopCorrectionHz = complexSymbols.carrierLoopCorrectionHz;
        candidate.stats.cqpskCarrierLoopPhaseErrorRmsRad = complexSymbols.carrierLoopPhaseErrorRmsRad;
        candidate.stats.cqpskCarrierLoopSymbols = complexSymbols.carrierLoopSymbols;
        return candidate;
    };
    auto rememberBestCqpsk = [&](const P25LiveDecodeResult& candidate,
                                 const CqpskCandidateParams& params,
                                 const P25BlockTimingState& timing,
                                 int trust) {
        if (!hasPhase2TrafficTelemetry(candidate) &&
            trust <= 0 &&
            !hasCqpskSoftContinuityEvidence(candidate)) {
            return;
        }
        if (!bestCqpskCandidate || betterLiveResult(candidate, *bestCqpskCandidate, &m_config)) {
            bestCqpskCandidate = candidate;
            bestCqpskParams = params;
            bestCqpskTimingState = timing;
            bestCqpskTrust = trust;
        }
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
        const bool lockedSoftPhase2Evidence = hasPhase2SoftCqpskLockEvidence(candidate);
        rememberBestCqpsk(candidate, locked, lockedTiming, trust);
        if (trust > 0 || lockedSoftPhase2Evidence || hasCqpskSoftContinuityEvidence(candidate)) {
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
        if ((trust > 0 || lockedSoftPhase2Evidence) && betterLiveResult(candidate, best, &m_config)) {
            selectedCqpskParams = locked;
            selectedCqpskTiming = lockedTiming;
            selectedCqpskTrust = trust;
            best = std::move(candidate);
        }
    }

    const bool phase2StandardsStateSeen =
        m_phase2SessionMacCrcSeen ||
        m_phase2Ess.known ||
        std::any_of(m_phase2SlotSessionMacCrcSeen.begin(),
                    m_phase2SlotSessionMacCrcSeen.end(),
                    [](bool seen) { return seen; }) ||
        std::any_of(m_phase2SlotEss.begin(),
                    m_phase2SlotEss.end(),
                    [](const P25Phase2EssState& ess) { return ess.known; });
    const bool allowRealtimeLockOnlyCandidate =
        m_config.realtimeVoiceSearch &&
        m_config.phase2CqpskTrafficDemod &&
        m_config.maxCqpskSearchCandidates > 0 &&
        m_config.maxCqpskSearchCandidates <= 1;
    const bool allowRealtimePhase2SoftDemodHold =
        m_config.realtimeVoiceSearch &&
        m_config.phase2CqpskTrafficDemod &&
        m_config.stopCqpskSearchOnHardLock &&
        (phase2StandardsStateSeen ||
         m_config.allowPhase2SoftAmbeMaskPhaseLock ||
         allowRealtimeLockOnlyCandidate);
    const bool allowSoftCqpskStop =
        !m_config.realtimeVoiceSearch ||
        !m_config.phase2CqpskTrafficDemod ||
        phase2StandardsStateSeen ||
        allowRealtimePhase2SoftDemodHold;
    const bool allowCurrentWindowSoftCqpskStop =
        allowSoftCqpskStop &&
        (phase2StandardsStateSeen || m_cqpskLock.valid);
    const int cqpskMissLimit = static_cast<int>(std::clamp<size_t>(
        m_config.cqpskLockMissTolerance,
        8,
        96));
    bool stopCqpskSearch = m_config.stopCqpskSearchOnHardLock &&
        isCqpskPath(best.stats.demodPath) &&
        (hasCqpskHardLockEvidence(best) ||
         (allowCurrentWindowSoftCqpskStop &&
          m_config.realtimeVoiceSearch &&
          hasPhase2SoftCqpskLockEvidence(best)));
    bool realtimeSoftHoldSelected = false;
    bool realtimeLockOnlyHoldSelected = false;
    if (!stopCqpskSearch &&
        allowSoftCqpskStop &&
        m_config.realtimeVoiceSearch &&
        m_cqpskLock.valid &&
        isCqpskPath(best.stats.demodPath) &&
        hasPhase2SoftCqpskLockEvidence(best)) {
        // Live Phase-2 traffic is a continuous CQPSK/H-DQPSK stream (SDRTrunk
        // traffic-channel model).  Once a locked candidate is producing real
        // TDMA structure, keep consuming instead of reopening the full grid.
        // Bare continuity without soft-lock evidence still allows a challenge
        // search so a wrong early burst cannot freeze the demod.
        if (m_cqpskLock.misses < 5) {
            stopCqpskSearch = true;
            realtimeSoftHoldSelected = true;
            m_cqpskLock.misses = std::min(m_cqpskLock.misses + 1, 96);
            best.stats.cqpskStickyOverride = true;
            best.stats.cqpskLockMisses = m_cqpskLock.misses;
        }
    }
    if (!stopCqpskSearch &&
        allowRealtimePhase2SoftDemodHold &&
        lockedCqpskCandidate &&
        lockedCqpskParams &&
        lockedCqpskTimingState &&
        m_cqpskLock.valid &&
        m_cqpskLock.misses < cqpskMissLimit &&
        isCqpskPath(lockedCqpskCandidate->stats.demodPath) &&
        hasCqpskSoftContinuityEvidence(*lockedCqpskCandidate)) {
        // SDRTrunk keeps one live H-DQPSK/CQPSK traffic-channel processor per
        // call and lets the framer/audio gate decide whether the current
        // timeslot contains usable voice.  After a clear grant has already
        // acquired a CQPSK lock, do the same bounded lock-only pass instead of
        // reopening the full permutation grid on every 40 ms GUI/CLI hop.
        selectedCqpskParams = *lockedCqpskParams;
        selectedCqpskTiming = *lockedCqpskTimingState;
        selectedCqpskTrust = lockedCqpskTrust;
        best = *lockedCqpskCandidate;
        best.stats.cqpskStickyOverride = true;
        best.stats.cqpskLockMisses = m_cqpskLock.misses;
        stopCqpskSearch = true;
        realtimeLockOnlyHoldSelected = true;
    }
    // Soft-locked streaming: never reopen the permutation grid.  Misses are
    // aged below when the locked candidate fails; a full re-search only happens
    // after the lock is cleared.
    if (m_config.realtimeVoiceSearch &&
        allowSoftCqpskStop &&
        m_cqpskLock.valid &&
        m_cqpskLock.misses < static_cast<int>(std::clamp<size_t>(m_config.cqpskLockMissTolerance, 8, 96)) &&
        (realtimeSoftHoldSelected ||
         (isCqpskPath(best.stats.demodPath) && hasPhase2SoftCqpskLockEvidence(best)))) {
        stopCqpskSearch = true;
    }
    size_t cqpskCandidatesEvaluated = 0;
    const auto cqpskSearchStarted = std::chrono::steady_clock::now();
    auto cqpskBudgetReached = [&]() {
        if (realtimeBudgetExceeded()) return true;
        return m_config.maxCqpskSearchCandidates > 0 &&
            cqpskCandidatesEvaluated >= m_config.maxCqpskSearchCandidates;
    };
    for (int phaseIndex : cqpskPhaseOrder) {
        if (stopCqpskSearch) break;
        if (cqpskBudgetReached()) break;
        const double phase = (static_cast<double>(phaseIndex) + 0.5) * sps / static_cast<double>(phaseSteps);
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
                        if (cqpskBudgetReached()) {
                            stopCqpskSearch = true;
                            break;
                        }
                        ++cqpskCandidatesEvaluated;
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
                        restorePhase1BitTail(phase1TailSnapshot);
                        auto candidate = processHardDibitsInternal(soft.dibits, false);
                        restorePhase1BitTail(phase1TailSnapshot);
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
                        candidate.stats.cqpskCarrierLoopApplied = complexSymbols.carrierLoopApplied;
                        candidate.stats.cqpskCarrierLoopCorrectionHz = complexSymbols.carrierLoopCorrectionHz;
                        candidate.stats.cqpskCarrierLoopPhaseErrorRmsRad = complexSymbols.carrierLoopPhaseErrorRmsRad;
                        candidate.stats.cqpskCarrierLoopSymbols = complexSymbols.carrierLoopSymbols;
                        const int trust = liveResultTrustScore(candidate);
                        rememberBestCqpsk(candidate, params, candidateTiming, trust);
                        if (betterLiveResult(candidate, best, &m_config)) {
                            selectedCqpskParams = params;
                            selectedCqpskTiming = candidateTiming;
                            selectedCqpskTrust = trust;
                            best = std::move(candidate);
                            if (m_config.stopCqpskSearchOnHardLock &&
                                isCqpskPath(best.stats.demodPath) &&
                                (hasCqpskHardLockEvidence(best) ||
                                 (allowCurrentWindowSoftCqpskStop &&
                                  m_config.realtimeVoiceSearch &&
                                  hasPhase2SoftCqpskLockEvidence(best)))) {
                                // Match SDRTrunk continuous demod: lock the first
                                // TDMA eye and stop burning the realtime budget on
                                // remaining CQPSK permutations this window.
                                stopCqpskSearch = true;
                                break;
                            }
                        }
                    }
                }
            }
        }
    }
    const long long cqpskSearchMs = elapsedMsSince(cqpskSearchStarted);
    if (m_config.phase2CqpskTrafficDemod &&
        bestCqpskCandidate &&
        bestCqpskParams &&
        bestCqpskTimingState &&
        !isCqpskPath(best.stats.demodPath) &&
        hasPhase2TrafficTelemetry(*bestCqpskCandidate) &&
        !hasCqpskHardLockEvidence(best)) {
        // Phase 2 traffic is H-DQPSK/CQPSK-family modulation.  Do not let a
        // C4FM discriminator candidate with only soft superframe/voice-looking
        // telemetry outrank the best CQPSK traffic candidate; that creates
        // p2sf/p2mask without RS-valid MAC/ESS and starves clear audio on LSM
        // systems.  Hard evidence above still wins regardless of demod path.
        selectedCqpskParams = *bestCqpskParams;
        selectedCqpskTiming = *bestCqpskTimingState;
        selectedCqpskTrust = bestCqpskTrust;
        best = *bestCqpskCandidate;
        best.warnings.push_back("Phase 2 traffic selected CQPSK demod over C4FM without hard MAC/ESS evidence.");
    }
    bool cqpskWarmHoldSelected = false;
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
        } else if (competingHard == 0 &&
                   hasCqpskSoftContinuityEvidence(*lockedCqpskCandidate) &&
                   m_cqpskLock.misses + 1 < cqpskMissLimit) {
            selectedCqpskParams = *lockedCqpskParams;
            selectedCqpskTiming = *lockedCqpskTimingState;
            selectedCqpskTrust = lockedCqpskTrust;
            best = *lockedCqpskCandidate;
            best.stats.cqpskStickyOverride = true;
            best.warnings.push_back("Held proven CQPSK/LSM demod lock through a soft-only continuity window.");
            cqpskWarmHoldSelected = true;
        }
    }
    const bool selectedHardCqpskLock =
        selectedCqpskTrust >= 40 && hasCqpskHardLockEvidence(best);
    const bool selectedSoftPhase2CqpskLock =
        m_config.realtimeVoiceSearch &&
        (phase2StandardsStateSeen || m_config.allowPhase2SoftAmbeMaskPhaseLock) &&
        hasPhase2SoftCqpskLockEvidence(best);
    if (selectedCqpskParams && isCqpskPath(best.stats.demodPath) &&
        (selectedHardCqpskLock || selectedSoftPhase2CqpskLock || realtimeLockOnlyHoldSelected)) {
        const bool selectedLockOnlyWithNoPhase2 =
            realtimeLockOnlyHoldSelected &&
            !selectedHardCqpskLock &&
            !selectedSoftPhase2CqpskLock &&
            !hasPhase2TrafficTelemetry(best);
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
        m_cqpskLock.trustScore = selectedHardCqpskLock ? selectedCqpskTrust : std::max(selectedCqpskTrust, 20);
        if (selectedHardCqpskLock || selectedSoftPhase2CqpskLock) {
            m_cqpskLock.misses = 0;
        } else if (selectedLockOnlyWithNoPhase2) {
            m_cqpskLock.misses = std::min(cqpskMissLimit, m_cqpskLock.misses + 1);
        } else if (cqpskCandidatesEvaluated > 0) {
            m_cqpskLock.misses = 0;
        }
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
        if (realtimeSoftHoldSelected || realtimeLockOnlyHoldSelected) best.stats.cqpskStickyOverride = true;
        if (selectedLockOnlyWithNoPhase2 && m_cqpskLock.misses >= cqpskMissLimit) m_cqpskLock = {};
    } else if (m_cqpskLock.valid && cqpskWarmHoldSelected) {
        const int retainedTrust = m_cqpskLock.trustScore;
        const int retainedMisses = std::min(cqpskMissLimit, m_cqpskLock.misses + 1);
        m_cqpskLock.misses = retainedMisses;
        best.stats.cqpskLockActive = true;
        best.stats.cqpskLockUsed = true;
        best.stats.cqpskLockTrustScore = retainedTrust;
        best.stats.cqpskLockMisses = retainedMisses;
        if (retainedMisses >= cqpskMissLimit) m_cqpskLock = {};
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
    long long commitMs = 0;
    if (!best.dibits.empty()) {
        const auto selectedStats = best.stats;
        // Phase-2 traffic (SDRTrunk HDQPSK model): never publish probe-only
        // telemetry without an annotate/commit pass.  Skipping commit after the
        // CQPSK grid burned the realtime budget produced the live signature
        // p2bursts>0 p2vcw=0 while RF was locked (capture 080701 TG30302).
        const bool mustAnnotateCommit =
            m_config.phase2CqpskTrafficDemod ||
            hasPhase2SoftCqpskLockEvidence(best) ||
            hasCqpskHardLockEvidence(best);
        if (realtimeBudgetExceeded() && m_config.realtimeVoiceSearch &&
            hasPhase2TrafficTelemetry(best) && !mustAnnotateCommit) {
            restorePhase1BitTail(phase1TailSnapshot);
            noteRealtimeBudget(best);
        } else {
            restorePhase1BitTail(phase1TailSnapshot);
            const auto commitStarted = std::chrono::steady_clock::now();
            auto committed = processHardDibitsInternal(best.dibits, true);
            commitMs = elapsedMsSince(commitStarted);
            restoreSelectedDemodStats(committed, selectedStats);
            best = std::move(committed);
            if (realtimeBudgetExceeded()) noteRealtimeBudget(best);
        }
    } else {
        restorePhase1BitTail(phase1TailSnapshot);
    }
    if (selectedCqpskTiming && isCqpskPath(best.stats.demodPath)) {
        timingStateStorage.cqpskValid = selectedCqpskTiming->cqpskValid;
        timingStateStorage.cqpskOmega = selectedCqpskTiming->cqpskOmega;
        timingStateStorage.cqpskMu = selectedCqpskTiming->cqpskMu;
        timingStateStorage.cqpskSampleRate = selectedCqpskTiming->cqpskSampleRate;
        timingStateStorage.cqpskCarrierLoopValid = selectedCqpskTiming->cqpskCarrierLoopValid;
        timingStateStorage.cqpskCarrierLoopPhase = selectedCqpskTiming->cqpskCarrierLoopPhase;
        timingStateStorage.cqpskCarrierLoopOmega = selectedCqpskTiming->cqpskCarrierLoopOmega;
        timingStateStorage.cqpskTail = selectedCqpskTiming->cqpskTail;
    }
    storeStreamTimingState(timingStateStorage);
    const long long totalMs = elapsedMsSince(realtimeStarted);
    best.stats.cqpskCandidatesEvaluated = cqpskCandidatesEvaluated;
    if (m_config.realtimeVoiceSearch || totalMs >= 200) {
        best.warnings.push_back(
            "decodeProfile channelizeMs=" + std::to_string(channelizeMs) +
            " cqpskSearchMs=" + std::to_string(cqpskSearchMs) +
            " cqpskCandidates=" + std::to_string(cqpskCandidatesEvaluated) +
            " commitMs=" + std::to_string(commitMs) +
            " totalMs=" + std::to_string(totalMs) +
            " softHold=" + std::string(realtimeSoftHoldSelected ? "1" : "0") +
            " lockOnlyHold=" + std::string(realtimeLockOnlyHoldSelected ? "1" : "0") +
            " lock=" + std::string(m_cqpskLock.valid ? "1" : "0"));
    }
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
    P25DecoderTraceScope trace("P25LiveDecoder::processFmDiscriminatorInternal");
    P25LiveDecodeResult best;
    best.stats.discriminatorSamples = discriminatorHz.size();
    best.stats.sampleRate = sampleRate;
    best.stats.symbolRate = m_config.symbolRate;

    auto timingStateStorage = streamTimingStateSnapshot();
    P25BlockTimingState* timingState = &timingStateStorage;
    const auto phase1TailSnapshot = snapshotPhase1BitTail();
    auto evaluateCandidateDibits = [&](const std::vector<int>& dibits) {
        restorePhase1BitTail(phase1TailSnapshot);
        auto candidate = processHardDibitsInternal(dibits, false);
        restorePhase1BitTail(phase1TailSnapshot);
        return candidate;
    };

    auto c4fmRecoveryConfig = m_config;
    if (annotateSessionCodewords && !c4fmRecoveryConfig.enableC4fmFixedPhaseSearch) {
        // Standalone discriminator decoding has no IQ/CQPSK arbiter to rescue a
        // Gardner timing lock that latched onto a false preamble.  Enable the
        // bounded fixed-phase grid here; processIq() keeps using the caller's
        // explicit config so CQPSK/LSM traffic is not displaced by weak C4FM
        // hypotheses.
        c4fmRecoveryConfig.enableC4fmFixedPhaseSearch = true;
        if (c4fmRecoveryConfig.maxC4fmFixedPhaseCandidates == 0) {
            c4fmRecoveryConfig.maxC4fmFixedPhaseCandidates = 10;
        }
    }
    const auto symbolCandidates = recoverC4fmSymbolCandidates(discriminatorHz, sampleRate, c4fmRecoveryConfig, timingState);
    if (symbolCandidates.empty()) {
        storeStreamTimingState(timingStateStorage);
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
                    auto candidate = evaluateCandidateDibits(soft.dibits);
                    stampSoftDibitStats(candidate, soft);
                    candidate.stats.symbolConfidence = symbols.confidence;
                    if (!haveCandidate || betterLiveResult(candidate, best, &m_config)) {
                        best = std::move(candidate);
                        selectedSymbolConfidence = symbols.confidence;
                        selectedPath = symbols.path;
                        haveCandidate = true;
                        if ((m_config.stopC4fmSearchOnHardLock && hasCqpskHardLockEvidence(best)) ||
                            (m_config.stopCqpskSearchOnHardLock && hasPhase2FastStopEvidence(best))) {
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
                    auto candidate = evaluateCandidateDibits(soft.dibits);
                    stampSoftDibitStats(candidate, soft);
                    candidate.stats.symbolConfidence = symbols.confidence;
                    if (!haveCandidate || betterLiveResult(candidate, best, &m_config)) {
                        best = std::move(candidate);
                        selectedSymbolConfidence = symbols.confidence;
                        selectedPath = symbols.path;
                        haveCandidate = true;
                        if ((m_config.stopC4fmSearchOnHardLock && hasCqpskHardLockEvidence(best)) ||
                            (m_config.stopCqpskSearchOnHardLock && hasPhase2FastStopEvidence(best))) {
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
        restorePhase1BitTail(phase1TailSnapshot);
        auto committed = processHardDibitsInternal(best.dibits, true);
        restoreSelectedDemodStats(committed, selectedStats);
        best = std::move(committed);
    } else {
        restorePhase1BitTail(phase1TailSnapshot);
    }
    storeStreamTimingState(timingStateStorage);
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
    P25DecoderTraceScope trace("P25LiveDecoder::processHardDibitsInternal");
    P25LiveDecodeResult result;
    result.dibits = dibits;
    appendBitsFromDibits(result.bits, dibits);
    if (m_config.enablePhase1Decode) {
        result = processHardBits(result.bits);
    } else {
        result.stats.bits = result.bits.size();
        result.stats.symbolRate = m_config.symbolRate;
        result.stats.voiceBackendAvailable = compiledVoiceBackendAvailable();
    }
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
        if (pdu.fecDecoded) ++result.stats.phase2MacFecDecoded;
        if (pdu.directCrcOk) ++result.stats.phase2MacDirectCrcValid;
        if (pdu.directCrcOk && !pdu.directCrcParseable) ++result.stats.phase2MacDirectCrcRejected;
        if (pdu.rsDecoded) ++result.stats.phase2MacRsDecoded;
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
    result.stats.phase2MaskParametersKnown = m_phase2MaskParams.valid;
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
    if (result.stats.phase2MacPdus > 0 &&
        result.stats.phase2MacCrcValid == 0 &&
        result.stats.phase2SuperframeBursts > 0) {
        result.warnings.push_back(
            "Phase 2 ACCH extracted but no CRC-valid MAC: fec=" +
            std::to_string(result.stats.phase2MacFecDecoded) +
            " rs=" + std::to_string(result.stats.phase2MacRsDecoded) +
            " direct=" + std::to_string(result.stats.phase2MacDirectCrcValid) +
            " directRejected=" + std::to_string(result.stats.phase2MacDirectCrcRejected) +
            " pdus=" + std::to_string(result.stats.phase2MacPdus));
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

    // Realtime traffic follows a retained superframe/mask lattice, so only keep
    // enough dibit tail to bridge partial timeslots. Forensic/cold paths still
    // keep a full superframe for acquisition and MAC/ESS recovery.
    const size_t phase2SyncTailDibits = m_config.realtimeVoiceSearch
        ? Phase2BurstDibits * 4
        : Phase2BurstDibits * 12;
    constexpr size_t kMinTrustedPhase2OverlapDibits = Phase2BurstDibits / 2;
    auto longestTailPrefixOverlap = [&]() -> size_t {
        const size_t n = std::min({m_phase2DibitTail.size(), dibits.size(), phase2SyncTailDibits * 4});
        if (n == 0) return 0;

        std::vector<int> combined;
        combined.reserve(n * 2 + 1);
        combined.insert(combined.end(), dibits.begin(), dibits.begin() + static_cast<std::ptrdiff_t>(n));
        combined.push_back(-1);
        combined.insert(combined.end(),
                        m_phase2DibitTail.end() - static_cast<std::ptrdiff_t>(n),
                        m_phase2DibitTail.end());

        std::vector<size_t> pi(combined.size(), 0);
        for (size_t i = 1; i < combined.size(); ++i) {
            size_t j = pi[i - 1];
            while (j > 0 && combined[i] != combined[j]) j = pi[j - 1];
            if (combined[i] == combined[j]) ++j;
            pi[i] = j;
        }
        const size_t overlap = std::min(pi.back(), n);
        return overlap >= kMinTrustedPhase2OverlapDibits ? overlap : 0u;
    };

    const size_t phase2InputOverlapDibits = annotateSessionCodewords
        ? longestTailPrefixOverlap()
        : 0u;
    std::vector<int> scanDibits;
    size_t phase2PrefixDibits = std::min(m_phase2DibitTail.size(), phase2SyncTailDibits);
    if (phase2PrefixDibits > 0) {
        scanDibits.reserve(phase2PrefixDibits + dibits.size());
        scanDibits.insert(scanDibits.end(),
                          m_phase2DibitTail.end() - static_cast<std::ptrdiff_t>(phase2PrefixDibits),
                          m_phase2DibitTail.end());
        for (size_t i = phase2InputOverlapDibits; i < dibits.size(); ++i) {
            scanDibits.push_back(dibits[i] & 0x03);
        }
    }
    const auto& workingDibits = scanDibits.empty() ? dibits : scanDibits;
    const uint64_t phase2WorkingStreamStart = m_phase2StreamDibits >= static_cast<uint64_t>(phase2PrefixDibits)
        ? m_phase2StreamDibits - static_cast<uint64_t>(phase2PrefixDibits)
        : 0;
    if (workingDibits.size() < Phase2BurstDibits) {
        out.ess = m_phase2Ess;
        return out;
    }

    // Match sdrtrunk's hard-dibit Phase-2 framer: this layer accepts only the
    // normal 40-bit Phase-2 sync. Rotated/180-degree sync patterns are
    // constellation/PLL correction evidence for the demodulator, not valid
    // frame boundaries for an unrotated payload.
    static constexpr std::array<uint64_t, 1> kSyncWords = {
        Phase2FrameSyncWord,
    };
    const int phase2SyncMaxErrors = std::clamp(std::max(3, static_cast<int>(m_config.maxFrameSyncBitErrors)), 0, 6);
    // CQPSK grid probes call this with annotateSessionCodewords=false for every
    // permutation.  At the correct 6000 baud H-DQPSK rate, sync hits appear and
    // the old unbounded 12-phase mask search per candidate hung realtime windows
    // for minutes.  Keep probe scans cheap; full mask/SF work runs on commit.
    const size_t syncHitCap = !annotateSessionCodewords
        ? std::min(m_config.maxPhase2SyncHits == 0 ? size_t{16} : m_config.maxPhase2SyncHits, size_t{16})
        : m_config.maxPhase2SyncHits;
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
        if (syncHitCap > 0 && hits.size() >= syncHitCap) {
            break;
        }
    }

    const auto* mask = m_phase2MaskParams.valid ? &m_phase2XorMask : nullptr;
    auto locks = findPhase2SuperframeLocks(workingDibits, hits, workingDibits.size());

    // If we already have a validated Phase-2 superframe epoch, keep later rolling
    // windows aligned to that same epoch.  Without this, a full 12-burst window with
    // repetitive sync hits can choose an equally-scored lock one burst early/late,
    // flipping logical TDMA slot labels while the RF stream itself is stable.  That
    // is exactly the field symptom: p2sf/p2mask remain high, but windows alternate
    // between targetVcw and oppVcw / "wrong TDMA slot" and audio starves.
    const bool stickyAnchorMaskMatches = m_phase2SuperframeAnchorKnown &&
        m_phase2MaskPhaseKnown &&
        mask &&
        m_phase2SuperframeAnchorMaskParams.valid &&
        m_phase2MaskParams.valid &&
        m_phase2SuperframeAnchorMaskParams.nac == m_phase2MaskParams.nac &&
        m_phase2SuperframeAnchorMaskParams.wacn == m_phase2MaskParams.wacn &&
        m_phase2SuperframeAnchorMaskParams.systemId == m_phase2MaskParams.systemId &&
        m_phase2SuperframeAnchorMaskPhase == m_phase2MaskPhase;
    constexpr uint64_t kPhase2StickyAnchorMaxAgeGenerationsForLocks = 24;
    const uint64_t currentAnchorGenerationForLocks = m_phase2DecodeGeneration +
        (annotateSessionCodewords ? 1ull : 0ull);
    const bool stickyAnchorFreshForLocks = m_phase2SuperframeAnchorGeneration != 0 &&
        currentAnchorGenerationForLocks >= m_phase2SuperframeAnchorGeneration &&
        currentAnchorGenerationForLocks - m_phase2SuperframeAnchorGeneration <=
            kPhase2StickyAnchorMaxAgeGenerationsForLocks;
    if (stickyAnchorMaskMatches && stickyAnchorFreshForLocks) {
        auto anchoredLocks = findPhase2AnchorAlignedSuperframeLocks(
            hits, workingDibits, workingDibits.size(), phase2WorkingStreamStart, m_phase2SuperframeAnchorDibit);
        if (!anchoredLocks.empty()) {
            locks = std::move(anchoredLocks);
        }
    }

    const size_t lockCap = !annotateSessionCodewords
        ? std::min(m_config.maxPhase2SuperframeLocks == 0 ? size_t{1} : m_config.maxPhase2SuperframeLocks, size_t{1})
        : m_config.maxPhase2SuperframeLocks;
    if (lockCap > 0 && locks.size() > lockCap) {
        locks.resize(lockCap);
    }

    // CQPSK candidate probes: sync-hit telemetry only.  Full lock/mask/MAC work
    // runs on the annotate/commit pass.  Without this, a correct 6000 baud eye
    // spends ~1s × 36 permutations re-decoding the same TDMA structure.
    if (!annotateSessionCodewords) {
        for (const auto& hit : hits) {
            if (hit.dibitOffset + Phase2BurstDibits > workingDibits.size()) continue;
            P25Phase2Burst burst;
            burst.valid = true;
            burst.dibitOffset = hit.dibitOffset >= phase2PrefixDibits
                ? hit.dibitOffset - phase2PrefixDibits
                : 0;
            burst.syncErrors = hit.errors;
            if (!locks.empty() &&
                hit.dibitOffset >= locks.front().dibitOffset &&
                hit.dibitOffset < locks.front().dibitOffset + Phase2BurstDibits * 12) {
                burst.superframeLocked = true;
                burst.superframeDibitOffset = locks.front().dibitOffset >= phase2PrefixDibits
                    ? locks.front().dibitOffset - phase2PrefixDibits
                    : 0;
                burst.superframeBurstIndexKnown = true;
                burst.superframeBurstIndex = static_cast<int>(
                    ((hit.dibitOffset - locks.front().dibitOffset) / Phase2BurstDibits) % 12u);
            }
            out.bursts.push_back(std::move(burst));
            if (out.bursts.size() >= 8) break;
        }
        out.ess = m_phase2Ess;
        return out;
    }

    std::vector<std::pair<size_t, size_t>> lockedWindows;
    auto normalizePhase2BurstOffsets = [&](P25Phase2Burst& burst) {
        if (phase2PrefixDibits == 0) return;
        for (auto& cw : burst.voiceCodewords) {
            cw.duplicateInSession = cw.duplicateInSession || cw.dibitOffset < phase2PrefixDibits;
            cw.dibitOffset = cw.dibitOffset >= phase2PrefixDibits
                ? cw.dibitOffset - phase2PrefixDibits + phase2InputOverlapDibits
                : 0;
        }
        burst.voiceCodewords.erase(
            std::remove_if(burst.voiceCodewords.begin(), burst.voiceCodewords.end(),
                [](const P25Phase2VoiceCodeword& cw) { return cw.duplicateInSession && cw.dibitOffset == 0; }),
            burst.voiceCodewords.end());
        burst.dibitOffset = burst.dibitOffset >= phase2PrefixDibits
            ? burst.dibitOffset - phase2PrefixDibits + phase2InputOverlapDibits
            : 0;
        if (burst.superframeDibitOffset >= phase2PrefixDibits) {
            burst.superframeDibitOffset =
                burst.superframeDibitOffset - phase2PrefixDibits + phase2InputOverlapDibits;
        }
        else burst.superframeDibitOffset = 0;
        if (burst.isch.dibitOffset >= phase2PrefixDibits) {
            burst.isch.dibitOffset = burst.isch.dibitOffset - phase2PrefixDibits + phase2InputOverlapDibits;
        }
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
            m_phase2SlotSessionMacCrcSeen[ts] ||
            m_phase2SlotFirst4vSlot[ts] >= 0 ||
            m_phase2SlotEssBNext[ts] != 0 ||
            std::any_of(m_phase2SlotEssBSeen[ts].begin(), m_phase2SlotEssBSeen[ts].end(), [](bool seen) { return seen; });
        session.ess = slotHasRetainedState ? m_phase2SlotEss[ts] : m_phase2Ess;
        session.essTrusted = session.ess.known && session.ess.fecValidated;
        session.essB = slotHasRetainedState ? m_phase2SlotEssB[ts] : m_phase2EssB;
        session.essBSeen = slotHasRetainedState ? m_phase2SlotEssBSeen[ts] : m_phase2EssBSeen;
        session.essBNext = slotHasRetainedState ? m_phase2SlotEssBNext[ts] : m_phase2EssBNext;
        session.macCrcSeen = slotHasRetainedState ? m_phase2SlotSessionMacCrcSeen[ts] : m_phase2SessionMacCrcSeen;
        session.pttSeen = session.macCrcSeen;
        session.first4vSlot = slotHasRetainedState ? m_phase2SlotFirst4vSlot[ts] : m_phase2First4vSlot;
        session.essHypotheses = slotHasRetainedState ? m_phase2SlotEssHypotheses[ts] : m_phase2EssHypotheses;
        session.essBHypotheses = slotHasRetainedState ? m_phase2SlotEssBHypotheses[ts] : m_phase2EssBHypotheses;
        session.essBSeenHypotheses = slotHasRetainedState ? m_phase2SlotEssBSeenHypotheses[ts] : m_phase2EssBSeenHypotheses;
    }
    Phase2SessionState& session = slotSessions[0];
    auto recentlyDecodedAcchBurst = [&](uint64_t streamDibit) {
        constexpr uint64_t kAcchDecodeDibitTolerance = 12;
        for (uint64_t prior : m_phase2RecentAcchDecodeBurstDibits) {
            const uint64_t distance = streamDibit > prior ? streamDibit - prior : prior - streamDibit;
            if (distance <= kAcchDecodeDibitTolerance) return true;
        }
        return false;
    };
    auto rememberAcchBurst = [&](uint64_t streamDibit) {
        m_phase2RecentAcchDecodeBurstDibits.push_back(streamDibit);
        constexpr size_t kMaxRecentAcchBursts = 128;
        while (m_phase2RecentAcchDecodeBurstDibits.size() > kMaxRecentAcchBursts) {
            m_phase2RecentAcchDecodeBurstDibits.pop_front();
        }
    };

    auto retainPhase2SlotSessions = [&]() {
        Phase2SessionState* retainedSession = &slotSessions[0];
        for (auto& candidate : slotSessions) {
            if (candidate.ess.known && candidate.essTrusted &&
                (!retainedSession->ess.known || candidate.ess.fecValidated)) {
                retainedSession = &candidate;
            }
        }
        out.ess = retainedSession->ess;
        for (size_t ts = 0; ts < slotSessions.size(); ++ts) {
            m_phase2SlotEss[ts] = slotSessions[ts].ess;
            m_phase2SlotEssB[ts] = slotSessions[ts].essB;
            m_phase2SlotEssBSeen[ts] = slotSessions[ts].essBSeen;
            m_phase2SlotEssBNext[ts] = slotSessions[ts].essBNext;
            m_phase2SlotSessionMacCrcSeen[ts] = slotSessions[ts].pttSeen;
            m_phase2SlotFirst4vSlot[ts] = slotSessions[ts].first4vSlot;
            m_phase2SlotEssHypotheses[ts] = slotSessions[ts].essHypotheses;
            m_phase2SlotEssBHypotheses[ts] = slotSessions[ts].essBHypotheses;
            m_phase2SlotEssBSeenHypotheses[ts] = slotSessions[ts].essBSeenHypotheses;
        }
        m_phase2Ess = retainedSession->ess;
        m_phase2EssB = retainedSession->essB;
        m_phase2EssBSeen = retainedSession->essBSeen;
        m_phase2EssBNext = retainedSession->essBNext;
        m_phase2SessionMacCrcSeen = retainedSession->pttSeen;
        m_phase2First4vSlot = retainedSession->first4vSlot;
        m_phase2EssHypotheses = retainedSession->essHypotheses;
        m_phase2EssBHypotheses = retainedSession->essBHypotheses;
        m_phase2EssBSeenHypotheses = retainedSession->essBSeenHypotheses;
    };

    // Hot Phase-2 traffic path: after MAC/ESS/ISCH has already established the
    // superframe lattice and XOR phase, keep walking that lattice directly.
    // This mirrors sdrtrunk's continuous P25P2SuperFrameDetector model and
    // avoids re-running full lock/mask/ACCH hunts over overlapped historical
    // context on every 40 ms GUI hop.
    if (annotateSessionCodewords &&
        m_config.realtimeVoiceSearch &&
        stickyAnchorMaskMatches &&
        stickyAnchorFreshForLocks &&
        mask &&
        workingDibits.size() >= Phase2BurstDibits) {
        const uint64_t workStartStream = phase2WorkingStreamStart;
        const uint64_t workEndStream = phase2WorkingStreamStart +
            static_cast<uint64_t>(workingDibits.size());
        const uint64_t freshStartStream = phase2WorkingStreamStart +
            static_cast<uint64_t>(phase2PrefixDibits);
        const uint64_t anchor = m_phase2SuperframeAnchorDibit;
        constexpr uint64_t kBurst = static_cast<uint64_t>(Phase2BurstDibits);
        constexpr size_t kMaxRealtimeStickyBursts = 18; // 540 ms cap; GUI chunks are smaller.
        uint64_t firstBurstNum = 0;
        if (workStartStream > anchor) {
            const uint64_t delta = workStartStream - anchor;
            firstBurstNum = (delta + kBurst - 1ull) / kBurst;
        }
        size_t fastBursts = 0;
        for (uint64_t burstNum = firstBurstNum;
             fastBursts < kMaxRealtimeStickyBursts;
             ++burstNum) {
            const uint64_t streamPos = anchor + burstNum * kBurst;
            if (streamPos + kBurst > workEndStream) break;
            if (streamPos < workStartStream) continue;
            if (streamPos + kBurst <= freshStartStream) continue;
            const size_t workPos = static_cast<size_t>(streamPos - workStartStream);
            if (workPos + Phase2BurstDibits > workingDibits.size()) break;

            const size_t superframeIndex = static_cast<size_t>(burstNum % 12ull);
            const size_t syntheticLockOffset = workPos >= superframeIndex * Phase2BurstDibits
                ? workPos - superframeIndex * Phase2BurstDibits
                : 0;
            constexpr size_t kMaxRealtimeStickySyncSlipDibits = 12;
            const auto hit = phase2SyncHitNear(hits, workPos, kMaxRealtimeStickySyncSlipDibits);
            const uint8_t trafficSlot =
                phase2TrafficSlotForSuperframeBurst(workingDibits, syntheticLockOffset, superframeIndex);
            auto& burstSession = slotSessions[trafficSlot & 0x01u];
            const uint64_t burstStreamDibit =
                phase2WorkingStreamStart + static_cast<uint64_t>(hit ? hit->dibitOffset : workPos);
            const bool duplicateRealtimeAcch = recentlyDecodedAcchBurst(burstStreamDibit);
            auto burst = decodePhase2BurstAt(
                workingDibits,
                hit ? hit->dibitOffset : workPos,
                hit ? hit->errors : -1,
                true,
                syntheticLockOffset,
                superframeIndex,
                4,
                hit ? hit->errors : 0,
                mask,
                m_phase2MaskPhase,
                std::max(1, m_phase2MaskPhaseScore),
                &burstSession,
                &out.macPdus,
                false,
                false,
                !duplicateRealtimeAcch);
            if (!burst.valid) continue;
            if (!duplicateRealtimeAcch && phase2BurstKindCarriesAcch(burst.kind)) {
                rememberAcchBurst(burstStreamDibit);
            }
            burst.stickySuperframe = true;
            burst.superframeLock = true;
            burst.maskPhaseLock = burst.xorMaskApplied && burst.xorMaskPhaseKnown;
            if (hit && hit->dibitOffset != workPos) {
                burst.syncOffsetAdjusted = true;
                burst.syncOffsetDibits = phase2SignedSyncSlipDibits(hit->dibitOffset, workPos);
            }
            normalizePhase2BurstOffsets(burst);
            out.bursts.push_back(std::move(burst));
            ++fastBursts;
        }
        if (!out.bursts.empty()) {
            m_phase2SuperframeAnchorGeneration = m_phase2DecodeGeneration + 1;
            retainPhase2SlotSessions();
            annotatePhase2SessionCodewords(out, dibits);
            return out;
        }
    }

    for (const auto& lock : locks) {
        lockedWindows.push_back({lock.dibitOffset, P25LiveDecoder::Phase2BurstDibits * 12});

        uint8_t selectedMaskPhase = m_phase2MaskPhaseKnown ? m_phase2MaskPhase : 0;
        int selectedMaskScore = m_phase2MaskPhaseKnown ? m_phase2MaskPhaseScore : 0;
        size_t selectedMacCrc = 0;
        bool realtimeMaskHuntThrottledForLock = false;
        if (mask) {
            // If a sticky phase repeatedly contradicts ISCH/ACCH evidence, re-open
            // the 12-phase hunt instead of permanently descrambling with garbage.
            // Do not re-hunt on a single voice window with no MAC CRC: live SACCH/
            // FACCH CRCs are sparse under fading, and thrashing the XOR phase is
            // worse than holding a proven sdrtrunk-style superframe lattice.
            constexpr uint8_t kMaskPhaseStarveLimit = 4;
            const bool forceMaskPhaseRehunt =
                annotateSessionCodewords &&
                m_phase2MaskPhaseKnown &&
                m_phase2MaskPhaseStarveWindows >= kMaskPhaseStarveLimit;
            if (forceMaskPhaseRehunt) {
                m_phase2MaskPhaseKnown = false;
                m_phase2MaskPhaseScore = 0;
                m_phase2MaskPhaseStarveWindows = 0;
            }
            if (m_phase2MaskPhaseKnown) {
                // Sustain sticky: keep the locked phase.  Do NOT re-score all
                // 12 phases with AMBE here — field logs showed DSP ~800ms–1s
                // per window and ringFill 2–9% underruns (blocky 20–80ms audio
                // islands).  Starve re-hunt + cold AMBE phase score already
                // correct a bad sticky phase without taxing every sustain tick.
                selectedMaskPhase = m_phase2MaskPhase;
                selectedMaskScore = std::max(1, m_phase2MaskPhaseScore);
                if (annotateSessionCodewords) {
                    m_phase2SuperframeAnchorKnown = true;
                    m_phase2SuperframeAnchorDibit = phase2WorkingStreamStart + static_cast<uint64_t>(lock.dibitOffset);
                    m_phase2SuperframeAnchorGeneration = m_phase2DecodeGeneration + 1;
                    m_phase2SuperframeAnchorMaskParams = m_phase2MaskParams;
                    m_phase2SuperframeAnchorMaskPhase = selectedMaskPhase;
                }
            } else {
                Phase2MaskPhaseWindow bestWindow;
                std::vector<Phase2MaskPhaseWindow> phaseWindows;
                bool haveWindow = false;
                // SDRTrunk/OP25 keep a continuous TDMA framer. In the rolling-window
                // live path, an unknown-security late entry must not redo a 12-phase
                // mask hunt on every hot slice; wait for MAC/ESS/ISCH evidence unless
                // enough stream generations have elapsed to justify another broad hunt.
                constexpr uint64_t kRealtimeFullMaskPhaseHuntSpacingGenerations = 24;
                realtimeMaskHuntThrottledForLock =
                    annotateSessionCodewords &&
                    m_config.realtimeVoiceSearch &&
                    !m_config.allowPhase2SoftAmbeMaskPhaseLock &&
                    m_phase2LastFullMaskPhaseHuntGeneration != 0 &&
                    m_phase2DecodeGeneration > m_phase2LastFullMaskPhaseHuntGeneration &&
                    (m_phase2DecodeGeneration - m_phase2LastFullMaskPhaseHuntGeneration) <
                        kRealtimeFullMaskPhaseHuntSpacingGenerations;
                const bool cheapProbeMask = !annotateSessionCodewords || realtimeMaskHuntThrottledForLock;
                if (!cheapProbeMask && annotateSessionCodewords && m_config.realtimeVoiceSearch) {
                    m_phase2LastFullMaskPhaseHuntGeneration = m_phase2DecodeGeneration;
                }
                // Cold hunt scores 6 slots/phase; early-exit on MAC/ISCH. Keep all 12 phases.
                const uint8_t phaseBegin = 0;
                const uint8_t phaseEnd = cheapProbeMask ? uint8_t{1} : uint8_t{12};
            phaseWindows.reserve(phaseEnd - phaseBegin);
            for (uint8_t phaseIdx = phaseBegin; phaseIdx < phaseEnd; ++phaseIdx) {
                const uint8_t phase = cheapProbeMask ? uint8_t{0} : phaseIdx;
                const bool sticky = false;
                // Realtime follows need bounded work per rolling hop.  Score a
                // partial superframe first and let the next hop continue the
                // stream; forensic mode still scores the full 12-slot epoch.
                const size_t scoreSlots = cheapProbeMask ? 6u : (m_config.realtimeVoiceSearch ? 3u : 12u);
                auto window = scorePhase2MaskPhaseWindow(workingDibits, hits, lock, mask, phase, slotSessions, sticky, scoreSlots);
                if (!haveWindow || betterPhase2MaskPhaseWindow(window, bestWindow)) {
                    bestWindow = std::move(window);
                    haveWindow = true;
                } else if (!cheapProbeMask) {
                    phaseWindows.push_back(std::move(window));
                }
                if (haveWindow && !cheapProbeMask && phaseWindows.empty()) {
                    phaseWindows.push_back(bestWindow);
                }
                if (cheapProbeMask) break;
                // Only early-exit when a phase produces hard MAC/ESS proof.
                // A raw score>=400 early-exit previously stuck the hunt on a high
                // sync/voice-only phase (often phase 0) and never tried the rest —
                // matching field logs: p2sf/p2mask high, p2mac=0 forever, AMBE mute.
                if (bestWindow.macCrcValid > 0 || bestWindow.essKnown) break;
            }
            if (haveWindow &&
                annotateSessionCodewords &&
                m_config.realtimeVoiceSearch &&
                mask &&
                bestWindow.macCrcValid == 0 &&
                !bestWindow.essKnown) {
                const bool bestPhaseQueued = std::any_of(
                    phaseWindows.begin(),
                    phaseWindows.end(),
                    [&](const Phase2MaskPhaseWindow& w) {
                        return w.phase == bestWindow.phase;
                    });
                if (!bestPhaseQueued) {
                    phaseWindows.push_back(bestWindow);
                }
                const bool hasVoiceOrMaskEvidence = std::any_of(
                    bestWindow.bursts.begin(),
                    bestWindow.bursts.end(),
                    [](const P25Phase2Burst& b) {
                        return b.xorMaskApplied &&
                            (phase2BurstKindHasVoice(b.kind) ||
                             !b.voiceCodewords.empty());
                    });
                if (hasVoiceOrMaskEvidence && !phaseWindows.empty()) {
                    std::sort(phaseWindows.begin(), phaseWindows.end(),
                        [](const Phase2MaskPhaseWindow& a, const Phase2MaskPhaseWindow& b) {
                            return betterPhase2MaskPhaseWindow(a, b);
                        });
                    const size_t maxRescueCandidates =
                        m_config.realtimeVoiceSearch ? std::min<size_t>(phaseWindows.size(), 1u)
                                                     : phaseWindows.size();
                    const size_t rescueScoreSlots = m_config.realtimeVoiceSearch ? 3u : 12u;
                    const size_t rescueDeepBudget = m_config.realtimeVoiceSearch ? 1u : 8u;
                    size_t rescueCandidates = 0;
                    for (const auto& candidate : phaseWindows) {
                        if (rescueCandidates++ >= maxRescueCandidates) break;
                        auto rescued = scorePhase2MaskPhaseWindow(
                            workingDibits, hits, lock, mask,
                            candidate.phase, slotSessions, false, rescueScoreSlots,
                            true, rescueDeepBudget);
                        if (rescued.macCrcValid > 0 || rescued.essKnown) {
                            if (betterPhase2MaskPhaseWindow(rescued, bestWindow)) {
                                bestWindow = std::move(rescued);
                            }
                            break;
                        }
                    }
                }
            }
            if (haveWindow) {
                const uint8_t candidateMaskPhase = bestWindow.phase;
                const int candidateMaskScore = bestWindow.score;
                const size_t candidateMacCrc = bestWindow.macCrcValid;
                const bool ischAnchoredMaskPhase = std::any_of(bestWindow.bursts.begin(), bestWindow.bursts.end(), [](const P25Phase2Burst& b) {
                    return b.isch.valid && !b.isch.sync &&
                        b.isch.channel <= 1 && b.isch.location <= 2 &&
                        b.superframeBurstIndexKnown && b.grantSlotKnown &&
                        static_cast<uint8_t>(b.isch.channel & 0x01u) == static_cast<uint8_t>(b.grantSlot & 0x01u) &&
                        b.isch.errors >= 0 && b.isch.errors < 3;
                }) && candidateMaskScore > 0;
                // Count I-ISCH mask-index hits for the selected phase so we can
                // require real standards-level phase evidence before sticky lock.
                size_t ischPhaseHits = 0;
                size_t ischPhaseMisses = 0;
                for (const auto& b : bestWindow.bursts) {
                    if (!b.isch.valid || b.isch.sync || b.isch.location > 2 || !b.superframeBurstIndexKnown) continue;
                    if (b.isch.errors >= 0 && b.isch.errors >= 3) continue;
                    const size_t burstIndex = static_cast<size_t>(b.superframeBurstIndex % 12u);
                    const size_t expectedMaskIndex = static_cast<size_t>(b.isch.location) * 4u + (burstIndex % 4u);
                    const size_t actualMaskIndex = (burstIndex + static_cast<size_t>(candidateMaskPhase)) % 12u;
                    if (actualMaskIndex == expectedMaskIndex) ++ischPhaseHits;
                    else ++ischPhaseMisses;
                }
                const bool ischPhaseConfirmed =
                    ischPhaseHits >= 1 && ischPhaseHits > ischPhaseMisses;
                // Do NOT sticky-lock from voice-codeword counts alone.  Any mask
                // phase that happens to decode a Voice2/4 DUID will extract VCWs;
                // sticky-locking that phase permanently mutes clear audio when the
                // descramble is wrong (classic field: p2vcw high, decoded=0, p2mac=0).
                // Stick with MAC CRC, ESS, or confirmed I-ISCH. AMBE/Golay score
                // remains useful telemetry, but is not standards-level evidence
                // for choosing the XOR segment on a cold voice-only window.
                const bool standardsPhaseEvidence =
                    candidateMacCrc > 0 || bestWindow.essKnown ||
                    ischAnchoredMaskPhase || ischPhaseConfirmed;
                const bool softAmbePhaseEvidence =
                    !standardsPhaseEvidence &&
                    bestWindow.ambeSamples >= 2 &&
                    bestWindow.ambeLowError >= 1 &&
                    candidateMaskScore > 0;
                if (standardsPhaseEvidence || softAmbePhaseEvidence) {
                    selectedMaskPhase = candidateMaskPhase;
                    selectedMaskScore = standardsPhaseEvidence ? candidateMaskScore : 0;
                    selectedMacCrc = candidateMacCrc;
                }
                if (annotateSessionCodewords &&
                    (standardsPhaseEvidence ||
                     (m_config.allowPhase2SoftAmbeMaskPhaseLock && softAmbePhaseEvidence))) {
                    m_phase2MaskPhaseKnown = true;
                    m_phase2MaskPhase = selectedMaskPhase;
                    m_phase2MaskPhaseScore = selectedMaskScore;
                    m_phase2MaskPhaseStarveWindows = 0;
                    m_phase2SuperframeAnchorKnown = true;
                    m_phase2SuperframeAnchorDibit = phase2WorkingStreamStart + static_cast<uint64_t>(lock.dibitOffset);
                    m_phase2SuperframeAnchorGeneration = m_phase2DecodeGeneration + 1;
                    m_phase2SuperframeAnchorMaskParams = m_phase2MaskParams;
                    m_phase2SuperframeAnchorMaskPhase = selectedMaskPhase;
                }
            }
            }
        }

        const bool throttleRealtimeUnknownSecurityAcchRescue =
            realtimeMaskHuntThrottledForLock &&
            !m_config.allowPhase2SoftAmbeMaskPhaseLock &&
            !m_phase2MaskPhaseKnown;
        size_t realtimeAcchRescueBudget =
            annotateSessionCodewords && m_config.realtimeVoiceSearch && mask &&
            !throttleRealtimeUnknownSecurityAcchRescue
            ? size_t{2}
            : size_t{0};
        for (size_t slot = 0; slot < 12; ++slot) {
            const size_t expectedPos = lock.dibitOffset + slot * Phase2BurstDibits;
            if (expectedPos + Phase2BurstDibits > workingDibits.size()) break;
            const auto hit = phase2SyncHitNear(hits, expectedPos, 2);
            const size_t pos = hit ? hit->dibitOffset : expectedPos;
            if (pos + Phase2BurstDibits > workingDibits.size()) continue;
            const uint8_t trafficSlot =
                phase2TrafficSlotForSuperframeBurst(workingDibits, lock.dibitOffset, slot);
            auto& burstSession = slotSessions[trafficSlot & 0x01u];
            const uint64_t burstStreamDibit =
                phase2WorkingStreamStart + static_cast<uint64_t>(pos);
            const bool duplicateRealtimeAcch =
                annotateSessionCodewords &&
                m_config.realtimeVoiceSearch &&
                mask &&
                recentlyDecodedAcchBurst(burstStreamDibit);
            // Shallow ACCH on the hot lock path.  Deep alt-layout RS fan-out was
            // ~3s/burst × 12 slots once 6000 baud produced real locks (39s windows)
            // while TG30003 clear audio still emitted with p2mac=0 via grant gate.
            // Deep search remains available on late-entry bootstrap / sticky repair.
            auto burst = decodePhase2BurstAt(workingDibits, pos, hit ? hit->errors : -1,
                                             true, lock.dibitOffset, slot,
                                             lock.syncScore, lock.syncErrors,
                                             mask, selectedMaskPhase, selectedMaskScore,
                                             &burstSession, &out.macPdus,
                                             false,
                                             true,
                                             !duplicateRealtimeAcch);
            if (!duplicateRealtimeAcch && burst.valid && phase2BurstKindCarriesAcch(burst.kind)) {
                rememberAcchBurst(burstStreamDibit);
            }
            if (burst.valid &&
                !burst.macCrcValid &&
                realtimeAcchRescueBudget > 0 &&
                phase2BurstKindCarriesAcch(burst.kind) &&
                burst.voiceCodewords.empty()) {
                // Realtime first pass is intentionally shallow.  When the RF
                // window already has a stable TDMA epoch/mask but no MAC CRC,
                // spend a bounded rescue budget on actual ACCH-bearing bursts.
                // Earlier builds burned the budget on early Unknown DUID slots
                // and missed later SACCH MAC_ACTIVE Group User bursts; the
                // speaker gate then queued voice forever with p2mac=0.
                --realtimeAcchRescueBudget;
                Phase2SessionState rescueSession = burstSession;
                std::vector<P25Phase2MacPdu> rescueMacPdus;
                auto rescueBurst = decodePhase2BurstAt(workingDibits, pos, hit ? hit->errors : -1,
                                                       true, lock.dibitOffset, slot,
                                                       lock.syncScore, lock.syncErrors,
                                                       mask, selectedMaskPhase, selectedMaskScore,
                                                       &rescueSession, &rescueMacPdus,
                                                       true,
                                                       false,
                                                       true);
                const bool nominalRescueCrc = std::any_of(
                    rescueMacPdus.begin(),
                    rescueMacPdus.end(),
                    [](const P25Phase2MacPdu& pdu) {
                        const bool nominalKind =
                            pdu.detectedKind == P25Phase2BurstKind::Unknown ||
                            pdu.detectedKind == pdu.source;
                        return pdu.crcValid &&
                            nominalKind &&
                            !pdu.acchBitOrderSwapped &&
                            !pdu.acchDibitInverted &&
                            pdu.acchSlipDibits == 0;
                    });
                if (rescueBurst.valid && nominalRescueCrc &&
                    (rescueBurst.macCrcValid ||
                     rescueBurst.sessionAudioRelease ||
                     rescueBurst.essKnown ||
                     rescueBurst.trafficSecurityKnown)) {
                    burstSession = rescueSession;
                    out.macPdus.insert(out.macPdus.end(),
                                       std::make_move_iterator(rescueMacPdus.begin()),
                                       std::make_move_iterator(rescueMacPdus.end()));
                    burst = std::move(rescueBurst);
                }
            }
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

    // Continuous sticky superframe walk (SDRTrunk P25P2SuperFrameDetector model).
    // After epoch+mask are locked, frame EVERY complete 180-dibit timeslot along
    // the working stream — not only the first 12-slot lock object.  Live logs
    // (20260712_125240) showed exactly 4 AMBE frames every ~1.5–2.5s: one Voice4
    // from a single lock while multi-superframe RF between worker jobs was never
    // walked.  SDRTrunk emits SuperFrameFragments continuously as dibits arrive.
    if (annotateSessionCodewords &&
        m_phase2SuperframeAnchorKnown &&
        m_phase2MaskPhaseKnown &&
        mask &&
        workingDibits.size() >= Phase2BurstDibits) {
        const bool anchorMaskMatches = m_phase2SuperframeAnchorMaskParams.valid &&
            m_phase2MaskParams.valid &&
            m_phase2SuperframeAnchorMaskParams.nac == m_phase2MaskParams.nac &&
            m_phase2SuperframeAnchorMaskParams.wacn == m_phase2MaskParams.wacn &&
            m_phase2SuperframeAnchorMaskParams.systemId == m_phase2MaskParams.systemId &&
            m_phase2SuperframeAnchorMaskPhase == m_phase2MaskPhase;
        if (anchorMaskMatches) {
            const uint64_t workStartStream = phase2WorkingStreamStart;
            const uint64_t workEndStream = phase2WorkingStreamStart +
                static_cast<uint64_t>(workingDibits.size());
            const uint64_t anchor = m_phase2SuperframeAnchorDibit;
            constexpr uint64_t kBurst = static_cast<uint64_t>(Phase2BurstDibits);
            constexpr size_t kMaxContinuousBursts = 36; // up to 3 superframes / window
            size_t continuousAdded = 0;
            // First burst index on/after the working window start that lands on
            // the retained superframe lattice.
            uint64_t firstBurstNum = 0;
            if (workStartStream > anchor) {
                const uint64_t delta = workStartStream - anchor;
                firstBurstNum = (delta + kBurst - 1ull) / kBurst;
            }
            for (uint64_t burstNum = firstBurstNum;
                 continuousAdded < kMaxContinuousBursts;
                 ++burstNum) {
                const uint64_t streamPos = anchor + burstNum * kBurst;
                if (streamPos + kBurst > workEndStream) break;
                if (streamPos < workStartStream) continue;
                const size_t workPos = static_cast<size_t>(streamPos - workStartStream);
                if (workPos + Phase2BurstDibits > workingDibits.size()) break;
                if (workPos + Phase2BurstDibits <= phase2PrefixDibits) continue;
                const bool alreadyCovered = std::any_of(
                    lockedWindows.begin(), lockedWindows.end(),
                    [&](const auto& window) {
                        return phase2OffsetInWindow(workPos, window.first, window.second);
                    });
                if (alreadyCovered) continue;
                // After normalize, dibitOffset is relative to the caller's input
                // (without the Phase-2 tail prefix).
                const size_t callerPos = workPos >= phase2PrefixDibits
                    ? workPos - phase2PrefixDibits + phase2InputOverlapDibits
                    : workPos;
                const bool alreadyEmitted = std::any_of(
                    out.bursts.begin(), out.bursts.end(),
                    [&](const P25Phase2Burst& b) {
                        if (!b.valid) return false;
                        const size_t a = b.dibitOffset;
                        const size_t d = a > callerPos ? a - callerPos : callerPos - a;
                        return d <= 2;
                    });
                if (alreadyEmitted) continue;

                const size_t superframeIndex = static_cast<size_t>(burstNum % 12ull);
                const size_t syntheticLockOffset = workPos >= superframeIndex * Phase2BurstDibits
                    ? workPos - superframeIndex * Phase2BurstDibits
                    : 0;
                constexpr size_t kMaxContinuousSyncSlipDibits = 12;
                const auto hit = phase2SyncHitNear(hits, workPos, kMaxContinuousSyncSlipDibits);
                const uint8_t trafficSlot =
                    phase2TrafficSlotForSuperframeBurst(workingDibits, syntheticLockOffset, superframeIndex);
                auto& contSession = slotSessions[trafficSlot & 0x01u];
                const uint64_t contBurstStreamDibit =
                    phase2WorkingStreamStart + static_cast<uint64_t>(hit ? hit->dibitOffset : workPos);
                const bool duplicateRealtimeAcch =
                    annotateSessionCodewords &&
                    m_config.realtimeVoiceSearch &&
                    mask &&
                    recentlyDecodedAcchBurst(contBurstStreamDibit);
                auto contBurst = decodePhase2BurstAt(
                    workingDibits,
                    hit ? hit->dibitOffset : workPos,
                    hit ? hit->errors : -1,
                    true,
                    syntheticLockOffset,
                    superframeIndex,
                    4,
                    hit ? hit->errors : 0,
                    mask,
                    m_phase2MaskPhase,
                    std::max(1, m_phase2MaskPhaseScore),
                    &contSession,
                    &out.macPdus,
                    false,
                    true,
                    !duplicateRealtimeAcch);
                if (!duplicateRealtimeAcch && contBurst.valid && phase2BurstKindCarriesAcch(contBurst.kind)) {
                    rememberAcchBurst(contBurstStreamDibit);
                }
                if (!contBurst.valid) continue;
                contBurst.stickySuperframe = true;
                contBurst.superframeLock = true;
                contBurst.maskPhaseLock =
                    contBurst.xorMaskApplied && contBurst.xorMaskPhaseKnown;
                if (hit && hit->dibitOffset != workPos) {
                    contBurst.syncOffsetAdjusted = true;
                    contBurst.syncOffsetDibits =
                        phase2SignedSyncSlipDibits(hit->dibitOffset, workPos);
                }
                normalizePhase2BurstOffsets(contBurst);
                out.bursts.push_back(std::move(contBurst));
                ++continuousAdded;
            }
        }
    }

    // Sticky mask-phase health.  Do NOT re-hunt merely because a voice-only
    // window has no MAC (SACCH/FACCH are sparse during continuous speech) —
    // that thrashing re-scrambled clear audio every hop.  Count a starve only
    // when standards-level evidence says this phase is wrong:
    //   - I-ISCH mask-index misses dominate hits, or
    //   - ACCH/FACCH/SACCH present but zero CRC while hard voice is flowing.
    if (annotateSessionCodewords && mask && m_phase2MaskPhaseKnown) {
        size_t macCrcHits = 0;
        size_t acchLikeBursts = 0;
        size_t hardVoiceHits = 0;
        size_t ischHits = 0;
        size_t ischMisses = 0;
        bool essHit = false;
        for (const auto& b : out.bursts) {
            if (b.macCrcValid || b.macCrcLock) ++macCrcHits;
            if (b.essKnown) essHit = true;
            if (phase2BurstKindHasVoice(b.kind) && b.xorMaskApplied && !b.voiceCodewords.empty()) {
                ++hardVoiceHits;
            }
            if (b.kind == P25Phase2BurstKind::SacchScrambled ||
                b.kind == P25Phase2BurstKind::FacchScrambled ||
                b.kind == P25Phase2BurstKind::SacchClear ||
                b.kind == P25Phase2BurstKind::FacchClear) {
                ++acchLikeBursts;
            }
            if (b.isch.valid && !b.isch.sync && b.isch.location <= 2 &&
                b.superframeBurstIndexKnown &&
                b.isch.errors >= 0 && b.isch.errors < 3) {
                const size_t burstIndex = static_cast<size_t>(b.superframeBurstIndex % 12u);
                const size_t expectedMaskIndex =
                    static_cast<size_t>(b.isch.location) * 4u + (burstIndex % 4u);
                const size_t actualMaskIndex =
                    (burstIndex + static_cast<size_t>(m_phase2MaskPhase)) % 12u;
                if (actualMaskIndex == expectedMaskIndex) ++ischHits;
                else ++ischMisses;
            }
        }
        for (const auto& pdu : out.macPdus) {
            if (pdu.crcValid) ++macCrcHits;
        }
        if (macCrcHits > 0 || essHit || (ischHits > 0 && ischHits >= ischMisses)) {
            m_phase2MaskPhaseStarveWindows = 0;
        } else {
            const bool ischSaysWrong = ischMisses >= 2 && ischMisses > ischHits;
            const bool acchFailedWithVoice =
                hardVoiceHits > 0 &&
                acchLikeBursts >= 3 &&
                macCrcHits == 0 &&
                ischMisses >= 1 &&
                ischMisses > ischHits;
            if (ischSaysWrong || acchFailedWithVoice) {
                m_phase2MaskPhaseStarveWindows = static_cast<uint8_t>(
                    std::min<int>(255, static_cast<int>(m_phase2MaskPhaseStarveWindows) + 1));
            }
        }
    }

    // Uncovered sync hits: sticky / bootstrap / bare decode.  Cap work so a
    // noisy sync correlator cannot turn one commit into thousands of RS trials.
    size_t uncoveredHitsProcessed = 0;
    constexpr size_t kMaxUncoveredHitsRealtime = 8;
    constexpr size_t kMaxUncoveredHitsForensic = 24;
    const size_t uncoveredHitCap = m_config.realtimeVoiceSearch
        ? kMaxUncoveredHitsRealtime
        : kMaxUncoveredHitsForensic;
    // At most one deep sticky ACCH repair per annotate window (late-entry MAC).
    size_t stickyDeepAcchBudget = (annotateSessionCodewords &&
                                   m_phase2MaskPhaseKnown &&
                                   mask &&
                                   !m_phase2SessionMacCrcSeen) ? 1u : 0u;
    for (const auto& hit : hits) {
        const bool alreadyCovered = std::any_of(lockedWindows.begin(), lockedWindows.end(), [&](const auto& window) {
            return phase2OffsetInWindow(hit.dibitOffset, window.first, window.second);
        });
        if (alreadyCovered) continue;
        if (++uncoveredHitsProcessed > uncoveredHitCap) break;

        bool stickyDecoded = false;
        if (m_phase2SuperframeAnchorKnown && m_phase2MaskPhaseKnown && mask) {
            const bool anchorMaskMatches = m_phase2SuperframeAnchorMaskParams.valid &&
                m_phase2MaskParams.valid &&
                m_phase2SuperframeAnchorMaskParams.nac == m_phase2MaskParams.nac &&
                m_phase2SuperframeAnchorMaskParams.wacn == m_phase2MaskParams.wacn &&
                m_phase2SuperframeAnchorMaskParams.systemId == m_phase2MaskParams.systemId &&
                m_phase2SuperframeAnchorMaskPhase == m_phase2MaskPhase;
            constexpr uint64_t kPhase2StickyAnchorMaxAgeGenerations = 24;
            const uint64_t currentAnchorGeneration = m_phase2DecodeGeneration + (annotateSessionCodewords ? 1ull : 0ull);
            const bool anchorGenerationFresh = m_phase2SuperframeAnchorGeneration != 0 &&
                currentAnchorGeneration >= m_phase2SuperframeAnchorGeneration &&
                currentAnchorGeneration - m_phase2SuperframeAnchorGeneration <= kPhase2StickyAnchorMaxAgeGenerations;
            if (!anchorMaskMatches || !anchorGenerationFresh) {
                m_phase2SuperframeAnchorKnown = false;
            } else {
            const uint64_t hitStreamDibit = phase2WorkingStreamStart + static_cast<uint64_t>(hit.dibitOffset);
            constexpr uint64_t kPhase2SuperframeSpanDibits = static_cast<uint64_t>(P25LiveDecoder::Phase2BurstDibits) * 12ull;
            // sdrtrunk keeps Phase 2 framing state as a continuous dibit stream. Our live path is
            // invoked in bounded IQ windows, so retain a validated superframe/mask anchor long
            // enough to bridge quiet scheduler gaps and late-entry chunks on the same grant.
            constexpr uint64_t kPhase2StickyAnchorMaxAgeDibits = kPhase2SuperframeSpanDibits * 72ull;
            // Rolling GUI/CLI traffic windows are re-demodulated chunks, not a
            // single monotonically-clocked symbol stream. The stream overlap
            // estimator can be several dibits off even when the burst sync itself
            // is good. Keep the retained sdrtrunk-style superframe epoch alive
            // for bounded timing slip; decode at the actual sync hit while using
            // the nearest retained superframe index for the XOR segment.
            constexpr uint64_t kPhase2StickyAnchorMaxSlipDibits =
                static_cast<uint64_t>(P25LiveDecoder::Phase2BurstDibits / 3);
            if (hitStreamDibit >= m_phase2SuperframeAnchorDibit &&
                hitStreamDibit - m_phase2SuperframeAnchorDibit <= kPhase2StickyAnchorMaxAgeDibits) {
                const uint64_t delta = hitStreamDibit - m_phase2SuperframeAnchorDibit;
                const uint64_t mod = delta % kPhase2SuperframeSpanDibits;
                const uint64_t nearest = (mod + static_cast<uint64_t>(P25LiveDecoder::Phase2BurstDibits / 2)) /
                    static_cast<uint64_t>(P25LiveDecoder::Phase2BurstDibits);
                const size_t superframeIndex = static_cast<size_t>(nearest % 12ull);
                const uint64_t expectedMod = static_cast<uint64_t>(superframeIndex) *
                    static_cast<uint64_t>(P25LiveDecoder::Phase2BurstDibits);
                const uint64_t superframeBaseStreamDibit = hitStreamDibit - mod;
                const uint64_t expectedStreamDibit = superframeBaseStreamDibit + expectedMod;
                const size_t expectedWorkingDibit = expectedStreamDibit >= phase2WorkingStreamStart
                    ? static_cast<size_t>(expectedStreamDibit - phase2WorkingStreamStart)
                    : 0u;
                const uint64_t err = mod > expectedMod ? mod - expectedMod : expectedMod - mod;
                const uint64_t wrappedErr = std::min(err, kPhase2SuperframeSpanDibits - err);
                if (wrappedErr <= kPhase2StickyAnchorMaxSlipDibits) {
                    const size_t syntheticLockOffset = hit.dibitOffset >= superframeIndex * Phase2BurstDibits
                        ? hit.dibitOffset - superframeIndex * Phase2BurstDibits
                        : 0;
                    const uint8_t trafficSlot =
                        phase2TrafficSlotForSuperframeBurst(workingDibits, syntheticLockOffset, superframeIndex);
                    auto& stickySession = slotSessions[trafficSlot & 0x01u];
                    // Hot sticky path is shallow ACCH only.  Deep alt-layout /
                    // maxUnknown=2 was ~seconds per hit after 6000 baud lock and
                    // starved one-RTL follow.  Allow at most one deep repair when
                    // we still have no session MAC CRC.
                    const bool deepSticky = stickyDeepAcchBudget > 0;
                    if (deepSticky) --stickyDeepAcchBudget;
                    const bool duplicateRealtimeAcch =
                        annotateSessionCodewords &&
                        m_config.realtimeVoiceSearch &&
                        mask &&
                        !deepSticky &&
                        recentlyDecodedAcchBurst(hitStreamDibit);
                    auto stickyBurst = decodePhase2BurstAt(workingDibits, hit.dibitOffset, hit.errors,
                                                            true, syntheticLockOffset, superframeIndex,
                                                            4, static_cast<int>(wrappedErr),
                                                            mask, m_phase2MaskPhase, m_phase2MaskPhaseScore,
                                                            &stickySession, &out.macPdus,
                                                            deepSticky,
                                                            true,
                                                            !duplicateRealtimeAcch);
                    if (!duplicateRealtimeAcch && stickyBurst.valid && phase2BurstKindCarriesAcch(stickyBurst.kind)) {
                        rememberAcchBurst(hitStreamDibit);
                    }
                    if (stickyBurst.valid) {
                        stickyBurst.stickySuperframe = true;
                        stickyBurst.superframeLock = true;
                        stickyBurst.maskPhaseLock = stickyBurst.xorMaskApplied && stickyBurst.xorMaskPhaseKnown;
                        if (wrappedErr > 0) {
                            stickyBurst.syncOffsetAdjusted = true;
                            stickyBurst.syncOffsetDibits =
                                phase2SignedSyncSlipDibits(hit.dibitOffset, expectedWorkingDibit);
                        }
                        normalizePhase2BurstOffsets(stickyBurst);
                        out.bursts.push_back(std::move(stickyBurst));
                        stickyDecoded = true;
                    }
                }
            }
            }
        }
        if (stickyDecoded) continue;

        bool bootstrappedMasked = false;
        // 12 slots × 12 mask phases per uncovered sync hit is late-entry only.
        // Once a mask phase is known, or on CQPSK candidate probes (!annotate),
        // skip it — otherwise a correct 6000 baud eye turns every window into
        // thousands of RS trials (field hang: 20–30s/window).
        if (mask && annotateSessionCodewords && !m_phase2MaskPhaseKnown) {
            int bestScore = 0;
            P25Phase2Burst bestBurst;
            uint8_t bestPhase = 0;
            size_t bestSessionSlot = 0;
            std::optional<Phase2SessionState> bestProbeSession;
            std::vector<P25Phase2MacPdu> bestProbeMacPdus;
            size_t bootstrapTrials = 0;
            // Realtime: 2 slots × 12 phases.  Forensic keeps 3×12.  Once a phase
            // locks, subsequent windows use the sticky single-phase path.
            const size_t kMaxBootstrapTrials = m_config.realtimeVoiceSearch ? 24u : 36u;
            for (size_t slotIndex = 0; slotIndex < 12u && bootstrapTrials < kMaxBootstrapTrials; ++slotIndex) {
                const size_t back = slotIndex * P25LiveDecoder::Phase2BurstDibits;
                if (hit.dibitOffset < back) continue;
                const size_t lockOffset = hit.dibitOffset - back;
                const int syncErr = hit.errors >= 0 ? hit.errors : 0;
                for (uint8_t phase = 0; phase < 12u && bootstrapTrials < kMaxBootstrapTrials; ++phase) {
                    ++bootstrapTrials;
                    const size_t probeSessionSlot = static_cast<size_t>(
                        phase2TrafficSlotForSuperframeBurst(workingDibits, lockOffset, slotIndex) & 0x01u);
                    auto probeSession = slotSessions[probeSessionSlot];
                    std::vector<P25Phase2MacPdu> probeMacPdus;
                    auto trial = decodePhase2BurstAt(workingDibits, hit.dibitOffset, hit.errors,
                                                     true, lockOffset, slotIndex,
                                                     1, syncErr,
                                                     mask, phase, 0,
                                                     &probeSession, &probeMacPdus,
                                                     false);
                    const int score = scoreBootstrappedPhase2MaskedBurst(trial);
                    if (score > bestScore) {
                        bestScore = score;
                        bestPhase = phase;
                        bestSessionSlot = probeSessionSlot;
                        bestProbeSession = probeSession;
                        bestProbeMacPdus = std::move(probeMacPdus);
                        bestBurst = std::move(trial);
                    }
                    if (bestScore >= 200) break;
                }
                if (bestScore >= 200) break;
            }
            // Sticky-lock only with MAC CRC, ESS, or I-ISCH-aligned phase evidence.
            // VCW count alone must never lock a mask phase (scrambled audio forever).
            const bool bootstrapIschOk =
                bestBurst.isch.valid && !bestBurst.isch.sync &&
                bestBurst.isch.location <= 2 &&
                bestBurst.isch.errors >= 0 && bestBurst.isch.errors < 3;
            const bool bootstrapHardProof =
                bestBurst.macCrcValid || bestBurst.essKnown || bootstrapIschOk;
            if (bestScore >= 80 && bestBurst.valid && bestBurst.xorMaskApplied &&
                bootstrapHardProof &&
                (bestBurst.macCrcValid || bestBurst.essKnown ||
                 phase2BurstKindHasVoice(bestBurst.kind))) {
                m_phase2MaskPhaseKnown = true;
                m_phase2MaskPhase = bestPhase;
                m_phase2MaskPhaseScore = bestScore;
                m_phase2SuperframeAnchorKnown = true;
                m_phase2SuperframeAnchorDibit = phase2WorkingStreamStart + static_cast<uint64_t>(hit.dibitOffset);
                m_phase2SuperframeAnchorGeneration = m_phase2DecodeGeneration + 1;
                m_phase2SuperframeAnchorMaskParams = m_phase2MaskParams;
                m_phase2SuperframeAnchorMaskPhase = bestPhase;
                if (bestProbeSession) {
                    slotSessions[bestSessionSlot] = *bestProbeSession;
                }
                if (!bestProbeMacPdus.empty()) {
                    out.macPdus.insert(out.macPdus.end(),
                                       std::make_move_iterator(bestProbeMacPdus.begin()),
                                       std::make_move_iterator(bestProbeMacPdus.end()));
                }
                bestBurst.maskPhaseLock = bestBurst.xorMaskApplied && bestScore >= 80;
                normalizePhase2BurstOffsets(bestBurst);
                out.bursts.push_back(std::move(bestBurst));
                bootstrappedMasked = true;
            } else if (bestBurst.valid && bestBurst.xorMaskApplied &&
                       phase2BurstKindHasVoice(bestBurst.kind) &&
                       !bestBurst.voiceCodewords.empty()) {
                // Emit candidate hard-voice burst for this window without sticky-locking phase.
                normalizePhase2BurstOffsets(bestBurst);
                out.bursts.push_back(std::move(bestBurst));
                bootstrappedMasked = true;
            }
        }
        if (bootstrappedMasked) continue;

        auto burst = decodePhase2BurstAt(workingDibits, hit.dibitOffset, hit.errors,
                                         false, 0, 0,
                                         0, 0,
                                         nullptr, 0, 0,
                                         &session, &out.macPdus,
                                         false);
        if (burst.valid) {
            if (annotateSessionCodewords &&
                m_config.realtimeVoiceSearch &&
                mask &&
                phase2BurstKindHasVoice(burst.kind)) {
                burst.voiceCodewords.clear();
            }
            normalizePhase2BurstOffsets(burst);
            out.bursts.push_back(std::move(burst));
        }
    }
    if (annotateSessionCodewords) {
        retainPhase2SlotSessions();
        annotatePhase2SessionCodewords(out, dibits);
    } else {
        Phase2SessionState* retainedSession = &slotSessions[0];
        for (auto& candidate : slotSessions) {
            if (candidate.ess.known && candidate.essTrusted &&
                (!retainedSession->ess.known || candidate.ess.fecValidated)) {
                retainedSession = &candidate;
            }
        }
        out.ess = retainedSession->ess;
    }
    return out;
}

P25LiveDecodeResult P25LiveDecoder::processHardBits(const std::vector<uint8_t>& inputBits)
{
    P25DecoderTraceScope trace("P25LiveDecoder::processHardBits");
    P25LiveDecodeResult result;
    size_t prefixBits = 0;
    result.bits = buildPhase1BitStreamWithTail(inputBits, prefixBits);
    result.stats.bits = inputBits.size();
    result.stats.symbolRate = m_config.symbolRate;

    if (result.bits.size() < FrameSyncBits) {
        storePhase1BitTail(result.bits);
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
        if (!nid.fecValidated) {
            result.nids.push_back(nid);
            result.warnings.push_back("P25 frame sync found but NID BCH validation/correction failed.");
            continue;
        }

        if (nid.duid == P25DataUnitId::TSDU) {
            const size_t blocksBefore = result.rawTsbkBlocks.size();
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
            if (result.rawTsbkBlocks.size() > blocksBefore) {
                const bool anyValid = std::any_of(result.rawTsbkBlocks.begin() + static_cast<std::ptrdiff_t>(blocksBefore),
                                                  result.rawTsbkBlocks.end(), [](const P25TsbkBlock& b) {
                    return b.fecDecoded && b.crcValid;
                });
                if (anyValid) {
                    nid.downstreamValidated = true;
                    nid.downstreamScore += 4;
                } else {
                    result.warnings.push_back("TSDU block candidates were trellis-decoded but failed CRC validation.");
                }
            }
        } else if (nid.duid == P25DataUnitId::PDU) {
            auto pdu = decodeP25PduSequence(split, payloadDibitOffset, m_config.maxRawTsbkBlocksPerFrame);
            if (pdu.headerFecDecoded) {
                if (pdu.headerCrcValid) {
                    nid.downstreamValidated = true;
                    nid.downstreamScore += pdu.format == 23 ? 4 : 3;
                } else {
                    result.warnings.push_back("P25 PDU header trellis-decoded but failed CRC validation.");
                }
                result.phase1Pdus.push_back(std::move(pdu));
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
            } else {
                nid.downstreamValidated = true;
                nid.downstreamScore += static_cast<int>(std::min<size_t>(decodedFrames, 3));
            }
        }
        result.nids.push_back(nid);
    }

    std::stable_sort(result.nids.begin(), result.nids.end(), [](const P25Nid& a, const P25Nid& b) {
        if (a.downstreamValidated != b.downstreamValidated) return a.downstreamValidated;
        if (a.downstreamScore != b.downstreamScore) return a.downstreamScore > b.downstreamScore;
        if (a.fecValidated != b.fecValidated) return a.fecValidated;
        if (a.correctedBitErrors != b.correctedBitErrors) return a.correctedBitErrors < b.correctedBitErrors;
        return a.bitOffset < b.bitOffset;
    });

    result.stats.voiceBackendAvailable = compiledVoiceBackendAvailable();
    result.stats.phase1PduHeaders = result.phase1Pdus.size();
    result.stats.phase1PduCrcValid = static_cast<size_t>(std::count_if(result.phase1Pdus.begin(), result.phase1Pdus.end(), [](const P25Phase1PduMessage& pdu) {
        return pdu.headerFecDecoded && pdu.headerCrcValid;
    }));
    result.stats.phase1AmbtcPdus = static_cast<size_t>(std::count_if(result.phase1Pdus.begin(), result.phase1Pdus.end(), [](const P25Phase1PduMessage& pdu) {
        return pdu.headerFecDecoded && pdu.headerCrcValid && pdu.format == 23;
    }));
    storePhase1BitTail(result.bits);
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
    result.pcm = normalizedMbelibPcm(audio, std::size(audio));
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
    result.pcm = normalizedMbelibPcm(audio, std::size(audio));
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

    std::array<uint8_t, 96> safeAmbe = ambe96;
    for (auto& b : safeAmbe) b &= 1u; // targeted force 0/1 for strong VCW/mask cases (MAC CRC often 0 on traffic)
    if (!validateUnpackedAmbe96(safeAmbe)) {
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
            ambeFrame[row][col] = static_cast<char>(safeAmbe[row * 24 + col] & 0x01u);
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
    result.pcm = normalizedMbelibPcm(audio, std::size(audio));
    result.message = errText[0] ? errText : "decoded";
    return result;
#else
    (void)ambe96;
    result.status = P25VoiceDecodeStatus::BackendUnavailable;
    result.message = "Clear P25 Phase 2 AMBE voice requires an mbelib-compatible backend built with SDR_TOWN_ENABLE_MBELIB=ON.";
    return result;
#endif
}
