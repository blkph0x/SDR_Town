#include "AptImageDecoder.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <numbers>
#include <utility>

namespace {
constexpr double kTwoPi = 2.0 * std::numbers::pi_v<double>;
}

const std::array<float, AptImageDecoder::kSyncWords>& AptImageDecoder::syncATemplate() {
    static const std::array<float, kSyncWords> pattern = [] {
        std::array<float, kSyncWords> result{};
        // Sync A: four low words, then seven 1040 Hz square-wave cycles.
        // At 4160 words/s, each cycle is two high + two low words.
        for (int pulse = 0; pulse < 7; ++pulse) {
            const int begin = 4 + pulse * 4;
            result[static_cast<size_t>(begin)] = 1.0f;
            result[static_cast<size_t>(begin + 1)] = 1.0f;
        }
        return result;
    }();
    return pattern;
}

const std::array<float, AptImageDecoder::kSyncWords>& AptImageDecoder::syncBTemplate() {
    static const std::array<float, kSyncWords> pattern = [] {
        std::array<float, kSyncWords> result{};
        // Sync B: four low words, then seven 832 pps pulses. At 4160
        // words/s each pulse occupies two high + three low words.
        for (int pulse = 0; pulse < 7; ++pulse) {
            const int begin = 4 + pulse * 5;
            result[static_cast<size_t>(begin)] = 1.0f;
            result[static_cast<size_t>(begin + 1)] = 1.0f;
        }
        return result;
    }();
    return pattern;
}

void AptImageDecoder::resetSignalState(bool clearLines) {
    sampleRateHz_ = 0.0;
    carrierPhase_ = 0.0;
    wordPhase_ = 0.0;
    inPhaseLp_ = 0.0f;
    quadratureLp_ = 0.0f;
    inPhaseLp2_ = 0.0f;
    quadratureLp2_ = 0.0f;
    wordEnvelopeSum_ = 0.0;
    wordEnvelopeCount_ = 0;
    syncSearchRaw_.clear();
    lineRaw_.clear();
    collectingLine_ = false;
    lastSyncScore_ = 0.0;
    lastSyncBScore_ = 0.0;
    if (clearLines) {
        lines_.clear();
        syncCount_ = 0;
        lastPath_.clear();
    }
}

void AptImageDecoder::reset() {
    resetSignalState(true);
}

double AptImageDecoder::correlation(
    const std::vector<float>& samples,
    size_t offset,
    const std::array<float, kSyncWords>& pattern)
{
    if (offset + pattern.size() > samples.size()) return 0.0;

    double sampleMean = 0.0;
    double patternMean = 0.0;
    for (size_t i = 0; i < pattern.size(); ++i) {
        sampleMean += static_cast<double>(samples[offset + i]);
        patternMean += static_cast<double>(pattern[i]);
    }
    sampleMean /= static_cast<double>(pattern.size());
    patternMean /= static_cast<double>(pattern.size());

    double numerator = 0.0;
    double samplePower = 0.0;
    double patternPower = 0.0;
    for (size_t i = 0; i < pattern.size(); ++i) {
        const double sample = static_cast<double>(samples[offset + i]) - sampleMean;
        const double expected = static_cast<double>(pattern[i]) - patternMean;
        numerator += sample * expected;
        samplePower += sample * sample;
        patternPower += expected * expected;
    }
    if (samplePower <= 1e-12 || patternPower <= 1e-12) return 0.0;
    return numerator / std::sqrt(samplePower * patternPower);
}

std::vector<uint8_t> AptImageDecoder::normalizeLine(const std::vector<float>& envelopes) {
    if (envelopes.empty()) return {};

    std::vector<float> sorted = envelopes;
    std::sort(sorted.begin(), sorted.end());
    const size_t lowIndex = std::min(sorted.size() - 1, sorted.size() / 100);
    const size_t highIndex = std::min(sorted.size() - 1, (sorted.size() * 99) / 100);
    const float low = sorted[lowIndex];
    const float high = sorted[highIndex];
    const float span = std::max(1e-6f, high - low);

    std::vector<uint8_t> pixels;
    pixels.reserve(envelopes.size());
    for (float envelope : envelopes) {
        if (!std::isfinite(envelope)) envelope = low;
        const float normalized = std::clamp((envelope - low) / span, 0.0f, 1.0f);
        pixels.push_back(static_cast<uint8_t>(std::lround(normalized * 255.0f)));
    }
    return pixels;
}

bool AptImageDecoder::pushWord(float envelope) {
    if (!std::isfinite(envelope) || envelope < 0.0f) envelope = 0.0f;

    if (collectingLine_) {
        lineRaw_.push_back(envelope);
        if (lineRaw_.size() < static_cast<size_t>(kLineWords)) return false;

        lastSyncBScore_ = correlation(lineRaw_, kHalfLineWords, syncBTemplate());
        lines_.push_back(normalizeLine(lineRaw_));
        if (lines_.size() > 1200) lines_.erase(lines_.begin());
        lineRaw_.clear();
        syncSearchRaw_.clear();
        collectingLine_ = false;
        return true;
    }

    syncSearchRaw_.push_back(envelope);
    if (syncSearchRaw_.size() > static_cast<size_t>(kSyncWords))
        syncSearchRaw_.erase(syncSearchRaw_.begin());
    if (syncSearchRaw_.size() < static_cast<size_t>(kSyncWords)) return false;

    lastSyncScore_ = correlation(syncSearchRaw_, 0, syncATemplate());
    if (lastSyncScore_ < 0.72) return false;

    lineRaw_ = syncSearchRaw_;
    syncSearchRaw_.clear();
    collectingLine_ = true;
    ++syncCount_;
    return false;
}

bool AptImageDecoder::processAudio(const float* samples, size_t count, double sampleRateHz) {
    if (!samples || count == 0 || !std::isfinite(sampleRateHz) || sampleRateHz < 8000.0)
        return false;

    if (sampleRateHz_ <= 0.0 || std::abs(sampleRateHz - sampleRateHz_) > 0.5) {
        const bool preserveCompletedImage = !lines_.empty();
        auto completed = preserveCompletedImage
            ? std::move(lines_)
            : std::vector<std::vector<uint8_t>>{};
        const uint64_t priorSyncCount = syncCount_;
        resetSignalState(false);
        sampleRateHz_ = sampleRateHz;
        if (preserveCompletedImage) lines_ = std::move(completed);
        syncCount_ = priorSyncCount;
    }

    const double phaseIncrement = kTwoPi * kSubcarrierHz / sampleRateHz_;
    const double wordIncrement = kWordRateHz / sampleRateHz_;
    // Two cascaded one-pole sections reject the 4800 Hz mixer image while
    // retaining the approximately 0-2080 Hz APT video baseband.
    const float alpha = static_cast<float>(
        1.0 - std::exp(-kTwoPi * 2100.0 / sampleRateHz_));

    bool newLine = false;
    for (size_t i = 0; i < count; ++i) {
        const float sample = std::isfinite(samples[i]) ? samples[i] : 0.0f;
        const float mixedI = static_cast<float>(sample * std::cos(carrierPhase_));
        const float mixedQ = static_cast<float>(-sample * std::sin(carrierPhase_));

        inPhaseLp_ += alpha * (mixedI - inPhaseLp_);
        quadratureLp_ += alpha * (mixedQ - quadratureLp_);
        inPhaseLp2_ += alpha * (inPhaseLp_ - inPhaseLp2_);
        quadratureLp2_ += alpha * (quadratureLp_ - quadratureLp2_);

        const float envelope = 2.0f * std::sqrt(
            inPhaseLp2_ * inPhaseLp2_ + quadratureLp2_ * quadratureLp2_);
        wordEnvelopeSum_ += envelope;
        ++wordEnvelopeCount_;
        wordPhase_ += wordIncrement;
        if (wordPhase_ >= 1.0) {
            const float averagedEnvelope = wordEnvelopeCount_ > 0
                ? static_cast<float>(wordEnvelopeSum_ /
                                     static_cast<double>(wordEnvelopeCount_))
                : envelope;
            if (pushWord(averagedEnvelope)) newLine = true;
            wordEnvelopeSum_ = 0.0;
            wordEnvelopeCount_ = 0;
            wordPhase_ -= 1.0;
        }

        carrierPhase_ += phaseIncrement;
        if (carrierPhase_ >= kTwoPi) carrierPhase_ -= kTwoPi;
    }
    return newLine;
}

bool AptImageDecoder::writePgm(const std::string& path) {
    if (lines_.empty() || path.empty()) return false;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << "P5\n" << kLineWords << " " << lines_.size() << "\n255\n";
    for (const auto& row : lines_) {
        if (row.size() != static_cast<size_t>(kLineWords)) return false;
        out.write(reinterpret_cast<const char*>(row.data()),
                  static_cast<std::streamsize>(row.size()));
    }
    if (!out) return false;
    lastPath_ = path;
    return true;
}
