#include "InmarsatEngine.h"
#include "InmarsatAcars.h"
#include "InmarsatVoice.h"
#include "AdsBTrackStore.h"
#include "DeviceManager.h"
#include "Receiver.h"
#include "SdrDeviceCandidate.h"
#include "InmarsatDiagnostics.h"

#include <QDir>
#include <QStandardPaths>

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
        {"recordDir", recordDir},
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
    config.recordVoice = false;
    config.recordDir = json.value("recordDir", config.recordDir);
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
    try {
        std::ofstream out(configPath());
        out << toJson().dump(2);
    } catch (...) {
    }
}

InmarsatEngine& InmarsatEngine::instance() {
    static InmarsatEngine engine;
    return engine;
}

InmarsatEngine::InmarsatEngine() {
    config_.load();
    acars_ = std::make_unique<InmarsatAcars>();
    voice_ = std::make_unique<InmarsatVoice>();
    InmarsatBandPlanStore::instance().reload(nullptr);
    if (const auto* plan = InmarsatBandPlanStore::instance().findById(config_.bandPlanId)) {
        bandPlanName_ = plan->name;
        if (!plan->channels.empty() && config_.channelHz <= 0) {
            config_.channelHz = plan->channels.front().freqHz;
            config_.mode = plan->channels.front().mode;
            config_.baud = plan->channels.front().baud;
        }
    }
    acars_->setSink([this](const InmarsatMessage& message) { onMessage(message); });
    iqRx_ = std::make_unique<Receiver>();
    lastStatus_ = "Experimental physical-layer monitor — select a receiver and press START / TAKE OVER";
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

void InmarsatEngine::setConfig(const InmarsatEngineConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
    config_.voiceFollow = false;
    config_.recordVoice = false;
    config_.save();
    if (voice_) voice_->setRecording(config_.recordVoice, config_.recordDir, 0);
}

InmarsatEngineConfig InmarsatEngine::config() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

InmarsatDemodMode InmarsatEngine::demodModeLocked() const {
    const bool egc = (config_.mode == "egc");
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
    {
        std::lock_guard<std::mutex> lock(mutex_);
        saved = previousDeviceState_;
        previousDeviceState_.reset();
        activeDeviceIndex_ = std::numeric_limits<size_t>::max();
        deviceConnected_ = false;
        streamState_ = "stopped";
    }

    auto& manager = DeviceManager::instance();
    if (!saved.has_value() || saved->deviceIndex == std::numeric_limits<size_t>::max()) {
        manager.releaseDeviceLease(DeviceManager::DeviceLeaseOwner::Inmarsat);
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
        config_.save();
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
        config_.save();
        lastStatus_ = restart ? "Changing channel and restarting receiver" : "Channel selected";
    }
    return !restart || start(true);
}

bool InmarsatEngine::start(bool force) {
    if (run_.load(std::memory_order_acquire)) return true;
    if (worker_.joinable()) worker_.join();

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
    if (!tuneAndConfirm(deviceIndex, selected.channelHz, 4000, &error)) {
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
        tunedHz_ = config_.channelHz;
        controlHz_ = config_.channelHz;
        followingVoice_ = false;
        carrierDetected_ = false;
        locked_ = false;
        quality_ = 0.0;
        ebnoDb_ = 0.0;
        rawBlocks_ = 0;
        validatedFrames_ = 0;
        lastStatus_ = "Running on live hardware (experimental physical-layer monitor)";
        QDir().mkpath(QString::fromStdString(config_.recordDir));
        if (voice_) voice_->setRecording(config_.recordVoice, config_.recordDir, 0);
        pipeline_ = {};
        pipelineReport_ = nlohmann::json::object();
        if (iqRx_) {
            iqRx_->deviceIndex = deviceIndex;
            iqRx_->lastConsumedAbsolute.store(0, std::memory_order_release);
        }
    }
    if (iqRx_) manager.setReceiverCursorToLiveEdge(deviceIndex, *iqRx_);
    run_.store(true, std::memory_order_release);
    worker_ = std::thread(&InmarsatEngine::workerLoop, this);
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
        if (voice_) voice_->setRecording(false, {}, 0);
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
    snapshot.recording = config_.recordVoice && voice_ && voice_->recording();
    snapshot.rawBlocks = rawBlocks_;
    snapshot.validatedFrames = validatedFrames_;
    snapshot.messages = messages_;
    snapshot.voiceFrames = voice_ ? voice_->framesDecoded() : voiceFrames_;
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
    json["voiceBackend"] = voice_ && voice_->backendAvailable();
    json["experimental"] = true;
    json["rfQualified"] = false;
    json["note"] = "Physical-layer monitor only — no validated unique-word/FEC or Aero AMBE mapping";
    return json;
}

void InmarsatEngine::onDecodedBytes(const uint8_t* data, size_t count) {
    if (!data || count == 0) return;
    double frequency = 0.0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        frequency = tunedHz_;
    }
    if (acars_) acars_->feedBytes(data, count, frequency);
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

    if (message.hasPosition && message.aesId != 0) {
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

bool InmarsatEngine::processIq() {
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
        if (spectrumDb_.size() > 256) {
            std::vector<float> downsampled(256, -120.0f);
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

    if (centerFrequency <= 0.0) centerFrequency = manager.getCurrentCenterFreq(deviceIndex);
    InmarsatDemodMode mode;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        mode = demodModeLocked();
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
        if (voice_) voiceFrames_ = voice_->framesDecoded();
        returnControl = followingVoice_ && unixNow() > voiceFollowUntilUnix_;
    }
    if (returnControl) returnToControl();
    return true;
}

void InmarsatEngine::workerLoop() {
    bool lostHardware = false;
    bool processingFailed = false;
    InmarsatDiagnostics diagnostics;
    try {
        diagnostics.open({}, "live");
        diagnostics.write("open", {{"state", "running"}});
        std::lock_guard<std::mutex> lock(mutex_);
        diagnosticLog_ = diagnostics.path().toStdString();
    } catch (const std::exception& e) {
        spdlog::warn("Inmarsat diagnostic log: {}", e.what());
    }
    auto nextLog = std::chrono::steady_clock::now();
    auto nextNotify = std::chrono::steady_clock::now();
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
            consumed = processIq();
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
        if (now >= nextLog && !diagnostics.path().isEmpty()) {
            diagnostics.write("progress", pipeline_.report());
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

    if (!diagnostics.path().isEmpty()) {
        auto report = pipeline_.report();
        report["state"] = processingFailed ? "error" : lostHardware ? "hardware_lost" : "stopped";
        report["errorCode"] = processingFailed ? "invalid_input" : lostHardware ? "hardware_lost" : "none";
        diagnostics.write("summary", report, true);
    }

    if (lostHardware || processingFailed) {
        if (voice_) voice_->setRecording(false, {}, 0);
        restorePreviousDeviceState();
        notify();
    }
}
