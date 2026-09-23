#pragma once

#include "SatcomAsyncLog.h"
#include "SatcomIqCursor.h"

#include <atomic>
#include <complex>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

struct SatcomPreset {
    std::string name;
    double lowHz = 420e6;
    double highHz = 430e6;
    double stepHz = 12.5e3;
    double bandwidthHz = 250e3;
    std::string mode = "NFM";
    double squelchDb = -90.0;
};

struct SatcomScannerConfig {
    double lowHz = 420e6;
    double highHz = 430e6;
    double stepHz = 12.5e3;
    int dwellMs = 80;
    double bandwidthHz = 250e3;
    std::string mode = "NFM";
    double squelchDb = -90.0;
    size_t deviceIndex = 0;
    std::string deviceStableKey;
    std::string recordDir;
    std::string logDir;
    bool enableAx25 = true;
    bool enableApt = true;
    bool autoCapture = true;
    bool monitorAudio = true;
    std::vector<SatcomPreset> presets;

    static SatcomScannerConfig defaults();
    nlohmann::json toJson() const;
    static SatcomScannerConfig fromJson(const nlohmann::json& j);
    void load();
    void save() const;
};

enum class SatcomScannerState {
    Idle = 0,
    Scanning = 1,
    Locked = 2,
    Recording = 3
};

struct SatcomScannerSnapshot {
    SatcomScannerState state = SatcomScannerState::Idle;
    SatcomScannerConfig config;
    double currentHz = 0.0;
    double lockHz = 0.0;
    double recordHz = 0.0;
    double audioRmsDb = -120.0;
    std::string deviceLabel;
    bool deviceConnected = false;
    std::string streamState;
    bool audioMonitoring = false;
    bool sharedMainAudio = false;
    bool hostTakeoverActive = false;
    uint64_t iqDiscontinuities = 0;
    std::vector<float> spectrumDb;
    double spectrumCenterHz = 0.0;
    double spectrumRateHz = 0.0;
    std::vector<std::string> recentDecodes;
    std::string aptPreviewPath;
    std::string recordPath;
    std::string sstvOutputDir;
    uint64_t logWritten = 0;
    uint64_t logDropped = 0;
    std::string lastStatus;
    bool passArmed = false;
    bool autoTrack = false;
    double dopplerHz = 0.0;
    double tunedHz = 0.0;
    std::string armedRole;
    size_t activeDeviceIndex = std::numeric_limits<size_t>::max();
};

class Ax25AprsDecoder;
class AptImageDecoder;
class AudioEngine;
class Demodulator;
class SstvReceiverFeed;

class SatcomScannerEngine {
public:
    static SatcomScannerEngine& instance();

    void setConfig(const SatcomScannerConfig& cfg);
    SatcomScannerConfig config() const;
    void setAutoCaptureEnabled(bool on);
    bool autoCaptureEnabled() const;
    void setMonitorAudioEnabled(bool on) {
        const bool active = run_.load(std::memory_order_acquire);
        {
            std::lock_guard<std::mutex> lk(mutex_);
            config_.monitorAudio = on;
            config_.save();
            if (!on) audioMonitoring_ = false;
        }

        if (!on) {
            shutdownAudioOutput();
        } else if (active) {
            std::string error;
            if (!ensureAudioOutput(&error) && !error.empty()) {
                std::lock_guard<std::mutex> lk(mutex_);
                lastStatus_ = error;
            }
        }
        notifyUpdate();
    }
    size_t resolveDeviceIndex(std::string* error = nullptr);

    // Manual Start/Arm uses force=true and takes over an ordinary Listen stream.
    // P25 ownership is always protected and is never interrupted by this class.
    bool start(bool force = false);
    void stop();
    void skip();
    bool startRecording();
    void stopRecording();
    bool applyPreset(const std::string& name);

    bool armPass(const std::string& satId, const std::string& downlinkId, bool autoTrack = true,
                 bool force = false, std::string* error = nullptr);
    void disarmPass();
    void setAutoTrack(bool on);
    void tickPassTrack();

    SatcomScannerSnapshot snapshot() const;
    std::string stateName() const;
    void setUpdateCallback(std::function<void()> cb);

private:
    SatcomScannerEngine();
    ~SatcomScannerEngine();

    struct PreviousDeviceState {
        size_t deviceIndex = static_cast<size_t>(-1);
        bool wasEnabled = false;
        bool wasStreaming = false;
        double centerHz = 0.0;
    };

    void workerLoop();
    bool refreshSpectrum(size_t deviceIndex, double* peakHz = nullptr, double* peakDb = nullptr);
    bool detectActivity(size_t deviceIndex, double& peakHz, double& peakDb);
    void processLockedAudio();
    void resetChronologicalInput();
    SatcomIqCursor::Result pullNewIq(size_t deviceIndex, size_t maxSamples);
    bool prepareReceiverForSatcom(size_t deviceIndex, bool force, std::string* error);
    bool tuneAndConfirm(size_t deviceIndex, double frequencyHz, int timeoutMs, std::string* error);
    bool waitForOperationalStream(size_t deviceIndex, int timeoutMs, std::string* error);
    bool ensureAudioOutput(std::string* error = nullptr);
    bool beginHostTakeover(size_t deviceIndex, std::string* error);
    void endHostTakeover();
    void pushMonitorAudio(const float* samples, size_t count);
    void shutdownAudioOutput();
    void capturePreviousDeviceState(size_t deviceIndex);
    void restorePreviousDeviceState();
    void notifyUpdate();
    void pushLog(SatcomLog::EventType t, double hz, const char* text);
    std::string makeCaptureStem(const std::string& satId, const std::string& downlinkId) const;
    void writeRecordingMetadata(const std::string& path) const;
    void startSstvCapture(const std::string& satId, const std::string& downlinkId);
    void finishSstvCapture(bool cancel);

    mutable std::mutex mutex_;
    SatcomScannerConfig config_;
    SatcomScannerState state_ = SatcomScannerState::Idle;
    double currentHz_ = 0.0;
    double lockHz_ = 0.0;
    double recordHz_ = 0.0;
    double audioRmsDb_ = -120.0;
    std::string deviceLabel_;
    bool deviceConnected_ = false;
    std::string streamState_ = "stopped";
    bool audioMonitoring_ = false;
    std::vector<float> spectrumDb_;
    double spectrumCenterHz_ = 0.0;
    double spectrumRateHz_ = 0.0;
    std::vector<std::string> recentDecodes_;
    std::string aptPreviewPath_;
    std::string recordPath_;
    std::string recordSatId_;
    std::string recordDownlinkId_;
    std::string armedRole_;
    std::string sstvOutputDir_;
    std::string lastStatus_;
    bool skipRequested_ = false;
    bool recordRequested_ = false;
    bool recording_ = false;
    bool passTrackActive_ = false;
    bool passStartedEngine_ = false;
    double lastTrackHz_ = 0.0;
    size_t activeDeviceIndex_ = static_cast<size_t>(-1);
    std::optional<PreviousDeviceState> previousDeviceState_;

    std::atomic<bool> run_{false};
    std::thread worker_;
    std::function<void()> updateCb_;

    SatcomLog::AsyncLog log_;
    std::unique_ptr<Ax25AprsDecoder> ax25_;
    std::unique_ptr<AptImageDecoder> apt_;
    std::unique_ptr<Demodulator> demod_;
    std::atomic<bool> demodResetRequested_{true};

    std::mutex iqMutex_;
    SatcomIqCursor iqCursor_;
    std::atomic<uint64_t> iqDiscontinuities_{0};
    std::atomic<uint64_t> sstvSourceEpoch_{1};
    std::atomic<uint64_t> sstvAudioFirstSample_{0};

    std::mutex audioMutex_;
    AudioEngine* audio_ = nullptr; // borrowed from MainWindow or fallbackAudio_
    std::unique_ptr<AudioEngine> fallbackAudio_;
    std::atomic<bool> usingSharedAudio_{false};
    bool hostTakeoverActive_ = false;

    std::shared_ptr<SstvReceiverFeed> sstvFeed_;
    mutable std::mutex sstvMutex_;
    std::thread sstvThread_;
    std::atomic<bool> sstvFinish_{false};
    std::atomic<bool> sstvCancel_{false};
};
