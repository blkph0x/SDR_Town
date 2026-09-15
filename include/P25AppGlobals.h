#pragma once

// Purpose: Shared live P25/GUI atomics, voice-diag mirror, and cadence rollup.
// Spec:    docs/DECISIONS.md DEC-0040 / ISS-0004 Phase 3 (mechanical extract from main.cpp)
// Invariants: no hop/TTL/CADENCE/feed-gate behavior changes — move-only.

#include "P25DebugStage.h"
#include "Receiver.h"

#include <atomic>
#include <cstdint>
#include <limits>

// Shared live diagnostic updated by demod calls from GUI worker and CLI monitor thread (P1 audit diags)
extern std::atomic<long long> gLastDspMicros;
extern std::atomic<long long> gP25AudioLastSpeakerOutputMs;
extern std::atomic<double> gLastRmsDb;   // live RF signal level used by squelch calibration/Auto/indicator
extern std::atomic<double> gLastNoiseFloorDb;
extern std::atomic<double> gLastSnrDb;
extern std::atomic<double> gLastAfcOffsetHz;
extern std::atomic<double> gLastAfcPpmDelta;
extern std::atomic<double> gLastAfcConfidence;
extern std::atomic<double> gLastAfcBinHz;
extern std::atomic<double> gP25LastTrustedControlFreqHz;
extern std::atomic<double> gP25LastTrustedControlOffsetHz;
extern std::atomic<long long> gP25LastTrustedControlOffsetMs;
// DEC-0044: last successful auto PPM apply (ms since epoch); 0 = never.
extern std::atomic<long long> gP25LastAutoPpmApplyMs;
extern std::atomic<double> gP25LastAutoPpmValue;

// Lock-free mirror of the active Phase-2 voice diagnostics.  The GUI follow
// status path uses try_to_lock on receiver state; when the voice worker owns
// that mutex the UI otherwise freezes on the last CC/stale snapshot.
struct P25VoiceDiagMirror {
    std::atomic<long long> updatedMs{0};
    std::atomic<uint32_t> talkgroupId{0};
    std::atomic<int> diag{0};
    std::atomic<long long> phase2Bursts{0};
    std::atomic<long long> phase2VoiceCodewords{0};
    std::atomic<long long> phase2SuperframeBursts{0};
    std::atomic<long long> phase2MaskedBursts{0};
    std::atomic<long long> phase2MacCrcValid{0};
    std::atomic<long long> phase2MacPdus{0};
    std::atomic<long long> decodedFrames{0};
    std::atomic<long long> phase2ExpectedVoiceCodewords{0};
    std::atomic<long long> phase2FedToMbelib{0};
    std::atomic<long long> phase2EmittedPcmFrames{0};
    std::atomic<long long> phase2DuplicateSuppressed{0};
    std::atomic<long long> phase2FeedGaps{0};
    std::atomic<int> tdmaSlot{-1};
};
extern P25VoiceDiagMirror gP25VoiceDiagMirror;

// Low-overhead session cadence rollup (relaxed atomics; logged at most ~1 Hz).
struct P25Phase2CadenceRollup {
    std::atomic<long long> windows{0};
    std::atomic<long long> vcw{0};
    std::atomic<long long> targetVcw{0};
    std::atomic<long long> fed{0};
    std::atomic<long long> emitted{0};
    std::atomic<long long> dups{0};
    std::atomic<long long> gaps{0};
    std::atomic<long long> reject{0};
    std::atomic<long long> lastLogMs{0};
};
extern P25Phase2CadenceRollup gP25Phase2Cadence;

extern P25DebugStage gP25DebugStageFilter;

// Accessors for the debug-stage filter (same storage as gP25DebugStageFilter).
inline P25DebugStage p25GetDebugStageFilter() noexcept
{
    return gP25DebugStageFilter;
}

inline void p25SetDebugStageFilter(P25DebugStage stage) noexcept
{
    gP25DebugStageFilter = stage;
}

// Defined after P25VoiceAudioBlock in main.cpp until ISS-0004 Phase 5 moves the struct.
struct P25VoiceAudioBlock;
void p25Phase2NoteCadenceWindow(const P25VoiceAudioBlock& out) noexcept;

void publishP25VoiceDiagMirror(const P25VoiceDiagSnapshot& diag, uint8_t tdmaSlot, bool slotKnown);
bool loadP25VoiceDiagMirror(P25VoiceDiagSnapshot& out, uint8_t& outSlot, bool& outSlotKnown);
