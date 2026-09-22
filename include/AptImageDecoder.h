#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

// NOAA APT decoder for the 2400 Hz AM subcarrier recovered from an FM receiver.
// The decoder resamples the detected envelope to the specified 4160 word/s
// stream, acquires the 39-word Sync A pattern, and assembles 2080-word lines.
class AptImageDecoder {
public:
    void reset();
    // Feed mono discriminator/audio PCM. Returns true when a synchronized line completed.
    bool processAudio(const float* samples, size_t count, double sampleRateHz);

    int width() const { return kLineWords; }
    int height() const { return static_cast<int>(lines_.size()); }
    const std::vector<std::vector<uint8_t>>& lines() const { return lines_; }
    bool synchronized() const { return collectingLine_; }
    uint64_t syncCount() const { return syncCount_; }
    double lastSyncScore() const { return lastSyncScore_; }
    double lastSyncBScore() const { return lastSyncBScore_; }

    // Write the progressive two-channel APT line raster as portable PGM.
    bool writePgm(const std::string& path);
    std::string lastPath() const { return lastPath_; }

private:
    static constexpr int kLineWords = 2080;
    static constexpr int kHalfLineWords = 1040;
    static constexpr int kSyncWords = 39;
    static constexpr double kWordRateHz = 4160.0;
    static constexpr double kSubcarrierHz = 2400.0;

    static const std::array<float, kSyncWords>& syncATemplate();
    static const std::array<float, kSyncWords>& syncBTemplate();
    static double correlation(const std::vector<uint8_t>& samples, size_t offset,
                              const std::array<float, kSyncWords>& pattern);

    uint8_t normalizeEnvelope(float envelope);
    bool pushWord(float envelope);
    void resetSignalState(bool clearLines);

    double sampleRateHz_ = 0.0;
    double carrierPhase_ = 0.0;
    double wordPhase_ = 0.0;
    float inPhaseLp_ = 0.0f;
    float quadratureLp_ = 0.0f;
    float inPhaseLp2_ = 0.0f;
    float quadratureLp2_ = 0.0f;
    double wordEnvelopeSum_ = 0.0;
    uint32_t wordEnvelopeCount_ = 0;

    bool levelInitialized_ = false;
    float lowLevel_ = 0.0f;
    float highLevel_ = 1.0f;

    std::vector<uint8_t> syncSearch_;
    std::vector<uint8_t> lineAcc_;
    bool collectingLine_ = false;
    std::vector<std::vector<uint8_t>> lines_;
    uint64_t syncCount_ = 0;
    double lastSyncScore_ = 0.0;
    double lastSyncBScore_ = 0.0;
    std::string lastPath_;
};
