#include "InmarsatEngine.h"
#include "AdsBTrackStore.h"
#include "DeviceManager.h"
#include "Receiver.h"
#include "SdrDeviceCandidate.h"
#include "InmarsatDiagnostics.h"
#include "InmarsatAudio.h"
#include "SatcomHostServices.h"

#include <QDir>
#include <QStandardPaths>
#include <QDateTime>
#include <QCoreApplication>
#include <QMetaObject>
#include <QSaveFile>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <fstream>
#include <limits>
#include <spdlog/spdlog.h>

namespace {

std::string configPath() {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(base);
    return (base + "/inmarsat_engine.json").toStdString();
}

double unixNow() {
    return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
}

double steadySeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool containsInsensitive(std::string value, std::string token) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    std::transform(token.begin(), token.end(), token.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value.find(token) != std::string::npos;
}

} // namespace

InmarsatEngineConfig InmarsatEngineConfig::defaults() {
    InmarsatEngineConfig config;
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    config.recordDir = (base + "/inmarsat_recordings").toStdString();
    return config;
}

nlohmann::json InmarsatEngineConfig::toJson() const {
    return {
        {"deviceIndex", deviceIndex},
        {"deviceStableKey", deviceStableKey},
        {"bandPlanId", bandPlanId},
        {"channelHz", channelHz},
        {"mode", mode},
        {"baud", baud},
        {"voiceFollow", voiceFollow},
        {"recordVoice", recordVoice},
        {"playAudio", playAudio},
        {"recordDir", recordDir},
        {"watch",watch.toJson()},
    };
}

InmarsatEngineConfig InmarsatEngineConfig::fromJson(const nlohmann::json& json) {
    InmarsatEngineConfig config = defaults();
    if (!json.is_object()) return config;
    config.deviceIndex = json.value("deviceIndex", config.deviceIndex);
    config.deviceStableKey = json.value("deviceStableKey", config.deviceStableKey);
    config.bandPlanId = json.value("bandPlanId", config.bandPlanId);
    config.channelHz = json.value("channelHz", config.channelHz);
    config.mode = json.value("mode", config.mode);
    config.baud = json.value("baud", config.baud);
    config.voiceFollow = false;
    config.recordVoice = json.value("recordVoice", false);
    config.playAudio = json.value("playAudio", true);
    config.recordDir = json.value("recordDir", config.recordDir);
    if(json.contains("watch")) config.watch=InmarsatWatchConfig::fromJson(json.at("watch"));
    return config;
}

void InmarsatEngineConfig::load() {
    try {
        std::ifstream in(configPath());
        if (!in) {
            *this = defaults();
            return;
        }
        nlohmann::json json;
        in >> json;
        *this = fromJson(json);
    } catch (...) {
        *this = defaults();
    }
}

void InmarsatEngineConfig::save() const {
    watch.validate();
    QSaveFile file(QString::fromStdString(configPath()));
    const auto data=toJson().dump(2);
    if(!file.open(QIODevice::WriteOnly) || file.write(data.data(),qint64(data.size()))!=qint64(data.size()) || !file.commit())
        throw std::runtime_error("Cannot save Inmarsat configuration: "+file.errorString().toStdString());
}

InmarsatEngine& InmarsatEngine::instance() {
    static InmarsatEngine engine;
    return engine;
}

InmarsatEngine::InmarsatEngine() {
    config_.load();
    InmarsatBandPlanStore::instance().reload(nullptr);
    if (const auto* plan = InmarsatBandPlanStore::instance().findById(config_.bandPlanId)) {
        bandPlanName_ = plan->name;
        if (!plan->channels.empty() && config_.channelHz <= 0) {
            config_.channelHz = plan->channels.front().freqHz;
            config_.mode = plan->channels.front().mode;
            config_.baud = plan->channels.front().baud;
        }
    }
    iqRx_ = std::make_unique<Receiver>();
    lastStatus_ = "Classic Aero experimental receiver ready";
}

InmarsatEngine::~InmarsatEngine() { stop(); }

void InmarsatEngine::setUpdateCallback(std::function<void()> callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    updateCb_ = std::move(callback);
}

void InmarsatEngine::notify() {
    std::function<void()> callback;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        callback = updateCb_;
    }
    if (callback) {
        try {
            callback();
        } catch (...) {
        }
    }
}

bool InmarsatEngine::setConfig(const InmarsatEngineConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    try {config.watch.validate();config.save();}
    catch(const std::exception& e) {lastStatus_=e.what();spdlog::warn("Inmarsat settings: {}",e.what());return false;}
    config_ = config;
    config_.voiceFollow = false;
    return true;
}

InmarsatEngineConfig InmarsatEngine::config() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

InmarsatDemodMode InmarsatEngine::demodModeLocked() const {
    const bool egc = (config_.mode == "egc");
    if(config_.mode=="aero_burst")return config_.baud==1200?InmarsatDemodMode::AeroBurstMsk1200:InmarsatDemodMode::AeroBurstOqpsk10500;
    return InmarsatDemod::modeFromBaud(config_.baud, egc);
}

size_t InmarsatEngine::resolveDeviceIndex(std::string* error) {
    auto& manager = DeviceManager::instance();
    const auto devices = manager.getDevices();
    if (devices.empty()) {
        if (error) *error = "No SDR devices are available; rescan devices first";
        return std::numeric_limits<size_t>::max();
    }

    InmarsatEngineConfig selected = config();
    size_t chosen = std::numeric_limits<size_t>::max();
    if (!selected.deviceStableKey.empty()) {
        for (size_t i = 0; i < devices.size(); ++i) {
            if (devices[i].stableKey == selected.deviceStableKey && SdrDeviceCandidate::canAttemptRealHardware(devices[i].label)) {
                chosen = i;
                break;
            }
        }
    }
    if (chosen == std::numeric_limits<size_t>::max() && selected.deviceIndex < devices.size() &&
        SdrDeviceCandidate::canAttemptRealHardware(devices[selected.deviceIndex].label)) {
        chosen = selected.deviceIndex;
    }

    const auto choose = [&](auto predicate) {
        if (chosen != std::numeric_limits<size_t>::max()) return;
        for (size_t i = 0; i < devices.size(); ++i) {
            if (SdrDeviceCandidate::canAttemptRealHardware(devices[i].label) && predicate(i, devices[i])) {
                chosen = i;
                return;
            }
        }
    };
    choose([&](size_t index, const DeviceInfo&) { return manager.isStreaming(index); });
    choose([](size_t, const DeviceInfo& device) { return device.enabled; });
    choose([](size_t, const DeviceInfo& device) { return device.isSdrplay; });
    choose([](size_t, const DeviceInfo&) { return true; });

    if (chosen == std::numeric_limits<size_t>::max()) {
        if (error) *error = "No hardware-capable SDR entry is available. Rescan devices; explicit (stub) demo entries cannot run Inmarsat.";
        return chosen;
    }

    const std::string stableKey = devices[chosen].stableKey;
    if (selected.deviceIndex != chosen || selected.deviceStableKey != stableKey) {
        selected.deviceIndex = chosen;
        selected.deviceStableKey = stableKey;
        setConfig(selected);
    }
    if (error) error->clear();
    return chosen;
}

void InmarsatEngine::capturePreviousDeviceState(size_t deviceIndex) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (previousDeviceState_.has_value()) return;
    }
    auto& manager = DeviceManager::instance();
    const auto devices = manager.getDevices();
    if (deviceIndex >= devices.size()) return;

    PreviousDeviceState saved;
    saved.deviceIndex = deviceIndex;
    saved.wasEnabled = devices[deviceIndex].enabled;
    saved.wasStreaming = manager.isStreaming(deviceIndex);
    saved.centerHz = manager.getCurrentCenterFreq(deviceIndex);

    std::lock_guard<std::mutex> lock(mutex_);
    if (!previousDeviceState_.has_value()) previousDeviceState_ = saved;
}

void InmarsatEngine::restorePreviousDeviceState() {
    std::optional<PreviousDeviceState> saved;
    bool restoreListen = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        saved = previousDeviceState_;
        previousDeviceState_.reset();
        restoreListen = hostTakeoverActive_;
        hostTakeoverActive_ = false;
        activeDeviceIndex_ = std::numeric_limits<size_t>::max();
        deviceConnected_ = false;
        streamState_ = "stopped";
    }

    auto& manager = DeviceManager::instance();
    if (!saved.has_value() || saved->deviceIndex == std::numeric_limits<size_t>::max()) {
        manager.releaseDeviceLease(DeviceManager::DeviceLeaseOwner::Inmarsat);
        if (restoreListen) SatcomHostServices::instance().endReceiverTakeover();
        return;
    }

    const size_t deviceIndex = saved->deviceIndex;
    if (saved->wasStreaming) {
        manager.setEnabled(deviceIndex, true);
        if (!manager.isStreaming(deviceIndex)) manager.startStreaming(deviceIndex, true);
        if (saved->centerHz > 0.0) {
            std::string ignored;
            manager.retuneWithLease(deviceIndex, saved->centerHz,
                                    DeviceManager::DeviceLeaseOwner::Inmarsat,
                                    true, &ignored);
        }
    } else {
        manager.stopStreaming(deviceIndex);
        manager.setEnabled(deviceIndex, saved->wasEnabled);
    }
    manager.releaseDeviceLease(DeviceManager::DeviceLeaseOwner::Inmarsat);
    // Restore RF first; otherwise ordinary Listen demodulates the satellite carrier.
    if (restoreListen) SatcomHostServices::instance().endReceiverTakeover();
}

bool InmarsatEngine::waitForOperationalStream(size_t deviceIndex, int timeoutMs,
                                              std::string* error) {
    auto& manager = DeviceManager::instance();
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(std::max(250, timeoutMs));
    std::string state = "starting";
    while (std::chrono::steady_clock::now() < deadline) {
        state = manager.getRuntimeStateLabel(deviceIndex);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            streamState_ = state;
        }
        if (state == "live hardware") {
            const auto probe = manager.getRecentIQWindowWithCursor(deviceIndex, 4096);
            if (!probe.samples.empty() && probe.endAbsolute > probe.startAbsolute) {
                if (error) error->clear();
                return true;
            }
        }
        if (containsInsensitive(state, "hardware failed") ||
            containsInsensitive(state, "driver stuck") ||
            state == "simulated/stub") {
            if (error) *error = "Receiver did not enter live hardware mode: " + state;
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    if (error) *error = "Timed out waiting for live hardware IQ; final state: " + state;
    return false;
}

bool InmarsatEngine::tuneAndConfirm(size_t deviceIndex, double frequencyHz,
                                    int timeoutMs, std::string* error) {
    auto& manager = DeviceManager::instance();
    std::string tuneError;
    if (!manager.retuneWithLease(deviceIndex, frequencyHz,
                                 DeviceManager::DeviceLeaseOwner::Inmarsat,
                                 true, &tuneError)) {
        if (error) *error = tuneError.empty() ? "Could not tune Inmarsat receiver" : tuneError;
        return false;
    }

    const uint64_t requested = manager.getCenterTuneRequestSeq(deviceIndex);
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(std::max(250, timeoutMs));
    while (std::chrono::steady_clock::now() < deadline) {
        const std::string state = manager.getRuntimeStateLabel(deviceIndex);
        if (state != "live hardware") {
            if (error) *error = "Inmarsat receiver left live hardware mode while tuning: " + state;
            return false;
        }
        if (manager.getCenterTuneAppliedSeq(deviceIndex) >= requested &&
            std::abs(manager.getCurrentCenterFreq(deviceIndex) - frequencyHz) <= 250.0) {
            if (error) error->clear();
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (error) {
        *error = "Inmarsat receiver did not confirm " +
                 std::to_string(frequencyHz / 1e6) + " MHz";
    }
    return false;
}

bool InmarsatEngine::selectBandPlan(const std::string& id) {
    const auto* plan = InmarsatBandPlanStore::instance().findById(id);
    if (!plan) return false;

    const bool restart = run_.load(std::memory_order_acquire);
    if (restart) stop();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        config_.bandPlanId = plan->id;
        bandPlanName_ = plan->name;
        if (!plan->channels.empty()) {
            const InmarsatChannel* pick = &plan->channels.front();
            for (const auto& channel : plan->channels) {
                if (channel.mode == "aero_oqpsk") {
                    pick = &channel;
                    break;
                }
            }
            config_.channelHz = pick->freqHz;
            config_.mode = pick->mode;
            config_.baud = pick->baud;
            tunedHz_ = pick->freqHz;
            controlHz_ = pick->freqHz;
        }
        try {config_.save();}
        catch(const std::exception& e){lastStatus_=e.what();return false;}
        lastStatus_ = "Band plan " + plan->name;
    }
    return !restart || start(true);
}

bool InmarsatEngine::selectChannel(double frequencyHz, const std::string& mode, int baud) {
    const bool restart = run_.load(std::memory_order_acquire);
    if (restart) stop();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        config_.channelHz = frequencyHz;
        if (!mode.empty()) config_.mode = mode;
        if (baud > 0) config_.baud = baud;
        tunedHz_ = frequencyHz;
        if (!followingVoice_) controlHz_ = frequencyHz;
        try {config_.save();}
        catch(const std::exception& e){lastStatus_=e.what();return false;}
        lastStatus_ = restart ? "Changing channel and restarting receiver" : "Channel selected";
    }
    return !restart || start(true);
}

bool InmarsatEngine::start(bool force) {
    if (run_.load(std::memory_order_acquire)) return true;
    const auto requestedConfig=config();
    if(requestedConfig.watch.enabled && std::none_of(requestedConfig.watch.channels.begin(),
        requestedConfig.watch.channels.end(),[](const auto& c){return c.enabled;})) {
        std::lock_guard<std::mutex> lock(mutex_);
        lastStatus_="Enable at least one saved Aero watch channel";
        return false;
    }
    if (worker_.joinable()) {
        worker_.join();
        // Finish a failed session's pending GUI restore before a new takeover.
        restorePreviousDeviceState();
    }

    std::string error;
    const size_t deviceIndex = resolveDeviceIndex(&error);
    if (deviceIndex == std::numeric_limits<size_t>::max()) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            lastStatus_ = error.empty() ? "No receiver selected" : error;
        }
        notify();
        return false;
    }

    auto& manager = DeviceManager::instance();
    const auto owner = manager.deviceLeaseOwner();
    if (owner == DeviceManager::DeviceLeaseOwner::P25 ||
        owner == DeviceManager::DeviceLeaseOwner::Satcom ||
        owner == DeviceManager::DeviceLeaseOwner::Aircraft) {
        error = std::string(DeviceManager::leaseOwnerName(owner)) +
                " owns the receiver; select another device or stop that mode first";
        {
            std::lock_guard<std::mutex> lock(mutex_);
            lastStatus_ = error;
        }
        notify();
        return false;
    }

    capturePreviousDeviceState(deviceIndex);
    if (!manager.acquireDeviceLease(deviceIndex, DeviceManager::DeviceLeaseOwner::Inmarsat,
                                    force, &error)) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            previousDeviceState_.reset();
            lastStatus_ = error;
        }
        notify();
        return false;
    }

    // DEC-0123: a tuner lease alone does not park the GUI's analog Listen DSP.
    // This existing host bridge also refuses an active P25 receiver.
    auto& host = SatcomHostServices::instance();
    if (!host.beginReceiverTakeover(deviceIndex, &error)) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            previousDeviceState_.reset(); // No hardware state has changed yet.
            lastStatus_ = error.empty() ? "Could not pause Listen audio for Inmarsat" : error;
        }
        manager.releaseDeviceLease(DeviceManager::DeviceLeaseOwner::Inmarsat);
        notify();
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        hostTakeoverActive_ = host.installed();
    }

    const bool reuseLive = manager.isStreaming(deviceIndex) &&
                           manager.getRuntimeStateLabel(deviceIndex) == "live hardware";
    {
        std::lock_guard<std::mutex> lock(mutex_);
        lastStatus_ = reuseLive
            ? "Taking over active Listen receiver without reopening hardware"
            : "Starting selected Inmarsat receiver";
    }

    if (!manager.setEnabled(deviceIndex, true) || !manager.startStreaming(deviceIndex, true)) {
        error = "Could not start selected Inmarsat receiver";
        {
            std::lock_guard<std::mutex> lock(mutex_);
            lastStatus_ = error;
            streamState_ = manager.getRuntimeStateLabel(deviceIndex);
        }
        restorePreviousDeviceState();
        notify();
        return false;
    }
    if (!waitForOperationalStream(deviceIndex, 10000, &error)) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            lastStatus_ = error;
            deviceConnected_ = false;
        }
        restorePreviousDeviceState();
        notify();
        return false;
    }

    const InmarsatEngineConfig selected = config();
    double firstCenter=selected.channelHz;
    try {
        if(selected.watch.enabled) firstCenter=planInmarsatWatch(selected.watch,manager.getCurrentSampleRate(deviceIndex)).front().centerHz;
    } catch(const std::exception& e) {error=e.what();}
    if (!error.empty() || !tuneAndConfirm(deviceIndex, firstCenter, 4000, &error)) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            lastStatus_ = error;
        }
        restorePreviousDeviceState();
        notify();
        return false;
    }

    const auto devices = manager.getDevices();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = InmarsatEngineState::Running;
        activeDeviceIndex_ = deviceIndex;
        deviceLabel_ = deviceIndex < devices.size() ? devices[deviceIndex].label : "receiver";
        deviceConnected_ = true;
        streamState_ = "live hardware";
        tunedHz_ = firstCenter;
        controlHz_ = config_.channelHz;
        followingVoice_ = false;
        carrierDetected_ = false;
        locked_ = false;
        quality_ = 0.0;
        ebnoDb_ = 0.0;
        rawBlocks_ = 0;
        validatedFrames_ = 0;
        voiceFrames_ = 0;
        lastStatus_ = "Classic Aero receiver running (experimental)";
        QDir().mkpath(QString::fromStdString(config_.recordDir));
        pipeline_ = {};
        pipelineReport_ = nlohmann::json::object();
        if (iqRx_) {
            iqRx_->deviceIndex = deviceIndex;
            iqRx_->lastConsumedAbsolute.store(0, std::memory_order_release);
        }
    }
    if (iqRx_) manager.setReceiverCursorToLiveEdge(deviceIndex, *iqRx_);
    run_.store(true, std::memory_order_release);
    try {
        worker_ = std::thread(&InmarsatEngine::workerLoop, this);
    } catch (const std::exception& e) {
        run_.store(false, std::memory_order_release);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_ = InmarsatEngineState::Idle;
            lastStatus_ = std::string("Could not start Inmarsat worker: ") + e.what();
        }
        restorePreviousDeviceState();
        notify();
        return false;
    }
    notify();
    return true;
}

void InmarsatEngine::stop() {
    const bool wasRunning = run_.exchange(false, std::memory_order_acq_rel);
    if (worker_.joinable()) worker_.join();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = InmarsatEngineState::Idle;
        followingVoice_ = false;
        deviceConnected_ = false;
        streamState_ = "stopped";
        lastStatus_ = wasRunning ? "Stopped; previous receiver state restored" : "Stopped";
    }
    restorePreviousDeviceState();
    notify();
}

std::string InmarsatEngine::stateName() const {
    std::lock_guard<std::mutex> lock(mutex_);
    switch (state_) {
    case InmarsatEngineState::Running: return "running";
    case InmarsatEngineState::VoiceFollow: return "voice_follow";
    case InmarsatEngineState::Idle:
    default: return "idle";
    }
}

InmarsatEngineSnapshot InmarsatEngine::snapshot() const {
    InmarsatEngineSnapshot snapshot;
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot.state = state_;
    snapshot.config = config_;
    snapshot.carrierDetected = carrierDetected_;
    snapshot.locked = locked_;
    snapshot.quality = quality_;
    snapshot.ebnoDb = ebnoDb_;
    snapshot.tunedHz = tunedHz_;
    snapshot.controlHz = controlHz_;
    snapshot.voiceHz = voiceHz_;
    snapshot.followingVoice = followingVoice_;
    snapshot.recording = state_ != InmarsatEngineState::Idle && pipelineReport_.contains("audio") &&
        pipelineReport_["audio"].value("recording",false);
    snapshot.rawBlocks = rawBlocks_;
    snapshot.validatedFrames = validatedFrames_;
    snapshot.messages = messages_;
    snapshot.voiceFrames = voiceFrames_;
    snapshot.bandPlanName = bandPlanName_;
    snapshot.lastStatus = lastStatus_;
    snapshot.deviceLabel = deviceLabel_;
    snapshot.streamState = streamState_;
    snapshot.deviceConnected = deviceConnected_;
    snapshot.activeDeviceIndex = activeDeviceIndex_;
    snapshot.spectrumDb = spectrumDb_;
    snapshot.spectrumCenterHz = spectrumCenterHz_;
    snapshot.spectrumRateHz = spectrumRateHz_;
    snapshot.recentLines = recentLines_;
    snapshot.diagnostics = pipelineReport_;
    snapshot.diagnosticLog = diagnosticLog_;
    return snapshot;
}

nlohmann::json InmarsatEngine::statusJson() const {
    const auto snapshot = this->snapshot();
    nlohmann::json json;
    json["state"] = stateName();
    json["carrierDetected"] = snapshot.carrierDetected;
    json["locked"] = snapshot.locked;
    json["quality"] = snapshot.quality;
    json["ebnoDb"] = snapshot.ebnoDb;
    json["tunedHz"] = snapshot.tunedHz;
    json["tunedMHz"] = snapshot.tunedHz / 1e6;
    json["controlHz"] = snapshot.controlHz;
    json["controlMHz"] = snapshot.controlHz / 1e6;
    json["voiceHz"] = snapshot.voiceHz;
    json["voiceMHz"] = snapshot.voiceHz / 1e6;
    json["followingVoice"] = snapshot.followingVoice;
    json["recording"] = snapshot.recording;
    json["rawBlocks"] = snapshot.rawBlocks;
    json["validatedFrames"] = snapshot.validatedFrames;
    json["messages"] = snapshot.messages;
    json["voiceFrames"] = snapshot.voiceFrames;
    json["bandPlanId"] = snapshot.config.bandPlanId;
    json["bandPlanName"] = snapshot.bandPlanName;
    json["lastStatus"] = snapshot.lastStatus;
    json["deviceLabel"] = snapshot.deviceLabel;
    json["streamState"] = snapshot.streamState;
    json["deviceConnected"] = snapshot.deviceConnected;
    json["activeDeviceIndex"] = snapshot.activeDeviceIndex;
    json["config"] = snapshot.config.toJson();
    json["spectrumCenterHz"] = snapshot.spectrumCenterHz;
    json["spectrumRateHz"] = snapshot.spectrumRateHz;
    json["spectrumDb"] = snapshot.spectrumDb;
    json["recentLines"] = snapshot.recentLines;
    json["diagnostics"] = snapshot.diagnostics;
    json["diagnosticLog"] = snapshot.diagnosticLog;
    json["voiceBackend"] = true;
    json["experimental"] = true;
    json["rfQualified"] = false;
    json["note"] = "Native Classic Aero receive/voice and CRC-validated ADS-C; live RF qualification pending";
    return json;
}

void InmarsatEngine::onMessage(const InmarsatMessage& message) {
    InmarsatMessageStore::instance().push(message);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++messages_;
        std::string line = InmarsatMessage::kindName(message.kind);
        line += " ";
        if (!message.label.empty()) {
            line += message.label;
            line += " ";
        }
        line += message.text.substr(0, 120);
        recentLines_.push_back(line);
        if (recentLines_.size() > 40) recentLines_.erase(recentLines_.begin());
        lastStatus_ = line.substr(0, 80);
    }

    if (message.validated && message.hasPosition && message.aesId != 0) {
        AdsBTrackStore::instance().ingestAdscPosition(
            message.aesId, message.latDeg, message.lonDeg,
            message.icaoHex, message.text.substr(0, 40));
    }
    notify();
}

void InmarsatEngine::applyVoiceFollow(const InmarsatMessage&) {
    std::lock_guard<std::mutex> lock(mutex_);
    followingVoice_ = false;
    if (config_.voiceFollow)
        lastStatus_ = "Voice follow requested but unavailable until assignment frames are validated";
}

void InmarsatEngine::returnToControl() {
    size_t deviceIndex = std::numeric_limits<size_t>::max();
    double frequencyHz = 0.0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!followingVoice_) return;
        followingVoice_ = false;
        state_ = InmarsatEngineState::Running;
        frequencyHz = controlHz_ > 0 ? controlHz_ : config_.channelHz;
        tunedHz_ = frequencyHz;
        config_.mode = "aero_oqpsk";
        config_.baud = 10500;
        deviceIndex = activeDeviceIndex_;
        lastStatus_ = "Returned to control";
        pipeline_ = {};
    }
    if (deviceIndex == std::numeric_limits<size_t>::max()) return;
    try {
        DeviceManager::instance().retuneWithLease(
            deviceIndex, frequencyHz, DeviceManager::DeviceLeaseOwner::Inmarsat,
            true, nullptr);
    } catch (...) {
    }
}

bool InmarsatEngine::processIq(InmarsatAudio& audio) {
    if (!run_.load(std::memory_order_acquire)) return false;
    size_t deviceIndex = std::numeric_limits<size_t>::max();
    double tuned = 0.0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        deviceIndex = activeDeviceIndex_;
        tuned = tunedHz_ > 0 ? tunedHz_ : config_.channelHz;
    }
    if (deviceIndex == std::numeric_limits<size_t>::max()) return false;

    auto& manager = DeviceManager::instance();
    std::vector<float> power;
    double centerFrequency = 0.0;
    double sampleRate = 0.0;
    if (manager.getLatestSpectrum(deviceIndex, power, centerFrequency, sampleRate) && !power.empty()) {
        std::lock_guard<std::mutex> lock(mutex_);
        spectrumDb_ = power;
        if (spectrumDb_.size() > 4096) {
            std::vector<float> downsampled(4096, -120.0f);
            for (size_t i = 0; i < downsampled.size(); ++i) {
                const size_t begin = i * power.size() / downsampled.size();
                const size_t end = (i + 1) * power.size() / downsampled.size();
                float maximum = -200.0f;
                for (size_t k = begin; k < end && k < power.size(); ++k)
                    maximum = std::max(maximum, power[k]);
                downsampled[i] = maximum;
            }
            spectrumDb_ = std::move(downsampled);
        }
        spectrumCenterHz_ = centerFrequency;
        spectrumRateHz_ = sampleRate;
    }

    if (sampleRate <= 0.0) sampleRate = manager.getCurrentSampleRate(deviceIndex);
    if (sampleRate <= 0.0) {
        const auto devices = manager.getDevices();
        if (deviceIndex < devices.size() && devices[deviceIndex].sampleRate > 0.0)
            sampleRate = devices[deviceIndex].sampleRate;
    }
    if (sampleRate <= 0.0) sampleRate = 2.048e6;

    std::vector<std::complex<float>> iq;
    bool gap = false;
    uint64_t startSample = 0;
    if (iqRx_) {
        const auto epoch = iqRx_->lastSeenStreamEpoch.load(std::memory_order_acquire);
        const auto expected = iqRx_->lastConsumedAbsolute.load(std::memory_order_acquire);
        auto window = manager.getNewIQWindowForReceiver(deviceIndex, *iqRx_, 65536);
        gap = window.cursorDiscontinuity || window.streamEpoch != epoch ||
              (!window.samples.empty() && window.startAbsolute != expected);
        startSample = window.startAbsolute;
        iq = std::move(window.samples);
    } else {
        // Without a chronological reader, reusing the latest window duplicates IQ.
        return false;
    }
    if (iq.empty()) return false;
    if(gap) audio.discardPlayback(); // Old-source PCM cannot survive an RF discontinuity.

    // Spectrum may still describe the previous tune. IQ metadata uses confirmed RF.
    centerFrequency = manager.getCurrentCenterFreq(deviceIndex);
    InmarsatDemodMode mode;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        mode = demodModeLocked();
    }
    if(watch_) {
        watch_->process(iq,startSample,sampleRate,centerFrequency,gap,steadySeconds());
        const auto report=watch_->report(steadySeconds());
        std::lock_guard<std::mutex> lock(mutex_);
        pipelineReport_=report;
        carrierDetected_=report.value("carrierDetected",false);locked_=report.value("protocolLock",false);
        quality_=report.value("quality",0.0);rawBlocks_=report.value("rawBlocks",uint64_t{0});
        validatedFrames_=report.value("validatedFrames",uint64_t{0});voiceFrames_=report.value("voiceFrames",uint64_t{0});
        return true;
    }
    pipeline_.process(iq.data(), iq.size(), startSample, sampleRate, centerFrequency, tuned, mode, gap);
    const auto stats = pipeline_.stats();
    bool returnControl = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        carrierDetected_ = stats.carrierDetected;
        locked_ = stats.locked;
        quality_ = stats.quality;
        ebnoDb_ = stats.ebnoDb;
        rawBlocks_ = stats.rawBlocksOut;
        validatedFrames_ = stats.framesOut;
        pipelineReport_ = pipeline_.report();
        streamState_ = manager.getRuntimeStateLabel(deviceIndex);
        deviceConnected_ = streamState_ == "live hardware";
        voiceFrames_ = pipelineReport_.value("voiceFrames",uint64_t{0});
        returnControl = followingVoice_ && unixNow() > voiceFollowUntilUnix_;
    }
    if (returnControl) returnToControl();
    return true;
}

void InmarsatEngine::workerLoop() {
    bool lostHardware = false;
    bool processingFailed = false;
    InmarsatDiagnostics diagnostics;
    std::unique_ptr<InmarsatAudio> audio;
    try {
        const auto cfg=config();
        QString wav;
        if(cfg.recordVoice) {
            QDir dir(QString::fromStdString(cfg.recordDir));
            if(!dir.mkpath(".")) throw std::runtime_error("Cannot create Inmarsat recording directory");
            wav=dir.filePath("aero_"+QDateTime::currentDateTimeUtc().toString("yyyyMMdd_HHmmss_zzz")+".wav");
        }
        audio=std::make_unique<InmarsatAudio>(cfg.playAudio,wav);
        pipeline_.setPcmSink([&](std::span<const int16_t> pcm,uint32_t){audio->push(pcm);});
        pipeline_.setMessageSink([this](const InmarsatMessage& message){onMessage(message);});
        if(cfg.watch.enabled) {
            size_t device;
            {std::lock_guard<std::mutex> lock(mutex_);device=activeDeviceIndex_;}
            watch_=std::make_unique<InmarsatWatchSession>(cfg.watch,
                DeviceManager::instance().getCurrentSampleRate(device),steadySeconds(),
                [this](const InmarsatMessage& m){onMessage(m);},
                [&](std::span<const int16_t> pcm,uint32_t){audio->push(pcm);},
                [&]{audio->discardPlayback();});
        }
    } catch(const std::exception& e) {
        std::lock_guard<std::mutex> lock(mutex_);
        lastStatus_=e.what();state_=InmarsatEngineState::Idle;
        run_.store(false);processingFailed=true;
    }
    try {
        // Transport still enforces the app's explicit opt-in/off configuration.
        diagnostics.open({}, "live",true);
        const auto cfg = config();
        diagnostics.write("open", {{"state", processingFailed ? "error" : "running"},
            {"channelHz", cfg.channelHz}, {"mode", cfg.mode}, {"baud", cfg.baud},
            {"speakerRequested", cfg.playAudio}, {"recordRequested", cfg.recordVoice}});
        std::lock_guard<std::mutex> lock(mutex_);
        diagnosticLog_ = diagnostics.path().toStdString();
    } catch (const std::exception& e) {
        spdlog::warn("Inmarsat diagnostic log: {}", e.what());
    }
    auto nextLog = std::chrono::steady_clock::now();
    auto nextNotify = std::chrono::steady_clock::now();
    auto lastIq=nextNotify;
    while (run_.load(std::memory_order_acquire)) {
        size_t deviceIndex = std::numeric_limits<size_t>::max();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            deviceIndex = activeDeviceIndex_;
        }
        if (deviceIndex == std::numeric_limits<size_t>::max() ||
            DeviceManager::instance().getRuntimeStateLabel(deviceIndex) != "live hardware") {
            const std::string state = deviceIndex == std::numeric_limits<size_t>::max()
                ? "no active receiver"
                : DeviceManager::instance().getRuntimeStateLabel(deviceIndex);
            {
                std::lock_guard<std::mutex> lock(mutex_);
                streamState_ = state;
                deviceConnected_ = false;
                state_ = InmarsatEngineState::Idle;
                lastStatus_ = "Inmarsat receiver lost live hardware: " + state;
            }
            lostHardware = true;
            run_.store(false, std::memory_order_release);
            break;
        }

        bool consumed = false;
        try {
            if(watch_ && watch_->advance(steadySeconds())) {
                const auto report=watch_->report(steadySeconds());
                diagnostics.write("watch_transition",report);
                std::string error;
                if(!tuneAndConfirm(deviceIndex,watch_->centerHz(),4000,&error)) throw std::runtime_error(error);
                DeviceManager::instance().setReceiverCursorToLiveEdge(deviceIndex,*iqRx_);
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    tunedHz_=watch_->centerHz();pipelineReport_=report;
                    lastStatus_=report["watch"].value("reason",std::string{});
                    spectrumDb_.clear();
                }
            }
            consumed = processIq(*audio);
            if(audio) {
                std::lock_guard<std::mutex> lock(mutex_);
                pipelineReport_["audio"]=audio->report();
            }
        } catch (const std::exception& exception) {
            spdlog::warn("InmarsatEngine: {}", exception.what());
            std::lock_guard<std::mutex> lock(mutex_);
            lastStatus_ = exception.what();
            processingFailed = true;
        } catch (...) {
            std::lock_guard<std::mutex> lock(mutex_);
            lastStatus_ = "IQ process error";
            processingFailed = true;
        }
        if (processingFailed) {
            std::lock_guard<std::mutex> lock(mutex_);
            state_ = InmarsatEngineState::Idle;
            deviceConnected_ = false;
            run_.store(false, std::memory_order_release);
            break;
        }
        const auto now = std::chrono::steady_clock::now();
        if(consumed)lastIq=now;
        else if(now-lastIq>std::chrono::milliseconds(500)) {
            // One complete Aero C-frame with no input: never leave a stale talking marker.
            std::lock_guard<std::mutex> lock(mutex_);pipelineReport_["voiceActive"]=false;
        }
        if (now >= nextLog && !diagnostics.path().isEmpty()) {
            auto report=watch_?watch_->report(steadySeconds()):pipeline_.report();
            if(audio) report["audio"]=audio->report();
            diagnostics.write("progress", report);
            nextLog = now + std::chrono::seconds(1);
        }
        if (now >= nextNotify) {
            notify();
            nextNotify = now + std::chrono::milliseconds(40);
        }
        // DEC-0111: UI cadence must not cap input at 65536 / 40ms.
        // Drain chronological IQ before waiting for more input.
        if (!consumed) std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }

    try { if(audio) audio->finish(); }
    catch(const std::exception& e) {
        processingFailed=true;
        std::lock_guard<std::mutex> lock(mutex_);lastStatus_=e.what();
    }
    if (!diagnostics.path().isEmpty()) {
        auto report = watch_?watch_->report(steadySeconds()):pipeline_.report();
        if(audio) report["audio"]=audio->report();
        report["state"] = processingFailed ? "error" : lostHardware ? "hardware_lost" : "stopped";
        report["errorCode"] = processingFailed ? "invalid_input" : lostHardware ? "hardware_lost" : "none";
        diagnostics.write("summary", report, true);
    }
    pipeline_={}; // Destroy modem QObjects on their owning worker thread.
    watch_.reset();
    audio.reset(); // Stop C-channel playback before restoring ordinary Listen.

    if (lostHardware || processingFailed) {
        bool hasGuiTakeover;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            hasGuiTakeover = hostTakeoverActive_;
        }
        if (hasGuiTakeover && QCoreApplication::instance()) {
            // The host's off-thread end is queued. Restore on the GUI thread
            // instead, so an old end cannot unpark Listen after a new Start.
            // Never block this worker on a GUI thread that may be joining it.
            QMetaObject::invokeMethod(QCoreApplication::instance(), [this] {
                if (!run_.load(std::memory_order_acquire)) restorePreviousDeviceState();
            }, Qt::QueuedConnection);
        } else restorePreviousDeviceState();
        notify();
    }
}
