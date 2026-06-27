#pragma once

#include <cstdint>

enum class P25FollowDiagCode : int {
    Idle = 0,
    SkippedEncrypted,
    WaitingForClearGrant,
    NoSync,
    NidUnlocked,
    BackendMissing,
    Phase2Unsupported,
    Phase2AudioLockMissing,
    Phase2MetadataMissing,
    Phase2MaskMissing,
    Phase2MaskAppliedNoMacCrc,
    Phase2EssMissing,
    Phase2WrongSlot,
    Phase2AmbeRejected,
    Phase2LateEntryWaiting,
    NoLduVoice,
    NoDecodedAudio,
    Decoding,
};

struct P25FollowSnapshot {
    int64_t nowMs = 0;
    int64_t tunedAtMs = 0;
    int64_t lastActiveMs = 0;
    int64_t diagUpdatedMs = 0;
    bool autoActive = false;
    bool phase2Voice = false;
    uint32_t talkgroupId = 0;
    uint32_t fallbackTalkgroupId = 0;
    int diag = static_cast<int>(P25FollowDiagCode::Idle);
    long long syncs = 0;
    long long nids = 0;
    long long imbeFrames = 0;
    long long decodedFrames = 0;
    long long phase2Bursts = 0;
    long long phase2VoiceCodewords = 0;
    long long phase2SuperframeBursts = 0;
    long long phase2MaskedBursts = 0;
    long long phase2MacPdus = 0;
    long long phase2MacCrcValid = 0;
    bool phase2EssKnown = false;
    bool phase2EssEncrypted = false;
};

enum class P25FollowAction {
    None,
    ReturnEncrypted,
    ReturnActivityGone,
    ReturnHardTimeout,
    ReturnNoMacEss,
    ReturnNoVoiceCodewords,
};

struct P25FollowDecision {
    P25FollowAction action = P25FollowAction::None;
    uint32_t effectiveTalkgroupId = 0;
    bool voiceStillLooksActive = false;
    bool encryptedOnVoice = false;
    bool activityGone = false;
    bool hardTimeout = false;
    bool tdmaEpochLockedNoMacEss = false;
    bool tdmaNoProgressTimeout = false;
    bool tdmaVcwNoSuperframeTimeout = false;
    bool tdmaNoVcwTimeout = false;
};

P25FollowDecision evaluateP25Follow(const P25FollowSnapshot& snapshot);

struct P25SlotProbeSnapshot {
    int64_t nowMs = 0;
    int64_t tunedAtMs = 0;
    int64_t trackedArmMs = 0;
    int64_t lastFlipMs = 0;
    uint32_t talkgroupId = 0;
    uint32_t trackedTalkgroupId = 0;
    double voiceHz = 0.0;
    double trackedVoiceHz = 0.0;
    int wrongSlotChecks = 0;
    int flipCount = 0;
    int maxFlips = 6;
    int minVoiceCodewords = 4;
    int wrongSlotThreshold = 1;
    int64_t minFlipIntervalMs = 5000;
    double voiceHzResetThreshold = 50.0;
    bool inPassband = false;
    int diag = static_cast<int>(P25FollowDiagCode::Idle);
    long long phase2VoiceCodewords = 0;
    long long phase2SuperframeBursts = 0;
    long long phase2MaskedBursts = 0;
    long long phase2MacPdus = 0;
    long long phase2MacCrcValid = 0;
    bool phase2EssKnown = false;
};

struct P25SlotProbeDecision {
    bool resetTracking = false;
    bool wrongSlotEligible = false;
    bool resetWrongSlot = false;
    bool tdmaEpochLocked = false;
    bool noMacEssYet = false;
    bool probeRateOk = false;
    bool shouldFlip = false;
    int wrongSlotChecksAfterObservation = 0;
    int flipCountAfterObservation = 0;
};

P25SlotProbeDecision evaluateP25SlotProbe(const P25SlotProbeSnapshot& snapshot);
