#include "dsp/P25FilterCache.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace p25dsp {

namespace {
constexpr double kPi = std::numbers::pi;

uint32_t hzToMilli(double hz) noexcept
{
    return static_cast<uint32_t>(std::max(0.0, hz * 1000.0 + 0.5));
}

} // namespace

bool P25FilterKey::operator==(const P25FilterKey& other) const noexcept
{
    return inputRateMilliHz == other.inputRateMilliHz &&
           outputRateMilliHz == other.outputRateMilliHz &&
           cutoffMilliHz == other.cutoffMilliHz &&
           transitionMilliHz == other.transitionMilliHz &&
           maxTaps == other.maxTaps &&
           kind == other.kind;
}

size_t P25FilterKeyHash::operator()(const P25FilterKey& key) const noexcept
{
    size_t h = key.inputRateMilliHz;
    h = h * 1315423911u + key.outputRateMilliHz;
    h = h * 1315423911u + key.cutoffMilliHz;
    h = h * 1315423911u + key.transitionMilliHz;
    h = h * 1315423911u + key.maxTaps;
    h = h * 1315423911u + key.kind;
    return h;
}

std::vector<double> P25FilterCache::designLowpassTaps(double sampleRate,
                                                      double cutoffHz,
                                                      double transitionHz,
                                                      int maxTaps)
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

std::vector<double> P25FilterCache::designRrcTaps(double sampleRate,
                                                  double symbolRate,
                                                  double alpha,
                                                  int maxTaps)
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

const std::vector<double>& P25FilterCache::lowpassTaps(double sampleRate,
                                                       double cutoffHz,
                                                       double transitionHz,
                                                       int maxTaps)
{
    P25FilterKey key{
        hzToMilli(sampleRate),
        hzToMilli(sampleRate),
        hzToMilli(cutoffHz),
        hzToMilli(transitionHz),
        static_cast<uint16_t>(maxTaps),
        0
    };
    auto it = m_cache.find(key);
    if (it != m_cache.end()) return it->second;
    ++m_designCalls;
    auto inserted = m_cache.emplace(key, designLowpassTaps(sampleRate, cutoffHz, transitionHz, maxTaps));
    return inserted.first->second;
}

const std::vector<double>& P25FilterCache::rrcTaps(double sampleRate,
                                                   double symbolRate,
                                                   double alpha,
                                                   int maxTaps)
{
    P25FilterKey key{
        hzToMilli(sampleRate),
        hzToMilli(symbolRate),
        hzToMilli(symbolRate * alpha),
        hzToMilli(symbolRate),
        static_cast<uint16_t>(maxTaps),
        1
    };
    auto it = m_cache.find(key);
    if (it != m_cache.end()) return it->second;
    ++m_designCalls;
    auto inserted = m_cache.emplace(key, designRrcTaps(sampleRate, symbolRate, alpha, maxTaps));
    return inserted.first->second;
}

} // namespace p25dsp
