#pragma once

// Purpose: P25 voice decode / feed / emit / AMBE path + shared RF helpers.
// Spec:    docs/DECISIONS.md DEC-0040 / ISS-0004 Phase 5 (mechanical extract from main.cpp)
// Invariants: no hop/TTL/CADENCE/feed-gate behavior changes — move-only.

#include "AudioEngine.h"
#include "Demod.h"
#include "P25AppGlobals.h"
#include "P25Control.h"
#include "P25FollowStateMachine.h"
#include "P25LiveDecoder.h"
#include "P25ReceiverSession.h"
#include "P25TalkgroupRegistry.h"
#include "P25VoiceTiming.h"
#include "Receiver.h"
#include "SignalClassifier.h"

#include <QJsonObject>
#include <QString>
#include <QtGlobal>

#include <complex>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

// Voice-path thresholds still used by decode (ISS-0004 Phase 5).
inline constexpr bool kP25Phase2AllowUnknownGrantFieldAudioProbe = false;
inline constexpr qint64 kP25Phase2UnknownGrantAudioProbeGraceMs = 400;
inline constexpr qint64 kP25Phase2RecentSecurityEvidenceTtlMs = 45000;
inline constexpr size_t kP25Phase2UnknownGrantAudioProbeMinFrames = 2;
inline constexpr size_t kP25Phase2UnknownGrantAudioProbeMinSamples = 1920u;
inline constexpr size_t kP25Phase2ExplicitClearGrantProbeMinFrames = 2;
inline constexpr size_t kP25Phase2PendingReleaseMinFrames = 1;
inline constexpr size_t kP25Phase2PendingQueueMaxFrames = 20; // 0.40 s at 20 ms/frame
inline constexpr size_t kP25Phase2PendingDrainMaxFramesPerTick = 8; // 160 ms
inline constexpr size_t kP25Phase2LateEntryStrongSuperframeBursts = 6;
inline constexpr size_t kP25Phase2LateEntryStrongMaskedBursts = 6;
inline constexpr size_t kP25Phase2LateEntryStrongTargetMaskedBursts = 4;
inline constexpr size_t kP25Phase2LateEntryStrongTargetVoiceCodewords = 8;

enum class P25VoiceDecodeProfile {
    Realtime = 0,
    Forensic
};

enum class P25VoiceDiagCode : int {
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

struct RfSquelchMetrics {
    double signalLevelDb = -120.0;
    double noiseFloorDb = -120.0;
    double snrDb = 0.0;
    bool valid = false;
};

struct BandPlanEntry {
    std::string name;
    double startHz = 0.0;
    double endHz = 0.0;
    DemodMode mode = DemodMode::NFM;
    double bandwidthHz = 12500.0;
    double lpfHz = 3000.0;
    double stepHz = 12500.0;
};

const BandPlanEntry* findBandPlanForFrequency(double freqHz);

struct SmartModeSelection {
    DemodMode mode = DemodMode::NFM;
    double bandwidthHz = 12500.0;
    double lpfHz = 3000.0;
    std::string source;
    SignalRecommendation classifier;
};

struct P25VoiceAudioBlock {
    std::vector<float> audio;
    uint32_t talkgroupId = 0;
    P25VoiceDiagCode diag = P25VoiceDiagCode::Idle;
    size_t imbeFrames = 0;
    size_t decodedFrames = 0;
    size_t syncs = 0;
    size_t nids = 0;
    size_t phase2Bursts = 0;
    size_t phase2VoiceCodewords = 0;
    size_t phase2TargetVoiceCodewords = 0;
    size_t phase2TargetMaskedBursts = 0;
    size_t phase2OppositeVoiceCodewords = 0;
    // Dual-slot observe scaffold (roadmap step 2): physical TS0/TS1 activity for
    // priority/UI. Speaker feed remains selected-slot only — never mix opp AMBE.
    size_t phase2Slot0VoiceCodewords = 0;
    size_t phase2Slot1VoiceCodewords = 0;
    size_t phase2Slot0MacCrcValid = 0;
    size_t phase2Slot1MacCrcValid = 0;
    // Companion-slot AMBE module (second AudioModule): decode/queue stats only.
    size_t phase2OppositeAmbeDecodeAttempts = 0;
    size_t phase2OppositeAmbeAcceptedFrames = 0;
    size_t phase2OppositePendingQueued = 0;
    size_t phase2OppositeRecordSamples = 0;
    // Companion-slot record PCM for this window only (never mixed into out.audio).
    std::vector<float> phase2OppositeRecordPcm;
    // Comparison: what the decoder "needs" (from burst kinds on target + mask + clear path)
    // vs what we actually extract, feed to mbelib, and emit as PCM.
    // Helps verify order (strictly increasing absDibit) and amounts (no dropped 20ms frames).
    size_t phase2ExpectedVoiceCodewords = 0;
    size_t phase2FedToMbelib = 0;
    size_t phase2EmittedPcmFrames = 0;
    size_t phase2ConcealmentFrames = 0;
    uint64_t phase2FirstFedAbsDibit = 0;
    uint64_t phase2LastFedAbsDibit = 0;
    size_t phase2FeedGaps = 0;
    size_t phase2FeedOrderIssues = 0;
    // Note: phase2TargetMaskedBursts tracks masked bursts for the followed target slot only
    // (populated during slot evidence pass; used for OP25-style direct audio emit on target masked voice).
    uint64_t phase2FreshStartAbsDibit = 0;
    bool phase2FreshStartAbsDibitKnown = false;
    size_t phase2ContextVoiceCodewords = 0;
    size_t phase2ContextSuppressedVoiceCodewords = 0;
    size_t phase2AmbeDecodeAttempts = 0;
    size_t phase2AmbeAcceptedFrames = 0;
    size_t phase2AmbeAcceptedCanonicalFrames = 0;
    size_t phase2AmbeAcceptedFallbackFrames = 0;
    size_t phase2DiagnosticAmbeProbeAttempts = 0;
    size_t phase2DiagnosticAmbeProbeAccepted = 0;
    size_t phase2AmbeVariantChanges = 0;
    size_t phase2SuperframeBursts = 0;
    size_t phase2MaskedBursts = 0;
    size_t phase2MacPdus = 0;
    size_t phase2MacCrcValid = 0;
    size_t phase2MacFecDecoded = 0;
    size_t phase2MacDirectCrcValid = 0;
    size_t phase2MacDirectCrcRejected = 0;
    size_t phase2MacRsDecoded = 0;
    size_t phase2MacNominalCrcValid = 0;
    size_t phase2MacAltKindCrcValid = 0;
    size_t phase2MacBitSwapCrcValid = 0;
    size_t phase2MacSlipCrcValid = 0;
    size_t phase2MacInvertCrcValid = 0;
    bool phase2EssKnown = false;
    bool phase2EssEncrypted = false;
    // DEC-0033 streaming HDQPSK diagnostics (copied from live.stats).
    bool cqpskLockActive = false;
    int cqpskLockMisses = 0;
    double cqpskResidualCarrierHz = 0.0;
    double cqpskPhaseErrorRmsRad = 0.0;
    uint64_t dspFramerBurstsEmitted = 0;
    std::string demodState;
    size_t dibitCount = 0;
    // Followed-slot-only security.  Aggregate live.stats ESS/MAC counters can
    // include the opposite TDMA slot on the same RF carrier; never use those
    // aggregate counters to release or drop speaker audio.
    bool phase2TargetEssKnown = false;
    bool phase2TargetEssEncrypted = false;
    bool phase2TargetMacCrcValid = false;
    bool phase2TargetSessionAudioRelease = false;
    bool phase2TargetSecurityStateFromPtt = false;
    // Set only from this window's selected-slot bursts (before sticky recent overlay).
    // Dual-slot MAC-dead mute must use these — never sticky ESS/MAC alone.
    bool phase2ThisWindowTargetMacCrcValid = false;
    bool phase2ThisWindowTargetEssClear = false;
    bool phase2ThisWindowTargetEssEncrypted = false;
    bool phase2ThisWindowTargetSessionAudioRelease = false;
    bool phase2SdrtrunkLateEntryVoiceRelease = false;
    bool phase2ExplicitClearGrantVoiceRelease = false;
    bool phase2CurrentFeedTrustedTargetBurst = false;
    bool phase2SameCallSelectedTimeslotContinuation = false;
    bool nidLock = false;
    bool skippedEncrypted = false;
    bool waitingForClearGrant = false;
    bool backendAvailable = false;
    bool decoderRan = false;
    bool phase2VoiceUnsupported = false;
    bool phase2AudioLockMissing = false;
    bool phase2MetadataMissing = false;
    bool phase2MaskMissing = false;
    bool phase2MaskAppliedNoMacCrc = false;
    bool phase2EssMissing = false;
    bool phase2WrongSlot = false;
    bool phase2AmbeRejected = false;
    bool phase2AmbeVariantUnstable = false;
    bool phase2LateEntryWaiting = false;
    size_t phase2RejectedVoiceCodewords = 0;
    size_t phase2InputQualityRejectedVoiceCodewords = 0;
    size_t phase2WrongSlotVoiceCodewords = 0;
    size_t phase2TrafficTalkgroupMismatchVoiceCodewords = 0;
    size_t phase2TrafficTalkgroupStaleMismatchVoiceCodewords = 0;
    size_t phase2DuplicateSuppressedVoiceCodewords = 0;
    size_t phase2AbsoluteDuplicateSuppressedVoiceCodewords = 0;
    size_t phase2SequencerSuppressedVoiceCodewords = 0;
    double centerFreqHz = 0.0;
    double effectiveTargetFreqHz = 0.0;
    size_t phase2PreSecurityAudioSamples = 0;
    size_t phase2PreSecurityDecodedFrames = 0;
    size_t phase2PendingAmbeFramesQueued = 0;
    size_t phase2PendingAmbeFramesReleased = 0;
    size_t phase2PendingAudioSamplesBefore = 0;
    size_t phase2PendingAudioSamplesAfter = 0;
    bool phase2SecurityTrustedClear = false;
    bool phase2SecurityTrustedEncrypted = false;
    bool phase2SecurityUnknown = false;
    bool phase2CurrentProbePcmUsable = false;
    bool phase2FieldAudioProbeAllowed = false;
    bool phase2UnknownProbeQualityOk = false;
    std::string phase2SecurityGateAction;
    std::string phase2UnknownProbeBlockReason;
    std::string phase2SpeakerGateReason;
    bool phase2AudioTailGraceActive = false;
    bool phase2StaleAudioTail = false;
    std::vector<int64_t> phase2EmittedSpeechOrdinals;
    std::string demodPath;
    std::vector<std::string> decoderWarnings;
};

struct ReceiverSessionKey {
    const Receiver* receiver = nullptr;
    uint64_t generation = 0;

    bool operator==(const ReceiverSessionKey& other) const noexcept
    {
        return receiver == other.receiver && generation == other.generation;
    }

    bool operator<(const ReceiverSessionKey& other) const noexcept
    {
        if (std::less<const Receiver*>{}(receiver, other.receiver)) return true;
        if (std::less<const Receiver*>{}(other.receiver, receiver)) return false;
        return generation < other.generation;
    }
};

struct ReceiverSessionKeyHash {
    size_t operator()(const ReceiverSessionKey& key) const noexcept
    {
        const auto ptrHash = std::hash<const Receiver*>{}(key.receiver);
        const auto genHash = std::hash<uint64_t>{}(key.generation);
        return ptrHash ^ (genHash + 0x9e3779b97f4a7c15ull + (ptrHash << 6) + (ptrHash >> 2));
    }
};

using P25SpeakerPendingMap = std::map<ReceiverSessionKey, P25Phase2SpeakerPendingQueue>;

enum class P25PendingClearReason : uint8_t {
    CallIdentityChanged = 0,
    EncryptedState,
    CallEnded,
    RetuneOrGeneration,
    UserStop,
    SlotProbeDestructive, // blocked when grant slot immutable
    PttStartReset,        // sdrtrunk PTT clears stale queued voice
    // Capture 20260811_021036: after the call is already speaking, prefer live
    // selected-slot VCWs over draining late-entry stash in the same window.
    LiveStreamPreferred,
};

enum class FrameOrderResult : uint8_t {
    Expected = 0,
    Future,
    Duplicate,
    Late,
    Unorderable
};

struct P25Phase2AmbeVariantProbe {
    int variant = 0;
    int status = static_cast<int>(P25VoiceDecodeStatus::InvalidFrame);
    int errors = 0;
    int totalErrors = 0;
    size_t pcmSamples = 0;
    double pcmPeak = 0.0;
    double pcmRms = 0.0;
    bool finite = false;
    bool usable = false;
    double score = 0.0;
};

struct P25Phase2AmbeValidationFrame {
    size_t burstDibitOffset = 0;
    bool superframeBurstIndexKnown = false;
    uint8_t superframeBurstIndex = 0;
    bool grantSlotKnown = false;
    uint8_t grantSlot = 0xffu;
    uint8_t voiceIndex = 0;
    bool haveAbsoluteDibits = false;
    uint64_t codewordAbsDibit = 0;
    uint64_t codewordEndAbsDibit = 0;
    bool duplicateInSession = false;
    bool duplicateSuppressed = false;
    bool duplicateSuppressedByAbsolute = false;
    bool duplicateSuppressedBySequencer = false;
    bool contextSuppressed = false;
    int lockedVariantBefore = -1;
    int lockedVariantAfter = -1;
    int variant = 0;
    double probeScore = 0.0;
    std::vector<P25Phase2AmbeVariantProbe> variantProbes;
    std::string ambeBits;
    int status = static_cast<int>(P25VoiceDecodeStatus::InvalidFrame);
    int errors = 0;
    int totalErrors = 0;
    std::string message;
    double pcmPeak = 0.0;
    double pcmRms = 0.0;
    bool accepted = false;
    bool timelineEmitted = false;
    bool inputQualityKnown = false;
    double inputSoftDecisionQuality = 0.0;
    size_t inputSoftDecisionSymbols = 0;
    size_t inputSoftLowConfidenceSymbols = 0;
    double inputSoftLowConfidenceRatio = 0.0;
    double inputCqpskPhaseErrorRmsRad = 0.0;
    int inputBestPhase2SyncErrors = -1;
    bool inputQualityAccepted = true;
    std::string inputQualityBlockReason;
};

struct P25TrafficProcessorStatusSnapshot {
    bool present = false;
    bool callActive = false;
    P25TrafficChannelProcessor::Diag diag;
};

struct P25FollowGuiStatusCache {
    qint64 updatedMs = 0;
    quintptr receiverKey = 0;
    uint64_t trafficGeneration = 0;
    bool independentTrafficSource = false;
    double trafficVoiceFreqHz = 0.0;
    P25VoiceDiagSnapshot voiceDiag;
    P25TrafficProcessorStatusSnapshot trafficStatus;
    bool voiceStateDecodeEnabled = false;
    bool voiceStatePhase2 = false;
    uint64_t voiceStateCurrentCallSessionId = 0;
    bool voiceStateClearKnown = false;
    bool voiceStateEncrypted = false;
    P25CallSecurityLatch voiceStateCallSecurityLatch = P25CallSecurityLatch::Unknown;
    bool voiceStateSlotKnown = false;
    uint8_t voiceStateSlot = 0;
    bool voiceStateMaskKnown = false;
    uint16_t voiceStateNac = 0;
    uint32_t voiceStateWacn = 0;
    uint16_t voiceStateSystemId = 0;
    bool voiceStateResetPending = false;
    bool voiceStateSlotProbePending = false;
    uint8_t voiceStateSlotProbeRequested = 0;
    qint64 voiceStateSettleUntilMs = 0;
    int voiceStateDiscardWindows = 0;
};

struct P25Phase2AmbeInputQuality {
    bool known = false;
    double softDecisionQuality = 0.0;
    size_t softDecisionSymbols = 0;
    size_t softLowConfidenceSymbols = 0;
    double softLowConfidenceRatio = 0.0;
    double cqpskPhaseErrorRmsRad = 0.0;
    int bestPhase2SyncErrors = -1;
};

struct P25Phase2AmbeResolveResult {
    int variant = 0;
    std::array<uint8_t, 96> frame{};
    std::vector<P25Phase2AmbeVariantProbe> probes;
    bool probed = false;
};

struct P25Phase2FollowedSlotEvidence {
    bool slotKnown = false;
    size_t targetBursts = 0;
    size_t targetVoiceCodewords = 0;
    size_t targetMaskedBursts = 0;
    size_t targetSuperframeBursts = 0;
    size_t targetMacPdus = 0;
    size_t targetMacCrcValid = 0;
    size_t targetIschDecoded = 0;
    bool targetEssKnown = false;
    bool targetEssEncrypted = false;
    bool targetSessionAudioRelease = false;
    size_t oppositeVoiceCodewords = 0;
};

double defaultBandwidthForMode(DemodMode mode);

double defaultLpfForMode(DemodMode mode);

double lpfForModeAndBandwidth(DemodMode mode, double bandwidthHz);

SmartModeSelection chooseSmartModeAndBandwidth(const std::vector<float>& powerDb,
                                                      double sampleRateHz,
                                                      double centerFreqHz,
                                                      double tunedFreqHz,
                                                      DemodMode requestedMode,
                                                      const SignalRecommendation* preferredRecommendation = nullptr);

RfSquelchMetrics computeRfSquelchMetrics(const std::vector<float>& powerDb,
                                                double sampleRateHz,
                                                double centerFreqHz,
                                                double targetFreqHz,
                                                double channelBwHz,
                                                DemodMode mode);

double applyNfmAfcFromSpectrum(Receiver& rx,
                                      const std::vector<float>& powerDb,
                                      double sampleRateHz,
                                      double centerFreqHz,
                                      double nominalFreqHz,
                                      double channelBwHz,
                                      DemodMode mode);

// DEC-0044: apply device PPM from sustained CC AFC while idle on control.
// Never call during Phase 2 voice follow (mid-park retune fights DEC-0016).
// Returns true when setFrequencyCorrection was invoked.
bool p25MaybeAutoApplyPpmFromControlAfc(size_t deviceIndex,
                                               double controlFreqHz,
                                               double afcOffsetHz,
                                               double afcConfidence,
                                               qint64 nowMs,
                                               QString* logLine);

double p25VoiceAfcTargetHz(const Receiver& rx, double nominalFreqHz, double channelBwHz);

ReceiverSessionKey p25ReceiverSessionKey(const Receiver& rx);

P25P2CallAudioKey p25CurrentPhase2AudioKey(const Receiver& rx, double targetFreqHz) noexcept;

void p25Phase2AdoptGrantSourceIdForCurrentCall(Receiver& rx,
                                                      uint32_t sourceId) noexcept;

P25Phase2SpeakerPendingQueue& p25SpeakerPendingFor(P25SpeakerPendingMap& map,
                                                            const Receiver& rx);

void p25Phase2BindSpeakerPendingToCall(P25Phase2SpeakerPendingQueue& queue,
                                              const Receiver& rx);

void p25Phase2ClearSpeakerPendingQueue(Receiver& rx,
                                              P25Phase2SpeakerPendingQueue& queue,
                                              P25PendingClearReason reason);

void p25Phase2ClearSpeakerPlaybackQueue(Receiver& rx,
                                               P25Phase2SpeakerPendingQueue& pendingSpeaker,
                                               P25PendingClearReason reason);

void p25Phase2ClearStaleResultSpeakerPending(P25SpeakerPendingMap& map,
                                                    const ReceiverSessionKey& sessionKey,
                                                    uint64_t callSessionId,
                                                    Receiver& rx,
                                                    P25PendingClearReason reason);

bool p25Phase2GrantedSlotIsImmutable(const Receiver& rx) noexcept;

void p25Phase2MarkGrantedSlotImmutable(Receiver& rx) noexcept;

void p25ClearPhase2PendingAudio(Receiver& rx);

void p25Phase2ResetTrafficTargetOffset(Receiver& rx) noexcept;

double p25Phase2EffectiveTrafficTargetOffsetHz(const Receiver& rx) noexcept;

double p25Phase2VoiceSchedulerNominalHz(const Receiver& rx) noexcept;

double p25Phase2TrafficSourceCenterHz(const Receiver& rx) noexcept;

double p25TranscriptVoiceLabelHz(const Receiver& rx,
                                        double decoderTargetHz,
                                        double fallbackVoiceHz) noexcept;

bool p25Phase2TargetHardClearEvidence(const P25VoiceAudioBlock& out) noexcept;

bool p25Phase2MacEssStarvedVoiceWindow(const P25VoiceAudioBlock& out) noexcept;

bool p25Phase2BlockHasTrustedClearContext(const P25VoiceAudioBlock& out) noexcept;

bool p25VoiceBlockHasSpeakerTimelineAudio(const P25VoiceAudioBlock& out) noexcept;

bool p25Phase2RollingDecodeWindowConsumed(const P25VoiceAudioBlock& out) noexcept;

bool p25Phase2CleanPlayoutBridgeAnchorWindow(const P25VoiceAudioBlock& out) noexcept;

bool p25Phase2SpeakerOutputCanRefreshFollowActivity(const P25VoiceAudioBlock& out) noexcept;

bool p25Phase2AudioTailGraceActive(const Receiver& rx) noexcept;

void p25Phase2UpdateSessionSustainState(Receiver& rx,
                                               const P25VoiceAudioBlock& out,
                                               qint64 nowMs,
                                               bool speakerEmitted) noexcept;

std::string p25VoiceBlockSpeakerGateReason(const P25VoiceAudioBlock& out);

bool p25VoiceBlockMayEmitAudio(const P25VoiceAudioBlock& out);

bool p25VoiceBlockMayBypassPostArmSettle(const P25VoiceAudioBlock& out,
                                                const std::string& rawSpeakerGateReason) noexcept;

bool p25Phase2ShouldFlushStaleVoicePipeline(const P25VoiceAudioBlock& out) noexcept;

bool p25Phase2ShouldFlushAudioTail(const P25VoiceAudioBlock& out) noexcept;

P25VoiceDiagSnapshot makeP25VoiceDiagnostics(const P25VoiceAudioBlock& out);

int boundedJsonInt(size_t value) noexcept;

void publishP25VoiceDiagnostics(Receiver& rx, const P25VoiceAudioBlock& out, bool publishReceiver = true);

void clearP25VoiceDiagnostics(Receiver& rx);

void clearP25VoiceFollowFieldsLocked(Receiver& rx, bool controlMute);

bool tryApplyP25VoiceResetLocked(Receiver& rx);

P25TrafficProcessorStatusSnapshot snapshotP25TrafficProcessorStatus(const Receiver& rx);

void updateP25FollowGuiStatusCache(const P25FollowGuiStatusCache& snapshot);

bool loadP25FollowGuiStatusCache(P25FollowGuiStatusCache& out, qint64 maxAgeMs = 3000);

bool p25FollowGuiStatusCacheMatchesActiveFollow(const P25FollowGuiStatusCache& cached,
                                                       bool independentTrafficActive,
                                                       uint32_t followTalkgroupId,
                                                       double followVoiceHz,
                                                       uint64_t trafficGeneration) noexcept;

void p25CommitPhase2TrafficMetadataFollow(Receiver& rx,
                                                 const P25TalkgroupEntry& followTg,
                                                 double ccHz,
                                                 qint64 nowMs);

bool p25Phase2ShouldFreezeCqpskDiscrete(const Receiver& rx) noexcept;

bool applyP25Phase2SlotProbeLocked(Receiver& rx, uint8_t newSlot, qint64 nowMs);

void pushAudioFrames(AudioEngine* engine,
                            std::vector<float>& pending,
                            const std::vector<float>& audio,
                            const std::vector<size_t>& activeOutputIndices = {},
                            size_t frameSize = 240,
                            size_t maxPendingSamples = 240 * 20);

size_t pushP25LiveStreamingAudio(AudioEngine* engine,
                                        std::vector<float>& pending,
                                        const std::vector<float>& audio,
                                        const std::vector<size_t>& activeOutputIndices,
                                        size_t frameSize = 240,
                                        double ringFillPercent = -1.0,
                                        bool warmPendingRealAudio = false,
                                        std::vector<float>* pushedRealAudio = nullptr);

size_t pushP25SpeakerAudio(AudioEngine* engine,
                                  std::vector<float>& pending,
                                  const std::vector<float>& audio,
                                  const std::vector<size_t>& activeOutputIndices,
                                  double ringFillPercent = -1.0,
                                  bool warmPendingRealAudio = false,
                                  std::vector<float>* pushedRealAudio = nullptr);

std::vector<float> p25Phase2SpeakerAudioForQueue(
    P25Phase2SpeakerPendingQueue& queue,
    const P25VoiceAudioBlock& block,
    const std::vector<float>& audio,
    size_t frameSize);

void p25Phase2ResetPlayoutBridge(Receiver& rx) noexcept;

void p25Phase2RememberLastEmittedSample(Receiver& rx,
                                               const P25P2CallAudioKey& key,
                                               const std::vector<float>& pcm,
                                               bool armPlayoutBridge) noexcept;

bool p25Phase2ShouldPreserveLivePlaybackBuffers(const Receiver& rx,
                                                       qint64 nowMs) noexcept;

size_t p25TopUpSpeakerPlaybackRing(AudioEngine* engine,
                                          P25SpeakerPendingMap& pendingByRx,
                                          const std::function<bool(const ReceiverSessionKey&)>& sessionActive,
                                          size_t* realAudioPushed = nullptr,
                                          size_t* bridgeAudioPushed = nullptr);

extern std::mutex gCaptureP25PendingMutex;

QString p25Phase2ValidationPath();

bool p25Phase2ValidationLoggingEnabled();

bool p25Phase2ValidationRedactionEnabled();

void clearP25SessionScopedState(Receiver& rx);

bool tryResetP25TrafficSessionNonBlocking(Receiver& rx, const char* reason, bool fullClear = true);

void writeP25Phase2AudioOutputTrace(const Receiver& rx,
                                           const P25VoiceAudioBlock& audio,
                                           const char* context,
                                           bool outputMutedForSettle,
                                           bool speakerMayEmit,
                                           bool engineAvailable,
                                           size_t activeOutputCount,
                                           size_t ringQueuedSamples,
                                           double ringFillPercent,
                                           int underrunCount,
                                           size_t outputSamples,
                                           double outputRateHz);

P25VoiceAudioBlock decodeP25Phase2VoiceBlock(Receiver& rx,
                                                    const P25LiveDecodeResult& live,
                                                    P25VoiceAudioBlock out,
                                                    double sampleRateHz,
                                                    double centerFreqHz,
                                                    double targetFreqHz,
                                                    double outputRateHz,
                                                    uint64_t windowStartAbsDibit,
                                                    bool haveAbsoluteDibits,
                                                    uint64_t freshStartAbsDibit,
                                                    bool haveFreshStartDibits);

P25VoiceAudioBlock decodeP25VoiceAudioBlock(Receiver& rx,
                                                   const std::vector<std::complex<float>>& iq,
                                                   double sampleRateHz,
                                                   double centerFreqHz,
                                                   double targetFreqHz,
                                                   double outputRateHz,
                                                   uint64_t iqStartAbsolute = 0,
                                                   bool iqStartAbsoluteKnown = false,
                                                   size_t contextIqSamples = 0);

