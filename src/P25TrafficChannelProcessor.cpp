#include "P25TrafficChannelProcessor.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <vector>

namespace {

uint64_t steadyNowMs()
{
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

} // namespace

P25TrafficChannelProcessor::P25TrafficChannelProcessor(uint64_t sessionId, uint32_t talkgroup, uint32_t voiceFreqHz, int grantedSlot)
    : m_sessionId(sessionId)
    , m_talkgroup(talkgroup)
    , m_voiceFreqHz(voiceFreqHz)
    , m_grantedSlot(grantedSlot)
    , m_decoder(P25LiveDecoderConfig{})
{
    P25LiveDecoderConfig cfg;
    // SDRTrunk P25P2DecoderHDQPSK: 6000 baud H-DQPSK, LPF-only (no RRC).
    cfg.symbolRate = 6000.0;
    cfg.workSampleRate = 48000.0;
    cfg.enablePhase2Decode = true;
    cfg.enablePhase1Decode = false;
    cfg.phase2CqpskTrafficDemod = true;
    cfg.cqpskUseMatchedRrcFilter = false;
    cfg.cqpskRrcAlpha = 0.20;
    cfg.cqpskCarrierLoopBandwidth = (2.0 * 3.14159265358979323846) / 300.0;
    cfg.cqpskCarrierLoopMaxCorrectionHz = 3000.0;
    cfg.realtimeVoiceSearch = true;
    cfg.maxFrameSyncBitErrors = 4;
    cfg.maxFrameSyncs = 6;
    cfg.maxRawTsbkBlocksPerFrame = 4;
    cfg.realtimeDecodeBudgetMs = 220;
    cfg.maxPhase2SyncHits = 96;
    cfg.maxPhase2SuperframeLocks = 4;
    m_decoder = P25LiveDecoder(cfg);

    const uint64_t now = steadyNowMs();
    m_createdMs.store(now, std::memory_order_release);
    m_lastActiveMs.store(now, std::memory_order_release);
}

P25TrafficChannelProcessor::~P25TrafficChannelProcessor() = default;

void P25TrafficChannelProcessor::feedHardDibits(const std::vector<int>& dibits, uint64_t absoluteDibitIndex)
{
    if (dibits.empty() || m_teardownRequested.load()) return;

    P25LiveDecodeResult result;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_decoder.alignPhase2AbsoluteDibitCursor(absoluteDibitIndex, dibits.size());
        result = m_decoder.processHardDibits(dibits);
    }
    observeDecodeResult(result, absoluteDibitIndex + dibits.size());
}

void P25TrafficChannelProcessor::processDibits(const int16_t* dibits, size_t count, uint64_t absoluteDibitIndex)
{
    if (!dibits || count == 0 || m_teardownRequested.load()) return;

    m_hardDibitScratch.clear();
    m_hardDibitScratch.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        m_hardDibitScratch.push_back(static_cast<int>(dibits[i]) & 0x03);
    }
    feedHardDibits(m_hardDibitScratch, absoluteDibitIndex);
}

void P25TrafficChannelProcessor::observeDecodeResult(const P25LiveDecodeResult& result, uint64_t absoluteDibitIndex)
{
    size_t burstVoiceCodewords = 0;
    size_t meaningfulVoiceCodewords = 0;
    bool sessionAudioRelease = false;
    bool burstEssKnown = false;
    bool burstEncrypted = false;
    bool macPttSeen = false;
    bool macActiveSeen = false;
    bool macEndPttSeen = false;
    bool macIdleSeen = false;
    bool macHangtimeSeen = false;
    for (const auto& burst : result.phase2Bursts) {
        burstVoiceCodewords += burst.voiceCodewords.size();
        // Only count as meaningful voice activity (for lifetime / "active" tracking) if it has
        // descrambling (mask) + some lock (superframe or mac or ess or session). This prevents
        // background noise or garbage dibits from being treated as "real data" keeping us stuck
        // on an inactive TG.
        const bool goodVoiceEvidence = burst.xorMaskApplied &&
            (!burst.voiceCodewords.empty() ||
             burst.superframeLock || burst.maskPhaseLock || burst.macCrcLock ||
             burst.essKnown || burst.sessionAudioRelease || burst.macPttSeen || burst.macActiveSeen);
        if (goodVoiceEvidence) {
            meaningfulVoiceCodewords += burst.voiceCodewords.size();
        }
        sessionAudioRelease = sessionAudioRelease || burst.sessionAudioRelease;
        burstEssKnown = burstEssKnown || burst.essKnown;
        burstEncrypted = burstEncrypted || (burst.essKnown && burst.encrypted);
        macPttSeen = macPttSeen || burst.macPttSeen;
        macActiveSeen = macActiveSeen || burst.macActiveSeen;
        macEndPttSeen = macEndPttSeen || burst.macEndPttSeen;
        macIdleSeen = macIdleSeen || burst.macIdleSeen;
        macHangtimeSeen = macHangtimeSeen || burst.macHangtimeSeen;
    }

    const int p2vcw = static_cast<int>(std::min<size_t>(
        std::max(result.stats.phase2VoiceCodewords, burstVoiceCodewords),
        static_cast<size_t>(std::numeric_limits<int>::max())));
    const int p2bursts = static_cast<int>(std::min<size_t>(
        std::max(result.stats.phase2Bursts, result.phase2Bursts.size()),
        static_cast<size_t>(std::numeric_limits<int>::max())));
    const int p2macPdus = static_cast<int>(std::min<size_t>(
        std::max(result.stats.phase2MacPdus, result.phase2MacPdus.size()),
        static_cast<size_t>(std::numeric_limits<int>::max())));
    const int p2macCrcValid = static_cast<int>(std::min<size_t>(
        result.stats.phase2MacCrcValid,
        static_cast<size_t>(std::numeric_limits<int>::max())));
    const int p2sf = static_cast<int>(std::min<size_t>(
        result.stats.phase2SuperframeBursts,
        static_cast<size_t>(std::numeric_limits<int>::max())));
    const int p2mask = static_cast<int>(std::min<size_t>(
        result.stats.phase2MaskedBursts,
        static_cast<size_t>(std::numeric_limits<int>::max())));

    const bool essKnown = result.stats.phase2EssKnown || result.phase2Ess.known || burstEssKnown;
    const bool encrypted = result.stats.phase2EssEncrypted ||
        (result.phase2Ess.known && result.phase2Ess.encrypted) ||
        burstEncrypted;
    const bool clearEss = essKnown && !encrypted;
    const bool callEnded = macEndPttSeen || macIdleSeen || macHangtimeSeen;
    const bool audioOpen = !callEnded && !encrypted && (sessionAudioRelease || clearEss || (meaningfulVoiceCodewords > 0 && p2mask > 0));
    const uint64_t now = steadyNowMs();

    m_p2bursts.store(p2bursts, std::memory_order_release);
    // For activity/lifetime use the filtered meaningful count; the raw p2vcw is still available via other stats if needed for UI.
    size_t effectiveP2vcw = (meaningfulVoiceCodewords > p2vcw) ? meaningfulVoiceCodewords : p2vcw;
    m_p2vcw.store(meaningfulVoiceCodewords > 0 ? effectiveP2vcw : p2vcw, std::memory_order_release);
    m_p2mac.store(p2macCrcValid, std::memory_order_release);
    m_p2macPdus.store(p2macPdus, std::memory_order_release);
    m_p2macCrcValid.store(p2macCrcValid, std::memory_order_release);
    m_p2sf.store(p2sf, std::memory_order_release);
    m_p2mask.store(p2mask, std::memory_order_release);
    m_sfLocked.store(p2sf >= 4, std::memory_order_release);
    m_maskLocked.store(result.stats.phase2MaskPhaseKnown && p2mask >= 4, std::memory_order_release);
    m_macTrusted.store(p2macCrcValid > 0, std::memory_order_release);
    m_essTrusted.store(essKnown, std::memory_order_release);
    m_encrypted.store(encrypted, std::memory_order_release);
    m_audioOpen.store(audioOpen, std::memory_order_release);
    m_lastAbsoluteDibit.store(absoluteDibitIndex, std::memory_order_release);
    if (macPttSeen) m_macPttSeen.store(true, std::memory_order_release);
    if (macActiveSeen) m_macActiveSeen.store(true, std::memory_order_release);
    if (macEndPttSeen) m_macEndPttSeen.store(true, std::memory_order_release);
    if (macIdleSeen) m_macIdleSeen.store(true, std::memory_order_release);
    if (macHangtimeSeen) m_macHangtimeSeen.store(true, std::memory_order_release);
    if (callEnded) {
        m_callEnded.store(true, std::memory_order_release);
        m_audioOpen.store(false, std::memory_order_release);
        m_endedMs.store(now, std::memory_order_release);
        std::lock_guard<std::mutex> lock(m_mutex);
        if (macEndPttSeen) m_endReason = "end-ptt";
        else if (macIdleSeen) m_endReason = "idle";
        else if (macHangtimeSeen) m_endReason = "hangtime";
    } else if (p2vcw > 0 || macPttSeen || macActiveSeen || sessionAudioRelease || audioOpen) {
        m_callEnded.store(false, std::memory_order_release);
        m_endedMs.store(0, std::memory_order_release);
        std::lock_guard<std::mutex> lock(m_mutex);
        m_endReason.clear();
    }
    if (meaningfulVoiceCodewords > 0 || burstVoiceCodewords > 0) {
        m_lastVoiceMs.store(now, std::memory_order_release);
    }
    if (p2macPdus > 0 || p2macCrcValid > 0 || macPttSeen || macActiveSeen || callEnded) {
        m_lastMacMs.store(now, std::memory_order_release);
    }
    if (essKnown) {
        m_lastEssMs.store(now, std::memory_order_release);
    }

    // Voice-call lifetime evidence should be driven by actual voice activity (VCW or voice MAC/ESS release),
    // not just framing (sf/mask) or any bursts. Framing telemetry can persist after a call ends or on
    // an inactive TG, causing "stuck on inactive talkgroup".
    // Matches SDRTrunk behavior where traffic channel lifetime is tied to voice frames and call state (PTT/end).
    const bool voiceCallEvidence =
        meaningfulVoiceCodewords > 0 ||
        macPttSeen || macActiveSeen || sessionAudioRelease || (audioOpen && meaningfulVoiceCodewords > 0) ||
        (burstVoiceCodewords > 0 && p2mask > 0);  // keep active on descrambled voice presence for established calls
    if (voiceCallEvidence) {
        m_lastActiveMs.store(now, std::memory_order_release);
    } else if ((p2sf > 0 || p2mask > 0) && m_lastVoiceMs.load(std::memory_order_acquire) == 0) {
        // Only during very early acquisition (no voice seen yet) allow sf/mask to bootstrap the active timer.
        // Once any voice VCW has been seen, only voice evidence refreshes it.
        m_lastActiveMs.store(now, std::memory_order_release);
    }
}

void P25TrafficChannelProcessor::setPhase2MaskParameters(uint16_t nac, uint32_t wacn, uint16_t systemId)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_decoder.setPhase2MaskParameters(nac, wacn, systemId);
}

void P25TrafficChannelProcessor::clearPhase2MaskParameters()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_decoder.clearPhase2MaskParameters();
}

bool P25TrafficChannelProcessor::isCallStillActive() const
{
    if (m_teardownRequested.load()) return false;
    const uint64_t now = steadyNowMs();
    if (m_callEnded.load(std::memory_order_acquire)) {
        const uint64_t ended = m_endedMs.load(std::memory_order_acquire);
        if (ended == 0 || now < ended) return false;
        std::string reason;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            reason = m_endReason;
        }
        const uint64_t hold = reason == "hangtime" ? kHangtimeHoldMs : kEndedHoldMs;
        return (now - ended) <= hold;
    }
    // Prefer recent voice activity for "still active" once we have seen any VCW.
    // This helps the follow machine return promptly from inactive TGs.
    // Use lastVoiceMs which is only updated on meaningful VCW.
    const uint64_t lastVoice = m_lastVoiceMs.load(std::memory_order_acquire);
    const uint64_t last = m_lastActiveMs.load(std::memory_order_acquire);
    const uint64_t effectiveLast = (lastVoice > 0) ? std::max(last, lastVoice) : last;
    if (now < effectiveLast) return true;
    // Longer timeout to avoid dropping active during normal speech pauses / bursty periods.
    // Background noise not an issue once we have mask+sf+vcw evidence.
    const uint64_t silenceLimit = (lastVoice > 0) ? 12000ULL : 15000ULL;  // 12s voice, 15s acq
    return (now - effectiveLast) <= silenceLimit;
}

bool P25TrafficChannelProcessor::mayEmitSustainedAudio() const
{
    return isCallStillActive() &&
        !m_callEnded.load(std::memory_order_acquire) &&
        m_audioOpen.load(std::memory_order_acquire) &&
        !m_encrypted.load(std::memory_order_acquire) &&
        m_p2vcw.load(std::memory_order_acquire) > 0;
}

void P25TrafficChannelProcessor::requestTeardown(const std::string& reason)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_teardownReason = reason;
    }
    m_teardownRequested.store(true);
}

P25TrafficChannelProcessor::Diag P25TrafficChannelProcessor::getDiag() const
{
    Diag d;
    d.talkgroup   = m_talkgroup;
    d.sessionId   = m_sessionId;
    d.grantedSlot = m_grantedSlot;
    d.voiceFreqHz = m_voiceFreqHz;
    d.p2bursts = m_p2bursts.load();
    d.p2vcw = m_p2vcw.load();
    d.p2mac = m_p2mac.load();
    d.p2macPdus = m_p2macPdus.load();
    d.p2macCrcValid = m_p2macCrcValid.load();
    d.p2sf  = m_p2sf.load();
    d.p2mask = m_p2mask.load();
    d.sfLocked   = m_sfLocked.load();
    d.maskLocked = m_maskLocked.load();
    d.macTrusted = m_macTrusted.load();
    d.essTrusted = m_essTrusted.load();
    d.encrypted  = m_encrypted.load();
    d.audioOpen  = m_audioOpen.load();
    d.macPttSeen = m_macPttSeen.load();
    d.macActiveSeen = m_macActiveSeen.load();
    d.macEndPttSeen = m_macEndPttSeen.load();
    d.macIdleSeen = m_macIdleSeen.load();
    d.macHangtimeSeen = m_macHangtimeSeen.load();
    d.callEnded = m_callEnded.load();
    d.lastActiveMs = m_lastActiveMs.load();
    d.lastVoiceMs = m_lastVoiceMs.load();
    d.lastMacMs = m_lastMacMs.load();
    d.lastEssMs = m_lastEssMs.load();
    d.endedMs = m_endedMs.load();
    d.lastAbsoluteDibit = m_lastAbsoluteDibit.load();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        d.teardownReason = m_teardownReason;
        d.endReason = m_endReason;
    }
    if (m_teardownRequested.load()) d.state = "teardown";
    else if (d.callEnded) d.state = d.endReason.empty() ? "ended" : d.endReason;
    else if (d.encrypted) d.state = "encrypted";
    else if (d.audioOpen) d.state = "decoding_sustained";
    else if (d.p2vcw > 0) d.state = "decoding";
    else if (d.sfLocked || d.maskLocked || d.macTrusted) d.state = "framing";
    else d.state = "acquiring";
    return d;
}
