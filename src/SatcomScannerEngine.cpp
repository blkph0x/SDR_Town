#include "SatcomScannerEngine.h"
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
#include <spdlog/spdlog.h>

namespace {

std::string configPath() {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(base);
    return (base + "/satcom_scanner.json").toStdString();
}

DemodMode modeFromString(const std::string& m) {
    if (m == "WFM") return DemodMode::WFM;
    if (m == "AM" || m == "APT") return DemodMode::AM;
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
            if (devices[i].stableKey == cfg.deviceStableKey) { chosen = i; break; }
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
    if (chosen == static_cast<size_t>(-1))
        chosen = cfg.deviceIndex < devices.size() ? cfg.deviceIndex : 0;

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

bool SatcomScannerEngine::start(bool force) {
    std::string err;
    const size_t dev = resolveDeviceIndex(&err);
    if (dev == static_cast<size_t>(-1)) {
        std::lock_guard<std::mutex> lk(mutex_);
        lastStatus_ = err.empty() ? "No receiver selected" : err;
        return false;
    }
    if (!DeviceManager::instance().acquireDeviceLease(dev, DeviceManager::DeviceLeaseOwner::Satcom, force, &err)) {
        std::lock_guard<std::mutex> lk(mutex_);
        lastStatus_ = err;
        return false;
    }
    if (run_.load(std::memory_order_acquire)) return true;
    // Join a previous completed/ending worker before making run_ true again;
    // otherwise an old worker could observe the new true state and never exit.
    if (worker_.joinable()) worker_.join();
    if (run_.exchange(true, std::memory_order_acq_rel)) return true;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        state_ = passTrackActive_ ? SatcomScannerState::Locked : SatcomScannerState::Scanning;
        currentHz_ = passTrackActive_ && lockHz_ > 0.0 ? lockHz_ : config_.lowHz;
        if (!passTrackActive_) lockHz_ = 0.0;
        skipRequested_ = false;
        lastStatus_ = passTrackActive_ ? "Pass armed" : "Scanning";
    }
    pushLog(SatcomLog::EventType::Start, currentHz_, "scan start");
    worker_ = std::thread(&SatcomScannerEngine::workerLoop, this);
    return true;
}

void SatcomScannerEngine::stop() {
    const bool wasRunning = run_.exchange(false);
    if (wasRunning && worker_.joinable()) worker_.join();
    {
        std::lock_guard<std::mutex> lk(mutex_);
        state_ = SatcomScannerState::Idle;
        recording_ = false;
        recordRequested_ = false;
        lastStatus_ = "Stopped";
    }
    DeviceManager::instance().releaseDeviceLease(DeviceManager::DeviceLeaseOwner::Satcom);
    pushLog(SatcomLog::EventType::Stop, currentHz_, "scan stop");
}

void SatcomScannerEngine::skip() {
    {
        std::lock_guard<std::mutex> lk(mutex_);
        skipRequested_ = true;
    }
    demodResetRequested_ = true;
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
    pushLog(SatcomLog::EventType::RecordStart, hz, "record");
    return static_cast<bool>(create);
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
            std::function<void()> cb;
            {
                std::lock_guard<std::mutex> stateLock(mutex_);
                recentDecodes_.push_back(message);
                if (recentDecodes_.size() > 100) recentDecodes_.erase(recentDecodes_.begin());
                lastStatus_ = message;
                cb = updateCb_;
            }
            if (cb) cb();
        } catch (const std::exception& ex) {
            if (sstvCancel_.load(std::memory_order_acquire)) return;
            std::function<void()> cb;
            {
                std::lock_guard<std::mutex> stateLock(mutex_);
                lastStatus_ = std::string("SSTV capture: ") + ex.what();
                recentDecodes_.push_back(lastStatus_);
                if (recentDecodes_.size() > 100) recentDecodes_.erase(recentDecodes_.begin());
                cb = updateCb_;
            }
            if (cb) cb();
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
    auto& deviceManager = DeviceManager::instance();
    std::string err;
    const size_t dev = resolveDeviceIndex(&err);
    if (dev == static_cast<size_t>(-1)) {
        if (error) *error = err.empty() ? "No receiver selected" : err;
        return false;
    }
    const bool streamWasRunning = deviceManager.isStreaming(dev);
    if (!deviceManager.acquireDeviceLease(dev, DeviceManager::DeviceLeaseOwner::Satcom, force, &err)) {
        if (error) *error = err;
        return false;
    }
    if (!SatPassPlanner::instance().arm(satId, downlinkId, autoTrack, &err)) {
        deviceManager.releaseDeviceLease(DeviceManager::DeviceLeaseOwner::Satcom);
        if (error) *error = err;
        return false;
    }
    const auto snap = SatPassPlanner::instance().snapshot();
    const std::string stem = makeCaptureStem(satId, snap.armed.downlinkId);
    {
        std::lock_guard<std::mutex> lk(mutex_);
        passTrackActive_ = true;
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
        lastStatus_ = "Pass armed";
        if (run_.load(std::memory_order_acquire)) state_ = SatcomScannerState::Locked;
        config_.save();
    }
    demodResetRequested_ = true;

    // Arm Pass owns the complete receiver transition. Start the selected
    // stream before queuing the tune so both manual and automatic paths enter
    // the same locked/decode state instead of merely changing planner flags.
    if (!deviceManager.setEnabled(dev, true) || !deviceManager.startStreaming(dev, true)) {
        const std::string startError = "Could not start selected satellite receiver";
        {
            std::lock_guard<std::mutex> lk(mutex_);
            passTrackActive_ = false;
            armedRole_.clear();
            state_ = SatcomScannerState::Idle;
            lastStatus_ = startError;
        }
        SatPassPlanner::instance().disarm();
        deviceManager.releaseDeviceLease(DeviceManager::DeviceLeaseOwner::Satcom);
        if (!streamWasRunning) deviceManager.stopStreaming(dev);
        if (error) *error = startError;
        return false;
    }

    // Always issue the base-frequency tune, even when Doppler auto-track is
    // disabled. Previously tickPassTrack() returned early in that mode, leaving
    // an apparently armed pass on the receiver's old frequency.
    if (!deviceManager.retuneWithLease(dev, snap.armed.freqHz,
                                       DeviceManager::DeviceLeaseOwner::Satcom,
                                       true, &err)) {
        const std::string tuneError = err.empty() ? "Could not tune pass receiver" : err;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            passTrackActive_ = false;
            armedRole_.clear();
            state_ = run_.load(std::memory_order_acquire)
                ? SatcomScannerState::Scanning
                : SatcomScannerState::Idle;
            lastStatus_ = tuneError;
        }
        SatPassPlanner::instance().disarm();
        deviceManager.releaseDeviceLease(DeviceManager::DeviceLeaseOwner::Satcom);
        if (!streamWasRunning) deviceManager.stopStreaming(dev);
        if (error) *error = tuneError;
        return false;
    }

    // Enter the locked pass path before the worker starts. This avoids a race
    // where the old UI started a band scan first and only armed the pass later.
    if (!run_.load(std::memory_order_acquire) && !start(force)) {
        std::string startError;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            startError = lastStatus_;
            passTrackActive_ = false;
            armedRole_.clear();
            state_ = SatcomScannerState::Idle;
            lastStatus_ = startError.empty() ? "Could not start pass receiver" : startError;
        }
        SatPassPlanner::instance().disarm();
        deviceManager.releaseDeviceLease(DeviceManager::DeviceLeaseOwner::Satcom);
        if (!streamWasRunning) deviceManager.stopStreaming(dev);
        if (error) *error = startError.empty() ? "Could not start pass receiver" : startError;
        return false;
    }

    pushLog(SatcomLog::EventType::Lock, snap.armed.freqHz, "pass arm");
    tickPassTrack();
    if (snap.armed.role == "sstv") startSstvCapture(satId, snap.armed.downlinkId);
    return true;
}

void SatcomScannerEngine::disarmPass() {
    stopRecording();
    finishSstvCapture(false);
    SatPassPlanner::instance().disarm();
    const bool running = run_.load();
    {
        std::lock_guard<std::mutex> lk(mutex_);
        passTrackActive_ = false;
        armedRole_.clear();
        lastStatus_ = "Pass disarmed";
    }
    demodResetRequested_ = true;
    if (!running)
        DeviceManager::instance().releaseDeviceLease(DeviceManager::DeviceLeaseOwner::Satcom);
}

void SatcomScannerEngine::setAutoTrack(bool on) {
    SatPassPlanner::instance().setAutoTrack(on);
}

void SatcomScannerEngine::tickPassTrack() {
    double tuned = 0.0;
    if (!SatPassPlanner::instance().tickAutoTrack(&tuned)) {
        const bool stillArmed = SatPassPlanner::instance().snapshot().armed.armed;
        bool ended = false;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            if (!stillArmed && passTrackActive_) {
                passTrackActive_ = false;
                ended = true;
                lastStatus_ = "Pass ended";
            }
        }
        if (ended) {
            stopRecording();
            finishSstvCapture(false);
            demodResetRequested_ = true;
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
    if (!DeviceManager::instance().retuneWithLease(dev, tuned, DeviceManager::DeviceLeaseOwner::Satcom, true, &err)) {
        std::lock_guard<std::mutex> lk(mutex_);
        lastStatus_ = err.empty() ? "auto-track retune blocked" : err;
    }
}

bool SatcomScannerEngine::detectActivity(double& peakHz, double& peakDb) {
    auto& mgr = DeviceManager::instance();
    size_t dev = 0;
    double sq = -90.0;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        dev = config_.deviceIndex;
        sq = config_.squelchDb;
    }
    std::vector<float> power;
    double cf = 0.0, sr = 0.0;
    if (!mgr.getLatestSpectrum(dev, power, cf, sr) || power.empty() || sr <= 0.0)
        return false;

    {
        std::lock_guard<std::mutex> lk(mutex_);
        spectrumDb_ = power;
        if (spectrumDb_.size() > 256) {
            std::vector<float> ds(256, -120.0f);
            for (size_t i = 0; i < 256; ++i) {
                size_t a = i * power.size() / 256;
                size_t b = (i + 1) * power.size() / 256;
                float m = -200.0f;
                for (size_t k = a; k < b && k < power.size(); ++k) m = std::max(m, power[k]);
                ds[i] = m;
            }
            spectrumDb_ = std::move(ds);
        }
        spectrumCenterHz_ = cf;
        spectrumRateHz_ = sr;
    }

    size_t peakIdx = 0;
    float peak = power[0];
    for (size_t i = 1; i < power.size(); ++i) {
        if (power[i] > peak) { peak = power[i]; peakIdx = i; }
    }
    peakDb = peak;
    const double binHz = sr / static_cast<double>(power.size());
    peakHz = cf - sr * 0.5 + (static_cast<double>(peakIdx) + 0.5) * binHz;
    return peak >= sq;
}

void SatcomScannerEngine::processLockedAudio() {
    auto& mgr = DeviceManager::instance();
    size_t dev = 0;
    std::string mode;
    std::string role;
    bool doAx = true, doApt = true;
    double lockHz = 0.0;
    double bw = 12.5e3;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        dev = config_.deviceIndex;
        mode = config_.mode;
        role = armedRole_;
        doAx = config_.enableAx25;
        doApt = config_.enableApt;
        lockHz = lockHz_;
        bw = std::min(config_.bandwidthHz, 50e3);
    }

    auto iq = mgr.getRecentIQWindow(dev, 65536);
    if (iq.size() < 1024) return;

    double sr = 2.048e6;
    {
        const auto devs = mgr.getDevices();
        if (dev < devs.size() && devs[dev].sampleRate > 0) sr = devs[dev].sampleRate;
    }

    if (demodResetRequested_.exchange(false)) demod_->resetState();
    const double reportedCenter = mgr.getCurrentCenterFreq(dev);
    const double iqCenterHz = reportedCenter > 0.0 ? reportedCenter : lockHz;
    double rms = -120.0;
    FmMultiplexBlock multiplex;
    FmMultiplexBlock* multiplexOut = role == "sstv" ? &multiplex : nullptr;
    auto audio = demod_->demodulateToAudio(iq, sr, iqCenterHz, lockHz, modeFromString(mode),
                                           rms, 3000.0, -120.0, 1.0, 75.0, 0.96, bw,
                                           0, 48000.0,
                                           std::numeric_limits<double>::quiet_NaN(), true,
                                           multiplexOut, lockHz);
    {
        std::lock_guard<std::mutex> lk(mutex_);
        audioRmsDb_ = rms;
    }

    if (multiplexOut && !multiplex.samples.empty())
        sstvFeed_->publish(multiplex, static_cast<uint64_t>(dev + 1));
    if (audio.empty()) return;

    if (doAx && (mode == "NFM" || mode == "APRS")) {
        auto frames = ax25_->processAudio(audio.data(), audio.size(), 48000.0);
        for (auto& f : frames) {
            pushLog(SatcomLog::EventType::DecodeOk, lockHz, f.c_str());
            std::lock_guard<std::mutex> lk(mutex_);
            recentDecodes_.push_back(f);
            if (recentDecodes_.size() > 100)
                recentDecodes_.erase(recentDecodes_.begin());
        }
    }

    if (doApt && (mode == "APT" || mode == "AM")) {
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
            }
        }
    }

    bool rec = false;
    std::string path;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        rec = recording_;
        path = recordPath_;
    }
    if (rec && !path.empty()) {
        QDir().mkpath(QFileInfo(QString::fromStdString(path)).absolutePath());
        std::ofstream out(path, std::ios::binary | std::ios::app);
        if (out) out.write(reinterpret_cast<const char*>(audio.data()),
                           static_cast<std::streamsize>(audio.size() * sizeof(float)));
    }
}

void SatcomScannerEngine::workerLoop() {
    auto& mgr = DeviceManager::instance();
    {
        std::lock_guard<std::mutex> lk(mutex_);
        const auto devs = mgr.getDevices();
        if (config_.deviceIndex < devs.size()) {
            deviceLabel_ = devs[config_.deviceIndex].label;
            deviceConnected_ = true;
        } else {
            deviceLabel_ = "no device";
            deviceConnected_ = false;
            lastStatus_ = "No device";
            run_ = false;
            state_ = SatcomScannerState::Idle;
            return;
        }
        if (!passTrackActive_) currentHz_ = config_.lowHz;
    }

    if (!mgr.setEnabled(config().deviceIndex, true) ||
        !mgr.startStreaming(config().deviceIndex, true)) {
        std::lock_guard<std::mutex> lk(mutex_);
        deviceConnected_ = false;
        lastStatus_ = "Could not start selected receiver";
        state_ = SatcomScannerState::Idle;
        run_ = false;
        mgr.releaseDeviceLease(DeviceManager::DeviceLeaseOwner::Satcom);
        return;
    }

    while (run_.load(std::memory_order_acquire)) {
        SatcomScannerConfig cfg;
        bool skip = false;
        SatcomScannerState st;
        bool passHold = false;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            cfg = config_;
            skip = skipRequested_;
            skipRequested_ = false;
            st = state_;
            passHold = passTrackActive_;
        }

        if (passHold) {
            tickPassTrack();
            processLockedAudio();
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            continue;
        }

        if (st == SatcomScannerState::Locked || st == SatcomScannerState::Recording) {
            if (skip) {
                pushLog(SatcomLog::EventType::Unlock, lockHz_, "skip unlock");
                ax25_->reset();
                demodResetRequested_ = true;
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
            if (!mgr.retuneWithLease(cfg.deviceIndex, tune, DeviceManager::DeviceLeaseOwner::Satcom, true, &err)) {
                std::lock_guard<std::mutex> lk(mutex_);
                lastStatus_ = err.empty() ? "scan retune blocked" : err;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(std::max(20, cfg.dwellMs)));

            double peakHz = 0.0, peakDb = -200.0;
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
                    ax25_->reset();
                    if (cfg.mode == "APT" || cfg.mode == "AM") apt_->reset();
                }
                demodResetRequested_ = true;
                mgr.retuneWithLease(cfg.deviceIndex, peakHz, DeviceManager::DeviceLeaseOwner::Satcom, true, nullptr);
                pushLog(SatcomLog::EventType::Lock, peakHz, "activity");
            } else {
                std::lock_guard<std::mutex> lk(mutex_);
                currentHz_ += cfg.stepHz;
                if (currentHz_ > cfg.highHz) currentHz_ = cfg.lowHz;
            }
        }

        std::function<void()> cb;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            cb = updateCb_;
        }
        if (cb) {
            try { cb(); } catch (...) {}
        }
    }

    std::lock_guard<std::mutex> lk(mutex_);
    state_ = SatcomScannerState::Idle;
}
