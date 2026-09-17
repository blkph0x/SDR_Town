#pragma once
#include <array>
#include <cstdint>
#include <mutex>
#include <span>
#include <string>
#include <vector>

struct DcsIdentity {
    uint16_t code = 0; // Octal payload, e.g. 0023, not decimal display digits.
    bool inverted = false;
    bool operator==(const DcsIdentity&) const = default;
};

// Algebraic protocol layer, one owner. No guessed correction of noisy words.
class DcsBitDecoder {
public:
    static std::span<const uint16_t> codes();
    static uint32_t encode(uint16_t code);
    static std::vector<DcsIdentity> aliases(uint32_t word);
    bool process(std::span<const uint8_t> bits);
    void reset();
    const std::vector<DcsIdentity>& identities() const { return identities_; }
    uint64_t bits() const { return bits_; }
private:
    std::array<uint32_t,23> words_{};
    std::array<unsigned,23> repeats_{};
    std::vector<DcsIdentity> identities_;
    uint32_t word_ = 0;
    uint64_t bits_ = 0, lastConfirmed_ = 0;
};

struct DcsSnapshot {
    std::vector<DcsIdentity> identities;
    uint64_t samples = 0, resets = 0;
    double sampleRate = 0, targetHz = 0;
    unsigned agreeingPhases = 0;
    int64_t updatedMs = 0;
    std::string status = "Inactive";
};

// One DSP owner; locked snapshot publication only. Never gates audio.
class DcsDecoder {
public:
    bool process(std::span<const float> samples, double rate, double targetHz,
                 uint64_t epoch, uint64_t firstSample, bool discontinuity);
    void reset();
    DcsSnapshot snapshot() const;
private:
    struct Lane { double phase = 0, sum = 0; DcsBitDecoder decoder; };
    std::array<Lane,8> lanes_{};
    std::array<double,2> lowpass_{};
    double dc_ = 0, dcAlpha_ = 0, alpha_ = 0;
    uint64_t epoch_ = 0, next_ = 0;
    mutable std::mutex mutex_;
    DcsSnapshot state_, published_;
    void publish();
};

DcsSnapshot decodeDcsFile(const std::string& path, size_t chunkSize = 4096);
std::vector<DcsIdentity> decodeDcsBitsFile(const std::string& path);
std::string dcsLabel(const DcsIdentity& identity);
