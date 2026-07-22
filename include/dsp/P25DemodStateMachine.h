#pragma once

#include "dsp/P25DspTypes.h"

namespace p25dsp {

class P25DemodStateMachine {
public:
    void reset() noexcept;

    P25DemodState state() const noexcept { return m_state; }
    const P25TrackingConfidence& confidence() const noexcept { return m_confidence; }

    void noteCarrierStable(bool stable) noexcept;
    void noteTimingStable(bool stable) noexcept;
    void noteSyncHit(int syncErrors, int synchronizedThreshold) noexcept;
    void noteProtocolEvidence(bool macCrc, bool isch, bool voice) noexcept;
    void noteProtocolFailure() noexcept;
    void noteFullyLost() noexcept;

    size_t maxTimingPhases() const noexcept;
    size_t maxMappingCandidates() const noexcept;
    bool allowFullGridSearch() const noexcept;

private:
    P25DemodState m_state = P25DemodState::Cold;
    P25TrackingConfidence m_confidence{};
    int m_consecutiveSyncMisses = 0;
    int m_consecutiveProtocolFailures = 0;

    void recomputeState() noexcept;
};

} // namespace p25dsp
