#pragma once

#include <cstdint>
#include <string>
#include <vector>

// NOAA APT grayscale line builder from AM demodulated audio (~2080 Hz line rate).
class AptImageDecoder {
public:
    void reset();
    // Feed mono PCM. Returns true when a new line was completed.
    bool processAudio(const float* samples, size_t count, double sampleRateHz);

    int width() const { return width_; }
    int height() const { return static_cast<int>(lines_.size()); }
    const std::vector<std::vector<uint8_t>>& lines() const { return lines_; }

    // Write progressive PNG-like raw PGM (portable, no Qt dependency in core).
    bool writePgm(const std::string& path) const;
    std::string lastPath() const { return lastPath_; }

private:
    static constexpr int kWidth = 2080;
    int width_ = kWidth;
    double sampleRate_ = 11025.0;
    std::vector<float> envBuf_;
    std::vector<uint8_t> lineAcc_;
    int linePos_ = 0;
    std::vector<std::vector<uint8_t>> lines_;
    std::string lastPath_;
    float envLp_ = 0.0f;
};
