#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <vector>

// DEC-0226: activity is RF evidence, not proof of speech or a digital decoder.
struct SatcomWatchChannel {
    double frequencyHz = 0;
    std::string label;
    int priority = 1; // 0 Low, 1 Medium, 2 High
    bool enabled = true;
};

struct SatcomWatchStatus {
    SatcomWatchChannel channel;
    bool inBand = false;
    bool active = false;
    bool listening = false;
    double signalDb = -160;
    double noiseDb = -160;
    uint64_t hits = 0;
};

namespace SatcomScanPolicy {
constexpr size_t maxChannels = 128;
constexpr size_t maxDecoders = 8;

inline bool validChannel(const SatcomWatchChannel& c) {
    return std::isfinite(c.frequencyHz) && c.frequencyHz >= 100e3 &&
        c.frequencyHz <= 6000e6 && c.priority >= 0 && c.priority <= 2 &&
        c.label.size() <= 80;
}

// Tile overlap deliberately covers the DC exclusion at an adjacent LO position.
inline double tileStride(double rate, double bandwidth) {
    if (!std::isfinite(rate) || !std::isfinite(bandwidth) || rate <= 0 ||
        bandwidth <= 0 || rate < 9 * bandwidth) return 0;
    return rate * 0.4 - bandwidth * 0.5;
}

inline bool observable(double frequency, double center, double rate, double bandwidth,
                       size_t bins = 8192) {
    const double half = tileStride(rate, bandwidth);
    const double offset = std::abs(frequency - center);
    return half > 0 && std::isfinite(offset) && offset <= half &&
        offset >= std::max(bandwidth, 3 * rate / bins);
}

inline double nextTile(double center, double low, double high, double rate, double bw) {
    const double stride = tileStride(rate, bw);
    if (!stride || !std::isfinite(center) || low > high) return 0;
    const double first = low + stride * 0.5;
    // Last tile extends past high so the final DC region is observed off-centre.
    // First centre has no predecessor: a 2/3-stride second tile covers its
    // lower DC guard. rate>=9*BW guarantees stride>=3.1*BW, so these two DC
    // guards cannot overlap; interior centres are covered by two neighbours.
    const double next = center + (std::abs(center - first) < 1 ? stride * (2.0 / 3) : stride);
    return next - stride > high ? first : next;
}

// Independent fresh-IQ periodogram. This does not change the speaker IQ or the
// shared display FFT. Radix-2 ordering matches DeviceManager's existing FFT.
inline std::vector<float> spectrum(std::span<const std::complex<float>> iq) {
    constexpr size_t n = 8192;
    if (iq.size() < n) return {};
    iq = iq.last(n);
    std::complex<double> mean{};
    for (auto s : iq) {
        if (!std::isfinite(s.real()) || !std::isfinite(s.imag())) return {};
        mean += std::complex<double>(s);
    }
    mean /= n;
    std::vector<std::complex<double>> a(n);
    double windowSum = 0;
    constexpr double pi = 3.14159265358979323846;
    for (size_t i = 0; i < n; ++i) {
        const double w = 0.5 - 0.5 * std::cos(2 * pi * i / (n - 1));
        a[i] = (std::complex<double>(iq[i]) - mean) * w;
        windowSum += w;
    }
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const auto step = std::polar(1.0, -2 * pi / len);
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w(1, 0);
            for (size_t j = 0; j < len / 2; ++j) {
                const auto u = a[i + j], v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= step;
            }
        }
    }
    std::vector<float> result(n);
    for (size_t i = 0; i < n; ++i)
        result[i] = float(10 * std::log10(std::max(1e-16,
            std::norm(a[(i + n / 2) % n]) / (windowSum * windowSum))));
    return result;
}

struct Measurement { bool valid = false; double signalDb = -160, noiseDb = -160; };

inline Measurement measure(std::span<const float> db, double center, double rate,
                           double frequency, double bw) {
    Measurement m;
    if (db.empty() || !observable(frequency, center, rate, bw, db.size())) return m;
    const double bin = rate / db.size();
    const auto index = [&](double f) {
        return std::clamp<int>(int(std::floor((f - center) / bin + db.size() * 0.5)),
                              0, int(db.size()) - 1);
    };
    const int lo = index(frequency - 4 * bw), hi = index(frequency + 4 * bw);
    std::vector<float> noise;
    double channelPower = 0;
    size_t channelBins = 0;
    for (int i = lo; i <= hi; ++i) {
        if (!std::isfinite(db[i])) continue;
        const double f = center + (i - double(db.size()) * 0.5) * bin;
        const double distance = std::abs(f - frequency);
        if (distance <= bw * 0.5) {
            // ISS-0125: maxima of noise bins can hold squelch indefinitely.
            // Integrate in linear power, then normalize by channel-bin count.
            channelPower += std::pow(10.0, double(db[i]) / 10);
            ++channelBins;
        }
        else if (distance >= bw && std::abs(f - center) >= bw &&
                 std::abs(f - center) < rate * 0.4) noise.push_back(db[i]);
    }
    if (noise.size() < 8 || !channelBins || !std::isfinite(channelPower) || channelPower <= 0) return m;
    m.signalDb = 10 * std::log10(channelPower / channelBins);
    if (m.signalDb <= -160) return m;
    auto mid = noise.begin() + noise.size() / 2;
    std::nth_element(noise.begin(), mid, noise.end());
    m.noiseDb = *mid;
    m.valid = true;
    return m;
}

inline std::vector<double> discover(std::span<const float> db, double center, double rate,
    double low, double high, double step, double bw, bool automatic, double margin, double threshold) {
    std::vector<std::pair<double, double>> candidates;
    if (db.empty() || !std::isfinite(low) || !std::isfinite(high) || low > high ||
        !std::isfinite(step) || step < 100 || !tileStride(rate, bw)) return {};
    const double first = low + std::max(0.0, std::ceil((center - rate * .4 - low) / step)) * step;
    for (double f = first; f <= std::min(high, center + rate * .4); f += step) {
        const auto m = measure(db, center, rate, f, bw);
        if (m.valid && m.signalDb >= (automatic ? m.noiseDb + margin : threshold))
            candidates.emplace_back(m.signalDb, f);
    }
    std::stable_sort(candidates.begin(), candidates.end(), [](auto a, auto b) { return a.first > b.first; });
    std::vector<double> result;
    for (const auto& [power, f] : candidates) {
        if (std::none_of(result.begin(), result.end(), [&](double other) { return std::abs(other - f) < bw; }))
            result.push_back(f);
        if (result.size() == maxDecoders) break;
    }
    return result;
}

struct Gate {
    bool active = false;
    double quietSeconds = 0;
    // Advance only by fresh input samples, never GUI polls or duplicate windows.
    bool update(Measurement m, double seconds, bool automatic, double margin,
                double threshold, double holdSeconds) {
        const double open = automatic ? m.noiseDb + margin : threshold;
        const double close = open - 4; // DEC-0226 hysteresis.
        const bool present = m.valid && m.signalDb >= (active ? close : open);
        if (present) { active = true; quietSeconds = 0; }
        else {
            quietSeconds += std::max(0.0, seconds);
            if (!m.valid || quietSeconds >= holdSeconds) active = false;
        }
        return present; // hang retains scheduling, not noise in the speaker.
    }
};

inline double selectForeground(const std::vector<SatcomWatchStatus>& statuses, double incumbent) {
    const SatcomWatchStatus* best = nullptr;
    for (const auto& s : statuses) {
        if (!s.active || !s.inBand || !s.channel.enabled) continue;
        if (!best || s.channel.priority > best->channel.priority ||
            (s.channel.priority == best->channel.priority && s.channel.frequencyHz == incumbent))
            best = &s;
    }
    return best ? best->channel.frequencyHz : 0;
}
}
