#pragma once

#include <cstdint>

// Purpose: Name the earliest P25 Phase 2 pipeline stage that dropped audio
//          in a CADENCE / voicetest window so we stop guessing timeouts.
// Spec:    docs/DECISIONS.md DEC-0002; SOURCE_OF_TRUTH.md L4; CAUSE_EFFECT_MAP
//          REQ-P2.0.
// Params:  counters already logged (targetVcw, fed, emit, dups) plus window
//          length in seconds and whether follow returned to control.
// Returns: earliest matching bucket. `Ok` means the window is idle or meets
//          the voicetest continuous duty bar.
// Invariants: no DSP, no I/O, no mutable process state.

enum class P25AudioDropBucket : std::uint8_t {
    Ok = 0,
    Extract = 1, // A
    Feed = 2,    // B
    Emit = 3,    // C
    Playout = 4, // D
    Follow = 5,  // E
};

struct P25AudioDropSample {
    long long targetVcw = 0;
    long long fed = 0;
    // Named emittedPcm: Qt's `emit` macro would smash a field called emit
    // when this header is included from the GUI TU (DEC-0002 counters).
    long long emittedPcm = 0;
    long long dups = 0;
    double windowSeconds = 1.0;
    bool followReturned = false;
};

// Same numeric bar as kP25Phase2LateEntryStrongTargetVoiceCodewords in
// src/main.cpp (8 selected-slot voice codewords), expressed per second.
// DEC-0002: do not invent a new extract floor.
inline constexpr double kP25AudioDropMinUniqueVcwPerSecond = 8.0;

// Same bar as voicetest continuousOk duty in src/main.cpp.
inline constexpr double kP25AudioDropContinuousDuty = 0.65;

// 20 ms AMBE frame period used by CADENCE dutySec = emit * 0.020.
inline constexpr double kP25AudioDropAmbeFrameSeconds = 0.020;

P25AudioDropBucket classifyP25AudioDrop(const P25AudioDropSample& sample) noexcept;

const char* p25AudioDropBucketLabel(P25AudioDropBucket bucket) noexcept;
