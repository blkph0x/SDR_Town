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
    cfg.enablePhase2Decode = true;
    cfg.realtimeVoiceSearch = true;
    cfg.maxFrameSyncBitErrors = 4;
    cfg.maxFrameSyncs = 128;
    cfg.maxRawTsbkBlocksPerFrame = 64;
    m_decoder = P25LiveDecoder(cfg);

    const uint64_t now = steadyNowMs();
    m_createdMs.store(now, std::memory_order_release);
    m_lastActiveMs.store(now, std::memory_order_release);
}

P25TrafficChannelProcessor::~P25TrafficChannelProcessor() = default;

void P25TrafficChannelProcessor::processDibits(const int16_t* dibits, size_t count, uint64_t absoluteSampleIndex)
{
    if (!dibits || count == 0 || m_teardownRequested.load()) return;

    std::vector<int> hardDibits;
    hardDibits.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        hardDibits.push_back(static_cast<int>(dibits[i]) & 0x03);
    }

    P25LiveDecodeResult result;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        result = m_decoder.processHardDibits(hardDibits);
    }
    observeDecodeResult(result, absoluteSampleIndex);
}

void P25TrafficChannelProcessor::observeDecodeResult(const P25LiveDecodeResult& result, uint64_t absoluteDibitIndex)
{
    size_t burstVoiceCodewords = 0;
    bool sessionAudioRelease = false;
    bool burstEssKnown = false;
    bool burstEncrypted = false;
    for (const auto& burst : result.phase2Bursts) {
        burstVoiceCodewords += burst.voiceCodewords.size();
        sessionAudioRelease = sessionAudioRelease || burst.sessionAudioRelease;
        burstEssKnown = burstEssKnown || burst.essKnown;
        burstEncrypted = burstEncrypted || (burst.essKnown && burst.encrypted);
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
    const bool audioOpen = !encrypted && (sessionAudioRelease || clearEss);

    m_p2bursts.store(p2bursts, std::memory_order_release);
    m_p2vcw.store(p2vcw, std::memory_order_release);
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

    const bool trafficEvidence =
        p2vcw > 0 ||
        p2macPdus > 0 ||
        p2macCrcValid > 0 ||
        p2sf > 0 ||
        p2mask > 0 ||
        !result.phase2Bursts.empty() ||
        !result.phase2MacPdus.empty();
    if (trafficEvidence) {
        m_lastActiveMs.store(steadyNowMs(), std::memory_order_release);
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
    const uint64_t last = m_lastActiveMs.load(std::memory_order_acquire);
    return (now - last) <= kMaxSilenceMs;
}

bool P25TrafficChannelProcessor::mayEmitSustainedAudio() const
{
    return isCallStillActive() &&
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
    d.lastActiveMs = m_lastActiveMs.load();
    d.lastAbsoluteDibit = m_lastAbsoluteDibit.load();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        d.teardownReason = m_teardownReason;
    }
    if (m_teardownRequested.load()) d.state = "teardown";
    else if (d.encrypted) d.state = "encrypted";
    else if (d.audioOpen) d.state = "decoding_sustained";
    else if (d.p2vcw > 0) d.state = "decoding";
    else if (d.sfLocked || d.maskLocked || d.macTrusted) d.state = "framing";
    else d.state = "acquiring";
    return d;
}
