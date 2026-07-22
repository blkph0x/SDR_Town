#pragma once

#include "dsp/P25DspTypes.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace p25dsp {

struct P25Phase2FramerBurst {
    std::array<int, kPhase2BurstDibits> dibits{};
    uint64_t absoluteStartDibit = 0;
    int syncErrors = -1;
    bool inverted = false;
    int dibitOffsetCorrection = 0;
};

struct P25Phase2FramerSuperframe {
    std::array<int, kPhase2SuperframeDibits> dibits{};
    uint64_t absoluteStartDibit = 0;
    int sync1Errors = -1;
    int sync2Errors = -1;
    int dibitOffsetCorrection = 0;
};

// Persistent streaming framer aligned with:
// - OP25 p25p2_framer::rx_sym (180-dibit burst assembly)
// - SDRTrunk P25P2SuperFrameDetector (720-dibit fragment, ±2 dibit realignment)
class P25Phase2Framer {
public:
    void reset() noexcept;

    // Feed recovered dibits in stream order (one call per dibit or batched).
    void consumeDibits(std::span<const int> dibits);

    std::vector<P25Phase2FramerBurst> takeBursts();
    std::vector<P25Phase2FramerSuperframe> takeSuperframes();

    uint64_t absoluteDibitCursor() const noexcept { return m_absoluteDibit; }
    bool synchronized() const noexcept { return m_synchronized; }
    int syncConfidence() const noexcept { return m_syncConfidence; }

private:
    uint64_t m_absoluteDibit = 0;
    uint64_t m_syncRegister = 0;
    bool m_synchronized = false;
    int m_syncConfidence = 0;
    int m_inSyncAllowance = 0;
    size_t m_burstFill = 0;
    std::array<int, kPhase2BurstDibits> m_burstBody{};
    std::array<int, kPhase2SuperframeDibits + 4> m_superframeRing{};
    size_t m_superframeFill = 0;

    std::vector<P25Phase2FramerBurst> m_pendingBursts;
    std::vector<P25Phase2FramerSuperframe> m_pendingSuperframes;

    static int syncHammingDistance(uint64_t reg, uint64_t expected) noexcept;
    int checkSynchronizedOffset(int baseIndex) const noexcept;
    void tryEmitBurst(int syncErrors, bool inverted, int offsetCorrection);
    void tryEmitSuperframe();
};

} // namespace p25dsp
