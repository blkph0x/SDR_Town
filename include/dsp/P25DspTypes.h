#pragma once

#include <cstdint>

namespace p25dsp {

// OP25/SDRTrunk-aligned Phase 2 air constants (must match P25LiveDecoder).
constexpr uint64_t kPhase2FrameSyncWord = 0x575D57F7FFull;
constexpr size_t kPhase2FrameSyncDibits = 20;

// OP25 p25p2_framer: P25P2_BURST_SIZE = 180 dibits per TDMA burst.
constexpr size_t kPhase2BurstDibits = 180;

// SDRTrunk P25P2SuperFrameDetector: FRAGMENT_DIBIT_LENGTH = 720 dibits.
constexpr size_t kPhase2SuperframeDibits = 720;

// SDRTrunk P25P2SuperFrameDetector sync Hamming thresholds.
constexpr int kSyncThresholdSynchronized = 7;
constexpr int kSyncThresholdUnsynchronized = 4;

// SDRTrunk isValidSyncOffset: dibit stuffing/deletion recovery in [-2, +2].
constexpr int kSyncOffsetMin = -2;
constexpr int kSyncOffsetMax = 2;

// OP25/SDRTrunk: 20 dibits (40 bits) for Phase 2 frame sync.
constexpr size_t kPhase2SyncDibits = 20;

enum class P25DemodState : uint8_t {
    Cold = 0,
    AcquiringTiming,
    AcquiringMapping,
    TrackingSoft,
    TrackingHard,
    Recovering
};

struct P25TrackingConfidence {
    int carrier = 0;
    int timing = 0;
    int sync = 0;
    int scrambling = 0;
    int protocol = 0;
};

struct P25DspProfileCounters {
    uint64_t inputSamplesProcessed = 0;
    uint64_t channelOutputSamples = 0;
    uint64_t heapAllocsPerBlock = 0;
    uint64_t bytesCopiedPerBlock = 0;
    uint64_t filterDesignCalls = 0;
    uint64_t candidateTimingPhases = 0;
    uint64_t candidateMappings = 0;
    uint64_t stagedSyncRejections = 0;
    uint64_t fullProtocolDecodes = 0;
    uint64_t syncCorrelations = 0;
    uint64_t framerBurstsEmitted = 0;
    uint64_t framerSuperframesEmitted = 0;
    uint64_t samplesReprocessedOverlap = 0;
};

} // namespace p25dsp
