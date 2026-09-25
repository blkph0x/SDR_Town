#pragma once

#include "InmarsatBandPlan.h"
#include "InmarsatDemod.h"
#include "InmarsatPipeline.h"
#include "InmarsatMessageStore.h"
#include "InmarsatWatch.h"
#include "SatcomHostServices.h"

#include <nlohmann/json.hpp>
#include <atomic>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

struct Receiver;
class InmarsatAudio;

enum class InmarsatEngineState {
    Idle = 0,
    Running = 1,
    VoiceFollow = 2
};

struct InmarsatEngineConfig {
    size_t deviceIndex = 0;
    std::string deviceStableKey;
    std::string bandPlanId = "4f2";
    double channelHz = 1542935000.0;
    std::string mode = "aero_oqpsk"; // aero_msk | aero_oqpsk | aero_voice | egc
    int baud = 10500;
    bool voiceFollow = false; // disabled until validated assignment frames exist
    bool recordVoice = false;
    bool playAudio = true;
    std::string recordDir;
    InmarsatWatchConfig watch;

    static InmarsatEngineConfig defaults();
    nlohmann::json toJson() const;
    static InmarsatEngineConfig fromJson(const nlohmann::json& j);
    void load();
    void save() const;
};

struct InmarsatEngineSnapshot {
    InmarsatEngineState state = InmarsatEngineState::Idle;
    InmarsatEngineConfig config;
    bool carrierDetected = false;
    bool locked = false; // validated protocol lock only
    double quality = 0.0;
    double ebnoDb = 0.0;
    double tunedHz = 0.0;
    double controlHz = 0.0;
    double voiceHz = 0.0;
    bool followingVoice = false;
    bool recording = false;
    uint64_t rawBlocks = 0;
    uint64_t validatedFrames = 0;
    uint64_t messages = 0;
    uint64_t voiceFrames = 0;
    std::string bandPlanName;
    std::string lastStatus;
    std::string deviceLabel;
    std::string streamState;
    bool deviceConnected = false;
    size_t activeDeviceIndex = std::numeric_limits<size_t>::max();
    std::vector<float> spectrumDb;
    double spectrumCenterHz = 0.0;
    double spectrumRateHz = 0.0;
    std::vector<std::string> recentLines;
    nlohmann::json diagnostics = nlohmann::json::object();
    std::string diagnosticLog;
};

struct InmarsatDisplaySnapshot {
    bool running=false;
    std::vector<float> spectrumDb;
    double centerHz=0,rateHz=0;
    std::vector<InmarsatWatchChannel> channels;
    std::vector<InmarsatChannelDisplay> decoders;
};

class InmarsatEngine {
public:
    static InmarsatEngine& instance();

    bool setConfig(const InmarsatEngineConfig& cfg);
    InmarsatEngineConfig config() const;

    bool start(bool force = false);
    InmarsatTakeoverResult prepareTakeover(bool stopP25 = false);
    void stop();
    bool selectChannel(double freqHz, const std::string& mode, int baud);
    bool selectBandPlan(const std::string& id);

    InmarsatEngineSnapshot snapshot() const;
    InmarsatDisplaySnapshot displaySnapshot() const;
    std::string stateName() const;
    nlohmann::json statusJson() const;

    void setUpdateCallback(std::function<void()> cb);

private:
    InmarsatEngine();
    ~InmarsatEngine();

    struct PreviousDeviceState {
        size_t deviceIndex = std::numeric_limits<size_t>::max();
        bool wasEnabled = false;
        bool wasStreaming = false;
        double centerHz = 0.0;
    };

    void workerLoop();
    bool processIq(InmarsatAudio& audio);
    void onMessage(const InmarsatMessage& msg);
    void applyVoiceFollow(const InmarsatMessage& msg);
    void returnToControl();
    void notify();
    InmarsatDemodMode demodModeLocked() const;
    size_t resolveDeviceIndex(std::string* error = nullptr);
    bool waitForOperationalStream(size_t deviceIndex, int timeoutMs, std::string* error);
    bool tuneAndConfirm(size_t deviceIndex, double frequencyHz, int timeoutMs, std::string* error);
    void capturePreviousDeviceState(size_t deviceIndex);
    void restorePreviousDeviceState();

    mutable std::mutex mutex_;
    InmarsatEngineConfig config_;
    InmarsatEngineState state_ = InmarsatEngineState::Idle;
    bool carrierDetected_ = false;
    bool locked_ = false;
    double quality_ = 0.0;
    double ebnoDb_ = 0.0;
    double tunedHz_ = 0.0;
    double controlHz_ = 0.0;
    double voiceHz_ = 0.0;
    bool followingVoice_ = false;
    uint64_t rawBlocks_ = 0;
    uint64_t validatedFrames_ = 0;
    uint64_t messages_ = 0;
    uint64_t voiceFrames_ = 0;
    std::string bandPlanName_;
    std::string lastStatus_ = "Inmarsat idle";
    std::string deviceLabel_;
    std::string streamState_ = "stopped";
    bool deviceConnected_ = false;
    size_t activeDeviceIndex_ = std::numeric_limits<size_t>::max();
    std::optional<PreviousDeviceState> previousDeviceState_;
    bool hostTakeoverActive_ = false;
    std::vector<float> spectrumDb_;
    double spectrumCenterHz_ = 0.0;
    double spectrumRateHz_ = 0.0;
    std::vector<std::string> recentLines_;
    double voiceFollowUntilUnix_ = 0.0;

    std::atomic<bool> run_{false};
    std::thread worker_;
    std::function<void()> updateCb_;

    InmarsatPipeline pipeline_;
    std::unique_ptr<InmarsatWatchSession> watch_; // Worker-owned, including destruction.
    nlohmann::json pipelineReport_ = nlohmann::json::object();
    std::string diagnosticLog_;
    std::vector<InmarsatChannelDisplay> displays_;
    double lastDisplayIqSeconds_=0;
    std::atomic<uint64_t> watchRevision_{0};
    std::unique_ptr<Receiver> iqRx_;
};
