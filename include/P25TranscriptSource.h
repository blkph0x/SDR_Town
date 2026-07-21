#pragma once

#include "ITranscriptSource.h"
#include "SttEngine.h"
#include "TranscriptHub.h"

#include <QObject>
#include <atomic>
#include <cstdint>

// P25 clear-voice → text feed.
// Taps PCM that has already passed the speaker gate (same stream pushed to AudioEngine).
// Encrypted / gated-silent audio must never call the tap.
class P25TranscriptSource : public TranscriptSourceBase
{
    Q_OBJECT
public:
    P25TranscriptSource(TranscriptHub* hub, SttEngine* stt, QObject* parent = nullptr);

    QString id() const override { return QStringLiteral("p25"); }
    QString displayName() const override { return QStringLiteral("P25 Voice"); }
    TranscriptCategory category() const override { return TranscriptCategory::VoiceStt; }

    // Install as the process-wide tap target (DSP-thread safe via atomic pointer).
    void installAsGlobalTap();
    void uninstallGlobalTap();

    // Fast path: copy PCM into SttEngine and return. Safe from DSP worker.
    void onClearSpeakerPcm(const float* samples,
                           size_t count,
                           int sampleRateHz,
                           uint32_t talkgroupId,
                           double freqHz,
                           int slot /* -1 if unknown */);

private slots:
    void onTranscriptReady(const QString& sourceId,
                           const QString& sourceName,
                           const QString& channel,
                           const QString& text,
                           const QVariantMap& meta);
    void onSegmentSkipped(const QString& sourceId,
                          const QString& sourceName,
                          const QString& channel,
                          double durationSec,
                          const QString& reason,
                          const QVariantMap& meta);

private:
    TranscriptHub* m_hub = nullptr;
    SttEngine* m_stt = nullptr;
    qint64 m_lastSkipLogMs = 0;
    QString m_lastSkipReason;
};

// Global DSP-safe entry point used from main.cpp speaker-push sites.
void p25TranscriptTapSpeakerPcm(const float* samples,
                                size_t count,
                                int sampleRateHz,
                                uint32_t talkgroupId,
                                double freqHz,
                                int slot);
