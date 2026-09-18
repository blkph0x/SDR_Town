#pragma once
#include "RdsDecoder.h"
#include <memory>
#include <mutex>
#include <span>

struct RdsMpxSnapshot {
    RdsStation station;
    std::string status = "Disabled";
    double targetHz = 0, sampleRate = 0;
    uint64_t samples = 0, bits = 0, groups = 0, correctedBlocks = 0, rejectedGroups = 0, resets = 0;
    int64_t lastGroupMs = 0;
    std::array<uint16_t, 4> lastGroupWords{}; // CRC-valid evidence, not confirmed identity.
};

// Process/reset: one DSP owner only. snapshot(): safe for the GUI to poll.
// No queue: each call consumes at most 262144 samples, split into ABI-sized blocks.
class RdsMpxDecoder {
public:
    RdsMpxDecoder();
    ~RdsMpxDecoder();
    RdsMpxDecoder(const RdsMpxDecoder&) = delete;
    RdsMpxDecoder& operator=(const RdsMpxDecoder&) = delete;
    bool process(std::span<const float> samples, double rate, double targetHz,
                 uint64_t epoch, uint64_t firstSample, bool discontinuity);
    void reset();
    RdsMpxSnapshot snapshot() const;
    static int64_t monotonicMs();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    mutable std::mutex mutex_;
    RdsMpxSnapshot published_;
    void publish();
};
