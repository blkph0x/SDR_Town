#pragma once

#include "InmarsatBandPlan.h"
#include "InmarsatDemod.h"
#include "InmarsatMessageStore.h"

#include <nlohmann/json.hpp>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class InmarsatAcars;
class InmarsatVoice;
struct Receiver;

enum class InmarsatEngineState {
    Idle = 0,
    Running = 1,
    VoiceFollow = 2
};

struct InmarsatEngineConfig {
    size_t deviceIndex = 0;
    std::string bandPlanId = "4f2";
    double channelHz = 1542935000.0;
    std::string mode = "aero_oqpsk"; // aero_msk | aero_oqpsk | aero_voice | egc
    int baud = 10500;
    bool voiceFollow = false; // not implemented: no unique-word / C-assign proof
    bool recordVoice = false;
    std::string recordDir;

    static InmarsatEngineConfig defaults();
    nlohmann::json toJson() const;
    static InmarsatEngineConfig fromJson(const nlohmann::json& j);
    void load();
    void save() const;
};

struct InmarsatEngineSnapshot {
    InmarsatEngineState state = InmarsatEngineState::Idle;
    InmarsatEngineConfig config;
    bool locked = false;
    double ebnoDb = 0.0;
    double tunedHz = 0.0;
    double controlHz = 0.0;
    double voiceHz = 0.0;
    bool followingVoice = false;
    bool recording = false;
    uint64_t messages = 0;
    uint64_t voiceFrames = 0;
    std::string bandPlanName;
    std::string lastStatus;
    std::vector<float> spectrumDb;
    double spectrumCenterHz = 0.0;
    double spectrumRateHz = 0.0;
    std::vector<std::string> recentLines;
};

class InmarsatEngine {
public:
    static InmarsatEngine& instance();

    void setConfig(const InmarsatEngineConfig& cfg);
    InmarsatEngineConfig config() const;

    bool start(bool force = false);
    void stop();
    bool selectChannel(double freqHz, const std::string& mode, int baud);
    bool selectBandPlan(const std::string& id);

    InmarsatEngineSnapshot snapshot() const;
    std::string stateName() const;
    nlohmann::json statusJson() const;

    void setUpdateCallback(std::function<void()> cb);

private:
    InmarsatEngine();
    ~InmarsatEngine();

    void workerLoop();
    void processIq();
    void onDecodedBytes(const uint8_t* data, size_t len);
    void onMessage(const InmarsatMessage& msg);
    void applyVoiceFollow(const InmarsatMessage& msg);
    void returnToControl();
    void notify();
    InmarsatDemodMode demodModeLocked() const;

    mutable std::mutex mutex_;
    InmarsatEngineConfig config_;
    InmarsatEngineState state_ = InmarsatEngineState::Idle;
    bool locked_ = false;
    double ebnoDb_ = 0.0;
    double tunedHz_ = 0.0;
    double controlHz_ = 0.0;
    double voiceHz_ = 0.0;
    bool followingVoice_ = false;
    uint64_t messages_ = 0;
    uint64_t voiceFrames_ = 0;
    std::string bandPlanName_;
    std::string lastStatus_ = "Inmarsat idle";
    std::vector<float> spectrumDb_;
    double spectrumCenterHz_ = 0.0;
    double spectrumRateHz_ = 0.0;
    std::vector<std::string> recentLines_;
    double voiceFollowUntilUnix_ = 0.0;

    std::atomic<bool> run_{false};
    std::thread worker_;
    std::function<void()> updateCb_;

    InmarsatDemod demod_;
    std::unique_ptr<Receiver> iqRx_;
    std::unique_ptr<InmarsatAcars> acars_;
    std::unique_ptr<InmarsatVoice> voice_;
};
