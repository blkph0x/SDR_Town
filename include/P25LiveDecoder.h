#pragma once

#include <array>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

enum class P25DataUnitId : uint8_t {
    HDU = 0x0,
    TDU = 0x3,
    LDU1 = 0x5,
    TSDU = 0x7,
    LDU2 = 0xA,
    PDU = 0xC,
    TDULC = 0xF,
    Unknown = 0xFF,
};

enum class P25VoiceDecodeStatus {
    Decoded,
    BackendUnavailable,
    InvalidFrame,
};

enum class P25Phase2BurstKind {
    Unknown,
    Voice4,
    Voice2,
    SacchScrambled,
    FacchScrambled,
    SacchClear,
    FacchClear,
    LcchClear,
};

struct P25LiveDecoderConfig {
    double symbolRate = 4800.0;
    double workSampleRate = 48000.0;
    double channelBandwidthHz = 12500.0;
    double c4fmInnerDeviationHz = 600.0;
    int maxFrameSyncBitErrors = 2;
    size_t maxFrameSyncs = 8;
    size_t maxRawTsbkBlocksPerFrame = 8;
    bool enableC4fmFixedPhaseSearch = false;
    size_t maxC4fmFixedPhaseCandidates = 10;
    bool stopC4fmSearchOnHardLock = false;
    bool enableFrontEndDcBlock = true;
    double frontEndDcBlockAlpha = 0.00025;
    bool enableCqpskSearch = true;
    bool enableCqpskCarrierLoop = true;
    double cqpskCarrierLoopBandwidth = 0.040;
    double cqpskCarrierLoopMaxCorrectionHz = 1800.0;
    bool stopCqpskSearchOnHardLock = true;
    size_t maxCqpskSearchCandidates = 0;
    size_t cqpskLockMissTolerance = 24;
    bool realtimeVoiceSearch = false;
    bool enablePhase1Decode = true;
    int realtimeDecodeBudgetMs = 0;
    size_t maxPhase2SyncHits = 0;
    size_t maxPhase2SuperframeLocks = 0;
    // Phase 2 traffic-channel demod: prefer CQPSK/H-DQPSK search over C4FM
    // fallback.  Profile flag is independent of symbolRate; Phase-2 air rate is
    // 6000 sps (do not reuse 4800 as a demod-mode sentinel).
    bool phase2CqpskTrafficDemod = false;
    // Optional RRC matched filter.  SDRTrunk P25P2DecoderHDQPSK uses LPF+AGC
    // only (no RRC); leave false for Phase-2 traffic parity.  P1 LSM may enable.
    bool cqpskUseMatchedRrcFilter = false;
    double cqpskRrcAlpha = 0.20;
    // When an external/trusted clear Phase-2 grant already proves the call is
    // safe to release, allow repeated low-error AMBE voice evidence to lock the
    // XOR mask phase/superframe lattice. Unknown/encrypted calls keep this off
    // and still require MAC/ESS/ISCH standards proof before speaker release.
    bool allowPhase2SoftAmbeMaskPhaseLock = false;
    bool phase2PreferredTdmaSlotKnown = false;
    uint8_t phase2PreferredTdmaSlot = 0;
    // Phase 2 burst/MAC decode is enabled by default so realtime control
    // monitoring and offline diagnostics share the same grant evidence path.
    // Low-power views may opt out explicitly when they only need Phase 1 TSBK.
    bool enablePhase2Decode = true;
};


struct P25BlockTimingState {
    bool c4fmValid = false;
    bool cqpskValid = false;
    double c4fmOmega = 0.0;
    double c4fmMu = 0.0;
    double cqpskOmega = 0.0;
    double cqpskMu = 0.0;
    double c4fmSampleRate = 0.0;
    double cqpskSampleRate = 0.0;
    bool cqpskCarrierLoopValid = false;
    double cqpskCarrierLoopPhase = 0.0;
    double cqpskCarrierLoopOmega = 0.0;
    std::vector<float> c4fmTail;
    std::vector<std::complex<float>> cqpskTail;
};

struct P25Phase2MaskParameters {
    bool valid = false;
    uint16_t nac = 0;
    uint32_t wacn = 0;
    uint16_t systemId = 0;
};

struct P25Phase2EssState {
    bool known = false;
    bool encrypted = false;
    uint8_t algId = 0x80;
    uint16_t keyId = 0;
    std::array<uint8_t, 9> messageIndicator{};
    bool fecValidated = false;
    int correctedSymbols = 0;
};

struct P25Phase2MacPdu {
    bool valid = false;
    size_t dibitOffset = 0;
    P25Phase2BurstKind detectedKind = P25Phase2BurstKind::Unknown;
    P25Phase2BurstKind source = P25Phase2BurstKind::Unknown;
    uint8_t opcode = 0;
    uint8_t offset = 0;
    bool fecDecoded = false;
    bool crcValid = false;
    bool directCrcOk = false;
    bool directCrcParseable = false;
    bool rsDecoded = false;
    int correctedSymbols = 0;
    bool acchHypothesisKnown = false;
    bool acchBitOrderSwapped = false;
    bool acchDibitInverted = false;
    int acchSlipDibits = 0;
    std::vector<uint8_t> bytes;
    bool essPresent = false;
    P25Phase2EssState ess;
};

struct P25Phase2IschState {
    bool valid = false;
    bool sync = false;
    size_t dibitOffset = 0;
    int errors = -1;
    uint8_t channel = 0;
    uint8_t location = 0;
    bool freeAccess = false;
    uint8_t ultraframeCounter = 0;
};

struct P25FrameSyncEvent {
    size_t bitOffset = 0;
    bool inverted = false;
    int bitErrors = 0;
    double confidence = 0.0;
};

struct P25Nid {
    bool valid = false;
    size_t bitOffset = 0;
    uint16_t nac = 0;
    P25DataUnitId duid = P25DataUnitId::Unknown;
    uint8_t rawDuid = 0;
    uint64_t raw = 0;
    bool fecValidated = false;
    int correctedBitErrors = 0;
    bool downstreamValidated = false;
    int downstreamScore = 0;
};

struct P25TsbkBlock {
    size_t bitOffset = 0;
    std::vector<uint8_t> bytes;
    bool crcPresent = true;
    bool crcValid = false;
    bool crcCorrected = false;
    bool crcPassedInvertedResidual = false;
    int crcCorrectedBits = 0;
    bool fecDecoded = false;
    int correctedDibitErrors = 0;
};

struct P25Phase1PduDataBlock {
    size_t bitOffset = 0;
    std::vector<uint8_t> bytes;
    bool fecDecoded = false;
    int correctedDibitErrors = 0;
};

struct P25Phase1PduMessage {
    size_t bitOffset = 0;
    std::vector<uint8_t> headerBytes;
    bool headerFecDecoded = false;
    bool headerCrcValid = false;
    bool headerCrcCorrected = false;
    bool headerCrcPassedInvertedResidual = false;
    int headerCrcCorrectedBits = 0;
    int headerCorrectedDibitErrors = 0;
    uint8_t format = 0;
    uint8_t vendor = 0;
    uint8_t opcode = 0;
    uint8_t blocksToFollow = 0;
    uint32_t logicalLinkId = 0;
    bool outbound = false;
    std::vector<P25Phase1PduDataBlock> dataBlocks;
};

struct P25LiveDecoderStats {
    size_t inputSamples = 0;
    size_t discriminatorSamples = 0;
    size_t symbols = 0;
    size_t bits = 0;
    size_t frameSyncs = 0;
    int bestFrameSyncBitErrors = -1;
    size_t bestFrameSyncBitOffset = 0;
    bool bestFrameSyncInverted = false;
    bool bestFrameSyncBitAligned = false;
    int bestNidBchDistance = -1;
    bool bestNidValid = false;
    uint16_t bestNidNac = 0;
    uint8_t bestNidRawDuid = 0;
    double sampleRate = 0.0;
    double channelSampleRate = 0.0;
    double inputTargetOffsetHz = 0.0;
    double discriminatorMeanHz = 0.0;
    double symbolRate = 4800.0;
    double symbolConfidence = 0.0;
    size_t softDecisionSymbols = 0;
    double softDecisionQuality = 0.0;
    double softBitLlrMean = 0.0;
    double softBitLlrMinimum = 0.0;
    size_t softLowConfidenceSymbols = 0;
    bool voiceBackendAvailable = false;
    size_t phase2Bursts = 0;
    size_t phase2VoiceCodewords = 0;
    size_t phase2SuperframeBursts = 0;
    size_t phase2MaskedBursts = 0;
    size_t phase2MacPdus = 0;
    size_t phase2MacCrcValid = 0;
    size_t phase2MacFecDecoded = 0;
    size_t phase2MacDirectCrcValid = 0;
    size_t phase2MacDirectCrcRejected = 0;
    size_t phase2MacRsDecoded = 0;
    size_t phase1PduHeaders = 0;
    size_t phase1PduCrcValid = 0;
    size_t phase1AmbtcPdus = 0;
    size_t phase2MacNominalCrcValid = 0;
    size_t phase2MacAltKindCrcValid = 0;
    size_t phase2MacBitSwapCrcValid = 0;
    size_t phase2MacSlipCrcValid = 0;
    size_t phase2MacInvertCrcValid = 0;
    bool phase2EssKnown = false;
    bool phase2EssEncrypted = false;
    bool phase2MaskParametersKnown = false;
    bool phase2MaskPhaseKnown = false;
    uint8_t phase2MaskPhase = 0;
    int phase2MaskPhaseScore = 0;
    size_t phase2MaskPhaseMacCrcValid = 0;
    bool cqpskLockActive = false;
    bool cqpskLockUsed = false;
    bool cqpskLockUpdated = false;
    double cqpskSymbolPhaseFraction = 0.0;
    bool cqpskFineCorrectionApplied = false;
    double cqpskFineRotationRad = 0.0;
    double cqpskResidualCarrierHz = 0.0;
    double cqpskPhaseErrorRmsRad = 0.0;
    size_t cqpskFineCorrectionSymbols = 0;
    int cqpskLockTrustScore = 0;
    int cqpskLockMisses = 0;
    bool cqpskStickyOverride = false;
    bool frontEndDcBlockApplied = false;
    double frontEndDcEstimateMagnitude = 0.0;
    bool cqpskCarrierLoopApplied = false;
    double cqpskCarrierLoopCorrectionHz = 0.0;
    double cqpskCarrierLoopPhaseErrorRmsRad = 0.0;
    size_t cqpskCarrierLoopSymbols = 0;
    size_t phase2IschDecoded = 0;
    size_t phase2IschSync = 0;
    size_t phase2SyncOffsetCorrections = 0;
    int phase2SyncOffsetCorrectionDibits = 0;
    int bestPhase2SyncErrors = -1;
    size_t bestPhase2SyncDibitOffset = 0;
    std::string demodPath;
};

struct P25ImbeFrame {
    bool valid = false;
    size_t bitOffset = 0;
    std::array<uint8_t, 11> imbe88{};
    int correctedErrors = 0;
    std::string message;
};

struct P25Phase2VoiceCodeword {
    size_t dibitOffset = 0;
    uint8_t voiceIndex = 0;
    bool sessionCodewordIdKnown = false;
    uint64_t sessionCodewordId = 0;
    bool duplicateInSession = false;
    std::array<uint8_t, 72> bits{};
};

struct P25Phase2Burst {
    bool valid = false;
    size_t dibitOffset = 0;
    int syncErrors = -1;
    bool superframeLocked = false;
    // True when this isolated burst was mapped to a retained streaming
    // superframe epoch instead of a fresh 12-burst lock in the current window.
    bool stickySuperframe = false;
    size_t superframeDibitOffset = 0;
    int superframeSyncScore = 0;
    int superframeSyncErrors = 0;
    bool phase2AudioLock = false;
    bool tdmaSyncLock = false;
    bool superframeLock = false;
    bool maskPhaseLock = false;
    bool macCrcLock = false;
    bool sessionAudioRelease = false;
    bool securityStateFromPtt = false;
    bool macPttSeen = false;
    bool macActiveSeen = false;
    bool macEndPttSeen = false;
    bool macIdleSeen = false;
    bool macHangtimeSeen = false;
    bool trafficSecurityKnown = false;
    bool trafficEncrypted = false;
    bool trafficTalkgroupKnown = false;
    uint32_t trafficTalkgroupId = 0;
    bool superframeBurstIndexKnown = false;
    uint8_t superframeBurstIndex = 0;
    bool syncOffsetAdjusted = false;
    int syncOffsetDibits = 0;
    bool grantSlotKnown = false;
    uint8_t grantSlot = 0;
    P25Phase2IschState isch;
    uint8_t rawDuidCodeword = 0;
    int duid = -1;
    int duidErrors = -1;
    P25Phase2BurstKind kind = P25Phase2BurstKind::Unknown;
    bool xorMaskApplied = false;
    bool xorMaskPhaseKnown = false;
    uint8_t xorMaskPhase = 0;
    int xorMaskPhaseScore = 0;
    bool macFecDecoded = false;
    bool macCrcValid = false;
    bool essKnown = false;
    bool encrypted = false;
    std::vector<int> rawPayloadDibits;
    std::vector<int> maskedPayloadDibits;
    std::vector<P25Phase2VoiceCodeword> voiceCodewords;
};

struct P25LiveDecodeResult {
    std::vector<int> dibits;
    std::vector<uint8_t> bits;
    std::vector<P25FrameSyncEvent> syncs;
    std::vector<P25Nid> nids;
    std::vector<P25TsbkBlock> rawTsbkBlocks;
    std::vector<P25Phase1PduMessage> phase1Pdus;
    std::vector<P25ImbeFrame> imbeFrames;
    std::vector<P25Phase2Burst> phase2Bursts;
    std::vector<P25Phase2MacPdu> phase2MacPdus;
    P25Phase2EssState phase2Ess;
    P25LiveDecoderStats stats;
    std::vector<std::string> warnings;
};

struct P25Phase2DecodeResult {
    std::vector<P25Phase2Burst> bursts;
    std::vector<P25Phase2MacPdu> macPdus;
    P25Phase2EssState ess;
};

struct P25VoiceDecodeResult {
    P25VoiceDecodeStatus status = P25VoiceDecodeStatus::BackendUnavailable;
    std::vector<float> pcm;
    double sampleRate = 8000.0;
    int errors = 0;
    int totalErrors = 0;
    std::string message;
};

P25ImbeFrame p25DecodeImbeFrameFromVoiceDibits(const std::vector<int>& voiceFrameDibits);
std::array<uint8_t, 96> p25Phase2VoiceCodewordToAmbe3600x2450Frame(const P25Phase2VoiceCodeword& codeword);
std::array<uint8_t, 96> p25Phase2VoiceCodewordToAmbe3600x2450FrameVariant(const P25Phase2VoiceCodeword& codeword, int variant);
int p25Phase2AmbeFrameVariantCount();
uint64_t p25EncodeNidBch(uint16_t nac, P25DataUnitId duid);

class P25LiveDecoder {
public:
    static constexpr uint64_t FrameSyncWord = 0x5575F5FF77FFull;
    static constexpr size_t FrameSyncBits = 48;
    static constexpr size_t NidBits = 64;
    static constexpr uint64_t Phase2FrameSyncWord = 0x575D57F7FFull;
    static constexpr size_t Phase2FrameSyncBits = 40;
    static constexpr size_t Phase2FrameSyncDibits = Phase2FrameSyncBits / 2;
    static constexpr size_t Phase2BurstDibits = 180;
    static constexpr double SymbolRate = 4800.0;

    explicit P25LiveDecoder(P25LiveDecoderConfig config = {});
    ~P25LiveDecoder();

    P25LiveDecoder(const P25LiveDecoder& other);
    P25LiveDecoder& operator=(const P25LiveDecoder& other);
    P25LiveDecoder(P25LiveDecoder&& other) noexcept;
    P25LiveDecoder& operator=(P25LiveDecoder&& other) noexcept;

    void reset();

    // Independent copy for offset/slot/CQPSK probe paths.  Retains demod
    // configuration and optionally Phase-2 mask parameters, but never copies
    // streaming timing tails, CQPSK lock, ESS/MAC session state, superframe
    // anchors, or codeword de-dupe history from the parent decoder.
    P25LiveDecoder createIndependentProbeCopy(bool retainPhase2MaskParameters = true) const;

    P25LiveDecodeResult processIq(const std::vector<std::complex<float>>& iq,
                                  double sampleRate,
                                  double centerFreqHz,
                                  double targetFreqHz);
    P25LiveDecodeResult processFmDiscriminator(const std::vector<float>& discriminatorHz,
                                                double sampleRate);
    P25LiveDecodeResult processHardDibits(const std::vector<int>& dibits);
    P25LiveDecodeResult processHardBits(const std::vector<uint8_t>& bits);
    std::vector<P25Phase2Burst> processPhase2HardDibits(const std::vector<int>& dibits);
    P25Phase2DecodeResult processPhase2HardDibitsDetailed(const std::vector<int>& dibits);

    void setPhase2MaskParameters(uint16_t nac, uint32_t wacn, uint16_t systemId);
    void clearPhase2MaskParameters();
    bool phase2MaskParametersKnown() const;
    bool phase2MaskParametersMatch(uint16_t nac, uint32_t wacn, uint16_t systemId) const;
    const P25LiveDecoderConfig& config() const { return m_config; }
    void setRealtimeDecodeBudgetMs(int budgetMs) { m_config.realtimeDecodeBudgetMs = std::max(0, budgetMs); }
    void setMaxCqpskSearchCandidates(size_t maxCandidates) { m_config.maxCqpskSearchCandidates = maxCandidates; }
    void setAllowPhase2SoftAmbeMaskPhaseLock(bool allow) { m_config.allowPhase2SoftAmbeMaskPhaseLock = allow; }
    void setPhase2PreferredTdmaSlot(bool known, uint8_t slot) {
        m_config.phase2PreferredTdmaSlotKnown = known;
        m_config.phase2PreferredTdmaSlot = static_cast<uint8_t>(slot & 0x01u);
    }
    bool cqpskLockValid() const noexcept { return m_cqpskLock.valid; }

    // Align the internal Phase-2 dibit stream counter to an absolute ring
    // cursor before processing a new traffic-channel chunk.
    void alignPhase2AbsoluteDibitCursor(uint64_t chunkStartAbsolute, size_t chunkDibitCount);

    static std::array<uint8_t, FrameSyncBits> frameSyncBits();
    static std::array<int, Phase2FrameSyncDibits> phase2FrameSyncDibits();
    static std::array<int, Phase2BurstDibits * 12> phase2XorMaskDibits(uint16_t nac,
                                                                       uint32_t wacn,
                                                                       uint16_t systemId);
    static int dibitFromBits(uint8_t first, uint8_t second);
    static std::array<uint8_t, 2> bitsFromDibit(int dibit);
    static double nominalC4fmLevelForDibit(int dibit);
    static std::string dataUnitIdToString(P25DataUnitId duid);
    static std::string phase2BurstKindToString(P25Phase2BurstKind kind);

private:
    struct CqpskDemodLock {
        bool valid = false;
        bool differential = true;
        bool conjugate = false;
        double rotation = 0.0;
        std::array<int, 4> permutation{0, 1, 2, 3};
        double symbolPhaseFraction = 0.5;
        double fineRotation = 0.0;
        double residualCarrierHz = 0.0;
        double phaseErrorRmsRad = 0.0;
        size_t fineCorrectionSymbols = 0;
        int trustScore = 0;
        int misses = 0;
    };

    P25LiveDecoderConfig m_config;
    struct RecentPhase2Codeword {
        uint64_t streamDibit = 0;
        uint64_t fingerprint = 0;
        uint64_t id = 0;
        uint64_t generation = 0;
    };

    P25BlockTimingState streamTimingStateSnapshot() const;
    void storeStreamTimingState(const P25BlockTimingState& timing);
    std::vector<uint8_t> buildPhase1BitStreamWithTail(const std::vector<uint8_t>& inputBits,
                                                      size_t& prefixBits) const;
    void storePhase1BitTail(const std::vector<uint8_t>& bits);
    std::deque<uint8_t> snapshotPhase1BitTail() const;
    void restorePhase1BitTail(const std::deque<uint8_t>& snapshot);

    P25LiveDecodeResult processHardDibitsInternal(const std::vector<int>& dibits,
                                                  bool annotateSessionCodewords);
    P25LiveDecodeResult processFmDiscriminatorInternal(const std::vector<float>& discriminatorHz,
                                                       double sampleRate,
                                                       bool annotateSessionCodewords);
    P25Phase2DecodeResult processPhase2HardDibitsDetailedInternal(const std::vector<int>& dibits,
                                                                  bool annotateSessionCodewords);
    void annotatePhase2SessionCodewords(P25Phase2DecodeResult& out,
                                        const std::vector<int>& dibits);

    mutable std::mutex m_streamingStateMutex;
    P25BlockTimingState m_streamTimingState;
    std::deque<uint8_t> m_phase1BitTail;
    P25Phase2MaskParameters m_phase2MaskParams;
    std::array<int, Phase2BurstDibits * 12> m_phase2XorMask{};
    // Retained Phase-2 session state.  The legacy single-session fields below are
    // kept as the public/summary view, but the live decoder now also retains one
    // independent session per logical TDMA traffic slot.  A Phase-2 RF carrier
    // carries TS1 and TS2 simultaneously; sharing ESS/MAC/PTT state between those
    // slots lets one call poison the other.  sdrtrunk keeps separate Phase-2
    // timeslot processors, so we mirror that ownership here.
    P25Phase2EssState m_phase2Ess;
    std::array<uint8_t, 16> m_phase2EssB{};
    std::array<bool, 4> m_phase2EssBSeen{};
    uint8_t m_phase2EssBNext = 0;
    bool m_phase2SessionMacCrcSeen = false;
    int m_phase2First4vSlot = -1;
    std::array<P25Phase2EssState, 5> m_phase2EssHypotheses{};
    std::array<std::array<uint8_t, 16>, 5> m_phase2EssBHypotheses{};
    std::array<std::array<bool, 4>, 5> m_phase2EssBSeenHypotheses{};
    std::array<P25Phase2EssState, 2> m_phase2SlotEss{};
    std::array<std::array<uint8_t, 16>, 2> m_phase2SlotEssB{};
    std::array<std::array<bool, 4>, 2> m_phase2SlotEssBSeen{};
    std::array<uint8_t, 2> m_phase2SlotEssBNext{};
    std::array<bool, 2> m_phase2SlotSessionMacCrcSeen{};
    std::array<int, 2> m_phase2SlotFirst4vSlot{{-1, -1}};
    std::array<std::array<P25Phase2EssState, 5>, 2> m_phase2SlotEssHypotheses{};
    std::array<std::array<std::array<uint8_t, 16>, 5>, 2> m_phase2SlotEssBHypotheses{};
    std::array<std::array<std::array<bool, 4>, 5>, 2> m_phase2SlotEssBSeenHypotheses{};
    bool m_phase2MaskPhaseKnown = false;
    uint8_t m_phase2MaskPhase = 0;
    int m_phase2MaskPhaseScore = 0;
    // Consecutive annotate windows where a sticky mask phase produced voice/superframe
    // evidence but zero MAC CRC and no ESS.  Field logs show wrong sticky phases can
    // permanently mute clear grants (p2sf/p2mask high, p2mac=0/N, AMBE never decodes).
    // After a short starve streak, re-open the 12-phase hunt (SDRTrunk never sticks to a
    // wrong scrambling segment because its continuous framer re-validates continuously).
    uint8_t m_phase2MaskPhaseStarveWindows = 0;
    // Sticky Phase-2 superframe epoch used for late-entry/live scanner follow.
    // sdrtrunk's traffic decoder is a continuous stream, so a single voice
    // timeslot after acquisition still has a known superframe index and XOR
    // mask segment.  Our rolling-window scanner path can see only one burst in
    // a window; retain the absolute stream dibit of burst-0 from the last
    // trusted 12-burst lock so those isolated bursts can still be descrambled
    // and mapped to the granted slot.
    bool m_phase2SuperframeAnchorKnown = false;
    uint64_t m_phase2SuperframeAnchorDibit = 0;
    uint64_t m_phase2SuperframeAnchorGeneration = 0;
    P25Phase2MaskParameters m_phase2SuperframeAnchorMaskParams{};
    uint8_t m_phase2SuperframeAnchorMaskPhase = 0;
    std::deque<RecentPhase2Codeword> m_phase2RecentCodewords;
    std::deque<int> m_phase2DibitTail;
    uint64_t m_phase2NextCodewordId = 1;
    uint64_t m_phase2DecodeGeneration = 0;
    uint64_t m_phase2StreamDibits = 0;
    CqpskDemodLock m_cqpskLock;
    bool m_frontEndDcEstimateValid = false;
    double m_frontEndDcSampleRate = 0.0;
    std::complex<double> m_frontEndDcEstimate{0.0, 0.0};
};

class P25ImbeVoiceDecoder {
public:
    P25ImbeVoiceDecoder();
    ~P25ImbeVoiceDecoder();

    P25ImbeVoiceDecoder(const P25ImbeVoiceDecoder&) = delete;
    P25ImbeVoiceDecoder& operator=(const P25ImbeVoiceDecoder&) = delete;
    P25ImbeVoiceDecoder(P25ImbeVoiceDecoder&&) noexcept;
    P25ImbeVoiceDecoder& operator=(P25ImbeVoiceDecoder&&) noexcept;

    bool backendAvailable() const;
    P25VoiceDecodeResult decodeImbe4400Frame(const std::array<uint8_t, 11>& imbe88);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

class P25AmbeVoiceDecoder {
public:
    P25AmbeVoiceDecoder();
    ~P25AmbeVoiceDecoder();

    P25AmbeVoiceDecoder(const P25AmbeVoiceDecoder&) = delete;
    P25AmbeVoiceDecoder& operator=(const P25AmbeVoiceDecoder&) = delete;
    P25AmbeVoiceDecoder(P25AmbeVoiceDecoder&&) noexcept;
    P25AmbeVoiceDecoder& operator=(P25AmbeVoiceDecoder&&) noexcept;

    bool backendAvailable() const;
    P25VoiceDecodeResult decodeAmbe2450Data(const std::array<uint8_t, 49>& ambe49);
    P25VoiceDecodeResult decodeAmbe3600x2450Frame(const std::array<uint8_t, 96>& ambe96);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
