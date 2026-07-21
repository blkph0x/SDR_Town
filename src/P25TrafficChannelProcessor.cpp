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
{
    const uint64_t now = steadyNowMs();
    m_createdMs.store(now, std::memory_order_release);
    m_lastActiveMs.store(now, std::memory_order_release);
}

P25TrafficChannelProcessor::~P25TrafficChannelProcessor() = default;

void P25TrafficChannelProcessor::feedHardDibits(const std::vector<int>& dibits, uint64_t absoluteDibitIndex)
{
    // Observational only: primary P25LiveDecoder owns dibit interpretation.
    // Advance the absolute cursor so lifetime/diagnostics stay monotonic.
    if (dibits.empty() || m_teardownRequested.load()) return;
    m_lastAbsoluteDibit.store(absoluteDibitIndex + dibits.size(), std::memory_order_release);
}

void P25TrafficChannelProcessor::processDibits(const int16_t* dibits, size_t count, uint64_t absoluteDibitIndex)
{
    if (!dibits || count == 0 || m_teardownRequested.load()) return;
    m_lastAbsoluteDibit.store(absoluteDibitIndex + count, std::memory_order_release);
}

void P25TrafficChannelProcessor::observeDecodeResult(const P25LiveDecodeResult& result, uint64_t absoluteDibitIndex)
{
    size_t burstVoiceCodewords = 0;
    size_t targetVoiceCodewords = 0;
    size_t meaningfulVoiceCodewords = 0;
    bool sessionAudioRelease = false;
    bool burstEssKnown = false;
    bool burstEncrypted = false;
    bool macPttSeen = false;
    bool macActiveSeen = false;
    bool macEndPttSeen = false;
    bool macIdleSeen = false;
    bool macHangtimeSeen = false;
    const bool targetSlotKnown = m_grantedSlot == 0 || m_grantedSlot == 1;
    const uint8_t targetSlot = static_cast<uint8_t>(m_grantedSlot & 0x01);
    for (const auto& burst : result.phase2Bursts) {
        burstVoiceCodewords += burst.voiceCodewords.size();
        const bool trafficTalkgroupMatches =
            !burst.trafficTalkgroupKnown ||
            m_talkgroup == 0 ||
            burst.trafficTalkgroupId == m_talkgroup;
        const bool slotMatches = !targetSlotKnown ||
            (burst.grantSlotKnown &&
             static_cast<uint8_t>(burst.grantSlot & 0x01u) == targetSlot);
        const bool burstTargetsCall = slotMatches && trafficTalkgroupMatches;
        if (!burstTargetsCall) continue;
        targetVoiceCodewords += burst.voiceCodewords.size();
        const bool goodVoiceEvidence = burst.xorMaskApplied &&
            (!burst.voiceCodewords.empty() ||
             burst.superframeLock || burst.maskPhaseLock || burst.macCrcLock ||
             burst.essKnown || burst.sessionAudioRelease || burst.macPttSeen || burst.macActiveSeen);
        if (goodVoiceEvidence) {
            meaningfulVoiceCodewords += burst.voiceCodewords.size();
        }
        sessionAudioRelease = sessionAudioRelease || (burst.sessionAudioRelease && trafficTalkgroupMatches);
        burstEssKnown = burstEssKnown || burst.essKnown || burst.trafficSecurityKnown;
        burstEncrypted = burstEncrypted ||
            (burst.essKnown && burst.encrypted) ||
            (burst.trafficSecurityKnown && burst.trafficEncrypted);
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

    const bool globalEssKnown = !targetSlotKnown &&
        (result.stats.phase2EssKnown || result.phase2Ess.known);
    const bool globalEncrypted = !targetSlotKnown &&
        (result.stats.phase2EssEncrypted ||
         (result.phase2Ess.known && result.phase2Ess.encrypted));
    const bool essKnown = burstEssKnown || globalEssKnown;
    const bool encrypted = burstEncrypted || globalEncrypted;
    const bool clearEss = essKnown && !encrypted;
    const bool callEnded = macEndPttSeen || macIdleSeen || macHangtimeSeen;
    const bool audioOpen = !callEnded && !encrypted && (sessionAudioRelease || clearEss);
    const uint64_t now = steadyNowMs();

    m_p2bursts.store(p2bursts, std::memory_order_release);
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
    if (meaningfulVoiceCodewords > 0 || targetVoiceCodewords > 0) {
        m_lastVoiceMs.store(now, std::memory_order_release);
    }
    if (p2macPdus > 0 || p2macCrcValid > 0 || macPttSeen || macActiveSeen || callEnded) {
        m_lastMacMs.store(now, std::memory_order_release);
    }
    if (essKnown) {
        m_lastEssMs.store(now, std::memory_order_release);
    }

    const bool voiceCallEvidence =
        meaningfulVoiceCodewords > 0 ||
        macPttSeen || macActiveSeen || sessionAudioRelease || (audioOpen && meaningfulVoiceCodewords > 0) ||
        (targetVoiceCodewords > 0 && p2mask > 0 && (sessionAudioRelease || clearEss));
    if (voiceCallEvidence) {
        m_lastActiveMs.store(now, std::memory_order_release);
    } else if ((p2sf > 0 || p2mask > 0) && m_lastVoiceMs.load(std::memory_order_acquire) == 0) {
        m_lastActiveMs.store(now, std::memory_order_release);
    }
}

void P25TrafficChannelProcessor::setPhase2MaskParameters(uint16_t nac, uint32_t wacn, uint16_t systemId)
{
    (void)nac;
    (void)wacn;
    (void)systemId;
}

void P25TrafficChannelProcessor::clearPhase2MaskParameters()
{
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
    const uint64_t lastVoice = m_lastVoiceMs.load(std::memory_order_acquire);
    const uint64_t last = m_lastActiveMs.load(std::memory_order_acquire);
    const uint64_t effectiveLast = (lastVoice > 0) ? std::max(last, lastVoice) : last;
    if (now < effectiveLast) return true;
    const uint64_t silenceLimit = (lastVoice > 0) ? 12000ULL : 15000ULL;
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
