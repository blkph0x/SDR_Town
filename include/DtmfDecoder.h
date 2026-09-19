#pragma once
#include <array>
#include <cstdint>
#include <mutex>
#include <span>
#include <string>
#include <vector>

// Receive-only DTMF identification from an NFM discriminator tap.
// Opt-in consumer only — never gates audio and never synthesizes tones.

struct DtmfSnapshot {
    char digit = 0;                 // Last confirmed digit, or 0.
    std::string sequence;           // Digits since last inter-sequence gap.
    std::string lastSequence;       // Completed sequence (after gap), for UI/log.
    double sampleRate = 0;
    double targetHz = 0;
    double purity = 0;              // Combined row+col energy vs window energy.
    double twistDb = 0;             // |row_dB - col_dB|; lower is better.
    uint64_t samples = 0;
    uint64_t frames = 0;
    uint64_t confirmedDigits = 0;
    uint64_t resets = 0;
    int64_t updatedMs = 0;
    int64_t lastDigitMs = 0;
    int64_t lastSequenceMs = 0;
    bool toneActive = false;
    std::string status = "Inactive";
};

class DtmfDecoder {
public:
    bool process(std::span<const float> samples, double rate, double targetHz,
                 uint64_t epoch, uint64_t firstSample, bool discontinuity);
    void reset();
    DtmfSnapshot snapshot() const;

    // Consume-and-clear pending log-worthy events (digits / completed sequences).
    struct PendingEvent {
        enum class Kind { Digit, Sequence } kind = Kind::Digit;
        char digit = 0;
        std::string sequence;
        int64_t ms = 0;
    };
    std::vector<PendingEvent> takePendingEvents();

private:
    void publish();
    void evaluateFrame();
    void finishSequence(int64_t nowMs);
    mutable std::mutex mutex_;
    DtmfSnapshot state_, published_;
    std::vector<PendingEvent> pending_;
    std::vector<float> window_;
    std::array<double, 3> bandpass_{};
    double dc_ = 0, dcAlpha_ = 0;
    uint64_t epoch_ = 0, nextSample_ = 0;
    size_t frameLen_ = 0, hopLen_ = 0;
    char candidate_ = 0;
    unsigned candidateHits_ = 0;
    char activeDigit_ = 0;
    unsigned silenceFrames_ = 0;
};

DtmfSnapshot decodeDtmfFile(const std::string& path, size_t chunkSize = 4096);
