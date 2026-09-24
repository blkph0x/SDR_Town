#pragma once

// Purpose: P25 talkgroup / CC / channel-ID persistence and grant helpers.
// Spec:    docs/DECISIONS.md DEC-0040 / ISS-0004 Phase 2 (mechanical extract from main.cpp)
// Invariants: no hop/TTL/CADENCE/feed-gate behavior changes — move-only.

#include "P25Control.h"
#include "P25LiveDecoder.h"
#include "Receiver.h"

#include <QString>
#include <QTableWidget>
#include <QDateTime>
#include <QtGlobal>

#include <array>
#include <cstdint>
#include <cmath>
#include <limits>
#include <map>
#include <string>
#include <vector>

struct P25TalkgroupEntry {
    double controlFreqHz = 0.0;
    uint32_t talkgroupId = 0;
    std::string alphaTag;
    uint32_t lastSourceId = 0;
    uint16_t lastChannel = 0;
    double lastVoiceFreqHz = 0.0;
    P25VoiceProtocol voiceProtocol = P25VoiceProtocol::Unknown;
    bool phase2Candidate = false;
    uint8_t tdmaSlot = 0;
    bool tdmaSlotKnown = false;
    bool p25MaskParamsKnown = false;
    uint16_t nac = 0;
    uint32_t wacn = 0;
    uint16_t systemId = 0;
    uint8_t rfssId = 0;
    uint8_t siteId = 0;
    int hitCount = 0;
    bool encryptionKnown = false;
    bool encrypted = false;
    bool verified = false;
    bool scannerEnabled = false;
    // User priority for auto-follow preempt (higher wins). 0 = unset/default.
    // Persisted in p25_talkgroups.json; roadmap item "user priority controls".
    int userPriority = 0;
    // Rolling activity score (hit density proxy) for auto most-active promotion.
    int activityScore = 0;
    qint64 firstSeenMs = 0;
    qint64 lastSeenMs = 0;
};

struct P25KnownControlChannel {
    double freqHz = 0.0;
    std::string label;
    qint64 createdMs = 0;
    qint64 lastUsedMs = 0;
};

// DEC-0111/A07: enrichment is site/control-source scoped, never TGID-only.
inline bool p25SameMetadataSource(const P25TalkgroupEntry& a, const P25TalkgroupEntry& b) {
    if (!(a.controlFreqHz > 0) || !(b.controlFreqHz > 0) ||
        !std::isfinite(a.controlFreqHz) || !std::isfinite(b.controlFreqHz) ||
        std::abs(a.controlFreqHz - b.controlFreqHz) > 50.0) return false;
    if (a.p25MaskParamsKnown && b.p25MaskParamsKnown &&
        (a.wacn != b.wacn || a.systemId != b.systemId || a.nac != b.nac)) return false;
    return !(a.rfssId && b.rfssId && a.rfssId != b.rfssId) &&
           !(a.siteId && b.siteId && a.siteId != b.siteId);
}

inline QString p25TalkgroupKey(const P25TalkgroupEntry& tg) {
    return QString("%1:%2:%3:%4:%5").arg(tg.controlFreqHz, 0, 'f', 0).arg(tg.talkgroupId)
        .arg(tg.p25MaskParamsKnown ? 1 : 0).arg(tg.wacn).arg(tg.systemId);
}

inline int selectedP25TalkgroupIndex(const QTableWidget* table, const std::vector<P25TalkgroupEntry>& entries) {
    const auto* item = table ? table->item(table->currentRow(), 0) : nullptr;
    if (!item) return -1;
    const auto key = item->data(Qt::UserRole).toString();
    for (size_t i = 0; i < entries.size(); ++i)
        if (!key.isEmpty() && p25TalkgroupKey(entries[i]) == key) return static_cast<int>(i);
    return -1;
}

inline bool selectP25Talkgroup(QTableWidget* table, const P25TalkgroupEntry& entry) {
    if (!table) return false;
    const auto key = p25TalkgroupKey(entry);
    for (int row = 0; row < table->rowCount(); ++row) {
        const auto* item = table->item(row, 0);
        if (item && item->data(Qt::UserRole).toString() == key) {
            table->setCurrentCell(row, 0);
            table->selectRow(row);
            return true;
        }
    }
    return false;
}

struct P25CachedChannelIdentifier {
    double controlFreqHz = 0.0;
    uint16_t nac = 0;
    uint32_t wacn = 0;
    uint16_t systemId = 0;
    uint8_t rfssId = 0;
    uint8_t siteId = 0;
    P25ChannelIdentifier identifier;
    qint64 firstSeenMs = 0;
    qint64 lastSeenMs = 0;
};

struct P25PendingVoiceGrant {
    P25ControlEvent event;
    qint64 firstSeenMs = 0;
    qint64 lastSeenMs = 0;
    int correctedDibitErrors = 0;
};

struct P25RepeatedVoiceGrant {
    QString key;
    P25ControlEvent event;
    qint64 firstSeenMs = 0;
    qint64 lastSeenMs = 0;
    int hitCount = 0;
    int bestCorrectedDibitErrors = std::numeric_limits<int>::max();
};

struct P25RepeatedVoiceGrantDecision {
    bool considered = false;
    bool promoted = false;
    QString key;
    int hitCount = 0;
    int bestCorrectedDibitErrors = std::numeric_limits<int>::max();
};

// Registry / grant / same-call hop thresholds (moved with helpers).
inline constexpr int kP25RegistryMaxCorrectedDibits = 10;
inline constexpr int kP25VoiceGrantMaxCorrectedDibits = 18;
// A CRC-valid voice grant must not be discarded just because its channel-ID
// table has not arrived yet. Once IDEN resolves it, the grant is subject to the
// same action threshold as a grant that happened to arrive after the table.
inline constexpr int kP25PendingVoiceGrantMaxCorrectedDibits = kP25VoiceGrantMaxCorrectedDibits;
static_assert(kP25PendingVoiceGrantMaxCorrectedDibits == kP25VoiceGrantMaxCorrectedDibits,
              "pending and resolved P25 voice grants must use the same trust budget");
inline constexpr int kP25RepeatedVoiceGrantMaxCorrectedDibits = 24;
inline constexpr int kP25RepeatedVoiceGrantMinHits = 2;
inline constexpr qint64 kP25RepeatedVoiceGrantTtlMs = 5000;
inline constexpr int kP25SessionIdentifierMaxCorrectedDibits = 32;
inline constexpr double kP25SameCallGrantUpdateMaxMHzHopHz = 125000.0;
inline constexpr double kP25SameCallGrantUpdateCorrectionMaxMHzHopHz = 2.5e6;
inline constexpr double kP25SameCallResolvedGrantMaxMHzHopHz = 10.0e6;
inline constexpr qint64 kP25SameCallMinMHzHopDwellMs = 8000;
inline constexpr qint64 kP25SameCallDecodeUnlockedHopMinDwellMs = 2000;
inline constexpr qint64 kP25PendingGrantTtlMs = 30000;

extern std::map<QString, qint64> gP25RecentExplicitEncryptedPhase2Grants;

std::vector<P25KnownControlChannel> loadP25KnownControlChannels();

bool upsertP25KnownControlChannel(double freqHz, std::string label);

bool p25ChannelIdentifierUsable(const P25ChannelIdentifier& identifier);


P25ChannelIdentifier p25IdentifierFromEvent(const P25ControlEvent& event);

bool upsertP25ChannelIdentifier(double controlFreqHz,
                                       const P25ControlEvent& event,
                                       qint64 nowMs = QDateTime::currentMSecsSinceEpoch());

size_t seedP25AnalyzerFromCachedChannelIdentifiers(P25ControlChannelAnalyzer& analyzer,
                                                          double controlFreqHz);

QString p25VoiceProtocolShort(P25VoiceProtocol protocol);

bool p25TalkgroupIsPhase2(const P25TalkgroupEntry& tg);

void p25PruneRecentExplicitEncryptedPhase2Grants(std::map<QString, qint64>& holds,
                                                        qint64 nowMs,
                                                        qint64 ttlMs = 15000);

void p25RememberExplicitEncryptedPhase2Grant(std::map<QString, qint64>& holds,
                                                    const P25ControlEvent& event,
                                                    const P25TalkgroupEntry* tg,
                                                    qint64 nowMs);

void p25RememberVoiceProvedEncryptedPhase2Grant(std::map<QString, qint64>& holds,
                                                       const P25TalkgroupEntry& tg,
                                                       qint64 nowMs);

qint64 p25RecentExplicitEncryptedPhase2GrantAgeMs(const std::map<QString, qint64>& holds,
                                                         const P25ControlEvent& event,
                                                         const P25TalkgroupEntry& tg,
                                                         qint64 nowMs,
                                                         qint64 ttlMs = 5000);

void p25ClearExplicitEncryptedPhase2GrantHold(std::map<QString, qint64>& holds,
                                                    const P25TalkgroupEntry& tg);

bool p25ShouldSuppressAnalogDemod(bool voiceDecodeEnabled,
                                         bool controlChannelMute,
                                         bool independentTrafficSource,
                                         bool voicePhase2) noexcept;

bool p25RecentSpeakerOutputActive(qint64 nowMs, qint64 holdMs) noexcept;

bool p25DiagTargetHardClear(const P25VoiceDiagSnapshot& diag) noexcept;

bool p25ActiveFollowTrafficDisprovesEncryption(const Receiver& rx) noexcept;

bool p25TalkgroupCanTuneForFollow(const P25TalkgroupEntry& tg);

bool p25TalkgroupGrantProvesSpeakerClear(const P25TalkgroupEntry& tg);

bool p25TalkgroupGrantProvesSpeakerEncrypted(const P25TalkgroupEntry& tg);

void p25PreserveTalkgroupEncryptionFromPrior(P25TalkgroupEntry& grantTg,
                                                    const P25TalkgroupEntry& prior);

bool p25PrepareTalkgroupForFollowGrant(P25TalkgroupEntry& tg,
                                              const P25ControlEvent& event,
                                              bool& probingUnknownPhase2EncryptedHistory);

bool p25TalkgroupHasUsableMaskMetadata(const P25TalkgroupEntry& tg);

P25TalkgroupEntry p25TalkgroupEntryFromCurrentGrant(double controlFreqHz,
                                                           const P25ControlEvent& event,
                                                           qint64 nowMs);

bool p25AugmentTalkgroupFromKnownSite(P25TalkgroupEntry& tg,
                                             const std::vector<P25TalkgroupEntry>& talkgroups,
                                             double controlFreqHz);

std::vector<P25TalkgroupEntry> loadP25Talkgroups();

bool p25RefreshFollowGrantFromRegistry(P25TalkgroupEntry& tg,
                                              const std::vector<P25TalkgroupEntry>& talkgroups,
                                              qint64 nowMs);

void saveP25Talkgroups(const std::vector<P25TalkgroupEntry>& talkgroups);

QString p25HexId(uint32_t value, int width);

QString p25BytesToHex(const std::vector<uint8_t>& bytes);

QString p25Phase2AcchStatsText(const P25LiveDecoderStats& stats);

QString p25Phase2AcchStatsText(const P25VoiceDiagSnapshot& diag);

QString p25Phase2MacPduHypothesisText(const P25Phase2MacPdu& pdu);

QString p25ChannelText(uint16_t channel);

QString p25EventLogText(const P25ControlEvent& ev);

QString p25GrantDetailLogText(const P25ControlEvent& ev);

QString p25FollowDetailLogText(const P25TalkgroupEntry& tg);
// Presentation-only: "TG 10120" or "TG 10120 AlphaTag" using registry + alias lists.
QString p25TalkgroupStatusLabel(uint32_t talkgroupId,
                                uint32_t wacn,
                                uint16_t systemId,
                                bool systemKnown);

void populateP25TalkgroupTable(QTableWidget* table, const std::vector<P25TalkgroupEntry>& talkgroups);

bool sameP25Talkgroup(const P25TalkgroupEntry& tg, double controlFreqHz, uint32_t talkgroupId);

bool p25ControlEventHasResolvedVoiceFrequency(const P25ControlEvent& event);

bool p25FollowTrafficDecodeUnlocked(const Receiver& rx) noexcept;

bool p25ControlEventIsResolvedVoiceGrant(const P25ControlEvent& event);

double p25SanitizedSameCallFollowVoiceHz(const P25ControlEvent& event,
                                                double liveVoiceHz,
                                                double grantVoiceHz,
                                                bool trafficDecodeUnlocked = false);

bool p25GrantAuthorizesSameCallVoiceMHzHop(const P25ControlEvent& event,
                                                  double liveVoiceHz,
                                                  double grantVoiceHz,
                                                  qint64 dwellSinceTuneMs,
                                                  qint64 dwellSinceLastHopMs,
                                                  bool trafficDecodeUnlocked = false);

bool p25RememberPendingVoiceGrant(std::vector<P25PendingVoiceGrant>& pendingGrants,
                                         const P25ControlEvent& ev,
                                         int correctedDibitErrors,
                                         qint64 nowMs);

std::vector<P25ControlEvent> p25ResolvePendingVoiceGrants(std::vector<P25PendingVoiceGrant>& pendingGrants,
                                                                 const P25ControlChannelAnalyzer& analyzer,
                                                                 qint64 nowMs);

P25RepeatedVoiceGrantDecision p25RememberRepeatedHighCorrectionResolvedVoiceGrant(
    std::vector<P25RepeatedVoiceGrant>& repeatedGrants,
    double controlFreqHz,
    const P25ControlEvent& event,
    int correctedDibitErrors,
    qint64 nowMs);

bool p25TsbkEventRegistryEligible(int correctedDibitErrors, const P25ControlEvent& event);

bool p25TsbkPendingVoiceGrantEligible(int correctedDibitErrors, const P25ControlEvent& event);

bool p25TsbkSessionIdentifierEligible(int correctedDibitErrors, const P25ControlEvent& event);

bool mergeP25TalkgroupEvent(std::vector<P25TalkgroupEntry>& talkgroups,
                                   double controlFreqHz,
                                   const P25ControlEvent& event,
                                   qint64 nowMs);

bool p25DecodeResultHasNidLock(const P25LiveDecodeResult& result);

QString p25LiveLockStageText(const P25LiveDecodeResult& result, size_t trustedTsbk);

std::string p25ControlAuditTsbkKey(const std::vector<uint8_t>& bytes);

std::string p25ControlAuditPhase2MacKey(const P25Phase2MacPdu& pdu);

std::string p25ControlAuditPhase1PduKey(const P25Phase1PduMessage& pdu);

QString p25ControlAuditOpsText(const std::map<std::string, size_t>& ops);

