#include "AptImageDecoder.h"

#include <algorithm>
#include <cmath>
#include <fstream>

void AptImageDecoder::reset() {
    envBuf_.clear();
    lineAcc_.assign(static_cast<size_t>(width_), 0);
    linePos_ = 0;
    lines_.clear();
    envLp_ = 0.0f;
    lastPath_.clear();
}

bool AptImageDecoder::processAudio(const float* samples, size_t count, double sampleRateHz) {
    if (!samples || count == 0) return false;
    if (lineAcc_.size() != static_cast<size_t>(width_))
        lineAcc_.assign(static_cast<size_t>(width_), 0);
    sampleRate_ = sampleRateHz > 0 ? sampleRateHz : 11025.0;
    // Target ~4160 samples/line at 11025 Hz ≈ 2 samples/pixel for 2080 width.
    const double samplesPerLine = sampleRate_ / 2.0; // ~2 lines/sec classic APT
    const int spp = std::max(1, static_cast<int>(std::lround(samplesPerLine / width_)));

    bool newLine = false;
    for (size_t i = 0; i < count; ++i) {
        const float a = std::fabs(samples[i]);
        envLp_ = envLp_ * 0.95f + a * 0.05f;
        envBuf_.push_back(envLp_);
        if (static_cast<int>(envBuf_.size()) >= spp) {
            float sum = 0.0f;
            for (float v : envBuf_) sum += v;
            const float avg = sum / static_cast<float>(envBuf_.size());
            envBuf_.clear();
            uint8_t pix = static_cast<uint8_t>(std::clamp(avg * 4.0f * 255.0f, 0.0f, 255.0f));
            if (linePos_ < width_) lineAcc_[static_cast<size_t>(linePos_++)] = pix;
            if (linePos_ >= width_) {
                lines_.push_back(lineAcc_);
                if (lines_.size() > 1200) lines_.erase(lines_.begin());
                linePos_ = 0;
                std::fill(lineAcc_.begin(), lineAcc_.end(), 0);
                newLine = true;
            }
        }
    }
    return newLine;
}

bool AptImageDecoder::writePgm(const std::string& path) const {
    if (lines_.empty()) return false;
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out << "P5\n" << width_ << " " << lines_.size() << "\n255\n";
    for (const auto& row : lines_) out.write(reinterpret_cast<const char*>(row.data()),
                                             static_cast<std::streamsize>(row.size()));
    return static_cast<bool>(out);
}
