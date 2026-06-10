#include "DeviceManager.h"
#include "Receiver.h"   // for getNewSamplesForReceiver(..., Receiver& rx, ... ) cursor update

#include <spdlog/spdlog.h>
#include <fstream>
#include <QStandardPaths>
#include <QDir>
#include <thread>
#include <queue>
#include <mutex>
#include <atomic>
#include <complex>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <QCoreApplication>
#include <cstdlib> // for _putenv_s
#include <windows.h> // for GetFileAttributesA

#ifdef HAVE_SOAPYSDR
#include <SoapySDR/Logger.hpp>
#endif

DeviceManager& DeviceManager::instance() {
    static DeviceManager mgr;
    return mgr;
}

std::vector<DeviceInfo> DeviceManager::enumerateDevices(bool probeHardware) {
    std::lock_guard<std::mutex> lk(devicesMutex);
    devices.clear();

    setupSoapyForRTLSDR();

#ifdef HAVE_SOAPYSDR
    try {
        SoapySDR::setLogLevel(SOAPY_SDR_INFO); // reduce noise
        auto results = SoapySDR::Device::enumerate();
        spdlog::info("SoapySDR enumerate returned {} device(s)", results.size());

        spdlog::info("Soapy enumerate found {} raw results", results.size());
        for (const auto& result : results) {
            DeviceInfo di;
            di.driver = result.count("driver") ? result.at("driver") : "unknown";
            di.label = result.count("label") ? result.at("label") : di.driver;
            di.serial = result.count("serial") ? result.at("serial") : "";
            di.hardware = result.count("hardware") ? result.at("hardware") : "";
            std::string info = "driver=" + di.driver + " label=" + di.label;
            if (!di.serial.empty()) info += " serial=" + di.serial;
            spdlog::info("  Soapy result: {}", info);

            if (probeHardware) {
                // Try to open to query capabilities (gains, antennas, rates). This can be slow or crashy
                // for some devices (especially RTL-SDR with marginal drivers). We SKIP this on initial
                // launch enumerate (probeHardware=false) so the app never does any hardware open/make
                // during startup, even if persisted devices are "enabled".
                try {
                    auto dev = SoapySDR::Device::make(result);
                    if (dev) {
                        // antennas
                        auto ants = dev->listAntennas(SOAPY_SDR_RX, 0);
                        di.antennas.assign(ants.begin(), ants.end());
                        if (di.antennas.empty()) {
                            if (di.driver == "rtlsdr") di.antennas = {"RX"};
                            else di.antennas = {"TX/RX"};
                        }
                        if (!di.antennas.empty()) di.antenna = di.antennas[0];

                        // sample rates (get a few)
                        auto rates = dev->listSampleRates(SOAPY_SDR_RX, 0);
                        for (size_t i = 0; i < rates.size() && i < 8; ++i) {
                            di.sampleRates.push_back(rates[i]);
                        }
                        if (di.sampleRates.empty()) {
                            if (di.driver == "rtlsdr") {
                                di.sampleRates = {0.25e6, 1.024e6, 2.048e6, 2.4e6};
                            } else {
                                di.sampleRates = {1e6, 2e6, 2.4e6, 5e6, 10e6};
                            }
                        }
                        if (!di.sampleRates.empty()) di.sampleRate = di.sampleRates[0];

                        // freq range rough
                        auto ranges = dev->getFrequencyRange(SOAPY_SDR_RX, 0);
                        if (!ranges.empty()) {
                            di.minFreq = ranges.front().minimum();
                            di.maxFreq = ranges.back().maximum();
                        }

                        // default gain (first range or current)
                        auto gains = dev->listGains(SOAPY_SDR_RX, 0);
                        if (!gains.empty()) {
                            di.gainName = gains[0];
                            auto gr = dev->getGainRange(SOAPY_SDR_RX, 0, di.gainName);
                            if (di.driver == "rtlsdr") {
                                di.gain = 20.0; // P1 audit smoking gun: 80 or 0.6*max overloads strong local WFM; 15-25 dB first test + BW 120-150 kHz
                            } else {
                                di.gain = std::min(40.0, gr.maximum() * 0.5);
                            }
                        } else {
                            di.gainName = "TUNER"; // common for RTL-SDR
                            if (di.driver == "rtlsdr") di.gain = 20.0;
                        }

                        SoapySDR::Device::unmake(dev);
                    }
                } catch (const std::exception& ex) {
                    spdlog::warn("Could not fully probe device {}: {}", di.label, ex.what());
                }
            } else {
                // Light path (launch): use safe defaults so we never touch hardware.
                if (di.driver == "rtlsdr") {
                    di.antennas = {"RX"};
                    di.antenna = "RX";
                    di.sampleRates = {0.25e6, 1.024e6, 2.048e6, 2.4e6};
                    di.sampleRate = 2.048e6;
                    di.minFreq = 24e6;
                    di.maxFreq = 1766e6;
                    di.gain = 20.0;  // P1 audit: strong local WFM overloads RTL at high gain (was 30/80); 15-25 safe for broadcast FM. BW 120-150kHz also recommended.
                    di.gainName = "TUNER";
                } else {
                    di.antennas = {"TX/RX"};
                    di.antenna = "TX/RX";
                    di.sampleRates = {1e6, 2e6, 2.4e6, 5e6, 10e6};
                    di.sampleRate = 2.4e6;
                    di.minFreq = 1e6;
                    di.maxFreq = 6e9;
                    di.gain = 40.0;
                }
            }

            devices.push_back(di);
        }

        // Always ensure an RTL-SDR entry (synthetic with safe defaults) so UI always has something.
        // We add it only if no rtlsdr was seen in this enumerate. The synthetic never does a real probe/make.
        bool hasRtl = false;
        for (const auto& d : devices) {
            if (d.driver == "rtlsdr") { hasRtl = true; break; }
        }
        if (!hasRtl) {
            DeviceInfo di;
            di.driver = "rtlsdr";
            di.label = "RTL-SDR (Generic RTL2832U)";
            di.serial = "";
            di.hardware = "RTL-SDR";
            di.antennas = {"RX"};
            di.antenna = "RX";
            di.sampleRates = {0.25e6, 1.024e6, 2.048e6, 2.4e6};
            di.sampleRate = 2.048e6;
            di.minFreq = 24e6;
            di.maxFreq = 1766e6;
            di.gain = 20.0;  // P1: default safe for strong local WFM (avoid 80 persisted overload at front-end)
            di.gainName = "TUNER";
            devices.push_back(di);
            spdlog::info("  Added RTL-SDR entry (will attempt real when enabled if hardware present)");
        }
    } catch (const std::exception& ex) {
        spdlog::error("SoapySDR enumeration failed: {}", ex.what());
    }
#else
    spdlog::warn("Built without SoapySDR support. Returning stub devices for UI testing.");
    // Provide a couple of fake devices so the UI can be exercised without hardware / Soapy
    DeviceInfo fake1;
    fake1.driver = "hackrf";
    fake1.label = "HackRF One (stub)";
    fake1.serial = "0000000000000000";
    fake1.antennas = {"TX/RX", "RX2"};
    fake1.antenna = "TX/RX";
    fake1.sampleRates = {2.4e6, 5e6, 10e6, 20e6};
    fake1.sampleRate = 2.4e6;
    fake1.gain = 40.0;
    fake1.minFreq = 1e6;
    fake1.maxFreq = 6e9;
    devices.push_back(fake1);

    DeviceInfo fake2;
    fake2.driver = "rtlsdr";
    fake2.label = "RTL-SDR (stub)";
    fake2.serial = "rtl-001";
    fake2.antennas = {"RX"};
    fake2.antenna = "RX";
    fake2.sampleRates = {1.024e6, 2.048e6, 2.4e6};
    fake2.sampleRate = 2.048e6;
    fake2.gain = 30.0;
    fake2.minFreq = 24e6;
    fake2.maxFreq = 1766e6;
    devices.push_back(fake2);
#endif

    // After enumerate, try to overlay saved settings (enabled, rate, gain, antenna from JSON).
    // This works for both light and full probe paths.
    loadSettings();

    return devices;
}

bool DeviceManager::setEnabled(size_t index, bool enabled) {
    std::lock_guard<std::mutex> lk(devicesMutex);
    if (index >= devices.size()) return false;
    devices[index].enabled = enabled;
    spdlog::info("Device {} '{}': {}", index, devices[index].label, enabled ? "ENABLED" : "disabled");
    saveSettings();
    return true;
}

DeviceInfo* DeviceManager::getDevice(size_t index) {
    std::lock_guard<std::mutex> lk(devicesMutex);
    if (index >= devices.size()) return nullptr;
    return &devices[index];
}

void DeviceManager::updateDeviceParams(size_t index, double sampleRate, double gain, const std::string& antenna) {
    std::lock_guard<std::mutex> lk(devicesMutex);
    if (index >= devices.size()) return;
    auto& d = devices[index];
    d.sampleRate = sampleRate;
    d.gain = gain;
    d.antenna = antenna;
    saveSettings();
    spdlog::debug("Updated params for {}: rate={}, gain={}, ant={}", d.label, sampleRate, gain, antenna);
}

void DeviceManager::loadSettings() {
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(appData);
    const std::string path = (appData + "/devices.json").toStdString();

    std::ifstream f(path);
    if (!f.is_open()) {
        spdlog::info("No previous device settings at {}", path);
        return;
    }

    try {
        nlohmann::json j;
        f >> j;
        fromJson(j);
        spdlog::info("Loaded device settings for {} device(s)", devices.size());
    } catch (const std::exception& ex) {
        spdlog::warn("Failed to load devices.json: {}", ex.what());
    }
}

void DeviceManager::saveSettings() const {
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(appData);
    const std::string path = (appData + "/devices.json").toStdString();

    try {
        std::ofstream f(path);
        if (f.is_open()) {
            f << toJson().dump(2);
            spdlog::debug("Saved device settings to {}", path);
        }
    } catch (const std::exception& ex) {
        spdlog::warn("Failed to save devices.json: {}", ex.what());
    }
}

nlohmann::json DeviceManager::toJson() const {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& d : devices) {
        nlohmann::json j;
        j["driver"] = d.driver;
        j["serial"] = d.serial;
        j["enabled"] = d.enabled;
        j["sampleRate"] = d.sampleRate;
        j["gain"] = d.gain;
        j["antenna"] = d.antenna;
        arr.push_back(j);
    }
    return arr;
}

void DeviceManager::fromJson(const nlohmann::json& j) {
    if (!j.is_array()) return;
    for (auto& d : devices) {
        for (const auto& saved : j) {
            if (saved.contains("driver") && saved["driver"] == d.driver &&
                saved.contains("serial") && saved["serial"] == d.serial) {
                if (saved.contains("enabled")) d.enabled = saved["enabled"];
                if (saved.contains("sampleRate")) d.sampleRate = saved["sampleRate"];
                if (saved.contains("gain")) d.gain = saved["gain"];
                if (saved.contains("antenna")) d.antenna = saved["antenna"];
                // P1 audit: persisted "gain":80 from old run is the #1 cause of scratch on strong WFM.
                // Force sane RTL default on load; user can still "gain 0 15" or edit json, but prevent auto-overload.
                if (d.driver == "rtlsdr" && d.gain > 25.0) {
                    spdlog::warn("Loaded high RTL gain {} from devices.json; capping to 20 dB for strong local FM (edit json or use CLI 'gain 0 XX' to persist lower).", d.gain);
                    d.gain = 20.0;
                }
                break;
            }
        }
    }
}

// --- Streaming implementation (real Soapy or simulated) ---

bool DeviceManager::startStreaming(size_t index, bool attemptReal) {
    DeviceInfo d;
    {
        std::lock_guard<std::mutex> lk(devicesMutex);
        if (index >= devices.size()) return false;
        d = devices[index];
    }
    if (streams.size() <= index) streams.resize(index + 1);
    if (!streams[index]) streams[index] = std::make_unique<StreamState>();

    auto& st = *streams[index];

    if (st.active) {
        if (attemptReal && !st.isReal) {
            // User explicitly wants real hardware (e.g. Apply in dialog), but we currently have
            // a safe stub running (from launch auto-start or previous failure). Stop the stub
            // cleanly then fall through to attempt the real open. This avoids "double use".
            stopStreaming(index);
            // fall through with active==false now
        } else {
            return true; // already streaming the desired mode
        }
    }

    // ALWAYS start a fast stub simulation first. This makes every "start" (Add Receiver,
    // Apply, Scan, CLI) return instantly with working spectrum + basic demod + audio routing.
    // No blocking on Soapy make / USB / driver init, which is the source of "hangs then crashes"
    // when the RTL dongle or audio devices are in a bad state.
    st.stopFlag = false;
    st.currentCenter = 100e6;
    double useRate = d.sampleRate;
    if (useRate < 0.25e6 || useRate > 60e6) useRate = (d.driver == "rtlsdr" ? 2.048e6 : 2.4e6);
    st.currentRate = useRate;

    // S0 / audit-followup-2: init per-device ring for cursor-based per-rx consumption.
    // Large enough for reasonable backlog (e.g. ~2s at 2MS/s ~ 4M samples ~32MB cf32 per active dev is acceptable).
    if (st.ringCapacity == 0) {
        st.ringCapacity = 1u << 22; // 4M samples
        st.iqRing.assign(st.ringCapacity, std::complex<float>(0,0));
        st.ringWriteIdx = 0;
        st.totalSamplesWritten = 0;
    }

    setupSoapyForRTLSDR();

    st.active = true;
    st.isReal = false;
    st.rxThread = std::thread(&DeviceManager::rxThreadFunc, this, index);
    if (!attemptReal) {
        spdlog::info("Started stub/sim streaming for device {} (safe mode)", index);
        return true;
    }

    // attemptReal=true (explicit user action): try the real Soapy open in a *background thread*...
    // S0-4: the entire real Soapy upgrade path (make, set, activate, close) is guarded so that
    // a build with Soapy disabled (or vcpkg manifest without soapysdr) compiles cleanly and still
    // provides full stub functionality for CLI/GUI "enable/tune/stats" flows (P1 audit).
#ifdef HAVE_SOAPYSDR
    uint64_t myGen = ++st.sessionGen;

    // Best practice per audit (P0 shutdown hang): NEVER use jthread (or any joinable thread handle that the caller will join) for the untrusted native Soapy open/make path.
    // SoapySDR::Device::make + USB driver stack can block forever on bad hardware/state. We launch a detached open-worker.
    // All safety is via sessionGen + stopFlag captured at launch time. The worker self-aborts and cleans (unmake) if gen mismatches or stop set.
    // stopStreaming simply bumps gen + stopFlag and never joins this worker. Abandoned make threads are reaped on process exit (acceptable; alternative is out-of-proc probe helper).
    st.realInitThread = std::thread([this, index, d, useRate, myGen]() mutable {
        if (index >= streams.size() || !streams[index]) return;
        auto& st = *streams[index];
        if (st.stopFlag) return;
        if (st.sessionGen.load() != myGen) return;
        try {
            if (d.driver == "rtlsdr") {
                std::string appDir = QCoreApplication::applicationDirPath().toStdString();
                try { SoapySDR::loadModule(appDir + "\\SoapyRTLSDR.dll"); } catch (...) {}
                try { SoapySDR::loadModule("C:\\Program Files\\PothosSDR\\lib\\SoapySDR\\modules0.8\\rtlsdrSupport.dll"); } catch (...) {}
            }

            SoapySDR::Kwargs args;
            if (!d.driver.empty()) args["driver"] = d.driver;
            if (!d.serial.empty()) args["serial"] = d.serial;
            spdlog::info("Background: Attempting Soapy make for device {}", index);
            st.soapyDev = SoapySDR::Device::make(args);
            if (!st.soapyDev) throw std::runtime_error("make returned null");

            st.soapyDev->setSampleRate(SOAPY_SDR_RX, 0, useRate);
            if (!d.antenna.empty()) try { st.soapyDev->setAntenna(SOAPY_SDR_RX, 0, d.antenna); } catch (...) {}
            double useGain = std::clamp(d.gain, 0.0, 80.0);
            if (d.driver == "rtlsdr" && useGain > 25.0) {
                spdlog::warn("RTL gain {} too high for strong signals (overload -> scratch/crackle on WFM). Capping to 20 dB.", useGain);
                useGain = 20.0;
            } else if (d.driver == "rtlsdr" && useGain < 5.0) {
                useGain = 15.0;
            }
            if (!d.gainName.empty()) {
                try { st.soapyDev->setGain(SOAPY_SDR_RX, 0, d.gainName, useGain); } catch (...) { st.soapyDev->setGain(SOAPY_SDR_RX, 0, useGain); }
            } else {
                try { st.soapyDev->setGain(SOAPY_SDR_RX, 0, useGain); } catch (...) {}
            }
            try { st.soapyDev->setFrequency(SOAPY_SDR_RX, 0, st.currentCenter); } catch (...) {}

            st.rxStream = st.soapyDev->setupStream(SOAPY_SDR_RX, "CF32");
            if (!st.rxStream) throw std::runtime_error("setupStream null");
            st.soapyDev->activateStream(st.rxStream);

            if (st.stopFlag || st.sessionGen.load() != myGen) {
                try {
                    if (st.rxStream && st.soapyDev) st.soapyDev->closeStream(st.rxStream);
                    if (st.soapyDev) SoapySDR::Device::unmake(st.soapyDev);
                } catch (...) {}
                st.soapyDev = nullptr;
                st.rxStream = nullptr;
                return;
            }
            if (st.sessionGen.load() != myGen) return;

            // Success path: stop stub (our code, safe to join), start real rx thread.
            st.stopFlag = true;
            if (st.rxThread.joinable()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                st.rxThread.join();
            }
            st.active = true;
            st.isReal = true;
            st.stopFlag = false;
            st.rxThread = std::thread(&DeviceManager::rxThreadFunc, this, index);
            spdlog::info("Background upgrade: Started real Soapy streaming for device {}", index);
        } catch (const std::exception& ex) {
            spdlog::warn("Background real init failed for device {} ({}). Keeping safe stub.", index, ex.what());
            if (st.soapyDev) { SoapySDR::Device::unmake(st.soapyDev); st.soapyDev = nullptr; }
            st.rxStream = nullptr;
        } catch (...) {
            spdlog::warn("Background real init failed for device {} with unknown exception (SEH/driver). Keeping safe stub.", index);
            if (st.soapyDev) { SoapySDR::Device::unmake(st.soapyDev); st.soapyDev = nullptr; }
            st.rxStream = nullptr;
        }
    });
    // Immediately detach: this is the open-worker for untrusted driver. We never join it again.
    if (st.realInitThread.joinable()) {
        st.realInitThread.detach();
    }

    return true;
#else
    // !HAVE_SOAPYSDR: stay on the fast safe stub that was started above. The CLI/GUI/ tests
    // continue to work for list/enable/tune/mode/stats/spectrum/demod (using the internal stub path in rxThreadFunc).
    spdlog::info("SoapySDR not available in this build — device {} staying on safe internal stub (no real hardware).", index);
#endif
    return true;
}

void DeviceManager::stopStreaming(size_t index) {
    if (index >= streams.size() || !streams[index]) return;
    auto& st = *streams[index];
    bool soapyIdle =
#ifdef HAVE_SOAPYSDR
        !st.soapyDev &&
#endif
        true;
    if (!st.active && soapyIdle && !st.realInitThread.joinable() && !st.rxThread.joinable()) return;

    // P1: bump generation *first* so any in-flight init thread will see the mismatch and refuse to publish/teardown.
    st.sessionGen.fetch_add(1, std::memory_order_acq_rel);
    st.stopFlag = true;

    // realInitThread is launched detached (see startStreaming). Never join it here — it is the untrusted open path.
    // The gen bump + stopFlag inside the worker is sufficient for it to self-abort and unmake if it ever wakes.
    if (st.realInitThread.joinable()) {
        // Best-effort: if somehow not yet detached by launcher, detach now without waiting.
        try { st.realInitThread.detach(); } catch (...) {}
    }

    // For the rxThread (post-activate, our code): still attempt short graceful join because the loop checks stopFlag,
    // but if readStream / driver is wedged we must not block the caller (CLI quit, app exit, updater launch, etc.).
    // Use the same timeout+detach escape. This fixes the "CLI/hardware shutdown hang is back".
    if (st.rxThread.joinable()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        auto start = std::chrono::steady_clock::now();
        while (st.rxThread.joinable()) {
            if (std::chrono::steady_clock::now() - start > std::chrono::milliseconds(300)) {
                spdlog::warn("rxThread for device {} still running after stop — detaching (possible stuck readStream / native driver).", index);
                try { st.rxThread.detach(); } catch (...) {}
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
        }
        if (st.rxThread.joinable()) {
            try { st.rxThread.join(); } catch (...) { try { st.rxThread.detach(); } catch (...) {} }
        }
    }

#ifdef HAVE_SOAPYSDR
    // Capture and null only when Soapy types are available (fixes no-Soapy build P1).
    SoapySDR::Device* devToClose = st.soapyDev;
    SoapySDR::Stream* streamToClose = st.rxStream;
    st.soapyDev = nullptr;
    st.rxStream = nullptr;

    try {
        if (streamToClose && devToClose) {
            devToClose->deactivateStream(streamToClose);
            devToClose->closeStream(streamToClose);
        }
        if (devToClose) {
            SoapySDR::Device::unmake(devToClose);
        }
    } catch (const std::exception& ex) {
        spdlog::warn("Exception during Soapy teardown for device {}: {}", index, ex.what());
    } catch (...) {
        spdlog::warn("Unknown exception during Soapy teardown for device {}", index);
    }
#endif

    st.active = false;
    st.isReal = false;
    // clear queue
    std::lock_guard<std::mutex> lk(st.queueMutex);
    while (!st.iqQueue.empty()) st.iqQueue.pop_front();
    st.frontBlockReadOffset = 0;
    spdlog::info("Stopped streaming for device {}", index);
}

bool DeviceManager::isStreaming(size_t index) const {
    if (index >= streams.size() || !streams[index]) return false;
    return streams[index]->active;
}

size_t DeviceManager::getNextIQBlock(size_t index, std::complex<float>* buffer, size_t maxSamples, int timeoutMs) {
    if (index >= streams.size() || !streams[index]) return 0;
    auto& st = *streams[index];
    if (!st.active) return 0;

    std::unique_lock<std::mutex> lk(st.queueMutex);
    if (timeoutMs > 0) {
        // Safe polling wait: unlock via the lock object, sleep, re-lock.
        // Avoids raw mutex calls that can corrupt unique_lock state.
        auto start = std::chrono::steady_clock::now();
        while (true) {
            if (!st.iqQueue.empty()) break;
            lk.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            lk.lock();
            if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count() > timeoutMs) {
                return 0;
            }
        }
    }
    if (st.iqQueue.empty()) return 0;

    auto& block = st.iqQueue.front();
    size_t avail = block.size() - st.frontBlockReadOffset;
    size_t n = std::min(maxSamples, avail);
    std::copy(block.begin() + st.frontBlockReadOffset, block.begin() + st.frontBlockReadOffset + n, buffer);
    st.frontBlockReadOffset += n;
    if (st.frontBlockReadOffset == block.size()) {
        st.iqQueue.pop_front();
        st.frontBlockReadOffset = 0;
    }
    lk.unlock();

    return n;
}

bool DeviceManager::getLatestSpectrum(size_t index, std::vector<float>& powerDb, double& centerFreq, double& sampleRate) {
    if (index >= streams.size() || !streams[index]) return false;
    auto& st = *streams[index];
    std::lock_guard<std::mutex> lk(st.queueMutex);
    if (!st.active || st.latestPower.empty()) return false;

    powerDb = st.latestPower;
    centerFreq = st.currentCenter;
    sampleRate = st.currentRate;
    return true;
}

double DeviceManager::getCurrentGain(size_t index) const {
    std::lock_guard<std::mutex> lk(devicesMutex);
    if (index >= devices.size()) return 0.0;
    return devices[index].gain;
}

void DeviceManager::setLiveGain(size_t index, double gainDb) {
    // Always update the persisted / model value
    {
        std::lock_guard<std::mutex> lk(devicesMutex);
        if (index >= devices.size()) return;
        devices[index].gain = gainDb;
    }

#ifdef HAVE_SOAPYSDR
    // If we have a live real streaming session, apply it to hardware right now.
    if (index < streams.size() && streams[index]) {
        auto& st = *streams[index];
        if (st.isReal && st.soapyDev && !st.stopFlag) {
            try {
                double useGain = std::clamp(gainDb, 0.0, 80.0);
                if (devices[index].driver == "rtlsdr" && useGain > 25.0) useGain = 20.0;
                if (!devices[index].gainName.empty()) {
                    st.soapyDev->setGain(SOAPY_SDR_RX, 0, devices[index].gainName, useGain);
                } else {
                    st.soapyDev->setGain(SOAPY_SDR_RX, 0, useGain);
                }
                spdlog::info("Live RF gain applied to device {}: {} dB", index, useGain);
            } catch (const std::exception& ex) {
                spdlog::warn("Failed to apply live gain to device {}: {}", index, ex.what());
            }
        }
    }
#endif
}

size_t DeviceManager::getIQQueueDepth(size_t index) const {
    // Safe under stream's queueMutex (P2 audit: was unlocked .size() read in rx path too)
    if (index >= streams.size() || !streams[index]) return 0;
    auto& st = *streams[index];
    std::lock_guard<std::mutex> lk(st.queueMutex);
    return st.iqQueue.size();
}

// S0-3 (P1): non-consuming recent window so N receivers on the same device each get a coherent
// recent RF capture for their private channelizer/demod. Does not advance frontBlockReadOffset
// or pop anything (unlike the consuming getNextIQBlock used by primary spectrum path).
std::vector<std::complex<float>> DeviceManager::getRecentIQWindow(size_t index, size_t maxSamples) {
    if (index >= streams.size() || !streams[index] || maxSamples == 0) return {};
    auto& st = *streams[index];
    std::lock_guard<std::mutex> lk(st.queueMutex);
    if (!st.active || st.iqQueue.empty()) return {};

    std::vector<std::complex<float>> out;
    out.reserve(maxSamples);

    // Start from the most recent full blocks (back of deque) and work toward older if needed.
    // Also incorporate any live remainder in the front block (the one being partially consumed by getNext).
    // This gives demod paths a consistent "latest N samples" view without affecting the primary cursor.
    auto it = st.iqQueue.rbegin(); // most recent full block first
    // First, if there is a partial front block in flight, we can only reliably use full previous blocks here.
    // For simplicity and low latency we copy full recent blocks from the tail until we have enough or run out.
    while (it != st.iqQueue.rend() && out.size() < maxSamples) {
        const auto& blk = *it;
        size_t take = std::min(blk.size(), maxSamples - out.size());
        // prepend in reverse iteration order — correct by inserting at front or build then reverse at end
        // Easier: collect in reverse then fix, or use a temp and insert.
        // Practical: push the recent ones and reverse at the end.
        out.insert(out.begin(), blk.end() - take, blk.end());  // take the newest part of this (older in iteration) blk? Wait, rbegin is newest.
        // Since we go from newest block backward, the newest samples end up at the front of 'out' after inserts at begin — reverse at end.
        ++it;
    }

    if (!out.empty()) {
        // We built newest-first because of rbegin + insert begin; reverse to chronological (oldest first) for the caller.
        std::reverse(out.begin(), out.end());
    }

    // Trim to exactly what was requested (in case we over-copied slightly).
    if (out.size() > maxSamples) out.resize(maxSamples);

    return out;
}

// S0 / audit-followup-2: cursor based new-samples only, chronological, per-rx.
// Replaces the "always take newest overlapping window" anti-pattern that caused repeated demod of the same data / chop.
std::vector<std::complex<float>> DeviceManager::getNewSamplesForReceiver(size_t devIndex, Receiver& rx, size_t maxSamples) {
    if (devIndex >= streams.size() || !streams[devIndex] || maxSamples == 0) return {};
    auto& st = *streams[devIndex];
    if (st.ringCapacity == 0) return {};

    uint64_t myLast = rx.lastConsumedAbsolute;
    uint64_t total = st.totalSamplesWritten.load(std::memory_order_acquire);
    uint64_t available = (total > myLast) ? (total - myLast) : 0;

    if (available == 0) return {};

    // If we are way behind the ring (data was overwritten), skip forward.
    // Log once per big drop and advance cursor. Return a short zero block so demod can ramp/squelch naturally (fade).
    if (available > st.ringCapacity) {
        spdlog::warn("Receiver on dev {} fell behind by {} samples; dropping old data and skipping forward (audio may have a brief dropout/fade).", devIndex, (unsigned long long)(available - st.ringCapacity));
        // Leave a little headroom so we have some new data this time
        myLast = total - (st.ringCapacity / 2);
        rx.lastConsumedAbsolute = myLast;
        available = (total > myLast) ? (total - myLast) : 0;
        if (available == 0) return {};
        // Return a small zeroed block to give the downstream (squelch, resample, audio) a chance to fade cleanly
        size_t fadeLen = std::min((size_t)256, maxSamples);
        return std::vector<std::complex<float>>(fadeLen, std::complex<float>(0,0));
    }

    size_t toRead = (size_t)std::min((uint64_t)maxSamples, available);

    std::vector<std::complex<float>> out;
    out.reserve(toRead);

    size_t cap = st.ringCapacity;
    size_t startWrapped = (size_t)(myLast % cap);

    for (size_t k = 0; k < toRead; ++k) {
        size_t pos = (startWrapped + k) & (cap - 1);
        out.push_back(st.iqRing[pos]);
    }

    rx.lastConsumedAbsolute += toRead;
    return out;
}

void DeviceManager::appendIQBlock(size_t index, std::vector<std::complex<float>>&& block) {
    if (index >= streams.size() || !streams[index] || block.empty()) return;
    auto& st = *streams[index];

    // Feed the per-rx ring *first* while we still own the data (before any move into queue).
    if (st.ringCapacity > 0) {
        size_t w = st.ringWriteIdx.load(std::memory_order_relaxed);
        for (const auto& s : block) {
            st.iqRing[w] = s;
            w = (w + 1) & (st.ringCapacity - 1);
        }
        st.ringWriteIdx.store(w, std::memory_order_release);
        st.totalSamplesWritten.fetch_add(block.size(), std::memory_order_relaxed);
    }

    // Then the consuming deque for getNext / spectrum (bounded).
    {
        std::lock_guard<std::mutex> lk(st.queueMutex);
        st.iqQueue.push_back(std::move(block));
        size_t maxQ = (index == 0 ? 128 : 64); // slightly larger for primary
        while (st.iqQueue.size() > maxQ) st.iqQueue.pop_front();
    }
}

std::vector<std::string> DeviceManager::getAvailableDrivers() const {
    // Ensure setup even for this call (const cast for simplicity since side effects are global env + module loads)
    const_cast<DeviceManager*>(this)->setupSoapyForRTLSDR();

    std::set<std::string> drivers;
#ifdef HAVE_SOAPYSDR
    try {
        auto results = SoapySDR::Device::enumerate();
        for (const auto& r : results) {
            if (r.count("driver")) drivers.insert(r.at("driver"));
        }
    } catch (...) {}
#endif
    // Always include known for stubs / info
    drivers.insert("rtlsdr");
    drivers.insert("hackrf");
    return std::vector<std::string>(drivers.begin(), drivers.end());
}

void DeviceManager::setupSoapyForRTLSDR() {
    // Help Soapy find RTL-SDR module from common bundles like PothosSDR
    // This registers the "rtlsdr" driver if the module is present there.
    const char* commonRoots[] = {
        "C:\\Program Files\\PothosSDR",
        "C:\\Program Files (x86)\\PothosSDR",
        nullptr
    };
    std::string appDir = QCoreApplication::applicationDirPath().toStdString();
    for (int i = 0; commonRoots[i]; ++i) {
        std::string root = commonRoots[i];
        std::string modPath = root + "\\lib\\SoapySDR\\modules";
        if (GetFileAttributesA(modPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
            _putenv_s("SOAPY_SDR_ROOT", root.c_str());
            spdlog::info("Set SOAPY_SDR_ROOT to {} for RTL-SDR module discovery", root);
            break;
        }
    }

    // Prepend PothosSDR bin (has compatible rtlsdr.dll + module) and app dir to PATH so driver and module resolve correctly.
    // Only do this once (or when not already present) to avoid PATH growing without bound on every
    // enumerate / start / getAvailableDrivers call.
    std::string pothosBin = "C:\\Program Files\\PothosSDR\\bin";
    std::string currentPath = getenv("PATH") ? getenv("PATH") : "";
    static bool pathSetupDone = false;
    if (!pathSetupDone) {
        bool needPothos = currentPath.find(pothosBin) == std::string::npos;
        bool needApp    = currentPath.find(appDir) == std::string::npos;
        if (needPothos || needApp) {
            std::string newPath = currentPath;
            if (needPothos) newPath = pothosBin + ";" + newPath;
            if (needApp)    newPath = appDir + ";" + newPath;
            _putenv_s("PATH", newPath.c_str());
            spdlog::debug("Updated process PATH for Soapy/RTL (once)");
        }
        pathSetupDone = true;
    }

#ifdef HAVE_SOAPYSDR
    // Note: Removed explicit loadModule here to avoid early hardware probe/crash on startup.
    // Setting ROOT and PATH should allow Soapy to discover/load the RTL module when needed (during enumerate with driver filter or make).
    // The forced rtlsdr enumerate below will ensure the device appears in the list for the user.
    spdlog::info("Soapy path setup done for RTL discovery (module will be loaded on demand).");
#endif
}

void DeviceManager::setCenterFreq(size_t index, double freqHz) {
    if (index >= streams.size() || !streams[index]) return;
    auto& st = *streams[index];
    if (!st.active) return;

    {
        std::lock_guard<std::mutex> lk(st.queueMutex);
        st.currentCenter = freqHz;
    }

#ifdef HAVE_SOAPYSDR
    if (st.soapyDev) {
        try {
            st.soapyDev->setFrequency(SOAPY_SDR_RX, 0, freqHz);
            spdlog::debug("Set center freq {} for device {}", freqHz, index);
        } catch (const std::exception& ex) {
            spdlog::warn("setFrequency failed: {}", ex.what());
        }
    }
#endif
}

// Real radix-2 FFT implementation (iterative, double precision for dynamic range).
// Bit-reversal + Danielson-Lanczos butterflies. Self-contained, no external FFT lib required for viz.
static void fftRadix2(std::vector<std::complex<double>>& x) {
    const size_t N = x.size();
    // bit reverse
    for (size_t i = 1, j = 0; i < N; ++i) {
        size_t bit = N >> 1;
        for (; j >= bit; bit >>= 1) j -= bit;
        j += bit;
        if (i < j) std::swap(x[i], x[j]);
    }
    for (size_t len = 2; len <= N; len <<= 1) {
        double ang = -2.0 * 3.141592653589793 * (1.0 / len);
        std::complex<double> wlen(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < N; i += len) {
            std::complex<double> w(1);
            for (size_t j = 0; j < len / 2; ++j) {
                auto u = x[i + j];
                auto v = x[i + j + len / 2] * w;
                x[i + j] = u + v;
                x[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

std::vector<float> DeviceManager::computeRealFFTPower(const std::vector<std::complex<float>>& time, size_t fftN, bool useBlackmanHarris) {
    if (fftN == 0 || (fftN & (fftN - 1)) != 0) fftN = 8192; // force pow2
    std::vector<std::complex<double>> buf(fftN);
    size_t n = std::min(fftN, time.size());
    // window + copy (zero pad if needed)
    for (size_t i = 0; i < fftN; ++i) {
        std::complex<float> s = (i < n) ? time[i] : std::complex<float>(0,0);
        double w;
        if (useBlackmanHarris) {
            // Blackman-Harris (approx 92 dB sidelobe) — excellent for SDR spectrum
            double a0 = 0.35875, a1 = 0.48829, a2 = 0.14128, a3 = 0.01168;
            double x = 2.0 * 3.141592653589793 * i / (fftN - 1.0);
            w = a0 - a1 * std::cos(x) + a2 * std::cos(2 * x) - a3 * std::cos(3 * x);
        } else {
            // Hann
            w = 0.5 * (1.0 - std::cos(2.0 * 3.141592653589793 * i / (fftN - 1.0)));
        }
        buf[i] = std::complex<double>(s.real() * w, s.imag() * w);
    }
    fftRadix2(buf);

    // Power spectrum, fftshifted so [0] = -fs/2, middle = 0, end = +fs/2 - bin
    std::vector<float> power(fftN);
    const double norm = 1.0 / (double)fftN;
    for (size_t i = 0; i < fftN; ++i) {
        size_t k = (i + fftN / 2) % fftN; // fftshift
        double re = buf[k].real() * norm;
        double im = buf[k].imag() * norm;
        double p = re*re + im*im;
        float db = 10.0f * std::log10(std::max(p, 1e-20));
        power[i] = db;
    }
    return power;
}

void DeviceManager::rxThreadFunc(size_t index) {
    if (index >= streams.size() || !streams[index]) return;
    auto& st = *streams[index];
    const size_t blockSize = 32768; // much larger blocks to sustain 2MS/s+ without overflow. 2048 was only ~1ms of RF at 2.048MS/s.

#ifdef HAVE_SOAPYSDR
    if (st.soapyDev && st.rxStream) {
        std::vector<std::complex<float>> buff(blockSize);
        // Broad guard: native readStream / USB / driver faults in the background thread must never terminate the process.
        try {
            auto lastSpectrumTime = std::chrono::steady_clock::now();
            while (!st.stopFlag) {
                int flags = 0;
                long long timeNs = 0;
                void* buffs[] = { buff.data() };
                int numElems = st.soapyDev->readStream(st.rxStream, buffs, blockSize, flags, timeNs, 100000);
                if (numElems < 0) {
                    spdlog::warn("readStream error code: {}", numElems);
                    if (numElems == -4) { // SOAPY_SDR_OVERFLOW - samples were dropped before we read
                        static std::atomic<int> overflowCount{0};
                        int c = ++overflowCount;
                        if (c % 50 == 1) spdlog::warn("OVERFLOW (-4) - IQ samples lost (count={}). Larger blocks + lean RX thread help.", c);
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    continue;
                }
                size_t numRead = static_cast<size_t>(numElems);
                if (numRead > 0) {
                    if (numRead > blockSize) numRead = blockSize;
                    std::vector<std::complex<float>> block(buff.begin(), buff.begin() + numRead);
                    // Centralized: feeds ring before move into queue (fixes ring getting no samples after move)
                    appendIQBlock(index, std::move(block));

                    // === State-of-the-art spectrum pipeline (P1 audit + this stabilization) ===
                    // - Real radix-2 FFT, 8192 bins (supports 4096/8192/16384)
                    // - Hann + Blackman-Harris windows (Blackman-Harris for main viz)
                    // - Window taken from high-quality per-device ring (overlap friendly via ring)
                    // - Exponential averaging + peak hold (slow decay) maintained in StreamState
                    // - Throttled (~16-30 Hz) in RX thread to keep readStream lean (future: can move to dedicated spectrum worker thread)
                    // - Published as high-res latestPower so SpectrumWidget can do true-resolution zoomed waterfall from source history.
                    auto now = std::chrono::steady_clock::now();
                    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastSpectrumTime).count() > 45) {
                        const size_t fftN = 8192; // SOTA default; higher = more resolution for zoom
                        std::vector<float> localPower;

                        // Draw window from ring (best continuous recent IQ, enables overlap if we hop)
                        std::vector<std::complex<float>> samples;
                        samples.reserve(fftN);
                        uint64_t total = st.totalSamplesWritten.load(std::memory_order_acquire);
                        if (st.ringCapacity > 0 && total >= fftN) {
                            uint64_t start = total - fftN;
                            for (size_t k = 0; k < fftN; ++k) {
                                size_t idx = (start + k) % st.ringCapacity;
                                samples.push_back(st.iqRing[idx]);
                            }
                        } else {
                            size_t take = std::min((size_t)numRead, fftN);
                            for (size_t i = 0; i < take; ++i) samples.push_back(buff[i]);
                            while (samples.size() < fftN) samples.push_back({0.f, 0.f});
                        }

                        // Real FFT power (Blackman-Harris primary for clean dynamic range; Hann available)
                        localPower = computeRealFFTPower(samples, fftN, /*useBlackmanHarris=*/true);

                        // Exponential avg + peak hold (state lives in StreamState for continuity across calls)
                        if (st.spectrumAvg.size() != localPower.size()) {
                            st.spectrumAvg.assign(localPower.size(), -110.0f);
                            st.spectrumPeak.assign(localPower.size(), -110.0f);
                        }
                        for (size_t b = 0; b < localPower.size(); ++b) {
                            st.spectrumAvg[b] = st.spectrumAvg[b] * 0.72f + localPower[b] * 0.28f;
                            float decayedPeak = st.spectrumPeak[b] * 0.985f;
                            st.spectrumPeak[b] = std::max(decayedPeak, localPower[b]);
                        }
                        // Publish the averaged high-res spectrum (UI can choose peak if wanted later)
                        {
                            std::lock_guard<std::mutex> lk(st.queueMutex);
                            st.latestPower = st.spectrumAvg; // high bin count vector
                            try {
                                st.currentRate = st.soapyDev->getSampleRate(SOAPY_SDR_RX, 0);
                            } catch (...) {}
                        }
                        lastSpectrumTime = now;
                    }
                }
                // No unconditional sleep. Only yield if queue is getting very full (backpressure).
                // P2 audit: .size() read was unlocked (race with push under queueMutex); fix by short re-lock or decision under lock.
                {
                    std::lock_guard<std::mutex> lk(st.queueMutex);
                    if (st.iqQueue.size() > 96) {
                        std::this_thread::sleep_for(std::chrono::microseconds(200));
                    }
                }
            }
        } catch (const std::exception& ex) {
            spdlog::error("Exception in real rxThread for device {}: {} (thread exiting cleanly to stub-safe state)", index, ex.what());
        } catch (...) {
            spdlog::error("Unknown exception in real rxThread for device {} (possible USB/driver fault). Thread exiting cleanly.", index);
        }
        return;
    }
#endif

    // Stub simulation: generate IQ with a few carriers + noise, update spectrum
    double t = 0.0;
    const double fs = 2.4e6;
    while (!st.stopFlag) {
        std::vector<std::complex<float>> block(blockSize);
        for (size_t i = 0; i < blockSize; ++i) {
            double phase = 2 * 3.14159265 * (100e6 + 0.1e6 * std::sin(t * 0.3)) / fs * i; // drifting carrier example
            float re = std::cos(phase) * 0.8f + (rand() % 1000 - 500) * 0.0002f;
            float im = std::sin(phase) * 0.8f + (rand() % 1000 - 500) * 0.0002f;
            block[i] = {re, im};
        }
        // Centralized append: ring fed before move (no more "ring receives no samples" after std::move)
        appendIQBlock(index, std::move(block));

        // spectrum for stub (high bin count via the *same* real FFT path so zoomed views look consistent and "high res" even in demo/no-hw)
        {
            const size_t fftN = 8192;
            std::vector<std::complex<float>> fake(fftN);
            for (size_t i = 0; i < fftN; ++i) {
                double phase = 2 * 3.14159265 * (100e6 + 0.1e6 * std::sin(t * 0.3)) / fs * (double)i;
                float re = std::cos(phase) * 0.7f + (rand() % 1000 - 500) * 0.00015f;
                float im = std::sin(phase) * 0.7f + (rand() % 1000 - 500) * 0.00015f;
                fake[i] = {re, im};
            }
            auto lp = computeRealFFTPower(fake, fftN, true);
            if (st.spectrumAvg.size() != lp.size()) st.spectrumAvg = lp;
            for (size_t b = 0; b < lp.size(); ++b) st.spectrumAvg[b] = st.spectrumAvg[b] * 0.7f + lp[b] * 0.3f;
            {
                std::lock_guard<std::mutex> lk(st.queueMutex);
                st.latestPower = st.spectrumAvg;
                st.currentRate = fs;
            }
        }

        t += 0.05;
        std::this_thread::sleep_for(std::chrono::milliseconds(2)); // faster production for real-time-ish audio rate in stub demo (was 40ms causing data starvation for 48kHz audio)
    }
}