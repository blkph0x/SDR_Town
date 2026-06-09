#include "DeviceManager.h"

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

    setupSoapyForRTLSDR();

    st.active = true;
    st.isReal = false;
    st.rxThread = std::thread(&DeviceManager::rxThreadFunc, this, index);
    if (!attemptReal) {
        spdlog::info("Started stub/sim streaming for device {} (safe mode)", index);
        return true;
    }

    // attemptReal=true (explicit user action): try the real Soapy open in a *background thread* assigned to the owned realInitThread.
    // We detach on stop (with guards) to avoid hanging UI/CLI/harness shutdown if Soapy make/setup is stuck (e.g. RTL driver hang).
    // Early stopFlag + post-make guard in lambda prevent resurrection/writes to soapyDev/rxStream/active after stop.
    // If it hangs, throws, or SEH-crashes inside the native driver, the main app + stub keeps running.
    // This (plus /EHa) fixes the init race without blocking.
    // Bump generation so any previous in-flight init knows it is stale.
    uint64_t myGen = ++st.sessionGen;

    st.realInitThread = std::thread([this, index, d, useRate, myGen]() mutable {
        if (index >= streams.size() || !streams[index]) return;
        auto& st = *streams[index];
        if (st.stopFlag) return;
        // If a stop has already bumped the generation, do not touch shared state.
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
            // P1 audit (smoking gun): RTL gain 80 (or even 30+) from devices.json / prior probe overloads front-end on strong local WFM -> scratch/crackle.
            // Now defaults/caps to 15-25 range. Test first with gain 15-25 + WFM BW 120-150 kHz before any DSP changes.
            if (d.driver == "rtlsdr" && useGain > 25.0) {
                spdlog::warn("RTL gain {} too high for strong signals (overload -> scratch/crackle on WFM). Capping to 20 dB. Use CLI 'gain 0 20' or lower devices.json.", useGain);
                useGain = 20.0;
            } else if (d.driver == "rtlsdr" && useGain < 5.0) {
                useGain = 15.0; // floor for usable sensitivity on weaker but still local stations
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
                // Stopped or a newer session took over during/after init; cleanup without resurrecting state.
                try {
                    if (st.rxStream && st.soapyDev) st.soapyDev->closeStream(st.rxStream);
                    if (st.soapyDev) SoapySDR::Device::unmake(st.soapyDev);
                } catch (...) {}
                st.soapyDev = nullptr;
                st.rxStream = nullptr;
                return;
            }

            // Success: only publish if generation still matches (prevents teardown race).
            if (st.sessionGen.load() != myGen) return;

            // Success: stop the stub thread we started in the caller, then start the real rx thread.
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
            // stub thread is already running from the fast path above
        } catch (...) {
            spdlog::warn("Background real init failed for device {} with unknown exception (SEH/driver). Keeping safe stub.", index);
            if (st.soapyDev) { SoapySDR::Device::unmake(st.soapyDev); st.soapyDev = nullptr; }
            st.rxStream = nullptr;
        }
    });

    return true;
}

void DeviceManager::stopStreaming(size_t index) {
    if (index >= streams.size() || !streams[index]) return;
    auto& st = *streams[index];
    if (!st.active && !st.soapyDev && !st.realInitThread.joinable() && !st.rxThread.joinable()) return;

    // P1: bump generation *first* so any in-flight init thread will see the mismatch and refuse to publish/teardown.
    st.sessionGen.fetch_add(1, std::memory_order_acq_rel);
    st.stopFlag = true;

    // Capture the resources that belong to the session we are stopping (prevents a racing init from changing them under us).
    SoapySDR::Device* devToClose = st.soapyDev;
    SoapySDR::Stream* streamToClose = st.rxStream;
    st.soapyDev = nullptr;
    st.rxStream = nullptr;

    if (st.realInitThread.joinable()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
        st.realInitThread.detach();
    }

    if (st.rxThread.joinable()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        st.rxThread.join();
    }

#ifdef HAVE_SOAPYSDR
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

size_t DeviceManager::getIQQueueDepth(size_t index) const {
    // Safe under stream's queueMutex (P2 audit: was unlocked .size() read in rx path too)
    if (index >= streams.size() || !streams[index]) return 0;
    auto& st = *streams[index];
    std::lock_guard<std::mutex> lk(st.queueMutex);
    return st.iqQueue.size();
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
                    {
                        std::lock_guard<std::mutex> lk(st.queueMutex);
                        st.iqQueue.push_back(std::move(block));
                        // keep queue bounded but larger to absorb bursts
                        while (st.iqQueue.size() > 128) st.iqQueue.pop_front();
                    }

                    // Throttled spectrum: do NOT compute expensive 256-bin DFT on every read.
                    // RX thread must stay lean to avoid SOAPY_SDR_OVERFLOW. Spectrum at ~20 Hz is plenty for UI.
                    auto now = std::chrono::steady_clock::now();
                    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastSpectrumTime).count() > 50) {
                        const int sbins = 256;
                        std::vector<float> localPower(sbins, -100.0f);
                        int N = std::min(256, (int)numRead);
                        if (N > 0) {
                            int startIdx = numRead - N;
                            for (int b = 0; b < sbins; ++b) {
                                std::complex<double> sum(0, 0);
                                double binFreq = (double)(b - sbins/2) / sbins;
                                double w = -2 * 3.1415926535 * binFreq;
                                for (int n = 0; n < N; ++n) {
                                    auto s = buff[startIdx + n];
                                    double phi = w * n;
                                    sum += std::complex<double>(s.real(), s.imag()) * std::complex<double>(std::cos(phi), std::sin(phi));
                                }
                                double p = std::norm(sum) / (N * N);
                                float db = 10.0f * std::log10(std::max(p, 1e-12));
                                localPower[b] = std::max(localPower[b] * 0.7f + db * 0.3f, db - 20);
                            }
                        }
                        try {
                            double devRate = st.soapyDev->getSampleRate(SOAPY_SDR_RX, 0);
                            std::lock_guard<std::mutex> lk(st.queueMutex);
                            st.latestPower = std::move(localPower);
                            st.currentRate = devRate;
                        } catch (...) {
                            std::lock_guard<std::mutex> lk(st.queueMutex);
                            st.latestPower = std::move(localPower);
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
        {
            std::lock_guard<std::mutex> lk(st.queueMutex);
            st.iqQueue.push_back(std::move(block));
            while (st.iqQueue.size() > 64) st.iqQueue.pop_front();
            // (size check for backpressure also under this lock in real path; stub production is slow so no extra yield here)
        }

        // spectrum for stub - compute local then publish under lock to avoid race with GUI/CLI getLatestSpectrum + demod
        size_t bins = 256;
        std::vector<float> localPower(bins, -95.0f);
        for (size_t b = 0; b < bins; ++b) {
            // simple energy in bin (demo)
            float energy = -90 + 25 * std::exp(-std::pow((double)b - 80 + 5*std::sin(t), 2) / 30.0);
            energy += (rand() % 10 - 5) * 0.5f;
            localPower[b] = std::max(localPower[b], energy);
        }
        {
            std::lock_guard<std::mutex> lk(st.queueMutex);
            st.latestPower = std::move(localPower);
            st.currentRate = fs;
            // Do NOT overwrite currentCenter here every frame. setCenterFreq (from GUI/CLI clicks or Set & Tune)
            // updates it for the demod (currentMonitorFreq) and display. Overwriting made tuning "snap back".
            // The stub is a demo around a nominal 100 MHz carrier; the reported center for getLatest is
            // controlled by the tuning path.
        }

        t += 0.05;
        std::this_thread::sleep_for(std::chrono::milliseconds(2)); // faster production for real-time-ish audio rate in stub demo (was 40ms causing data starvation for 48kHz audio)
    }
}