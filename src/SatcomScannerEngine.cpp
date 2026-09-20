#include "SatcomScannerEngine.h"
#include "Ax25AprsDecoder.h"
#include "AptImageDecoder.h"
#include "DeviceManager.h"
#include "Demod.h"
#include "SatPassPlanner.h"

#include <QStandardPaths>
#include <QDir>

#include <algorithm>
#include <chrono>
#include <cmath>
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
    return DemodMode::NFM; // NFM / APRS
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
    j["recordDir"] = recordDir;
    j["logDir"] = logDir;
    j["enableAx25"] = enableAx25;
    j["enableApt"] = enableApt;
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
    c.recordDir = j.value("recordDir", c.recordDir);
    c.logDir = j.value("logDir", c.logDir);
    c.enableAx25 = j.value("enableAx25", c.enableAx25);
    c.enableApt = j.value("enableApt", c.enableApt);
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
    log_.setLogDirectory(config_.logDir);
    log_.start();
}

SatcomScannerEngine::~SatcomScannerEngine() {
    stop();
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

void SatcomScannerEngine::setUpdateCallback(std::function<void()> cb) {
    std::lock_guard<std::mutex> lk(mutex_);
    updateCb_ = std::move(cb);
}

void SatcomScannerEngine::pushLog(SatcomLog::EventType t, double hz, const char* text) {
    log_.tryPush(t, hz, text);
}

bool SatcomScannerEngine::start(bool force) {
    std::string err;
    const size_t dev = config().deviceIndex;
    if (!DeviceManager::instance().acquireDeviceLease(dev, DeviceManager::DeviceLeaseOwner::Satcom, force, &err)) {
        std::lock_guard<std::mutex> lk(mutex_);
        lastStatus_ = err;
        return false;
    }
    if (run_.exchange(true)) return true;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        state_ = SatcomScannerState::Scanning;
        currentHz_ = config_.lowHz;
        lockHz_ = 0.0;
        skipRequested_ = false;
        lastStatus_ = "Scanning";
    }
    pushLog(SatcomLog::EventType::Start, config_.lowHz, "scan start");
    worker_ = std::thread(&SatcomScannerEngine::workerLoop, this);
    return true;
}

void SatcomScannerEngine::stop() {
    if (!run_.exchange(false)) return;
    if (worker_.joinable()) worker_.join();
    {
        std::lock_guard<std::mutex> lk(mutex_);
        state_ = SatcomScannerState::Idle;
        recording_ = false;
        lastStatus_ = "Stopped";
    }
    DeviceManager::instance().releaseDeviceLease(DeviceManager::DeviceLeaseOwner::Satcom);
    pushLog(SatcomLog::EventType::Stop, currentHz_, "scan stop");
}

void SatcomScannerEngine::skip() {
    std::lock_guard<std::mutex> lk(mutex_);
    skipRequested_ = true;
    pushLog(SatcomLog::EventType::Skip, lockHz_ > 0 ? lockHz_ : currentHz_, "skip");
}

bool SatcomScannerEngine::startRecording() {
    std::lock_guard<std::mutex> lk(mutex_);
    if (state_ != SatcomScannerState::Locked && state_ != SatcomScannerState::Recording)
        return false;
    recordRequested_ = true;
    recording_ = true;
    recordHz_ = lockHz_;
    state_ = SatcomScannerState::Recording;
    pushLog(SatcomLog::EventType::RecordStart, recordHz_, "record");
    return true;
}

void SatcomScannerEngine::stopRecording() {
    std::lock_guard<std::mutex> lk(mutex_);
    recording_ = false;
    recordRequested_ = false;
    if (state_ == SatcomScannerState::Recording) state_ = SatcomScannerState::Locked;
    pushLog(SatcomLog::EventType::RecordStop, recordHz_, "record stop");
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
    s.logWritten = log_.eventsWritten();
    s.logDropped = log_.eventsDropped();
    s.lastStatus = lastStatus_;
    const auto arm = SatPassPlanner::instance().snapshot().armed;
    s.passArmed = arm.armed;
    s.autoTrack = arm.autoTrack;
    s.dopplerHz = arm.dopplerHz;
    s.tunedHz = arm.tunedHz;
    s.armedRole = arm.role;
    return s;
}

bool SatcomScannerEngine::armPass(const std::string& satId, const std::string& downlinkId, bool autoTrack,
                                  bool force, std::string* error) {
    std::string err;
    const size_t dev = config().deviceIndex;
    if (!DeviceManager::instance().acquireDeviceLease(dev, DeviceManager::DeviceLeaseOwner::Satcom, force, &err)) {
        if (error) *error = err;
        return false;
    }
    if (!SatPassPlanner::instance().arm(satId, downlinkId, autoTrack, &err)) {
        if (error) *error = err;
        return false;
    }
    const auto snap = SatPassPlanner::instance().snapshot();
    {
        std::lock_guard<std::mutex> lk(mutex_);
        passTrackActive_ = true;
        config_.mode = snap.armed.mode.empty() ? "NFM" : snap.armed.mode;
        if (config_.mode == "sstv" || snap.armed.role == "sstv") config_.mode = "NFM";
        if (snap.armed.role == "apt") config_.mode = "APT";
        if (snap.armed.role == "aprs") config_.mode = "APRS";
        currentHz_ = snap.armed.freqHz;
        lockHz_ = snap.armed.freqHz;
        lastTrackHz_ = 0.0;
        lastStatus_ = "Pass armed";
        // Pause band scan worker into locked-style hold if running
        if (run_.load()) state_ = SatcomScannerState::Locked;
        config_.save();
    }
    pushLog(SatcomLog::EventType::Lock, snap.armed.freqHz, "pass arm");
    tickPassTrack();
    return true;
}

void SatcomScannerEngine::disarmPass() {
    SatPassPlanner::instance().disarm();
    bool running = run_.load();
    {
        std::lock_guard<std::mutex> lk(mutex_);
        passTrackActive_ = false;
        lastStatus_ = "Pass disarmed";
    }
    if (!running)
        DeviceManager::instance().releaseDeviceLease(DeviceManager::DeviceLeaseOwner::Satcom);
}

void SatcomScannerEngine::setAutoTrack(bool on) {
    SatPassPlanner::instance().setAutoTrack(on);
}

void SatcomScannerEngine::tickPassTrack() {
    double tuned = 0.0;
    if (!SatPassPlanner::instance().tickAutoTrack(&tuned)) {
        std::lock_guard<std::mutex> lk(mutex_);
        if (!SatPassPlanner::instance().snapshot().armed.armed)
            passTrackActive_ = false;
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
        // Downsample for API if huge
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
    bool doAx = true, doApt = true;
    double lockHz = 0.0;
    double bw = 12.5e3;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        dev = config_.deviceIndex;
        mode = config_.mode;
        doAx = config_.enableAx25;
        doApt = config_.enableApt;
        lockHz = lockHz_;
        bw = std::min(config_.bandwidthHz, 25e3);
    }

    auto iq = mgr.getRecentIQWindow(dev, 65536);
    if (iq.size() < 1024) return;

    double sr = 2.048e6;
    {
        const auto devs = mgr.getDevices();
        if (dev < devs.size() && devs[dev].sampleRate > 0) sr = devs[dev].sampleRate;
    }

    double rms = -120.0;
    Demodulator demod;
    auto audio = demod.demodulateToAudio(iq, sr, lockHz, lockHz, modeFromString(mode),
                                         rms, 3000.0, -120.0, 1.0, 75.0, 0.96, bw,
                                         0, 48000.0);
    {
        std::lock_guard<std::mutex> lk(mutex_);
        audioRmsDb_ = rms;
    }

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
            const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
            QDir().mkpath(base + "/satcom_apt");
            const std::string path = (base + "/satcom_apt/preview.pgm").toStdString();
            if (apt_->writePgm(path)) {
                std::lock_guard<std::mutex> lk(mutex_);
                aptPreviewPath_ = path;
            }
        }
    }

    // Optional PCM record (append float dump) — keep simple for v1
    bool rec = false;
    std::string recDir;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        rec = recording_;
        recDir = config_.recordDir;
    }
    if (rec && !recDir.empty()) {
        QDir().mkpath(QString::fromStdString(recDir));
        const std::string path = recDir + "/satcom_lock.f32";
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
        currentHz_ = config_.lowHz;
    }

    mgr.setEnabled(config().deviceIndex, true);
    mgr.startStreaming(config().deviceIndex, true);

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
            // Scanning
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
                    ax25_->reset();
                    if (cfg.mode == "APT" || cfg.mode == "AM") apt_->reset();
                }
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
