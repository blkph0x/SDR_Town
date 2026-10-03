#pragma once
#include <cstdint>
#include <memory>
#include <span>
#include <string>

struct CwOptions { float pitchHz = 0, speedWpm = 0; }; // Zero selects automatic estimation.
struct CwSnapshot {
    std::string text;
    double pitchHz = 0, speedWpm = 0, processingMs = 0;
    uint64_t samples = 0, blocks = 0, resets = 0, rejected = 0;
};

// DEC-0167: one worker owner, bounded input and local-only transcript.
class CwDecoder final {
public:
    explicit CwDecoder(CwOptions options = {});
    ~CwDecoder();
    void process(std::span<const float> audio, double rate, uint64_t epoch,
                 uint64_t firstSample, bool discontinuity = false);
    CwSnapshot snapshot() const { return snapshot_; }
private:
    struct State;
    std::unique_ptr<State> state_;
    CwOptions options_;
    CwSnapshot snapshot_;
};
