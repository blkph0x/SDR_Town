#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QMutex>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

// Async offline STT worker. Never blocks the DSP path.
// v1 backend: external Python + faster-whisper (scripts/stt_transcribe_wav.py).
// If the engine/model is missing, status reports install guidance and PCM is still segmented.
enum class SttEngineStatus {
    Unavailable = 0,
    Ready,
    Listening,
    Transcribing,
    Error
};

class SttEngine : public QObject
{
    Q_OBJECT
public:
    explicit SttEngine(QObject* parent = nullptr);
    ~SttEngine() override;

    void start();
    void stop();

    SttEngineStatus status() const;
    QString statusText() const;
    bool isAvailable() const;

    // Thread-safe, non-blocking. Copies samples and returns immediately.
    void submitPcm(const float* samples,
                   size_t count,
                   int sampleRateHz,
                   const QString& sourceId,
                   const QString& sourceName,
                   const QString& channelLabel,
                   const QVariantMap& meta = {});

    // Force-flush the current open segment (e.g. call end).
    void flushCurrentSegment();

signals:
    void statusChanged(SttEngineStatus status, const QString& text);
    void transcriptReady(const QString& sourceId,
                         const QString& sourceName,
                         const QString& channel,
                         const QString& text,
                         const QVariantMap& meta);
    void segmentSkipped(const QString& sourceId,
                        const QString& sourceName,
                        const QString& channel,
                        double durationSec,
                        const QString& reason,
                        const QVariantMap& meta);

private:
    struct PcmChunk {
        std::vector<float> samples;
        int sampleRateHz = 16000;
        QString sourceId;
        QString sourceName;
        QString channel;
        QVariantMap meta;
    };

    struct SegmentJob {
        std::vector<float> samples; // mono float, original rate
        int sampleRateHz = 16000;
        QString sourceId;
        QString sourceName;
        QString channel;
        QVariantMap meta;
        double durationSec = 0.0;
    };

    void workerLoop();
    void probeAvailability();
    void setStatus(SttEngineStatus status, const QString& text);
    void ingestChunk(PcmChunk chunk);
    void flushOpenSegmentLocked(const char* reason);
    bool writeWav16k(const QString& path, const std::vector<float>& samples, int sampleRateHz) const;
    QString runFasterWhisper(const QString& wavPath, QString* errorOut) const;
    QString findTranscribeScript() const;
    QString findPythonExecutable() const;

    mutable QMutex m_statusMutex;
    SttEngineStatus m_status = SttEngineStatus::Unavailable;
    QString m_statusText = QStringLiteral("STT not started");

    std::atomic<bool> m_running{false};
    std::thread m_worker;
    std::mutex m_queueMutex;
    std::condition_variable m_queueCv;
    std::deque<PcmChunk> m_ingestQueue;
    std::deque<SegmentJob> m_jobQueue;
    bool m_flushRequested = false;

    // Open segment (worker thread only)
    std::vector<float> m_openSamples;
    int m_openRateHz = 0;
    QString m_openSourceId;
    QString m_openSourceName;
    QString m_openChannel;
    QVariantMap m_openMeta;
    int m_silenceFrames = 0;
    bool m_hadSpeech = false;

    QString m_pythonExe;
    QString m_scriptPath;
    bool m_engineAvailable = false;
    QString m_unavailableReason;

    static constexpr double kMinSegmentSec = 0.70;
    static constexpr double kMaxSegmentSec = 4.00;
    static constexpr double kSilenceFlushSec = 0.45;
    static constexpr float kSpeechRmsThreshold = 0.012f;
    static constexpr size_t kMaxQueuedJobs = 8;
    static constexpr size_t kMaxIngestQueue = 64;
};
