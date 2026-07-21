#include "SttEngine.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutexLocker>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <spdlog/spdlog.h>

namespace {

float frameRms(const float* samples, size_t count)
{
    if (!samples || count == 0) return 0.0f;
    double acc = 0.0;
    for (size_t i = 0; i < count; ++i) {
        const double v = static_cast<double>(samples[i]);
        acc += v * v;
    }
    return static_cast<float>(std::sqrt(acc / static_cast<double>(count)));
}

std::vector<float> downsampleTo16k(const std::vector<float>& in, int inRate)
{
    if (in.empty() || inRate <= 0) return {};
    if (inRate == 16000) return in;
    const double ratio = static_cast<double>(inRate) / 16000.0;
    const size_t outCount = static_cast<size_t>(std::floor(static_cast<double>(in.size()) / ratio));
    std::vector<float> out;
    out.resize(outCount);
    for (size_t i = 0; i < outCount; ++i) {
        const double src = static_cast<double>(i) * ratio;
        const size_t i0 = static_cast<size_t>(src);
        const size_t i1 = std::min(i0 + 1, in.size() - 1);
        const float frac = static_cast<float>(src - static_cast<double>(i0));
        out[i] = in[i0] * (1.0f - frac) + in[i1] * frac;
    }
    return out;
}

} // namespace

SttEngine::SttEngine(QObject* parent)
    : QObject(parent)
{
}

SttEngine::~SttEngine()
{
    stop();
}

void SttEngine::start()
{
    if (m_running.load(std::memory_order_acquire)) return;
    probeAvailability();
    m_running.store(true, std::memory_order_release);
    m_worker = std::thread([this]() { workerLoop(); });
    if (m_engineAvailable) {
        setStatus(SttEngineStatus::Ready, QStringLiteral("STT ready (faster-whisper)"));
    } else {
        setStatus(SttEngineStatus::Unavailable, m_unavailableReason);
    }
}

void SttEngine::stop()
{
    if (!m_running.exchange(false, std::memory_order_acq_rel)) {
        if (m_worker.joinable()) m_worker.join();
        return;
    }
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_flushRequested = true;
    }
    m_queueCv.notify_all();
    if (m_worker.joinable()) m_worker.join();
    setStatus(SttEngineStatus::Unavailable, QStringLiteral("STT stopped"));
}

SttEngineStatus SttEngine::status() const
{
    QMutexLocker lock(&m_statusMutex);
    return m_status;
}

QString SttEngine::statusText() const
{
    QMutexLocker lock(&m_statusMutex);
    return m_statusText;
}

bool SttEngine::isAvailable() const
{
    return m_engineAvailable;
}

void SttEngine::submitPcm(const float* samples,
                          size_t count,
                          int sampleRateHz,
                          const QString& sourceId,
                          const QString& sourceName,
                          const QString& channelLabel,
                          const QVariantMap& meta)
{
    if (!samples || count == 0 || sampleRateHz <= 0) return;
    if (!m_running.load(std::memory_order_acquire)) return;

    PcmChunk chunk;
    chunk.samples.assign(samples, samples + count);
    chunk.sampleRateHz = sampleRateHz;
    chunk.sourceId = sourceId;
    chunk.sourceName = sourceName;
    chunk.channel = channelLabel;
    chunk.meta = meta;

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        if (m_ingestQueue.size() >= kMaxIngestQueue) {
            m_ingestQueue.pop_front();
        }
        m_ingestQueue.push_back(std::move(chunk));
    }
    m_queueCv.notify_one();
}

void SttEngine::flushCurrentSegment()
{
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_flushRequested = true;
    }
    m_queueCv.notify_one();
}

void SttEngine::setStatus(SttEngineStatus status, const QString& text)
{
    {
        QMutexLocker lock(&m_statusMutex);
        if (m_status == status && m_statusText == text) return;
        m_status = status;
        m_statusText = text;
    }
    emit statusChanged(status, text);
}

void SttEngine::probeAvailability()
{
    m_pythonExe = findPythonExecutable();
    m_scriptPath = findTranscribeScript();
    m_engineAvailable = false;
    m_unavailableReason.clear();

    if (m_pythonExe.isEmpty()) {
        m_unavailableReason = QStringLiteral(
            "STT unavailable — install Python 3 and faster-whisper "
            "(pip install faster-whisper)");
        return;
    }
    if (m_scriptPath.isEmpty()) {
        m_unavailableReason = QStringLiteral(
            "STT unavailable — missing scripts/stt_transcribe_wav.py");
        return;
    }

    QProcess probe;
    probe.setProcessChannelMode(QProcess::MergedChannels);
    probe.start(m_pythonExe, {QStringLiteral("-c"),
                              QStringLiteral("import faster_whisper; print('ok')")});
    if (!probe.waitForStarted(3000)) {
        m_unavailableReason = QStringLiteral(
            "STT unavailable — could not start Python (%1)").arg(m_pythonExe);
        return;
    }
    if (!probe.waitForFinished(20000)) {
        probe.kill();
        m_unavailableReason = QStringLiteral(
            "STT unavailable — Python probe timed out");
        return;
    }
    const QString out = QString::fromUtf8(probe.readAllStandardOutput()).trimmed();
    if (probe.exitCode() != 0 || !out.contains(QStringLiteral("ok"))) {
        m_unavailableReason = QStringLiteral(
            "STT unavailable — install faster-whisper: pip install faster-whisper");
        spdlog::info("STT probe failed: exit={} out={}",
                     probe.exitCode(), out.toStdString());
        return;
    }

    m_engineAvailable = true;
    m_unavailableReason.clear();
    spdlog::info("STT engine available via {} + {}",
                 m_pythonExe.toStdString(), m_scriptPath.toStdString());
}

QString SttEngine::findPythonExecutable() const
{
    const QByteArray envPy = qgetenv("SDR_TOWN_STT_PYTHON");
    if (!envPy.isEmpty() && QFileInfo::exists(QString::fromLocal8Bit(envPy))) {
        return QString::fromLocal8Bit(envPy);
    }

    // Prefer the project-local STT venv (scripts/setup_stt.ps1).
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList venvCandidates = {
#ifdef Q_OS_WIN
        QDir(appDir).absoluteFilePath(QStringLiteral(".venv-stt/Scripts/python.exe")),
        QDir(appDir).absoluteFilePath(QStringLiteral("../.venv-stt/Scripts/python.exe")),
        QDir(appDir).absoluteFilePath(QStringLiteral("../../.venv-stt/Scripts/python.exe")),
        QDir(appDir).absoluteFilePath(QStringLiteral("../../../.venv-stt/Scripts/python.exe")),
        QDir::current().absoluteFilePath(QStringLiteral(".venv-stt/Scripts/python.exe")),
#else
        QDir(appDir).absoluteFilePath(QStringLiteral(".venv-stt/bin/python")),
        QDir(appDir).absoluteFilePath(QStringLiteral("../.venv-stt/bin/python")),
        QDir(appDir).absoluteFilePath(QStringLiteral("../../.venv-stt/bin/python")),
        QDir(appDir).absoluteFilePath(QStringLiteral("../../../.venv-stt/bin/python")),
        QDir::current().absoluteFilePath(QStringLiteral(".venv-stt/bin/python")),
#endif
    };
    for (const QString& path : venvCandidates) {
        if (QFileInfo::exists(path)) {
            return QFileInfo(path).absoluteFilePath();
        }
    }

    const QStringList candidates = {
        QStringLiteral("python"),
        QStringLiteral("python3"),
        QStringLiteral("py"),
    };
    for (const QString& name : candidates) {
        const QString found = QStandardPaths::findExecutable(name);
        if (!found.isEmpty()) return found;
    }
#ifdef Q_OS_WIN
    // `py -3` launcher: store as "py" and pass -3 in run path when needed.
    const QString pyLauncher = QStandardPaths::findExecutable(QStringLiteral("py"));
    if (!pyLauncher.isEmpty()) return pyLauncher;
#endif
    return {};
}

QString SttEngine::findTranscribeScript() const
{
    const QByteArray envScript = qgetenv("SDR_TOWN_STT_SCRIPT");
    if (!envScript.isEmpty() && QFileInfo::exists(QString::fromLocal8Bit(envScript))) {
        return QString::fromLocal8Bit(envScript);
    }

    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(appDir).absoluteFilePath(QStringLiteral("scripts/stt_transcribe_wav.py")),
        QDir(appDir).absoluteFilePath(QStringLiteral("../scripts/stt_transcribe_wav.py")),
        QDir(appDir).absoluteFilePath(QStringLiteral("../../scripts/stt_transcribe_wav.py")),
        QDir(appDir).absoluteFilePath(QStringLiteral("../../../scripts/stt_transcribe_wav.py")),
        QDir::current().absoluteFilePath(QStringLiteral("scripts/stt_transcribe_wav.py")),
    };
    for (const QString& path : candidates) {
        if (QFileInfo::exists(path)) return QFileInfo(path).absoluteFilePath();
    }
    return {};
}

void SttEngine::ingestChunk(PcmChunk chunk)
{
    if (chunk.samples.empty() || chunk.sampleRateHz <= 0) return;

    const bool channelChanged =
        !m_openSamples.empty() &&
        (m_openSourceId != chunk.sourceId || m_openChannel != chunk.channel);
    if (channelChanged) {
        flushOpenSegmentLocked("channel-change");
    }

    if (m_openSamples.empty()) {
        m_openRateHz = chunk.sampleRateHz;
        m_openSourceId = chunk.sourceId;
        m_openSourceName = chunk.sourceName;
        m_openChannel = chunk.channel;
        m_openMeta = chunk.meta;
        m_silenceFrames = 0;
        m_hadSpeech = false;
    }

    // Resample mismatch within an open segment: keep first rate by simple skip
    // of mismatched chunks (rare; speaker path is stable).
    if (chunk.sampleRateHz != m_openRateHz) {
        return;
    }

    const size_t frameSamples = std::max<size_t>(1,
        static_cast<size_t>(m_openRateHz * 0.020)); // 20 ms
    size_t offset = 0;
    while (offset < chunk.samples.size()) {
        const size_t n = std::min(frameSamples, chunk.samples.size() - offset);
        const float rms = frameRms(chunk.samples.data() + offset, n);
        const bool speech = rms >= kSpeechRmsThreshold;
        m_openSamples.insert(m_openSamples.end(),
                             chunk.samples.begin() + static_cast<std::ptrdiff_t>(offset),
                             chunk.samples.begin() + static_cast<std::ptrdiff_t>(offset + n));
        if (speech) {
            m_hadSpeech = true;
            m_silenceFrames = 0;
        } else if (m_hadSpeech) {
            ++m_silenceFrames;
        }
        offset += n;

        const double openSec = static_cast<double>(m_openSamples.size()) /
            static_cast<double>(m_openRateHz);
        const double silenceSec = static_cast<double>(m_silenceFrames) * 0.020;
        if (m_hadSpeech && silenceSec >= kSilenceFlushSec && openSec >= kMinSegmentSec) {
            flushOpenSegmentLocked("silence");
        } else if (openSec >= kMaxSegmentSec) {
            flushOpenSegmentLocked("max-duration");
        }
    }

    if (m_hadSpeech && m_engineAvailable) {
        setStatus(SttEngineStatus::Listening, QStringLiteral("STT listening"));
    }
}

void SttEngine::flushOpenSegmentLocked(const char* reason)
{
    if (m_openSamples.empty() || m_openRateHz <= 0) {
        m_openSamples.clear();
        m_hadSpeech = false;
        m_silenceFrames = 0;
        return;
    }

    const bool hadSpeech = m_hadSpeech;
    const double durationSec = static_cast<double>(m_openSamples.size()) /
        static_cast<double>(m_openRateHz);
    SegmentJob job;
    job.samples = std::move(m_openSamples);
    job.sampleRateHz = m_openRateHz;
    job.sourceId = m_openSourceId;
    job.sourceName = m_openSourceName;
    job.channel = m_openChannel;
    job.meta = m_openMeta;
    job.durationSec = durationSec;
    job.meta.insert(QStringLiteral("flushReason"), QString::fromUtf8(reason));
    job.meta.insert(QStringLiteral("durationSec"), durationSec);
    job.meta.insert(QStringLiteral("hadSpeech"), hadSpeech);

    m_openSamples.clear();
    m_openRateHz = 0;
    m_hadSpeech = false;
    m_silenceFrames = 0;

    if (!hadSpeech) {
        return;
    }
    if (durationSec < kMinSegmentSec) {
        emit segmentSkipped(job.sourceId, job.sourceName, job.channel, durationSec,
                            QStringLiteral("segment too short"), job.meta);
        return;
    }

    if (m_jobQueue.size() >= kMaxQueuedJobs) {
        m_jobQueue.pop_front();
        emit segmentSkipped(job.sourceId, job.sourceName, job.channel, durationSec,
                            QStringLiteral("queue full — dropped oldest"), job.meta);
    }
    m_jobQueue.push_back(std::move(job));
}

bool SttEngine::writeWav16k(const QString& path,
                            const std::vector<float>& samples,
                            int sampleRateHz) const
{
    const std::vector<float> pcm16k = downsampleTo16k(samples, sampleRateHz);
    if (pcm16k.empty()) return false;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;

    const quint32 sampleRate = 16000;
    const quint16 channels = 1;
    const quint16 bitsPerSample = 16;
    const quint32 byteRate = sampleRate * channels * (bitsPerSample / 8);
    const quint16 blockAlign = channels * (bitsPerSample / 8);
    const quint32 dataBytes = static_cast<quint32>(pcm16k.size() * sizeof(qint16));
    const quint32 riffSize = 36 + dataBytes;

    auto writeU32 = [&](quint32 v) {
        char b[4] = {
            static_cast<char>(v & 0xff),
            static_cast<char>((v >> 8) & 0xff),
            static_cast<char>((v >> 16) & 0xff),
            static_cast<char>((v >> 24) & 0xff)
        };
        f.write(b, 4);
    };
    auto writeU16 = [&](quint16 v) {
        char b[2] = {
            static_cast<char>(v & 0xff),
            static_cast<char>((v >> 8) & 0xff)
        };
        f.write(b, 2);
    };

    f.write("RIFF", 4);
    writeU32(riffSize);
    f.write("WAVE", 4);
    f.write("fmt ", 4);
    writeU32(16);
    writeU16(1); // PCM
    writeU16(channels);
    writeU32(sampleRate);
    writeU32(byteRate);
    writeU16(blockAlign);
    writeU16(bitsPerSample);
    f.write("data", 4);
    writeU32(dataBytes);

    std::vector<char> raw(dataBytes);
    for (size_t i = 0; i < pcm16k.size(); ++i) {
        float s = std::clamp(pcm16k[i], -1.0f, 1.0f);
        const qint16 v = static_cast<qint16>(std::lrintf(s * 32767.0f));
        raw[i * 2] = static_cast<char>(v & 0xff);
        raw[i * 2 + 1] = static_cast<char>((v >> 8) & 0xff);
    }
    f.write(raw.data(), static_cast<qint64>(raw.size()));
    f.close();
    return true;
}

QString SttEngine::runFasterWhisper(const QString& wavPath, QString* errorOut) const
{
    if (!m_engineAvailable) {
        if (errorOut) *errorOut = m_unavailableReason;
        return {};
    }

    QStringList args;
    QString program = m_pythonExe;
    const QFileInfo pyInfo(m_pythonExe);
    if (pyInfo.fileName().compare(QStringLiteral("py"), Qt::CaseInsensitive) == 0 ||
        pyInfo.fileName().compare(QStringLiteral("py.exe"), Qt::CaseInsensitive) == 0) {
        args << QStringLiteral("-3");
    }
    args << m_scriptPath << wavPath;

    QProcess proc;
    proc.setProcessChannelMode(QProcess::SeparateChannels);
    proc.start(program, args);
    if (!proc.waitForStarted(5000)) {
        if (errorOut) *errorOut = QStringLiteral("failed to start STT process");
        return {};
    }
    if (!proc.waitForFinished(180000)) {
        proc.kill();
        if (errorOut) *errorOut = QStringLiteral("STT timed out");
        return {};
    }

    const QByteArray stdoutBytes = proc.readAllStandardOutput();
    const QByteArray stderrBytes = proc.readAllStandardError();
    if (proc.exitCode() != 0) {
        if (errorOut) {
            *errorOut = QString::fromUtf8(stderrBytes).trimmed();
            if (errorOut->isEmpty()) {
                *errorOut = QStringLiteral("STT exit code %1").arg(proc.exitCode());
            }
        }
        return {};
    }

    const QString text = QString::fromUtf8(stdoutBytes).trimmed();
    if (text.isEmpty() && errorOut) {
        *errorOut = QStringLiteral("empty transcript");
    }
    return text;
}

void SttEngine::workerLoop()
{
    while (m_running.load(std::memory_order_acquire)) {
        PcmChunk chunk;
        bool doFlush = false;
        SegmentJob job;
        bool haveJob = false;

        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            m_queueCv.wait_for(lock, std::chrono::milliseconds(200), [this]() {
                return !m_running.load(std::memory_order_acquire) ||
                       !m_ingestQueue.empty() ||
                       m_flushRequested ||
                       !m_jobQueue.empty();
            });

            if (!m_ingestQueue.empty()) {
                chunk = std::move(m_ingestQueue.front());
                m_ingestQueue.pop_front();
            }
            doFlush = m_flushRequested;
            m_flushRequested = false;
            if (!m_jobQueue.empty() && chunk.samples.empty()) {
                job = std::move(m_jobQueue.front());
                m_jobQueue.pop_front();
                haveJob = true;
            }
        }

        if (!chunk.samples.empty()) {
            ingestChunk(std::move(chunk));
        }
        if (doFlush) {
            flushOpenSegmentLocked("explicit-flush");
        }

        if (!haveJob) {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            if (!m_jobQueue.empty()) {
                job = std::move(m_jobQueue.front());
                m_jobQueue.pop_front();
                haveJob = true;
            }
        }

        if (!haveJob) continue;

        if (!m_engineAvailable) {
            emit segmentSkipped(job.sourceId, job.sourceName, job.channel, job.durationSec,
                                m_unavailableReason.isEmpty()
                                    ? QStringLiteral("STT unavailable — install faster-whisper: pip install faster-whisper")
                                    : m_unavailableReason,
                                job.meta);
            continue;
        }

        setStatus(SttEngineStatus::Transcribing,
                  QStringLiteral("STT transcribing %1…").arg(job.channel));

        QTemporaryDir tmp;
        if (!tmp.isValid()) {
            setStatus(SttEngineStatus::Error, QStringLiteral("STT temp dir failed"));
            emit segmentSkipped(job.sourceId, job.sourceName, job.channel, job.durationSec,
                                QStringLiteral("temp dir failed"), job.meta);
            continue;
        }
        const QString wavPath = tmp.filePath(QStringLiteral("segment.wav"));
        if (!writeWav16k(wavPath, job.samples, job.sampleRateHz)) {
            setStatus(SttEngineStatus::Error, QStringLiteral("STT WAV write failed"));
            emit segmentSkipped(job.sourceId, job.sourceName, job.channel, job.durationSec,
                                QStringLiteral("wav write failed"), job.meta);
            continue;
        }

        QString err;
        const QString text = runFasterWhisper(wavPath, &err);
        if (text.isEmpty()) {
            setStatus(SttEngineStatus::Error,
                      err.isEmpty() ? QStringLiteral("STT produced no text") : err);
            emit segmentSkipped(job.sourceId, job.sourceName, job.channel, job.durationSec,
                                err.isEmpty() ? QStringLiteral("no text") : err,
                                job.meta);
        } else {
            emit transcriptReady(job.sourceId, job.sourceName, job.channel, text, job.meta);
            setStatus(SttEngineStatus::Ready, QStringLiteral("STT ready (faster-whisper)"));
        }
    }

    // Drain open segment on shutdown without starting new work if stopped.
    flushOpenSegmentLocked("shutdown");
}
