#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <vector>

// Per-receiver P25 audio/session state.  Replaces the former main.cpp globals
// keyed by raw Receiver* pointers (gP25P2PendingAudioByRx, gP25AudioResamplers,
// gP25Phase2AmbeEmitDedupe).  Owned by Receiver and cleared on traffic-session reset.

struct P25P2CallAudioKey {
    uint16_t nac = 0;
    uint32_t wacn = 0;
    uint16_t systemId = 0;
    uint32_t talkgroupId = 0;
    uint32_t sourceId = 0;
    uint64_t callSessionId = 0;
    uint8_t slot = 0xffu;
    int64_t frequencyHz = 0;
    int64_t grantEpochMs = 0;

    bool valid() const noexcept
    {
        return talkgroupId != 0 && slot < 2 && frequencyHz > 0 && callSessionId != 0;
    }

    bool operator==(const P25P2CallAudioKey& other) const noexcept
    {
        return nac == other.nac &&
            wacn == other.wacn &&
            systemId == other.systemId &&
            talkgroupId == other.talkgroupId &&
            sourceId == other.sourceId &&
            callSessionId == other.callSessionId &&
            slot == other.slot &&
            frequencyHz == other.frequencyHz &&
            grantEpochMs == other.grantEpochMs;
    }
};

struct P25P2PendingAmbeFrame {
    std::array<uint8_t, 96> ambe96{};
    uint8_t voiceIndex = 0;
    bool grantSlotKnown = false;
    uint8_t grantSlot = 0xffu;
    bool haveAbsoluteDibits = false;
    uint64_t codewordAbsDibit = 0;
    uint64_t codewordEndAbsDibit = 0;
};

struct P25P2PendingAudioQueue {
    P25P2CallAudioKey key;
    std::deque<P25P2PendingAmbeFrame> ambeFrames;
    bool armed = false;
};

struct P25AudioResamplerState {
    double phase = 0.0;
    double lastInputRate = 0.0;
    double lastOutputRate = 0.0;
    float histYm2 = 0.0f;
    float histYm1 = 0.0f;
    float histY0 = 0.0f;
    bool haveHist = false;
    float longTermPeak = 0.5f;
    float dcBlockX1 = 0.0f;
    float dcBlockY1 = 0.0f;
};

struct P25Phase2AmbeEmitDedupeState {
    uint32_t talkgroupId = 0;
    uint32_t sourceId = 0;
    uint64_t callSessionId = 0;
    int64_t grantEpochMs = 0;
    bool slotKnown = false;
    uint8_t slot = 0xffu;
    double voiceFreqHz = 0.0;
    uint64_t lastAbsDibit = 0;
    std::vector<uint64_t> recentAbsDibits;
};

// Protocol-derived speech-frame identity (SDRTrunk/OP25 ordering).  Absolute
// dibit position is for duplicate detection only — not 20 ms timeline.
struct Phase2VoiceFrameKey {
    uint64_t superframeAnchor = 0;
    uint8_t burstIndex = 0xffu;
    uint8_t slot = 0xffu;
    uint8_t voiceIndex = 0xffu;

    bool operator==(const Phase2VoiceFrameKey& other) const noexcept
    {
        return superframeAnchor == other.superframeAnchor &&
            burstIndex == other.burstIndex &&
            slot == other.slot &&
            voiceIndex == other.voiceIndex;
    }
};

// Per-call speech-frame sequencer.  Ordinals advance one per accepted
// protocol-position frame in burst order; RF dibit distance must not insert
// artificial 20 ms silence gaps.
struct P25Phase2FrameSequencer {
    uint64_t callSessionId = 0;
    uint32_t talkgroupId = 0;
    uint8_t slot = 0xffu;
    int64_t grantEpochMs = 0;
    bool armed = false;
    int64_t nextSpeechOrdinal = 0;
    uint64_t acceptedFrames = 0;
    uint64_t duplicateOrLateDrops = 0;
    std::vector<Phase2VoiceFrameKey> recentKeys;
};

// Monotonic per-call security latch: Unknown -> Clear or Unknown -> Encrypted
// only. Never returns to Unknown until call/session reset.
enum class P25CallSecurityLatch : uint8_t {
    Unknown = 0,
    Clear = 1,
    Encrypted = 2
};

struct P25Phase2AudioTailState {
    int64_t lastFreshTargetVoiceMs = 0;
    int64_t lastPlayoutBridgeMs = 0;
    uint64_t lastForwardedFedAbsDibit = 0;
    int consecutiveNoForwardFedWindows = 0;
    int consecutiveEmptyFeedWindows = 0;
    int consecutivePlayoutBridgeFrames = 0;
    // Last real speaker sample — used only to ramp clock-only silence bridges
    // so island tails do not click when the ring switches to zeros.
    float lastEmittedSample = 0.0f;
    bool haveLastEmittedSample = false;
};

// Session-level sustain peaks survive per-window diagnostic resets.  sdrtrunk's
// traffic-channel manager keeps call/decode state across timeslots; our rolling
// IQ path publishes last-window stats that can drop to zero between overlap
// slices and must not re-arm acquisition offset probes or wide reacquire windows.
struct P25Phase2SessionSustainState {
    int64_t sessionStartMs = 0;
    int64_t lastEmitMs = 0;
    long long peakDecodedFrames = 0;
    long long peakPhase2Bursts = 0;
    long long peakPhase2SuperframeBursts = 0;
    long long peakPhase2MaskedBursts = 0;
    long long peakPhase2TargetVoiceCodewords = 0;
    long long cumulativeAudioSamples = 0;
    bool hadSuccessfulEmit = false;
    bool hadBootstrapMaskLock = false;
    // Counts cold acquire processIq passes so the first post-retune eye can be
    // a wider contiguous window while later passes stream like SDRTrunk.
    int coldAcquirePasses = 0;
};

struct P25ReceiverSessionState {
    P25P2PendingAudioQueue pendingAudio;
    P25AudioResamplerState resampler;
    P25Phase2AmbeEmitDedupeState ambeDedupe;
    P25Phase2FrameSequencer frameSequencer;
    P25Phase2AudioTailState audioTail;
    P25Phase2SessionSustainState sustain;
    P25CallSecurityLatch callSecurityLatch = P25CallSecurityLatch::Unknown;

    void clearAll() noexcept
    {
        pendingAudio = {};
        resampler = {};
        ambeDedupe = {};
        frameSequencer = {};
        audioTail = {};
        sustain = {};
        callSecurityLatch = P25CallSecurityLatch::Unknown;
    }
};
