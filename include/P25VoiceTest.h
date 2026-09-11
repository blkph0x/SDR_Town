#pragma once

// Purpose: SigMF/WAV capture helpers + P25 replay follow/voicetest runners.
// Spec:    docs/DECISIONS.md DEC-0040 / ISS-0004 Phase 6 (mechanical extract from main.cpp)
// Invariants: no hop/TTL/CADENCE/feed-gate behavior changes — move-only.

#include "DeviceManager.h"
#include "Demod.h"
#include "P25Control.h"
#include "P25LiveDecoder.h"
#include "P25TalkgroupRegistry.h"
#include "P25VoiceDecode.h"
#include "Receiver.h"
#include "SignalClassifier.h"

#include <QDateTime>
#include <QString>
#include <QStringList>

#include <nlohmann/json.hpp>

#include <complex>
#include <cstdint>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

using json = nlohmann::json;

inline constexpr qint64 kCaptureStorageReserveBytes = 1024ll * 1024ll * 1024ll;
inline constexpr double kManualCapturePreflightSeconds = 60.0;

struct SigmfIqCapture {
    bool ok = false;
    QString metaPath;
    QString dataPath;
    std::string error;
    std::string datatype;
    double sampleRateHz = 0.0;
    double centerFreqHz = 0.0;
    double targetFreqHz = 0.0;
    uint64_t totalSamples = 0;
    uint64_t totalBytes = 0;
    double totalDurationMs = 0.0;
    double startOffsetMs = 0.0;
    uint64_t firstSampleOffset = 0;
    std::vector<std::complex<float>> iq;
};
struct SigmfCaptureInfo {
    bool ok = false;
    QString metaPath;
    QString dataPath;
    std::string error;
    std::string datatype;
    double sampleRateHz = 0.0;
    double centerFreqHz = 0.0;
    double targetFreqHz = 0.0;
    uint64_t totalSamples = 0;
    uint64_t totalBytes = 0;
    double totalDurationMs = 0.0;
};
struct P25ReplayCliArgs {
    bool ok = false;
    std::string path;
    double targetMhz = 0.0;
    double ms = 0.0;
    double followMs = 0.0;
    double skipMs = 0.0;
    double centerMhz = 0.0;
    double voiceCenterMhz = 0.0;
    double trafficTargetOffsetHz = 0.0;
    uint32_t followTalkgroupId = 0;
    int tdmaSlot = -1;
    bool phase2Voice = false;
    int nac = -1;
    int64_t wacn = -1;
    int systemId = -1;
    bool clearGrant = false;
    bool encryptedGrant = false;
    bool forensicVoice = false;
    bool traceReplay = false;
    bool fieldAudioProbe = kP25Phase2AllowUnknownGrantFieldAudioProbe;
    // Continuous streaming replay (SDRTrunk-style): small hop, large lookback,
    // absolute-dibit de-dupe, optional WAV for automation.
    bool streamVoice = true;
    double windowMs = 0.0;   // 0 => default two-superframe cold window
    double hopMs = 0.0;      // 0 => stream auto cadence, legacy default half-window
    long long minDecodedFrames = 0; // automation gate (0 = any audio passes)
    double minAudioSeconds = 0.0;   // automation gate
    std::string wavOutPath;
    std::string oppositeWavOutPath;
    std::string error;
};
struct TrainingCaptureRequest {
    std::string label;
    size_t deviceIndex = 0;
    double tunedFreqHz = 100e6;
    DemodMode mode = DemodMode::AUTO;
    double channelBwHz = 12500.0;
    double lpfHz = 3000.0;
    bool audioLpfEnabled = true;
    double squelchDb = -105.0;
    std::vector<std::complex<float>> iq;
    std::vector<float> spectrumDb;
    double centerFreqHz = 100e6;
    double sampleRateHz = 2.048e6;
    SignalRecommendation recommendation;
    ClassifierTile tile;
    DeviceInfo device;
};
struct TrainingCaptureResult {
    bool ok = false;
    QString directory;
    QString message;
};
struct IqTestCaptureRequest {
    std::string label;
    size_t deviceIndex = 0;
    double tunedFreqHz = 100e6;
    DemodMode mode = DemodMode::AUTO;
    double channelBwHz = 12500.0;
    double lpfHz = 3000.0;
    bool audioLpfEnabled = true;
    double squelchDb = -105.0;
    double centerFreqHz = 100e6;
    double sampleRateHz = 2.048e6;
    double requestedSeconds = 5.0;
    uint64_t startAbsolute = 0;
    uint64_t endAbsolute = 0;
    QDateTime captureStartedUtc;
    QDateTime captureEndedUtc;
    std::vector<std::complex<float>> iq;
    std::vector<float> spectrumDb;
    DeviceInfo device;
    QStringList p25LogSnapshot;
    double signalLevelDb = -120.0;
    double noiseFloorDb = -120.0;
    double snrDb = 0.0;
    double afcOffsetHz = 0.0;
};
struct IqTestCaptureResult {
    bool ok = false;
    QString directory;
    QString message;
};
struct LiveIqCaptureSession {
    bool active = false;
    std::string label;
    std::string sessionId;
    size_t deviceIndex = 0;
    double tunedFreqHz = 100e6;
    DemodMode mode = DemodMode::AUTO;
    double channelBwHz = 12500.0;
    double lpfHz = 3000.0;
    bool audioLpfEnabled = true;
    double squelchDb = -105.0;
    double centerFreqHz = 100e6;
    double sampleRateHz = 2.048e6;
    DeviceInfo device;
    QDateTime startedUtc;
    QDateTime stoppedUtc;
    QDateTime lastPollUtc;
    uint64_t startAbsolute = 0;
    uint64_t cursorAbsolute = 0;
    uint64_t endAbsolute = 0;
    uint64_t ringOverrunSamples = 0;
    uint64_t maxSingleGapSamples = 0;
    uint64_t ringEpochResets = 0;
    uint64_t ringEpochResetSkippedSamples = 0;
    uint64_t zeroAppendPolls = 0;
    uint64_t fileWriteErrorPolls = 0;
    uint64_t pollCount = 0;
    size_t samplesWritten = 0;
    uint64_t bytesWritten = 0;
    QString directory;
    QString baseName;
    QString dataPath;
    QString metaPath;
    QString eventsPath;
    QString p25TextPath;
    QString ringCsvPath;
    QString statusPath;
    QString summaryPath;
    QString replayPath;
    std::ofstream data;
    std::ofstream events;
    std::ofstream ringCsv;
    std::ofstream p25LogStream;
    QStringList startP25LogSnapshot; // legacy/small startup context only; no live capture log buffering.
    QStringList p25LogDuringCapture; // unused after streaming-log fix; kept for source compatibility.
    size_t p25CaptureDroppedLines = 0;
    uint64_t p25CaptureLinesWritten = 0;
    uint64_t p25CaptureWriteErrors = 0;
    size_t startP25LogIndex = 0;
    double lastSignalLevelDb = -120.0;
    double lastNoiseFloorDb = -120.0;
    double lastSnrDb = 0.0;
    double lastAfcOffsetHz = 0.0;
    bool storageStopRequested = false;
    QString storageStopReason;
    qint64 storageStopAvailableBytes = -1;
    qint64 storageStopReserveBytes = -1;
};
struct LiveIqCaptureResult {
    bool ok = false;
    QString directory;
    QString message;
};
class Pcm16WavCapture {
public:
    bool open(const QString& path, uint32_t sampleRate, QString* error = nullptr)
    {
        close();
        m_path = path;
        m_sampleRate = sampleRate > 0 ? sampleRate : 48000u;
        m_samples = 0;
        m_out.open(path.toStdString(), std::ios::binary | std::ios::trunc);
        if (!m_out.is_open()) {
            if (error) *error = QString("could not open %1").arg(path);
            return false;
        }
        writeHeader();
        if (!m_out.good()) {
            if (error) *error = QString("could not write WAV header to %1").arg(path);
            m_out.close();
            return false;
        }
        return true;
    }

    void append(const std::vector<float>& samples)
    {
        if (!m_out.is_open() || samples.empty()) return;
        for (float sample : samples) {
            const double v = std::isfinite(sample) ? std::clamp<double>(sample, -1.0, 1.0) : 0.0;
            const int16_t s = static_cast<int16_t>(std::lround(v * 32767.0));
            writeU16(static_cast<uint16_t>(s));
            ++m_samples;
        }
    }

    void close()
    {
        if (!m_out.is_open()) return;
        const uint64_t dataBytes64 = std::min<uint64_t>(m_samples * 2u, 0xffffffffull);
        const uint32_t dataBytes = static_cast<uint32_t>(dataBytes64);
        const uint32_t riffSize = dataBytes <= 0xfffffff7u ? 36u + dataBytes : 0xffffffffu;
        m_out.seekp(4, std::ios::beg);
        writeU32(riffSize);
        m_out.seekp(40, std::ios::beg);
        writeU32(dataBytes);
        m_out.seekp(0, std::ios::end);
        m_out.flush();
        m_out.close();
    }

    bool active() const noexcept { return m_out.is_open(); }
    uint64_t sampleCount() const noexcept { return m_samples; }
    uint32_t sampleRate() const noexcept { return m_sampleRate; }
    QString path() const { return m_path; }

private:
    void writeHeader()
    {
        m_out.write("RIFF", 4);
        writeU32(0);
        m_out.write("WAVE", 4);
        m_out.write("fmt ", 4);
        writeU32(16);
        writeU16(1);
        writeU16(1);
        writeU32(m_sampleRate);
        writeU32(m_sampleRate * 2u);
        writeU16(2);
        writeU16(16);
        m_out.write("data", 4);
        writeU32(0);
    }

    void writeU16(uint16_t v)
    {
        const char bytes[2] = {
            static_cast<char>(v & 0xffu),
            static_cast<char>((v >> 8) & 0xffu)
        };
        m_out.write(bytes, sizeof(bytes));
    }

    void writeU32(uint32_t v)
    {
        const char bytes[4] = {
            static_cast<char>(v & 0xffu),
            static_cast<char>((v >> 8) & 0xffu),
            static_cast<char>((v >> 16) & 0xffu),
            static_cast<char>((v >> 24) & 0xffu)
        };
        m_out.write(bytes, sizeof(bytes));
    }

    QString m_path;
    std::ofstream m_out;
    uint32_t m_sampleRate = 48000;
    uint64_t m_samples = 0;
};

struct CliP25WavCaptureSummary {
    bool active = false;
    QString path;
    uint64_t samples = 0;
    uint32_t sampleRate = 48000;
};

extern QString gIqTestCapturesRootOverride;

SigmfCaptureInfo inspectSigmfCf32Capture(const QString& requestedPath);
SigmfIqCapture loadSigmfCf32Capture(const QString& requestedPath, double maxMs, double skipMs = 0.0);

bool parseFiniteDoubleToken(const std::string& text, double& out);
bool p25ReplayHasMaskParameters(const P25ReplayCliArgs& args);
bool trySeedP25ReplayMaskFromCaptureLog(P25ReplayCliArgs& args);
P25ReplayCliArgs parseP25ReplayCliArgs(const std::string& rest);

QString trainingCapturesRoot();
QString iqTestCapturesRoot();
QString humanBytes(qint64 bytes);
qint64 captureDataBytesForSeconds(double sampleRateHz, double seconds);
qint64 captureStartRequiredBytes(double sampleRateHz, int plannedDurationMs);
bool captureStorageReadyForStart(const QString& root,
                                 double sampleRateHz,
                                 int plannedDurationMs,
                                 QString* message);
bool captureStorageBelowStopReserve(const QString& path,
                                    qint64* availableBytes = nullptr,
                                    QString* message = nullptr);
std::string sanitizeFileToken(std::string s);
std::string makeCaptureSessionId(const QDateTime& utc, const std::string& label, double freqHz);

QString makeCliP25WavCapturePath(double ccHz, uint32_t talkgroupId, double voiceHz);
QString makeCliP25OppositeWavCapturePath(double ccHz, double voiceHz, int slot);
bool startCliP25WavCapture(const QString& path, double sampleRate, QString* error = nullptr);
bool startCliP25OppositeWavCapture(const QString& path, double sampleRate, QString* error = nullptr);
void appendCliP25WavCapture(const std::vector<float>& samples);
void appendCliP25OppositeWavCapture(const std::vector<float>& samples);
CliP25WavCaptureSummary stopCliP25WavCapture();
CliP25WavCaptureSummary stopCliP25OppositeWavCapture();

bool writeJsonDocumentFile(const QString& path, const json& doc, QString* error = nullptr);
const char* captureHealthVerdict(uint64_t samplesWritten,
                                 uint64_t ringOverrunSamples,
                                 uint64_t fileWriteErrorPolls,
                                 double seconds,
                                 uint64_t ringEpochResetSkippedSamples = 0);

IqTestCaptureResult saveIqTestCapture(const IqTestCaptureRequest& req);
IqTestCaptureResult saveCliP25FollowIqCapture(DeviceManager& mgr,
                                              size_t devIndex,
                                              double controlFreqHz,
                                              const P25TalkgroupEntry& tg,
                                              double requestedSeconds,
                                              const P25VoiceDiagSnapshot& finalDiag,
                                              const QString& reason,
                                              const std::string& lastVoiceSig,
                                              const QString& selectedGrantEventText = {},
                                              const QString& selectedGrantDetailText = {},
                                              double captureCenterFreqHz = 0.0,
                                              const DeviceManager::RecentIQWindow* preferredWindow = nullptr,
                                              const QDateTime& preferredWindowEndUtc = {},
                                              const QString& preferredWindowSource = {});
TrainingCaptureResult saveTrainingCapture(const TrainingCaptureRequest& req);

void runP25ReplayFollowTest(P25ReplayCliArgs args);
void runP25ReplayVoiceTest(const P25ReplayCliArgs& argsIn);
