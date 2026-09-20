#include "InmarsatEngine.h"
#include "InmarsatAcars.h"
#include "InmarsatVoice.h"
#include "AdsBTrackStore.h"
#include "DeviceManager.h"
#include "Receiver.h"

#include <QDir>
#include <QStandardPaths>

#include <chrono>
#include <cmath>
#include <fstream>
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

} // namespace

InmarsatEngineConfig InmarsatEngineConfig::defaults() {
    InmarsatEngineConfig c;
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    c.recordDir = (base + "/inmarsat_recordings").toStdString();
    return c;
}

nlohmann::json InmarsatEngineConfig::toJson() const {
    return {
        {"deviceIndex", deviceIndex},
        {"bandPlanId", bandPlanId},
        {"channelHz", channelHz},
        {"mode", mode},
        {"baud", baud},
        {"voiceFollow", voiceFollow},
        {"recordVoice", recordVoice},
        {"recordDir", recordDir},
    };
}

InmarsatEngineConfig InmarsatEngineConfig::fromJson(const nlohmann::json& j) {
    InmarsatEngineConfig c = defaults();
    if (!j.is_object()) return c;
    c.deviceIndex = j.value("deviceIndex", c.deviceIndex);
    c.bandPlanId = j.value("bandPlanId", c.bandPlanId);
    c.channelHz = j.value("channelHz", c.channelHz);
    c.mode = j.value("mode", c.mode);
    c.baud = j.value("baud", c.baud);
    c.voiceFollow = j.value("voiceFollow", c.voiceFollow);
    c.recordVoice = j.value("recordVoice", c.recordVoice);
    c.recordDir = j.value("recordDir", c.recordDir);
    return c;
}

void InmarsatEngineConfig::load() {
    try {
        std::ifstream in(configPath());
        if (!in) {
            *this = defaults();
            return;
        }
        nlohmann::json j;
        in >> j;
        *this = fromJson(j);
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
    static InmarsatEngine eng;
    return eng;
}

InmarsatEngine::InmarsatEngine() {
    config_.load();
    acars_ = std::make_unique<InmarsatAcars>();
    voice_ = std::make_unique<InmarsatVoice>();
    InmarsatBandPlanStore::instance().reload(nullptr);
    if (const auto* p = InmarsatBandPlanStore::instance().findById(config_.bandPlanId)) {
        bandPlanName_ = p->name;
        if (!p->channels.empty() && config_.channelHz <= 0) {
            config_.channelHz = p->channels.front().freqHz;
            config_.mode = p->channels.front().mode;
            config_.baud = p->channels.front().baud;
        }
    }
    acars_->setSink([this](const InmarsatMessage& m) { onMessage(m); });
    demod_.setByteSink([this](const uint8_t* d, size_t n) { onDecodedBytes(d, n); });
    iqRx_ = std::make_unique<Receiver>();
    lastStatus_ = "experimental prototype — no unique-word/FEC; not RF-qualified";
}

InmarsatEngine::~InmarsatEngine() { stop(); }

void InmarsatEngine::setUpdateCallback(std::function<void()> cb) {
    std::lock_guard<std::mutex> lk(mutex_);
    updateCb_ = std::move(cb);
}

void InmarsatEngine::notify() {
    std::function<void()> cb;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        cb = updateCb_;
    }
    if (cb) cb();
}

void InmarsatEngine::setConfig(const InmarsatEngineConfig& cfg) {
    std::lock_guard<std::mutex> lk(mutex_);
    config_ = cfg;
    config_.save();
    if (voice_) voice_->setRecording(config_.recordVoice, config_.recordDir, 0);
}

InmarsatEngineConfig InmarsatEngine::config() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return config_;
}

InmarsatDemodMode InmarsatEngine::demodModeLocked() const {
    const bool egc = (config_.mode == "egc");
    return InmarsatDemod::modeFromBaud(config_.baud, egc);
}

bool InmarsatEngine::selectBandPlan(const std::string& id) {
    const auto* p = InmarsatBandPlanStore::instance().findById(id);
    if (!p) return false;
    std::lock_guard<std::mutex> lk(mutex_);
    config_.bandPlanId = p->id;
    bandPlanName_ = p->name;
    if (!p->channels.empty()) {
        // Prefer first aero_oqpsk if present
        const InmarsatChannel* pick = &p->channels.front();
        for (const auto& c : p->channels) {
            if (c.mode == "aero_oqpsk") {
                pick = &c;
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
    lastStatus_ = "Band plan " + p->name;
    return true;
}

bool InmarsatEngine::selectChannel(double freqHz, const std::string& mode, int baud) {
    std::lock_guard<std::mutex> lk(mutex_);
    config_.channelHz = freqHz;
    if (!mode.empty()) config_.mode = mode;
    if (baud > 0) config_.baud = baud;
    tunedHz_ = freqHz;
    if (!followingVoice_) controlHz_ = freqHz;
    config_.save();
    lastStatus_ = "Channel selected";
    return true;
}

bool InmarsatEngine::start(bool force) {
    std::string err;
    const size_t dev = config().deviceIndex;
    if (!DeviceManager::instance().acquireDeviceLease(dev, DeviceManager::DeviceLeaseOwner::Inmarsat, force, &err)) {
        std::lock_guard<std::mutex> lk(mutex_);
        lastStatus_ = err;
        notify();
        return false;
    }
    if (run_.exchange(true)) return true;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        state_ = InmarsatEngineState::Running;
        tunedHz_ = config_.channelHz;
        controlHz_ = config_.channelHz;
        followingVoice_ = false;
        lastStatus_ = "Running (experimental prototype, no FEC)";
        QDir().mkpath(QString::fromStdString(config_.recordDir));
        if (voice_) voice_->setRecording(config_.recordVoice, config_.recordDir, 0);
        demod_.reset(demodModeLocked(), 2.048e6, 0.0);
        if (iqRx_) {
            iqRx_->deviceIndex = config_.deviceIndex;
            iqRx_->lastConsumedAbsolute.store(0, std::memory_order_release);
        }
    }
    auto& mgr = DeviceManager::instance();
    mgr.setEnabled(dev, true);
    mgr.startStreaming(dev, true);
    mgr.retuneWithLease(dev, config().channelHz, DeviceManager::DeviceLeaseOwner::Inmarsat, true, nullptr);
    if (iqRx_) mgr.setReceiverCursorToLiveEdge(dev, *iqRx_);
    worker_ = std::thread(&InmarsatEngine::workerLoop, this);
    notify();
    return true;
}

void InmarsatEngine::stop() {
    if (!run_.exchange(false)) return;
    if (worker_.joinable()) worker_.join();
    {
        std::lock_guard<std::mutex> lk(mutex_);
        state_ = InmarsatEngineState::Idle;
        followingVoice_ = false;
        lastStatus_ = "Stopped";
        if (voice_) voice_->setRecording(false, {}, 0);
    }
    DeviceManager::instance().releaseDeviceLease(DeviceManager::DeviceLeaseOwner::Inmarsat);
    notify();
}

std::string InmarsatEngine::stateName() const {
    std::lock_guard<std::mutex> lk(mutex_);
    switch (state_) {
    case InmarsatEngineState::Running: return "running";
    case InmarsatEngineState::VoiceFollow: return "voice_follow";
    case InmarsatEngineState::Idle:
    default: return "idle";
    }
}

InmarsatEngineSnapshot InmarsatEngine::snapshot() const {
    InmarsatEngineSnapshot s;
    std::lock_guard<std::mutex> lk(mutex_);
    s.state = state_;
    s.config = config_;
    s.locked = locked_;
    s.ebnoDb = ebnoDb_;
    s.tunedHz = tunedHz_;
    s.controlHz = controlHz_;
    s.voiceHz = voiceHz_;
    s.followingVoice = followingVoice_;
    s.recording = config_.recordVoice && voice_ && voice_->recording();
    s.messages = messages_;
    s.voiceFrames = voice_ ? voice_->framesDecoded() : voiceFrames_;
    s.bandPlanName = bandPlanName_;
    s.lastStatus = lastStatus_;
    s.spectrumDb = spectrumDb_;
    s.spectrumCenterHz = spectrumCenterHz_;
    s.spectrumRateHz = spectrumRateHz_;
    s.recentLines = recentLines_;
    return s;
}

nlohmann::json InmarsatEngine::statusJson() const {
    const auto snap = snapshot();
    nlohmann::json j;
    j["state"] = stateName();
    j["locked"] = snap.locked;
    j["ebnoDb"] = snap.ebnoDb;
    j["tunedHz"] = snap.tunedHz;
    j["tunedMHz"] = snap.tunedHz / 1e6;
    j["controlHz"] = snap.controlHz;
    j["controlMHz"] = snap.controlHz / 1e6;
    j["voiceHz"] = snap.voiceHz;
    j["voiceMHz"] = snap.voiceHz / 1e6;
    j["followingVoice"] = snap.followingVoice;
    j["recording"] = snap.recording;
    j["messages"] = snap.messages;
    j["voiceFrames"] = snap.voiceFrames;
    j["bandPlanId"] = snap.config.bandPlanId;
    j["bandPlanName"] = snap.bandPlanName;
    j["lastStatus"] = snap.lastStatus;
    j["config"] = snap.config.toJson();
    j["spectrumCenterHz"] = snap.spectrumCenterHz;
    j["spectrumRateHz"] = snap.spectrumRateHz;
    j["spectrumDb"] = snap.spectrumDb;
    j["recentLines"] = snap.recentLines;
    j["voiceBackend"] = voice_ && voice_->backendAvailable();
    j["experimental"] = true;
    j["rfQualified"] = false;
    j["note"] = "Prototype slicer only — no unique-word, FEC, or verified Aero AMBE mapping";
    return j;
}

void InmarsatEngine::onDecodedBytes(const uint8_t* data, size_t len) {
    if (!data || len == 0) return;
    double freq = 0;
    std::string mode;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        freq = tunedHz_;
        mode = config_.mode;
    }
    if (mode == "aero_voice" || followingVoice_) {
        if (voice_) voice_->feedBytes(data, len);
    }
    if (acars_) acars_->feedBytes(data, len, freq);
}

void InmarsatEngine::onMessage(const InmarsatMessage& msg) {
    InmarsatMessageStore::instance().push(msg);
    {
        std::lock_guard<std::mutex> lk(mutex_);
        ++messages_;
        std::string line = InmarsatMessage::kindName(msg.kind);
        line += " ";
        if (!msg.label.empty()) {
            line += msg.label;
            line += " ";
        }
        line += msg.text.substr(0, 120);
        recentLines_.push_back(line);
        if (recentLines_.size() > 40) recentLines_.erase(recentLines_.begin());
        lastStatus_ = line.substr(0, 80);
    }

    if (msg.hasPosition && msg.aesId != 0) {
        AdsBTrackStore::instance().ingestAdscPosition(msg.aesId, msg.latDeg, msg.lonDeg, msg.icaoHex,
                                                      msg.text.substr(0, 40));
    }

    if (msg.kind == InmarsatMsgKind::CAssign) applyVoiceFollow(msg);
    notify();
}

void InmarsatEngine::applyVoiceFollow(const InmarsatMessage& msg) {
    bool follow = false;
    size_t dev = 0;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        follow = config_.voiceFollow;
        dev = config_.deviceIndex;
        if (!follow || msg.voiceRxHz <= 0) return;
        if (!followingVoice_) controlHz_ = tunedHz_ > 0 ? tunedHz_ : config_.channelHz;
        followingVoice_ = true;
        state_ = InmarsatEngineState::VoiceFollow;
        voiceHz_ = msg.voiceRxHz;
        tunedHz_ = msg.voiceRxHz;
        config_.mode = "aero_voice";
        config_.baud = 8400;
        voiceFollowUntilUnix_ = unixNow() + 90.0;
        lastStatus_ = "Voice follow";
        if (voice_) voice_->setRecording(config_.recordVoice, config_.recordDir, msg.aesId);
        demod_.reset(InmarsatDemodMode::AeroVoice8400, 2.048e6, 0.0);
    }
    try {
        DeviceManager::instance().retuneWithLease(dev, msg.voiceRxHz, DeviceManager::DeviceLeaseOwner::Inmarsat, true, nullptr);
    } catch (...) {
    }
}

void InmarsatEngine::returnToControl() {
    size_t dev = 0;
    double hz = 0;
    std::string mode;
    int baud = 10500;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (!followingVoice_) return;
        followingVoice_ = false;
        state_ = InmarsatEngineState::Running;
        hz = controlHz_ > 0 ? controlHz_ : config_.channelHz;
        tunedHz_ = hz;
        config_.mode = "aero_oqpsk";
        config_.baud = 10500;
        mode = config_.mode;
        baud = config_.baud;
        dev = config_.deviceIndex;
        lastStatus_ = "Returned to control";
        demod_.reset(demodModeLocked(), 2.048e6, 0.0);
    }
    (void)mode;
    (void)baud;
    try {
        DeviceManager::instance().retuneWithLease(dev, hz, DeviceManager::DeviceLeaseOwner::Inmarsat, true, nullptr);
    } catch (...) {
    }
}

void InmarsatEngine::processIq() {
    // Never touch IQ unless the user started the Inmarsat engine.
    if (!run_.load()) return;
    size_t dev = 0;
    double tuned = 0;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        dev = config_.deviceIndex;
        tuned = tunedHz_ > 0 ? tunedHz_ : config_.channelHz;
    }
    auto& mgr = DeviceManager::instance();
    std::vector<float> power;
    double cf = 0, sr = 0;
    if (mgr.getLatestSpectrum(dev, power, cf, sr) && !power.empty()) {
        std::lock_guard<std::mutex> lk(mutex_);
        spectrumDb_ = power;
        if (spectrumDb_.size() > 256) {
            std::vector<float> ds(256, -120.f);
            for (size_t i = 0; i < 256; ++i) {
                size_t a = i * power.size() / 256;
                size_t b = (i + 1) * power.size() / 256;
                float m = -200.f;
                for (size_t k = a; k < b && k < power.size(); ++k) m = std::max(m, power[k]);
                ds[i] = m;
            }
            spectrumDb_ = std::move(ds);
        }
        spectrumCenterHz_ = cf;
        spectrumRateHz_ = sr;
    }

    double sampleRate = (sr > 0.0) ? sr : 2.048e6;
    {
        const auto devs = mgr.getDevices();
        if (dev < devs.size() && devs[dev].sampleRate > 0) sampleRate = devs[dev].sampleRate;
    }
    std::vector<std::complex<float>> iq;
    bool gap = false;
    if (iqRx_) {
        auto window = mgr.getNewIQWindowForReceiver(dev, *iqRx_, 65536);
        iq = std::move(window.samples);
        gap = window.cursorDiscontinuity;
    } else {
        iq = mgr.getRecentIQWindow(dev, 65536);
    }
    if (iq.size() < 256) return;
    const double offset = (cf > 0.0) ? (tuned - cf) : 0.0;
    if (gap) {
        demod_.reset(demodModeLocked(), sampleRate, offset);
    }
    {
        static thread_local double lastSr = 0.0;
        static thread_local InmarsatDemodMode lastMode = InmarsatDemodMode::AeroOqpsk10500;
        const auto mode = demodModeLocked();
        if (std::abs(sampleRate - lastSr) > 1.0 || mode != lastMode) {
            demod_.reset(mode, sampleRate, offset);
            lastSr = sampleRate;
            lastMode = mode;
        } else {
            demod_.setChannelOffset(offset);
        }
    }
    demod_.process(iq.data(), iq.size());

    const auto st = demod_.stats();
    {
        std::lock_guard<std::mutex> lk(mutex_);
        locked_ = st.locked;
        ebnoDb_ = st.ebnoDb;
        if (voice_) voiceFrames_ = voice_->framesDecoded();
        if (followingVoice_ && unixNow() > voiceFollowUntilUnix_) {
            // schedule return outside lock
        }
    }
    if (followingVoice_ && unixNow() > voiceFollowUntilUnix_) returnToControl();
}

void InmarsatEngine::workerLoop() {
    while (run_.load()) {
        try {
            processIq();
        } catch (const std::exception& ex) {
            spdlog::warn("InmarsatEngine: {}", ex.what());
            std::lock_guard<std::mutex> lk(mutex_);
            lastStatus_ = ex.what();
        } catch (...) {
            std::lock_guard<std::mutex> lk(mutex_);
            lastStatus_ = "IQ process error";
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }
}
