#pragma once

#include <algorithm>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <vector>

// Converts DeviceManager's non-consuming absolute IQ ring snapshots into a
// chronological, exactly-once stream for the Satcom feature.  This is kept
// independent of DeviceManager/Receiver so the frozen P25 sample path is not
// modified.
class SatcomIqCursor {
public:
    struct Result {
        std::vector<std::complex<float>> samples;
        bool discontinuity = false;
        uint64_t startAbsolute = 0;
        uint64_t endAbsolute = 0;
        uint64_t streamEpoch = 0;
    };

    void reset() noexcept {
        valid_ = false;
        nextAbsolute_ = 0;
        streamEpoch_ = 0;
    }

    bool valid() const noexcept { return valid_; }
    uint64_t nextAbsolute() const noexcept { return nextAbsolute_; }
    uint64_t streamEpoch() const noexcept { return streamEpoch_; }

    Result consume(const std::vector<std::complex<float>>& window,
                   uint64_t startAbsolute,
                   uint64_t endAbsolute,
                   uint64_t streamEpoch,
                   bool sourceDiscontinuity = false)
    {
        Result result;
        result.streamEpoch = streamEpoch;
        if (window.empty() || endAbsolute <= startAbsolute) return result;

        const uint64_t declaredCount = endAbsolute - startAbsolute;
        const bool shapeMismatch = declaredCount != static_cast<uint64_t>(window.size());
        const bool newEpoch = valid_ && streamEpoch != streamEpoch_;
        const bool rewound = valid_ && endAbsolute < nextAbsolute_;

        if (!valid_ || newEpoch || rewound || sourceDiscontinuity || shapeMismatch) {
            result.samples = window;
            result.discontinuity = true;
            result.startAbsolute = startAbsolute;
            result.endAbsolute = endAbsolute;
            valid_ = true;
            nextAbsolute_ = endAbsolute;
            streamEpoch_ = streamEpoch;
            return result;
        }

        streamEpoch_ = streamEpoch;
        if (endAbsolute <= nextAbsolute_) return result; // duplicate/older snapshot

        if (nextAbsolute_ < startAbsolute) {
            // Consumer fell behind the retained ring.  Deliver the earliest
            // still-available sample, but explicitly mark the decoder gap.
            result.samples = window;
            result.discontinuity = true;
            result.startAbsolute = startAbsolute;
            result.endAbsolute = endAbsolute;
            nextAbsolute_ = endAbsolute;
            return result;
        }

        const uint64_t offset64 = nextAbsolute_ - startAbsolute;
        if (offset64 >= static_cast<uint64_t>(window.size())) {
            // Metadata and payload disagree in a way that cannot be safely
            // stitched.  Re-anchor without replaying stale audio.
            result.discontinuity = true;
            nextAbsolute_ = endAbsolute;
            return result;
        }

        const size_t offset = static_cast<size_t>(offset64);
        result.samples.assign(window.begin() + static_cast<std::ptrdiff_t>(offset), window.end());
        result.startAbsolute = nextAbsolute_;
        result.endAbsolute = endAbsolute;
        nextAbsolute_ = endAbsolute;
        return result;
    }

private:
    bool valid_ = false;
    uint64_t nextAbsolute_ = 0;
    uint64_t streamEpoch_ = 0;
};
