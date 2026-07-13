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
    uint8_t slot = 0xffu;
    int64_t frequencyHz = 0;
    int64_t grantEpochMs = 0;

    bool valid() const noexcept
    {
        return talkgroupId != 0 && slot < 2 && frequencyHz > 0;
    }

    bool operator==(const P25P2CallAudioKey& other) const noexcept
    {
        return nac == other.nac &&
            wacn == other.wacn &&
            systemId == other.systemId &&
            talkgroupId == other.talkgroupId &&
            slot == other.slot &&
            frequencyHz == other.frequencyHz;
    }
};

struct P25P2PendingAmbeFrame {
    std::array<uint8_t, 96> ambe96{};
    uint8_t voiceIndex = 0;
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
};

struct P25Phase2AmbeEmitDedupeState {
    uint32_t talkgroupId = 0;
    bool slotKnown = false;
    uint8_t slot = 0xffu;
    double voiceFreqHz = 0.0;
    uint64_t lastAbsDibit = 0;
    std::vector<uint64_t> recentAbsDibits;
};

struct P25Phase2AudioTailState {
    int64_t lastFreshTargetVoiceMs = 0;
    uint64_t lastForwardedFedAbsDibit = 0;
    int consecutiveNoForwardFedWindows = 0;
    int consecutiveEmptyFeedWindows = 0;
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
    P25Phase2AudioTailState audioTail;
    P25Phase2SessionSustainState sustain;

    void clearAll() noexcept
    {
        pendingAudio = {};
        resampler = {};
        ambeDedupe = {};
        audioTail = {};
        sustain = {};
    }
};
