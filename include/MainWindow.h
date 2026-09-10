#pragma once

// Purpose: Primary Qt MainWindow declaration (bodies in MainWindow.cpp).
// Spec:    docs/DECISIONS.md DEC-0040 / ISS-0004 Phase B (out-of-line method bodies)
// Invariants: no hop/TTL/CADENCE/feed-gate behavior changes — move-only.

#include "AppBootstrap.h"
#include "AudioCapture.h"
#include "AudioEngine.h"
#include "ClassifierModelBackend.h"
#include "CliApp.h"
#include "Demod.h"
#include "DemodModeUtils.h"
#include "DeviceManager.h"
#include "IP25AmbeEncoder.h"
#include "P25AppGlobals.h"
#include "P25AudioDropClass.h"
#include "P25Control.h"
#include "P25DebugStage.h"
#include "P25DecodeConfig.h"
#include "P25FollowStateMachine.h"
#include "P25LiveDecoder.h"
#include "P25Phase2TxFramer.h"
#include "P25RollingIq.h"
#include "P25SdrtrunkTune.h"
#include "P25TalkgroupRegistry.h"
#include "P25TranscriptSource.h"
#include "P25TxConfig.h"
#include "P25TxSession.h"
#include "P25VoiceDecode.h"
#include "P25VoiceSession.h"
#include "P25VoiceTest.h"
#include "P25VoiceTiming.h"
#include "SavedFrequencies.h"
#include "Receiver.h"
#include "RemoteDiagnostics.h"
#include "SignalClassifier.h"
#include "SpectrumWidget.h"
#include "SttEngine.h"
#include "TranscriptHub.h"
#include "TranscriptWindow.h"
#include "UpdateManager.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMetaObject>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QStandardPaths>
#include <QThread>
#include <QStatusBar>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <complex>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

using json = nlohmann::json;

inline constexpr long long kGuiP25ClearAudioMinAcceptedFrames = 10; // 200 ms at 20 ms/frame
inline constexpr long long kGuiP25ClearAudioMinSamples = 9600;      // 200 ms at 48 kHz

#ifndef SDR_TOWN_VERSION
#define SDR_TOWN_VERSION "0.0.0"
#endif
#ifndef SDR_TOWN_P25_AUDIO_BASELINE
#define SDR_TOWN_P25_AUDIO_BASELINE "p25-clear-continuous-20260810"
#endif

void populateP25Table(QTableWidget* table,
                             const std::vector<P25ControlCandidate>& hits,
                             const std::vector<P25KnownControlChannel>& knownChannels = loadP25KnownControlChannels());

// DSP implementation now lives in src/Demod.cpp (Demodulator class owns all state per Receiver).
// classifyMode, detectChannelBandwidth, and demodulateToAudio are provided via Demod.h + Demod.cpp.
// Phase 0: no more global gGuiDemod/gCliDemod - each Receiver owns its Demodulator instance (state isolation).


enum class P25VoicePublishOutcome : uint8_t {
    Deferred = 0,
    Published,
    DiscardedStale,
    ReceiverGone
};

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(const GuiRuntimeConfig& config = GuiRuntimeConfig{}, QWidget* parent = nullptr);

private slots:
    void checkRemoteIssueFixStatus();

    QJsonObject diagnosticsRuntimeSnapshot(const QString& reason);

    void maybeShowAlphaDiagnosticsDisclosure();

    void submitDiagnosticsStartupSnapshot();

    void startDiagnosticsHealthMonitors();

    void showDiagnosticsReportDialog();

    void showMyDiagnosticsReports();

    void showAbout();

    void onAudioConfig();

    void onDevices();

    void showDevicesDialog();

private:
    struct P25VoiceDecodeJob {
        std::shared_ptr<Receiver> rx;
        std::vector<std::complex<float>> iq;
        std::vector<size_t> audioOutputIndices;
        double sampleRateHz = 0.0;
        double centerFreqHz = 0.0;
        double targetFreqHz = 0.0;
        double outputRateHz = 48000.0;
        uint64_t iqStartAbsolute = 0;
        bool iqStartAbsoluteKnown = false;
        uint64_t iqDecodeEndAbsolute = 0;
        bool iqDecodeEndAbsoluteKnown = false;
        bool outputMutedForSettle = false;
        bool rollingDecode = false;
        size_t freshIqSamples = 0;
        size_t contextIqSamples = 0;
        uint64_t trafficGeneration = 0;
        bool independentTrafficSource = false;
        bool speakerSustainDecode = false;
        uint32_t talkgroupId = 0;
        uint32_t sourceId = 0;
        bool tdmaSlotKnown = false;
        uint8_t tdmaSlot = 0;
        double voiceFreqHz = 0.0;
        uint64_t sequence = 0;
        uint64_t flushSeq = 0;
        ReceiverSessionKey receiverSessionKey{};
        uint64_t callSessionId = 0;
    };

    struct P25VoiceDecodeResult {
        std::shared_ptr<Receiver> rx;
        P25VoiceAudioBlock audio;
        std::vector<float> speakerAudio;
        std::vector<size_t> audioOutputIndices;
        double sampleRateHz = 0.0;
        double centerFreqHz = 0.0;
        double targetFreqHz = 0.0;
        double outputRateHz = 48000.0;
        double rmsDb = -120.0;
        long long dspMicros = 0;
        uint64_t iqStartAbsolute = 0;
        bool iqStartAbsoluteKnown = false;
        uint64_t iqDecodeEndAbsolute = 0;
        bool iqDecodeEndAbsoluteKnown = false;
        bool outputMutedForSettle = false;
        bool rollingDecode = false;
        bool speakerMayEmit = false;
        bool hasAudioBlock = false;
        bool publishVoiceDiag = false;
        bool stale = false;
        size_t iqSamples = 0;
        size_t freshIqSamples = 0;
        size_t contextIqSamples = 0;
        uint64_t trafficGeneration = 0;
        uint32_t talkgroupId = 0;
        uint32_t sourceId = 0;
        bool tdmaSlotKnown = false;
        uint8_t tdmaSlot = 0;
        double voiceFreqHz = 0.0;
        uint64_t sequence = 0;
        uint64_t flushSeq = 0;
        ReceiverSessionKey receiverSessionKey{};
        uint64_t callSessionId = 0;
        std::string speakerGateReason;
        std::string staleReason;
        std::string error;
    };

    struct P25VoiceWorkerQueueSnapshot {
    bool stopping = false;
    bool threadRunning = false;
    bool pending = false;
    bool busy = false;
    size_t pendingJobs = 0;
    uint64_t nextSequence = 0;
    long long droppedJobs = 0;
        long long droppedResults = 0;
        size_t completedResults = 0;
        long long publicationLockMisses = 0;
        size_t pendingPublishResults = 0;
    };

    QTimer* updateTimer = nullptr;
    std::unique_ptr<AudioEngine> engineForAudio;
    std::thread guiDspWorker;
    std::thread p25ControlWorkerThread;
    std::thread p25VoiceWorkerThread;
    GuiRuntimeConfig guiRuntimeConfig;
    QStringList guiRuntimeStartupErrors;
    qint64 guiRuntimeStartupAppliedMs = 0;
    std::atomic<long long> guiP25AudioOutputEvents{0};
    std::atomic<long long> guiP25AudioOutputSamples{0};
    std::atomic<long long> guiP25AudioDecodedFrames{0};
    std::atomic<long long> guiP25AudioAcceptedAmbeFrames{0};
    std::atomic<long long> guiP25AudioLastOutputMs{0};
    std::atomic<long long> guiIqReplayWindows{0};
    std::atomic<long long> guiIqReplayAudioOutputEvents{0};
    std::atomic<long long> guiIqReplayAudioOutputSamples{0};
    std::atomic<long long> guiIqReplayDecodedFrames{0};
    std::atomic<long long> guiIqReplayLastOutputMs{0};
    std::mutex guiIqReplayStatusMutex;
    QString guiIqReplayLastStatus;
    QStringList guiIqReplayRecentStatus;

    UpdateManager* m_updateManager = nullptr;   // professional GitHub release + in-app updater (state-of-the-art, safe)
    TranscriptHub* m_transcriptHub = nullptr;
    SttEngine* m_sttEngine = nullptr;
    P25TranscriptSource* m_p25TranscriptSource = nullptr;
    TranscriptWindow* m_transcriptWindow = nullptr;
    std::atomic<bool> stopDspWorker{false};
    std::atomic<bool> p25VoiceWorkerStop{false};
    std::atomic<bool> p25VoiceWorkerBusy{false};
    std::atomic<uint64_t> p25VoiceJobSequence{0};
    std::atomic<long long> p25VoiceDroppedJobs{0};
    std::atomic<long long> p25VoiceDroppedResults{0};
    std::atomic<long long> p25VoicePublicationLockMisses{0};
    std::atomic<size_t> p25VoicePendingPublishDepth{0};
    std::mutex p25VoiceWorkerMutex;
    std::condition_variable p25VoiceWorkerCv;
    std::deque<P25VoiceDecodeJob> p25VoicePendingJobs;
    std::deque<P25VoiceDecodeResult> p25VoiceCompletedResults;
    std::mutex monitorParamsMutex;  // protects currentMonitor* / monitor* vars between GUI DSP worker and UI thread (P2) -- transitional during per-receiver refactor

    // S0-2 (P0 audit): mutex + snapshot for receivers vector. GUI thread mutates (Add, Remove, tune etc).
    // DSP worker takes short snapshot then processes without holding lock or & across yields.
    // Same pattern applied to CLI cliReceivers.
    std::mutex receiversMutex;

    // S0-5 (P1): centralize AudioEngine creation. DSP worker and hot paths call this;
    // creation happens at most once (call_once). UI config also uses it.
    // guiStartupSettled stays false until after show()/event-loop entry so the DSP
    // worker cannot open WASAPI/miniaudio (or queue UI updates) during ctor.
    std::atomic<bool> guiStartupSettled{false};
    std::atomic<bool> shutdownStarted{false};
    std::atomic<bool> stopAllStreamingStarted{false};
    std::once_flag audioEngineInitFlag;
    AudioEngine* getOrCreateAudioEngine();

    // Non-creating peek for idle DSP paths. Never opens devices.
    AudioEngine* peekAudioEngineIfReady() const noexcept;

    AudioEngine* ensureAudioOutputActive(const char* reason = "audio");

    void startP25VoiceWorker();

    void stopP25VoiceWorker();

    bool submitP25VoiceDecodeJob(P25VoiceDecodeJob job);

    bool p25VoiceWorkerCanAcceptJob();

    bool p25VoiceWorkerCanAcceptJobForDepth(bool speakerSustainHint);

    P25VoiceWorkerQueueSnapshot p25VoiceWorkerQueueSnapshot();

    std::vector<P25VoiceDecodeResult> takeP25VoiceDecodeResults();

    struct P25VoiceDecodeWorkPurge {
        size_t pendingJobs = 0;
        size_t completedResults = 0;

        size_t total() const noexcept { return pendingJobs + completedResults; }
    };

    P25VoiceDecodeWorkPurge purgeP25VoiceDecodeWorkForSession(
        const ReceiverSessionKey& sessionKey,
        uint64_t afterSequence);

    P25VoicePublishOutcome publishP25VoiceDecodeResult(const P25VoiceDecodeResult& result,
                                                       P25SpeakerPendingMap& pendingAudioByRx);

    // Phase 0 / S0-2: per-receiver foundation using shared_ptr so that vector reallocation (Add) never
    // invalidates live Receiver/Demodulator instances held by DSP worker snapshots or other references.
    // This is the clean, SOTA way to solve the P0 cross-thread vector mutation race.
    std::vector<std::shared_ptr<Receiver>> receivers;

    // Transitional single-monitor state (will be replaced by receivers[i] fields)
    double currentMonitorFreq = 100e6;
    DemodMode currentMonitorMode = DemodMode::AUTO;
    bool autoDetectMode = true;
    double monitorLpfHz = 15000;
    bool monitorAudioLpfEnabled = true;
    double monitorSquelchDb = -105;
    double monitorRfGainDb = 20.0;
    double monitorMasterVolume = 0.85;
    // Last Apply selection from Configure Output Devices (full device names).
    // ensureAudioOutputActive restores these instead of blindly opening "default",
    // which fought the dialog after re-enumerate (log 20260811 10:36 outputs→None).
    std::vector<std::string> preferredAudioOutputNames;
    double monitorGain = 1.0; // audio gain, not RF gain
    double monitorWfmDeTauUs = 75.0;
    double monitorWfmPilotNotchR = 0.96;
    double monitorChannelBwHz = 180000.0; // AUTO/WFM default; NFM snaps to 12.5 kHz for modern CB/PMR
    double p25MonitoredControlFreqHz = 0.0;
    bool p25AutoFollowEnabled = false;
    bool p25IndependentTrafficEnabled = true;
    bool p25IndependentTrafficActive = false;
    // True only for the one-RTL traffic-source mode where the physical tuner
    // has been retuned away from the control channel.  In this mode return to
    // control must retune RF before the UI/log can claim CC monitoring.
    bool p25IndependentTrafficRetunedPrimary = false;
    // Increments every time a P25 traffic-channel source is created.  The DSP
    // worker snapshots shared_ptrs, so an old traffic receiver can still exist
    // briefly after a newer grant replaces it.  The generation check prevents
    // stale sources from decoding/logging/audio after replacement.
    std::atomic<uint64_t> p25TrafficSourceGeneration{0};
    std::atomic<uint64_t> p25PendingAudioFlushSeq{0};
    bool p25FollowEnabled = false;
    bool p25FollowAutoActive = false;
    uint32_t p25FollowTalkgroupId = 0;
    double p25AutoFollowReturnControlFreqHz = 0.0;
    double p25AutoFollowVoiceFreqHz = 0.0;
    qint64 p25AutoFollowTunedAtMs = 0;
    qint64 p25AutoFollowLastGrantMs = 0;
    qint64 p25AutoFollowLastActiveMs = 0;
    qint64 p25AutoFollowLastMHzHopMs = 0;
    qint64 p25AutoFollowLastReturnMs = 0;
    double p25AutoFollowLastReturnVoiceHz = 0.0;
    qint64 p25AutoFollowWarmStandbyUntilMs = 0;
    double p25AutoFollowWarmStandbyVoiceHz = 0.0;
    qint64 p25LastSameRfMetadataSwitchMs = 0;
    qint64 p25SameRfClearGrantHoldUntilMs = 0;
    P25LiveDecoder p25LiveDecoder;
    // Stage 4: keep heavy P25 control-channel decode off the Qt GUI timer.
    // The worker owns this decoder instance; GUI thread only consumes completed results.
    P25LiveDecoder p25ControlWorkerDecoder{p25RealtimeControlDecoderConfig()};
    std::mutex p25ControlWorkerDecoderMutex;
    std::mutex p25ControlPendingMutex;
    std::optional<P25LiveDecodeResult> p25ControlPendingResult;
    std::atomic<bool> p25ControlWorkerBusy{false};
    std::atomic<long long> p25ControlDroppedResults{0};
    std::atomic<bool> p25ControlWorkerResetPending{false};
    P25ControlChannelAnalyzer p25LiveControlAnalyzer;
    std::vector<P25PendingVoiceGrant> p25PendingVoiceGrants;
    std::vector<P25RepeatedVoiceGrant> p25RepeatedVoiceGrants;
    QDialog* p25LogDialog = nullptr;
    QTextEdit* p25LogText = nullptr;
    QDialog* iqReplayDialog = nullptr;
    QCheckBox* p25AutoFollowCheckBox = nullptr;
    QCheckBox* p25IndependentTrafficCheckBox = nullptr;
    QLabel* p25StatusLabel = nullptr;
    // Sprint 0–2 P25 clear TX shell (mic capture; no RF encode yet).
    P25TxSnapshot p25TxSnapshot{};
    AudioCapture p25TxMic;
    QCheckBox* p25TxArmCheckBox = nullptr;
    QPushButton* p25TxPttButton = nullptr;
    QLabel* p25TxStatusLabel = nullptr;
    QSpinBox* p25TxRidSpinBox = nullptr;
    QSpinBox* p25TxTgSpinBox = nullptr;
    QSpinBox* p25TxNacSpinBox = nullptr;
    QSpinBox* p25TxDevSpinBox = nullptr;
    QComboBox* p25TxMicComboBox = nullptr;
    QProgressBar* p25TxMicMeterBar = nullptr;
    QCheckBox* p25TxMuteSpkCheckBox = nullptr;
    QTimer* p25TxMicMeterTimer = nullptr;
    // Sprint 3: mic → 8 kHz → AMBE placeholder packetizer while PTT held.
    std::unique_ptr<P25TxVoicePacketizer> p25TxPacketizer;
    std::vector<P25AmbeEncodedFrame> p25TxAmbeSession;
    qint64 p25TxLastEncodeLogMs = 0;
    QTimer* diagnosticsHeartbeatTimer = nullptr;
    QTimer* diagnosticsResourceTimer = nullptr;
    qint64 diagnosticsLastHeartbeatMs = 0;
    qint64 diagnosticsLastUiStallReportMs = 0;
    qint64 diagnosticsLastResourceReportMs = 0;
    QStringList p25LogLines;
    QStringList p25VisibleLogPending;
    bool p25VisibleLogFlushQueued = false;
    LiveIqCaptureSession liveIqCapture;
    std::atomic<bool> liveIqCaptureLogActive{false};
    QStringList liveIqCapturePendingP25Lines;
    QTimer* liveIqCaptureTimer = nullptr;
    std::thread liveIqCaptureWriterThread;
    std::atomic<bool> stopLiveIqCaptureWriter{false};
    QString p25LastDiagSignature;
    qint64 p25LastDiagLogMs = 0;
    std::map<std::string, qint64> p25LogThrottleByKey;
    QDoubleSpinBox* bwSpin = nullptr;
    QDoubleSpinBox* lpfSpin = nullptr;
    QCheckBox* lpfEnableCheck = nullptr;
    WaterfallRoiBuilder classifierRoiBuilder{128};
    // spectrumWidget kept for future if needed

    void appendP25LogLine(const QString& text);

    void flushVisibleP25LogLines();

    void appendP25LogLineThrottled(const QString& signature, const QString& text, qint64 minIntervalMs = 1200);

    void appendP25LogLineKeyed(const QString& key, const QString& text, qint64 minIntervalMs = 3000);

    void showTranscriptWindow();

    void showP25LogWindow();

    void showIqReplayWindow();

    size_t guiRuntimeDeviceIndex() const noexcept;

    void recordGuiRuntimeError(const QString& message);

    bool selectDefaultAudioOutputForGuiStartup(const char* reason);

    bool startGuiRuntimeDeviceAt(double freqHz, bool p25Defaults);

    bool armGuiRuntimeP25Control(double ccHz, bool grantTest);

    QString guiRuntimeSelfTestPath() const;

    bool guiRuntimeClearAudioDetected() const noexcept;

    void writeGuiRuntimeSelfTestResult(const char* phase);

    QString guiRuntimeCaptureLabel();

    void scheduleGuiRuntimeIqCapture();

    void scheduleGuiRuntimeIqReplay();

    void scheduleGuiRuntimeSelfTest();

    void applyGuiRuntimeStartupConfig();

    // Helper to ensure at least one receiver exists (transitional Phase 0)
    void ensureReceiver();

    // Phase 0 sync: keep receivers[0] in sync with the live monitor* control vars (UI writes here)
    void syncMonitorVarsToReceiver(size_t idx = 0);

    void setReceiverActive(size_t idx, bool active);

    TrainingCaptureResult captureTrainingSample(const std::string& label);


    void writeLiveIqCaptureEvent(const json& row, bool flushNow = true);

    LiveIqCaptureResult startLiveIqCapture(const std::string& label, int plannedDurationMs = 0);

    void pollLiveIqCapture(bool finalPoll);

    LiveIqCaptureResult stopLiveIqCapture();

    IqTestCaptureResult captureIqTestWindow(const std::string& label, double seconds, const QDateTime& startedUtc);


    void stopAllStreaming();

    void closeEvent(QCloseEvent* event) override;

    void createMenus();
};

