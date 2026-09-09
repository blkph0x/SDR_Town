#include "P25TalkgroupRegistry.h"

#include "P25AppGlobals.h"
#include "P25VoiceTiming.h"

#include <QDateTime>
#include <QDir>
#include <QStandardPaths>
#include <QStringList>
#include <QTableWidgetItem>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <fstream>

using json = nlohmann::json;

std::string trimCopy(const std::string& s);

std::map<QString, qint64> gP25RecentExplicitEncryptedPhase2Grants;

static bool sameP25ControlFrequency(double a, double b)
{
    return std::isfinite(a) && std::isfinite(b) && std::abs(a - b) <= 50.0;
}

static QString p25TalkgroupsPath()
{
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(appData);
    return appData + "/p25_talkgroups.json";
}

static QString p25KnownControlChannelsPath()
{
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(appData);
    return appData + "/p25_control_channels.json";
}

static QString p25ChannelIdentifiersPath()
{
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(appData);
    return appData + "/p25_channel_identifiers.json";
}

std::vector<P25KnownControlChannel> loadP25KnownControlChannels()
{
    std::vector<P25KnownControlChannel> out;
    std::ifstream f(p25KnownControlChannelsPath().toStdString());
    if (!f.is_open()) return out;
    try {
        json arr;
        f >> arr;
        if (!arr.is_array()) return out;
        for (const auto& item : arr) {
            P25KnownControlChannel cc;
            cc.freqHz = item.value("freqHz", 0.0);
            cc.label = item.value("label", std::string());
            cc.createdMs = item.value("createdMs", static_cast<qint64>(0));
            cc.lastUsedMs = item.value("lastUsedMs", static_cast<qint64>(0));
            if (std::isfinite(cc.freqHz) && cc.freqHz > 0.0) out.push_back(cc);
        }
    } catch (const std::exception& ex) {
        spdlog::warn("Failed to load p25_control_channels.json: {}", ex.what());
    }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
        return a.freqHz < b.freqHz;
    });
    return out;
}

static void saveP25KnownControlChannels(const std::vector<P25KnownControlChannel>& channels)
{
    json arr = json::array();
    for (const auto& cc : channels) {
        arr.push_back({
            {"freqHz", cc.freqHz},
            {"label", cc.label},
            {"createdMs", cc.createdMs},
            {"lastUsedMs", cc.lastUsedMs},
        });
    }
    try {
        std::ofstream f(p25KnownControlChannelsPath().toStdString());
        if (f.is_open()) f << arr.dump(2);
    } catch (const std::exception& ex) {
        spdlog::warn("Failed to save p25_control_channels.json: {}", ex.what());
    }
}

bool upsertP25KnownControlChannel(double freqHz, std::string label)
{
    if (!std::isfinite(freqHz) || freqHz <= 0.0) return false;
    auto channels = loadP25KnownControlChannels();
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    auto it = std::find_if(channels.begin(), channels.end(), [&](const P25KnownControlChannel& cc) {
        return std::abs(cc.freqHz - freqHz) <= 50.0;
    });
    if (it == channels.end()) {
        P25KnownControlChannel cc;
        cc.freqHz = freqHz;
        cc.label = trimCopy(label);
        cc.createdMs = nowMs;
        cc.lastUsedMs = nowMs;
        channels.push_back(cc);
    } else {
        it->freqHz = freqHz;
        if (!trimCopy(label).empty()) it->label = trimCopy(label);
        it->lastUsedMs = nowMs;
    }
    std::sort(channels.begin(), channels.end(), [](const auto& a, const auto& b) {
        return a.freqHz < b.freqHz;
    });
    saveP25KnownControlChannels(channels);
    return true;
}

bool p25ChannelIdentifierUsable(const P25ChannelIdentifier& identifier)
{
    return identifier.valid &&
        identifier.id < 16 &&
        std::isfinite(identifier.baseHz) &&
        std::isfinite(identifier.spacingHz) &&
        identifier.baseHz > 1e6 &&
        identifier.spacingHz > 0.0;
}

P25ChannelIdentifier p25IdentifierFromEvent(const P25ControlEvent& event)
{
    P25ChannelIdentifier identifier;
    identifier.valid = event.type == P25ControlEventType::IdentifierUpdate &&
        event.identifierKnown &&
        event.baseFrequencyHz > 1e6 &&
        event.channelSpacingHz > 0.0;
    identifier.id = event.identifier;
    identifier.channelType = event.channelType;
    identifier.baseHz = event.baseFrequencyHz;
    identifier.spacingHz = event.channelSpacingHz;
    identifier.txOffsetHz = event.transmitOffsetHz;
    identifier.bandwidthHz = event.bandwidthHz;
    identifier.slotsPerCarrier = std::max(1, event.slotsPerCarrier);
    identifier.phase2Capable = event.phase2Candidate || identifier.slotsPerCarrier > 1;
    return identifier;
}

static void p25ApplyScopeFromEvent(P25CachedChannelIdentifier& rec, const P25ControlEvent& event)
{
    if (event.nacKnown) rec.nac = static_cast<uint16_t>(event.nac & 0x0fffu);
    if (event.networkStatusKnown) {
        if ((event.wacn & 0xfffffu) != 0) rec.wacn = event.wacn & 0xfffffu;
        if ((event.systemId & 0x0fffu) != 0) rec.systemId = static_cast<uint16_t>(event.systemId & 0x0fffu);
    }
    if (event.rfssStatusKnown) {
        if ((event.systemId & 0x0fffu) != 0) rec.systemId = static_cast<uint16_t>(event.systemId & 0x0fffu);
        if (event.rfssId != 0) rec.rfssId = event.rfssId;
        if (event.siteId != 0) rec.siteId = event.siteId;
    }
}

static bool p25CachedIdentifierScopeCompatible(const P25CachedChannelIdentifier& rec,
                                               const P25ControlEvent& event)
{
    if (event.nacKnown && rec.nac != 0 && rec.nac != static_cast<uint16_t>(event.nac & 0x0fffu)) return false;
    if (event.networkStatusKnown) {
        const uint32_t wacn = event.wacn & 0xfffffu;
        const uint16_t systemId = static_cast<uint16_t>(event.systemId & 0x0fffu);
        if (wacn != 0 && rec.wacn != 0 && rec.wacn != wacn) return false;
        if (systemId != 0 && rec.systemId != 0 && rec.systemId != systemId) return false;
    }
    if (event.rfssStatusKnown) {
        const uint16_t systemId = static_cast<uint16_t>(event.systemId & 0x0fffu);
        if (systemId != 0 && rec.systemId != 0 && rec.systemId != systemId) return false;
        if (event.rfssId != 0 && rec.rfssId != 0 && rec.rfssId != event.rfssId) return false;
        if (event.siteId != 0 && rec.siteId != 0 && rec.siteId != event.siteId) return false;
    }
    return true;
}

static bool p25TalkgroupScopeCompatible(const P25TalkgroupEntry& tg,
                                        const P25ControlEvent& event)
{
    if (event.nacKnown && tg.nac != 0 && tg.nac != static_cast<uint16_t>(event.nac & 0x0fffu)) return false;
    if (event.networkStatusKnown) {
        const uint32_t wacn = event.wacn & 0xfffffu;
        const uint16_t systemId = static_cast<uint16_t>(event.systemId & 0x0fffu);
        if (wacn != 0 && tg.wacn != 0 && tg.wacn != wacn) return false;
        if (systemId != 0 && tg.systemId != 0 && tg.systemId != systemId) return false;
    }
    if (event.rfssStatusKnown) {
        const uint16_t systemId = static_cast<uint16_t>(event.systemId & 0x0fffu);
        if (systemId != 0 && tg.systemId != 0 && tg.systemId != systemId) return false;
        if (event.rfssId != 0 && tg.rfssId != 0 && tg.rfssId != event.rfssId) return false;
        if (event.siteId != 0 && tg.siteId != 0 && tg.siteId != event.siteId) return false;
    }
    return true;
}

static std::vector<P25CachedChannelIdentifier> loadP25ChannelIdentifiers()
{
    std::vector<P25CachedChannelIdentifier> out;
    std::ifstream f(p25ChannelIdentifiersPath().toStdString());
    if (!f.is_open()) return out;
    try {
        json arr;
        f >> arr;
        if (!arr.is_array()) return out;
        for (const auto& item : arr) {
            P25CachedChannelIdentifier rec;
            rec.controlFreqHz = item.value("controlFreqHz", 0.0);
            rec.nac = static_cast<uint16_t>(item.value("nac", 0u) & 0x0fffu);
            rec.wacn = item.value("wacn", 0u) & 0xfffffu;
            rec.systemId = static_cast<uint16_t>(item.value("systemId", 0u) & 0x0fffu);
            rec.rfssId = static_cast<uint8_t>(item.value("rfssId", 0u) & 0xffu);
            rec.siteId = static_cast<uint8_t>(item.value("siteId", 0u) & 0xffu);
            rec.firstSeenMs = item.value("firstSeenMs", static_cast<qint64>(0));
            rec.lastSeenMs = item.value("lastSeenMs", static_cast<qint64>(0));
            const auto& planJson = item.contains("identifier") ? item.at("identifier") : item;
            rec.identifier.valid = planJson.value("valid", true);
            rec.identifier.id = static_cast<uint8_t>(planJson.value("id", 0u) & 0x0fu);
            rec.identifier.channelType = static_cast<uint8_t>(planJson.value("channelType", 0u) & 0xffu);
            rec.identifier.baseHz = planJson.value("baseHz", 0.0);
            rec.identifier.spacingHz = planJson.value("spacingHz", 0.0);
            rec.identifier.txOffsetHz = planJson.value("txOffsetHz", 0.0);
            rec.identifier.bandwidthHz = planJson.value("bandwidthHz", 0.0);
            rec.identifier.slotsPerCarrier = std::max(1, planJson.value("slotsPerCarrier", 1));
            rec.identifier.phase2Capable = planJson.value("phase2Capable", false) ||
                rec.identifier.slotsPerCarrier > 1;
            if (std::isfinite(rec.controlFreqHz) && rec.controlFreqHz > 0.0 &&
                p25ChannelIdentifierUsable(rec.identifier)) {
                out.push_back(rec);
            }
        }
    } catch (const std::exception& ex) {
        spdlog::warn("Failed to load p25_channel_identifiers.json: {}", ex.what());
    }
    return out;
}

static void saveP25ChannelIdentifiers(std::vector<P25CachedChannelIdentifier> identifiers)
{
    identifiers.erase(std::remove_if(identifiers.begin(), identifiers.end(), [](const auto& rec) {
        return !std::isfinite(rec.controlFreqHz) || rec.controlFreqHz <= 0.0 ||
            !p25ChannelIdentifierUsable(rec.identifier);
    }), identifiers.end());
    std::sort(identifiers.begin(), identifiers.end(), [](const auto& a, const auto& b) {
        if (std::abs(a.controlFreqHz - b.controlFreqHz) > 50.0) return a.controlFreqHz < b.controlFreqHz;
        if (a.nac != b.nac) return a.nac < b.nac;
        if (a.wacn != b.wacn) return a.wacn < b.wacn;
        if (a.systemId != b.systemId) return a.systemId < b.systemId;
        if (a.rfssId != b.rfssId) return a.rfssId < b.rfssId;
        if (a.siteId != b.siteId) return a.siteId < b.siteId;
        return a.identifier.id < b.identifier.id;
    });
    json arr = json::array();
    for (const auto& rec : identifiers) {
        arr.push_back({
            {"controlFreqHz", rec.controlFreqHz},
            {"nac", rec.nac},
            {"wacn", rec.wacn},
            {"systemId", rec.systemId},
            {"rfssId", rec.rfssId},
            {"siteId", rec.siteId},
            {"firstSeenMs", rec.firstSeenMs},
            {"lastSeenMs", rec.lastSeenMs},
            {"identifier", {
                {"valid", rec.identifier.valid},
                {"id", rec.identifier.id},
                {"channelType", rec.identifier.channelType},
                {"baseHz", rec.identifier.baseHz},
                {"spacingHz", rec.identifier.spacingHz},
                {"txOffsetHz", rec.identifier.txOffsetHz},
                {"bandwidthHz", rec.identifier.bandwidthHz},
                {"slotsPerCarrier", rec.identifier.slotsPerCarrier},
                {"phase2Capable", rec.identifier.phase2Capable},
            }},
        });
    }
    try {
        std::ofstream f(p25ChannelIdentifiersPath().toStdString());
        if (f.is_open()) f << arr.dump(2);
    } catch (const std::exception& ex) {
        spdlog::warn("Failed to save p25_channel_identifiers.json: {}", ex.what());
    }
}

bool upsertP25ChannelIdentifier(double controlFreqHz,
                                       const P25ControlEvent& event,
                                       qint64 nowMs)
{
    if (!std::isfinite(controlFreqHz) || controlFreqHz <= 0.0) return false;
    P25ChannelIdentifier identifier = p25IdentifierFromEvent(event);
    if (!p25ChannelIdentifierUsable(identifier)) return false;

    auto identifiers = loadP25ChannelIdentifiers();
    auto it = std::find_if(identifiers.begin(), identifiers.end(), [&](const auto& rec) {
        return sameP25ControlFrequency(rec.controlFreqHz, controlFreqHz) &&
            rec.identifier.id == identifier.id &&
            p25CachedIdentifierScopeCompatible(rec, event);
    });
    if (it == identifiers.end()) {
        P25CachedChannelIdentifier rec;
        rec.controlFreqHz = controlFreqHz;
        p25ApplyScopeFromEvent(rec, event);
        rec.identifier = identifier;
        rec.firstSeenMs = nowMs;
        rec.lastSeenMs = nowMs;
        identifiers.push_back(rec);
    } else {
        it->controlFreqHz = controlFreqHz;
        p25ApplyScopeFromEvent(*it, event);
        it->identifier = identifier;
        if (it->firstSeenMs <= 0) it->firstSeenMs = nowMs;
        it->lastSeenMs = nowMs;
    }
    saveP25ChannelIdentifiers(std::move(identifiers));
    return true;
}

size_t seedP25AnalyzerFromCachedChannelIdentifiers(P25ControlChannelAnalyzer& analyzer,
                                                          double controlFreqHz)
{
    size_t count = 0;
    for (const auto& rec : loadP25ChannelIdentifiers()) {
        if (!sameP25ControlFrequency(rec.controlFreqHz, controlFreqHz)) continue;
        analyzer.setChannelIdentifier(rec.identifier);
        ++count;
    }
    return count;
}

static std::string p25VoiceProtocolStorage(P25VoiceProtocol protocol)
{
    switch (protocol) {
        case P25VoiceProtocol::Phase1FDMA: return "phase1_fdma";
        case P25VoiceProtocol::Phase2TDMA: return "phase2_tdma";
        case P25VoiceProtocol::Unknown:
        default: return "unknown";
    }
}

static P25VoiceProtocol p25VoiceProtocolFromStorage(std::string text)
{
    for (char& c : text) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (c == '-' || c == ' ') c = '_';
    }
    if (text == "phase1" || text == "phase1_fdma" || text == "p25_phase_1_fdma") return P25VoiceProtocol::Phase1FDMA;
    if (text == "phase2" || text == "phase2_tdma" || text == "p25_phase_2_tdma") return P25VoiceProtocol::Phase2TDMA;
    return P25VoiceProtocol::Unknown;
}

QString p25VoiceProtocolShort(P25VoiceProtocol protocol)
{
    switch (protocol) {
        case P25VoiceProtocol::Phase1FDMA: return "P1";
        case P25VoiceProtocol::Phase2TDMA: return "P2";
        case P25VoiceProtocol::Unknown:
        default: return "-";
    }
}

bool p25TalkgroupIsPhase2(const P25TalkgroupEntry& tg)
{
    return tg.voiceProtocol == P25VoiceProtocol::Phase2TDMA || tg.phase2Candidate || tg.tdmaSlotKnown;
}

static QString p25Phase2GrantHoldKey(uint32_t talkgroupId, uint16_t channel, double voiceFrequencyHz)
{
    if (talkgroupId == 0 || channel == 0) return {};
    const qlonglong roundedVoiceHz = std::isfinite(voiceFrequencyHz) && voiceFrequencyHz > 0.0
        ? static_cast<qlonglong>(std::llround(voiceFrequencyHz))
        : 0;
    return QString("%1:%2:%3")
        .arg(talkgroupId)
        .arg(channel)
        .arg(roundedVoiceHz);
}

void p25PruneRecentExplicitEncryptedPhase2Grants(std::map<QString, qint64>& holds,
                                                        qint64 nowMs,
                                                        qint64 ttlMs)
{
    for (auto it = holds.begin(); it != holds.end(); ) {
        if (nowMs - it->second > ttlMs) it = holds.erase(it);
        else ++it;
    }
}

void p25RememberExplicitEncryptedPhase2Grant(std::map<QString, qint64>& holds,
                                                    const P25ControlEvent& event,
                                                    const P25TalkgroupEntry* tg,
                                                    qint64 nowMs)
{
    if (!event.encryptionKnown || !event.encrypted) return;

    const bool phase2Grant =
        event.phase2Candidate ||
        event.voiceProtocol == P25VoiceProtocol::Phase2TDMA ||
        event.tdmaSlotKnown ||
        (tg != nullptr && p25TalkgroupIsPhase2(*tg));
    if (!phase2Grant) return;

    const uint32_t talkgroupId = event.talkgroupId != 0
        ? event.talkgroupId
        : (tg != nullptr ? tg->talkgroupId : 0);
    const uint16_t channel = event.channel != 0
        ? event.channel
        : (tg != nullptr ? tg->lastChannel : 0);
    const double voiceHz = event.voiceFrequencyHz > 0.0
        ? event.voiceFrequencyHz
        : (tg != nullptr ? tg->lastVoiceFreqHz : 0.0);

    // current-call encrypted hold: remember both exact TG/channel/frequency and
    // unresolved TG/channel-only grants.  The latter fixes the live 420.350 MHz
    // case where an explicit encrypted grant arrived before the IDEN table, then
    // a service-option-less update resolved a few seconds later and was followed.
    const QString channelOnlyKey = p25Phase2GrantHoldKey(talkgroupId, channel, 0.0);
    if (!channelOnlyKey.isEmpty()) holds[channelOnlyKey] = nowMs;
    const QString exactKey = p25Phase2GrantHoldKey(talkgroupId, channel, voiceHz);
    if (!exactKey.isEmpty()) holds[exactKey] = nowMs;
}

void p25RememberVoiceProvedEncryptedPhase2Grant(std::map<QString, qint64>& holds,
                                                       const P25TalkgroupEntry& tg,
                                                       qint64 nowMs)
{
    if (!p25TalkgroupIsPhase2(tg) || tg.talkgroupId == 0) return;
    const QString channelOnlyKey = p25Phase2GrantHoldKey(tg.talkgroupId, tg.lastChannel, 0.0);
    if (!channelOnlyKey.isEmpty()) holds[channelOnlyKey] = nowMs;
    const QString exactKey = p25Phase2GrantHoldKey(tg.talkgroupId, tg.lastChannel, tg.lastVoiceFreqHz);
    if (!exactKey.isEmpty()) holds[exactKey] = nowMs;
}

qint64 p25RecentExplicitEncryptedPhase2GrantAgeMs(const std::map<QString, qint64>& holds,
                                                         const P25ControlEvent& event,
                                                         const P25TalkgroupEntry& tg,
                                                         qint64 nowMs,
                                                         qint64 ttlMs)
{
    if (!p25TalkgroupIsPhase2(tg)) return -1;

    const uint32_t talkgroupId = event.talkgroupId != 0 ? event.talkgroupId : tg.talkgroupId;
    const uint16_t channel = event.channel != 0 ? event.channel : tg.lastChannel;
    const double voiceHz = event.voiceFrequencyHz > 0.0 ? event.voiceFrequencyHz : tg.lastVoiceFreqHz;

    qint64 bestAge = std::numeric_limits<qint64>::max();
    for (const QString& key : {
             p25Phase2GrantHoldKey(talkgroupId, channel, voiceHz),
             p25Phase2GrantHoldKey(talkgroupId, channel, 0.0)}) {
        if (key.isEmpty()) continue;
        auto it = holds.find(key);
        if (it == holds.end()) continue;
        const qint64 age = nowMs - it->second;
        if (age >= 0 && age <= ttlMs) bestAge = std::min(bestAge, age);
    }
    return bestAge == std::numeric_limits<qint64>::max() ? -1 : bestAge;
}

void p25ClearExplicitEncryptedPhase2GrantHold(std::map<QString, qint64>& holds,
                                                    const P25TalkgroupEntry& tg)
{
    if (tg.talkgroupId == 0) return;
    for (const QString& key : {
             p25Phase2GrantHoldKey(tg.talkgroupId, tg.lastChannel, tg.lastVoiceFreqHz),
             p25Phase2GrantHoldKey(tg.talkgroupId, tg.lastChannel, 0.0)}) {
        if (!key.isEmpty()) holds.erase(key);
    }
}

bool p25ShouldSuppressAnalogDemod(bool voiceDecodeEnabled,
                                         bool controlChannelMute,
                                         bool independentTrafficSource,
                                         bool voicePhase2) noexcept
{
    // SDRTrunk never attaches analog FM to a P25 decoder channel
    // (P25P2DecoderHDQPSK.receive is I/Q LPF + AGC + DQPSK only).
    // Analog NFM of P25 RF is the digital-static blast. Ordinary analog
    // receivers keep all four flags false and still run demodulateToAudio.
    return voiceDecodeEnabled ||
           controlChannelMute ||
           independentTrafficSource ||
           voicePhase2;
}

bool p25RecentSpeakerOutputActive(qint64 nowMs, qint64 holdMs) noexcept
{
    const qint64 lastSpeaker = gP25AudioLastSpeakerOutputMs.load(std::memory_order_relaxed);
    return lastSpeaker > 0 && nowMs > 0 && nowMs - lastSpeaker <= holdMs;
}

static bool p25DiagTargetPttSessionClear(const P25VoiceDiagSnapshot& diag) noexcept
{
    // P25LiveDecoder::sessionAudioRelease is already target-slot clear-session
    // evidence: it is set after per-slot ESS/PTT or MAC_ACTIVE traffic security
    // has proven the selected call clear.  MAC_ACTIVE clipped captures often do
    // not carry a same-window PTT flag; requiring it here hid real clear traffic.
    return diag.phase2TargetSessionAudioRelease &&
        !diag.phase2TargetEssEncrypted;
}

static bool p25DiagTargetEssClear(const P25VoiceDiagSnapshot& diag) noexcept
{
    return diag.phase2TargetEssKnown && !diag.phase2TargetEssEncrypted;
}

bool p25DiagTargetHardClear(const P25VoiceDiagSnapshot& diag) noexcept
{
    return p25DiagTargetPttSessionClear(diag) || p25DiagTargetEssClear(diag);
}

bool p25ActiveFollowTrafficDisprovesEncryption(const Receiver& rx) noexcept
{
    if (!rx.p25VoicePhase2 || rx.p25VoiceEncrypted) return false;
    const P25VoiceDiagSnapshot& diag = rx.p25VoiceDiagnostics;
    return p25DiagTargetHardClear(diag);
}

bool p25TalkgroupCanTuneForFollow(const P25TalkgroupEntry& tg)
{
    if (tg.encryptionKnown) return !tg.encrypted;
    // Match sdrtrunk's Phase-2 audio module behavior: an unknown grant/update
    // may allocate a traffic channel.  Speaker audio still remains gated until
    // target-slot traffic PTT/MAC/ESS proves the current call clear.
    return p25TalkgroupIsPhase2(tg);
}

bool p25TalkgroupGrantProvesSpeakerClear(const P25TalkgroupEntry& tg)
{
    // A CRC-valid current voice grant carries the call service options. Track
    // explicit clear here for follow/tune decisions and Phase-1 handling. The
    // Phase-2 speaker gate still requires target-slot PTT/ESS/MAC evidence or
    // validated hard-voice AMBE, matching sdrtrunk's queue/drain behavior while
    // preserving late-entry clear calls.
    return tg.encryptionKnown && !tg.encrypted;
}

bool p25TalkgroupGrantProvesSpeakerEncrypted(const P25TalkgroupEntry& tg)
{
    // Explicit encrypted grant state is still fail-closed for both phases.
    return tg.encryptionKnown && tg.encrypted;
}

void p25PreserveTalkgroupEncryptionFromPrior(P25TalkgroupEntry& grantTg,
                                                    const P25TalkgroupEntry& prior)
{
    if (prior.talkgroupId == 0 || prior.talkgroupId != grantTg.talkgroupId) return;
    // Service-option-less OP=0x02 updates must not erase sticky call security.
    // Preserve both clear and encrypted history until a fresh OP=0x00 grant
    // explicitly overrides.  Field capture 20260712_072438 showed TG 12068
    // (sticky encrypted) lose ENC on OP=0x02 and then emit probe audio.
    if (grantTg.encryptionKnown) return;
    if (!prior.encryptionKnown) return;
    grantTg.encryptionKnown = true;
    grantTg.encrypted = prior.encrypted;
}

bool p25PrepareTalkgroupForFollowGrant(P25TalkgroupEntry& tg,
                                              const P25ControlEvent& event,
                                              bool& probingUnknownPhase2EncryptedHistory)
{
    probingUnknownPhase2EncryptedHistory = false;
    if (event.encryptionKnown) {
        tg.encryptionKnown = true;
        tg.encrypted = event.encrypted;
    } else if (p25TalkgroupIsPhase2(tg)) {
        // A Phase-2 service-option-less grant/update does not prove call security.
        // Preserve sticky clear, but do not let stale encrypted registry history
        // stop the retune entirely.  sdrtrunk follows the current allocation and
        // lets target-slot traffic MAC/ESS/PTT decide the live call security.
        // A recent explicit encrypted grant for the same TG/channel/frequency is
        // still blocked by the current-call hold in the auto-follow/followtest
        // callers before any speaker audio can open.
        if (tg.encryptionKnown && !tg.encrypted) {
            return p25TalkgroupCanTuneForFollow(tg);
        }
        if (tg.encryptionKnown && tg.encrypted) {
            probingUnknownPhase2EncryptedHistory = true;
            tg.encryptionKnown = false;
            tg.encrypted = false;
            return true;
        }
    }
    return p25TalkgroupCanTuneForFollow(tg);
}

bool p25TalkgroupHasUsableMaskMetadata(const P25TalkgroupEntry& tg)
{
    return tg.p25MaskParamsKnown &&
        tg.nac != 0 &&
        tg.wacn != 0 &&
        tg.systemId != 0;
}

static bool p25ApplyControlEventSystemMetadata(P25TalkgroupEntry& tg,
                                               const P25ControlEvent& event)
{
    bool changed = false;
    if (event.nacKnown) {
        const uint16_t nac = static_cast<uint16_t>(event.nac & 0x0fffu);
        if (tg.nac != nac) {
            tg.nac = nac;
            changed = true;
        }
    }
    if (event.networkStatusKnown) {
        const uint32_t wacn = event.wacn & 0xfffffu;
        const uint16_t systemId = static_cast<uint16_t>(event.systemId & 0x0fffu);
        if (wacn != 0 && tg.wacn != wacn) {
            tg.wacn = wacn;
            changed = true;
        }
        if (systemId != 0 && tg.systemId != systemId) {
            tg.systemId = systemId;
            changed = true;
        }
    }
    if (event.rfssStatusKnown) {
        const uint16_t systemId = static_cast<uint16_t>(event.systemId & 0x0fffu);
        if (systemId != 0 && tg.systemId != systemId) {
            tg.systemId = systemId;
            changed = true;
        }
        if (tg.rfssId != event.rfssId) {
            tg.rfssId = event.rfssId;
            changed = true;
        }
        if (tg.siteId != event.siteId) {
            tg.siteId = event.siteId;
            changed = true;
        }
    }
    const bool canMask = tg.nac != 0 && tg.wacn != 0 && tg.systemId != 0;
    if (canMask && !tg.p25MaskParamsKnown) {
        tg.p25MaskParamsKnown = true;
        changed = true;
    }
    return changed;
}

P25TalkgroupEntry p25TalkgroupEntryFromCurrentGrant(double controlFreqHz,
                                                           const P25ControlEvent& event,
                                                           qint64 nowMs)
{
    P25TalkgroupEntry tg;
    tg.controlFreqHz = controlFreqHz;
    tg.talkgroupId = event.talkgroupId;
    tg.firstSeenMs = nowMs;
    tg.lastSeenMs = nowMs;
    tg.hitCount = 1;
    if (event.sourceId != 0) tg.lastSourceId = event.sourceId;
    if (event.channel != 0) tg.lastChannel = event.channel;
    if (event.voiceFrequencyHz > 0.0) tg.lastVoiceFreqHz = event.voiceFrequencyHz;
    tg.voiceProtocol = event.voiceProtocol != P25VoiceProtocol::Unknown
        ? event.voiceProtocol
        : (event.phase2Candidate ? P25VoiceProtocol::Phase2TDMA : P25VoiceProtocol::Phase1FDMA);
    tg.phase2Candidate = event.phase2Candidate || tg.voiceProtocol == P25VoiceProtocol::Phase2TDMA;
    if (event.tdmaSlotKnown) {
        tg.tdmaSlotKnown = true;
        tg.tdmaSlot = event.tdmaSlot;
    }
    p25ApplyControlEventSystemMetadata(tg, event);
    if (event.encryptionKnown) {
        tg.encryptionKnown = true;
        tg.encrypted = event.encrypted;
    }
    return tg;
}

static bool p25CopySiteMetadata(P25TalkgroupEntry& dst,
                                const P25TalkgroupEntry& src,
                                bool requireMissingMask)
{
    if (!p25TalkgroupHasUsableMaskMetadata(src)) return false;
    if (requireMissingMask && p25TalkgroupHasUsableMaskMetadata(dst)) return false;

    bool changed = false;
    if (!p25TalkgroupHasUsableMaskMetadata(dst)) {
        if (dst.nac != src.nac) {
            dst.nac = src.nac;
            changed = true;
        }
        if (dst.wacn != src.wacn) {
            dst.wacn = src.wacn;
            changed = true;
        }
        if (dst.systemId != src.systemId) {
            dst.systemId = src.systemId;
            changed = true;
        }
        if (!dst.p25MaskParamsKnown) {
            dst.p25MaskParamsKnown = true;
            changed = true;
        }
    }
    if (dst.rfssId == 0 && src.rfssId != 0) {
        dst.rfssId = src.rfssId;
        changed = true;
    }
    if (dst.siteId == 0 && src.siteId != 0) {
        dst.siteId = src.siteId;
        changed = true;
    }
    return changed;
}

bool p25AugmentTalkgroupFromKnownSite(P25TalkgroupEntry& tg,
                                             const std::vector<P25TalkgroupEntry>& talkgroups,
                                             double controlFreqHz)
{
    const double ccHz = std::isfinite(controlFreqHz) && controlFreqHz > 0.0
        ? controlFreqHz
        : tg.controlFreqHz;
    if (!std::isfinite(ccHz) || ccHz <= 0.0) return false;
    if (p25TalkgroupHasUsableMaskMetadata(tg) && tg.rfssId != 0 && tg.siteId != 0) return false;

    const P25TalkgroupEntry* best = nullptr;
    int bestScore = -1;
    qint64 bestLastSeen = std::numeric_limits<qint64>::min();
    for (const auto& candidate : talkgroups) {
        if (!sameP25ControlFrequency(candidate.controlFreqHz, ccHz)) continue;
        if (!p25TalkgroupHasUsableMaskMetadata(candidate)) continue;

        int score = 0;
        if (candidate.talkgroupId == tg.talkgroupId) score += 100;
        if (tg.rfssId != 0 && candidate.rfssId == tg.rfssId) score += 20;
        if (tg.siteId != 0 && candidate.siteId == tg.siteId) score += 20;
        if (candidate.rfssId != 0 && candidate.siteId != 0) score += 5;
        if (!p25TalkgroupHasUsableMaskMetadata(tg)) score += 10;

        if (!best || score > bestScore ||
            (score == bestScore && candidate.lastSeenMs > bestLastSeen)) {
            best = &candidate;
            bestScore = score;
            bestLastSeen = candidate.lastSeenMs;
        }
    }
    return best ? p25CopySiteMetadata(tg, *best, false) : false;
}

std::vector<P25TalkgroupEntry> loadP25Talkgroups()
{
    std::vector<P25TalkgroupEntry> out;
    std::ifstream f(p25TalkgroupsPath().toStdString());
    if (!f.is_open()) return out;
    try {
        json arr;
        f >> arr;
        if (!arr.is_array()) return out;
        for (const auto& item : arr) {
            P25TalkgroupEntry tg;
            tg.controlFreqHz = item.value("controlFreqHz", 0.0);
            tg.talkgroupId = item.value("talkgroupId", 0u);
            tg.alphaTag = item.value("alphaTag", std::string());
            tg.lastSourceId = item.value("lastSourceId", 0u);
            tg.lastChannel = item.value("lastChannel", 0u);
            tg.lastVoiceFreqHz = item.value("lastVoiceFreqHz", 0.0);
            tg.voiceProtocol = p25VoiceProtocolFromStorage(item.value("voiceProtocol", std::string("unknown")));
            tg.phase2Candidate = item.value("phase2Candidate", false);
            tg.tdmaSlot = static_cast<uint8_t>(item.value("tdmaSlot", 0u) & 0xffu);
            tg.tdmaSlotKnown = item.value("tdmaSlotKnown", false);
            tg.p25MaskParamsKnown = item.value("p25MaskParamsKnown", false);
            tg.nac = static_cast<uint16_t>(item.value("nac", 0u) & 0x0fffu);
            tg.wacn = item.value("wacn", 0u);
            tg.systemId = static_cast<uint16_t>(item.value("systemId", 0u) & 0x0fffu);
            tg.rfssId = static_cast<uint8_t>(item.value("rfssId", 0u) & 0xffu);
            tg.siteId = static_cast<uint8_t>(item.value("siteId", 0u) & 0xffu);
            tg.hitCount = item.value("hitCount", 0);
            tg.encryptionKnown = item.value("encryptionKnown", false);
            tg.encrypted = item.value("encrypted", false);
            tg.verified = item.value("verified", false);
            tg.scannerEnabled = item.value("scannerEnabled", false);
            tg.userPriority = item.value("userPriority", 0);
            tg.activityScore = item.value("activityScore", 0);
            tg.firstSeenMs = item.value("firstSeenMs", static_cast<qint64>(0));
            tg.lastSeenMs = item.value("lastSeenMs", static_cast<qint64>(0));
            if (tg.talkgroupId > 0 && std::isfinite(tg.controlFreqHz) && tg.controlFreqHz > 0.0) {
                out.push_back(tg);
            }
        }
    } catch (const std::exception& ex) {
        spdlog::warn("Failed to load p25_talkgroups.json: {}", ex.what());
    }
    return out;
}

bool p25RefreshFollowGrantFromRegistry(P25TalkgroupEntry& tg,
                                              const std::vector<P25TalkgroupEntry>& talkgroups,
                                              qint64 nowMs)
{
    if (tg.talkgroupId == 0) return false;

    const P25TalkgroupEntry* best = nullptr;
    int bestScore = -1;
    for (const auto& candidate : talkgroups) {
        if (candidate.talkgroupId != tg.talkgroupId) continue;
        if (tg.firstSeenMs > 0 && candidate.lastSeenMs > 0 &&
            candidate.lastSeenMs + 250 < tg.firstSeenMs) {
            continue;
        }
        if (nowMs > 0 && candidate.lastSeenMs > 0 &&
            nowMs - candidate.lastSeenMs > 30000) {
            continue;
        }
        if (tg.lastVoiceFreqHz > 0.0 && candidate.lastVoiceFreqHz > 0.0 &&
            std::abs(candidate.lastVoiceFreqHz - tg.lastVoiceFreqHz) > 50.0) {
            continue;
        }
        if (tg.lastChannel != 0 && candidate.lastChannel != 0 &&
            candidate.lastChannel != tg.lastChannel) {
            continue;
        }

        int score = 0;
        if (candidate.encryptionKnown) score += 1000;
        if (candidate.lastSourceId != 0) score += 80;
        if (candidate.tdmaSlotKnown) score += 60;
        if (p25TalkgroupHasUsableMaskMetadata(candidate)) score += 50;
        if (candidate.lastVoiceFreqHz > 0.0) score += 20;
        if (candidate.lastChannel != 0) score += 10;
        score += static_cast<int>(std::clamp<qint64>(candidate.lastSeenMs - tg.firstSeenMs, 0, 1000));
        if (!best || score > bestScore ||
            (score == bestScore && candidate.lastSeenMs > best->lastSeenMs)) {
            best = &candidate;
            bestScore = score;
        }
    }
    if (!best) return false;

    bool changed = false;
    auto assignBool = [&](bool& dst, bool value) {
        if (dst != value) {
            dst = value;
            changed = true;
        }
    };
    auto assignU8 = [&](uint8_t& dst, uint8_t value) {
        if (dst != value) {
            dst = value;
            changed = true;
        }
    };
    auto assignU16 = [&](uint16_t& dst, uint16_t value) {
        if (dst != value) {
            dst = value;
            changed = true;
        }
    };
    auto assignU32 = [&](uint32_t& dst, uint32_t value) {
        if (dst != value) {
            dst = value;
            changed = true;
        }
    };

    if (best->lastSeenMs > tg.lastSeenMs) {
        tg.lastSeenMs = best->lastSeenMs;
        changed = true;
    }
    if (best->lastSourceId != 0) assignU32(tg.lastSourceId, best->lastSourceId);
    if (best->lastChannel != 0) assignU16(tg.lastChannel, best->lastChannel);
    if (best->lastVoiceFreqHz > 0.0 &&
        (tg.lastVoiceFreqHz <= 0.0 || std::abs(best->lastVoiceFreqHz - tg.lastVoiceFreqHz) <= 50.0)) {
        if (std::abs(tg.lastVoiceFreqHz - best->lastVoiceFreqHz) > 0.5) {
            tg.lastVoiceFreqHz = best->lastVoiceFreqHz;
            changed = true;
        }
    }
    if (best->voiceProtocol != P25VoiceProtocol::Unknown &&
        tg.voiceProtocol != best->voiceProtocol) {
        tg.voiceProtocol = best->voiceProtocol;
        changed = true;
    }
    if (best->phase2Candidate && !tg.phase2Candidate) {
        tg.phase2Candidate = true;
        changed = true;
    }
    if (best->tdmaSlotKnown) {
        assignBool(tg.tdmaSlotKnown, true);
        assignU8(tg.tdmaSlot, static_cast<uint8_t>(best->tdmaSlot & 0x01u));
    }
    if (p25TalkgroupHasUsableMaskMetadata(*best)) {
        assignBool(tg.p25MaskParamsKnown, true);
        assignU16(tg.nac, best->nac);
        assignU32(tg.wacn, best->wacn);
        assignU16(tg.systemId, best->systemId);
    }
    if (best->rfssId != 0) assignU8(tg.rfssId, best->rfssId);
    if (best->siteId != 0) assignU8(tg.siteId, best->siteId);
    // Service-option-less Phase-2 updates are common.  If the current event did
    // not carry service options, preserve recent same-call clear/encrypted state
    // from the registry; never override a grant that explicitly did carry it.
    if (!tg.encryptionKnown && best->encryptionKnown) {
        assignBool(tg.encryptionKnown, true);
        assignBool(tg.encrypted, best->encrypted);
    }
    return changed;
}

void saveP25Talkgroups(const std::vector<P25TalkgroupEntry>& talkgroups)
{
    json arr = json::array();
    for (const auto& tg : talkgroups) {
        arr.push_back({
            {"controlFreqHz", tg.controlFreqHz},
            {"talkgroupId", tg.talkgroupId},
            {"alphaTag", tg.alphaTag},
            {"lastSourceId", tg.lastSourceId},
            {"lastChannel", tg.lastChannel},
            {"lastVoiceFreqHz", tg.lastVoiceFreqHz},
            {"voiceProtocol", p25VoiceProtocolStorage(tg.voiceProtocol)},
            {"phase2Candidate", tg.phase2Candidate},
            {"tdmaSlot", tg.tdmaSlot},
            {"tdmaSlotKnown", tg.tdmaSlotKnown},
            {"p25MaskParamsKnown", tg.p25MaskParamsKnown},
            {"nac", tg.nac},
            {"wacn", tg.wacn},
            {"systemId", tg.systemId},
            {"rfssId", tg.rfssId},
            {"siteId", tg.siteId},
            {"hitCount", tg.hitCount},
            {"encryptionKnown", tg.encryptionKnown},
            {"encrypted", tg.encrypted},
            {"verified", tg.verified},
            {"scannerEnabled", tg.scannerEnabled},
            {"userPriority", tg.userPriority},
            {"activityScore", tg.activityScore},
            {"firstSeenMs", tg.firstSeenMs},
            {"lastSeenMs", tg.lastSeenMs},
        });
    }
    try {
        std::ofstream f(p25TalkgroupsPath().toStdString());
        if (f.is_open()) f << arr.dump(2);
    } catch (const std::exception& ex) {
        spdlog::warn("Failed to save p25_talkgroups.json: {}", ex.what());
    }
}

static QString p25TimeText(qint64 ms)
{
    if (ms <= 0) return "-";
    return QDateTime::fromMSecsSinceEpoch(ms).toString("yyyy-MM-dd HH:mm:ss");
}

QString p25HexId(uint32_t value, int width)
{
    return QString("0x%1").arg(value, width, 16, QLatin1Char('0')).toUpper();
}

QString p25BytesToHex(const std::vector<uint8_t>& bytes)
{
    QStringList parts;
    for (uint8_t byte : bytes) {
        parts << QString("%1").arg(byte, 2, 16, QLatin1Char('0')).toUpper();
    }
    return parts.join(' ');
}

static QString p25MessageIndicatorText(const std::array<uint8_t, 9>& bytes)
{
    QStringList parts;
    for (uint8_t byte : bytes) {
        parts << QString("%1").arg(byte, 2, 16, QLatin1Char('0')).toUpper();
    }
    return parts.join(QString());
}

QString p25Phase2AcchStatsText(const P25LiveDecoderStats& stats)
{
    return QString("p2acch=nom:%1 altKind:%2 swap:%3 slip:%4 inv:%5 fec:%6 rs:%7 dir:%8 drej:%9")
        .arg(static_cast<qulonglong>(stats.phase2MacNominalCrcValid))
        .arg(static_cast<qulonglong>(stats.phase2MacAltKindCrcValid))
        .arg(static_cast<qulonglong>(stats.phase2MacBitSwapCrcValid))
        .arg(static_cast<qulonglong>(stats.phase2MacSlipCrcValid))
        .arg(static_cast<qulonglong>(stats.phase2MacInvertCrcValid))
        .arg(static_cast<qulonglong>(stats.phase2MacFecDecoded))
        .arg(static_cast<qulonglong>(stats.phase2MacRsDecoded))
        .arg(static_cast<qulonglong>(stats.phase2MacDirectCrcValid))
        .arg(static_cast<qulonglong>(stats.phase2MacDirectCrcRejected));
}

QString p25Phase2AcchStatsText(const P25VoiceDiagSnapshot& diag)
{
    return QString("p2acch=nom:%1 altKind:%2 swap:%3 slip:%4 inv:%5 fec:%6 rs:%7 dir:%8 drej:%9")
        .arg(diag.phase2MacNominalCrcValid)
        .arg(diag.phase2MacAltKindCrcValid)
        .arg(diag.phase2MacBitSwapCrcValid)
        .arg(diag.phase2MacSlipCrcValid)
        .arg(diag.phase2MacInvertCrcValid)
        .arg(diag.phase2MacFecDecoded)
        .arg(diag.phase2MacRsDecoded)
        .arg(diag.phase2MacDirectCrcValid)
        .arg(diag.phase2MacDirectCrcRejected);
}

QString p25Phase2MacPduHypothesisText(const P25Phase2MacPdu& pdu)
{
    QStringList parts;
    parts << QString("detected=%1").arg(QString::fromStdString(P25LiveDecoder::phase2BurstKindToString(pdu.detectedKind)));
    parts << QString("attempt=%1").arg(QString::fromStdString(P25LiveDecoder::phase2BurstKindToString(pdu.source)));
    if (pdu.acchHypothesisKnown) {
        parts << QString("swap=%1").arg(pdu.acchBitOrderSwapped ? "yes" : "no");
        parts << QString("invert=%1").arg(pdu.acchDibitInverted ? "yes" : "no");
        parts << QString("slip=%1").arg(pdu.acchSlipDibits);
    } else {
        parts << "hyp=unknown";
    }
    return parts.join(' ');
}

QString p25ChannelText(uint16_t channel)
{
    return QString("0x%1").arg(channel, 4, 16, QLatin1Char('0')).toUpper();
}

QString p25EventLogText(const P25ControlEvent& ev)
{
    QStringList parts;
    const bool identifierUpdate = ev.type == P25ControlEventType::IdentifierUpdate;
    parts << QString::fromStdString(p25ControlEventTypeToString(ev.type));
    parts << QString("op=0x%1").arg(ev.opcode, 2, 16, QLatin1Char('0')).toUpper();
    parts << QString("mfid=0x%1").arg(ev.mfid, 2, 16, QLatin1Char('0')).toUpper();
    if (!ev.label.empty()) parts << QString::fromStdString(ev.label);
    if (ev.phase2Mac) {
        parts << QString("macPdu=%1").arg(QString::fromStdString(p25Phase2MacPduTypeToString(ev.macPduType)));
        parts << QString("macPduType=0x%1").arg(ev.macPduType, 1, 16, QLatin1Char('0')).toUpper();
        parts << QString("macOff=%1").arg(static_cast<int>(ev.macPduOffset));
        parts << QString("macMsg=0x%1").arg(ev.macMessageOpcode, 2, 16, QLatin1Char('0')).toUpper();
        parts << QString("macByte=%1").arg(static_cast<qulonglong>(ev.macMessageOffset));
    }
    if (ev.identifierKnown) parts << QString("id=%1").arg(static_cast<int>(ev.identifier));
    if (ev.channelType) parts << QString("type=%1").arg(static_cast<int>(ev.channelType));
    if (ev.slotsPerCarrier > 1) parts << QString("tdma-slots=%1").arg(ev.slotsPerCarrier);
    if (ev.baseFrequencyHz > 0.0) parts << QString("base=%1MHz").arg(ev.baseFrequencyHz / 1e6, 0, 'f', 5);
    if (ev.channelSpacingHz > 0.0) parts << QString("step=%1kHz").arg(ev.channelSpacingHz / 1000.0, 0, 'f', 3);
    if (ev.talkgroupId) parts << QString("tg=%1").arg(ev.talkgroupId);
    if (ev.sourceId) parts << QString("src=%1").arg(p25HexId(ev.sourceId, 6));
    if (ev.channel || (identifierUpdate && ev.identifierKnown)) {
        parts << QString(identifierUpdate ? "table=%1" : "ch=%1").arg(p25ChannelText(ev.channel));
    }
    if (ev.channelB) parts << QString("chB=%1").arg(p25ChannelText(ev.channelB));
    if (ev.explicitChannelKnown && ev.explicitChannel.valid) {
        parts << QString("protoTx=%1").arg(p25ChannelText(ev.explicitChannel.protocolTransmitChannel));
        parts << QString("protoRx=%1").arg(p25ChannelText(ev.explicitChannel.protocolReceiveChannel));
        parts << QString("scannerDownlink=%1").arg(p25ChannelText(ev.explicitChannel.scannerDownlinkChannel));
        parts << QString("subscriberUplink=%1").arg(p25ChannelText(ev.explicitChannel.subscriberUplinkChannel));
        parts << QString("downlink=%1").arg(p25ChannelText(ev.explicitChannel.downlinkChannel));
        parts << QString("uplink=%1").arg(p25ChannelText(ev.explicitChannel.uplinkChannel));
        if (ev.explicitChannel.downlinkHz > 0.0) {
            parts << QString("downlinkFreq=%1MHz").arg(ev.explicitChannel.downlinkHz / 1e6, 0, 'f', 5);
        }
        if (ev.explicitChannel.uplinkHz > 0.0) {
            parts << QString("uplinkFreq=%1MHz").arg(ev.explicitChannel.uplinkHz / 1e6, 0, 'f', 5);
        }
    }
    if (!identifierUpdate && ev.channel && ev.identifierKnown && ev.channelSpacingHz > 0.0 && ev.baseFrequencyHz > 0.0) {
        const int slotCount = std::max(1, ev.slotsPerCarrier);
        const uint16_t rawNumber = static_cast<uint16_t>(ev.channel & 0x0fffu);
        const uint16_t carrierNumber = static_cast<uint16_t>(rawNumber / static_cast<uint16_t>(slotCount));
        parts << QString("rawNumber=0x%1").arg(rawNumber, 3, 16, QLatin1Char('0')).toUpper();
        parts << QString("carrier=%1").arg(carrierNumber);
        parts << QString("calcSlot=%1").arg(rawNumber % static_cast<uint16_t>(slotCount));
        parts << QString("calcBase=%1MHz").arg(ev.baseFrequencyHz / 1e6, 0, 'f', 5);
        parts << QString("calcStep=%1k").arg(ev.channelSpacingHz / 1000.0, 0, 'f', 3);
    }
    if (ev.nacKnown) parts << QString("nac=%1").arg(p25HexId(ev.nac, 3));
    if (ev.networkStatusKnown) parts << QString("wacn=%1").arg(p25HexId(ev.wacn, 5));
    if (ev.networkStatusKnown || ev.rfssStatusKnown) parts << QString("sys=%1").arg(p25HexId(ev.systemId, 3));
    if (ev.rfssStatusKnown) {
        parts << QString("rfss=%1").arg(static_cast<int>(ev.rfssId));
        parts << QString("site=%1").arg(static_cast<int>(ev.siteId));
    }
    if (ev.networkStatusKnown || ev.rfssStatusKnown) parts << QString("lra=%1").arg(p25HexId(ev.lra, 2));
    if (ev.rfssStatusKnown && ev.rfssNetworkActiveKnown) parts << (ev.rfssNetworkActive ? "rfss-active" : "rfss-failsoft");
    if (ev.controlChannel) parts << QString("cc=%1").arg(p25ChannelText(ev.controlChannel));
    if (ev.controlChannelB) parts << QString("ccB=%1").arg(p25ChannelText(ev.controlChannelB));
    if (ev.voiceFrequencyHz > 0.0) parts << QString("voice=%1MHz").arg(ev.voiceFrequencyHz / 1e6, 0, 'f', 5);
    if (ev.voiceFrequencyHzB > 0.0) parts << QString("voiceB=%1MHz").arg(ev.voiceFrequencyHzB / 1e6, 0, 'f', 5);
    if (ev.channelFrequencyHz > 0.0) parts << QString("freq=%1MHz").arg(ev.channelFrequencyHz / 1e6, 0, 'f', 5);
    if (ev.channelFrequencyHzB > 0.0) parts << QString("freqB=%1MHz").arg(ev.channelFrequencyHzB / 1e6, 0, 'f', 5);
    if (ev.controlChannelFrequencyHz > 0.0) parts << QString("ccFreq=%1MHz").arg(ev.controlChannelFrequencyHz / 1e6, 0, 'f', 5);
    if (ev.controlChannelFrequencyHzB > 0.0) parts << QString("ccFreqB=%1MHz").arg(ev.controlChannelFrequencyHzB / 1e6, 0, 'f', 5);
    if (ev.serviceOptionsKnown) {
        parts << QString("svc=0x%1").arg(ev.serviceOptions, 2, 16, QLatin1Char('0')).toUpper();
        if (ev.serviceEmergency) parts << "emergency";
        if (ev.serviceDuplexFull) parts << "duplex=full";
        if (ev.servicePacketMode) parts << "service=packet";
        parts << QString("priority=%1").arg(static_cast<int>(ev.servicePriority));
    }
    if (ev.pttEncryptionSyncKnown) {
        parts << QString("mi=%1").arg(p25MessageIndicatorText(ev.messageIndicator));
        parts << QString("alg=0x%1").arg(ev.algorithmId, 2, 16, QLatin1Char('0')).toUpper();
        parts << QString("kid=0x%1").arg(ev.keyId, 4, 16, QLatin1Char('0')).toUpper();
    }
    if (ev.endPttNacKnown) parts << QString("endNac=%1").arg(p25HexId(ev.endPttNac, 3));
    if (ev.encryptionKnown) parts << (ev.encrypted ? "encrypted" : "clear");
    if (ev.voiceProtocol != P25VoiceProtocol::Unknown) {
        parts << QString::fromStdString(p25VoiceProtocolToString(ev.voiceProtocol));
    }
    if (ev.tdmaSlotKnown) parts << QString("slot=%1").arg(static_cast<int>(ev.tdmaSlot));
    if (ev.phase2Candidate) parts << "phase2-candidate";
    return parts.join(" | ");
}

QString p25GrantDetailLogText(const P25ControlEvent& ev)
{
    QStringList parts;
    const uint8_t identifier = static_cast<uint8_t>((ev.channel >> 12) & 0x0f);
    const uint16_t rawNumber = static_cast<uint16_t>(ev.channel & 0x0fffu);
    const int slotCount = std::max(1, ev.slotsPerCarrier);
    const uint16_t carrierNumber = static_cast<uint16_t>(rawNumber / static_cast<uint16_t>(slotCount));
    const uint16_t slot = static_cast<uint16_t>(rawNumber % static_cast<uint16_t>(slotCount));
    parts << QString("Grant: TG=%1").arg(ev.talkgroupId);
    if (ev.sourceId) parts << QString("SRC=%1").arg(p25HexId(ev.sourceId, 6));
    parts << QString("CH=%1").arg(p25ChannelText(ev.channel));
    parts << QString("ID=%1").arg(static_cast<int>(identifier));
    parts << QString("CHAN=0x%1").arg(rawNumber, 3, 16, QLatin1Char('0')).toUpper();
    parts << QString("CARRIER=%1").arg(carrierNumber);
    if (ev.tdmaSlotKnown || slotCount > 1) {
        parts << QString("SLOT=%1").arg(ev.tdmaSlotKnown ? static_cast<int>(ev.tdmaSlot) : static_cast<int>(slot));
    }
    if (ev.explicitChannelKnown && ev.explicitChannel.valid) {
        parts << QString("PROTO_TX=%1").arg(p25ChannelText(ev.explicitChannel.protocolTransmitChannel));
        parts << QString("PROTO_RX=%1").arg(p25ChannelText(ev.explicitChannel.protocolReceiveChannel));
        parts << QString("DOWNLINK=%1").arg(p25ChannelText(ev.explicitChannel.downlinkChannel));
        parts << QString("UPLINK=%1").arg(p25ChannelText(ev.explicitChannel.uplinkChannel));
    }
    if (ev.voiceFrequencyHz > 0.0) parts << QString("FREQ=%1MHz").arg(ev.voiceFrequencyHz / 1e6, 0, 'f', 5);
    else parts << "FREQ=pending";
    if (ev.voiceFrequencyHzB > 0.0) parts << QString("FREQ_B=%1MHz").arg(ev.voiceFrequencyHzB / 1e6, 0, 'f', 5);
    parts << QString("PHASE2=%1").arg(ev.phase2Candidate ? "yes" : "no");
    parts << QString("ENC=%1").arg(ev.encryptionKnown ? (ev.encrypted ? "encrypted" : "clear") : "unknown");
    if (ev.serviceOptionsKnown) {
        parts << QString("SVC=0x%1").arg(ev.serviceOptions, 2, 16, QLatin1Char('0')).toUpper();
        if (ev.serviceEmergency) parts << "EMERGENCY";
        if (ev.serviceDuplexFull) parts << "DUPLEX=full";
        if (ev.servicePacketMode) parts << "SERVICE=packet";
        parts << QString("PRI=%1").arg(static_cast<int>(ev.servicePriority));
    }
    if (ev.pttEncryptionSyncKnown) {
        parts << QString("ALG=0x%1").arg(ev.algorithmId, 2, 16, QLatin1Char('0')).toUpper();
        parts << QString("KID=0x%1").arg(ev.keyId, 4, 16, QLatin1Char('0')).toUpper();
    }
    if (ev.endPttNacKnown) parts << QString("END_NAC=%1").arg(p25HexId(ev.endPttNac, 3));
    parts << QString("OP=0x%1").arg(ev.opcode, 2, 16, QLatin1Char('0')).toUpper();
    if (ev.phase2Mac) {
        parts << QString("MACPDU=%1").arg(QString::fromStdString(p25Phase2MacPduTypeToString(ev.macPduType)));
        parts << QString("MACMSG=0x%1").arg(ev.macMessageOpcode, 2, 16, QLatin1Char('0')).toUpper();
    }
    parts << QString("MFID=0x%1").arg(ev.mfid, 2, 16, QLatin1Char('0')).toUpper();
    return parts.join(' ');
}

QString p25FollowDetailLogText(const P25TalkgroupEntry& tg)
{
    const bool phase2 = p25TalkgroupIsPhase2(tg);
    QString text = QString("Following %1: freq=%2MHz tg=%3 src=%4 control=%5MHz enc=%6")
        .arg(phase2 ? "Phase 2" : "Phase 1")
        .arg(tg.lastVoiceFreqHz / 1e6, 0, 'f', 5)
        .arg(tg.talkgroupId)
        .arg(tg.lastSourceId ? p25HexId(tg.lastSourceId, 6) : QString("-"))
        .arg(tg.controlFreqHz / 1e6, 0, 'f', 5)
        .arg(tg.encryptionKnown ? (tg.encrypted ? "encrypted" : "clear") : "unknown");
    if (phase2) {
        text += QString(" slot=%1 mask=%2. TDMA sync: searching; MAC/ESS: pending.")
            .arg(tg.tdmaSlotKnown ? QString::number(static_cast<int>(tg.tdmaSlot)) : QString("unknown"))
            .arg(tg.p25MaskParamsKnown ? "known" : "unknown");
    } else {
        text += ". NID/IMBE sync: searching.";
    }
    return text;
}

void populateP25TalkgroupTable(QTableWidget* table, const std::vector<P25TalkgroupEntry>& talkgroups)
{
    if (!table) return;
    table->setRowCount(static_cast<int>(talkgroups.size()));
    for (int row = 0; row < static_cast<int>(talkgroups.size()); ++row) {
        const auto& tg = talkgroups[static_cast<size_t>(row)];
        QString status = tg.scannerEnabled ? "Scanner"
                       : tg.verified ? "Verified"
                       : "Discovered";
        QString protocol = p25TalkgroupIsPhase2(tg) ? "P2" : p25VoiceProtocolShort(tg.voiceProtocol);
        if (tg.tdmaSlotKnown) protocol += QString(" S%1").arg(static_cast<int>(tg.tdmaSlot));
        if (p25TalkgroupIsPhase2(tg) && tg.p25MaskParamsKnown) protocol += " Meta";
        if (protocol != "-") status = protocol + " / " + status;
        table->setItem(row, 0, new QTableWidgetItem(QString::number(tg.controlFreqHz / 1e6, 'f', 5)));
        table->setItem(row, 1, new QTableWidgetItem(QString::number(tg.talkgroupId)));
        table->setItem(row, 2, new QTableWidgetItem(QString::fromStdString(tg.alphaTag)));
        table->setItem(row, 3, new QTableWidgetItem(tg.lastVoiceFreqHz > 0.0 ? QString::number(tg.lastVoiceFreqHz / 1e6, 'f', 5) : "-"));
        table->setItem(row, 4, new QTableWidgetItem(tg.lastSourceId ? p25HexId(tg.lastSourceId, 6) : "-"));
        table->setItem(row, 5, new QTableWidgetItem(QString::number(tg.hitCount)));
        table->setItem(row, 6, new QTableWidgetItem(QString::number(tg.userPriority)));
        table->setItem(row, 7, new QTableWidgetItem(tg.encryptionKnown ? (tg.encrypted ? "Yes" : "No") : "Unknown"));
        table->setItem(row, 8, new QTableWidgetItem(status));
        table->setItem(row, 9, new QTableWidgetItem(p25TimeText(tg.lastSeenMs)));
    }
}

bool sameP25Talkgroup(const P25TalkgroupEntry& tg, double controlFreqHz, uint32_t talkgroupId)
{
    return tg.talkgroupId == talkgroupId && std::abs(tg.controlFreqHz - controlFreqHz) <= 50.0;
}

static bool p25ControlEventHasTalkgroupActivity(const P25ControlEvent& event)
{
    return event.talkgroupId != 0 &&
        (p25ControlEventIsVoiceGrant(event) ||
         event.type == P25ControlEventType::GroupVoiceUser ||
         event.type == P25ControlEventType::GroupVoiceEnd);
}

bool p25ControlEventHasResolvedVoiceFrequency(const P25ControlEvent& event)
{
    return event.voiceFrequencyHz > 0.0;
}

bool p25FollowTrafficDecodeUnlocked(const Receiver& rx) noexcept
{
    const auto& d = rx.p25VoiceDiagnostics;
    return d.phase2FedToMbelib == 0 &&
           d.phase2AmbeAcceptedFrames == 0 &&
           d.phase2MaskedBursts == 0;
}

bool p25ControlEventIsResolvedVoiceGrant(const P25ControlEvent& event)
{
    return p25ControlEventIsVoiceGrant(event) &&
        event.talkgroupId != 0 &&
        p25ControlEventHasResolvedVoiceFrequency(event) &&
        std::isfinite(event.voiceFrequencyHz) &&
        event.voiceFrequencyHz > 1e6;
}

double p25SanitizedSameCallFollowVoiceHz(const P25ControlEvent& event,
                                                double liveVoiceHz,
                                                double grantVoiceHz,
                                                bool trafficDecodeUnlocked)
{
    if (grantVoiceHz <= 0.0 || !std::isfinite(grantVoiceHz)) {
        return liveVoiceHz > 0.0 ? liveVoiceHz : grantVoiceHz;
    }
    if (liveVoiceHz <= 0.0 || !std::isfinite(liveVoiceHz)) return grantVoiceHz;
    const double deltaHz = std::abs(liveVoiceHz - grantVoiceHz);
    if (deltaHz <= 50.0) return grantVoiceHz;
    if (event.type == P25ControlEventType::GroupVoiceGrant) return grantVoiceHz;
    if (event.type == P25ControlEventType::GroupVoiceUpdate &&
        deltaHz > kP25SameCallGrantUpdateMaxMHzHopHz) {
        if (trafficDecodeUnlocked &&
            deltaHz <= kP25SameCallGrantUpdateCorrectionMaxMHzHopHz) {
            return grantVoiceHz;
        }
        return liveVoiceHz;
    }
    return grantVoiceHz;
}

bool p25GrantAuthorizesSameCallVoiceMHzHop(const P25ControlEvent& event,
                                                  double liveVoiceHz,
                                                  double grantVoiceHz,
                                                  qint64 dwellSinceTuneMs,
                                                  qint64 dwellSinceLastHopMs,
                                                  bool trafficDecodeUnlocked)
{
    if (grantVoiceHz <= 0.0 || liveVoiceHz <= 0.0) return false;
    const double deltaHz = std::abs(liveVoiceHz - grantVoiceHz);
    if (deltaHz <= 50.0) return false;
    if (event.type == P25ControlEventType::GroupVoiceGrant ||
        event.type == P25ControlEventType::GroupVoiceGrantExplicit) {
        return deltaHz <= kP25SameCallResolvedGrantMaxMHzHopHz;
    }
    const qint64 minHopDwellMs = trafficDecodeUnlocked
        ? kP25SameCallDecodeUnlockedHopMinDwellMs
        : kP25SameCallMinMHzHopDwellMs;
    if (dwellSinceLastHopMs >= 0 && dwellSinceLastHopMs < minHopDwellMs) {
        return false;
    }
    if (deltaHz <= 12500.0) return dwellSinceTuneMs >= 0;
    if (event.type == P25ControlEventType::GroupVoiceUpdate) {
        if (trafficDecodeUnlocked &&
            deltaHz <= kP25SameCallGrantUpdateCorrectionMaxMHzHopHz) {
            return dwellSinceTuneMs >= kP25SameCallDecodeUnlockedHopMinDwellMs;
        }
        return deltaHz <= kP25SameCallGrantUpdateMaxMHzHopHz;
    }
    return deltaHz <= kP25SameCallGrantUpdateMaxMHzHopHz;
}

static bool p25ResolveVoiceGrantFromAnalyzer(P25ControlEvent& event,
                                             const P25ControlChannelAnalyzer& analyzer)
{
    if (!p25ControlEventIsVoiceGrant(event) || event.channel == 0) return false;
    auto freq = analyzer.channelToFrequencyHz(event.channel);
    if (!freq.has_value() || !std::isfinite(*freq) || *freq <= 1e6) return false;

    const uint8_t id = static_cast<uint8_t>((event.channel >> 12) & 0x0f);
    const auto& identifiers = analyzer.channelIdentifiers();
    if (id >= identifiers.size() || !p25ChannelIdentifierUsable(identifiers[id])) return false;
    const auto& plan = identifiers[id];
    event.voiceFrequencyHz = *freq;
    event.identifierKnown = true;
    event.identifier = id;
    event.channelType = plan.channelType;
    event.slotsPerCarrier = std::max(1, plan.slotsPerCarrier);
    event.baseFrequencyHz = plan.baseHz;
    event.channelSpacingHz = plan.spacingHz;
    event.transmitOffsetHz = plan.txOffsetHz;
    event.bandwidthHz = plan.bandwidthHz;
    event.phase2Candidate = event.phase2Candidate || plan.phase2Capable || plan.slotsPerCarrier > 1;
    if (plan.slotsPerCarrier > 1) {
        event.voiceProtocol = P25VoiceProtocol::Phase2TDMA;
        event.tdmaSlotKnown = true;
        event.tdmaSlot = static_cast<uint8_t>((event.channel & 0x0fffu) %
                                              static_cast<uint16_t>(std::max(1, plan.slotsPerCarrier)));
    } else if (event.voiceProtocol == P25VoiceProtocol::Unknown) {
        event.voiceProtocol = P25VoiceProtocol::Phase1FDMA;
    }
    if (event.channelB != 0) {
        if (auto freqB = analyzer.channelToFrequencyHz(event.channelB)) event.voiceFrequencyHzB = *freqB;
    }
    return true;
}

bool p25RememberPendingVoiceGrant(std::vector<P25PendingVoiceGrant>& pendingGrants,
                                         const P25ControlEvent& ev,
                                         int correctedDibitErrors,
                                         qint64 nowMs)
{
    if (!p25ControlEventIsVoiceGrant(ev) || p25ControlEventIsResolvedVoiceGrant(ev) ||
        ev.talkgroupId == 0 || ev.channel == 0) {
        return false;
    }

    pendingGrants.erase(std::remove_if(pendingGrants.begin(), pendingGrants.end(),
        [nowMs](const P25PendingVoiceGrant& pending) {
            return nowMs - pending.lastSeenMs > kP25PendingGrantTtlMs;
        }), pendingGrants.end());

    auto it = std::find_if(pendingGrants.begin(), pendingGrants.end(), [&](const auto& pending) {
        return pending.event.talkgroupId == ev.talkgroupId && pending.event.channel == ev.channel;
    });
    if (it == pendingGrants.end()) {
        P25PendingVoiceGrant pending;
        pending.event = ev;
        pending.firstSeenMs = nowMs;
        pending.lastSeenMs = nowMs;
        pending.correctedDibitErrors = correctedDibitErrors;
        pendingGrants.push_back(pending);
    } else {
        P25ControlEvent merged = ev;
        if (!merged.encryptionKnown && it->event.encryptionKnown && nowMs - it->lastSeenMs <= 5000) {
            merged.encryptionKnown = true;
            merged.encrypted = it->event.encrypted;
        }
        if (merged.sourceId == 0 && it->event.sourceId != 0) merged.sourceId = it->event.sourceId;
        it->event = std::move(merged);
        it->lastSeenMs = nowMs;
        it->correctedDibitErrors = correctedDibitErrors;
    }
    return true;
}

std::vector<P25ControlEvent> p25ResolvePendingVoiceGrants(std::vector<P25PendingVoiceGrant>& pendingGrants,
                                                                 const P25ControlChannelAnalyzer& analyzer,
                                                                 qint64 nowMs)
{
    std::vector<P25ControlEvent> resolvedGrants;
    if (pendingGrants.empty()) return resolvedGrants;

    std::vector<P25PendingVoiceGrant> stillPending;
    stillPending.reserve(pendingGrants.size());
    for (auto pending : pendingGrants) {
        if (nowMs - pending.lastSeenMs > kP25PendingGrantTtlMs) continue;

        P25ControlEvent resolved = pending.event;
        if (!p25ResolveVoiceGrantFromAnalyzer(resolved, analyzer)) {
            stillPending.push_back(std::move(pending));
            continue;
        }
        analyzer.annotateCurrentSystemMetadata(resolved);
        resolvedGrants.push_back(std::move(resolved));
    }

    pendingGrants = std::move(stillPending);
    return resolvedGrants;
}

static bool p25ControlEventIsResolvedPhase2VoiceGrant(const P25ControlEvent& event)
{
    return p25ControlEventIsResolvedVoiceGrant(event) &&
        (event.voiceProtocol == P25VoiceProtocol::Phase2TDMA ||
         event.phase2Candidate ||
         event.slotsPerCarrier > 1 ||
         event.tdmaSlotKnown);
}

static QString p25RepeatedResolvedVoiceGrantKey(double controlFreqHz, const P25ControlEvent& event)
{
    const qint64 controlRoundedHz = static_cast<qint64>(std::llround(controlFreqHz));
    const qint64 voiceRoundedHz = static_cast<qint64>(std::llround(event.voiceFrequencyHz));
    const int slot = event.tdmaSlotKnown
        ? static_cast<int>(event.tdmaSlot & 0x01u)
        : (event.slotsPerCarrier > 1 ? static_cast<int>(event.channel & 0x0001u) : -1);
    return QString("%1:%2:%3:%4:%5:%6")
        .arg(controlRoundedHz)
        .arg(event.talkgroupId)
        .arg(event.channel)
        .arg(voiceRoundedHz)
        .arg(slot)
        .arg(static_cast<int>(event.voiceProtocol));
}

P25RepeatedVoiceGrantDecision p25RememberRepeatedHighCorrectionResolvedVoiceGrant(
    std::vector<P25RepeatedVoiceGrant>& repeatedGrants,
    double controlFreqHz,
    const P25ControlEvent& event,
    int correctedDibitErrors,
    qint64 nowMs)
{
    P25RepeatedVoiceGrantDecision decision;
    if (correctedDibitErrors <= kP25VoiceGrantMaxCorrectedDibits ||
        correctedDibitErrors > kP25RepeatedVoiceGrantMaxCorrectedDibits ||
        !p25ControlEventIsResolvedPhase2VoiceGrant(event) ||
        (event.encryptionKnown && event.encrypted) ||
        !std::isfinite(controlFreqHz) ||
        controlFreqHz <= 0.0) {
        return decision;
    }

    repeatedGrants.erase(std::remove_if(repeatedGrants.begin(), repeatedGrants.end(),
        [nowMs](const P25RepeatedVoiceGrant& seen) {
            return nowMs - seen.lastSeenMs > kP25RepeatedVoiceGrantTtlMs;
        }), repeatedGrants.end());

    const QString key = p25RepeatedResolvedVoiceGrantKey(controlFreqHz, event);
    decision.considered = true;
    decision.key = key;

    auto it = std::find_if(repeatedGrants.begin(), repeatedGrants.end(), [&](const auto& seen) {
        return seen.key == key;
    });
    if (it == repeatedGrants.end()) {
        P25RepeatedVoiceGrant seen;
        seen.key = key;
        seen.event = event;
        seen.firstSeenMs = nowMs;
        seen.lastSeenMs = nowMs;
        seen.hitCount = 1;
        seen.bestCorrectedDibitErrors = correctedDibitErrors;
        repeatedGrants.push_back(std::move(seen));
        it = std::prev(repeatedGrants.end());
    } else {
        it->event = event;
        it->lastSeenMs = nowMs;
        it->hitCount = std::max(1, it->hitCount) + 1;
        it->bestCorrectedDibitErrors = std::min(it->bestCorrectedDibitErrors, correctedDibitErrors);
    }

    decision.hitCount = it->hitCount;
    decision.bestCorrectedDibitErrors = it->bestCorrectedDibitErrors;
    decision.promoted = it->hitCount >= kP25RepeatedVoiceGrantMinHits;
    // Explicit clear OP=0x00 GroupVoiceGrant: promote on the first CRC-valid
    // sighting inside the repeated-grant correction band.  Capture 080701 missed
    // TG30302 clear (corrected_dibits=13, hits=1/2) and only chased a later
    // OP=0x02 update ~5s late — by then the call was already thin/wrong-slot.
    // Encrypted grants stay fail-closed via the encrypted auto-follow gate; this
    // only accelerates clear SVC grants that already carry service options.
    if (!decision.promoted &&
        event.encryptionKnown && !event.encrypted &&
        event.type == P25ControlEventType::GroupVoiceGrant) {
        decision.promoted = true;
    }
    return decision;
}

bool p25TsbkEventRegistryEligible(int correctedDibitErrors, const P25ControlEvent& event)
{
    if (correctedDibitErrors <= kP25RegistryMaxCorrectedDibits) return true;

    // Resolved voice grants are actionable retune commands, so they use the
    // same strict correction gate as persistent state. Weak grant-shaped blocks
    // are logged for diagnosis instead of being allowed to send the radio away.
    return correctedDibitErrors <= kP25VoiceGrantMaxCorrectedDibits &&
        p25ControlEventIsResolvedVoiceGrant(event);
}

bool p25TsbkPendingVoiceGrantEligible(int correctedDibitErrors, const P25ControlEvent& event)
{
    // Do not let weak unresolved grants mutate persistent talkgroup/site state.
    // They are retained only while they remain inside the strict action gate;
    // higher-correction grant-shaped blocks are diagnostic noise until another
    // independently trusted message repeats the instruction.
    return correctedDibitErrors <= kP25PendingVoiceGrantMaxCorrectedDibits &&
        p25ControlEventIsVoiceGrant(event) &&
        !p25ControlEventIsResolvedVoiceGrant(event) &&
        event.talkgroupId != 0 &&
        event.channel != 0;
}

bool p25TsbkSessionIdentifierEligible(int correctedDibitErrors, const P25ControlEvent& event)
{
    return correctedDibitErrors <= kP25SessionIdentifierMaxCorrectedDibits &&
        event.type == P25ControlEventType::IdentifierUpdate &&
        p25ChannelIdentifierUsable(p25IdentifierFromEvent(event));
}

bool mergeP25TalkgroupEvent(std::vector<P25TalkgroupEntry>& talkgroups,
                                   double controlFreqHz,
                                   const P25ControlEvent& event,
                                   qint64 nowMs)
{
    if (!p25ControlEventHasTalkgroupActivity(event) || !std::isfinite(controlFreqHz) || controlFreqHz <= 0.0) return false;
    auto it = std::find_if(talkgroups.begin(), talkgroups.end(), [&](const P25TalkgroupEntry& tg) {
        return sameP25Talkgroup(tg, controlFreqHz, event.talkgroupId) &&
            p25TalkgroupScopeCompatible(tg, event);
    });
    if (it == talkgroups.end()) {
        P25TalkgroupEntry tg;
        tg.controlFreqHz = controlFreqHz;
        tg.talkgroupId = event.talkgroupId;
        tg.firstSeenMs = nowMs;
        tg.lastSeenMs = nowMs;
        talkgroups.push_back(tg);
        it = std::prev(talkgroups.end());
    }

    it->lastSeenMs = nowMs;
    it->hitCount = std::max(0, it->hitCount) + 1;
    // Rolling activity for auto most-active / priority scoring (roadmap).
    it->activityScore = std::min(it->activityScore + 1, 1000000);
    if (event.sourceId != 0) it->lastSourceId = event.sourceId;
    if (event.channel != 0) it->lastChannel = event.channel;
    if (event.voiceFrequencyHz > 0.0) it->lastVoiceFreqHz = event.voiceFrequencyHz;
    if (event.voiceProtocol != P25VoiceProtocol::Unknown) {
        it->voiceProtocol = event.voiceProtocol;
        it->phase2Candidate = event.phase2Candidate;
        if (!event.tdmaSlotKnown) {
            it->tdmaSlotKnown = false;
            it->tdmaSlot = 0;
        }
    } else if (event.phase2Candidate) {
        it->phase2Candidate = true;
    }
    if (event.tdmaSlotKnown) {
        it->tdmaSlotKnown = true;
        it->tdmaSlot = event.tdmaSlot;
    }
    p25ApplyControlEventSystemMetadata(*it, event);
    p25AugmentTalkgroupFromKnownSite(*it, talkgroups, controlFreqHz);
    if (event.encryptionKnown) {
        it->encryptionKnown = true;
        it->encrypted = event.encrypted;
    }
    return true;
}

bool p25DecodeResultHasNidLock(const P25LiveDecodeResult& result)
{
    return result.stats.bestNidValid ||
        std::any_of(result.nids.begin(), result.nids.end(), [](const P25Nid& nid) {
            return nid.fecValidated;
        });
}

QString p25LiveLockStageText(const P25LiveDecodeResult& result, size_t trustedTsbk)
{
    const bool nidLock = p25DecodeResultHasNidLock(result);
    if (result.stats.phase2EssKnown && !result.stats.phase2EssEncrypted &&
        result.stats.phase2MacCrcValid > 0 && result.stats.phase2MaskedBursts > 0) {
        return "ClearVoiceReady";
    }
    if (result.stats.phase2EssKnown) {
        if (result.stats.phase2EssEncrypted) {
            return result.stats.phase2MacCrcValid > 0 ? "Phase2EssEncrypted" : "Phase2EssEncryptedPendingMac";
        }
        return "Phase2EssClear";
    }
    if (result.stats.phase2MacCrcValid > 0) return "Phase2MacCrcValid";
    if (result.stats.phase2MaskedBursts > 0) return "Phase2MaskHypothesis";
    if (result.stats.phase2SuperframeBursts > 0) return "Phase2SuperframeSync";
    if (result.stats.phase1PduCrcValid > 0) return "TrustedPhase1Pdu";
    if (trustedTsbk > 0) return "TrustedTsbk";
    if (nidLock) return "Phase1NidValid";
    if (result.stats.phase2Bursts > 0) return "Phase2BurstTelemetry";
    if (!result.syncs.empty()) return "FrameSyncCandidate";
    if (result.stats.symbols > 0) return "SymbolStream";
    if (result.stats.inputSamples > 0) return "IqPresent";
    return "NoSignal";
}

std::string p25ControlAuditTsbkKey(const std::vector<uint8_t>& bytes)
{
    if (bytes.empty()) return "TSBK op=unknown mfid=unknown";
    const uint8_t opcode = static_cast<uint8_t>(bytes[0] & 0x3f);
    const uint8_t mfid = bytes.size() > 1 ? bytes[1] : 0;
    return QString("TSBK op=0x%1 mfid=0x%2")
        .arg(static_cast<int>(opcode), 2, 16, QLatin1Char('0'))
        .arg(static_cast<int>(mfid), 2, 16, QLatin1Char('0'))
        .toUpper()
        .toStdString();
}

std::string p25ControlAuditPhase2MacKey(const P25Phase2MacPdu& pdu)
{
    return QString("P2MAC type=0x%1 offset=%2")
        .arg(static_cast<int>(pdu.opcode), 2, 16, QLatin1Char('0'))
        .arg(static_cast<int>(pdu.offset))
        .toUpper()
        .toStdString();
}

std::string p25ControlAuditPhase1PduKey(const P25Phase1PduMessage& pdu)
{
    return QString("P1PDU format=%1 vendor=0x%2 op=0x%3")
        .arg(static_cast<int>(pdu.format))
        .arg(static_cast<int>(pdu.vendor), 2, 16, QLatin1Char('0'))
        .arg(static_cast<int>(pdu.opcode), 2, 16, QLatin1Char('0'))
        .toUpper()
        .toStdString();
}

QString p25ControlAuditOpsText(const std::map<std::string, size_t>& ops)
{
    if (ops.empty()) return "none";
    QStringList parts;
    int shown = 0;
    for (const auto& [op, count] : ops) {
        if (shown++ >= 10) {
            parts << QString("+%1 more").arg(static_cast<int>(ops.size()) - 10);
            break;
        }
        parts << QString("%1 x%2").arg(QString::fromStdString(op)).arg(static_cast<qulonglong>(count));
    }
    return parts.join(", ");
}

