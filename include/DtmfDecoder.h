#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <mutex>
#include <span>
#include <string>
#include <vector>

// Receive-only observer: never gates speaker audio or executes decoded commands.
struct DtmfOptions {
    bool fast = false;              // Non-telephone-conformant short-burst analysis.
    bool inverted = false;          // Spectral inversion, not audio polarity.
    double inversionHz = 3300;
    double pitchScale = 1;
    double shiftHz = 0;
    bool operator==(const DtmfOptions&) const = default;
};

struct DtmfDetection {
    char digit = 0;
    uint64_t epoch = 0, firstSample = 0, confirmedSample = 0;
    double rate = 0, targetHz = 0, rowHz = 0, columnHz = 0;
    double purity = 0, twistDb = 0;
    DtmfOptions options;
};

struct DtmfSnapshot {
    char digit = 0;
    std::string sequence, lastSequence;
    double sampleRate = 0, targetHz = 0;
    double purity = 0, twistDb = 0; // Signed twist: column minus row, dB.
    double rowHz = 0, columnHz = 0;
    uint64_t samples = 0, frames = 0, confirmedDigits = 0, resets = 0;
    uint64_t rejectedFrames = 0, droppedEvents = 0, truncatedDigits = 0, processingUs = 0;
    int64_t updatedMs = 0, lastDigitMs = 0, lastSequenceMs = 0;
    bool toneActive = false;
    std::string status = "Inactive", rejection;
    DtmfOptions options;
    std::vector<DtmfDetection> history; // Most recent 64 digits, not an unbounded log.
};

class DtmfDecoder {
public:
    explicit DtmfDecoder(DtmfOptions options = {});
    // DSP methods have one producer. Options, snapshot and event drains are thread safe.
    bool process(std::span<const float> samples, double rate, double targetHz,
                 uint64_t epoch, uint64_t firstSample, bool discontinuity);
    void reset();
    void finish();                  // EOF: finish without manufacturing PCM or time.
    void setOptions(DtmfOptions options);
    DtmfOptions options() const;
    DtmfSnapshot snapshot() const;
    struct PendingEvent {
        enum class Kind { Digit, Sequence } kind = Kind::Digit;
        char digit = 0;
        std::string sequence;
        int64_t ms = 0;
        double targetHz = 0;
        DtmfDetection detection;
    };
    std::vector<PendingEvent> takePendingEvents();

private:
    void publish();
    void queue(PendingEvent event);
    void evaluateFrame(uint64_t frameEnd);
    void finishSequence(int64_t nowMs);
    mutable std::mutex mutex_;
    DtmfOptions requested_, active_;
    DtmfSnapshot state_, published_;
    std::vector<PendingEvent> pending_;
    std::vector<float> window_;
    std::vector<double> centered_;
    std::array<std::array<double, 3>, 8> frequencies_{}, coefficients_{};
    uint64_t epoch_ = 0, nextSample_ = 0, candidateStart_ = 0;
    size_t frameLen_ = 0, hopLen_ = 0, candidateSamples_ = 0, silenceSamples_ = 0;
    char candidate_ = 0, activeDigit_ = 0;
};

DtmfSnapshot decodeDtmfFile(const std::string& path, size_t chunkSize = 4096,
    DtmfOptions options = {}, const std::function<bool()>& cancelled = {});
