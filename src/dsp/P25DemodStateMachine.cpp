#include "dsp/P25DemodStateMachine.h"

#include <algorithm>

namespace p25dsp {

void P25DemodStateMachine::reset() noexcept
{
    m_state = P25DemodState::Cold;
    m_confidence = {};
    m_consecutiveSyncMisses = 0;
    m_consecutiveProtocolFailures = 0;
}

void P25DemodStateMachine::noteCarrierStable(bool stable) noexcept
{
    if (stable) {
        m_confidence.carrier = std::min(100, m_confidence.carrier + 4);
    } else {
        m_confidence.carrier = std::max(0, m_confidence.carrier - 8);
    }
    recomputeState();
}

void P25DemodStateMachine::noteTimingStable(bool stable) noexcept
{
    if (stable) {
        m_confidence.timing = std::min(100, m_confidence.timing + 4);
    } else {
        m_confidence.timing = std::max(0, m_confidence.timing - 8);
    }
    recomputeState();
}

void P25DemodStateMachine::noteSyncHit(int syncErrors, int synchronizedThreshold) noexcept
{
    if (syncErrors <= synchronizedThreshold) {
        m_confidence.sync = std::min(100, m_confidence.sync + 12);
        m_consecutiveSyncMisses = 0;
    } else {
        m_confidence.sync = std::max(0, m_confidence.sync - 6);
        ++m_consecutiveSyncMisses;
    }
    recomputeState();
}

void P25DemodStateMachine::noteProtocolEvidence(bool macCrc, bool isch, bool voice) noexcept
{
    if (macCrc) m_confidence.protocol = std::min(100, m_confidence.protocol + 20);
    if (isch) m_confidence.protocol = std::min(100, m_confidence.protocol + 8);
    if (voice) m_confidence.protocol = std::min(100, m_confidence.protocol + 4);
    if (macCrc || isch || voice) {
        m_consecutiveProtocolFailures = 0;
    }
    recomputeState();
}

void P25DemodStateMachine::noteProtocolFailure() noexcept
{
    m_confidence.protocol = std::max(0, m_confidence.protocol - 6);
    ++m_consecutiveProtocolFailures;
    recomputeState();
}

void P25DemodStateMachine::noteFullyLost() noexcept
{
    m_state = P25DemodState::Cold;
    m_confidence = {};
    m_consecutiveSyncMisses = 0;
    m_consecutiveProtocolFailures = 0;
}

void P25DemodStateMachine::recomputeState() noexcept
{
    if (m_confidence.carrier < 20 || m_confidence.timing < 20) {
        m_state = P25DemodState::Cold;
        return;
    }
    if (m_consecutiveSyncMisses >= 6) {
        m_state = P25DemodState::Recovering;
        return;
    }
    if (m_confidence.sync < 25) {
        m_state = P25DemodState::AcquiringTiming;
        return;
    }
    if (m_confidence.protocol < 25) {
        m_state = P25DemodState::AcquiringMapping;
        return;
    }
    if (m_confidence.protocol >= 60 && m_confidence.sync >= 60) {
        m_state = P25DemodState::TrackingHard;
        return;
    }
    m_state = P25DemodState::TrackingSoft;
}

size_t P25DemodStateMachine::maxTimingPhases() const noexcept
{
    switch (m_state) {
    case P25DemodState::Cold:
    case P25DemodState::AcquiringTiming:
        return 6;
    case P25DemodState::AcquiringMapping:
        return 4;
    case P25DemodState::Recovering:
        return 3;
    case P25DemodState::TrackingSoft:
        return 2;
    case P25DemodState::TrackingHard:
        return 1;
    }
    return 1;
}

size_t P25DemodStateMachine::maxMappingCandidates() const noexcept
{
    switch (m_state) {
    case P25DemodState::Cold:
    case P25DemodState::AcquiringTiming:
    case P25DemodState::AcquiringMapping:
        return 0; // 0 = unlimited (legacy grid) during acquisition
    case P25DemodState::Recovering:
        return 8;
    case P25DemodState::TrackingSoft:
        return 2;
    case P25DemodState::TrackingHard:
        return 1;
    }
    return 1;
}

bool P25DemodStateMachine::allowFullGridSearch() const noexcept
{
    return m_state == P25DemodState::Cold ||
           m_state == P25DemodState::AcquiringTiming ||
           m_state == P25DemodState::AcquiringMapping;
}

} // namespace p25dsp
