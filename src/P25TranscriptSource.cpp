#include "P25TranscriptSource.h"

#include <QDateTime>

namespace {
std::atomic<P25TranscriptSource*> g_p25TranscriptTap{nullptr};
}

P25TranscriptSource::P25TranscriptSource(TranscriptHub* hub, SttEngine* stt, QObject* parent)
    : TranscriptSourceBase(parent)
    , m_hub(hub)
    , m_stt(stt)
{
    if (m_hub) {
        m_hub->registerSource(info());
    }
    if (m_stt) {
        connect(m_stt, &SttEngine::transcriptReady,
                this, &P25TranscriptSource::onTranscriptReady);
        connect(m_stt, &SttEngine::segmentSkipped,
                this, &P25TranscriptSource::onSegmentSkipped);
    }
}

void P25TranscriptSource::installAsGlobalTap()
{
    g_p25TranscriptTap.store(this, std::memory_order_release);
}

void P25TranscriptSource::uninstallGlobalTap()
{
    P25TranscriptSource* expected = this;
    g_p25TranscriptTap.compare_exchange_strong(expected, nullptr,
                                               std::memory_order_acq_rel);
}

void P25TranscriptSource::onClearSpeakerPcm(const float* samples,
                                            size_t count,
                                            int sampleRateHz,
                                            uint32_t talkgroupId,
                                            double freqHz,
                                            int slot)
{
    if (!m_stt || !samples || count == 0 || sampleRateHz <= 0) return;

    QString channel;
    if (talkgroupId > 0) {
        channel = QStringLiteral("TG %1").arg(talkgroupId);
    } else {
        channel = QStringLiteral("TG ?");
    }
    if (freqHz > 0.0) {
        channel += QStringLiteral(" @ %1 MHz").arg(freqHz / 1e6, 0, 'f', 5);
    }
    if (slot >= 0) {
        channel += QStringLiteral(" slot%1").arg(slot);
    }

    QVariantMap meta;
    meta.insert(QStringLiteral("talkgroupId"), static_cast<qulonglong>(talkgroupId));
    meta.insert(QStringLiteral("freqHz"), freqHz);
    if (slot >= 0) meta.insert(QStringLiteral("slot"), slot);

    m_stt->submitPcm(samples, count, sampleRateHz,
                     id(), displayName(), channel, meta);
}

void P25TranscriptSource::onTranscriptReady(const QString& sourceId,
                                            const QString& sourceName,
                                            const QString& channel,
                                            const QString& text,
                                            const QVariantMap& meta)
{
    if (!m_hub || sourceId != id()) return;
    TranscriptMessage msg;
    msg.timestamp = QDateTime::currentDateTime();
    msg.sourceId = sourceId;
    msg.sourceName = sourceName;
    msg.category = TranscriptCategory::VoiceStt;
    msg.channel = channel;
    msg.text = text;
    msg.meta = meta;
    m_hub->append(std::move(msg));
}

void P25TranscriptSource::onSegmentSkipped(const QString& sourceId,
                                           const QString& sourceName,
                                           const QString& channel,
                                           double durationSec,
                                           const QString& reason,
                                           const QVariantMap& meta)
{
    if (!m_hub || sourceId != id()) return;

    // Only surface unavailable / error skips — avoid spam for short segments.
    const bool noteworthy =
        reason.contains(QStringLiteral("unavailable"), Qt::CaseInsensitive) ||
        reason.contains(QStringLiteral("install"), Qt::CaseInsensitive) ||
        reason.contains(QStringLiteral("failed"), Qt::CaseInsensitive) ||
        reason.contains(QStringLiteral("timed out"), Qt::CaseInsensitive) ||
        reason.contains(QStringLiteral("queue full"), Qt::CaseInsensitive);
    if (!noteworthy) return;

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const bool sameReason = (reason == m_lastSkipReason);
    if (sameReason && m_lastSkipLogMs > 0 && (nowMs - m_lastSkipLogMs) < 15000) {
        return;
    }
    m_lastSkipLogMs = nowMs;
    m_lastSkipReason = reason;

    TranscriptMessage msg;
    msg.timestamp = QDateTime::currentDateTime();
    msg.sourceId = sourceId;
    msg.sourceName = sourceName;
    msg.category = TranscriptCategory::VoiceStt;
    msg.channel = channel;
    msg.text = QStringLiteral("[voice %1s] %2")
                   .arg(durationSec, 0, 'f', 1)
                   .arg(reason);
    msg.meta = meta;
    m_hub->append(std::move(msg));
}

void p25TranscriptTapSpeakerPcm(const float* samples,
                                size_t count,
                                int sampleRateHz,
                                uint32_t talkgroupId,
                                double freqHz,
                                int slot)
{
    P25TranscriptSource* tap = g_p25TranscriptTap.load(std::memory_order_acquire);
    if (!tap || !samples || count == 0) return;
    tap->onClearSpeakerPcm(samples, count, sampleRateHz, talkgroupId, freqHz, slot);
}
