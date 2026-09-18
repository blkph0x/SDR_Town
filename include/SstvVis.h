#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct SstvVisEvent {
    uint64_t headerStartSample = 0, headerEndSample = 0;
    unsigned code = 0; // Seven data bits; parity is validated, not part of the ID.
    std::string mode;
};
struct SstvVisReport {
    uint32_t sampleRate = 0;
    uint64_t samples = 0, candidates = 0, parityRejected = 0, framingRejected = 0;
    std::vector<SstvVisEvent> headers;
};

// DEC-0091: header detector only. Single processing owner; no retained input,
// image decoder, automatic RF mode changes or audio-path mutations.
class SstvVisDetector {
public:
    explicit SstvVisDetector(uint32_t sampleRate);
    void reset(); // Source change/loss starts a new sample epoch.
    std::vector<SstvVisEvent> process(std::span<const float> samples);
    SstvVisReport counters() const;
    static std::string_view modeName(unsigned code);
private:
    uint64_t atMs(uint64_t ms) const;
    int tone(uint64_t start, uint64_t end) const;
    bool inspect(uint64_t startMs, SstvVisEvent& event);
    uint32_t rate_;
    std::vector<float> history_;
    uint64_t samples_ = 0, nextMs_ = 0, candidates_ = 0, parityRejected_ = 0, framingRejected_ = 0;
};

SstvVisReport inspectSstvAudioFile(const std::string& path, size_t chunkSamples = 4096);
