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
#include <array>
#include <QCoreApplication>
#include <cstdlib>
#ifdef _WIN32
#include <windows.h> // for GetFileAttributesA
#endif

#ifdef HAVE_SOAPYSDR
#include <SoapySDR/Logger.hpp>
#endif

DeviceManager& DeviceManager::instance() {
    static DeviceManager mgr;
    return mgr;
}

static double clampGainForDevice(const DeviceInfo& d, double gainDb) {
    double minGain = d.gainMin;
    double maxGain = d.gainMax;
    if (!std::isfinite(minGain) || !std::isfinite(maxGain) || maxGain <= minGain) {
        minGain = 0.0;
        maxGain = (d.driver == "rtlsdr") ? 49.6 : 80.0;
    }
    return std::clamp(gainDb, minGain, maxGain);
}

static double clampFrequencyCorrectionPpm(double ppm) {
    if (!std::isfinite(ppm)) return 0.0;
    return std::clamp(ppm, -200.0, 200.0);
}

static double correctedTuneFrequencyHz(double logicalHz, double ppm) {
    if (!std::isfinite(logicalHz) || logicalHz <= 0.0) return logicalHz;
    ppm = clampFrequencyCorrectionPpm(ppm);
    const double scale = 1.0 + ppm * 1e-6;
    if (std::abs(scale) < 1e-9) return logicalHz;
    return logicalHz / scale;
}

static size_t normalizeSpectrumFftBins(size_t bins) {
    if (bins <= 4096) return 4096;
    if (bins <= 8192) return 8192;
    if (bins <= 16384) return 16384;
    return 65536;
}

static double chooseDefaultSampleRate(const DeviceInfo& d) {
    if (d.sampleRates.empty()) return d.driver == "rtlsdr" ? 2.048e6 : 2.4e6;
    const std::array<double, 2> preferred = d.driver == "rtlsdr"
        ? std::array<double, 2>{2.048e6, 2.4e6}
        : std::array<double, 2>{2.4e6, 2.048e6};

    for (double target : preferred) {
        for (double rate : d.sampleRates) {
            if (std::isfinite(rate) && std::abs(rate - target) <= std::max(1.0, target * 0.002)) {
                return rate;
            }
        }
    }

    const double target = preferred.front();
    return *std::min_element(d.sampleRates.begin(), d.sampleRates.end(), [target](double a, double b) {
        if (!std::isfinite(a)) return false;
        if (!std::isfinite(b)) return true;
        return std::abs(a - target) < std::abs(b - target);
    });
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
                        if (!di.sampleRates.empty()) di.sampleRate = chooseDefaultSampleRate(di);

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
                            di.gainMin = gr.minimum();
                            di.gainMax = gr.maximum();
                            if (di.driver == "rtlsdr") {
                                di.gain = 20.0; // P1 audit smoking gun: 80 or 0.6*max overloads strong local WFM; 15-25 dB first test + BW 120-150 kHz
                            } else {
                                di.gain = std::min(40.0, gr.maximum() * 0.5);
                            }
                        } else {
                            di.gainName = "TUNER"; // common for RTL-SDR
                            if (di.driver == "rtlsdr") {
                                di.gain = 20.0;
                                di.gainMin = 0.0;
                                di.gainMax = 49.6;
                            }
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
                    di.gainMin = 0.0;
                    di.gainMax = 49.6;
                    di.gainName = "TUNER";
                } else {
                    di.antennas = {"TX/RX"};
                    di.antenna = "TX/RX";
                    di.sampleRates = {1e6, 2e6, 2.4e6, 5e6, 10e6};
                    di.sampleRate = 2.4e6;
                    di.minFreq = 1e6;
                    di.maxFreq = 6e9;
                    di.gain = 40.0;
                    di.gainMin = 0.0;
                    di.gainMax = 80.0;
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
            di.gainMin = 0.0;
            di.gainMax = 49.6;
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
    fake1.gainMin = 0.0;
    fake1.gainMax = 80.0;
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
    fake2.gain = 20.0;
    fake2.gainMin = 0.0;
    fake2.gainMax = 49.6;
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

void DeviceManager::updateDeviceParams(size_t index, double sampleRate, double gain, const std::string& antenna, double frequencyCorrectionPpm) {
    std::lock_guard<std::mutex> lk(devicesMutex);
    if (index >= devices.size()) return;
    auto& d = devices[index];
    d.sampleRate = sampleRate;
    d.gain = clampGainForDevice(d, gain);
    d.antenna = antenna;
    d.frequencyCorrectionPpm = clampFrequencyCorrectionPpm(frequencyCorrectionPpm);
    saveSettings();
    spdlog::debug("Updated params for {}: rate={}, gain={}, ant={}, ppm={}", d.label, sampleRate, d.gain, antenna, d.frequencyCorrectionPpm);
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
        j["frequencyCorrectionPpm"] = d.frequencyCorrectionPpm;
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
                if (saved.contains("gain")) d.gain = clampGainForDevice(d, saved["gain"].get<double>());
                if (saved.contains("frequencyCorrectionPpm")) d.frequencyCorrectionPpm = clampFrequencyCorrectionPpm(saved["frequencyCorrectionPpm"].get<double>());
                else if (saved.contains("ppm")) d.frequencyCorrectionPpm = clampFrequencyCorrectionPpm(saved["ppm"].get<double>());
                if (saved.contains("antenna")) d.antenna = saved["antenna"];
                break;
            }
        }
    }
}

void DeviceManager::resetStreamBuffers(StreamState& st) {
    {
        std::lock_guard<std::mutex> lk(st.queueMutex);
        st.iqQueue.clear();
        st.frontBlockReadOffset = 0;
        st.latestPower.clear();
        st.spectrumAvg.clear();
        st.spectrumPeak.clear();
    }

    {
        std::lock_guard<std::mutex> ringLock(st.ringMutex);
        if (st.ringCapacity == 0) {
            st.ringCapacity = 1u << 22; // 4M samples
            st.iqRing.assign(st.ringCapacity, std::complex<float>(0, 0));
        }
        st.ringWriteIdx.store(0, std::memory_order_release);
        st.totalSamplesWritten.store(0, std::memory_order_release);
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

    bool wasActive = false;
    bool wasReal = false;
    {
        std::lock_guard<std::mutex> lk(st.stateMutex);
        wasActive = st.active;
        wasReal = st.isReal;
    }
    if (wasActive) {
        if (attemptReal && !wasReal) {
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
    const uint64_t streamGen = st.sessionGen.fetch_add(1, std::memory_order_acq_rel) + 1;
    st.stopFlag = false;
    st.currentCenter = 100e6;
    double useRate = d.sampleRate;
    if (useRate < 0.25e6 || useRate > 60e6) useRate = (d.driver == "rtlsdr" ? 2.048e6 : 2.4e6);
    st.currentRate = useRate;
    st.frequencyCorrectionPpm = clampFrequencyCorrectionPpm(d.frequencyCorrectionPpm);
    st.nativeFrequencyCorrectionActive = false;

    // S0 / audit-followup-2: reset per-device IQ buffers for every session so a new
    // tune/enable cannot consume stale IQ from a previous station, stub, or failed real handoff.
    resetStreamBuffers(st);

    setupSoapyForRTLSDR();

    {
        std::lock_guard<std::mutex> lk(st.stateMutex);
        st.active = true;
        st.isReal = false;
        st.runtimeState = attemptReal ? "opening hardware (stub active)" : "simulated/stub";
    }
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
    uint64_t myGen = streamGen;

    // Best practice per audit (P0 shutdown hang): NEVER use jthread (or any joinable thread handle that the caller will join) for the untrusted native Soapy open/make path.
    // SoapySDR::Device::make + USB driver stack can block forever on bad hardware/state. We launch a detached open-worker.
    // All safety is via sessionGen + stopFlag captured at launch time. The worker self-aborts and cleans (unmake) if gen mismatches or stop set.
    // stopStreaming simply bumps gen + stopFlag and never joins this worker. Abandoned make threads are reaped on process exit (acceptable; alternative is out-of-proc probe helper).
    st.realInitThread = std::thread([this, index, d, useRate, myGen]() mutable {
        if (index >= streams.size() || !streams[index]) return;
        auto& st = *streams[index];
        if (st.stopFlag) return;
        if (st.sessionGen.load() != myGen) return;
        SoapySDR::Device* localDev = nullptr;
        SoapySDR::Stream* localStream = nullptr;
        auto cleanupLocal = [&]() {
            try {
                if (localStream && localDev) localDev->closeStream(localStream);
                if (localDev) SoapySDR::Device::unmake(localDev);
            } catch (...) {}
            localDev = nullptr;
            localStream = nullptr;
        };
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
            localDev = SoapySDR::Device::make(args);
            if (!localDev) throw std::runtime_error("make returned null");

            localDev->setSampleRate(SOAPY_SDR_RX, 0, useRate);
            if (!d.antenna.empty()) try { localDev->setAntenna(SOAPY_SDR_RX, 0, d.antenna); } catch (...) {}

            // Re-read the *latest* desired gain right before applying (user may have changed the main GUI
            // RF Gain spin or the Device Manager dialog *while* this background Soapy open/make/activate
            // was running in the detached thread). This is a key part of making "live" gain reliable.
            double useGain;
            std::string useGainName;
            double usePpm = d.frequencyCorrectionPpm;
            {
                std::lock_guard<std::mutex> lk(devicesMutex);
                if (index < devices.size()) {
                    useGain = clampGainForDevice(devices[index], devices[index].gain);
                    useGainName = devices[index].gainName;
                    usePpm = clampFrequencyCorrectionPpm(devices[index].frequencyCorrectionPpm);
                } else {
                    useGain = clampGainForDevice(d, d.gain);
                    useGainName = d.gainName;
                    usePpm = clampFrequencyCorrectionPpm(d.frequencyCorrectionPpm);
                }
            }

            try { localDev->setGainMode(SOAPY_SDR_RX, 0, false); } catch (...) {}
            if (!useGainName.empty()) {
                try { localDev->setGain(SOAPY_SDR_RX, 0, useGainName, useGain); } catch (...) { localDev->setGain(SOAPY_SDR_RX, 0, useGain); }
            } else {
                try { localDev->setGain(SOAPY_SDR_RX, 0, useGain); } catch (...) {}
            }
            double center = 100e6;
            {
                std::lock_guard<std::mutex> lk(st.queueMutex);
                center = st.currentCenter;
            }
            bool nativePpm = false;
            try {
                if (localDev->hasFrequencyCorrection(SOAPY_SDR_RX, 0)) {
                    localDev->setFrequencyCorrection(SOAPY_SDR_RX, 0, usePpm);
                    nativePpm = true;
                    spdlog::info("Applied native frequency correction to device {}: {} ppm", index, usePpm);
                }
            } catch (const std::exception& ex) {
                spdlog::warn("Native frequency correction unavailable for device {}: {}", index, ex.what());
                nativePpm = false;
            } catch (...) {
                nativePpm = false;
            }
            const double tuneCenter = nativePpm ? center : correctedTuneFrequencyHz(center, usePpm);
            try { localDev->setFrequency(SOAPY_SDR_RX, 0, tuneCenter); } catch (...) {}

            localStream = localDev->setupStream(SOAPY_SDR_RX, "CF32");
            if (!localStream) throw std::runtime_error("setupStream null");
            localDev->activateStream(localStream);

            if (st.stopFlag || st.sessionGen.load() != myGen) {
                cleanupLocal();
                return;
            }
            if (st.sessionGen.load() != myGen) {
                cleanupLocal();
                return;
            }

            // Success path: stop stub (our code, normally safe to join), start real rx thread.
            // Keep this bounded anyway: a wedged thread must never block the async upgrade path.
            st.stopFlag = true;
            bool stubDetached = false;
            if (st.rxThread.joinable()) {
                auto start = std::chrono::steady_clock::now();
                while (st.rxThreadRunning.load(std::memory_order_acquire) &&
                       std::chrono::steady_clock::now() - start < std::chrono::milliseconds(500)) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                }
                if (st.rxThreadRunning.load(std::memory_order_acquire)) {
                    spdlog::warn("Stub rxThread for device {} did not stop during real upgrade; detaching and aborting upgrade.", index);
                    try { st.rxThread.detach(); } catch (...) {}
                    stubDetached = true;
                } else {
                    try { st.rxThread.join(); } catch (...) { try { st.rxThread.detach(); stubDetached = true; } catch (...) {} }
                }
            }
            if (stubDetached) {
                cleanupLocal();
                {
                    std::lock_guard<std::mutex> lk(st.stateMutex);
                    st.active = false;
                    st.isReal = false;
                    st.runtimeState = "driver stuck, restart recommended";
                }
                return;
            }
            if (st.sessionGen.load(std::memory_order_acquire) != myGen) {
                cleanupLocal();
                return;
            }
            {
                std::lock_guard<std::mutex> lk(st.stateMutex);
                st.soapyDev = localDev;
                st.rxStream = localStream;
                st.active = true;
                st.isReal = true;
                st.runtimeState = "live hardware";
                st.frequencyCorrectionPpm = usePpm;
                st.nativeFrequencyCorrectionActive = nativePpm;
                localDev = nullptr;
                localStream = nullptr;
            }
            resetStreamBuffers(st);
            st.stopFlag = false;
            st.rxThread = std::thread(&DeviceManager::rxThreadFunc, this, index);
            spdlog::info("Background upgrade: Started real Soapy streaming for device {}", index);

            // Catch-up: re-apply the current desired RF gain now that the real soapyDev is published and active.
            // This fixes the case where the user changed the main-screen "RF Gain (dB)" spin (or dialog gain)
            // *during* the time the detached realInitThread was doing the slow USB/Soapy make + activate.
            // setLiveGain will see the freshly assigned soapyDev and push the (latest) value to hardware.
            double latestGain = useGain;
            {
                std::lock_guard<std::mutex> lk(devicesMutex);
                if (index < devices.size()) {
                    latestGain = devices[index].gain;
                    // Call setLiveGain — it will re-update the model (harmless) and because soapyDev is now visible
                    // it will execute the live setGain path. This makes "live RF gain" work reliably even for
                    // changes made while the async hardware open was in flight.
                    // We do this *outside* the previous devices lock to avoid nested lock order issues.
                    // (setLiveGain will take its own brief devicesMutex.)
                    // Unlock first by ending the scope.
                }
            }
            // Now safe to call (no devicesMutex held).
            setLiveGain(index, latestGain);
            double latestPpm = usePpm;
            {
                std::lock_guard<std::mutex> lk(devicesMutex);
                if (index < devices.size()) latestPpm = devices[index].frequencyCorrectionPpm;
            }
            setFrequencyCorrection(index, latestPpm);
        } catch (const std::exception& ex) {
            spdlog::warn("Background real init failed for device {} ({}). Keeping safe stub.", index, ex.what());
            cleanupLocal();
            if (!st.stopFlag && st.sessionGen.load(std::memory_order_acquire) == myGen) {
                std::lock_guard<std::mutex> lk(st.stateMutex);
                st.runtimeState = "hardware failed, using stub";
            }
        } catch (...) {
            spdlog::warn("Background real init failed for device {} with unknown exception (SEH/driver). Keeping safe stub.", index);
            cleanupLocal();
            if (!st.stopFlag && st.sessionGen.load(std::memory_order_acquire) == myGen) {
                std::lock_guard<std::mutex> lk(st.stateMutex);
                st.runtimeState = "hardware failed, using stub";
            }
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
    {
        std::lock_guard<std::mutex> lk(st.stateMutex);
        st.runtimeState = "simulated/stub";
    }
#endif
    return true;
}

void DeviceManager::stopStreaming(size_t index) {
    if (index >= streams.size() || !streams[index]) return;
    auto& st = *streams[index];
    bool activeNow = false;
    bool soapyIdle =
#ifdef HAVE_SOAPYSDR
        true &&
#endif
        true;
    {
        std::lock_guard<std::mutex> lk(st.stateMutex);
        activeNow = st.active;
#ifdef HAVE_SOAPYSDR
        soapyIdle = !st.soapyDev;
#endif
    }
    if (!activeNow && soapyIdle && !st.realInitThread.joinable() && !st.rxThread.joinable()) return;

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
    bool rxDetached = false;
    if (st.rxThread.joinable()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        auto start = std::chrono::steady_clock::now();
        while (st.rxThreadRunning.load(std::memory_order_acquire)) {
            if (std::chrono::steady_clock::now() - start > std::chrono::milliseconds(300)) {
                spdlog::warn("rxThread for device {} still running after stop — detaching (possible stuck readStream / native driver).", index);
                try { st.rxThread.detach(); } catch (...) {}
                rxDetached = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
        }
        if (!rxDetached && st.rxThread.joinable()) {
            try { st.rxThread.join(); } catch (...) { try { st.rxThread.detach(); rxDetached = true; } catch (...) {} }
        }
    }

#ifdef HAVE_SOAPYSDR
    // Capture and null only when Soapy types are available (fixes no-Soapy build P1).
    SoapySDR::Device* devToClose = nullptr;
    SoapySDR::Stream* streamToClose = nullptr;
    {
        std::lock_guard<std::mutex> lk(st.stateMutex);
        devToClose = st.soapyDev;
        streamToClose = st.rxStream;
        st.soapyDev = nullptr;
        st.rxStream = nullptr;
    }

    try {
        if (rxDetached && devToClose) {
            // Closing/unmaking while a detached native readStream may still be using the device
            // is a use-after-free risk. Leak this stuck handle until process exit instead.
            spdlog::warn("Leaving Soapy device {} open because its rxThread was detached while stuck in native code.", index);
        } else if (streamToClose && devToClose) {
            devToClose->deactivateStream(streamToClose);
            devToClose->closeStream(streamToClose);
            SoapySDR::Device::unmake(devToClose);
        } else if (devToClose) {
            SoapySDR::Device::unmake(devToClose);
        }
    } catch (const std::exception& ex) {
        spdlog::warn("Exception during Soapy teardown for device {}: {}", index, ex.what());
    } catch (...) {
        spdlog::warn("Unknown exception during Soapy teardown for device {}", index);
    }
#endif

    {
        std::lock_guard<std::mutex> lk(st.stateMutex);
        st.active = false;
        st.isReal = false;
        st.nativeFrequencyCorrectionActive = false;
        st.runtimeState = "stopped";
    }
    resetStreamBuffers(st);
    spdlog::info("Stopped streaming for device {}", index);
}

bool DeviceManager::isStreaming(size_t index) const {
    if (index >= streams.size() || !streams[index]) return false;
    auto& st = *streams[index];
    std::lock_guard<std::mutex> lk(st.stateMutex);
    return st.active;
}

size_t DeviceManager::getNextIQBlock(size_t index, std::complex<float>* buffer, size_t maxSamples, int timeoutMs) {
    if (index >= streams.size() || !streams[index]) return 0;
    auto& st = *streams[index];
    {
        std::lock_guard<std::mutex> stateLock(st.stateMutex);
        if (!st.active) return 0;
    }

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
    {
        std::lock_guard<std::mutex> stateLock(st.stateMutex);
        if (!st.active) return false;
    }

    std::lock_guard<std::mutex> lk(st.queueMutex);
    if (st.latestPower.empty()) return false;
    powerDb = st.latestPower;
    centerFreq = st.currentCenter;
    sampleRate = st.currentRate;
    return true;
}

void DeviceManager::setSpectrumFftBins(size_t index, size_t bins) {
    const size_t fftBins = normalizeSpectrumFftBins(bins);
    {
        std::lock_guard<std::mutex> lk(devicesMutex);
        if (index >= devices.size()) return;
        if (streams.size() <= index) streams.resize(index + 1);
        if (!streams[index]) streams[index] = std::make_unique<StreamState>();
    }

    auto& st = *streams[index];
    {
        std::lock_guard<std::mutex> lk(st.queueMutex);
        if (st.spectrumBins == fftBins) return;
        st.spectrumBins = fftBins;
        st.latestPower.clear();
        st.spectrumAvg.clear();
        st.spectrumPeak.clear();
    }
    spdlog::info("Device {} spectrum FFT bins set to {}", index, fftBins);
}

size_t DeviceManager::getSpectrumFftBins(size_t index) const {
    if (index >= streams.size() || !streams[index]) return 8192;
    auto& st = *streams[index];
    std::lock_guard<std::mutex> lk(st.queueMutex);
    return st.spectrumBins;
}

double DeviceManager::getCurrentGain(size_t index) const {
    std::lock_guard<std::mutex> lk(devicesMutex);
    if (index >= devices.size()) return 0.0;
    return devices[index].gain;
}

void DeviceManager::setLiveGain(size_t index, double gainDb) {
    DeviceInfo d;
    double useGain = gainDb;

    // Always update the persisted / model value with the effective RF gain.
    {
        std::lock_guard<std::mutex> lk(devicesMutex);
        if (index >= devices.size()) return;
        useGain = clampGainForDevice(devices[index], gainDb);
        devices[index].gain = useGain;
        d = devices[index];
        saveSettings();
    }

#ifdef HAVE_SOAPYSDR
    // Apply live to hardware whenever we have an open Soapy device for this index and the stream is not stopped.
    // We no longer require the "isReal" flag (which is set late in the background upgrade path).
    // This makes GUI RF gain changes (main window spin and per-device dialog) take effect immediately on a running device.
    // The background real-init path still does its own initial setGain from the DeviceInfo snapshot at launch time.
    bool appliedLive = false;
    if (index < streams.size() && streams[index]) {
        auto& st = *streams[index];
        std::lock_guard<std::mutex> stateLock(st.stateMutex);
        if (st.soapyDev && !st.stopFlag) {
            try {
                try { st.soapyDev->setGainMode(SOAPY_SDR_RX, 0, false); } catch (...) {}
                if (!d.gainName.empty()) {
                    st.soapyDev->setGain(SOAPY_SDR_RX, 0, d.gainName, useGain);
                } else {
                    st.soapyDev->setGain(SOAPY_SDR_RX, 0, useGain);
                }
                spdlog::info("Live RF gain applied to device {}: {} dB (gainName='{}')", index, useGain, d.gainName);
                appliedLive = true;
            } catch (const std::exception& ex) {
                spdlog::warn("Failed to apply live gain to device {}: {}", index, ex.what());
            }
        }
    }
    if (!appliedLive) {
        spdlog::debug("Live RF gain for device {} recorded as {} dB (model updated). No active soapyDev yet (stub, real upgrade still in progress, or device not started). Hardware will see it on next real start or via catch-up after upgrade.", index, useGain);
    }
#endif
}

void DeviceManager::setFrequencyCorrection(size_t index, double ppm) {
    const double usePpm = clampFrequencyCorrectionPpm(ppm);
    {
        std::lock_guard<std::mutex> lk(devicesMutex);
        if (index >= devices.size()) return;
        devices[index].frequencyCorrectionPpm = usePpm;
        saveSettings();
    }

    if (index >= streams.size() || !streams[index]) return;
    auto& st = *streams[index];

    double logicalCenter = 0.0;
    {
        std::lock_guard<std::mutex> lk(st.queueMutex);
        logicalCenter = st.currentCenter;
    }

#ifdef HAVE_SOAPYSDR
    bool appliedNative = false;
    {
        std::lock_guard<std::mutex> stateLock(st.stateMutex);
        st.frequencyCorrectionPpm = usePpm;
        st.nativeFrequencyCorrectionActive = false;
        if (st.soapyDev && !st.stopFlag) {
            try {
                if (st.soapyDev->hasFrequencyCorrection(SOAPY_SDR_RX, 0)) {
                    st.soapyDev->setFrequencyCorrection(SOAPY_SDR_RX, 0, usePpm);
                    st.nativeFrequencyCorrectionActive = true;
                    appliedNative = true;
                }
            } catch (const std::exception& ex) {
                spdlog::warn("Failed native PPM correction on device {}: {}", index, ex.what());
                st.nativeFrequencyCorrectionActive = false;
            } catch (...) {
                st.nativeFrequencyCorrectionActive = false;
            }

            const double tuneHz = st.nativeFrequencyCorrectionActive
                ? logicalCenter
                : correctedTuneFrequencyHz(logicalCenter, usePpm);
            try {
                st.soapyDev->setFrequency(SOAPY_SDR_RX, 0, tuneHz);
            } catch (const std::exception& ex) {
                spdlog::warn("Retune after PPM correction failed for device {}: {}", index, ex.what());
            } catch (...) {}
        }
    }
    if (appliedNative) {
        spdlog::info("Live frequency correction applied to device {}: {} ppm (native)", index, usePpm);
    } else {
        spdlog::debug("Frequency correction for device {} recorded as {} ppm; using corrected tune fallback when needed.", index, usePpm);
    }
#else
    {
        std::lock_guard<std::mutex> stateLock(st.stateMutex);
        st.frequencyCorrectionPpm = usePpm;
        st.nativeFrequencyCorrectionActive = false;
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

std::string DeviceManager::getRuntimeStateLabel(size_t index) const {
    if (index >= streams.size() || !streams[index]) return "stopped";
    auto& st = *streams[index];
    std::lock_guard<std::mutex> lk(st.stateMutex);
    return st.runtimeState.empty() ? std::string("stopped") : st.runtimeState;
}

// S0-3 (P1): non-consuming recent window so N receivers on the same device each get a coherent
// recent RF capture for their private channelizer/demod. Read from the absolute-sample ring
// instead of the consuming iqQueue so monitor/CLI P25 sync cannot starve or disturb spectrum.
std::vector<std::complex<float>> DeviceManager::getRecentIQWindow(size_t index, size_t maxSamples) {
    if (index >= streams.size() || !streams[index] || maxSamples == 0) return {};
    auto& st = *streams[index];
    {
        std::lock_guard<std::mutex> stateLock(st.stateMutex);
        if (!st.active) return {};
    }

    std::lock_guard<std::mutex> ringLock(st.ringMutex);
    const size_t cap = st.ringCapacity;
    const uint64_t total = st.totalSamplesWritten.load(std::memory_order_acquire);
    if (cap == 0 || st.iqRing.empty() || total == 0) return {};

    const uint64_t available = std::min<uint64_t>(total, static_cast<uint64_t>(cap));
    const size_t toRead = static_cast<size_t>(std::min<uint64_t>(available, static_cast<uint64_t>(maxSamples)));
    std::vector<std::complex<float>> out;
    out.reserve(toRead);

    // Return the newest contiguous ring window in chronological order.
    const uint64_t start = total - static_cast<uint64_t>(toRead);
    const bool powerOfTwoCap = (cap & (cap - 1)) == 0;
    for (size_t k = 0; k < toRead; ++k) {
        const uint64_t absolute = start + static_cast<uint64_t>(k);
        const size_t idx = powerOfTwoCap
            ? static_cast<size_t>(absolute) & (cap - 1)
            : static_cast<size_t>(absolute % static_cast<uint64_t>(cap));
        out.push_back(st.iqRing[idx]);
    }
    return out;

}

// S0 / audit-followup-2: cursor based new-samples only, chronological, per-rx.
// Replaces the "always take newest overlapping window" anti-pattern that caused repeated demod of the same data / chop.
std::vector<std::complex<float>> DeviceManager::getNewSamplesForReceiver(size_t devIndex, Receiver& rx, size_t maxSamples) {
    if (devIndex >= streams.size() || !streams[devIndex] || maxSamples == 0) return {};
    auto& st = *streams[devIndex];
    if (st.ringCapacity == 0) return {};

    std::lock_guard<std::mutex> ringLock(st.ringMutex);
    uint64_t myLast = rx.lastConsumedAbsolute;
    uint64_t total = st.totalSamplesWritten.load(std::memory_order_acquire);
    uint64_t available = (total > myLast) ? (total - myLast) : 0;

    if (myLast > total) {
        myLast = (total > (uint64_t)maxSamples) ? (total - (uint64_t)maxSamples) : 0;
        rx.lastConsumedAbsolute = myLast;
        available = (total > myLast) ? (total - myLast) : 0;
    }

    // New/reactivated/retuned receivers should monitor the live edge, not drain old IQ
    // left in the shared ring from a previous station or mode.
    if (myLast == 0 && total > (uint64_t)maxSamples) {
        myLast = total - (uint64_t)maxSamples;
        rx.lastConsumedAbsolute = myLast;
        available = total - myLast;
    }

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

void DeviceManager::setReceiverCursorToLiveEdge(size_t devIndex, Receiver& rx) {
    if (devIndex >= streams.size() || !streams[devIndex]) {
        rx.lastConsumedAbsolute = 0;
        return;
    }
    auto& st = *streams[devIndex];
    std::lock_guard<std::mutex> ringLock(st.ringMutex);
    rx.lastConsumedAbsolute = st.totalSamplesWritten.load(std::memory_order_acquire);
}

void DeviceManager::appendIQBlock(size_t index, std::vector<std::complex<float>>&& block) {
    if (index >= streams.size() || !streams[index] || block.empty()) return;
    auto& st = *streams[index];

    // Feed the per-rx ring *first* while we still own the data (before any move into queue).
    if (st.ringCapacity > 0) {
        std::lock_guard<std::mutex> ringLock(st.ringMutex);
        size_t w = st.ringWriteIdx.load(std::memory_order_relaxed);
        for (const auto& s : block) {
            st.iqRing[w] = s;
            w = (w + 1) & (st.ringCapacity - 1);
        }
        st.ringWriteIdx.store(w, std::memory_order_release);
        st.totalSamplesWritten.fetch_add(block.size(), std::memory_order_release);
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
#ifdef _WIN32
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
#endif

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
    double ppm = 0.0;
    bool nativePpm = false;
#ifdef HAVE_SOAPYSDR
    SoapySDR::Device* dev = nullptr;
#endif
    {
        std::lock_guard<std::mutex> lk(st.stateMutex);
        if (!st.active) return;
        ppm = st.frequencyCorrectionPpm;
        nativePpm = st.nativeFrequencyCorrectionActive;
#ifdef HAVE_SOAPYSDR
        dev = st.soapyDev;
#endif
    }

    {
        std::lock_guard<std::mutex> lk(st.queueMutex);
        st.currentCenter = freqHz;
    }

#ifdef HAVE_SOAPYSDR
    if (dev) {
        try {
            const double tuneHz = nativePpm ? freqHz : correctedTuneFrequencyHz(freqHz, ppm);
            dev->setFrequency(SOAPY_SDR_RX, 0, tuneHz);
            spdlog::debug("Set center freq {} for device {} (hardware tune {}, ppm {})", freqHz, index, tuneHz, ppm);
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
    st.rxThreadRunning.store(true, std::memory_order_release);
    struct RunningGuard {
        StreamState& st;
        ~RunningGuard() { st.rxThreadRunning.store(false, std::memory_order_release); }
    } runningGuard{st};
    const uint64_t myGen = st.sessionGen.load(std::memory_order_acquire);
    const size_t blockSize = 32768; // much larger blocks to sustain 2MS/s+ without overflow. 2048 was only ~1ms of RF at 2.048MS/s.

#ifdef HAVE_SOAPYSDR
    SoapySDR::Device* dev = nullptr;
    SoapySDR::Stream* stream = nullptr;
    {
        std::lock_guard<std::mutex> lk(st.stateMutex);
        dev = st.soapyDev;
        stream = st.rxStream;
    }
    if (dev && stream) {
        std::vector<std::complex<float>> buff(blockSize);
        bool realReadFaulted = false;
        // Broad guard: native readStream / USB / driver faults in the background thread must never terminate the process.
        try {
            auto lastSpectrumTime = std::chrono::steady_clock::now();
            while (!st.stopFlag && st.sessionGen.load(std::memory_order_acquire) == myGen) {
                int flags = 0;
                long long timeNs = 0;
                void* buffs[] = { buff.data() };
                int numElems = dev->readStream(stream, buffs, blockSize, flags, timeNs, 100000);
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
                    // - Real radix-2 FFT, selectable 4K/8K/16K/64K bins
                    // - Hann + Blackman-Harris windows (Blackman-Harris for main viz)
                    // - Window taken from high-quality per-device ring (overlap friendly via ring)
                    // - Exponential averaging + peak hold (slow decay) maintained in StreamState
                    // - Throttled (~16-30 Hz) in RX thread to keep readStream lean (future: can move to dedicated spectrum worker thread)
                    // - Published as high-res latestPower so SpectrumWidget can do true-resolution zoomed waterfall from source history.
                    auto now = std::chrono::steady_clock::now();
                    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastSpectrumTime).count() > 45) {
                        size_t fftN = 8192;
                        {
                            std::lock_guard<std::mutex> lk(st.queueMutex);
                            fftN = st.spectrumBins;
                        }
                        std::vector<float> localPower;

                        // Draw window from ring (best continuous recent IQ, enables overlap if we hop)
                        std::vector<std::complex<float>> samples;
                        samples.reserve(fftN);
                        {
                            std::lock_guard<std::mutex> ringLock(st.ringMutex);
                            uint64_t total = st.totalSamplesWritten.load(std::memory_order_acquire);
                            if (st.ringCapacity > 0 && total >= fftN) {
                                uint64_t start = total - fftN;
                                for (size_t k = 0; k < fftN; ++k) {
                                    size_t idx = (start + k) % st.ringCapacity;
                                    samples.push_back(st.iqRing[idx]);
                                }
                            }
                        }
                        if (samples.empty()) {
                            size_t take = std::min((size_t)numRead, fftN);
                            for (size_t i = 0; i < take; ++i) samples.push_back(buff[i]);
                            while (samples.size() < fftN) samples.push_back({0.f, 0.f});
                        }

                        // Real FFT power (Blackman-Harris primary for clean dynamic range; Hann available)
                        localPower = computeRealFFTPower(samples, fftN, /*useBlackmanHarris=*/true);

                        double publishedRate = 0.0;
                        try {
                            publishedRate = dev->getSampleRate(SOAPY_SDR_RX, 0);
                        } catch (...) {}

                        // Publish the averaged high-res spectrum (UI can choose peak if wanted later)
                        {
                            std::lock_guard<std::mutex> lk(st.queueMutex);
                            // Exponential avg + peak hold (state lives in StreamState for continuity across calls)
                            if (st.spectrumAvg.size() != localPower.size()) {
                                st.spectrumAvg.assign(localPower.size(), -110.0f);
                                st.spectrumPeak.assign(localPower.size(), -110.0f);
                            }
                            std::vector<float> published(localPower.size(), -120.0f);
                            for (size_t b = 0; b < localPower.size(); ++b) {
                                st.spectrumAvg[b] = st.spectrumAvg[b] * 0.72f + localPower[b] * 0.28f;
                                float decayedPeak = std::max(-180.0f, st.spectrumPeak[b] - 0.8f);
                                st.spectrumPeak[b] = std::max(decayedPeak, localPower[b]);
                                published[b] = std::max(st.spectrumAvg[b], st.spectrumPeak[b] - 4.0f);
                            }
                            st.latestPower = std::move(published); // high bin count vector, avg + fast peak visibility
                            if (publishedRate > 0.0 && std::isfinite(publishedRate)) st.currentRate = publishedRate;
                        }
                        lastSpectrumTime = now;
                    }
                }
                // No unconditional sleep. Only yield if queue is getting very full (backpressure).
                // P2 audit: decide under the lock, then sleep after releasing it so consumers are never blocked by backpressure.
                bool shouldBackpressureSleep = false;
                {
                    std::lock_guard<std::mutex> lk(st.queueMutex);
                    shouldBackpressureSleep = st.iqQueue.size() > 96;
                }
                if (shouldBackpressureSleep) {
                    std::this_thread::sleep_for(std::chrono::microseconds(200));
                }
            }
        } catch (const std::exception& ex) {
            spdlog::error("Exception in real rxThread for device {}: {}.", index, ex.what());
            realReadFaulted = true;
        } catch (...) {
            spdlog::error("Unknown exception in real rxThread for device {} (possible USB/driver fault).", index);
            realReadFaulted = true;
        }
        if (!realReadFaulted || st.stopFlag || st.sessionGen.load(std::memory_order_acquire) != myGen) {
            return;
        }

        spdlog::warn("Real RX thread for device {} faulted; falling back to safe stub streaming for this session.", index);
        {
            std::lock_guard<std::mutex> lk(st.stateMutex);
            if (st.soapyDev == dev) st.soapyDev = nullptr;
            if (st.rxStream == stream) st.rxStream = nullptr;
            st.isReal = false;
            st.active = true;
            st.runtimeState = "hardware failed, using stub";
        }
        try {
            if (stream && dev) {
                dev->deactivateStream(stream);
                dev->closeStream(stream);
            }
            if (dev) SoapySDR::Device::unmake(dev);
        } catch (const std::exception& ex) {
            spdlog::warn("Exception while cleaning faulted Soapy device {}: {}", index, ex.what());
        } catch (...) {
            spdlog::warn("Unknown exception while cleaning faulted Soapy device {}", index);
        }
        resetStreamBuffers(st);
    }
#endif

    // Stub simulation: generate IQ with a few carriers + noise, update spectrum
    double t = 0.0;
    const double fs = 2.4e6;
    auto nextStubBlockTime = std::chrono::steady_clock::now();
    const auto stubBlockPeriod = std::chrono::duration<double>((double)blockSize / fs);
    while (!st.stopFlag && st.sessionGen.load(std::memory_order_acquire) == myGen) {
        std::vector<std::complex<float>> block(blockSize);
        for (size_t i = 0; i < blockSize; ++i) {
            double phase = 2 * 3.14159265 * (100e6 + 0.1e6 * std::sin(t * 0.3)) / fs * i; // drifting carrier example
            float re = std::cos(phase) * 0.8f + (rand() % 1000 - 500) * 0.0002f;
            float im = std::sin(phase) * 0.8f + (rand() % 1000 - 500) * 0.0002f;
            block[i] = {re, im};
        }
        // Centralized append: ring fed before move (no more "ring receives no samples" after std::move)
        appendIQBlock(index, std::move(block));

        // spectrum for stub (same real FFT path so zoomed views look consistent and high-res even in demo/no-hw)
        {
            size_t fftN = 8192;
            {
                std::lock_guard<std::mutex> lk(st.queueMutex);
                fftN = st.spectrumBins;
            }
            std::vector<std::complex<float>> fake(fftN);
            for (size_t i = 0; i < fftN; ++i) {
                double phase = 2 * 3.14159265 * (100e6 + 0.1e6 * std::sin(t * 0.3)) / fs * (double)i;
                float re = std::cos(phase) * 0.7f + (rand() % 1000 - 500) * 0.00015f;
                float im = std::sin(phase) * 0.7f + (rand() % 1000 - 500) * 0.00015f;
                fake[i] = {re, im};
            }
            auto lp = computeRealFFTPower(fake, fftN, true);
            {
                std::lock_guard<std::mutex> lk(st.queueMutex);
                if (st.spectrumAvg.size() != lp.size()) {
                    st.spectrumAvg = lp;
                    st.spectrumPeak = lp;
                }
                std::vector<float> published(lp.size(), -120.0f);
                for (size_t b = 0; b < lp.size(); ++b) {
                    st.spectrumAvg[b] = st.spectrumAvg[b] * 0.7f + lp[b] * 0.3f;
                    float decayedPeak = std::max(-180.0f, st.spectrumPeak[b] - 0.8f);
                    st.spectrumPeak[b] = std::max(decayedPeak, lp[b]);
                    published[b] = std::max(st.spectrumAvg[b], st.spectrumPeak[b] - 4.0f);
                }
                st.latestPower = std::move(published);
                st.currentRate = fs;
            }
        }

        t += 0.05;
        nextStubBlockTime += std::chrono::duration_cast<std::chrono::steady_clock::duration>(stubBlockPeriod);
        auto now = std::chrono::steady_clock::now();
        if (nextStubBlockTime > now) {
            std::this_thread::sleep_until(nextStubBlockTime);
        } else if (now - nextStubBlockTime > std::chrono::milliseconds(100)) {
            nextStubBlockTime = now;
        }
    }
}
