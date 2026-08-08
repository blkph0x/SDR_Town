#pragma once

#include "IP25AmbeEncoder.h"

#include <cstdint>
#include <string>
#include <vector>

// Sprint 4 skeleton: build Phase 2 clear superframe dibits for one TDMA slot.
// Current implementation is structural only — ISCH/MAC/ESS fields are placeholders
// until bit-exact packing against captures is finished. Safe for offline dump tests.

struct P25Phase2TxFramerConfig {
    uint16_t nac = 0;
    uint32_t wacn = 0;
    uint16_t systemId = 0;
    uint32_t talkgroupId = 0;
    uint32_t unitId = 0;
    uint8_t slot = 0; // 0 or 1
    bool clearOnly = true;
};

struct P25Phase2TxSuperframe {
    // 360 ms superframe at 6000 sps → 2160 symbols → 4320 dibit-bits as 0..3.
    // Stored as one dibit per byte (0..3) for easy H-CPM later.
    std::vector<uint8_t> dibits;
    size_t voiceFramesUsed = 0;
    bool valid = false;
    std::string note;
};

class P25Phase2TxFramer {
public:
    void setConfig(const P25Phase2TxFramerConfig& cfg);
    const P25Phase2TxFramerConfig& config() const noexcept { return m_cfg; }

    // Consumes up to 18 half-rate frames (Voice4×4 + Voice2×1 patterns vary);
    // uses as many as available for a prototype Voice4-heavy superframe skeleton.
    P25Phase2TxSuperframe buildSuperframe(const std::vector<P25AmbeEncodedFrame>& frames);

    // LFSR XOR mask phase seed from NAC/WACN/SYS (matches RX identity; apply later).
    uint64_t maskSeed() const noexcept;

private:
    P25Phase2TxFramerConfig m_cfg;
    uint32_t m_superframeSeq = 0;
};

// Write dibits as raw bytes (one dibit 0..3 per byte) for offline tools.
bool p25WriteTxDibitDump(const std::string& path, const P25Phase2TxSuperframe& sf);
