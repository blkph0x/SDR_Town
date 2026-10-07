#include "SatcomScannerEngine.h"
#include "WorkflowSessionId.h"
#include <QPointer>
#include <QSaveFile>
#include <map>
#include <QUuid>
#include "AudioEngine.h"
#include "SatcomHostServices.h"
#include "Ax25AprsDecoder.h"
#include "AptImageDecoder.h"
#include "DeviceManager.h"
#include "Demod.h"
#include "SatPassPlanner.h"
#include "SstvLiveSession.h"
#include "SstvReceiverFeed.h"
#include "SdrDeviceCandidate.h"
#include "SatcomSignalSelection.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <fstream>
#include <limits>
#include <utility>
#include <spdlog/spdlog.h>

namespace {

std::mutex enginesMutex;
std::map<std::string, SatcomScannerEngine*> engines;

std::string configPath(const std::string& sessionId) {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(base);
    const auto id = normalizedWorkflowSessionId(sessionId);
    return (base + (id.empty() ? "/satcom_scanner.json" : "/satcom_scanner_" + QString::fromStdString(id) + ".json")).toStdString();
}

DemodMode modeFromString(const std::string& mode) {
    if (mode == "WFM") return DemodMode::WFM;
    if (mode == "AM") return DemodMode::AM;
    if (mode == "APT") return DemodMode::NFM;
    if (mode == "USB") return DemodMode::USB;
    if (mode == "LSB") return DemodMode::LSB;
    return DemodMode::NFM;
}

bool isSstvSidebandMode(const std::string& mode) {
    return mode == "USB" || mode == "LSB";
}

std::string safeToken(std::string value) {
    for (char& c : value) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (!std::isalnum(u) && c != '-' && c != '_') c = '_';
    }
    if (value.empty()) value = "unknown";
    return value;
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

SatcomScannerConfig SatcomScannerConfig::defaults() {
    SatcomScannerConfig config;
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    config.recordDir = (base + "/satcom_recordings").toStdString();
    config.logDir = (base + "/satcom_logs").toStdString();
    config.presets = {
        {"UHF Scan 420-430", 420e6, 430e6, 12.5e3, 250e3, "NFM", -90.0},
        {"NOAA APT 137.100", 137.05e6, 137.15e6, 5e3, 40e3, "APT", -95.0},
        {"NOAA APT 137.9125", 137.85e6, 137.95e6, 5e3, 40e3, "APT", -95.0},
        {"ISS FM 145.800", 145.75e6, 145.85e6, 5e3, 15e3, "APRS", -92.0},
        {"Amateur VHF 145-146", 145e6, 146e6, 5e3, 15e3, "NFM", -92.0},
    };
    return config;
}

nlohmann::json SatcomScannerConfig::toJson() const {
    nlohmann::json json;
    json["lowHz"] = lowHz;
    json["highHz"] = highHz;
    json["stepHz"] = stepHz;
    json["dwellMs"] = dwellMs;
    json["bandwidthHz"] = bandwidthHz;
    json["mode"] = mode;
    json["squelchDb"] = squelchDb;
    json["deviceIndex"] = deviceIndex;
    json["deviceStableKey"] = deviceStableKey;
    json["recordDir"] = recordDir;
    json["logDir"] = logDir;
    json["enableAx25"] = enableAx25;
    json["enableApt"] = enableApt;
    json["autoCapture"] = autoCapture;
    json["monitorAudio"] = monitorAudio;
    nlohmann::json entries = nlohmann::json::array();
    for (const auto& preset : presets) {
        entries.push_back({
            {"name", preset.name}, {"lowHz", preset.lowHz}, {"highHz", preset.highHz},
            {"stepHz", preset.stepHz}, {"bandwidthHz", preset.bandwidthHz},
            {"mode", preset.mode}, {"squelchDb", preset.squelchDb}
        });
    }
    json["presets"] = entries;
    return json;
}

SatcomScannerConfig SatcomScannerConfig::fromJson(const nlohmann::json& json) {
    SatcomScannerConfig config = defaults();
    if (!json.is_object()) return config;
    config.lowHz = json.value("lowHz", config.lowHz);
    config.highHz = json.value("highHz", config.highHz);
    config.stepHz = json.value("stepHz", config.stepHz);
    config.dwellMs = json.value("dwellMs", config.dwellMs);
    config.bandwidthHz = json.value("bandwidthHz", config.bandwidthHz);
    config.mode = json.value("mode", config.mode);
    config.squelchDb = json.value("squelchDb", config.squelchDb);
    config.deviceIndex = json.value("deviceIndex", config.deviceIndex);
    config.deviceStableKey = json.value("deviceStableKey", config.deviceStableKey);
    config.recordDir = json.value("recordDir", config.recordDir);
    config.logDir = json.value("logDir", config.logDir);
    config.enableAx25 = json.value("enableAx25", config.enableAx25);
    config.enableApt = json.value("enableApt", config.enableApt);
    config.autoCapture = json.value("autoCapture", config.autoCapture);
    config.monitorAudio = json.value("monitorAudio", config.monitorAudio);
    if (json.contains("presets") && json["presets"].is_array()) {
        config.presets.clear();
        for (const auto& entry : json["presets"]) {
            SatcomPreset preset;
            preset.name = entry.value("name", "preset");
            preset.lowHz = entry.value("lowHz", 420e6);
            preset.highHz = entry.value("highHz", 430e6);
            preset.stepHz = entry.value("stepHz", 12.5e3);
            preset.bandwidthHz = entry.value("bandwidthHz", 250e3);
            preset.mode = entry.value("mode", "NFM");
            preset.squelchDb = entry.value("squelchDb", -90.0);
            config.presets.push_back(preset);
        }
    }
    return config;
}

void SatcomScannerConfig::load(const std::string& sessionId) {
    try {
        std::ifstream in(configPath(sessionId));
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

void SatcomScannerConfig::save(const std::string& sessionId) const {
    QSaveFile file(QString::fromStdString(configPath(sessionId)));
    const auto text = toJson().dump(2);
    if (!file.open(QIODevice::WriteOnly) || file.write(text.data(), text.size()) != qint64(text.size()) || !file.commit())
        spdlog::warn("Satcom session {} settings save failed", sessionId);
}

SatcomScannerEngine& SatcomScannerEngine::instance() {
    static SatcomScannerEngine engine;
    return engine;
}

SatcomScannerEngine::SatcomScannerEngine(const std::string& sessionId)
    : sessionId_(normalizedWorkflowSessionId(sessionId)) {
    config_.load(sessionId_);
    // DEC-0205: never inherit saved autoCapture. The default hub engine used
    // to load autoCapture=true and seize Listen at GUI open (SO-50 436.795 MHz)
    // before the operator could retune. Named sessions already required a fresh
    // arm this run; the default engine now matches. The checkbox still enables
    // it for the rest of the session and still saves the preference.
    config_.autoCapture = false;
    if (!sessionId_.empty()) {
        planner_ = std::make_unique<SatPassPlanner>(sessionId_);
        config_.logDir = SatcomScannerConfig::defaults().logDir + "/" + sessionId_;
    }
    ax25_ = std::make_unique<Ax25AprsDecoder>();
    apt_ = std::make_unique<AptImageDecoder>();
    demod_ = std::make_unique<Demodulator>();
    sstvFeed_ = std::make_shared<SstvReceiverFeed>();
    log_.setLogDirectory(config_.logDir);
    log_.start();
    std::lock_guard lock(enginesMutex);
    if (!engines.emplace(sessionId_, this).second)
        throw std::invalid_argument("Satcom session is already open");
}

SatcomScannerEngine::~SatcomScannerEngine() {
    setUpdateCallback({});
    stop();
    finishSstvCapture(false);
    shutdownAudioOutput();
    log_.stop();
    std::lock_guard lock(enginesMutex);
    engines.erase(sessionId_);
}

SatPassPlanner& SatcomScannerEngine::planner() const {
    return planner_ ? *planner_ : SatPassPlanner::instance();
}

void SatcomScannerEngine::stopAll() {
    std::vector<QPointer<SatcomScannerEngine>> active;
    { std::lock_guard lock(enginesMutex); for (const auto& [id, engine] : engines) active.emplace_back(engine); }
    for (const auto& engine : active) if (engine) {
        { std::lock_guard lock(engine->mutex_); engine->autoCaptureSuspended_ = true; }
        engine->stop();
    }
}

void SatcomScannerEngine::setConfig(const SatcomScannerConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (deviceLease_ && (config.deviceStableKey != config_.deviceStableKey || config.deviceIndex != config_.deviceIndex)) {
        lastStatus_ = "Stop Satcom before changing its radio";
        return;
    }
    config_ = config;
    config_.save(sessionId_);
    log_.setLogDirectory(config_.logDir);
}

SatcomScannerConfig SatcomScannerEngine::config() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

void SatcomScannerEngine::setAutoCaptureEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_.autoCapture = enabled;
    autoCaptureSuspended_ = false;
    config_.save(sessionId_);
}

bool SatcomScannerEngine::autoCaptureEnabled() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_.autoCapture && !autoCaptureSuspended_;
}

size_t SatcomScannerEngine::resolveDeviceIndex(std::string* error) {
    auto& manager = DeviceManager::instance();
    const auto devices = manager.getDevices();
    if (devices.empty()) {
        if (error) *error = "No SDR devices are available; rescan devices first";
        return static_cast<size_t>(-1);
    }

    SatcomScannerConfig selected = config();
    if (!sessionId_.empty() && selected.deviceStableKey.empty()) {
        if (error) *error = "Select a radio for this Satcom session";
        return size_t(-1);
    }
    const auto active = leaseToken();
    if (active && manager.ownsDevice(active)) return active.index;
    const size_t chosen = manager.resolveWorkflowDevice(DeviceOwnership::Owner::Satcom,
        selected.deviceStableKey, selected.deviceIndex, error);
    if (chosen == size_t(-1)) return chosen;
    if (chosen >= devices.size()) { if (error) *error = "Radio list changed; select the radio again"; return size_t(-1); }

    const std::string stableKey = devices[chosen].stableKey;
    if (selected.deviceIndex != chosen || selected.deviceStableKey != stableKey) {
        selected.deviceIndex = chosen;
        selected.deviceStableKey = stableKey;
        setConfig(selected);
    }
    return chosen;
}

void SatcomScannerEngine::setUpdateCallback(std::function<void()> callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    updateCb_ = std::move(callback);
}

void SatcomScannerEngine::notifyUpdate() {
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

void SatcomScannerEngine::pushLog(SatcomLog::EventType type, double frequencyHz,
                                  const char* text) {
    log_.tryPush(type, frequencyHz, text);
}

std::string SatcomScannerEngine::makeCaptureStem(const std::string& satId,
                                                 const std::string& downlinkId) const {
    const std::string timestamp = QDateTime::currentDateTimeUtc()
                                      .toString("yyyyMMdd_HHmmss_zzz")
                                      .toStdString();
    return timestamp + "_" + safeToken(sessionId_) + "_" + safeToken(satId) + "_" + safeToken(downlinkId) +
        "_" + QUuid::createUuid().toString(QUuid::Id128).toStdString();
}

void SatcomScannerEngine::capturePreviousDeviceState(size_t deviceIndex) {
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

DeviceOwnership::Token SatcomScannerEngine::leaseToken() const {
    std::lock_guard lock(mutex_);
    return deviceLease_;
}

void SatcomScannerEngine::restorePreviousDeviceState() {
    std::optional<PreviousDeviceState> saved;
    DeviceOwnership::Token token;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        saved = previousDeviceState_;
        previousDeviceState_.reset();
        token = deviceLease_;
        deviceLease_ = {};
        activeDeviceIndex_ = static_cast<size_t>(-1);
    }

    auto& manager = DeviceManager::instance();
    const bool restored = saved && token && manager.restoreDevice(token,
        saved->wasStreaming, saved->wasEnabled, saved->centerHz);
    std::lock_guard lock(mutex_);
    if (hostLease_ == token) hostRestoreAllowed_ = restored;
}

bool SatcomScannerEngine::beginHostTakeover(size_t deviceIndex, std::string* error) {
    auto& host = SatcomHostServices::instance();
    if (!host.installed()) {
        if (error) error->clear();
        return true;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (hostTakeoverActive_) {
            if (error) error->clear();
            return true;
        }
    }

    std::string hostError;
    const auto token = leaseToken();
    if (token.index != deviceIndex || !host.beginReceiverTakeover(token, &hostError)) {
        const std::string message = hostError.empty()
            ? "SDR Town could not park the selected Listen receiver"
            : hostError;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            lastStatus_ = message;
        }
        if (error) *error = message;
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        hostTakeoverActive_ = true;
        hostLease_ = token;
        hostRestoreAllowed_ = false;
    }
    host.publishStatus("Satcom owns the selected receiver");
    if (error) error->clear();
    return true;
}

void SatcomScannerEngine::endHostTakeover() {
    bool active = false;
    bool restore = false;
    DeviceOwnership::Token token;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        active = hostTakeoverActive_;
        hostTakeoverActive_ = false;
        token = hostLease_;
        hostLease_ = {};
        restore = hostRestoreAllowed_;
        hostRestoreAllowed_ = false;
    }
    if (!active) return;
    SatcomHostServices::instance().endReceiverTakeover(token, restore);
    SatcomHostServices::instance().publishStatus("Satcom receiver released");
}

bool SatcomScannerEngine::prepareReceiverForSatcom(size_t deviceIndex, bool force,
                                                   std::string* error) {
    auto& manager = DeviceManager::instance();
    const auto owner = manager.deviceLeaseOwner(deviceIndex);
    if (owner == DeviceManager::DeviceLeaseOwner::P25) {
        const std::string message =
            "P25 owns the receiver; Satcom will not interrupt it. Select another device.";
        {
            std::lock_guard<std::mutex> lock(mutex_);
            lastStatus_ = message;
        }
        if (error) *error = message;
        return false;
    }

    capturePreviousDeviceState(deviceIndex);
    std::string leaseError;
    const auto token = manager.claimDevice(deviceIndex, DeviceManager::DeviceLeaseOwner::Satcom,
        QUuid::createUuid().toString(QUuid::Id128).toStdString(), &leaseError, config().deviceStableKey, force);
    if (!token) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            previousDeviceState_.reset();
            lastStatus_ = leaseError;
        }
        if (error) *error = leaseError;
        return false;
    }

    { std::lock_guard lock(mutex_); deviceLease_ = token; }
    // Park only this radio's receivers before starting hardware or changing RF.
    if (!beginHostTakeover(deviceIndex, error)) {
        manager.releaseDevice(token);
        std::lock_guard lock(mutex_);
        deviceLease_ = {};
        previousDeviceState_.reset();
        return false;
    }
    bool wasStreaming = false;
    std::string existingState;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        wasStreaming = previousDeviceState_.has_value() && previousDeviceState_->wasStreaming;
        existingState = manager.getRuntimeStateLabel(deviceIndex);
        lastStatus_ = wasStreaming && existingState == "live hardware"
            ? "Taking over active Listen receiver without reopening hardware"
            : "Starting selected Satcom receiver";
    }

    // Do not stop a healthy live stream during takeover. DeviceManager starts a
    // temporary stub before its asynchronous Soapy open; recycling a working
    // receiver here caused Start/Arm to fall back to "opening hardware (stub active)".
    // Reuse is allowed only for confirmed live hardware; an opening/stub stream
    // is not restarted underneath another asynchronous opener.
    if (!manager.startDevice(token, &leaseError, true)) {
        const std::string message = "Could not start selected Satcom receiver";
        {
            std::lock_guard<std::mutex> lock(mutex_);
            lastStatus_ = message;
            streamState_ = manager.getRuntimeStateLabel(deviceIndex);
        }
        restorePreviousDeviceState();
        if (error) *error = message;
        endHostTakeover();
        return false;
    }

    std::string streamError;
    if (!waitForOperationalStream(deviceIndex, 10000, &streamError)) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            lastStatus_ = streamError;
            deviceConnected_ = false;
        }
        restorePreviousDeviceState();
        if (error) *error = streamError;
        endHostTakeover();
        return false;
    }

    const auto devices = manager.getDevices();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        activeDeviceIndex_ = deviceIndex;
        deviceLabel_ = deviceIndex < devices.size() ? devices[deviceIndex].label : "receiver";
        deviceConnected_ = true;
        streamState_ = "live hardware";
    }
    if (!beginHostTakeover(deviceIndex, error)) {
        restorePreviousDeviceState();
        return false;
    }
    if (error) error->clear();
    return true;
}

bool SatcomScannerEngine::tuneAndConfirm(size_t deviceIndex, double frequencyHz,
                                        int timeoutMs, std::string* error) {
    auto& manager = DeviceManager::instance();
    std::string tuneError;
    const auto token = leaseToken();
    if (token.index != deviceIndex || !manager.tuneDevice(token, frequencyHz, &tuneError)) {
        if (error) *error = tuneError.empty() ? "Could not tune Satcom receiver" : tuneError;
        return false;
    }

    const uint64_t requested = manager.getCenterTuneRequestSeq(deviceIndex);
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(std::max(250, timeoutMs));
    while (std::chrono::steady_clock::now() < deadline) {
        if (manager.getRuntimeStateLabel(deviceIndex) != "live hardware") {
            if (error) {
                *error = "Satcom receiver left live hardware mode while tuning: " +
                         manager.getRuntimeStateLabel(deviceIndex);
            }
            return false;
        }
        if (!manager.ownsDevice(token)) { if (error) *error = "Satcom radio ownership expired"; return false; }
        if (manager.getCenterTuneAppliedSeq(deviceIndex) >= requested &&
            std::abs(manager.getCurrentCenterFreq(deviceIndex) - frequencyHz) <= 100.0) {
            if (error) error->clear();
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (error) {
        *error = "Satellite receiver did not confirm " +
                 std::to_string(frequencyHz / 1e6) + " MHz";
    }
    return false;
}

void SatcomScannerEngine::resetChronologicalInput() {
    {
        std::lock_guard<std::mutex> lock(iqMutex_);
        iqCursor_.reset();
    }
    sstvSourceEpoch_.fetch_add(1, std::memory_order_acq_rel);
    sstvAudioFirstSample_.store(0, std::memory_order_release);
    demodResetRequested_.store(true, std::memory_order_release);
}

SatcomIqCursor::Result SatcomScannerEngine::pullNewIq(size_t deviceIndex, size_t maxSamples) {
    auto& manager = DeviceManager::instance();
    const auto token = leaseToken();
    if (token.index != deviceIndex || !manager.ownsDevice(token)) return {};
    const auto window = manager.getRecentIQWindowWithCursor(deviceIndex, maxSamples);
    std::lock_guard<std::mutex> lock(iqMutex_);
    return iqCursor_.consume(window.samples,
                             window.startAbsolute,
                             window.endAbsolute,
                             window.streamEpoch,
                             window.cursorDiscontinuity);
}

bool SatcomScannerEngine::waitForOperationalStream(size_t deviceIndex, int timeoutMs,
                                                    std::string* error) {
    auto& manager = DeviceManager::instance();
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(std::max(250, timeoutMs));
    std::string lastState = "starting";

    while (std::chrono::steady_clock::now() < deadline) {
        if (!manager.ownsDevice(leaseToken())) { if (error) *error = "Satcom radio ownership expired"; return false; }
        lastState = manager.getRuntimeStateLabel(deviceIndex);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            streamState_ = lastState;
        }

        if (lastState == "live hardware") {
            const auto probe = manager.getRecentIQWindowWithCursor(deviceIndex, 4096);
            if (!probe.samples.empty() && probe.endAbsolute > probe.startAbsolute) return true;
        }

        // DeviceManager deliberately runs a safe stub while Soapy opens the real
        // device in the background. "opening hardware (stub active)" is therefore
        // a normal transitional state, not a failure. Only terminal states abort.
        if (containsInsensitive(lastState, "hardware failed") ||
            containsInsensitive(lastState, "driver stuck") ||
            lastState == "simulated/stub") {
            if (error) *error = "Receiver did not enter live hardware mode: " + lastState;
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }

    if (error) *error = "Timed out waiting for live hardware IQ; final state: " + lastState;
    return false;
}

bool SatcomScannerEngine::ensureAudioOutput(std::string* error) {
    bool enabled = true;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        enabled = config_.monitorAudio;
        if (!enabled) audioMonitoring_ = false;
    }
    if (!enabled) return true;

    std::lock_guard<std::mutex> lock(audioMutex_);
    if (audio_ && audio_->activeOutputCount() > 0) {
        std::lock_guard<std::mutex> stateLock(mutex_);
        audioMonitoring_ = true;
        return true;
    }

    std::string hostError;
    if (AudioEngine* shared = sessionId_.empty() ?
            SatcomHostServices::instance().acquireAudioEngine(&hostError) : nullptr) {
        if (shared->activeOutputCount() == 0) {
            if (error) *error = hostError.empty()
                ? "Configured SDR Town playback output is unavailable"
                : hostError;
            std::lock_guard<std::mutex> stateLock(mutex_);
            audioMonitoring_ = false;
            return false;
        }
        fallbackAudio_.reset();
        audio_ = shared;
        usingSharedAudio_ = true;
        {
            std::lock_guard<std::mutex> stateLock(mutex_);
            audioMonitoring_ = true;
            lastStatus_ = "Satellite audio routed through SDR Town";
        }
        if (error) error->clear();
        return true;
    }

    // DEC-0189: named sessions own separate PCM queues, never append to P25's
    // primary output. The OS mixer combines explicitly enabled session outputs.
    if (sessionId_.empty() && SatcomHostServices::instance().installed()) {
        if (error) *error = hostError.empty()
            ? "SDR Town playback output could not be activated"
            : hostError;
        std::lock_guard<std::mutex> stateLock(mutex_);
        audioMonitoring_ = false;
        return false;
    }

    try {
        auto candidate = std::make_unique<AudioEngine>();
        const auto outputs = candidate->enumeratePlaybackDevices();
        if (outputs.empty()) {
            if (error) *error = "No playback device is available for satellite audio";
            std::lock_guard<std::mutex> stateLock(mutex_);
            audioMonitoring_ = false;
            return false;
        }
        size_t selected = 0;
        for (size_t i = 0; i < outputs.size(); ++i) {
            if (outputs[i].isDefault) {
                selected = i;
                break;
            }
        }
        candidate->setActiveOutputs({selected});
        if (candidate->activeOutputCount() == 0) {
            if (error) *error = "Default playback device could not be opened for satellite audio";
            std::lock_guard<std::mutex> stateLock(mutex_);
            audioMonitoring_ = false;
            return false;
        }
        fallbackAudio_ = std::move(candidate);
        audio_ = fallbackAudio_.get();
        usingSharedAudio_ = false;
        std::lock_guard<std::mutex> stateLock(mutex_);
        audioMonitoring_ = true;
        return true;
    } catch (const std::exception& ex) {
        if (error) *error = std::string("Satellite audio output: ") + ex.what();
    } catch (...) {
        if (error) *error = "Satellite audio output could not be initialized";
    }
    std::lock_guard<std::mutex> stateLock(mutex_);
    audioMonitoring_ = false;
    return false;
}

void SatcomScannerEngine::pushMonitorAudio(const float* samples, size_t count) {
    if (!samples || count == 0) return;
    std::lock_guard<std::mutex> lock(audioMutex_);
    if (!audio_) return;
    // The MainWindow engine can also be carrying Listen audio from a second SDR.
    // Trimming that shared queue would discard another receiver's PCM. Keep the
    // Satcom-only latency clamp only for the standalone fallback engine.
    if (!usingSharedAudio_.load(std::memory_order_acquire))
        audio_->trimQueuedAudio(12000);
    audio_->pushAudio(samples, count);
}

void SatcomScannerEngine::shutdownAudioOutput() {
    std::unique_ptr<AudioEngine> oldFallback;
    {
        std::lock_guard<std::mutex> lock(audioMutex_);
        audio_ = nullptr;
        usingSharedAudio_ = false;
        oldFallback = std::move(fallbackAudio_);
    }
    if (oldFallback) oldFallback->clearBuffers();
    std::lock_guard<std::mutex> stateLock(mutex_);
    audioMonitoring_ = false;
}

bool SatcomScannerEngine::start(bool force) {
    std::string deviceError;
    const size_t selectedDevice = resolveDeviceIndex(&deviceError);
    if (selectedDevice == static_cast<size_t>(-1)) {
        std::lock_guard<std::mutex> lock(mutex_);
        lastStatus_ = deviceError.empty() ? "No receiver selected" : deviceError;
        return false;
    }

    if (run_.load(std::memory_order_acquire)) {
        size_t activeDevice = static_cast<size_t>(-1);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            activeDevice = activeDeviceIndex_;
        }
        if (activeDevice == selectedDevice) return true;
        stop();
    }

    // A failed worker can have queued its host cleanup; complete that old
    // lease before opening a replacement session, even before Qt dispatches it.
    if (worker_.joinable()) stop();
    if (!prepareReceiverForSatcom(selectedDevice, force, &deviceError)) return false;

    double firstFrequency = 0.0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        firstFrequency = config_.lowHz;
    }
    if (!tuneAndConfirm(selectedDevice, firstFrequency, 3000, &deviceError)) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            lastStatus_ = deviceError;
        }
        restorePreviousDeviceState();
        endHostTakeover();
        return false;
    }

    std::string audioError;
    if (!ensureAudioOutput(&audioError) && !audioError.empty()) spdlog::warn("{}", audioError);

    run_.store(true, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = passTrackActive_ ? SatcomScannerState::Locked : SatcomScannerState::Scanning;
        currentHz_ = passTrackActive_ && lockHz_ > 0.0 ? lockHz_ : firstFrequency;
        if (!passTrackActive_) lockHz_ = 0.0;
        skipRequested_ = false;
        deviceConnected_ = true;
        streamState_ = "live hardware";
        lastStatus_ = passTrackActive_ ? "Pass armed - live hardware" : "Scanning - live hardware";
    }
    iqDiscontinuities_.store(0, std::memory_order_release);
    resetChronologicalInput();
    pushLog(SatcomLog::EventType::Start, firstFrequency, "scan start live hardware");
    spdlog::info("Satcom session={} device={} key={} lease={} started frequencyHz={}",
        sessionId_, selectedDevice, config().deviceStableKey, leaseToken().id, firstFrequency);
    worker_ = std::thread(&SatcomScannerEngine::workerLoop, this);
    notifyUpdate();
    return true;
}

void SatcomScannerEngine::stop() {
    run_.store(false, std::memory_order_release);
    if (worker_.joinable()) worker_.join();
    if (planner_) planner_->disarm();
    finishSstvCapture(false);
    shutdownAudioOutput();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = SatcomScannerState::Idle;
        recording_ = false;
        recordRequested_ = false;
        passTrackActive_ = false;
        passStartedEngine_ = false;
        deviceConnected_ = false;
        streamState_ = "stopped";
        lastStatus_ = "Stopped";
    }
    resetChronologicalInput();
    restorePreviousDeviceState();
    endHostTakeover();
    pushLog(SatcomLog::EventType::Stop, currentHz_, "scan stop");
    notifyUpdate();
}

void SatcomScannerEngine::skip() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        skipRequested_ = true;
    }
    resetChronologicalInput();
    pushLog(SatcomLog::EventType::Skip, lockHz_ > 0 ? lockHz_ : currentHz_, "skip");
}

bool SatcomScannerEngine::startRecording() {
    std::string path;
    double frequencyHz = 0.0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ != SatcomScannerState::Locked && state_ != SatcomScannerState::Recording)
            return false;
        if (recording_) return true;
        if (recordPath_.empty()) {
            const std::string sat = recordSatId_.empty() ? "scan" : recordSatId_;
            const std::string downlink = recordDownlinkId_.empty() ? "activity" : recordDownlinkId_;
            QDir().mkpath(QString::fromStdString(config_.recordDir));
            recordPath_ = config_.recordDir + "/" + makeCaptureStem(sat, downlink) + ".f32";
        }
        recordRequested_ = true;
        recording_ = true;
        recordHz_ = lockHz_;
        state_ = SatcomScannerState::Recording;
        path = recordPath_;
        frequencyHz = recordHz_;
        lastStatus_ = "Recording " + path;
    }
    QDir().mkpath(QFileInfo(QString::fromStdString(path)).absolutePath());
    std::ofstream create(path, std::ios::binary | std::ios::trunc);
    if (!create) {
        std::lock_guard<std::mutex> lock(mutex_);
        recording_ = false;
        recordRequested_ = false;
        if (state_ == SatcomScannerState::Recording) state_ = SatcomScannerState::Locked;
        lastStatus_ = "Could not create recording: " + path;
        return false;
    }
    pushLog(SatcomLog::EventType::RecordStart, frequencyHz, "record");
    return true;
}

void SatcomScannerEngine::writeRecordingMetadata(const std::string& path) const {
    if (path.empty() || !QFileInfo::exists(QString::fromStdString(path))) return;
    std::string satId;
    std::string downlinkId;
    std::string role;
    std::string mode;
    double frequencyHz = 0.0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        satId = recordSatId_;
        downlinkId = recordDownlinkId_;
        role = armedRole_;
        mode = config_.mode;
        frequencyHz = recordHz_;
    }
    const QFileInfo info(QString::fromStdString(path));
    const nlohmann::json metadata = {
        {"sessionId", sessionId_},
        {"format", "float32-le-mono"},
        {"sampleRate", 48000},
        {"bytes", info.size()},
        {"frequencyHz", frequencyHz},
        {"satId", satId},
        {"downlinkId", downlinkId},
        {"role", role},
        {"mode", mode},
        {"iqDiscontinuities", iqDiscontinuities_.load(std::memory_order_acquire)},
        {"capturedUtc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toStdString()},
    };
    try {
        std::ofstream out(path + ".json", std::ios::trunc);
        if (out) out << metadata.dump(2);
    } catch (...) {
    }
}

void SatcomScannerEngine::stopRecording() {
    std::string path;
    double frequencyHz = 0.0;
    bool wasRecording = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        wasRecording = recording_;
        recording_ = false;
        recordRequested_ = false;
        path = recordPath_;
        frequencyHz = recordHz_;
        if (state_ == SatcomScannerState::Recording) state_ = SatcomScannerState::Locked;
    }
    if (wasRecording) {
        writeRecordingMetadata(path);
        pushLog(SatcomLog::EventType::RecordStop, frequencyHz, "record stop");
    }
}

bool SatcomScannerEngine::applyPreset(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& preset : config_.presets) {
        if (preset.name == name) {
            config_.lowHz = preset.lowHz;
            config_.highHz = preset.highHz;
            config_.stepHz = preset.stepHz;
            config_.bandwidthHz = preset.bandwidthHz;
            config_.mode = preset.mode;
            config_.squelchDb = preset.squelchDb;
            config_.save(sessionId_);
            return true;
        }
    }
    return false;
}

std::string SatcomScannerEngine::stateName() const {
    std::lock_guard<std::mutex> lock(mutex_);
    switch (state_) {
    case SatcomScannerState::Scanning: return "scanning";
    case SatcomScannerState::Locked: return "locked";
    case SatcomScannerState::Recording: return "recording";
    case SatcomScannerState::Idle:
    default: return "idle";
    }
}

SatcomScannerSnapshot SatcomScannerEngine::snapshot() const {
    SatcomScannerSnapshot snapshot;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot.state = state_;
        snapshot.config = config_;
        snapshot.currentHz = currentHz_;
        snapshot.lockHz = lockHz_;
        snapshot.recordHz = recordHz_;
        snapshot.audioRmsDb = audioRmsDb_;
        snapshot.deviceLabel = deviceLabel_;
        snapshot.deviceConnected = deviceConnected_;
        snapshot.streamState = streamState_;
        snapshot.audioMonitoring = audioMonitoring_;
        snapshot.sharedMainAudio = usingSharedAudio_.load(std::memory_order_acquire);
        snapshot.hostTakeoverActive = hostTakeoverActive_;
        snapshot.spectrumDb = spectrumDb_;
        snapshot.spectrumCenterHz = spectrumCenterHz_;
        snapshot.spectrumRateHz = spectrumRateHz_;
        snapshot.recentDecodes = recentDecodes_;
        snapshot.aptPreviewPath = aptPreviewPath_;
        snapshot.recordPath = recordPath_;
        snapshot.sstvOutputDir = sstvOutputDir_;
        snapshot.logWritten = log_.eventsWritten();
        snapshot.logDropped = log_.eventsDropped();
        snapshot.lastStatus = lastStatus_;
        snapshot.armedRole = armedRole_;
        snapshot.activeDeviceIndex = activeDeviceIndex_;
    }
    snapshot.iqDiscontinuities = iqDiscontinuities_.load(std::memory_order_acquire);
    const auto armed = planner().snapshot().armed;
    snapshot.passArmed = armed.armed;
    snapshot.autoTrack = armed.autoTrack;
    snapshot.dopplerHz = armed.dopplerHz;
    snapshot.tunedHz = armed.tunedHz;
    if (!armed.role.empty()) snapshot.armedRole = armed.role;
    return snapshot;
}

void SatcomScannerEngine::startSstvCapture(const std::string& satId,
                                           const std::string& downlinkId) {
    finishSstvCapture(false);
    const QString parent = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
                           "/satcom_sstv";
    QDir().mkpath(parent);
    const QString output = QDir(parent).filePath(
        QString::fromStdString(makeCaptureStem(satId, downlinkId)));
    {
        std::lock_guard<std::mutex> lock(mutex_);
        sstvOutputDir_ = output.toStdString();
    }
    sstvFinish_ = false;
    sstvCancel_ = false;
    const auto feed = sstvFeed_;
    std::lock_guard<std::mutex> lock(sstvMutex_);
    sstvThread_ = std::thread([this, feed, output]() {
        try {
            const auto report = decodeSstvLive(
                feed,
                []() {},
                output,
                "auto",
                [this]() { return sstvFinish_.load(std::memory_order_acquire); },
                [this]() { return sstvCancel_.load(std::memory_order_acquire); },
                {});
            const size_t images = report.value("images", nlohmann::json::array()).size();
            const std::string message = "SSTV session saved: " + output.toStdString() +
                                        " (" + std::to_string(images) + " image(s))";
            {
                std::lock_guard<std::mutex> stateLock(mutex_);
                recentDecodes_.push_back(message);
                if (recentDecodes_.size() > 100) recentDecodes_.erase(recentDecodes_.begin());
                lastStatus_ = message;
            }
            notifyUpdate();
        } catch (const std::exception& ex) {
            if (sstvCancel_.load(std::memory_order_acquire)) return;
            {
                std::lock_guard<std::mutex> stateLock(mutex_);
                lastStatus_ = std::string("SSTV capture: ") + ex.what();
                recentDecodes_.push_back(lastStatus_);
                if (recentDecodes_.size() > 100) recentDecodes_.erase(recentDecodes_.begin());
            }
            notifyUpdate();
        }
    });
}

void SatcomScannerEngine::finishSstvCapture(bool cancel) {
    std::thread pending;
    {
        std::lock_guard<std::mutex> lock(sstvMutex_);
        if (!sstvThread_.joinable()) return;
        if (cancel) sstvCancel_ = true;
        else sstvFinish_ = true;
        pending = std::move(sstvThread_);
    }
    if (pending.joinable()) pending.join();
}

bool SatcomScannerEngine::armPass(const std::string& satId, const std::string& downlinkId,
                                  bool autoTrack, bool force, std::string* error) {
    finishSstvCapture(false);
    std::string failure;
    const size_t selectedDevice = resolveDeviceIndex(&failure);
    if (selectedDevice == static_cast<size_t>(-1)) {
        if (error) *error = failure.empty() ? "No receiver selected" : failure;
        return false;
    }

    bool engineWasRunning = run_.load(std::memory_order_acquire);
    if (engineWasRunning) {
        size_t activeDevice = static_cast<size_t>(-1);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            activeDevice = activeDeviceIndex_;
        }
        if (activeDevice != selectedDevice) {
            stop();
            engineWasRunning = false;
        }
    }

    if (!planner().arm(satId, downlinkId, autoTrack, &failure)) {
        if (error) *error = failure;
        return false;
    }

    if (!engineWasRunning && !prepareReceiverForSatcom(selectedDevice, force, &failure)) {
        planner().disarm();
        if (error) *error = failure;
        return false;
    }

    const auto plan = planner().snapshot();
    const std::string stem = makeCaptureStem(satId, plan.armed.downlinkId);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        passTrackActive_ = true;
        passStartedEngine_ = !engineWasRunning;
        armedRole_ = plan.armed.role;
        config_.mode = plan.armed.mode.empty() ? "NFM" : plan.armed.mode;
        if (config_.mode == "sstv") config_.mode = "NFM";
        if (plan.armed.role == "apt") {
            config_.mode = "APT";
            config_.bandwidthHz = 40e3;
        } else if (plan.armed.role == "sstv") {
            // HfDemod's BW is twice the SSB audio cutoff (DEC-0117).
            config_.bandwidthHz = isSstvSidebandMode(config_.mode) ? 6e3 : 15e3;
        } else if (plan.armed.role == "aprs" || plan.armed.role == "voice") {
            config_.bandwidthHz = 15e3;
        } else {
            config_.bandwidthHz = std::min(config_.bandwidthHz, 25e3);
        }
        currentHz_ = plan.armed.freqHz;
        lockHz_ = plan.armed.freqHz;
        passNominalHz_ = plan.armed.freqHz;
        lastTrackHz_ = 0.0;
        recordSatId_ = satId;
        recordDownlinkId_ = plan.armed.downlinkId;
        QDir().mkpath(QString::fromStdString(config_.recordDir));
        recordPath_ = config_.recordDir + "/" + stem + ".f32";
        aptPreviewPath_.clear();
        if (plan.armed.role == "apt") {
            const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
                                 "/satcom_apt";
            QDir().mkpath(base);
            aptPreviewPath_ = QDir(base).filePath(QString::fromStdString(stem) + ".pgm").toStdString();
        }
        sstvOutputDir_.clear();
        lastStatus_ = "Preparing pass receiver";
        config_.save(sessionId_);
    }
    resetChronologicalInput();

    if (!tuneAndConfirm(selectedDevice, plan.armed.freqHz, 3000, &failure)) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            passTrackActive_ = false;
            passStartedEngine_ = false;
            armedRole_.clear();
            state_ = engineWasRunning ? SatcomScannerState::Scanning : SatcomScannerState::Idle;
            lastStatus_ = failure;
        }
        planner().disarm();
        if (!engineWasRunning) {
            restorePreviousDeviceState();
            endHostTakeover();
        }
        if (error) *error = failure;
        return false;
    }

    std::string audioError;
    if (!ensureAudioOutput(&audioError) && !audioError.empty()) spdlog::warn("{}", audioError);

    resetChronologicalInput();
    if (!engineWasRunning) {
        if (worker_.joinable()) worker_.join();
        run_.store(true, std::memory_order_release);
        iqDiscontinuities_.store(0, std::memory_order_release);
        worker_ = std::thread(&SatcomScannerEngine::workerLoop, this);
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = SatcomScannerState::Locked;
        deviceConnected_ = true;
        streamState_ = "live hardware";
        lastStatus_ = "Pass armed - tune confirmed (" + config_.mode + ")";
    }

    pushLog(SatcomLog::EventType::Lock, plan.armed.freqHz, "pass arm tune confirmed");
    tickPassTrack();
    if (plan.armed.role == "sstv") startSstvCapture(satId, plan.armed.downlinkId);
    notifyUpdate();
    if (error) error->clear();
    return true;
}

void SatcomScannerEngine::disarmPass() {
    stopRecording();
    finishSstvCapture(false);
    planner().disarm();
    bool stopPassOnly = false;
    const bool running = run_.load(std::memory_order_acquire);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopPassOnly = passStartedEngine_;
        passStartedEngine_ = false;
        passTrackActive_ = false;
        armedRole_.clear();
        lockHz_ = 0.0;
        state_ = running ? SatcomScannerState::Scanning : SatcomScannerState::Idle;
        lastStatus_ = running ? "Pass disarmed - scanning" : "Pass disarmed";
    }
    resetChronologicalInput();
    if (stopPassOnly) {
        stop();
        return;
    }
    if (!running) {
        shutdownAudioOutput();
        restorePreviousDeviceState();
        endHostTakeover();
    }
    notifyUpdate();
}

void SatcomScannerEngine::setAutoTrack(bool enabled) {
    planner().setAutoTrack(enabled);
}

void SatcomScannerEngine::tickPassTrack() {
    double tunedHz = 0.0;
    if (!planner().tickAutoTrack(&tunedHz)) {
        const bool stillArmed = planner().snapshot().armed.armed;
        bool ended = false;
        bool stopPassOnly = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!stillArmed && passTrackActive_) {
                passTrackActive_ = false;
                ended = true;
                stopPassOnly = passStartedEngine_;
                passStartedEngine_ = false;
                lockHz_ = 0.0;
                state_ = stopPassOnly
                    ? SatcomScannerState::Idle
                    : (run_.load(std::memory_order_acquire)
                        ? SatcomScannerState::Scanning
                        : SatcomScannerState::Idle);
                lastStatus_ = stopPassOnly
                    ? "Pass ended - receiver restored"
                    : "Pass ended - scanning";
            }
        }
        if (ended) {
            stopRecording();
            finishSstvCapture(false);
            resetChronologicalInput();
            if (stopPassOnly) {
                run_.store(false, std::memory_order_release);
                const auto endedLease = leaseToken();
                QMetaObject::invokeMethod(this, [this, endedLease] {
                    if (!run_.load() && leaseToken().id == endedLease.id) stop();
                }, Qt::QueuedConnection);
            }
        }
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!passTrackActive_) return;
        lastTrackHz_ = tunedHz;
        currentHz_ = tunedHz;
        lockHz_ = tunedHz;
        lastStatus_ = "Auto-track (digital Doppler)";
    }
    // DEC-0114: the IQ worker applies the offset without retuning the hardware
    // or changing decoder identity, including when automatic tracking pauses.
}

bool SatcomScannerEngine::refreshSpectrum(size_t deviceIndex, double* peakHz,
                                          double* peakDb) {
    auto& manager = DeviceManager::instance();
    std::vector<float> power;
    double center = 0.0;
    double rate = 0.0;
    if (!manager.getLatestSpectrum(deviceIndex, power, center, rate) ||
        power.empty() || rate <= 0.0) {
        return false;
    }

    const auto settings = config();
    const auto peak = selectSatcomSignalPeak(power, center, rate, settings.lowHz, settings.highHz);
    if (peakDb) *peakDb = peak ? peak->powerDb : -std::numeric_limits<double>::infinity();
    if (peakHz) *peakHz = peak ? peak->frequencyHz : 0.0;

    std::vector<float> display = power;
    if (display.size() > 256) {
        std::vector<float> downsampled(256, -120.0f);
        for (size_t i = 0; i < downsampled.size(); ++i) {
            const size_t begin = i * display.size() / downsampled.size();
            const size_t end = (i + 1) * display.size() / downsampled.size();
            float maximum = -200.0f;
            for (size_t k = begin; k < end && k < display.size(); ++k)
                maximum = std::max(maximum, display[k]);
            downsampled[i] = maximum;
        }
        display = std::move(downsampled);
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        spectrumDb_ = std::move(display);
        spectrumCenterHz_ = center;
        spectrumRateHz_ = rate;
        streamState_ = manager.getRuntimeStateLabel(deviceIndex);
        deviceConnected_ = streamState_ == "live hardware";
    }
    SatcomHostServices::instance().publishSpectrum(leaseToken(), power, center, rate);
    return true;
}

bool SatcomScannerEngine::detectActivity(size_t deviceIndex, double& peakHz,
                                         double& peakDb) {
    double squelch = -90.0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        squelch = config_.squelchDb;
    }
    if (!refreshSpectrum(deviceIndex, &peakHz, &peakDb)) return false;
    return peakDb >= squelch;
}

void SatcomScannerEngine::processLockedAudio() {
    auto& manager = DeviceManager::instance();
    size_t deviceIndex = static_cast<size_t>(-1);
    std::string mode;
    std::string role;
    bool decodeAx25 = true;
    bool decodeApt = true;
    double lockFrequencyHz = 0.0;
    double nominalFrequencyHz = 0.0;
    bool passTracking = false;
    double bandwidthHz = 12.5e3;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        deviceIndex = activeDeviceIndex_;
        mode = config_.mode;
        role = armedRole_;
        decodeAx25 = config_.enableAx25;
        decodeApt = config_.enableApt;
        lockFrequencyHz = lockHz_;
        passTracking = passTrackActive_;
        nominalFrequencyHz = passTracking ? passNominalHz_ : lockFrequencyHz;
        bandwidthHz = std::min(config_.bandwidthHz, 50e3);
    }
    if (deviceIndex == static_cast<size_t>(-1)) return;

    refreshSpectrum(deviceIndex);

    auto input = pullNewIq(deviceIndex, 524288);
    if (input.discontinuity) {
        iqDiscontinuities_.fetch_add(1, std::memory_order_acq_rel);
        sstvSourceEpoch_.fetch_add(1, std::memory_order_acq_rel);
        sstvAudioFirstSample_.store(0, std::memory_order_release);
        demod_->resetState();
        doppler_.reset();
        ax25_->reset();
        apt_->reset();
        demodResetRequested_.store(false, std::memory_order_release);
        pushLog(SatcomLog::EventType::Info, lockFrequencyHz,
                "IQ discontinuity; decoder state reset");
    } else if (demodResetRequested_.exchange(false, std::memory_order_acq_rel)) {
        demod_->resetState();
        doppler_.reset();
        ax25_->reset();
        apt_->reset();
    }
    if (input.samples.empty()) return;

    double sampleRate = manager.getCurrentSampleRate(deviceIndex);
    if (sampleRate <= 0.0) {
        const auto devices = manager.getDevices();
        if (deviceIndex < devices.size() && devices[deviceIndex].sampleRate > 0.0)
            sampleRate = devices[deviceIndex].sampleRate;
    }
    if (sampleRate <= 0.0) sampleRate = 2.048e6;

    const double reportedCenter = manager.getCurrentCenterFreq(deviceIndex);
    const double iqCenterHz = reportedCenter > 0.0 ? reportedCenter : lockFrequencyHz;
    if (passTracking && !doppler_.translate(input.samples, sampleRate, iqCenterHz,
                                           lockFrequencyHz, nominalFrequencyHz, bandwidthHz)) {
        // Fail closed outside the captured channel; resumption is a real gap.
        demodResetRequested_.store(true, std::memory_order_release);
        std::lock_guard<std::mutex> lock(mutex_);
        lastStatus_ = "Tracked channel outside captured bandwidth; re-arm with a wider sample rate";
        return;
    }
    const DemodMode demodMode = modeFromString(mode);
    const bool nfmSstv = role == "sstv" && demodMode == DemodMode::NFM;
    const bool ssbSstv = role == "sstv" &&
                         (demodMode == DemodMode::USB || demodMode == DemodMode::LSB);
    double rmsDb = -120.0;
    FmMultiplexBlock multiplex;
    // Request the demodulator's clean pre-squelch decoder block for
    // both FM and sideband SSTV. Reconstructing USB/LSB input from speaker
    // audio loses provenance and can include output gain/gating artifacts.
    const bool aprsData = decodeAx25 && (mode == "NFM" || mode == "APRS");
    const bool aptData = decodeApt && (role == "apt" || mode == "APT") && demodMode == DemodMode::NFM;
    FmMultiplexBlock* multiplexOutput =
        (nfmSstv || ssbSstv || aprsData || aptData) ? &multiplex : nullptr;
    auto audio = demod_->demodulateToAudio(
        input.samples, sampleRate, iqCenterHz, nominalFrequencyHz, demodMode,
        rmsDb, 3000.0, -120.0, 1.0, 75.0, 0.96, bandwidthHz,
        0, 48000.0,
        std::numeric_limits<double>::quiet_NaN(), true,
        multiplexOutput, nominalFrequencyHz);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        audioRmsDb_ = rmsDb;
    }

    if ((nfmSstv || ssbSstv) && !multiplex.samples.empty()) {
        sstvFeed_->publish(multiplex,
                           sstvSourceEpoch_.load(std::memory_order_acquire),
                           demodMode);
    }
    if (!audio.empty()) pushMonitorAudio(audio.data(), audio.size());

    if (aprsData && !multiplex.samples.empty()) {
        auto frames = ax25_->processAudio(multiplex.samples.data(), multiplex.samples.size(), multiplex.sampleRate);
        for (auto& frame : frames) {
            pushLog(SatcomLog::EventType::DecodeOk, lockFrequencyHz, frame.c_str());
            std::lock_guard<std::mutex> lock(mutex_);
            recentDecodes_.push_back(frame);
            if (recentDecodes_.size() > 100) recentDecodes_.erase(recentDecodes_.begin());
        }
    }

    if (aptData && !multiplex.samples.empty()) {
        if (apt_->processAudio(multiplex.samples.data(), multiplex.samples.size(), multiplex.sampleRate)) {
            std::string path;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                path = aptPreviewPath_;
            }
            if (path.empty()) {
                const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
                                     "/satcom_apt";
                QDir().mkpath(base);
                path = QDir(base).filePath(sessionId_.empty() ? "preview.pgm" :
                    "preview_" + QString::fromStdString(sessionId_) + ".pgm").toStdString();
            }
            if (apt_->writePgm(path)) {
                std::lock_guard<std::mutex> lock(mutex_);
                aptPreviewPath_ = path;
                lastStatus_ = "APT image updated: " + path;
            }
        }
    }

    bool recording = false;
    std::string path;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        recording = recording_;
        path = recordPath_;
    }
    if (recording && !path.empty() && !audio.empty()) {
        QDir().mkpath(QFileInfo(QString::fromStdString(path)).absolutePath());
        std::ofstream out(path, std::ios::binary | std::ios::app);
        if (out) {
            out.write(reinterpret_cast<const char*>(audio.data()),
                      static_cast<std::streamsize>(audio.size() * sizeof(float)));
        }
    }
}

void SatcomScannerEngine::workerLoop() {
    auto& manager = DeviceManager::instance();
    bool lostHardware = false;
    size_t deviceIndex = static_cast<size_t>(-1);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        deviceIndex = activeDeviceIndex_;
        const auto devices = manager.getDevices();
        if (deviceIndex < devices.size()) {
            deviceLabel_ = devices[deviceIndex].label;
            streamState_ = manager.getRuntimeStateLabel(deviceIndex);
            deviceConnected_ = streamState_ == "live hardware";
        } else {
            deviceLabel_ = "no device";
            deviceConnected_ = false;
            streamState_ = "stopped";
            lastStatus_ = "No active Satcom device";
            run_.store(false, std::memory_order_release);
            state_ = SatcomScannerState::Idle;
            return;
        }
        if (!passTrackActive_) currentHz_ = config_.lowHz;
    }

    while (run_.load(std::memory_order_acquire)) {
        SatcomScannerConfig config;
        bool skip = false;
        SatcomScannerState state;
        bool passHold = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            config = config_;
            skip = skipRequested_;
            skipRequested_ = false;
            state = state_;
            passHold = passTrackActive_;
        }

        if (!manager.ownsDevice(leaseToken()) || manager.getRuntimeStateLabel(deviceIndex) != "live hardware") {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                streamState_ = manager.getRuntimeStateLabel(deviceIndex);
                deviceConnected_ = false;
                lastStatus_ = "Satellite receiver lost live hardware: " + streamState_;
            }
            pushLog(SatcomLog::EventType::Info, currentHz_, "live hardware stream lost");
            lostHardware = true;
            run_.store(false, std::memory_order_release);
            notifyUpdate();
            break;
        }

        if (passHold) {
            tickPassTrack();
            if (!run_.load(std::memory_order_acquire)) break;
            processLockedAudio();
            notifyUpdate();
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
            continue;
        }

        if (state == SatcomScannerState::Locked || state == SatcomScannerState::Recording) {
            if (skip) {
                pushLog(SatcomLog::EventType::Unlock, lockHz_, "skip unlock");
                resetChronologicalInput();
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    state_ = SatcomScannerState::Scanning;
                    lockHz_ = 0.0;
                    recording_ = false;
                    currentHz_ = std::min(config.highHz, currentHz_ + config.stepHz);
                    lastStatus_ = "Scanning";
                }
            } else {
                processLockedAudio();
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
            }
        } else {
            double tuneHz = 0.0;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (currentHz_ < config.lowHz) currentHz_ = config.lowHz;
                if (currentHz_ > config.highHz) currentHz_ = config.lowHz;
                tuneHz = currentHz_;
                lastStatus_ = "Scanning";
            }
            std::string tuneError;
            if (!manager.tuneDevice(leaseToken(), tuneHz, &tuneError)) {
                std::lock_guard<std::mutex> lock(mutex_);
                lastStatus_ = tuneError.empty() ? "scan retune blocked" : tuneError;
            }
            std::this_thread::sleep_for(
                std::chrono::milliseconds(std::max(20, config.dwellMs)));

            double peakHz = 0.0;
            double peakDb = -200.0;
            if (detectActivity(deviceIndex, peakHz, peakDb)) {
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    lockHz_ = peakHz;
                    currentHz_ = peakHz;
                    state_ = SatcomScannerState::Locked;
                    lastStatus_ = "Locked";
                    recordSatId_ = "scan";
                    recordDownlinkId_ = "activity";
                    recordPath_.clear();
                    armedRole_.clear();
                }
                resetChronologicalInput();
                manager.tuneDevice(leaseToken(), peakHz, nullptr);
                pushLog(SatcomLog::EventType::Lock, peakHz, "activity");
            } else {
                std::lock_guard<std::mutex> lock(mutex_);
                currentHz_ += config.stepHz;
                if (currentHz_ > config.highHz) currentHz_ = config.lowHz;
            }
        }

        notifyUpdate();
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = SatcomScannerState::Idle;
        deviceConnected_ = false;
        passTrackActive_ = false;
        passStartedEngine_ = false;
    }

    if (lostHardware) {
        // Host restore may invoke GUI callbacks. Never wait on the GUI from a
        // worker that the same GUI can be joining (DEC-0189).
        const auto failedLease = leaseToken();
        QMetaObject::invokeMethod(this, [this, failedLease] {
            if (!run_.load() && leaseToken().id == failedLease.id) {
                planner().disarm();
                stop();
            }
        }, Qt::QueuedConnection);
    }
}
