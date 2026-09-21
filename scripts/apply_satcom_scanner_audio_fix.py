from pathlib import Path


def read(path: str) -> str:
    return Path(path).read_text(encoding="utf-8-sig")


def write(path: str, text: str) -> None:
    Path(path).write_text(text, encoding="utf-8", newline="\n")


def replace_once(path: str, old: str, new: str) -> None:
    text = read(path)
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected exactly one match, found {count}\n--- needle ---\n{old}")
    write(path, text.replace(old, new, 1))


def replace_between(path: str, start: str, end: str, replacement: str) -> None:
    text = read(path)
    if text.count(start) != 1:
        raise RuntimeError(f"{path}: start marker count={text.count(start)}: {start}")
    start_pos = text.index(start)
    end_pos = text.index(end, start_pos + len(start))
    write(path, text[:start_pos] + replacement + text[end_pos:])


# AudioEngine: expose the process's first playback engine so feature workers can
# share the configured/default output instead of silently discarding PCM or
# opening a competing output device.
replace_once(
    "include/AudioEngine.h",
    "    AudioEngine();\n    ~AudioEngine();\n\n    // Enumeration",
    "    AudioEngine();\n    ~AudioEngine();\n\n    // First live AudioEngine in this process. Feature workers use this shared\n    // playback path; nullptr means no engine has been constructed yet.\n    static AudioEngine* primaryInstance() noexcept;\n\n    // Enumeration",
)
replace_once(
    "include/AudioEngine.h",
    "private:\n    static constexpr uint8_t kRingSampleReal = 0;",
    "private:\n    static std::atomic<AudioEngine*> s_primaryInstance;\n\n    static constexpr uint8_t kRingSampleReal = 0;",
)
replace_once(
    "src/AudioEngine.cpp",
    "}\n\nstatic void data_callback",
    "}\n\nstd::atomic<AudioEngine*> AudioEngine::s_primaryInstance{nullptr};\n\nstatic void data_callback",
)
replace_once(
    "src/AudioEngine.cpp",
    "AudioEngine::AudioEngine()\n{\n    ma_context_config ctxCfg = ma_context_config_init();",
    "AudioEngine::AudioEngine()\n{\n    AudioEngine* expected = nullptr;\n    s_primaryInstance.compare_exchange_strong(\n        expected, this, std::memory_order_acq_rel, std::memory_order_acquire);\n\n    ma_context_config ctxCfg = ma_context_config_init();",
)
replace_once(
    "src/AudioEngine.cpp",
    "AudioEngine::~AudioEngine()\n{\n    std::vector<std::shared_ptr<ActiveOutput>> oldOutputs;",
    "AudioEngine::~AudioEngine()\n{\n    AudioEngine* expected = this;\n    s_primaryInstance.compare_exchange_strong(\n        expected, nullptr, std::memory_order_acq_rel, std::memory_order_acquire);\n\n    std::vector<std::shared_ptr<ActiveOutput>> oldOutputs;",
)
replace_once(
    "src/AudioEngine.cpp",
    "    spdlog::info(\"AudioEngine destroyed\");\n}\n\nstd::vector<AudioDeviceInfo> AudioEngine::enumeratePlaybackDevices()",
    "    spdlog::info(\"AudioEngine destroyed\");\n}\n\nAudioEngine* AudioEngine::primaryInstance() noexcept\n{\n    return s_primaryInstance.load(std::memory_order_acquire);\n}\n\nstd::vector<AudioDeviceInfo> AudioEngine::enumeratePlaybackDevices()",
)

# Satcom engine: chronological IQ, continuous spectrum, and shared playback.
replace_once(
    "include/SatcomScannerEngine.h",
    "class Demodulator;\nclass SstvReceiverFeed;",
    "class AudioEngine;\nclass Demodulator;\nclass SstvReceiverFeed;\nstruct Receiver;",
)
replace_once(
    "include/SatcomScannerEngine.h",
    "    void workerLoop();\n    bool detectActivity(double& peakHz, double& peakDb);\n    void processLockedAudio();",
    "    void workerLoop();\n    bool refreshSpectrumSnapshot(double* peakHz = nullptr, double* peakDb = nullptr);\n    bool detectActivity(double& peakHz, double& peakDb);\n    void processLockedAudio();\n    void resetIqCursor(size_t deviceIndex);\n    AudioEngine* ensureAudioOutput();",
)
replace_once(
    "include/SatcomScannerEngine.h",
    "    std::unique_ptr<Demodulator> demod_;\n    std::atomic<bool> demodResetRequested_{true};",
    "    std::unique_ptr<Demodulator> demod_;\n    std::unique_ptr<Receiver> iqReceiver_;\n    std::unique_ptr<AudioEngine> ownedAudio_;\n    mutable std::mutex audioMutex_;\n    int64_t lastAudioAttemptMs_ = 0;\n    std::atomic<bool> demodResetRequested_{true};",
)
replace_once(
    "src/SatcomScannerEngine.cpp",
    "#include \"AptImageDecoder.h\"\n#include \"DeviceManager.h\"\n#include \"Demod.h\"",
    "#include \"AptImageDecoder.h\"\n#include \"AudioEngine.h\"\n#include \"DeviceManager.h\"\n#include \"Demod.h\"\n#include \"Receiver.h\"",
)
replace_once(
    "src/SatcomScannerEngine.cpp",
    "bool isPlaceholderDevice(const DeviceInfo& device) {\n    std::string label = device.label;\n    std::transform(label.begin(), label.end(), label.begin(), [](unsigned char c) {\n        return static_cast<char>(std::tolower(c));\n    });\n    return label.find(\"placeholder\") != std::string::npos ||\n           label.find(\"(stub)\") != std::string::npos;\n}\n",
    "bool isPlaceholderDevice(const DeviceInfo& device) {\n    std::string label = device.label;\n    std::transform(label.begin(), label.end(), label.begin(), [](unsigned char c) {\n        return static_cast<char>(std::tolower(c));\n    });\n    return label.find(\"placeholder\") != std::string::npos ||\n           label.find(\"(stub)\") != std::string::npos;\n}\n\nbool isProtectedReceiverOwner(DeviceManager::DeviceLeaseOwner owner) {\n    using Owner = DeviceManager::DeviceLeaseOwner;\n    return owner == Owner::P25 || owner == Owner::Inmarsat || owner == Owner::Aircraft;\n}\n",
)
replace_once(
    "src/SatcomScannerEngine.cpp",
    "    demod_ = std::make_unique<Demodulator>();\n    sstvFeed_ = std::make_shared<SstvReceiverFeed>();",
    "    demod_ = std::make_unique<Demodulator>();\n    iqReceiver_ = std::make_unique<Receiver>();\n    sstvFeed_ = std::make_shared<SstvReceiverFeed>();",
)
replace_once(
    "src/SatcomScannerEngine.cpp",
    "    return timestamp + \"_\" + safeToken(satId) + \"_\" + safeToken(downlinkId);\n}\n\nbool SatcomScannerEngine::start(bool force) {",
    "    return timestamp + \"_\" + safeToken(satId) + \"_\" + safeToken(downlinkId);\n}\n\nAudioEngine* SatcomScannerEngine::ensureAudioOutput() {\n    std::lock_guard<std::mutex> audioLock(audioMutex_);\n    AudioEngine* output = AudioEngine::primaryInstance();\n    if (!output) {\n        if (!ownedAudio_) ownedAudio_ = std::make_unique<AudioEngine>();\n        output = ownedAudio_.get();\n    }\n    if (!output) return nullptr;\n\n    if (output->activeOutputCount() == 0) {\n        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();\n        if (nowMs - lastAudioAttemptMs_ < 2000) return nullptr;\n        lastAudioAttemptMs_ = nowMs;\n        try {\n            const auto outputs = output->enumeratePlaybackDevices();\n            if (outputs.empty()) return nullptr;\n            size_t selected = 0;\n            for (size_t i = 0; i < outputs.size(); ++i) {\n                if (outputs[i].isDefault) { selected = i; break; }\n            }\n            output->setActiveOutputs({selected});\n            spdlog::info(\"Satcom audio output activated: {}\", output->getActiveDeviceNames());\n        } catch (const std::exception& ex) {\n            spdlog::warn(\"Satcom audio output activation failed: {}\", ex.what());\n            return nullptr;\n        } catch (...) {\n            spdlog::warn(\"Satcom audio output activation failed: unknown error\");\n            return nullptr;\n        }\n    }\n    return output->activeOutputCount() > 0 ? output : nullptr;\n}\n\nvoid SatcomScannerEngine::resetIqCursor(size_t deviceIndex) {\n    if (!iqReceiver_) iqReceiver_ = std::make_unique<Receiver>();\n    iqReceiver_->deviceIndex = deviceIndex;\n    DeviceManager::instance().setReceiverCursorToLiveEdge(deviceIndex, *iqReceiver_);\n    demodResetRequested_ = true;\n}\n\nbool SatcomScannerEngine::start(bool force) {",
)
replace_once(
    "src/SatcomScannerEngine.cpp",
    "bool SatcomScannerEngine::start(bool force) {\n    std::string err;\n    const size_t dev = resolveDeviceIndex(&err);",
    "bool SatcomScannerEngine::start(bool force) {\n    auto& deviceManager = DeviceManager::instance();\n    std::string err;\n    const size_t dev = resolveDeviceIndex(&err);",
)
replace_once(
    "src/SatcomScannerEngine.cpp",
    "    if (!DeviceManager::instance().acquireDeviceLease(dev, DeviceManager::DeviceLeaseOwner::Satcom, force, &err)) {",
    "    const auto priorOwner = deviceManager.deviceLeaseOwner();\n    if (force && isProtectedReceiverOwner(priorOwner)) {\n        std::lock_guard<std::mutex> lk(mutex_);\n        lastStatus_ = std::string(\"Receiver is busy with \" ) +\n                      DeviceManager::leaseOwnerName(priorOwner);\n        return false;\n    }\n    if (!deviceManager.acquireDeviceLease(dev, DeviceManager::DeviceLeaseOwner::Satcom, force, &err)) {",
)
replace_once(
    "src/SatcomScannerEngine.cpp",
    "    if (run_.load(std::memory_order_acquire)) return true;",
    "    if (run_.load(std::memory_order_acquire)) return true;\n    if (AudioEngine* output = AudioEngine::primaryInstance()) output->clearBuffers();",
)
replace_once(
    "src/SatcomScannerEngine.cpp",
    "    const bool streamWasRunning = deviceManager.isStreaming(dev);\n    if (!deviceManager.acquireDeviceLease(dev, DeviceManager::DeviceLeaseOwner::Satcom, force, &err)) {",
    "    const bool streamWasRunning = deviceManager.isStreaming(dev);\n    const auto priorOwner = deviceManager.deviceLeaseOwner();\n    if (force && isProtectedReceiverOwner(priorOwner)) {\n        if (error) {\n            *error = std::string(\"Receiver is busy with \" ) +\n                     DeviceManager::leaseOwnerName(priorOwner);\n        }\n        return false;\n    }\n    if (!deviceManager.acquireDeviceLease(dev, DeviceManager::DeviceLeaseOwner::Satcom, force, &err)) {",
)

new_spectrum = r'''bool SatcomScannerEngine::refreshSpectrumSnapshot(double* peakHz, double* peakDb) {
    auto& mgr = DeviceManager::instance();
    size_t dev = 0;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        dev = config_.deviceIndex;
    }

    std::vector<float> power;
    double cf = 0.0, sr = 0.0;
    if (!mgr.getLatestSpectrum(dev, power, cf, sr) || power.empty() || sr <= 0.0)
        return false;

    size_t peakIdx = 0;
    float peak = power.front();
    for (size_t i = 1; i < power.size(); ++i) {
        if (power[i] > peak) { peak = power[i]; peakIdx = i; }
    }

    std::vector<float> display = power;
    if (display.size() > 512) {
        std::vector<float> downsampled(512, -120.0f);
        for (size_t i = 0; i < downsampled.size(); ++i) {
            const size_t a = i * display.size() / downsampled.size();
            const size_t b = (i + 1) * display.size() / downsampled.size();
            float value = -200.0f;
            for (size_t k = a; k < b && k < display.size(); ++k)
                value = std::max(value, display[k]);
            downsampled[i] = value;
        }
        display = std::move(downsampled);
    }

    {
        std::lock_guard<std::mutex> lk(mutex_);
        spectrumDb_ = std::move(display);
        spectrumCenterHz_ = cf;
        spectrumRateHz_ = sr;
    }

    if (peakDb) *peakDb = peak;
    if (peakHz) {
        const double binHz = sr / static_cast<double>(power.size());
        *peakHz = cf - sr * 0.5 + (static_cast<double>(peakIdx) + 0.5) * binHz;
    }
    return true;
}

bool SatcomScannerEngine::detectActivity(double& peakHz, double& peakDb) {
    double squelch = -90.0;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        squelch = config_.squelchDb;
    }
    if (!refreshSpectrumSnapshot(&peakHz, &peakDb)) return false;
    return peakDb >= squelch;
}

'''
replace_between(
    "src/SatcomScannerEngine.cpp",
    "bool SatcomScannerEngine::detectActivity(double& peakHz, double& peakDb) {",
    "void SatcomScannerEngine::processLockedAudio() {",
    new_spectrum,
)

new_audio = r'''void SatcomScannerEngine::processLockedAudio() {
    auto& mgr = DeviceManager::instance();
    size_t dev = 0;
    std::string mode;
    std::string role;
    bool doAx = true, doApt = true;
    double lockHz = 0.0;
    double bw = 12.5e3;
    double squelchDb = -90.0;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        dev = config_.deviceIndex;
        mode = config_.mode;
        role = armedRole_;
        doAx = config_.enableAx25;
        doApt = config_.enableApt;
        lockHz = lockHz_;
        bw = std::min(config_.bandwidthHz, 50e3);
        squelchDb = config_.squelchDb;
    }

    double sr = mgr.getCurrentSampleRate(dev);
    if (!std::isfinite(sr) || sr <= 0.0) {
        const auto devs = mgr.getDevices();
        sr = dev < devs.size() && devs[dev].sampleRate > 0.0
            ? devs[dev].sampleRate
            : 2.048e6;
    }

    if (!iqReceiver_) return;
    const size_t chunkSamples = std::clamp<size_t>(
        static_cast<size_t>(std::llround(sr * 0.040)), 4096u, 131072u);
    const size_t maxLagSamples = std::clamp<size_t>(
        static_cast<size_t>(std::llround(sr * 0.250)), chunkSamples * 2, 1048576u);
    auto iqWindow = mgr.getNewIQWindowForReceiver(dev, *iqReceiver_, chunkSamples, maxLagSamples);
    if (iqWindow.cursorDiscontinuity) {
        demodResetRequested_ = true;
        ax25_->reset();
        if (mode == "APT" || mode == "AM") apt_->reset();
    }
    auto& iq = iqWindow.samples;
    if (iq.size() < 512) return;

    if (demodResetRequested_.exchange(false)) demod_->resetState();
    const double reportedCenter = mgr.getCurrentCenterFreq(dev);
    const double iqCenterHz = reportedCenter > 0.0 ? reportedCenter : lockHz;
    double rms = -120.0;
    FmMultiplexBlock multiplex;
    FmMultiplexBlock* multiplexOut = role == "sstv" ? &multiplex : nullptr;
    auto audio = demod_->demodulateToAudio(iq, sr, iqCenterHz, lockHz, modeFromString(mode),
                                           rms, 3000.0, squelchDb, 1.0, 75.0, 0.96, bw,
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

    if (AudioEngine* output = ensureAudioOutput()) {
        // Bound queued PCM so a slow/paused output cannot turn live satellite
        // reception into delayed audio after the pass has moved on.
        output->trimQueuedAudio(12000);
        output->pushAudio(audio.data(), audio.size());
    }

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

'''
replace_between(
    "src/SatcomScannerEngine.cpp",
    "void SatcomScannerEngine::processLockedAudio() {",
    "void SatcomScannerEngine::workerLoop() {",
    new_audio,
)
replace_once(
    "src/SatcomScannerEngine.cpp",
    "        mgr.releaseDeviceLease(DeviceManager::DeviceLeaseOwner::Satcom);\n        return;\n    }\n\n    while (run_.load(std::memory_order_acquire)) {",
    "        mgr.releaseDeviceLease(DeviceManager::DeviceLeaseOwner::Satcom);\n        return;\n    }\n\n    resetIqCursor(config().deviceIndex);\n    refreshSpectrumSnapshot();\n\n    while (run_.load(std::memory_order_acquire)) {",
)
replace_once(
    "src/SatcomScannerEngine.cpp",
    "        if (passHold) {\n            tickPassTrack();\n            processLockedAudio();\n            std::this_thread::sleep_for(std::chrono::milliseconds(200));\n            continue;\n        }",
    "        if (passHold) {\n            tickPassTrack();\n            refreshSpectrumSnapshot();\n            processLockedAudio();\n            std::function<void()> cb;\n            {\n                std::lock_guard<std::mutex> lk(mutex_);\n                cb = updateCb_;\n            }\n            if (cb) { try { cb(); } catch (...) {} }\n            std::this_thread::sleep_for(std::chrono::milliseconds(20));\n            continue;\n        }",
)
replace_once(
    "src/SatcomScannerEngine.cpp",
    "            } else {\n                processLockedAudio();\n                std::this_thread::sleep_for(std::chrono::milliseconds(30));\n            }",
    "            } else {\n                refreshSpectrumSnapshot();\n                processLockedAudio();\n                std::this_thread::sleep_for(std::chrono::milliseconds(20));\n            }",
)
replace_once(
    "src/SatcomScannerEngine.cpp",
    "            if (!mgr.retuneWithLease(cfg.deviceIndex, tune, DeviceManager::DeviceLeaseOwner::Satcom, true, &err)) {\n                std::lock_guard<std::mutex> lk(mutex_);\n                lastStatus_ = err.empty() ? \"scan retune blocked\" : err;\n            }\n            std::this_thread::sleep_for(std::chrono::milliseconds(std::max(20, cfg.dwellMs)));",
    "            if (!mgr.retuneWithLease(cfg.deviceIndex, tune, DeviceManager::DeviceLeaseOwner::Satcom, true, &err)) {\n                std::lock_guard<std::mutex> lk(mutex_);\n                lastStatus_ = err.empty() ? \"scan retune blocked\" : err;\n            } else {\n                const uint64_t tuneSeq = mgr.getCenterTuneRequestSeq(cfg.deviceIndex);\n                if (tuneSeq != 0) {\n                    mgr.waitForCenterTuneApplied(\n                        cfg.deviceIndex, tuneSeq, std::clamp(cfg.dwellMs * 4, 100, 750));\n                }\n            }\n            std::this_thread::sleep_for(std::chrono::milliseconds(std::max(20, cfg.dwellMs)));",
)
replace_once(
    "src/SatcomScannerEngine.cpp",
    "                demodResetRequested_ = true;\n                mgr.retuneWithLease(cfg.deviceIndex, peakHz, DeviceManager::DeviceLeaseOwner::Satcom, true, nullptr);\n                pushLog(SatcomLog::EventType::Lock, peakHz, \"activity\");",
    "                demodResetRequested_ = true;\n                std::string lockTuneError;\n                if (mgr.retuneWithLease(cfg.deviceIndex, peakHz,\n                                        DeviceManager::DeviceLeaseOwner::Satcom, true,\n                                        &lockTuneError)) {\n                    const uint64_t tuneSeq = mgr.getCenterTuneRequestSeq(cfg.deviceIndex);\n                    if (tuneSeq != 0)\n                        mgr.waitForCenterTuneApplied(cfg.deviceIndex, tuneSeq, 750);\n                    resetIqCursor(cfg.deviceIndex);\n                } else {\n                    std::lock_guard<std::mutex> lk(mutex_);\n                    lastStatus_ = lockTuneError.empty() ? \"lock retune failed\" : lockTuneError;\n                }\n                pushLog(SatcomLog::EventType::Lock, peakHz, \"activity\");",
)
replace_once(
    "src/SatcomScannerEngine.cpp",
    "    DeviceManager::instance().releaseDeviceLease(DeviceManager::DeviceLeaseOwner::Satcom);\n    pushLog(SatcomLog::EventType::Stop, currentHz_, \"scan stop\");",
    "    DeviceManager::instance().releaseDeviceLease(DeviceManager::DeviceLeaseOwner::Satcom);\n    if (AudioEngine* output = AudioEngine::primaryInstance()) output->clearBuffers();\n    pushLog(SatcomLog::EventType::Stop, currentHz_, \"scan stop\");",
)

# Explicit Start Scan is a user-requested tuner takeover. It may take normal
# listening, but protected P25/Inmarsat/Aircraft owners are rejected in engine.
replace_once(
    "src/SatcomScannerWidget.cpp",
    "    if (!SatcomScannerEngine::instance().start(false)) {",
    "    if (!SatcomScannerEngine::instance().start(true)) {",
)
replace_once(
    "src/SatcomScannerWidget.cpp",
    "    const auto snap = SatcomScannerEngine::instance().snapshot();\n    const auto& cfg = snap.config;\n\n    static int passTableThrottle = 0;",
    "    const auto snap = SatcomScannerEngine::instance().snapshot();\n    const auto& cfg = snap.config;\n    spectrum_->setSquelchThreshold(cfg.squelchDb);\n    spectrum_->setLiveRms(snap.audioRmsDb);\n\n    static int passTableThrottle = 0;",
)

# Ensure feature worker and shared audio cannot outlive MainWindow during shutdown.
replace_once(
    "src/MainWindow.cpp",
    "void MainWindow::closeEvent(QCloseEvent* event)\n{\n        if (workspaceLayout && !guiRuntimeConfig.hasStartupWork()) {",
    "void MainWindow::closeEvent(QCloseEvent* event)\n{\n        SatcomScannerEngine::instance().stopRecording();\n        SatcomScannerEngine::instance().stop();\n        SatcomScannerEngine::instance().disarmPass();\n        if (workspaceLayout && !guiRuntimeConfig.hasStartupWork()) {",
)

# Regression coverage for process-shared playback discovery.
with Path("tests/test_audioengine.cpp").open("a", encoding="utf-8", newline="\n") as out:
    out.write(r'''

TEST_CASE("AudioEngine exposes a live primary playback instance", "[audioengine][satcom]") {
    AudioEngine* before = AudioEngine::primaryInstance();
    auto* candidate = new AudioEngine();
    if (before) {
        REQUIRE(AudioEngine::primaryInstance() == before);
    } else {
        REQUIRE(AudioEngine::primaryInstance() == candidate);
    }
    delete candidate;
    if (!before) REQUIRE(AudioEngine::primaryInstance() == nullptr);
}
''')

print("Applied Satcom scanner lease/audio/spectrum/IQ runtime repair")
