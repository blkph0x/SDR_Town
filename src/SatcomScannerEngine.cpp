#include "SatcomScannerEngine.h"
#include "AudioEngine.h"
#include "Ax25AprsDecoder.h"
#include "AptImageDecoder.h"
#include "DeviceManager.h"
#include "Demod.h"
#include "SatPassPlanner.h"
#include "SstvLiveSession.h"
#include "SstvReceiverFeed.h"

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

std::string configPath() {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(base);
    return (base + "/satcom_scanner.json").toStdString();
}

DemodMode modeFromString(const std::string& m) {
    if (m == "WFM") return DemodMode::WFM;
    if (m == "AM") return DemodMode::AM;
    // NOAA APT uses a 2400 Hz AM subcarrier carried on an FM RF downlink.
    // The AptImageDecoder performs the subcarrier envelope recovery after the
    // RF FM discriminator, so APT must never select the RF AM demodulator.
    if (m == "APT") return DemodMode::NFM;
    if (m == "USB") return DemodMode::USB;
    if (m == "LSB") return DemodMode::LSB;
    return DemodMode::NFM; // NFM / APRS / SSTV
}

std::string safeToken(std::string value) {
    for (char& c : value) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (!std::isalnum(u) && c != '-' && c != '_') c = '_';
    }
    if (value.empty()) value = "unknown";
    return value;
}

bool isPlaceholderDevice(const DeviceInfo& device) {
    std::string label = device.label;
    std::transform(label.begin(), label.end(), label.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return label.find("placeholder") != std::string::npos ||
           label.find("(stub)") != std::string::npos;
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
    SatcomScannerConfig c;
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    c.recordDir = (base + "/satcom_recordings").toStdString();
    c.logDir = (base + "/satcom_logs").toStdString();
    c.presets = {
        {"UHF Scan 420-430", 420e6, 430e6, 12.5e3, 250e3, "NFM", -90.0},
        {"NOAA APT 137.100", 137.05e6, 137.15e6, 5e3, 40e3, "APT", -95.0},
        {"NOAA APT 137.9125", 137.85e6, 137.95e6, 5e3, 40e3, "APT", -95.0},
        {"ISS FM 145.800", 145.75e6, 145.85e6, 5e3, 15e3, "APRS", -92.0},
        {"Amateur VHF 145-146", 145e6, 146e6, 5e3, 15e3, "NFM", -92.0},
    };
    return c;
}

nlohmann::json SatcomScannerConfig::toJson() const {
    nlohmann::json j;
    j["lowHz"] = lowHz;
    j["highHz"] = highHz;
    j["stepHz"] = stepHz;
    j["dwellMs"] = dwellMs;
    j["bandwidthHz"] = bandwidthHz;
    j["mode"] = mode;
    j["squelchDb"] = squelchDb;
    j["deviceIndex"] = deviceIndex;
    j["deviceStableKey"] = deviceStableKey;
    j["recordDir"] = recordDir;
    j["logDir"] = logDir;
    j["enableAx25"] = enableAx25;
    j["enableApt"] = enableApt;
    j["autoCapture"] = autoCapture;
    j["monitorAudio"] = monitorAudio;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& p : presets) {
        arr.push_back({
            {"name", p.name}, {"lowHz", p.lowHz}, {"highHz", p.highHz},
            {"stepHz", p.stepHz}, {"bandwidthHz", p.bandwidthHz},
            {"mode", p.mode}, {"squelchDb", p.squelchDb}
        });
    }
    j["presets"] = arr;
    return j;
}

SatcomScannerConfig SatcomScannerConfig::fromJson(const nlohmann::json& j) {
    SatcomScannerConfig c = defaults();
    if (!j.is_object()) return c;
    c.lowHz = j.value("lowHz", c.lowHz);
    c.highHz = j.value("highHz", c.highHz);
    c.stepHz = j.value("stepHz", c.stepHz);
    c.dwellMs = j.value("dwellMs", c.dwellMs);
    c.bandwidthHz = j.value("bandwidthHz", c.bandwidthHz);
    c.mode = j.value("mode", c.mode);
    c.squelchDb = j.value("squelchDb", c.squelchDb);
    c.deviceIndex = j.value("deviceIndex", c.deviceIndex);
    c.deviceStableKey = j.value("deviceStableKey", c.deviceStableKey);
    c.recordDir = j.value("recordDir", c.recordDir);
    c.logDir = j.value("logDir", c.logDir);
    c.enableAx25 = j.value("enableAx25", c.enableAx25);
    c.enableApt = j.value("enableApt", c.enableApt);
    c.autoCapture = j.value("autoCapture", c.autoCapture);
    c.monitorAudio = j.value("monitorAudio", c.monitorAudio);
    if (j.contains("presets") && j["presets"].is_array()) {
        c.presets.clear();
        for (const auto& pj : j["presets"]) {
            SatcomPreset p;
            p.name = pj.value("name", "preset");
            p.lowHz = pj.value("lowHz", 420e6);
            p.highHz = pj.value("highHz", 430e6);
            p.stepHz = pj.value("stepHz", 12.5e3);
            p.bandwidthHz = pj.value("bandwidthHz", 250e3);
            p.mode = pj.value("mode", "NFM");
            p.squelchDb = pj.value("squelchDb", -90.0);
            c.presets.push_back(p);
        }
    }
    return c;
}

void SatcomScannerConfig::load() {
    try {
        std::ifstream in(configPath());
        if (!in) { *this = defaults(); return; }
        nlohmann::json j;
        in >> j;
        *this = fromJson(j);
    } catch (...) {
        *this = defaults();
    }
}

void SatcomScannerConfig::save() const {
    try {
        std::ofstream out(configPath());
        out << toJson().dump(2);
    } catch (...) {}
}

SatcomScannerEngine& SatcomScannerEngine::instance() {
    static SatcomScannerEngine eng;
    return eng;
}

SatcomScannerEngine::SatcomScannerEngine() {
    config_.load();
    ax25_ = std::make_unique<Ax25AprsDecoder>();
    apt_ = std::make_unique<AptImageDecoder>();
    demod_ = std::make_unique<Demodulator>();
    sstvFeed_ = std::make_shared<SstvReceiverFeed>();
    log_.setLogDirectory(config_.logDir);
    log_.start();
}

SatcomScannerEngine::~SatcomScannerEngine() {
    stop();
    finishSstvCapture(false);
    shutdownAudioOutput();
    log_.stop();
}

void SatcomScannerEngine::setConfig(const SatcomScannerConfig& cfg) {
    std::lock_guard<std::mutex> lk(mutex_);
    config_ = cfg;
    config_.save();
    log_.setLogDirectory(config_.logDir);
}

SatcomScannerConfig SatcomScannerEngine::config() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return config_;
}

void SatcomScannerEngine::setAutoCaptureEnabled(bool on) {
    std::lock_guard<std::mutex> lk(mutex_);
    config_.autoCapture = on;
    config_.save();
}

bool SatcomScannerEngine::autoCaptureEnabled() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return config_.autoCapture;
}

size_t SatcomScannerEngine::resolveDeviceIndex(std::string* error) {
    auto& manager = DeviceManager::instance();
    const auto devices = manager.getDevices();
    if (devices.empty()) {
        if (error) *error = "No SDR devices are available; rescan devices first";
        return static_cast<size_t>(-1);
    }

    SatcomScannerConfig cfg = config();
    size_t chosen = static_cast<size_t>(-1);
    if (!cfg.deviceStableKey.empty()) {
        for (size_t i = 0; i < devices.size(); ++i) {
            if (devices[i].stableKey == cfg.deviceStableKey && !isPlaceholderDevice(devices[i])) {
                chosen = i;
                break;
            }
        }
    }
    if (chosen == static_cast<size_t>(-1) && cfg.deviceIndex < devices.size() &&
        !isPlaceholderDevice(devices[cfg.deviceIndex])) {
        chosen = cfg.deviceIndex;
    }
    auto choose = [&](auto predicate) {
        if (chosen != static_cast<size_t>(-1)) return;
        for (size_t i = 0; i < devices.size(); ++i) {
            if (!isPlaceholderDevice(devices[i]) && predicate(i, devices[i])) {
                chosen = i;
                return;
            }
        }
    };
    choose([&](size_t i, const DeviceInfo&) { return manager.isStreaming(i); });
    choose([](size_t, const DeviceInfo& d) { return d.enabled; });
    choose([](size_t, const DeviceInfo& d) { return d.isSdrplay; });
    choose([](size_t, const DeviceInfo&) { return true; });

    if (chosen == static_cast<size_t>(-1)) {
        if (error) *error = "No real SDR device is available; placeholder/stub devices cannot run Satcom";
        return chosen;
    }

    const std::string stableKey = devices[chosen].stableKey;
    if (cfg.deviceIndex != chosen || cfg.deviceStableKey != stableKey) {
        cfg.deviceIndex = chosen;
        cfg.deviceStableKey = stableKey;
        setConfig(cfg);
    }
    return chosen;
}

void SatcomScannerEngine::setUpdateCallback(std::function<void()> cb) {
    std::lock_guard<std::mutex> lk(mutex_);
    updateCb_ = std::move(cb);
}

void SatcomScannerEngine::notifyUpdate() {
    std::function<void()> cb;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        cb = updateCb_;
    }
    if (cb) {
        try { cb(); } catch (...) {}
    }
}

void SatcomScannerEngine::pushLog(SatcomLog::EventType t, double hz, const char* text) {
    log_.tryPush(t, hz, text);
}

std::string SatcomScannerEngine::makeCaptureStem(const std::string& satId,
                                                 const std::string& downlinkId) const {
    const std::string timestamp = QDateTime::currentDateTimeUtc()
                                      .toString("yyyyMMdd_HHmmss_zzz")
                                      .toStdString();
    return timestamp + "_" + safeToken(satId) + "_" + safeToken(downlinkId);
}

void SatcomScannerEngine::capturePreviousDeviceState(size_t deviceIndex) {
    {
        std::lock_guard<std::mutex> lk(mutex_);
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
    std::lock_guard<std::mutex> lk(mutex_);
    if (!previousDeviceState_.has_value()) previousDeviceState_ = saved;
}

void SatcomScannerEngine::restorePreviousDeviceState() {
    std::optional<PreviousDeviceState> saved;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        saved = previousDeviceState_;
        previousDeviceState_.reset();
    }

    auto& manager = DeviceManager::instance();
    if (!saved.has_value() || saved->deviceIndex == static_cast<size_t>(-1)) {
        manager.releaseDeviceLease(DeviceManager::DeviceLeaseOwner::Satcom);
        return;
    }

    const size_t dev = saved->deviceIndex;
    if (saved->wasStreaming) {
        if (!manager.isStreaming(dev)) manager.startStreaming(dev, true);
        if (saved->centerHz > 0.0) {
            std::string ignored;
            manager.retuneWithLease(dev, saved->centerHz,
                                    DeviceManager::DeviceLeaseOwner::Satcom,
                                    true, &ignored);
        }
        manager.setEnabled(dev, true);
    } else {
        manager.stopStreaming(dev);
        manager.setEnabled(dev, saved->wasEnabled);
    }
    manager.releaseDeviceLease(DeviceManager::DeviceLeaseOwner::Satcom);
}

void SatcomScannerEngine::resetChronologicalInput() {
    {
        std::lock_guard<std::mutex> lk(iqMutex_);
        iqCursor_.reset();
    }
    sstvSourceEpoch_.fetch_add(1, std::memory_order_acq_rel);
    demodResetRequested_.store(true, std::memory_order_release);
}

SatcomIqCursor::Result SatcomScannerEngine::pullNewIq(size_t deviceIndex, size_t maxSamples) {
    const auto window = DeviceManager::instance().getRecentIQWindowWithCursor(deviceIndex, maxSamples);
    std::lock_guard<std::mutex> lk(iqMutex_);
    return iqCursor_.consume(window.samples,
                             window.startAbsolute,
                             window.endAbsolute,
                             window.streamEpoch,
                             window.cursorDiscontinuity);
}

bool SatcomScannerEngine::waitForOperationalStream(
    size_t deviceIndex, int timeoutMs, std::string* error)
{
    auto& manager = DeviceManager::instance();
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(std::max(250, timeoutMs));
    std::string lastState = "starting";

    while (std::chrono::steady_clock::now() < deadline) {
        lastState = manager.getRuntimeStateLabel(deviceIndex);
        {
            std::lock_guard<std::mutex> lk(mutex_);
            streamState_ = lastState;
        }

        if (lastState == "live hardware") {
            const auto probe = manager.getRecentIQWindowWithCursor(deviceIndex, 4096);
            if (!probe.samples.empty() && probe.endAbsolute > probe.startAbsolute) return true;
        }
        if (containsInsensitive(lastState, "failed") ||
            containsInsensitive(lastState, "stub")) {
            if (error) *error = "Receiver did not enter live hardware mode: " + lastState;
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }

    if (error) {
        *error = "Timed out waiting for live hardware IQ; final state: " + lastState;
    }
    return false;
}

bool SatcomScannerEngine::ensureAudioOutput(std::string* error) {
    bool enabled = true;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        enabled = config_.monitorAudio;
        if (!enabled) audioMonitoring_ = false;
    }
    if (!enabled) return true;

    std::lock_guard<std::mutex> lk(audioMutex_);
    if (audio_ && audio_->activeOutputCount() > 0) {
        std::lock_guard<std::mutex> stateLock(mutex_);
        audioMonitoring_ = true;
        return true;
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
        audio_ = std::move(candidate);
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
    std::lock_guard<std::mutex> lk(audioMutex_);
    if (!audio_) return;
    // Keep the independent Satcom monitor close to live time after a slow
    // decoder/UI interval rather than replaying seconds of stale pass audio.
    audio_->trimQueuedAudio(12000);
    audio_->pushAudio(samples, count);
}

void SatcomScannerEngine::shutdownAudioOutput() {
    std::unique_ptr<AudioEngine> old;
    {
        std::lock_guard<std::mutex> lk(audioMutex_);
        old = std::move(audio_);
    }
    if (old) old->clearBuffers();
    std::lock_guard<std::mutex> stateLock(mutex_);
    audioMonitoring_ = false;
}

bool SatcomScannerEngine::start(bool force) {
    if (run_.load(std::memory_order_acquire)) return true;

    std::string err;
    const size_t dev = resolveDeviceIndex(&err);
    if (dev == static_cast<size_t>(-1)) {
        std::lock_guard<std::mutex> lk(mutex_);
        lastStatus_ = err.empty() ? "No receiver selected" : err;
        return false;
    }

    auto& manager = DeviceManager::instance();
    if (manager.deviceLeaseOwner() == DeviceManager::DeviceLeaseOwner::P25) {
        std::lock_guard<std::mutex> lk(mutex_);
        lastStatus_ = "P25 owns the receiver; Satcom will not interrupt it. Select another device.";
        return false;
    }
    if (!manager.acquireDeviceLease(dev, DeviceManager::DeviceLeaseOwner::Satcom, force, &err)) {
        std::lock_guard<std::mutex> lk(mutex_);
        lastStatus_ = err;
        return false;
    }
    capturePreviousDeviceState(dev);

    if (worker_.joinable()) worker_.join();
    if (!manager.setEnabled(dev, true) || !manager.startStreaming(dev, true)) {
        {
            std::lock_guard<std::mutex> lk(mutex_);
            lastStatus_ = "Could not start selected receiver";
            streamState_ = manager.getRuntimeStateLabel(dev);
        }
        restorePreviousDeviceState();
        return false;
    }
    if (!waitForOperationalStream(dev, 10000, &err)) {
        {
            std::lock_guard<std::mutex> lk(mutex_);
            lastStatus_ = err;
            deviceConnected_ = false;
        }
        restorePreviousDeviceState();
        return false;
    }

    std::string audioError;
    if (!ensureAudioOutput(&audioError) && !audioError.empty()) {
        spdlog::warn("{}", audioError);
    }

    run_.store(true, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lk(mutex_);
        state_ = passTrackActive_ ? SatcomScannerState::Locked : SatcomScannerState::Scanning;
        currentHz_ = passTrackActive_ && lockHz_ > 0.0 ? lockHz_ : config_.lowHz;
        if (!passTrackActive_) lockHz_ = 0.0;
        skipRequested_ = false;
        deviceConnected_ = true;
        streamState_ = "live hardware";
        lastStatus_ = passTrackActive_ ? "Pass armed - live hardware" : "Scanning - live hardware";
    }
    iqDiscontinuities_.store(0, std::memory_order_release);
    resetChronologicalInput();
    pushLog(SatcomLog::EventType::Start, currentHz_, "scan start live hardware");
    worker_ = std::thread(&SatcomScannerEngine::workerLoop, this);
    notifyUpdate();
    return true;
}

void SatcomScannerEngine::stop() {
    run_.store(false, std::memory_order_release);
    if (worker_.joinable()) worker_.join();
    finishSstvCapture(false);
    shutdownAudioOutput();
    {
        std::lock_guard<std::mutex> lk(mutex_);
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
    pushLog(SatcomLog::EventType::Stop, currentHz_, "scan stop");
    notifyUpdate();
}

void SatcomScannerEngine::skip() {
    {
        std::lock_guard<std::mutex> lk(mutex_);
        skipRequested_ = true;
    }
    resetChronologicalInput();
    pushLog(SatcomLog::EventType::Skip, lockHz_ > 0 ? lockHz_ : currentHz_, "skip");
}

bool SatcomScannerEngine::startRecording() {
    std::string path;
    double hz = 0.0;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (state_ != SatcomScannerState::Locked && state_ != SatcomScannerState::Recording)
            return false;
        if (recording_) return true;
        if (recordPath_.empty()) {
            const std::string sat = recordSatId_.empty() ? "scan" : recordSatId_;
            const std::string dl = recordDownlinkId_.empty() ? "activity" : recordDownlinkId_;
            QDir().mkpath(QString::fromStdString(config_.recordDir));
            recordPath_ = config_.recordDir + "/" + makeCaptureStem(sat, dl) + ".f32";
        }
        recordRequested_ = true;
        recording_ = true;
        recordHz_ = lockHz_;
        state_ = SatcomScannerState::Recording;
        path = recordPath_;
        hz = recordHz_;
        lastStatus_ = "Recording " + path;
    }
    QDir().mkpath(QFileInfo(QString::fromStdString(path)).absolutePath());
    std::ofstream create(path, std::ios::binary | std::ios::trunc);
    if (!create) {
        std::lock_guard<std::mutex> lk(mutex_);
        recording_ = false;
        recordRequested_ = false;
        if (state_ == SatcomScannerState::Recording) state_ = SatcomScannerState::Locked;
        lastStatus_ = "Could not create recording: " + path;
        return false;
    }
    pushLog(SatcomLog::EventType::RecordStart, hz, "record");
    return true;
}

void SatcomScannerEngine::writeRecordingMetadata(const std::string& path) const {
    if (path.empty() || !QFileInfo::exists(QString::fromStdString(path))) return;
    std::string satId, downlinkId, role, mode;
    double hz = 0.0;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        satId = recordSatId_;
        downlinkId = recordDownlinkId_;
        role = armedRole_;
        mode = config_.mode;
        hz = recordHz_;
    }
    const QFileInfo info(QString::fromStdString(path));
    const nlohmann::json meta = {
        {"format", "float32-le-mono"},
        {"sampleRate", 48000},
        {"bytes", info.size()},
        {"frequencyHz", hz},
        {"satId", satId},
        {"downlinkId", downlinkId},
        {"role", role},
        {"mode", mode},
        {"iqDiscontinuities", iqDiscontinuities_.load(std::memory_order_acquire)},
        {"capturedUtc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toStdString()},
    };
    try {
        std::ofstream out(path + ".json", std::ios::trunc);
        if (out) out << meta.dump(2);
    } catch (...) {}
}

void SatcomScannerEngine::stopRecording() {
    std::string path;
    double hz = 0.0;
    bool wasRecording = false;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        wasRecording = recording_;
        recording_ = false;
        recordRequested_ = false;
        path = recordPath_;
        hz = recordHz_;
        if (state_ == SatcomScannerState::Recording) state_ = SatcomScannerState::Locked;
    }
    if (wasRecording) {
        writeRecordingMetadata(path);
        pushLog(SatcomLog::EventType::RecordStop, hz, "record stop");
    }
}

bool SatcomScannerEngine::applyPreset(const std::string& name) {
    std::lock_guard<std::mutex> lk(mutex_);
    for (const auto& p : config_.presets) {
        if (p.name == name) {
            config_.lowHz = p.lowHz;
            config_.highHz = p.highHz;
            config_.stepHz = p.stepHz;
            config_.bandwidthHz = p.bandwidthHz;
            config_.mode = p.mode;
            config_.squelchDb = p.squelchDb;
            config_.save();
            return true;
        }
    }
    return false;
}

std::string SatcomScannerEngine::stateName() const {
    std::lock_guard<std::mutex> lk(mutex_);
    switch (state_) {
    case SatcomScannerState::Scanning: return "scanning";
    case SatcomScannerState::Locked: return "locked";
    case SatcomScannerState::Recording: return "recording";
    case SatcomScannerState::Idle:
    default: return "idle";
    }
}

SatcomScannerSnapshot SatcomScannerEngine::snapshot() const {
    SatcomScannerSnapshot s;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        s.state = state_;
        s.config = config_;
        s.currentHz = currentHz_;
        s.lockHz = lockHz_;
        s.recordHz = recordHz_;
        s.audioRmsDb = audioRmsDb_;
        s.deviceLabel = deviceLabel_;
        s.deviceConnected = deviceConnected_;
        s.streamState = streamState_;
        s.audioMonitoring = audioMonitoring_;
        s.spectrumDb = spectrumDb_;
        s.spectrumCenterHz = spectrumCenterHz_;
        s.spectrumRateHz = spectrumRateHz_;
        s.recentDecodes = recentDecodes_;
        s.aptPreviewPath = aptPreviewPath_;
        s.recordPath = recordPath_;
        s.sstvOutputDir = sstvOutputDir_;
        s.logWritten = log_.eventsWritten();
        s.logDropped = log_.eventsDropped();
        s.lastStatus = lastStatus_;
        s.armedRole = armedRole_;
    }
    s.iqDiscontinuities = iqDiscontinuities_.load(std::memory_order_acquire);
    const auto arm = SatPassPlanner::instance().snapshot().armed;
    s.passArmed = arm.armed;
    s.autoTrack = arm.autoTrack;
    s.dopplerHz = arm.dopplerHz;
    s.tunedHz = arm.tunedHz;
    if (!arm.role.empty()) s.armedRole = arm.role;
    return s;
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
        std::lock_guard<std::mutex> lk(mutex_);
        sstvOutputDir_ = output.toStdString();
    }
    sstvFinish_ = false;
    sstvCancel_ = false;
    const auto feed = sstvFeed_;
    std::lock_guard<std::mutex> lk(sstvMutex_);
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
        std::lock_guard<std::mutex> lk(sstvMutex_);
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
    auto& manager = DeviceManager::instance();
    std::string err;
    const size_t dev = resolveDeviceIndex(&err);
    if (dev == static_cast<size_t>(-1)) {
        if (error) *error = err.empty() ? "No receiver selected" : err;
        return false;
    }
    if (manager.deviceLeaseOwner() == DeviceManager::DeviceLeaseOwner::P25) {
        const std::string blocked =
            "P25 owns the receiver; Satcom will not interrupt it. Select another device.";
        {
            std::lock_guard<std::mutex> lk(mutex_);
            lastStatus_ = blocked;
        }
        if (error) *error = blocked;
        return false;
    }
    if (!manager.acquireDeviceLease(dev, DeviceManager::DeviceLeaseOwner::Satcom, force, &err)) {
        if (error) *error = err;
        return false;
    }
    capturePreviousDeviceState(dev);

    if (!SatPassPlanner::instance().arm(satId, downlinkId, autoTrack, &err)) {
        restorePreviousDeviceState();
        if (error) *error = err;
        return false;
    }
    const bool engineWasRunning = run_.load(std::memory_order_acquire);
    const auto snap = SatPassPlanner::instance().snapshot();
    const std::string stem = makeCaptureStem(satId, snap.armed.downlinkId);
    {
        std::lock_guard<std::mutex> lk(mutex_);
        passTrackActive_ = true;
        passStartedEngine_ = !engineWasRunning;
        armedRole_ = snap.armed.role;
        config_.mode = snap.armed.mode.empty() ? "NFM" : snap.armed.mode;
        if (config_.mode == "sstv" || snap.armed.role == "sstv") config_.mode = "NFM";
        if (snap.armed.role == "apt") {
            config_.mode = "APT";
            config_.bandwidthHz = 40e3;
        } else if (snap.armed.role == "aprs" || snap.armed.role == "sstv" ||
                   snap.armed.role == "voice") {
            config_.bandwidthHz = 15e3;
        } else {
            config_.bandwidthHz = std::min(config_.bandwidthHz, 25e3);
        }
        currentHz_ = snap.armed.freqHz;
        lockHz_ = snap.armed.freqHz;
        lastTrackHz_ = 0.0;
        recordSatId_ = satId;
        recordDownlinkId_ = snap.armed.downlinkId;
        QDir().mkpath(QString::fromStdString(config_.recordDir));
        recordPath_ = config_.recordDir + "/" + stem + ".f32";
        aptPreviewPath_.clear();
        if (snap.armed.role == "apt") {
            const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
                                 "/satcom_apt";
            QDir().mkpath(base);
            aptPreviewPath_ = QDir(base).filePath(QString::fromStdString(stem) + ".pgm").toStdString();
        }
        sstvOutputDir_.clear();
        lastStatus_ = "Preparing pass receiver";
        config_.save();
    }
    resetChronologicalInput();

    if (!engineWasRunning) {
        if (worker_.joinable()) worker_.join();
        if (!manager.setEnabled(dev, true) || !manager.startStreaming(dev, true) ||
            !waitForOperationalStream(dev, 10000, &err)) {
            const std::string startError = err.empty()
                ? "Could not start selected satellite receiver in live hardware mode"
                : err;
            {
                std::lock_guard<std::mutex> lk(mutex_);
                passTrackActive_ = false;
                passStartedEngine_ = false;
                armedRole_.clear();
                state_ = SatcomScannerState::Idle;
                deviceConnected_ = false;
                lastStatus_ = startError;
            }
            SatPassPlanner::instance().disarm();
            restorePreviousDeviceState();
            if (error) *error = startError;
            return false;
        }
        std::string audioError;
        if (!ensureAudioOutput(&audioError) && !audioError.empty()) spdlog::warn("{}", audioError);
    }

    if (!manager.retuneWithLease(dev, snap.armed.freqHz,
                                 DeviceManager::DeviceLeaseOwner::Satcom,
                                 true, &err)) {
        const std::string tuneError = err.empty() ? "Could not tune pass receiver" : err;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            passTrackActive_ = false;
            passStartedEngine_ = false;
            armedRole_.clear();
            state_ = run_.load(std::memory_order_acquire)
                ? SatcomScannerState::Scanning
                : SatcomScannerState::Idle;
            lastStatus_ = tuneError;
        }
        SatPassPlanner::instance().disarm();
        if (!engineWasRunning) restorePreviousDeviceState();
        if (error) *error = tuneError;
        return false;
    }

    const uint64_t tuneRequest = manager.getCenterTuneRequestSeq(dev);
    const auto tuneDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    bool tuneApplied = false;
    while (std::chrono::steady_clock::now() < tuneDeadline) {
        if (manager.getCenterTuneAppliedSeq(dev) >= tuneRequest &&
            std::abs(manager.getCurrentCenterFreq(dev) - snap.armed.freqHz) <= 100.0) {
            tuneApplied = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!tuneApplied) {
        const std::string tuneError = "Satellite receiver did not confirm the requested pass frequency";
        {
            std::lock_guard<std::mutex> lk(mutex_);
            lastStatus_ = tuneError;
            passTrackActive_ = false;
            passStartedEngine_ = false;
            state_ = run_.load(std::memory_order_acquire)
                ? SatcomScannerState::Scanning
                : SatcomScannerState::Idle;
        }
        SatPassPlanner::instance().disarm();
        if (!engineWasRunning) restorePreviousDeviceState();
        if (error) *error = tuneError;
        return false;
    }

    resetChronologicalInput();
    if (!engineWasRunning) {
        run_.store(true, std::memory_order_release);
        iqDiscontinuities_.store(0, std::memory_order_release);
        worker_ = std::thread(&SatcomScannerEngine::workerLoop, this);
    }
    {
        std::lock_guard<std::mutex> lk(mutex_);
        state_ = SatcomScannerState::Locked;
        deviceConnected_ = true;
        streamState_ = "live hardware";
        lastStatus_ = "Pass armed - tune confirmed";
    }

    pushLog(SatcomLog::EventType::Lock, snap.armed.freqHz, "pass arm tune confirmed");
    tickPassTrack();
    if (snap.armed.role == "sstv") startSstvCapture(satId, snap.armed.downlinkId);
    notifyUpdate();
    return true;
}

void SatcomScannerEngine::disarmPass() {
    stopRecording();
    finishSstvCapture(false);
    SatPassPlanner::instance().disarm();
    bool stopPassOnly = false;
    const bool running = run_.load(std::memory_order_acquire);
    {
        std::lock_guard<std::mutex> lk(mutex_);
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
    }
    notifyUpdate();
}

void SatcomScannerEngine::setAutoTrack(bool on) {
    SatPassPlanner::instance().setAutoTrack(on);
}

void SatcomScannerEngine::tickPassTrack() {
    double tuned = 0.0;
    if (!SatPassPlanner::instance().tickAutoTrack(&tuned)) {
        const bool stillArmed = SatPassPlanner::instance().snapshot().armed.armed;
        bool ended = false;
        bool stopPassOnly = false;
        {
            std::lock_guard<std::mutex> lk(mutex_);
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
                lastStatus_ = stopPassOnly ? "Pass ended - receiver restored" : "Pass ended - scanning";
            }
        }
        if (ended) {
            stopRecording();
            finishSstvCapture(false);
            resetChronologicalInput();
            if (stopPassOnly) {
                run_.store(false, std::memory_order_release);
                shutdownAudioOutput();
                restorePreviousDeviceState();
            }
        }
        return;
    }
    size_t dev = 0;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (!passTrackActive_) return;
        if (std::abs(tuned - lastTrackHz_) < 50.0) return; // 50 Hz deadband
        lastTrackHz_ = tuned;
        currentHz_ = tuned;
        lockHz_ = tuned;
        dev = config_.deviceIndex;
        lastStatus_ = "Auto-track";
    }
    std::string err;
    if (!DeviceManager::instance().retuneWithLease(
            dev, tuned, DeviceManager::DeviceLeaseOwner::Satcom, true, &err)) {
        std::lock_guard<std::mutex> lk(mutex_);
        lastStatus_ = err.empty() ? "auto-track retune blocked" : err;
    }
}

bool SatcomScannerEngine::refreshSpectrum(
    size_t deviceIndex, double* peakHz, double* peakDb)
{
    auto& manager = DeviceManager::instance();
    std::vector<float> power;
    double center = 0.0;
    double rate = 0.0;
    if (!manager.getLatestSpectrum(deviceIndex, power, center, rate) ||
        power.empty() || rate <= 0.0) {
        return false;
    }

    size_t peakIndex = 0;
    float peak = power[0];
    for (size_t i = 1; i < power.size(); ++i) {
        if (power[i] > peak) {
            peak = power[i];
            peakIndex = i;
        }
    }
    if (peakDb) *peakDb = peak;
    if (peakHz) {
        const double binHz = rate / static_cast<double>(power.size());
        *peakHz = center - rate * 0.5 + (static_cast<double>(peakIndex) + 0.5) * binHz;
    }

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
        std::lock_guard<std::mutex> lk(mutex_);
        spectrumDb_ = std::move(display);
        spectrumCenterHz_ = center;
        spectrumRateHz_ = rate;
        streamState_ = manager.getRuntimeStateLabel(deviceIndex);
        deviceConnected_ = streamState_ == "live hardware";
    }
    return true;
}

bool SatcomScannerEngine::detectActivity(double& peakHz, double& peakDb) {
    size_t dev = 0;
    double squelch = -90.0;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        dev = config_.deviceIndex;
        squelch = config_.squelchDb;
    }
    if (!refreshSpectrum(dev, &peakHz, &peakDb)) return false;
    return peakDb >= squelch;
}

void SatcomScannerEngine::processLockedAudio() {
    auto& manager = DeviceManager::instance();
    size_t dev = 0;
    std::string mode;
    std::string role;
    bool doAx = true;
    bool doApt = true;
    double lockHz = 0.0;
    double bandwidth = 12.5e3;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        dev = config_.deviceIndex;
        mode = config_.mode;
        role = armedRole_;
        doAx = config_.enableAx25;
        doApt = config_.enableApt;
        lockHz = lockHz_;
        bandwidth = std::min(config_.bandwidthHz, 50e3);
    }

    // Keep spectrum/waterfall live in locked and armed-pass states too.
    refreshSpectrum(dev);

    auto input = pullNewIq(dev, 524288);
    if (input.discontinuity) {
        iqDiscontinuities_.fetch_add(1, std::memory_order_acq_rel);
        sstvSourceEpoch_.fetch_add(1, std::memory_order_acq_rel);
        demod_->resetState();
        ax25_->reset();
        apt_->reset();
        demodResetRequested_.store(false, std::memory_order_release);
        pushLog(SatcomLog::EventType::Info, lockHz, "IQ discontinuity; decoder state reset");
    } else if (demodResetRequested_.exchange(false, std::memory_order_acq_rel)) {
        demod_->resetState();
        ax25_->reset();
        apt_->reset();
    }
    if (input.samples.size() < 1024) return;

    double sampleRate = manager.getCurrentSampleRate(dev);
    if (sampleRate <= 0.0) {
        const auto devices = manager.getDevices();
        if (dev < devices.size() && devices[dev].sampleRate > 0.0)
            sampleRate = devices[dev].sampleRate;
    }
    if (sampleRate <= 0.0) sampleRate = 2.048e6;

    const double reportedCenter = manager.getCurrentCenterFreq(dev);
    const double iqCenterHz = reportedCenter > 0.0 ? reportedCenter : lockHz;
    double rms = -120.0;
    FmMultiplexBlock multiplex;
    FmMultiplexBlock* multiplexOut = role == "sstv" ? &multiplex : nullptr;
    auto audio = demod_->demodulateToAudio(
        input.samples, sampleRate, iqCenterHz, lockHz, modeFromString(mode),
        rms, 3000.0, -120.0, 1.0, 75.0, 0.96, bandwidth,
        0, 48000.0,
        std::numeric_limits<double>::quiet_NaN(), true,
        multiplexOut, lockHz);
    {
        std::lock_guard<std::mutex> lk(mutex_);
        audioRmsDb_ = rms;
    }

    if (multiplexOut && !multiplex.samples.empty()) {
        sstvFeed_->publish(multiplex, sstvSourceEpoch_.load(std::memory_order_acquire));
    }
    if (audio.empty()) return;

    pushMonitorAudio(audio.data(), audio.size());

    if (doAx && (mode == "NFM" || mode == "APRS")) {
        auto frames = ax25_->processAudio(audio.data(), audio.size(), 48000.0);
        for (auto& frame : frames) {
            pushLog(SatcomLog::EventType::DecodeOk, lockHz, frame.c_str());
            std::lock_guard<std::mutex> lk(mutex_);
            recentDecodes_.push_back(frame);
            if (recentDecodes_.size() > 100) recentDecodes_.erase(recentDecodes_.begin());
        }
    }

    if (doApt && (role == "apt" || mode == "APT" || mode == "AM")) {
        if (apt_->processAudio(audio.data(), audio.size(), 48000.0)) {
            std::string path;
            {
                std::lock_guard<std::mutex> lk(mutex_);
                path = aptPreviewPath_;
            }
            if (path.empty()) {
                const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
                                     "/satcom_apt";
                QDir().mkpath(base);
                path = QDir(base).filePath("preview.pgm").toStdString();
            }
            if (apt_->writePgm(path)) {
                std::lock_guard<std::mutex> lk(mutex_);
                aptPreviewPath_ = path;
                lastStatus_ = "APT image updated: " + path;
            }
        }
    }

    bool recording = false;
    std::string path;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        recording = recording_;
        path = recordPath_;
    }
    if (recording && !path.empty()) {
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
    {
        std::lock_guard<std::mutex> lk(mutex_);
        const auto devices = manager.getDevices();
        if (config_.deviceIndex < devices.size()) {
            deviceLabel_ = devices[config_.deviceIndex].label;
            deviceConnected_ = manager.getRuntimeStateLabel(config_.deviceIndex) == "live hardware";
            streamState_ = manager.getRuntimeStateLabel(config_.deviceIndex);
        } else {
            deviceLabel_ = "no device";
            deviceConnected_ = false;
            streamState_ = "stopped";
            lastStatus_ = "No device";
            run_.store(false, std::memory_order_release);
            state_ = SatcomScannerState::Idle;
            return;
        }
        if (!passTrackActive_) currentHz_ = config_.lowHz;
    }

    while (run_.load(std::memory_order_acquire)) {
        SatcomScannerConfig cfg;
        bool skip = false;
        SatcomScannerState state;
        bool passHold = false;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            cfg = config_;
            skip = skipRequested_;
            skipRequested_ = false;
            state = state_;
            passHold = passTrackActive_;
        }

        if (manager.getRuntimeStateLabel(cfg.deviceIndex) != "live hardware") {
            {
                std::lock_guard<std::mutex> lk(mutex_);
                streamState_ = manager.getRuntimeStateLabel(cfg.deviceIndex);
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
                    std::lock_guard<std::mutex> lk(mutex_);
                    state_ = SatcomScannerState::Scanning;
                    lockHz_ = 0.0;
                    recording_ = false;
                    currentHz_ = std::min(cfg.highHz, currentHz_ + cfg.stepHz);
                    lastStatus_ = "Scanning";
                }
            } else {
                processLockedAudio();
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
            }
        } else {
            double tune = 0.0;
            {
                std::lock_guard<std::mutex> lk(mutex_);
                if (currentHz_ < cfg.lowHz) currentHz_ = cfg.lowHz;
                if (currentHz_ > cfg.highHz) currentHz_ = cfg.lowHz;
                tune = currentHz_;
                lastStatus_ = "Scanning";
            }
            std::string err;
            if (!manager.retuneWithLease(cfg.deviceIndex, tune,
                                         DeviceManager::DeviceLeaseOwner::Satcom,
                                         true, &err)) {
                std::lock_guard<std::mutex> lk(mutex_);
                lastStatus_ = err.empty() ? "scan retune blocked" : err;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(std::max(20, cfg.dwellMs)));

            double peakHz = 0.0;
            double peakDb = -200.0;
            if (detectActivity(peakHz, peakDb)) {
                {
                    std::lock_guard<std::mutex> lk(mutex_);
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
                manager.retuneWithLease(cfg.deviceIndex, peakHz,
                                        DeviceManager::DeviceLeaseOwner::Satcom,
                                        true, nullptr);
                pushLog(SatcomLog::EventType::Lock, peakHz, "activity");
            } else {
                std::lock_guard<std::mutex> lk(mutex_);
                currentHz_ += cfg.stepHz;
                if (currentHz_ > cfg.highHz) currentHz_ = cfg.lowHz;
            }
        }

        notifyUpdate();
    }

    {
        std::lock_guard<std::mutex> lk(mutex_);
        state_ = SatcomScannerState::Idle;
        deviceConnected_ = false;
        passTrackActive_ = false;
        passStartedEngine_ = false;
    }

    if (lostHardware) {
        stopRecording();
        finishSstvCapture(false);
        SatPassPlanner::instance().disarm();
        shutdownAudioOutput();
        resetChronologicalInput();
        restorePreviousDeviceState();
        notifyUpdate();
    }
}
