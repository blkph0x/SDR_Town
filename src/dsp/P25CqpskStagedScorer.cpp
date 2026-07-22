#include "dsp/P25CqpskStagedScorer.h"

#include "dsp/P25DspTypes.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace p25dsp {

namespace {
constexpr double kPi = std::numbers::pi;

int popcount64(uint64_t v) noexcept
{
#if defined(_MSC_VER)
    return static_cast<int>(__popcnt64(v));
#elif defined(__GNUC__) || defined(__clang__)
    return __builtin_popcountll(v);
#else
    int count = 0;
    while (v) {
        count += static_cast<int>(v & 1u);
        v >>= 1;
    }
    return count;
#endif
}

double wrapPhase(double radians) noexcept
{
    while (radians > kPi) radians -= 2.0 * kPi;
    while (radians < -kPi) radians += 2.0 * kPi;
    return radians;
}

} // namespace

P25Phase2SyncPreview scorePhase2SyncPreview(std::span<const int> dibits, bool synchronized) noexcept
{
    P25Phase2SyncPreview preview;
    if (dibits.empty()) return preview;

    constexpr uint64_t kMask = (1ull << (p25dsp::kPhase2FrameSyncDibits * 2)) - 1ull;
    const uint64_t syncWord = p25dsp::kPhase2FrameSyncWord;
    const uint64_t invertedSync = syncWord ^ kMask;
    const int threshold = synchronized ? kSyncThresholdSynchronized : kSyncThresholdUnsynchronized;

    const size_t scanLimit = std::min(dibits.size(), size_t{240});
    uint64_t reg = 0;
    for (size_t i = 0; i < scanLimit; ++i) {
        const uint64_t dib = static_cast<uint64_t>(dibits[i] & 0x03);
        reg = ((reg << 2) | dib) & kMask;
        const int normalErrors = popcount64((reg ^ syncWord) & kMask);
        const int invertedErrors = popcount64((reg ^ invertedSync) & kMask);
        if (normalErrors < preview.bestSyncErrors) {
            preview.bestSyncErrors = normalErrors;
            preview.bestInvertedErrors = invertedErrors;
            preview.bestOffsetDibits = i >= (p25dsp::kPhase2FrameSyncDibits - 1)
                ? i - (p25dsp::kPhase2FrameSyncDibits - 1)
                : 0;
            preview.inverted = false;
        }
        if (invertedErrors < preview.bestInvertedErrors) {
            preview.bestInvertedErrors = invertedErrors;
            if (invertedErrors <= normalErrors) {
                preview.bestSyncErrors = invertedErrors;
                preview.inverted = true;
            }
        }
    }

    preview.plausible = preview.bestSyncErrors <= threshold ||
                        preview.bestInvertedErrors <= threshold;
    return preview;
}

uint8_t classifyDqpskQuadrant(float di, float dq) noexcept
{
    if (dq > 0.0f) {
        return di > 0.0f ? 0u : 1u;
    }
    return di > 0.0f ? 3u : 2u;
}

std::vector<uint8_t> buildDifferentialQuadrants(std::span<const std::complex<double>> symbols,
                                                bool differential,
                                                bool conjugate,
                                                double rotationRad) noexcept
{
    std::vector<uint8_t> quadrants;
    if (symbols.empty()) return quadrants;
    quadrants.reserve(symbols.size());
    const size_t start = differential ? 1u : 0u;
    const double rotCos = std::cos(rotationRad);
    const double rotSin = std::sin(rotationRad);
    for (size_t i = start; i < symbols.size(); ++i) {
        std::complex<double> z = differential ? symbols[i] * std::conj(symbols[i - 1]) : symbols[i];
        if (conjugate) z = std::conj(z);
        const double rotatedI = z.real() * rotCos - z.imag() * rotSin;
        const double rotatedQ = z.real() * rotSin + z.imag() * rotCos;
        quadrants.push_back(classifyDqpskQuadrant(static_cast<float>(rotatedI),
                                                  static_cast<float>(rotatedQ)));
    }
    return quadrants;
}

std::vector<int> mapQuadrantsToDibits(std::span<const uint8_t> quadrants,
                                      const P25CqpskMappingParams& mapping) noexcept
{
    std::vector<int> dibits;
    dibits.reserve(quadrants.size());
    for (uint8_t quadrant : quadrants) {
        const size_t idx = static_cast<size_t>(quadrant & 0x03u);
        dibits.push_back(mapping.permutation[idx] & 0x03);
    }
    return dibits;
}

bool passesStagedCqpskGate(const P25Phase2SyncPreview& preview,
                           P25DemodState state,
                           bool fromLock) noexcept
{
    if (fromLock) return true;
    if (state == P25DemodState::TrackingHard) return true;
    if (state == P25DemodState::Cold ||
        state == P25DemodState::AcquiringTiming ||
        state == P25DemodState::AcquiringMapping) {
        return preview.plausible;
    }
    // TrackingSoft / Recovering: require tighter sync like SDRTrunk synchronized threshold.
    return preview.bestSyncErrors <= kSyncThresholdSynchronized ||
           preview.bestInvertedErrors <= kSyncThresholdSynchronized;
}

} // namespace p25dsp
