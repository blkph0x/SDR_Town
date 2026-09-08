#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <optional>
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
    int64_t grantEpochMs = 0;
    uint8_t slot = 0xffu;
    int64_t frequencyHz = 0;

    bool valid() const noexcept
    {
        return talkgroupId != 0 && slot < 2 && frequencyHz > 0 && callSessionId != 0;
    }

    bool operator==(const P25P2CallAudioKey& other) const noexcept
    {
        // Audio state is bound to one selected traffic allocation.  NAC/WACN/
        // system are site metadata.  Source/RID can arrive late or be refreshed
        // by control-channel metadata while the same TDMA timeslot continues, so
        // treat zero-or-different source as metadata here and keep the hard
        // boundaries on talkgroup, call session, grant epoch, slot, and carrier.
        return talkgroupId == other.talkgroupId &&
            callSessionId == other.callSessionId &&
            grantEpochMs == other.grantEpochMs &&
            slot == other.slot &&
            frequencyHz == other.frequencyHz;
    }
};

// Protocol-derived speech-frame identity (SDRTrunk/OP25 ordering).
// streamDibit is the call-global chronological coordinate; window-local
// superframe anchors must not be used for cross-window ordering.
struct Phase2VoiceFrameKey {
    bool sessionCodewordIdKnown = false;
    uint64_t sessionCodewordId = 0;
    bool streamDibitKnown = false;
    uint64_t streamDibit = 0;
    bool streamBurstStartDibitKnown = false;
    uint64_t streamBurstStartDibit = 0;
    bool sessionBurstIdKnown = false;
    uint64_t sessionBurstId = 0;
    uint64_t superframeAnchor = 0;
    uint8_t burstIndex = 0xffu;
    uint8_t slot = 0xffu;
    uint8_t voiceIndex = 0xffu;
    uint8_t burstVoiceCount = 0;

    bool operator==(const Phase2VoiceFrameKey& other) const noexcept
    {
        if (sessionCodewordIdKnown && other.sessionCodewordIdKnown) {
            return sessionCodewordId == other.sessionCodewordId;
        }
        if (streamDibitKnown && other.streamDibitKnown &&
            streamDibit == other.streamDibit &&
            slot == other.slot &&
            voiceIndex == other.voiceIndex) {
            return true;
        }
        if (sessionBurstIdKnown && other.sessionBurstIdKnown &&
            sessionBurstId == other.sessionBurstId &&
            slot == other.slot &&
            voiceIndex == other.voiceIndex) {
            return true;
        }
        return superframeAnchor == other.superframeAnchor &&
            burstIndex == other.burstIndex &&
            slot == other.slot &&
            voiceIndex == other.voiceIndex;
    }
};

inline int p25Phase2CompareVoiceFrameKeys(const Phase2VoiceFrameKey& a,
                                          const Phase2VoiceFrameKey& b) noexcept
{
    if (a.sessionCodewordIdKnown && b.sessionCodewordIdKnown) {
        if (a.sessionCodewordId < b.sessionCodewordId) return -1;
        if (a.sessionCodewordId > b.sessionCodewordId) return 1;
        return 0;
    }
    if (a.streamDibitKnown && b.streamDibitKnown) {
        if (a.streamDibit < b.streamDibit) return -1;
        if (a.streamDibit > b.streamDibit) return 1;
    }
    if (a.sessionBurstIdKnown && b.sessionBurstIdKnown) {
        if (a.sessionBurstId < b.sessionBurstId) return -1;
        if (a.sessionBurstId > b.sessionBurstId) return 1;
        if (a.voiceIndex < b.voiceIndex) return -1;
        if (a.voiceIndex > b.voiceIndex) return 1;
        if (a.slot < b.slot) return -1;
        if (a.slot > b.slot) return 1;
        return 0;
    }
    if (a.sessionCodewordIdKnown != b.sessionCodewordIdKnown ||
        a.streamDibitKnown != b.streamDibitKnown ||
        a.sessionBurstIdKnown != b.sessionBurstIdKnown) {
        return 2;
    }
    if (a.superframeAnchor < b.superframeAnchor) return -1;
    if (a.superframeAnchor > b.superframeAnchor) return 1;
    if (a.burstIndex < b.burstIndex) return -1;
    if (a.burstIndex > b.burstIndex) return 1;
    if (a.voiceIndex < b.voiceIndex) return -1;
    if (a.voiceIndex > b.voiceIndex) return 1;
    if (a.slot < b.slot) return -1;
    if (a.slot > b.slot) return 1;
    return 0;
}

inline bool p25Phase2VoiceFrameKeysSameBurst(const Phase2VoiceFrameKey& a,
                                             const Phase2VoiceFrameKey& b) noexcept
{
    if (a.streamBurstStartDibitKnown && b.streamBurstStartDibitKnown) {
        return a.streamBurstStartDibit == b.streamBurstStartDibit && a.slot == b.slot;
    }
    if (a.sessionBurstIdKnown && b.sessionBurstIdKnown) {
        return a.sessionBurstId == b.sessionBurstId && a.slot == b.slot;
    }
    if (a.superframeAnchor != b.superframeAnchor) return false;
    return a.burstIndex == b.burstIndex && a.slot == b.slot;
}

inline bool p25Phase2VoiceFrameKeyHasProtocolIdentity(const Phase2VoiceFrameKey& key) noexcept
{
    if (key.sessionCodewordIdKnown && key.sessionCodewordId != 0) return true;
    if (key.streamDibitKnown && key.streamBurstStartDibitKnown && key.slot < 2) return true;
    return key.sessionBurstIdKnown && key.sessionBurstId != 0 && key.slot < 2 && key.voiceIndex < 4;
}

inline bool p25Phase2VoiceFrameNeedsAbsoluteDedupeFallback(const Phase2VoiceFrameKey& key) noexcept
{
    return !p25Phase2VoiceFrameKeyHasProtocolIdentity(key);
}

struct P25Phase2SequencerSpeechInput {
    Phase2VoiceFrameKey key{};
    std::array<uint8_t, 96> ambe96{};
    bool haveAmbe = false;
    bool speechOrdinalKnown = false;
    int64_t speechOrdinal = -1;
    bool grantSlotKnown = false;
    uint8_t grantSlot = 0xffu;
    bool haveAbsoluteDibits = false;
    uint64_t codewordAbsDibit = 0;
    uint64_t codewordEndAbsDibit = 0;
    bool inputQualityKnown = false;
    double inputSoftDecisionQuality = 0.0;
    size_t inputSoftDecisionSymbols = 0;
    size_t inputSoftLowConfidenceSymbols = 0;
    double inputCqpskPhaseErrorRmsRad = 0.0;
    int inputBestPhase2SyncErrors = -1;
};

struct P25P2PendingAmbeFrame {
    std::array<uint8_t, 96> ambe96{};
    Phase2VoiceFrameKey frameKey{};
    bool frameKeyValid = false;
    uint8_t voiceIndex = 0;
    bool grantSlotKnown = false;
    uint8_t grantSlot = 0xffu;
    bool haveAbsoluteDibits = false;
    uint64_t codewordAbsDibit = 0;
    uint64_t codewordEndAbsDibit = 0;
    bool inputQualityKnown = false;
    double inputSoftDecisionQuality = 0.0;
    size_t inputSoftDecisionSymbols = 0;
    size_t inputSoftLowConfidenceSymbols = 0;
    double inputCqpskPhaseErrorRmsRad = 0.0;
    int inputBestPhase2SyncErrors = -1;
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
    // Independent block-channelize eyes recover jittered abs (DEC-0008), so
    // 12-dibit ShouldEmit misses overlap replays (073304). Slot+ISCH burst
    // index+voiceIndex is stable across those eyes for one superframe
    // (~360 ms). TTL must be >= overlap (280 ms) and < one superframe.
    struct LatticeEmit {
        uint8_t slot = 0xffu;
        uint8_t burstIndex = 0xffu;
        uint8_t voiceIndex = 0xffu;
        uint64_t absDibit = 0;
        bool absKnown = false;
        int64_t emitMs = 0;
    };
    std::vector<LatticeEmit> recentLatticeKeys;
};

// Per-call speech-frame sequencer.  Tracks Voice2/Voice4 burst cadence and
// assigns monotonic speech ordinals (one per 20 ms position).
struct P25Phase2FrameSequencer {
    uint64_t callSessionId = 0;
    uint32_t talkgroupId = 0;
    uint8_t slot = 0xffu;
    bool armed = false;
    int64_t nextSpeechOrdinal = 0;
    uint64_t acceptedFrames = 0;
    uint64_t duplicateOrLateDrops = 0;
    uint64_t outOfOrderDrops = 0;
    uint64_t protocolOrderIssues = 0;
    uint64_t gapSilenceOrdinals = 0;
    Phase2VoiceFrameKey lastAcceptedKey{};
    bool haveLastAcceptedKey = false;
    std::vector<Phase2VoiceFrameKey> recentKeys;
    uint64_t activeBurstAnchor = 0;
    uint8_t activeBurstIndex = 0xffu;
    uint8_t expectedVoiceIndex = 0;
    uint8_t activeBurstVoiceCount = 0;
    bool haveActiveBurst = false;
    uint64_t activeSessionBurstId = 0;
    bool activeSessionBurstIdKnown = false;
    uint64_t activeStreamBurstStartDibit = 0;
    bool activeStreamBurstStartKnown = false;
    uint8_t activeBurstSlot = 0xffu;
    std::array<std::optional<P25Phase2SequencerSpeechInput>, 4> heldFutureFrames{};
    uint64_t reorderHeld = 0;
    uint64_t reorderReleased = 0;
    uint64_t reorderExpired = 0;
};

// Producer-side speaker PCM bound to one call session.
struct P25Phase2SpeakerPendingQueue {
    uint64_t callSessionId = 0;
    uint32_t talkgroupId = 0;
    uint32_t sourceId = 0;
    int64_t grantEpochMs = 0;
    uint8_t slot = 0xffu;
    int64_t frequencyHz = 0;
    bool nextSpeechOrdinalKnown = false;
    int64_t nextSpeechOrdinal = 0;
    std::vector<float> samples;
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
    P25P2CallAudioKey lastSpeakerKey{};
    bool haveLastSpeakerKey = false;
    bool playoutBridgeEligible = false;
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
    // Companion-slot pending AMBE (multi-record / priority observe). Never drained
    // to the selected speaker path.
    P25P2PendingAudioQueue pendingAudioOpposite;
    P25AudioResamplerState resampler;
    // Independent resampler for companion-slot record path (never shares selected).
    P25AudioResamplerState resamplerOpposite;
    P25Phase2AmbeEmitDedupeState ambeDedupe;
    P25Phase2FrameSequencer frameSequencer;
    P25Phase2AudioTailState audioTail;
    P25Phase2SessionSustainState sustain;
    P25CallSecurityLatch callSecurityLatch = P25CallSecurityLatch::Unknown;
    // PTT evidence is retained across voice windows for SDRTrunk-style clear
    // release, but the queued-AMBE PTT reset is an edge operation.  Remember the
    // call session already reset so sticky PTT state cannot repeatedly discard
    // selected-slot voice context mid-call.
    uint64_t lastPttStartPendingClearCallSessionId = 0;

    void clearAll() noexcept
    {
        pendingAudio = {};
        pendingAudioOpposite = {};
        resampler = {};
        resamplerOpposite = {};
        ambeDedupe = {};
        frameSequencer = {};
        audioTail = {};
        sustain = {};
        callSecurityLatch = P25CallSecurityLatch::Unknown;
        lastPttStartPendingClearCallSessionId = 0;
    }
};
