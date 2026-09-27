#pragma once
#include "ModeS.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

// DEC-0161: retain only the unexamined 120 us packet tail, not overlapping
// already scanned windows. Epoch/sample discontinuities cannot form a packet.
class AircraftMagnitudeStream {
public:
    std::vector<std::vector<uint8_t>> process(std::span<const float> input,
        uint64_t start, uint64_t epoch, double rate) {
        if (!std::isfinite(rate) || rate < 2e6 || rate > 20e6 || input.size() > 16384) {
            tail_.clear(); initialized_ = false; return {};
        }
        if (!initialized_ || epoch != epoch_ || start != next_ || rate != rate_) tail_.clear();
        initialized_ = true; epoch_ = epoch; next_ = start + input.size(); rate_ = rate;
        tail_.insert(tail_.end(), input.begin(), input.end());
        auto frames = ModeS::extractFramesFromMagnitude(tail_.data(), tail_.size(), rate);
        const auto retain = static_cast<size_t>(std::ceil(rate * 120e-6)) + 3;
        if (tail_.size() > retain) tail_.erase(tail_.begin(), tail_.end() - retain);
        return frames;
    }
    void reset() { tail_.clear(); initialized_ = false; }
private:
    std::vector<float> tail_;
    uint64_t next_ = 0, epoch_ = 0;
    double rate_ = 0;
    bool initialized_ = false;
};
