#pragma once
#include <array>
#include <cstdint>
#include <mutex>
#include <span>
#include <string>
#include <vector>

struct CtcssSnapshot {
    double frequencyHz = 0, targetHz = 0, sampleRate = 0, purity = 0;
    uint64_t samples = 0, windows = 0, confirmedWindows = 0, resets = 0;
    int64_t updatedMs = 0;
    std::string status = "Inactive";
};

// One DSP owner calls process/reset; GUI only uses snapshot(). No audio gate.
class CtcssDecoder {
public:
    static std::span<const double> tones();
    bool process(std::span<const float> samples, double rate, double targetHz,
                 uint64_t epoch, uint64_t firstSample, bool discontinuity);
    void reset();
    CtcssSnapshot snapshot() const;
private:
    void evaluate();
    void publish();
    mutable std::mutex mutex_;
    CtcssSnapshot state_, published_;
    std::vector<double> window_;
    std::array<double,4> lowpass_{};
    double dc_ = 0, alpha_ = 0, dcAlpha_ = 0, candidate_ = 0;
    unsigned matches_ = 0;
    uint64_t epoch_ = 0, nextSample_ = 0;
    size_t length_ = 0;
};

CtcssSnapshot decodeCtcssFile(const std::string& path, size_t chunkSize = 4096);
