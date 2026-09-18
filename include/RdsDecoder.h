#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

struct RdsStation {
    bool identified = false;
    uint16_t pi = 0;
    uint8_t pty = 0; // Numeric: regional RDS/RBDS names differ.
    bool trafficProgramme = false;
    bool trafficAnnouncement = false;
    std::string programmeService;
    std::string radioText;
};

struct RdsGroupEvent {
    std::array<uint16_t, 4> words{};
    uint64_t endBit = 0;
    unsigned correctedBlocks = 0;
    RdsStation station;
};

// Single-owner, already-demodulated RDS bits, MSB first, after differential
// decoding. This is not an IQ or audio decoder. Call reset on discontinuity.
class RdsDecoder {
public:
    RdsDecoder();
    ~RdsDecoder();
    RdsDecoder(const RdsDecoder&) = delete;
    RdsDecoder& operator=(const RdsDecoder&) = delete;
    std::optional<RdsGroupEvent> pushBit(bool bit);
    void reset();
    const RdsStation& station() const;
    uint64_t rejectedGroups() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
