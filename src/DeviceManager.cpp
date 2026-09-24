#include "DeviceManager.h"
#include "Receiver.h"   // for getNewSamplesForReceiver(..., Receiver& rx, ... ) cursor update
#include "SdrplayProfile.h"
#include "SdrplayDiversity.h"

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
#include <set>
#include <map>
#include <QCoreApplication>
#include <cstdlib>
#ifdef _WIN32
#include <windows.h> // for GetFileAttributesA
#endif

#ifdef HAVE_SOAPYSDR
#include <SoapySDR/Logger.hpp>
#endif

#ifdef HAVE_SOAPYSDR
// Serialize live Soapy I/O calls that can otherwise race during P25 voice-follow retunes.
// RTL/USB backends are not always safe when setFrequency/setGain/PPM happens while
// readStream is active; that race matches the observed hang right after TG follow.
static std::mutex gSoapyLiveIoMutex;

// RSPduo Dual Tuner: one Soapy device, two RX channels/streams.
struct SharedSdrplayDevice {
    SoapySDR::Device* dev = nullptr;
    int refCount = 0;
};
static std::mutex gSharedSdrplayMutex;
static std::map<std::string, SharedSdrplayDevice> gSharedSdrplayDevices;

static std::string sdrplayShareKey(const DeviceInfo& d) {
    return d.serial + "@" + (d.sdrplayDuoMode.empty() ? "ST" : d.sdrplayDuoMode);
}
#endif

DeviceManager::DeviceManager() = default;

DeviceManager::~DeviceManager()
{
    // Fail-safe: never leave PA/TX stream running after process teardown.
    try { stopAllTx(); } catch (...) {}
}

DeviceManager& DeviceManager::instance() {
    static DeviceManager mgr;
    return mgr;
}

DeviceManager::StreamState* DeviceManager::streamState(size_t index) const {
    std::lock_guard<std::mutex> lk(devicesMutex);
    if (index >= streams.size()) return nullptr;
    return streams[index].get();
}

void DeviceManager::ensureTxStreamSlot(size_t index)
{
    std::lock_guard<std::mutex> lk(txStreamsMutex);
    if (index >= txStreams.size()) {
        txStreams.resize(index + 1);
    }
    if (!txStreams[index]) {
        txStreams[index] = std::make_unique<TxStreamState>();
    }
}

DeviceManager::TxStreamState* DeviceManager::txStreamState(size_t index)
{
    std::lock_guard<std::mutex> lk(txStreamsMutex);
    if (index >= txStreams.size()) return nullptr;
    return txStreams[index].get();
}

const DeviceManager::TxStreamState* DeviceManager::txStreamState(size_t index) const
{
    std::lock_guard<std::mutex> lk(txStreamsMutex);
    if (index >= txStreams.size()) return nullptr;
    return txStreams[index].get();
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

static int clampDirectSamplingMode(int mode) {
    if (mode < 0 || mode > 2) return 0;
    return mode;
}

static bool isRtlBlogV4(const DeviceInfo& d) {
    std::string name = d.label + " " + d.hardware;
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return d.driver == "rtlsdr" && name.find("blog") != std::string::npos && name.find("v4") != std::string::npos;
}

static void updateRtlFreqLimitsForDirectSampling(DeviceInfo& d) {
    if (d.driver != "rtlsdr") return;
    if (d.tunerMaxFreq <= 0) {
        d.tunerMinFreq = d.minFreq;
        d.tunerMaxFreq = d.maxFreq;
    }
    if (isRtlBlogV4(d)) d.directSampling = 0;
    if (d.directSampling > 0) {
        // R820T bypassed — ADC samples HF directly (~500 kHz usable floor).
        d.minFreq = 500e3;
        d.maxFreq = 28.8e6;
    } else {
        d.minFreq = d.tunerMinFreq;
        d.maxFreq = d.tunerMaxFreq;
    }
}

#ifdef HAVE_SOAPYSDR
static bool applySoapyDirectSampling(SoapySDR::Device* dev, int mode, size_t indexForLog) {
    if (!dev) return false;
    const int useMode = clampDirectSamplingMode(mode);
    const std::string value = std::to_string(useMode);
    // SoapyRTLSDR primary key is direct_samp; try aliases for forks.
    static const char* kKeys[] = {"direct_samp", "directSamp", "direct_sampling"};
    const auto settings = dev->getSettingInfo();
    for (const char* key : kKeys) {
        if (std::none_of(settings.begin(), settings.end(), [key](const auto& setting) { return setting.key == key; })) continue;
        try {
            dev->writeSetting(key, value);
            if (dev->readSetting(key) != value) continue;
            spdlog::info("Applied RTL direct sampling on device {}: {}={} ({})",
                         indexForLog, key, value,
                         useMode == 0 ? "tuner/off" : (useMode == 1 ? "I-ADC" : "Q-ADC"));
            return true;
        } catch (const std::exception& ex) {
            spdlog::debug("direct sampling key '{}' failed on device {}: {}", key, indexForLog, ex.what());
        } catch (...) {
            spdlog::debug("direct sampling key '{}' failed on device {}: unknown error", key, indexForLog);
        }
    }
    if (useMode != 0) {
        spdlog::warn("Could not apply direct sampling mode {} on device {} (setting unsupported?)",
                     useMode, indexForLog);
    }
    return false;
}
#endif

static double correctedTuneFrequencyHz(double logicalHz, double ppm) {
    if (!std::isfinite(logicalHz) || logicalHz <= 0.0) return logicalHz;
    ppm = clampFrequencyCorrectionPpm(ppm);
    const double scale = 1.0 + ppm * 1e-6;
    if (std::abs(scale) < 1e-9) return logicalHz;
    return logicalHz / scale;
}

static std::string makeDeviceStableKey(const DeviceInfo& d) {
    std::string key = d.driver;
    key += "|";
    if (!d.serial.empty()) {
        key += "serial:" + d.serial;
    } else if (!d.hardware.empty()) {
        key += "hardware:" + d.hardware;
    } else {
        key += "label:" + d.label;
    }
    if (d.isSdrplay) {
        if (!d.sdrplayDuoMode.empty()) key += "|duo:" + d.sdrplayDuoMode;
        key += "|ch:" + std::to_string(d.rxChannel);
    }
    return key;
}

#ifdef HAVE_SOAPYSDR
static void applySdrplayProfileToDevice(SoapySDR::Device* dev, const DeviceInfo& d, size_t indexForLog) {
    if (!dev || !d.isSdrplay) return;
    const size_t ch = d.rxChannel;
    try {
        if (d.bandwidthHz > 0.0) {
            try { dev->setBandwidth(SOAPY_SDR_RX, ch, d.bandwidthHz); } catch (...) {}
        }
        try { dev->setGainMode(SOAPY_SDR_RX, ch, d.agcEnabled); } catch (...) {}
        if (!d.agcEnabled) {
            try { dev->setGain(SOAPY_SDR_RX, ch, "IFGR", d.ifgrDb); } catch (...) {}
            try { dev->setGain(SOAPY_SDR_RX, ch, "RFGR", d.rfgrDb); } catch (...) {}
            if (!d.gainName.empty()) {
                try { dev->setGain(SOAPY_SDR_RX, ch, d.gainName, d.gain); } catch (...) {}
            }
        }
        for (const auto& kv : d.soapySettings) {
            try {
                dev->writeSetting(kv.first, kv.second);
            } catch (const std::exception& ex) {
                spdlog::debug("SDRplay setting {}={} failed on device {}: {}", kv.first, kv.second, indexForLog, ex.what());
            } catch (...) {}
        }
        spdlog::info("Applied SDRplay profile on device {} ch{} AGC={} IFGR={} RFGR={} settings={}",
                     indexForLog, ch, d.agcEnabled, d.ifgrDb, d.rfgrDb, d.soapySettings.size());
    } catch (const std::exception& ex) {
        spdlog::warn("SDRplay profile apply failed on device {}: {}", indexForLog, ex.what());
    }
}

static void enrichSdrplayDeviceInfo(SoapySDR::Device* dev, DeviceInfo& di, size_t channel) {
    if (!dev) return;
    di.isSdrplay = true;
    di.rxChannel = channel;
    di.canTx = false;
    di.sdrplayModel = SdrplayProfile::normalizeModel(di.hardware, di.label);

    try {
        auto gains = dev->listGains(SOAPY_SDR_RX, channel);
        di.gainElements.assign(gains.begin(), gains.end());
    } catch (...) {}
    try {
        auto ants = dev->listAntennas(SOAPY_SDR_RX, channel);
        di.antennas.assign(ants.begin(), ants.end());
        if (!di.antennas.empty() && di.antenna.empty()) di.antenna = di.antennas[0];
    } catch (...) {}
    try {
        auto bws = dev->listBandwidths(SOAPY_SDR_RX, channel);
        di.bandwidthsHz.assign(bws.begin(), bws.end());
    } catch (...) {}
    try {
        auto info = dev->getSettingInfo();
        di.sdrplaySettingKeys.clear();
        di.sdrplaySettingOptions.clear();
        for (const auto& arg : info) {
            di.sdrplaySettingKeys.push_back(arg.key);
            if (!arg.options.empty()) {
                di.sdrplaySettingOptions[arg.key] = arg.options;
            }
        }
    } catch (...) {}

    // Prefer RFGR as the "main" gain knob (LNA / RF gain reduction).
    di.gainName = "RFGR";
    try {
        auto gr = dev->getGainRange(SOAPY_SDR_RX, channel, "RFGR");
        di.gainMin = gr.minimum();
        di.gainMax = gr.maximum();
        di.rfgrDb = std::clamp(di.rfgrDb, di.gainMin, di.gainMax);
        di.gain = di.rfgrDb;
    } catch (...) {
        di.gainMin = 0.0;
        di.gainMax = 27.0;
    }
    try {
        auto igr = dev->getGainRange(SOAPY_SDR_RX, channel, "IFGR");
        di.ifgrDb = std::clamp(di.ifgrDb, igr.minimum(), igr.maximum());
    } catch (...) {}

    auto caps = SdrplayProfile::capabilitiesFromProbe(
        di.driver, di.hardware, di.label, di.gainElements, di.antennas,
        di.bandwidthsHz, di.sdrplaySettingKeys, di.sdrplaySettingOptions);
    if (di.soapySettings.empty()) {
        auto defaults = SdrplayProfile::defaultSettings(caps);
        di.soapySettings = defaults.soapySettings;
        di.agcEnabled = defaults.agcEnabled;
        di.ifgrDb = defaults.ifgrDb;
        di.rfgrDb = defaults.rfgrDb;
        di.gain = di.rfgrDb;
    }
}
#endif

static size_t normalizeSpectrumFftBins(size_t bins) {
    if (bins <= 4096) return 4096;
    if (bins <= 8192) return 8192;
    if (bins <= 16384) return 16384;
    return 65536;
}

static double chooseDefaultSampleRate(const DeviceInfo& d) {
    if (d.sampleRates.empty()) {
        if (d.isSdrplay) return SdrplayProfile::clampSampleRateHz(d.sdrplayDuoMode, 2.048e6);
        return d.driver == "rtlsdr" ? 2.048e6 : 2.4e6;
    }
    const std::array<double, 2> preferred = d.isSdrplay
        ? (d.sdrplayDuoMode == "DT" ? std::array<double, 2>{2.0e6, 1.0e6}
                                    : std::array<double, 2>{2.048e6, 2.0e6})
        : (d.driver == "rtlsdr" ? std::array<double, 2>{2.048e6, 2.4e6}
                                : std::array<double, 2>{2.4e6, 2.048e6});

    for (double target : preferred) {
        for (double rate : d.sampleRates) {
            if (std::isfinite(rate) && std::abs(rate - target) <= std::max(1.0, target * 0.002)) {
                return rate;
            }
        }
    }

    const double target = preferred.front();
    double picked = *std::min_element(d.sampleRates.begin(), d.sampleRates.end(), [target](double a, double b) {
        if (!std::isfinite(a)) return false;
        if (!std::isfinite(b)) return true;
        return std::abs(a - target) < std::abs(b - target);
    });
    if (d.isSdrplay) picked = SdrplayProfile::clampSampleRateHz(d.sdrplayDuoMode, picked);
    return picked;
}

std::vector<DeviceInfo> DeviceManager::enumerateDevices(bool probeHardware, bool stopActiveStreams) {
    std::vector<size_t> activeStreams;
    {
        std::lock_guard<std::mutex> lk(devicesMutex);
        for (size_t i = 0; i < streams.size(); ++i) {
            if (!streams[i]) continue;
            std::lock_guard<std::mutex> stateLock(streams[i]->stateMutex);
            if (streams[i]->active) activeStreams.push_back(i);
        }
    }
    if (stopActiveStreams) {
        for (size_t index : activeStreams) {
            spdlog::warn("Stopping active stream {} before device re-enumeration to avoid index/device identity mismatch.", index);
            stopStreaming(index);
        }
    } else if (!activeStreams.empty()) {
        spdlog::info("enumerateDevices skipped stream teardown ({} active); returning current device list",
                     activeStreams.size());
        std::lock_guard<std::mutex> lk(devicesMutex);
        return devices;
    }

    {
    std::lock_guard<std::mutex> lk(devicesMutex);
    devices.clear();

    setupSoapyForRTLSDR();
    setupSoapyForSDRplay();

#ifdef HAVE_SOAPYSDR
    try {
        SoapySDR::setLogLevel(SOAPY_SDR_INFO); // reduce noise
        auto results = SoapySDR::Device::enumerate();
        spdlog::info("SoapySDR enumerate returned {} device(s)", results.size());

        spdlog::info("Soapy enumerate found {} raw results", results.size());
        for (const auto& result : results) {
            DeviceInfo base;
            base.driver = result.count("driver") ? result.at("driver") : "unknown";
            base.label = result.count("label") ? result.at("label") : base.driver;
            base.serial = result.count("serial") ? result.at("serial") : "";
            base.hardware = result.count("hardware") ? result.at("hardware") : "";
            if (result.count("mode")) base.sdrplayDuoMode = result.at("mode");
            base.isSdrplay = SdrplayProfile::isSdrplayDriver(base.driver);
            if (base.isSdrplay) {
                base.sdrplayModel = SdrplayProfile::normalizeModel(base.hardware, base.label);
                base.canTx = false;
            }
            std::string info = "driver=" + base.driver + " label=" + base.label;
            if (!base.serial.empty()) info += " serial=" + base.serial;
            if (!base.sdrplayDuoMode.empty()) info += " mode=" + base.sdrplayDuoMode;
            spdlog::info("  Soapy result: {}", info);

            auto finishOne = [&](DeviceInfo di) {
                di.stableKey = makeDeviceStableKey(di);
                devices.push_back(std::move(di));
            };

            const bool dualTuner = base.isSdrplay && base.sdrplayDuoMode == "DT";
            const size_t channelCount = dualTuner ? 2 : 1;

            if (probeHardware) {
                try {
                    auto dev = SoapySDR::Device::make(result);
                    if (dev) {
                        for (size_t ch = 0; ch < channelCount; ++ch) {
                            DeviceInfo di = base;
                            if (dualTuner) {
                                di.label = base.label + " (ch" + std::to_string(ch) + ")";
                                di.rxChannel = ch;
                            }

                            if (di.isSdrplay) {
                                enrichSdrplayDeviceInfo(dev, di, ch);
                            } else {
                                // antennas
                                auto ants = dev->listAntennas(SOAPY_SDR_RX, 0);
                                di.antennas.assign(ants.begin(), ants.end());
                                if (di.antennas.empty()) {
                                    if (di.driver == "rtlsdr") di.antennas = {"RX"};
                                    else di.antennas = {"TX/RX"};
                                }
                                if (!di.antennas.empty()) di.antenna = di.antennas[0];

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

                                auto ranges = dev->getFrequencyRange(SOAPY_SDR_RX, 0);
                                if (!ranges.empty()) {
                                    di.minFreq = ranges.front().minimum();
                                    di.maxFreq = ranges.back().maximum();
                                }

                                auto gains = dev->listGains(SOAPY_SDR_RX, 0);
                                if (!gains.empty()) {
                                    di.gainName = gains[0];
                                    auto gr = dev->getGainRange(SOAPY_SDR_RX, 0, di.gainName);
                                    di.gainMin = gr.minimum();
                                    di.gainMax = gr.maximum();
                                    if (di.driver == "rtlsdr") {
                                        di.gain = 20.0;
                                    } else {
                                        di.gain = std::min(40.0, gr.maximum() * 0.5);
                                    }
                                } else {
                                    di.gainName = "TUNER";
                                    if (di.driver == "rtlsdr") {
                                        di.gain = 20.0;
                                        di.gainMin = 0.0;
                                        di.gainMax = 49.6;
                                    }
                                }

                                di.canTx = false;
                                di.txAntennas.clear();
                                if (di.driver != "rtlsdr") {
                                    try {
                                        auto txAnts = dev->listAntennas(SOAPY_SDR_TX, 0);
                                        di.txAntennas.assign(txAnts.begin(), txAnts.end());
                                        if (!di.txAntennas.empty()) {
                                            di.canTx = true;
                                        } else if (di.driver == "hackrf" || di.driver == "plutosdr" ||
                                                   di.driver == "lime" || di.driver == "uhd" ||
                                                   di.driver == "bladerf" || di.driver == "soapyremote") {
                                            di.canTx = true;
                                            di.txAntennas = {"TX"};
                                        }
                                    } catch (...) {
                                        if (di.driver == "hackrf" || di.driver == "plutosdr" ||
                                            di.driver == "lime" || di.driver == "uhd") {
                                            di.canTx = true;
                                        }
                                    }
                                }
                            }

                            // Common sample-rate / freq probe for SDRplay too
                            if (di.isSdrplay) {
                                try {
                                    auto rates = dev->listSampleRates(SOAPY_SDR_RX, ch);
                                    for (size_t i = 0; i < rates.size() && i < 12; ++i)
                                        di.sampleRates.push_back(rates[i]);
                                    if (di.sampleRates.empty())
                                        di.sampleRates = {0.25e6, 1e6, 2e6, 2.048e6, 2.4e6, 3e6, 5e6, 6e6, 8e6, 10e6};
                                    di.sampleRate = chooseDefaultSampleRate(di);
                                } catch (...) {
                                    di.sampleRates = {2e6, 2.048e6, 2.4e6, 5e6, 6e6, 8e6, 10e6};
                                    di.sampleRate = 2.048e6;
                                }
                                try {
                                    auto ranges = dev->getFrequencyRange(SOAPY_SDR_RX, ch);
                                    if (!ranges.empty()) {
                                        di.minFreq = ranges.front().minimum();
                                        di.maxFreq = ranges.back().maximum();
                                    }
                                } catch (...) {
                                    di.minFreq = 1e3;
                                    di.maxFreq = 2e9;
                                }
                            }

                            finishOne(std::move(di));
                            if (!dualTuner) break;
                        }
                        SoapySDR::Device::unmake(dev);
                    }
                } catch (const std::exception& ex) {
                    spdlog::warn("Could not fully probe device {}: {}", base.label, ex.what());
                    // Still publish light entries so the device is selectable.
                    for (size_t ch = 0; ch < channelCount; ++ch) {
                        DeviceInfo di = base;
                        if (dualTuner) {
                            di.label = base.label + " (ch" + std::to_string(ch) + ")";
                            di.rxChannel = ch;
                        }
                        if (di.isSdrplay) {
                            di.antennas = {"RX"};
                            di.antenna = "RX";
                            di.sampleRates = {2e6, 2.048e6, 2.4e6, 5e6, 6e6, 8e6, 10e6};
                            di.sampleRate = SdrplayProfile::clampSampleRateHz(di.sdrplayDuoMode, 2.048e6);
                            di.minFreq = 1e3;
                            di.maxFreq = 2e9;
                            di.gainName = "RFGR";
                            di.gainMin = 0.0;
                            di.gainMax = 27.0;
                            di.gain = 4.0;
                            di.rfgrDb = 4.0;
                            di.ifgrDb = 40.0;
                            di.gainElements = {"IFGR", "RFGR"};
                        } else if (di.driver == "rtlsdr") {
                            di.antennas = {"RX"};
                            di.antenna = "RX";
                            di.sampleRates = {0.25e6, 1.024e6, 2.048e6, 2.4e6};
                            di.sampleRate = 2.048e6;
                            di.minFreq = 24e6;
                            di.maxFreq = 1766e6;
                            di.gain = 20.0;
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
                        finishOne(std::move(di));
                        if (!dualTuner) break;
                    }
                }
            } else {
                // Light path (launch): use safe defaults so we never touch hardware.
                for (size_t ch = 0; ch < channelCount; ++ch) {
                    DeviceInfo di = base;
                    if (dualTuner) {
                        di.label = base.label + " (ch" + std::to_string(ch) + ")";
                        di.rxChannel = ch;
                    }
                    if (di.isSdrplay) {
                        di.antennas = {"RX"};
                        di.antenna = "RX";
                        di.sampleRates = {2e6, 2.048e6, 2.4e6, 5e6, 6e6, 8e6, 10e6};
                        di.sampleRate = SdrplayProfile::clampSampleRateHz(di.sdrplayDuoMode, 2.048e6);
                        di.minFreq = 1e3;
                        di.maxFreq = 2e9;
                        di.gainName = "RFGR";
                        di.gain = 4.0;
                        di.rfgrDb = 4.0;
                        di.ifgrDb = 40.0;
                        di.gainMin = 0.0;
                        di.gainMax = 27.0;
                        di.gainElements = {"IFGR", "RFGR"};
                    } else if (di.driver == "rtlsdr") {
                        di.antennas = {"RX"};
                        di.antenna = "RX";
                        di.sampleRates = {0.25e6, 1.024e6, 2.048e6, 2.4e6};
                        di.sampleRate = 2.048e6;
                        di.minFreq = 24e6;
                        di.maxFreq = 1766e6;
                        di.gain = 20.0;
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
                    finishOne(std::move(di));
                    if (!dualTuner) break;
                }
            }
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
            di.label = "RTL-SDR placeholder (enable will try hardware)";
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
            di.stableKey = makeDeviceStableKey(di);
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
    fake1.stableKey = makeDeviceStableKey(fake1);
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
    fake2.stableKey = makeDeviceStableKey(fake2);
    devices.push_back(fake2);

    DeviceInfo fake3;
    fake3.driver = "sdrplay";
    fake3.label = "SDRplay RSPdx (stub)";
    fake3.serial = "sdrplay-stub";
    fake3.hardware = "RSPdx";
    fake3.isSdrplay = true;
    fake3.sdrplayModel = "RSPdx";
    fake3.antennas = {"Antenna A", "Antenna B", "Antenna C"};
    fake3.antenna = "Antenna A";
    fake3.sampleRates = {2e6, 2.048e6, 2.4e6, 5e6, 6e6, 8e6, 10e6};
    fake3.sampleRate = 2.048e6;
    fake3.gainName = "RFGR";
    fake3.gain = 4.0;
    fake3.rfgrDb = 4.0;
    fake3.ifgrDb = 40.0;
    fake3.gainMin = 0.0;
    fake3.gainMax = 27.0;
    fake3.gainElements = {"IFGR", "RFGR"};
    fake3.sdrplaySettingKeys = SdrplayProfile::knownSettingKeys();
    fake3.soapySettings = SdrplayProfile::defaultSettings(
        SdrplayProfile::capabilitiesFromProbe(fake3.driver, fake3.hardware, fake3.label,
            fake3.gainElements, fake3.antennas, {}, fake3.sdrplaySettingKeys, {})).soapySettings;
    fake3.minFreq = 1e3;
    fake3.maxFreq = 2e9;
    fake3.stableKey = makeDeviceStableKey(fake3);
    devices.push_back(fake3);
#endif

    // After enumerate, try to overlay saved settings (enabled, rate, gain, antenna from JSON).
    // This works for both light and full probe paths.
    loadSettings();

    // unlock before diversity restore — ensureDiversityCompositeDevice takes devicesMutex
    }
    restoreDiversityCompositeAfterEnumerate();
    {
        std::lock_guard<std::mutex> lk(devicesMutex);
        return devices;
    }
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
    if (d.isSdrplay) {
        sampleRate = SdrplayProfile::clampSampleRateHz(d.sdrplayDuoMode, sampleRate);
        // Bias-T off when switching to a non-compatible port.
        if (!SdrplayProfile::biasTAllowedForAntenna(d.sdrplayModel, antenna)) {
            d.soapySettings[SdrplaySettings::kBiasT] = "false";
        }
    }
    d.sampleRate = sampleRate;
    d.gain = clampGainForDevice(d, gain);
    if (d.isSdrplay) d.rfgrDb = d.gain;
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
        j["stableKey"] = d.stableKey.empty() ? makeDeviceStableKey(d) : d.stableKey;
        j["enabled"] = d.enabled;
        j["sampleRate"] = d.sampleRate;
        j["gain"] = d.gain;
        j["frequencyCorrectionPpm"] = d.frequencyCorrectionPpm;
        j["antenna"] = d.antenna;
        j["directSampling"] = d.directSampling;
        if (d.isSdrplay) {
            j["isSdrplay"] = true;
            j["sdrplayModel"] = d.sdrplayModel;
            j["sdrplayDuoMode"] = d.sdrplayDuoMode;
            j["rxChannel"] = d.rxChannel;
            j["agcEnabled"] = d.agcEnabled;
            j["ifgrDb"] = d.ifgrDb;
            j["rfgrDb"] = d.rfgrDb;
            j["bandwidthHz"] = d.bandwidthHz;
            j["soapySettings"] = d.soapySettings;
        }
        arr.push_back(j);
    }
    return arr;
}

void DeviceManager::fromJson(const nlohmann::json& j) {
    if (!j.is_array()) return;
    for (auto& d : devices) {
        if (d.stableKey.empty()) d.stableKey = makeDeviceStableKey(d);
        for (const auto& saved : j) {
            const bool stableMatch = saved.contains("stableKey") && saved["stableKey"].is_string() &&
                saved["stableKey"].get<std::string>() == d.stableKey;
            const bool legacyMatch = saved.contains("driver") && saved["driver"] == d.driver &&
                saved.contains("serial") && saved["serial"] == d.serial &&
                (!d.isSdrplay ||
                 ((!saved.contains("sdrplayDuoMode") || saved["sdrplayDuoMode"] == d.sdrplayDuoMode) &&
                  (!saved.contains("rxChannel") || saved["rxChannel"].get<size_t>() == d.rxChannel)));
            if (stableMatch || legacyMatch) {
                if (saved.contains("enabled")) d.enabled = saved["enabled"];
                if (saved.contains("sampleRate")) d.sampleRate = saved["sampleRate"];
                if (saved.contains("gain")) d.gain = clampGainForDevice(d, saved["gain"].get<double>());
                if (saved.contains("frequencyCorrectionPpm")) d.frequencyCorrectionPpm = clampFrequencyCorrectionPpm(saved["frequencyCorrectionPpm"].get<double>());
                else if (saved.contains("ppm")) d.frequencyCorrectionPpm = clampFrequencyCorrectionPpm(saved["ppm"].get<double>());
                if (saved.contains("antenna")) d.antenna = saved["antenna"];
                if (saved.contains("directSampling")) {
                    d.directSampling = clampDirectSamplingMode(saved["directSampling"].get<int>());
                    updateRtlFreqLimitsForDirectSampling(d);
                }
                if (d.isSdrplay) {
                    if (saved.contains("agcEnabled")) d.agcEnabled = saved["agcEnabled"].get<bool>();
                    if (saved.contains("ifgrDb")) d.ifgrDb = saved["ifgrDb"].get<double>();
                    if (saved.contains("rfgrDb")) {
                        d.rfgrDb = saved["rfgrDb"].get<double>();
                        d.gain = clampGainForDevice(d, d.rfgrDb);
                    } else if (saved.contains("gain")) {
                        d.rfgrDb = d.gain;
                    }
                    if (saved.contains("bandwidthHz")) d.bandwidthHz = saved["bandwidthHz"].get<double>();
                    if (saved.contains("soapySettings") && saved["soapySettings"].is_object()) {
                        d.soapySettings.clear();
                        for (auto it = saved["soapySettings"].begin(); it != saved["soapySettings"].end(); ++it) {
                            if (it.value().is_string()) d.soapySettings[it.key()] = it.value().get<std::string>();
                            else d.soapySettings[it.key()] = it.value().dump();
                        }
                    }
                }
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
            // Keep enough recent RF for P25 follow diagnostics.  At 2.048 MS/s
            // this is about 16 seconds of complex IQ, which lets saved
            // follow captures include the actual post-retune traffic window.
            st.ringCapacity = 1u << 25;
            st.iqRing.assign(st.ringCapacity, std::complex<float>(0, 0));
        }
        st.ringWriteIdx.store(0, std::memory_order_release);
        st.totalSamplesWritten.store(0, std::memory_order_release);
        st.retuneValidFromAbsolute.store(0, std::memory_order_release);
        st.streamEpoch.fetch_add(1, std::memory_order_acq_rel);
    }
}

void DeviceManager::markStreamRetune(StreamState& st, double appliedCenterHz) {
    std::lock_guard<std::mutex> ringLock(st.ringMutex);
    if (st.ringCapacity == 0) {
        st.ringCapacity = 1u << 25;
        st.iqRing.assign(st.ringCapacity, std::complex<float>(0, 0));
    }
    const double lastHz = st.lastAppliedCenterHz.load(std::memory_order_relaxed);
    const bool haveNewCenter = std::isfinite(appliedCenterHz) && appliedCenterHz > 0.0;
    const bool haveLastCenter = std::isfinite(lastHz) && lastHz > 0.0;
    const bool mhzHop = haveNewCenter && haveLastCenter &&
        std::abs(appliedCenterHz - lastHz) > 25000.0;
    if (mhzHop) {
        const uint64_t total = st.totalSamplesWritten.load(std::memory_order_acquire);
        st.retuneValidFromAbsolute.store(total, std::memory_order_release);
        st.streamEpoch.fetch_add(1, std::memory_order_acq_rel);
    }
    if (haveNewCenter) {
        st.lastAppliedCenterHz.store(appliedCenterHz, std::memory_order_release);
    }
}

// --- Streaming implementation (real Soapy or simulated) ---

bool DeviceManager::startStreaming(size_t index, bool attemptReal) {
    DeviceInfo d;
    StreamState* stPtr = nullptr;
    {
        std::lock_guard<std::mutex> lk(devicesMutex);
        if (index >= devices.size()) return false;
        d = devices[index];
        if (streams.size() <= index) streams.resize(index + 1);
        if (!streams[index]) streams[index] = std::make_unique<StreamState>();
        stPtr = streams[index].get();
    }
    auto& st = *stPtr;

    // Diversity composite never opens Soapy — it soft-combines Dual Tuner A/B rings.
    if (d.isDiversityComposite) {
        attemptReal = false;
        size_t a = d.diversitySourceA, b = d.diversitySourceB;
        if (a != static_cast<size_t>(-1) && a != index) startStreaming(a, true);
        if (b != static_cast<size_t>(-1) && b != index) startStreaming(b, true);
    }

    bool wasActive = false;
    bool wasReal = false;
    std::string wasRuntime;
    {
        std::lock_guard<std::mutex> lk(st.stateMutex);
        wasActive = st.active;
        wasReal = st.isReal;
        wasRuntime = st.runtimeState;
    }
    if (wasActive) {
        if (attemptReal && !wasReal) {
            if (wasRuntime.find("opening hardware") != std::string::npos) {
                return true;
            }
            // User explicitly wants real hardware (e.g. Apply in dialog), but we currently have
            // a safe stub running (from launch auto-start or previous failure). Stop the stub
            // cleanly then fall through to attempt the real open. This avoids "double use".
            stopStreaming(index);
            // fall through with active==false now
        } else {
            return true; // already streaming the desired mode
        }
    }

    std::unique_lock<std::mutex> lifecycleLock(st.lifecycleMutex);
    {
        bool activeNow = false;
        bool realNow = false;
        std::string runtimeNow;
        {
            std::lock_guard<std::mutex> lk(st.stateMutex);
            activeNow = st.active;
            realNow = st.isReal;
            runtimeNow = st.runtimeState;
        }
        if (activeNow) {
            const bool realOpenInFlight = runtimeNow.find("opening hardware") != std::string::npos;
            if (attemptReal && !realNow && !realOpenInFlight) {
                lifecycleLock.unlock();
                stopStreaming(index);
                lifecycleLock.lock();
                {
                    std::lock_guard<std::mutex> lk(st.stateMutex);
                    activeNow = st.active;
                    realNow = st.isReal;
                    runtimeNow = st.runtimeState;
                }
                if (activeNow) {
                    spdlog::warn("Device {} start requested real hardware but existing stream state=\"{}\" did not stop cleanly; refusing duplicate rxThread start.", index, runtimeNow);
                    return realNow || !attemptReal;
                }
            } else {
                return true;
            }
        }
    }

    // ALWAYS start a fast stub simulation first. This makes every "start" (Add Receiver,
    // Apply, Scan, CLI) return instantly with working spectrum + basic demod + audio routing.
    // No blocking on Soapy make / USB / driver init, which is the source of "hangs then crashes"
    // when the RTL dongle or audio devices are in a bad state.
    const uint64_t streamGen = st.sessionGen.fetch_add(1, std::memory_order_acq_rel) + 1;
    st.stopFlag = false;
    double requestedCenter = 100e6;
    {
        std::lock_guard<std::mutex> lk(st.queueMutex);
        if (std::isfinite(st.currentCenter) && st.currentCenter > 0.0) {
            requestedCenter = st.currentCenter;
        }
    }
    double useRate = d.sampleRate;
    if (d.isSdrplay) {
        useRate = SdrplayProfile::clampSampleRateHz(d.sdrplayDuoMode, useRate);
        if (useRate != d.sampleRate) {
            std::lock_guard<std::mutex> lk(devicesMutex);
            if (index < devices.size()) {
                devices[index].sampleRate = useRate;
                saveSettings();
            }
        }
    }
    if (useRate < 0.25e6 || useRate > 60e6) useRate = (d.driver == "rtlsdr" ? 2.048e6 : 2.4e6);
    {
        std::lock_guard<std::mutex> lk(st.queueMutex);
        st.currentCenter = requestedCenter;
        st.currentRate = useRate;
    }
    const uint64_t initialTuneSeq = st.centerTuneRequestSeq.load(std::memory_order_acquire);
    if (!attemptReal) st.centerTuneAppliedSeq.store(initialTuneSeq, std::memory_order_release);
    st.frequencyCorrectionPpm = clampFrequencyCorrectionPpm(d.frequencyCorrectionPpm);
    st.nativeFrequencyCorrectionActive = false;

    // S0 / audit-followup-2: reset per-device IQ buffers for every session so a new
    // tune/enable cannot consume stale IQ from a previous station, stub, or failed real handoff.
    resetStreamBuffers(st);

    setupSoapyForRTLSDR();
    if (d.isSdrplay || SdrplayProfile::isSdrplayDriver(d.driver))
        setupSoapyForSDRplay();

    {
        std::lock_guard<std::mutex> lk(st.stateMutex);
        st.active = true;
        st.isReal = false;
        st.runtimeState = attemptReal ? "opening hardware (stub active)" : "simulated/stub";
    }
    st.rxThread = std::thread(&DeviceManager::rxThreadFunc, this, index, streamGen);
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
        auto* stPtr = streamState(index);
        if (!stPtr) return;
        auto& st = *stPtr;
        if (st.stopFlag) return;
        if (st.sessionGen.load() != myGen) return;
        SoapySDR::Device* localDev = nullptr;
        SoapySDR::Stream* localStream = nullptr;
        bool dualShared = false;
        bool ownsSharedRef = false;
        std::string shareKey;
        size_t rxCh = 0;
        auto cleanupLocal = [&]() {
            try {
                if (localStream && localDev) localDev->closeStream(localStream);
            } catch (...) {}
            localStream = nullptr;
            if (dualShared && ownsSharedRef) {
                std::lock_guard<std::mutex> shareLock(gSharedSdrplayMutex);
                auto it = gSharedSdrplayDevices.find(shareKey);
                if (it != gSharedSdrplayDevices.end()) {
                    it->second.refCount = std::max(0, it->second.refCount - 1);
                    if (it->second.refCount <= 0 && it->second.dev) {
                        try { SoapySDR::Device::unmake(it->second.dev); } catch (...) {}
                        gSharedSdrplayDevices.erase(it);
                    }
                }
                ownsSharedRef = false;
                localDev = nullptr;
            } else if (localDev) {
                try { SoapySDR::Device::unmake(localDev); } catch (...) {}
                localDev = nullptr;
            }
        };
        try {
            if (d.driver == "rtlsdr") {
                std::string appDir = QCoreApplication::applicationDirPath().toStdString();
                try { SoapySDR::loadModule(appDir + "\\SoapyRTLSDR.dll"); } catch (...) {}
                try { SoapySDR::loadModule("C:\\Program Files\\PothosSDR\\lib\\SoapySDR\\modules0.8\\rtlsdrSupport.dll"); } catch (...) {}
            }
            if (d.isSdrplay || SdrplayProfile::isSdrplayDriver(d.driver)) {
                std::string appDir = QCoreApplication::applicationDirPath().toStdString();
#ifdef _WIN32
                for (const auto& module : SdrplayProfile::windowsSoapyModuleCandidates(appDir)) {
                    if (GetFileAttributesA(module.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
                    try {
                        SoapySDR::loadModule(module);
                    } catch (const std::exception& ex) {
                        spdlog::debug("SDRplay stream module load skipped for {}: {}", module, ex.what());
                    } catch (...) {
                        spdlog::debug("SDRplay stream module load skipped for {}: unknown error", module);
                    }
                }
#else
                try { SoapySDR::loadModule(appDir + "/lib/SoapySDR/modules/sdrPlaySupport.so"); } catch (...) {}
#endif
            }

            DeviceInfo liveInfo = d;
            {
                std::lock_guard<std::mutex> lk(devicesMutex);
                if (index < devices.size()) liveInfo = devices[index];
            }
            rxCh = liveInfo.isSdrplay ? liveInfo.rxChannel : 0;
            dualShared = liveInfo.isSdrplay && liveInfo.sdrplayDuoMode == "DT";

            SoapySDR::Kwargs args;
            if (!liveInfo.driver.empty()) args["driver"] = liveInfo.driver;
            if (!liveInfo.serial.empty()) args["serial"] = liveInfo.serial;
            if (liveInfo.isSdrplay && !liveInfo.sdrplayDuoMode.empty())
                args["mode"] = liveInfo.sdrplayDuoMode;

            if (dualShared) {
                shareKey = sdrplayShareKey(liveInfo);
                std::lock_guard<std::mutex> shareLock(gSharedSdrplayMutex);
                auto& slot = gSharedSdrplayDevices[shareKey];
                if (!slot.dev) {
                    spdlog::info("Background: Attempting shared Soapy make for SDRplay Dual Tuner {}", shareKey);
                    std::lock_guard<std::mutex> soapyLiveLock(gSoapyLiveIoMutex);
                    slot.dev = SoapySDR::Device::make(args);
                    if (!slot.dev) throw std::runtime_error("make returned null");
                    slot.refCount = 0;
                }
                slot.refCount += 1;
                ownsSharedRef = true;
                localDev = slot.dev;
            } else {
                spdlog::info("Background: Attempting Soapy make for device {}", index);
                std::lock_guard<std::mutex> soapyLiveLock(gSoapyLiveIoMutex);
                localDev = SoapySDR::Device::make(args);
                if (!localDev) throw std::runtime_error("make returned null");
            }

            {
                std::lock_guard<std::mutex> soapyLiveLock(gSoapyLiveIoMutex);
                localDev->setSampleRate(SOAPY_SDR_RX, rxCh, useRate);
                if (!liveInfo.antenna.empty()) try { localDev->setAntenna(SOAPY_SDR_RX, rxCh, liveInfo.antenna); } catch (...) {}
            }

            // Re-read the *latest* desired gain right before applying (user may have changed the main GUI
            // RF Gain spin or the Device Manager dialog *while* this background Soapy open/make/activate
            // was running in the detached thread). This is a key part of making "live" gain reliable.
            double useGain;
            std::string useGainName;
            double usePpm = liveInfo.frequencyCorrectionPpm;
            DeviceInfo applyInfo = liveInfo;
            {
                std::lock_guard<std::mutex> lk(devicesMutex);
                if (index < devices.size()) {
                    applyInfo = devices[index];
                    useGain = clampGainForDevice(devices[index], devices[index].gain);
                    useGainName = devices[index].gainName;
                    usePpm = clampFrequencyCorrectionPpm(devices[index].frequencyCorrectionPpm);
                } else {
                    useGain = clampGainForDevice(d, d.gain);
                    useGainName = d.gainName;
                    usePpm = clampFrequencyCorrectionPpm(d.frequencyCorrectionPpm);
                }
            }

            {
                std::lock_guard<std::mutex> soapyLiveLock(gSoapyLiveIoMutex);
                if (applyInfo.isSdrplay) {
                    applyInfo.rfgrDb = useGain;
                    applyInfo.gain = useGain;
                    applySdrplayProfileToDevice(localDev, applyInfo, index);
                } else {
                    try { localDev->setGainMode(SOAPY_SDR_RX, rxCh, false); } catch (...) {}
                    if (!useGainName.empty()) {
                        try { localDev->setGain(SOAPY_SDR_RX, rxCh, useGainName, useGain); } catch (...) { localDev->setGain(SOAPY_SDR_RX, rxCh, useGain); }
                    } else {
                        try { localDev->setGain(SOAPY_SDR_RX, rxCh, useGain); } catch (...) {}
                    }
                }
                if (applyInfo.driver == "rtlsdr") {
                    if (!applySoapyDirectSampling(localDev, applyInfo.directSampling, index) && applyInfo.directSampling != 0)
                        throw std::runtime_error("RTL driver did not confirm requested direct-sampling mode");
                }
            }
            double center = 100e6;
            uint64_t centerTuneSeq = st.centerTuneRequestSeq.load(std::memory_order_acquire);
            {
                std::lock_guard<std::mutex> lk(st.queueMutex);
                center = st.currentCenter;
            }
            bool nativePpm = false;
            const double tuneCenterPrep = center;
            {
                std::lock_guard<std::mutex> soapyLiveLock(gSoapyLiveIoMutex);
                try {
                    if (localDev->hasFrequencyCorrection(SOAPY_SDR_RX, rxCh)) {
                        localDev->setFrequencyCorrection(SOAPY_SDR_RX, rxCh, usePpm);
                        nativePpm = true;
                        spdlog::info("Applied native frequency correction to device {}: {} ppm", index, usePpm);
                    }
                } catch (const std::exception& ex) {
                    spdlog::warn("Native frequency correction unavailable for device {}: {}", index, ex.what());
                    nativePpm = false;
                } catch (...) {
                    nativePpm = false;
                }
                const double tuneCenter = nativePpm ? tuneCenterPrep : correctedTuneFrequencyHz(tuneCenterPrep, usePpm);
                try {
                    localDev->setFrequency(SOAPY_SDR_RX, rxCh, tuneCenter);
                    st.centerTuneAppliedSeq.store(centerTuneSeq, std::memory_order_release);
                } catch (...) {}

                std::vector<size_t> channels = {rxCh};
                localStream = localDev->setupStream(SOAPY_SDR_RX, "CF32", channels);
                if (!localStream) throw std::runtime_error("setupStream null");
                localDev->activateStream(localStream);
            }

            if (st.stopFlag || st.sessionGen.load() != myGen) {
                cleanupLocal();
                return;
            }
            if (st.sessionGen.load() != myGen) {
                cleanupLocal();
                return;
            }

            // Success path: serialize final stub->real handoff with stopStreaming(). The generation
            // guard prevents stale publication; lifecycleMutex prevents stop from racing the final
            // stopFlag=false + rxThread assignment.
            {
                std::unique_lock<std::mutex> lifecycleLock(st.lifecycleMutex);
                if (st.stopFlag.load(std::memory_order_acquire) ||
                    st.sessionGen.load(std::memory_order_acquire) != myGen) {
                    cleanupLocal();
                    return;
                }

                st.stopFlag.store(true, std::memory_order_release);
                bool stubDetached = false;
                if (st.rxThread.joinable()) {
                    auto start = std::chrono::steady_clock::now();
                    while (st.rxThreadRunning.load(std::memory_order_acquire) &&
                           std::chrono::steady_clock::now() - start < std::chrono::milliseconds(500)) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(2));
                    }
                    if (st.rxThreadRunning.load(std::memory_order_acquire)) {
                        const int count = st.stubStopTimeoutCount.fetch_add(1, std::memory_order_relaxed) + 1;
                        spdlog::error("Internal stub rxThread for device {} did not stop during real upgrade (count={}); detaching and aborting upgrade. This is not a native Soapy hang and needs RX-loop investigation.", index, count);
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

                resetStreamBuffers(st);

                bool staleSession = false;
                {
                    std::lock_guard<std::mutex> lk(st.stateMutex);
                    if (st.sessionGen.load(std::memory_order_acquire) != myGen) {
                        staleSession = true;
                    } else {
                        st.soapyDev = localDev;
                        st.rxStream = localStream;
                        st.soapyShared = dualShared;
                        st.sdrplayShareKey = shareKey;
                        st.stopFlag.store(false, std::memory_order_release);
                        st.active = true;
                        st.isReal = true;
                        st.runtimeState = "live hardware";
                        st.frequencyCorrectionPpm = usePpm;
                        st.nativeFrequencyCorrectionActive = nativePpm;
                        localDev = nullptr;
                        localStream = nullptr;
                        ownsSharedRef = false; // StreamState now owns the shared ref
                    }
                }
                if (staleSession) {
                    cleanupLocal();
                    return;
                }

                st.rxThread = std::thread(&DeviceManager::rxThreadFunc, this, index, myGen);
            }
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
    auto* stPtr = streamState(index);
    if (!stPtr) return;
    auto& st = *stPtr;
    std::unique_lock<std::mutex> lifecycleLock(st.lifecycleMutex);
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
    bool sharedClose = false;
    std::string sharedKey;
    {
        std::lock_guard<std::mutex> lk(st.stateMutex);
        devToClose = st.soapyDev;
        streamToClose = st.rxStream;
        sharedClose = st.soapyShared;
        sharedKey = st.sdrplayShareKey;
        st.soapyDev = nullptr;
        st.rxStream = nullptr;
        st.soapyShared = false;
        st.sdrplayShareKey.clear();
    }

    try {
        if (rxDetached && devToClose) {
            // Closing/unmaking while a detached native readStream may still be using the device
            // is a use-after-free risk. Leak this stuck handle until process exit instead.
            spdlog::warn("Leaving Soapy device {} open because its rxThread was detached while stuck in native code.", index);
        } else if (streamToClose && devToClose) {
            std::lock_guard<std::mutex> soapyLiveLock(gSoapyLiveIoMutex);
            try { devToClose->deactivateStream(streamToClose); } catch (...) {}
            try { devToClose->closeStream(streamToClose); } catch (...) {}
            if (sharedClose) {
                std::lock_guard<std::mutex> shareLock(gSharedSdrplayMutex);
                auto it = gSharedSdrplayDevices.find(sharedKey);
                if (it != gSharedSdrplayDevices.end()) {
                    it->second.refCount = std::max(0, it->second.refCount - 1);
                    if (it->second.refCount <= 0) {
                        try { SoapySDR::Device::unmake(it->second.dev); } catch (...) {}
                        gSharedSdrplayDevices.erase(it);
                    }
                }
            } else {
                SoapySDR::Device::unmake(devToClose);
            }
        } else if (devToClose) {
            std::lock_guard<std::mutex> soapyLiveLock(gSoapyLiveIoMutex);
            if (sharedClose) {
                std::lock_guard<std::mutex> shareLock(gSharedSdrplayMutex);
                auto it = gSharedSdrplayDevices.find(sharedKey);
                if (it != gSharedSdrplayDevices.end()) {
                    it->second.refCount = std::max(0, it->second.refCount - 1);
                    if (it->second.refCount <= 0) {
                        try { SoapySDR::Device::unmake(it->second.dev); } catch (...) {}
                        gSharedSdrplayDevices.erase(it);
                    }
                }
            } else {
                SoapySDR::Device::unmake(devToClose);
            }
        }
    } catch (const std::exception& ex) {
        spdlog::warn("Soapy teardown reported a recoverable native issue for device {}: {}", index, ex.what());
    } catch (...) {
        spdlog::warn("Soapy teardown reported a recoverable non-standard native issue for device {}", index);
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
    auto* stPtr = streamState(index);
    if (!stPtr) return false;
    auto& st = *stPtr;
    std::lock_guard<std::mutex> lk(st.stateMutex);
    return st.active;
}

size_t DeviceManager::getNextIQBlock(size_t index, std::complex<float>* buffer, size_t maxSamples, int timeoutMs) {
    auto* stPtr = streamState(index);
    if (!stPtr) return 0;
    auto& st = *stPtr;
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
    auto* stPtr = streamState(index);
    if (!stPtr) return false;
    auto& st = *stPtr;
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
    StreamState* stPtr = nullptr;
    {
        std::lock_guard<std::mutex> lk(devicesMutex);
        if (index >= devices.size()) return;
        if (streams.size() <= index) streams.resize(index + 1);
        if (!streams[index]) streams[index] = std::make_unique<StreamState>();
        stPtr = streams[index].get();
    }

    auto& st = *stPtr;
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
    auto* stPtr = streamState(index);
    if (!stPtr) return 8192;
    auto& st = *stPtr;
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
        if (devices[index].isSdrplay) {
            devices[index].rfgrDb = useGain;
            devices[index].agcEnabled = false;
        }
        d = devices[index];
        saveSettings();
    }

#ifdef HAVE_SOAPYSDR
    bool appliedLive = false;
    if (auto* stPtr = streamState(index)) {
        auto& st = *stPtr;
        std::lock_guard<std::mutex> stateLock(st.stateMutex);
        if (st.soapyDev && !st.stopFlag) {
            try {
                std::lock_guard<std::mutex> soapyLiveLock(gSoapyLiveIoMutex);
                const size_t ch = d.isSdrplay ? d.rxChannel : 0;
                try { st.soapyDev->setGainMode(SOAPY_SDR_RX, ch, false); } catch (...) {}
                if (d.isSdrplay) {
                    try { st.soapyDev->setGain(SOAPY_SDR_RX, ch, "RFGR", useGain); } catch (...) {}
                    try { st.soapyDev->setGain(SOAPY_SDR_RX, ch, "IFGR", d.ifgrDb); } catch (...) {}
                } else if (!d.gainName.empty()) {
                    st.soapyDev->setGain(SOAPY_SDR_RX, ch, d.gainName, useGain);
                } else {
                    st.soapyDev->setGain(SOAPY_SDR_RX, ch, useGain);
                }
                spdlog::info("Live RF gain applied to device {}: {} dB (gainName='{}' ch={})", index, useGain, d.gainName, ch);
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

    auto* stPtr = streamState(index);
    if (!stPtr) return;
    auto& st = *stPtr;

    double logicalCenter = 0.0;
    {
        std::lock_guard<std::mutex> lk(st.queueMutex);
        logicalCenter = st.currentCenter;
    }

#ifdef HAVE_SOAPYSDR
    bool appliedNative = false;
    size_t rxCh = 0;
    {
        std::lock_guard<std::mutex> lk(devicesMutex);
        if (index < devices.size() && devices[index].isSdrplay) rxCh = devices[index].rxChannel;
    }
    {
        std::lock_guard<std::mutex> stateLock(st.stateMutex);
        st.frequencyCorrectionPpm = usePpm;
        st.nativeFrequencyCorrectionActive = false;
        if (st.soapyDev && !st.stopFlag) {
            std::lock_guard<std::mutex> soapyLiveLock(gSoapyLiveIoMutex);
            try {
                if (st.soapyDev->hasFrequencyCorrection(SOAPY_SDR_RX, rxCh)) {
                    st.soapyDev->setFrequencyCorrection(SOAPY_SDR_RX, rxCh, usePpm);
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
                st.soapyDev->setFrequency(SOAPY_SDR_RX, rxCh, tuneHz);
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

bool DeviceManager::setDirectSampling(size_t index, int mode, std::string* error) {
    const auto fail = [&](const std::string& message) {
        if (error) *error = message;
        spdlog::warn("Direct sampling device {}: {}", index, message);
        return false;
    };
    if (mode < 0 || mode > 2) return fail("Invalid direct-sampling mode");
    DeviceInfo info;
    {
        std::lock_guard<std::mutex> lock(devicesMutex);
        if (index >= devices.size()) return fail("Device is unavailable");
        info = devices[index];
    }
    if (info.driver != "rtlsdr") return mode == 0 ? true : fail("Device does not use RTL direct sampling");
    if (mode != 0 && isRtlBlogV4(info)) return fail("RTL-SDR Blog V4 uses native HF; leave direct sampling off");
    if (mode == info.directSampling) {
        if (error) error->clear();
        return true; // Startup verifies persisted settings; repeated Apply must not reset IQ.
    }

#ifdef HAVE_SOAPYSDR
    if (auto* st = streamState(index)) {
        double center = 0.0;
        {
            std::lock_guard<std::mutex> lock(st->queueMutex);
            center = st->currentCenter;
        }
        std::lock_guard<std::mutex> stateLock(st->stateMutex);
        if (st->soapyDev && !st->stopFlag) {
            std::lock_guard<std::mutex> ioLock(gSoapyLiveIoMutex);
            try {
                if (!applySoapyDirectSampling(st->soapyDev, mode, index))
                    throw std::runtime_error("Driver did not confirm direct-sampling mode");
                const double tune = st->nativeFrequencyCorrectionActive ? center :
                    correctedTuneFrequencyHz(center, st->frequencyCorrectionPpm);
                if (center > 0) st->soapyDev->setFrequency(SOAPY_SDR_RX, info.rxChannel, tune);
                // Direct-sampling switches change IQ provenance even at the same RF.
                resetStreamBuffers(*st);
            } catch (const std::exception& ex) {
                try { applySoapyDirectSampling(st->soapyDev, info.directSampling, index); } catch (...) {}
                resetStreamBuffers(*st);
                return fail(ex.what());
            } catch (...) {
                try { applySoapyDirectSampling(st->soapyDev, info.directSampling, index); } catch (...) {}
                resetStreamBuffers(*st);
                return fail("Driver failed to change direct sampling");
            }
        }
    }
#endif
    {
        std::lock_guard<std::mutex> lock(devicesMutex);
        if (index >= devices.size() || devices[index].stableKey != info.stableKey)
            return fail("Device changed during direct-sampling request");
        devices[index].directSampling = mode;
        updateRtlFreqLimitsForDirectSampling(devices[index]);
        saveSettings();
    }
    if (error) error->clear();
    return true;
}

int DeviceManager::getDirectSampling(size_t index) const {
    std::lock_guard<std::mutex> lk(devicesMutex);
    if (index >= devices.size()) return 0;
    return devices[index].directSampling;
}

SdrplayCapabilities DeviceManager::getSdrplayCapabilities(size_t index) const {
    std::lock_guard<std::mutex> lk(devicesMutex);
    if (index >= devices.size()) return {};
    const auto& d = devices[index];
    return SdrplayProfile::capabilitiesFromProbe(
        d.driver, d.hardware, d.label, d.gainElements, d.antennas,
        d.bandwidthsHz, d.sdrplaySettingKeys, d.sdrplaySettingOptions);
}

std::string DeviceManager::getSdrplaySetupStatus() const {
    return sdrplaySetupStatus_;
}

const char* DeviceManager::leaseOwnerName(DeviceLeaseOwner owner) {
    switch (owner) {
    case DeviceLeaseOwner::Listen: return "listen";
    case DeviceLeaseOwner::P25: return "p25";
    case DeviceLeaseOwner::Satcom: return "satcom";
    case DeviceLeaseOwner::Inmarsat: return "inmarsat";
    case DeviceLeaseOwner::Aircraft: return "aircraft";
    case DeviceLeaseOwner::None:
    default: return "none";
    }
}

static int leasePriority(DeviceManager::DeviceLeaseOwner owner) {
    using O = DeviceManager::DeviceLeaseOwner;
    switch (owner) {
    case O::P25: return 30;
    case O::Listen: return 20;
    case O::Satcom:
    case O::Inmarsat:
    case O::Aircraft: return 10;
    default: return 0;
    }
}

DeviceManager::DeviceLeaseOwner DeviceManager::deviceLeaseOwner() const {
    std::lock_guard<std::mutex> lk(leaseMutex_);
    return deviceLeaseOwner_;
}

bool DeviceManager::acquireDeviceLease(size_t index, DeviceLeaseOwner owner, bool force, std::string* error) {
    if (owner == DeviceLeaseOwner::None) {
        if (error) *error = "invalid lease owner";
        return false;
    }
    {
        std::lock_guard<std::mutex> lk(devicesMutex);
        if (index >= devices.size()) {
            if (error) *error = "bad device index";
            return false;
        }
    }
    std::lock_guard<std::mutex> lk(leaseMutex_);
    if (deviceLeaseOwner_ != DeviceLeaseOwner::None && deviceLeaseOwner_ != owner) {
        const bool streaming = isStreaming(deviceLeaseIndex_ == static_cast<size_t>(-1) ? index : deviceLeaseIndex_);
        if (streaming && !force && leasePriority(owner) < leasePriority(deviceLeaseOwner_)) {
            if (error) {
                *error = std::string("device leased by ") + leaseOwnerName(deviceLeaseOwner_) +
                         " — pass force=true to take the tuner";
            }
            return false;
        }
    } else if (deviceLeaseOwner_ == DeviceLeaseOwner::None && !force &&
               (owner == DeviceLeaseOwner::Satcom || owner == DeviceLeaseOwner::Inmarsat ||
                owner == DeviceLeaseOwner::Aircraft) &&
               isStreaming(index)) {
        if (error) {
            *error = "live listen session owns the tuner — pass force=true to retune";
        }
        return false;
    }
    deviceLeaseOwner_ = owner;
    deviceLeaseIndex_ = index;
    return true;
}

void DeviceManager::releaseDeviceLease(DeviceLeaseOwner owner) {
    std::lock_guard<std::mutex> lk(leaseMutex_);
    if (deviceLeaseOwner_ == owner) {
        deviceLeaseOwner_ = DeviceLeaseOwner::None;
        deviceLeaseIndex_ = static_cast<size_t>(-1);
    }
}

bool DeviceManager::retuneWithLease(size_t index, double freqHz, DeviceLeaseOwner owner, bool force,
                                    std::string* error) {
    if (!std::isfinite(freqHz) || freqHz <= 0) {
        if (error) *error = "Center frequency must be finite and positive";
        return false;
    }
    if (!acquireDeviceLease(index, owner, force, error)) return false;
    const uint64_t requestSeq = setCenterFreq(index, freqHz);
    if (requestSeq == 0) {
        if (error) {
            *error = "device index " + std::to_string(index) +
                     " is unavailable; rescan devices and select a valid receiver";
        }
        return false;
    }
    return true;
}

double DeviceManager::getCurrentCenterFreq(size_t index) const {
    auto* st = streamState(index);
    if (!st) return 0.0;
    std::lock_guard<std::mutex> lk(st->queueMutex);
    return st->currentCenter;
}

double DeviceManager::getCurrentSampleRate(size_t index) const {
    auto* st = streamState(index);
    if (!st) {
        std::lock_guard<std::mutex> lk(devicesMutex);
        if (index >= devices.size()) return 0.0;
        return devices[index].sampleRate;
    }
    std::lock_guard<std::mutex> lk(st->queueMutex);
    return st->currentRate;
}

void DeviceManager::applyLiveSampleRate(size_t index, double sampleRateHz) {
    DeviceInfo d;
    {
        std::lock_guard<std::mutex> lk(devicesMutex);
        if (index >= devices.size()) return;
        d = devices[index];
    }
    if (d.isSdrplay) sampleRateHz = SdrplayProfile::clampSampleRateHz(d.sdrplayDuoMode, sampleRateHz);
    const double live = getCurrentSampleRate(index);
    updateDeviceParams(index, sampleRateHz, d.gain, d.antenna, d.frequencyCorrectionPpm);
    if (isStreaming(index) && std::abs(live - sampleRateHz) > 1.0) {
        stopStreaming(index);
        startStreaming(index, true);
    }
}

size_t DeviceManager::preferredListenDeviceIndex() const {
    std::lock_guard<std::mutex> lk(devicesMutex);
    if (preferredListenDeviceIndex_ < devices.size()) return preferredListenDeviceIndex_;
    return 0;
}

void DeviceManager::setPreferredListenDeviceIndex(size_t index) {
    std::lock_guard<std::mutex> lk(devicesMutex);
    preferredListenDeviceIndex_ = index;
}

void DeviceManager::setLiveAgc(size_t index, bool enabled) {
    DeviceInfo d;
    {
        std::lock_guard<std::mutex> lk(devicesMutex);
        if (index >= devices.size() || !devices[index].isSdrplay) return;
        devices[index].agcEnabled = enabled;
        d = devices[index];
        saveSettings();
    }
#ifdef HAVE_SOAPYSDR
    if (auto* stPtr = streamState(index)) {
        auto& st = *stPtr;
        std::lock_guard<std::mutex> stateLock(st.stateMutex);
        if (st.soapyDev && !st.stopFlag) {
            try {
                std::lock_guard<std::mutex> soapyLiveLock(gSoapyLiveIoMutex);
                st.soapyDev->setGainMode(SOAPY_SDR_RX, d.rxChannel, enabled);
                if (!enabled) {
                    try { st.soapyDev->setGain(SOAPY_SDR_RX, d.rxChannel, "IFGR", d.ifgrDb); } catch (...) {}
                    try { st.soapyDev->setGain(SOAPY_SDR_RX, d.rxChannel, "RFGR", d.rfgrDb); } catch (...) {}
                }
            } catch (const std::exception& ex) {
                spdlog::warn("Live AGC failed on device {}: {}", index, ex.what());
            }
        }
    }
#endif
}

void DeviceManager::setLiveGainElement(size_t index, const std::string& element, double valueDb) {
    DeviceInfo d;
    {
        std::lock_guard<std::mutex> lk(devicesMutex);
        if (index >= devices.size() || !devices[index].isSdrplay) return;
        if (element == "IFGR") devices[index].ifgrDb = valueDb;
        else if (element == "RFGR") {
            devices[index].rfgrDb = valueDb;
            devices[index].gain = clampGainForDevice(devices[index], valueDb);
        }
        devices[index].agcEnabled = false;
        d = devices[index];
        saveSettings();
    }
#ifdef HAVE_SOAPYSDR
    if (auto* stPtr = streamState(index)) {
        auto& st = *stPtr;
        std::lock_guard<std::mutex> stateLock(st.stateMutex);
        if (st.soapyDev && !st.stopFlag) {
            try {
                std::lock_guard<std::mutex> soapyLiveLock(gSoapyLiveIoMutex);
                try { st.soapyDev->setGainMode(SOAPY_SDR_RX, d.rxChannel, false); } catch (...) {}
                st.soapyDev->setGain(SOAPY_SDR_RX, d.rxChannel, element, valueDb);
            } catch (const std::exception& ex) {
                spdlog::warn("Live gain element {} failed on device {}: {}", element, index, ex.what());
            }
        }
    }
#endif
}

void DeviceManager::setLiveBandwidth(size_t index, double bandwidthHz) {
    DeviceInfo d;
    {
        std::lock_guard<std::mutex> lk(devicesMutex);
        if (index >= devices.size() || !devices[index].isSdrplay) return;
        devices[index].bandwidthHz = bandwidthHz;
        d = devices[index];
        saveSettings();
    }
#ifdef HAVE_SOAPYSDR
    if (bandwidthHz <= 0.0) return;
    if (auto* stPtr = streamState(index)) {
        auto& st = *stPtr;
        std::lock_guard<std::mutex> stateLock(st.stateMutex);
        if (st.soapyDev && !st.stopFlag) {
            try {
                std::lock_guard<std::mutex> soapyLiveLock(gSoapyLiveIoMutex);
                st.soapyDev->setBandwidth(SOAPY_SDR_RX, d.rxChannel, bandwidthHz);
            } catch (const std::exception& ex) {
                spdlog::warn("Live bandwidth failed on device {}: {}", index, ex.what());
            }
        }
    }
#endif
}

void DeviceManager::setLiveSdrplaySetting(size_t index, const std::string& key, const std::string& value) {
    DeviceInfo d;
    {
        std::lock_guard<std::mutex> lk(devicesMutex);
        if (index >= devices.size() || !devices[index].isSdrplay) return;
        // Refuse Bias-T on Hi-Z / Antenna C (BNC) — forces off and skips hardware write.
        if (key == SdrplaySettings::kBiasT &&
            SdrplayProfile::parseBoolSetting(value, false) &&
            !SdrplayProfile::biasTAllowedForAntenna(devices[index].sdrplayModel, devices[index].antenna)) {
            devices[index].soapySettings[key] = "false";
            saveSettings();
            spdlog::warn("Bias-T blocked on device {} antenna '{}' (incompatible port)",
                         index, devices[index].antenna);
            return;
        }
        devices[index].soapySettings[key] = value;
        d = devices[index];
        saveSettings();
    }
#ifdef HAVE_SOAPYSDR
    if (auto* stPtr = streamState(index)) {
        auto& st = *stPtr;
        std::lock_guard<std::mutex> stateLock(st.stateMutex);
        if (st.soapyDev && !st.stopFlag) {
            try {
                std::lock_guard<std::mutex> soapyLiveLock(gSoapyLiveIoMutex);
                st.soapyDev->writeSetting(key, value);
            } catch (const std::exception& ex) {
                spdlog::warn("Live SDRplay setting {}={} failed on device {}: {}", key, value, index, ex.what());
            }
        }
    }
#endif
}

void DeviceManager::setLiveAntenna(size_t index, const std::string& antenna) {
    DeviceInfo d;
    {
        std::lock_guard<std::mutex> lk(devicesMutex);
        if (index >= devices.size()) return;
        devices[index].antenna = antenna;
        if (devices[index].isSdrplay &&
            !SdrplayProfile::biasTAllowedForAntenna(devices[index].sdrplayModel, antenna)) {
            devices[index].soapySettings[SdrplaySettings::kBiasT] = "false";
        }
        d = devices[index];
        saveSettings();
    }
#ifdef HAVE_SOAPYSDR
    if (d.isDiversityComposite) return;
    if (auto* stPtr = streamState(index)) {
        auto& st = *stPtr;
        std::lock_guard<std::mutex> stateLock(st.stateMutex);
        if (st.soapyDev && !st.stopFlag) {
            try {
                std::lock_guard<std::mutex> soapyLiveLock(gSoapyLiveIoMutex);
                const size_t ch = d.isSdrplay ? d.rxChannel : 0;
                st.soapyDev->setAntenna(SOAPY_SDR_RX, ch, antenna);
                if (d.isSdrplay &&
                    !SdrplayProfile::biasTAllowedForAntenna(d.sdrplayModel, antenna)) {
                    try { st.soapyDev->writeSetting(SdrplaySettings::kBiasT, "false"); } catch (...) {}
                }
                spdlog::info("Live antenna applied to device {}: {}", index, antenna);
            } catch (const std::exception& ex) {
                spdlog::warn("Live antenna failed on device {}: {}", index, ex.what());
            }
        }
    }
#endif
}

SdrplayDiversity::Config DeviceManager::getDiversityConfig() const {
    return diversityConfig_;
}

size_t DeviceManager::ensureDiversityCompositeDeviceLocked(size_t deviceIndexHint) {
    if (deviceIndexHint >= devices.size()) return static_cast<size_t>(-1);
    const auto& hint = devices[deviceIndexHint];
    if (!hint.isSdrplay || hint.sdrplayDuoMode != "DT") return static_cast<size_t>(-1);

    size_t idxA = static_cast<size_t>(-1);
    size_t idxB = static_cast<size_t>(-1);
    for (size_t i = 0; i < devices.size(); ++i) {
        const auto& d = devices[i];
        if (!d.isSdrplay || d.isDiversityComposite) continue;
        if (d.serial != hint.serial || d.sdrplayDuoMode != "DT") continue;
        if (d.rxChannel == 0) idxA = i;
        if (d.rxChannel == 1) idxB = i;
    }
    if (idxA == static_cast<size_t>(-1) || idxB == static_cast<size_t>(-1))
        return static_cast<size_t>(-1);

    for (size_t i = 0; i < devices.size(); ++i) {
        if (devices[i].isDiversityComposite && devices[i].serial == hint.serial) {
            devices[i].diversitySourceA = idxA;
            devices[i].diversitySourceB = idxB;
            diversityCompositeIndex_ = i;
            return i;
        }
    }

    DeviceInfo comp;
    comp.driver = "sdrplay";
    comp.isSdrplay = true;
    comp.isDiversityComposite = true;
    comp.serial = hint.serial;
    comp.sdrplayModel = "RSPduo";
    comp.sdrplayDuoMode = "DT";
    comp.label = "SDRplay Diversity (" + hint.serial + ")";
    comp.hardware = "RSPduo-Diversity";
    comp.antennas = {"Diversity A+B"};
    comp.antenna = "Diversity A+B";
    comp.sampleRates = {2e6};
    comp.sampleRate = std::min(devices[idxA].sampleRate, devices[idxB].sampleRate);
    if (comp.sampleRate <= 0) comp.sampleRate = 2e6;
    comp.minFreq = std::min(devices[idxA].minFreq, devices[idxB].minFreq);
    comp.maxFreq = std::max(devices[idxA].maxFreq, devices[idxB].maxFreq);
    comp.gainName = "RFGR";
    comp.gain = devices[idxA].gain;
    comp.gainMin = devices[idxA].gainMin;
    comp.gainMax = devices[idxA].gainMax;
    comp.diversitySourceA = idxA;
    comp.diversitySourceB = idxB;
    comp.stableKey = "sdrplay|serial:" + comp.serial + "|diversity";
    devices.push_back(std::move(comp));
    diversityCompositeIndex_ = devices.size() - 1;
    if (streams.size() <= diversityCompositeIndex_) streams.resize(diversityCompositeIndex_ + 1);
    spdlog::info("Created SDRplay diversity composite device at index {} (A={}, B={})",
                 diversityCompositeIndex_, idxA, idxB);
    return diversityCompositeIndex_;
}

size_t DeviceManager::ensureDiversityCompositeDevice(size_t deviceIndexHint) {
    std::lock_guard<std::mutex> lk(devicesMutex);
    return ensureDiversityCompositeDeviceLocked(deviceIndexHint);
}

void DeviceManager::restoreDiversityCompositeAfterEnumerate() {
    if (diversityConfig_.mode == SdrplayDiversity::Mode::Off) {
        diversityCompositeIndex_ = static_cast<size_t>(-1);
        return;
    }
    size_t hint = static_cast<size_t>(-1);
    {
        std::lock_guard<std::mutex> lk(devicesMutex);
        for (size_t i = 0; i < devices.size(); ++i) {
            if (devices[i].isSdrplay && !devices[i].isDiversityComposite &&
                devices[i].sdrplayDuoMode == "DT") {
                hint = i;
                break;
            }
        }
        if (hint == static_cast<size_t>(-1)) {
            diversityCompositeIndex_ = static_cast<size_t>(-1);
            return;
        }
        ensureDiversityCompositeDeviceLocked(hint);
    }
}

bool DeviceManager::configureDiversity(size_t deviceIndexHint, SdrplayDiversity::Config config) {
    diversityConfig_ = config;
    if (config.mode == SdrplayDiversity::Mode::Off) {
        spdlog::info("SDRplay diversity disabled");
        return true;
    }
    const size_t comp = ensureDiversityCompositeDevice(deviceIndexHint);
    if (comp == static_cast<size_t>(-1)) {
        spdlog::warn("SDRplay diversity requires Dual Tuner ch0+ch1 for the selected device");
        diversityConfig_.mode = SdrplayDiversity::Mode::Off;
        return false;
    }
    size_t a = static_cast<size_t>(-1), b = static_cast<size_t>(-1);
    {
        std::lock_guard<std::mutex> lk(devicesMutex);
        if (comp < devices.size()) {
            a = devices[comp].diversitySourceA;
            b = devices[comp].diversitySourceB;
            devices[comp].enabled = true;
        }
    }
    if (a != static_cast<size_t>(-1)) startStreaming(a, true);
    if (b != static_cast<size_t>(-1)) startStreaming(b, true);
    startStreaming(comp, false); // composite uses soft combine path (no Soapy)
    setPreferredListenDeviceIndex(comp);
    spdlog::info("SDRplay diversity enabled mode={} phase={} deg ampB={} composite={}",
                 SdrplayDiversity::modeName(config.mode), config.phaseDeg, config.amplitudeB, comp);
    return true;
}

std::vector<std::complex<float>> DeviceManager::getDiversityCombinedIQ(size_t maxSamples) const {
    size_t a = static_cast<size_t>(-1), b = static_cast<size_t>(-1);
    SdrplayDiversity::Config cfg = diversityConfig_;
    {
        std::lock_guard<std::mutex> lk(devicesMutex);
        if (diversityCompositeIndex_ < devices.size() && devices[diversityCompositeIndex_].isDiversityComposite) {
            a = devices[diversityCompositeIndex_].diversitySourceA;
            b = devices[diversityCompositeIndex_].diversitySourceB;
        }
    }
    if (cfg.mode == SdrplayDiversity::Mode::Off || a == static_cast<size_t>(-1) || b == static_cast<size_t>(-1))
        return {};
    auto wa = const_cast<DeviceManager*>(this)->getRecentIQWindow(a, maxSamples);
    auto wb = const_cast<DeviceManager*>(this)->getRecentIQWindow(b, maxSamples);
    if (wa.size() > wb.size()) wa.erase(wa.begin(), wa.begin() + static_cast<std::ptrdiff_t>(wa.size() - wb.size()));
    if (wb.size() > wa.size()) wb.erase(wb.begin(), wb.begin() + static_cast<std::ptrdiff_t>(wb.size() - wa.size()));
    return SdrplayDiversity::combine(wa, wb, cfg);
}

size_t DeviceManager::getIQQueueDepth(size_t index) const {
    // Safe under stream's queueMutex (P2 audit: was unlocked .size() read in rx path too)
    auto* stPtr = streamState(index);
    if (!stPtr) return 0;
    auto& st = *stPtr;
    std::lock_guard<std::mutex> lk(st.queueMutex);
    return st.iqQueue.size();
}

std::string DeviceManager::getRuntimeStateLabel(size_t index) const {
    auto* stPtr = streamState(index);
    if (!stPtr) return "stopped";
    auto& st = *stPtr;
    std::lock_guard<std::mutex> lk(st.stateMutex);
    return st.runtimeState.empty() ? std::string("stopped") : st.runtimeState;
}

// S0-3 (P1): non-consuming recent window so N receivers on the same device each get a coherent
// recent RF capture for their private channelizer/demod. Read from the absolute-sample ring
// instead of the consuming iqQueue so monitor/CLI P25 sync cannot starve or disturb spectrum.
DeviceManager::RecentIQWindow DeviceManager::getRecentIQWindowWithCursor(size_t index, size_t maxSamples) {
    RecentIQWindow outWindow;
    if (maxSamples == 0) return outWindow;
    auto* stPtr = streamState(index);
    if (!stPtr) return outWindow;
    auto& st = *stPtr;
    {
        std::lock_guard<std::mutex> stateLock(st.stateMutex);
        if (!st.active) return outWindow;
    }

    std::lock_guard<std::mutex> ringLock(st.ringMutex);
    const size_t cap = st.ringCapacity;
    outWindow.streamEpoch = st.streamEpoch.load(std::memory_order_acquire);
    const uint64_t total = st.totalSamplesWritten.load(std::memory_order_acquire);
    if (cap == 0 || st.iqRing.empty() || total == 0) return outWindow;

    const uint64_t available = std::min<uint64_t>(total, static_cast<uint64_t>(cap));
    const size_t toRead = static_cast<size_t>(std::min<uint64_t>(available, static_cast<uint64_t>(maxSamples)));
    // Return the newest contiguous ring window in chronological order.
    // Fast bulk copy (1-2 segments) to minimize time holding ringMutex.
    // Previous element-by-element push_back loop could hold the mutex for tens of ms on
    // large pulls (e.g. 256k-2M samples), causing GUI control-decode / DSP readers waiting
    // on ring to stall the event loop / workers -> perceived freezes during capture + returns.
    const uint64_t start = total - static_cast<uint64_t>(toRead);
    outWindow.startAbsolute = start;
    outWindow.endAbsolute = total;
    outWindow.samples.resize(toRead);
    if (toRead > 0) {
        const bool powerOfTwoCap = (cap & (cap - 1)) == 0;
        const uint64_t startAbs = start;
        const size_t startIdx = powerOfTwoCap
            ? static_cast<size_t>(startAbs) & (cap - 1)
            : static_cast<size_t>(startAbs % static_cast<uint64_t>(cap));
        const size_t firstPart = std::min(toRead, cap - startIdx);
        std::copy(st.iqRing.begin() + startIdx,
                  st.iqRing.begin() + startIdx + firstPart,
                  outWindow.samples.begin());
        if (firstPart < toRead) {
            std::copy(st.iqRing.begin(),
                      st.iqRing.begin() + (toRead - firstPart),
                      outWindow.samples.begin() + firstPart);
        }
    }
    return outWindow;

}

std::vector<std::complex<float>> DeviceManager::getRecentIQWindow(size_t index, size_t maxSamples) {
    return getRecentIQWindowWithCursor(index, maxSamples).samples;
}

// S0 / audit-followup-2: cursor based new-samples only, chronological, per-rx.
// Replaces the "always take newest overlapping window" anti-pattern that caused repeated demod of the same data / chop.
DeviceManager::RecentIQWindow DeviceManager::getNewIQWindowForReceiver(size_t devIndex, Receiver& rx, size_t maxSamples,
                                                                       size_t maxLagSamples) {
    RecentIQWindow outWindow;
    if (maxSamples == 0) return outWindow;
    auto* stPtr = streamState(devIndex);
    if (!stPtr) return outWindow;
    auto& st = *stPtr;
    if (st.ringCapacity == 0) return outWindow;

    std::lock_guard<std::mutex> ringLock(st.ringMutex);
    const uint64_t epoch = st.streamEpoch.load(std::memory_order_acquire);
    outWindow.streamEpoch = epoch;
    uint64_t myLast = rx.lastConsumedAbsolute.load(std::memory_order_acquire);
    uint64_t total = st.totalSamplesWritten.load(std::memory_order_acquire);
    const uint64_t retuneFloor = st.retuneValidFromAbsolute.load(std::memory_order_acquire);

    const uint64_t rxEpoch = rx.lastSeenStreamEpoch.load(std::memory_order_acquire);
    if (rxEpoch != epoch) {
        // Soft retune handoff: re-anchor at the live edge with pre-roll IQ instead of
        // returning an empty discontinuity window.  Downstream P25 resets timing state
        // on streamEpoch change but keeps decoding the returned samples.
        myLast = (total > static_cast<uint64_t>(maxSamples))
            ? total - static_cast<uint64_t>(maxSamples)
            : 0;
        if (retuneFloor > 0 && myLast < retuneFloor) {
            myLast = retuneFloor;
        }
        rx.lastConsumedAbsolute.store(myLast, std::memory_order_release);
        rx.lastSeenStreamEpoch.store(epoch, std::memory_order_release);
    } else if (retuneFloor > 0 && myLast < retuneFloor) {
        myLast = retuneFloor;
        rx.lastConsumedAbsolute.store(myLast, std::memory_order_release);
    }

    uint64_t available = (total > myLast) ? (total - myLast) : 0;

    if (myLast > total) {
        // Absolute cursor belongs to an older stream epoch or ring reset. Do not
        // silently rewind; tell callers to reset their P25 rolling state.
        myLast = (total > static_cast<uint64_t>(maxSamples))
            ? total - static_cast<uint64_t>(maxSamples)
            : 0;
        rx.lastConsumedAbsolute.store(myLast, std::memory_order_release);
        outWindow.startAbsolute = myLast;
        outWindow.endAbsolute = myLast;
        outWindow.cursorDiscontinuity = true;
        return outWindow;
    }

    // New/reactivated/retuned receivers should monitor the live edge, not drain old IQ
    // left in the shared ring from a previous station or mode.
    if (myLast == 0 && total > (uint64_t)maxSamples) {
        myLast = total - (uint64_t)maxSamples;
        rx.lastConsumedAbsolute.store(myLast, std::memory_order_release);
        available = total - myLast;
    }

    // Analog realtime path: the IQ ring can hold ~10+ seconds. If DSP falls behind,
    // skip old samples instead of playing speech many seconds late.
    // Keep a healthy live tail (~100 ms worth of maxSamples blocks) so FIR/squelch
    // are not reset every couple of blocks (that sounds like a helicopter).
    // IMPORTANT: soft lag skip must NOT set cursorDiscontinuity. Marking a gap here
    // forced WFM multiplex/RDS reset every catch-up → permanent buzz + UI thrash
    // (see sdr_town.log "analog catch-up" every ~3s on 98.1 WFM).
    if (maxLagSamples > 0 && available > static_cast<uint64_t>(maxLagSamples)) {
        const uint64_t keep = std::min<uint64_t>(
            static_cast<uint64_t>(maxSamples) * 4ull,
            std::max<uint64_t>(static_cast<uint64_t>(maxLagSamples) / 4ull, static_cast<uint64_t>(maxSamples)));
        myLast = (total > keep) ? total - keep : 0;
        rx.lastConsumedAbsolute.store(myLast, std::memory_order_release);
        available = (total > myLast) ? (total - myLast) : 0;
        static thread_local auto lastLagLog = std::chrono::steady_clock::time_point{};
        const auto nowLag = std::chrono::steady_clock::now();
        if (nowLag - lastLagLog > std::chrono::seconds(2)) {
            spdlog::warn("Receiver on dev {} analog catch-up: dropped backlog to stay within {} samples of live edge (soft, no demod reset).",
                devIndex, (unsigned long long)maxLagSamples);
            lastLagLog = nowLag;
        }
    }

    if (available == 0) return outWindow;

    // If we are way behind the ring (data was overwritten), skip forward.
    // Log once per big drop and advance cursor. Return a short zero block so demod can ramp/squelch naturally (fade).
    if (available > st.ringCapacity) {
        spdlog::warn("Receiver on dev {} fell behind by {} samples; dropping old data and skipping forward (audio may have a brief dropout/fade).", devIndex, (unsigned long long)(available - st.ringCapacity));
        // Leave a little headroom so we have some new data this time
        myLast = total - (st.ringCapacity / 2);
        rx.lastConsumedAbsolute.store(myLast, std::memory_order_release);
        available = (total > myLast) ? (total - myLast) : 0;
        if (available == 0) return outWindow;
        // Return a small zeroed block to give the downstream (squelch, resample, audio) a chance to fade cleanly
        size_t fadeLen = std::min((size_t)256, maxSamples);
        outWindow.samples.assign(fadeLen, std::complex<float>(0,0));
        outWindow.startAbsolute = myLast;
        outWindow.endAbsolute = myLast + static_cast<uint64_t>(fadeLen);
        outWindow.cursorDiscontinuity = true;
        rx.lastConsumedAbsolute.store(outWindow.endAbsolute, std::memory_order_release);
        return outWindow;
    }

    size_t toRead = (size_t)std::min((uint64_t)maxSamples, available);

    outWindow.samples.resize(toRead);
    outWindow.startAbsolute = myLast;
    outWindow.endAbsolute = myLast + static_cast<uint64_t>(toRead);

    // Same 1–2 segment memcpy as getRecentIQWindowWithCursor. The old
    // per-sample push_back held ringMutex across hundreds of thousands of
    // live RTL samples and stalled the voice worker behind the tuner clock.
    if (toRead > 0) {
        const size_t cap = st.ringCapacity;
        const bool powerOfTwoCap = (cap & (cap - 1)) == 0;
        const size_t startIdx = powerOfTwoCap
            ? static_cast<size_t>(myLast) & (cap - 1)
            : static_cast<size_t>(myLast % static_cast<uint64_t>(cap));
        const size_t firstPart = std::min(toRead, cap - startIdx);
        std::copy(st.iqRing.begin() + static_cast<std::ptrdiff_t>(startIdx),
                  st.iqRing.begin() + static_cast<std::ptrdiff_t>(startIdx + firstPart),
                  outWindow.samples.begin());
        if (firstPart < toRead) {
            std::copy(st.iqRing.begin(),
                      st.iqRing.begin() + static_cast<std::ptrdiff_t>(toRead - firstPart),
                      outWindow.samples.begin() + static_cast<std::ptrdiff_t>(firstPart));
        }
    }

    rx.lastConsumedAbsolute.store(outWindow.endAbsolute, std::memory_order_release);
    return outWindow;
}

std::vector<std::complex<float>> DeviceManager::getNewSamplesForReceiver(size_t devIndex, Receiver& rx, size_t maxSamples) {
    return getNewIQWindowForReceiver(devIndex, rx, maxSamples).samples;
}

void DeviceManager::setReceiverCursorToLiveEdge(size_t devIndex, Receiver& rx) {
    auto* stPtr = streamState(devIndex);
    if (!stPtr) {
        rx.lastConsumedAbsolute.store(0, std::memory_order_release);
        rx.lastSeenStreamEpoch.store(0, std::memory_order_release);
        return;
    }
    auto& st = *stPtr;
    std::lock_guard<std::mutex> ringLock(st.ringMutex);
    rx.lastSeenStreamEpoch.store(st.streamEpoch.load(std::memory_order_acquire),
                                 std::memory_order_release);
    rx.lastConsumedAbsolute.store(st.totalSamplesWritten.load(std::memory_order_acquire),
                                  std::memory_order_release);
}

void DeviceManager::syncReceiverCursorToAbsolute(size_t devIndex, Receiver& rx, uint64_t absoluteSample) {
    auto* stPtr = streamState(devIndex);
    if (!stPtr) return;
    auto& st = *stPtr;
    std::lock_guard<std::mutex> ringLock(st.ringMutex);
    const uint64_t total = st.totalSamplesWritten.load(std::memory_order_acquire);
    const uint64_t epoch = st.streamEpoch.load(std::memory_order_acquire);
    uint64_t anchor = std::min(absoluteSample, total);
    if (st.ringCapacity > 0 && total > st.ringCapacity) {
        anchor = std::max(anchor, total - st.ringCapacity);
    }
    rx.lastConsumedAbsolute.store(anchor, std::memory_order_release);
    rx.lastSeenStreamEpoch.store(epoch, std::memory_order_release);
}

void DeviceManager::setReceiverCursorBeforeLiveEdge(size_t devIndex, Receiver& rx, size_t preRollSamples) {
    auto* stPtr = streamState(devIndex);
    if (!stPtr) {
        rx.lastConsumedAbsolute.store(0, std::memory_order_release);
        rx.lastSeenStreamEpoch.store(0, std::memory_order_release);
        return;
    }
    auto& st = *stPtr;
    std::lock_guard<std::mutex> ringLock(st.ringMutex);
    rx.lastSeenStreamEpoch.store(st.streamEpoch.load(std::memory_order_acquire),
                                 std::memory_order_release);
    const uint64_t total = st.totalSamplesWritten.load(std::memory_order_acquire);
    const uint64_t available = std::min<uint64_t>(total, static_cast<uint64_t>(st.ringCapacity));
    const uint64_t pre = std::min<uint64_t>(available, static_cast<uint64_t>(preRollSamples));
    uint64_t cursor = total >= pre ? total - pre : 0;
    const uint64_t retuneFloor = st.retuneValidFromAbsolute.load(std::memory_order_acquire);
    if (retuneFloor > 0 && cursor < retuneFloor) {
        cursor = retuneFloor;
    }
    rx.lastConsumedAbsolute.store(cursor, std::memory_order_release);
}

void DeviceManager::appendIQBlock(size_t index, StreamState& st, std::vector<std::complex<float>>&& block) {
    if (block.empty()) return;
    // DEC-0115: RX owns this state. No device lookup while holding live-I/O;
    // enumeration and settings can already hold devicesMutex/stateMutex.

    // Feed the per-rx ring *first* while we still own the data (before any move into queue).
    if (st.ringCapacity > 0) {
        std::lock_guard<std::mutex> ringLock(st.ringMutex);
        const size_t cap = st.ringCapacity;
        const bool powerOfTwoCap = (cap & (cap - 1)) == 0;
        size_t w = st.ringWriteIdx.load(std::memory_order_relaxed);
        for (const auto& s : block) {
            st.iqRing[w] = s;
            w = powerOfTwoCap ? ((w + 1) & (cap - 1)) : ((w + 1) % cap);
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
    const_cast<DeviceManager*>(this)->setupSoapyForSDRplay();

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
    drivers.insert("sdrplay");
    return std::vector<std::string>(drivers.begin(), drivers.end());
}

void DeviceManager::setupSoapyForRTLSDR() {
#ifdef _WIN32
    // Help Soapy find RTL-SDR module from common bundles like PothosSDR
    // This registers the "rtlsdr" driver if the module is present there.
    const char* commonRoots[] = {
        "C:\\Program Files\\PothosSDR",
        "C:\\Program Files (x86)\\PothosSDR",
        "C:\\ProgramData\\radioconda\\Library",
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

    // Prepend known SDR bins and app dir to PATH so driver and module dependencies resolve correctly.
    // Only do this once (or when not already present) to avoid PATH growing without bound on every
    // enumerate / start / getAvailableDrivers call.
    std::string pothosBin = "C:\\Program Files\\PothosSDR\\bin";
    std::string radiocondaBin = "C:\\ProgramData\\radioconda\\Library\\bin";
    std::string currentPath = getenv("PATH") ? getenv("PATH") : "";
    static bool pathSetupDone = false;
    if (!pathSetupDone) {
        bool needPothos = currentPath.find(pothosBin) == std::string::npos;
        bool needRadioconda = currentPath.find(radiocondaBin) == std::string::npos &&
            GetFileAttributesA(radiocondaBin.c_str()) != INVALID_FILE_ATTRIBUTES;
        bool needApp    = currentPath.find(appDir) == std::string::npos;
        if (needPothos || needRadioconda || needApp) {
            std::string newPath = currentPath;
            if (needPothos) newPath = pothosBin + ";" + newPath;
            if (needRadioconda) newPath = radiocondaBin + ";" + newPath;
            if (needApp)    newPath = appDir + ";" + newPath;
            _putenv_s("PATH", newPath.c_str());
            spdlog::debug("Updated process PATH for Soapy/RTL (once)");
        }
        pathSetupDone = true;
    }
#endif

#ifdef HAVE_SOAPYSDR
    // Register the bundled RTL factory before enumerate. This loads the module
    // only; hardware open/make still happens on the detached streaming worker.
    static bool bundledRtlModuleTried = false;
    if (!bundledRtlModuleTried) {
        bundledRtlModuleTried = true;
        try {
            const std::string bundledModule = appDir + "\\SoapyRTLSDR.dll";
            if (GetFileAttributesA(bundledModule.c_str()) != INVALID_FILE_ATTRIBUTES) {
                SoapySDR::loadModule(bundledModule);
                spdlog::info("Loaded bundled SoapyRTLSDR module for RTL discovery: {}", bundledModule);
            }
        } catch (const std::exception& ex) {
            spdlog::warn("Bundled SoapyRTLSDR module load failed during discovery: {}", ex.what());
        } catch (...) {
            spdlog::warn("Bundled SoapyRTLSDR module load failed during discovery: unknown error");
        }
    }
    spdlog::info("Soapy path setup done for RTL discovery.");
#endif
}

void DeviceManager::setupSoapyForSDRplay() {
#ifdef _WIN32
    std::string appDir = QCoreApplication::applicationDirPath().toStdString();
    const char* sdrplayRoots[] = {
        "C:\\Program Files\\SDRplay",
        "C:\\Program Files\\SDRplay\\API",
        "C:\\Program Files\\PothosSDR",
        "C:\\Program Files (x86)\\PothosSDR",
        "C:\\ProgramData\\radioconda\\Library",
        nullptr
    };
    std::string currentPath = getenv("PATH") ? getenv("PATH") : "";
    static bool sdrplayPathDone = false;
    if (!sdrplayPathDone) {
        std::string newPath = currentPath;
        bool changed = false;
        for (int i = 0; sdrplayRoots[i]; ++i) {
            std::string root = sdrplayRoots[i];
            std::string bin = root + "\\bin";
            if (GetFileAttributesA(bin.c_str()) != INVALID_FILE_ATTRIBUTES &&
                newPath.find(bin) == std::string::npos) {
                newPath = bin + ";" + newPath;
                changed = true;
            }
            if (GetFileAttributesA(root.c_str()) != INVALID_FILE_ATTRIBUTES &&
                newPath.find(root) == std::string::npos) {
                newPath = root + ";" + newPath;
                changed = true;
            }
        }
        if (newPath.find(appDir) == std::string::npos) {
            newPath = appDir + ";" + newPath;
            changed = true;
        }
        if (changed) {
            _putenv_s("PATH", newPath.c_str());
            spdlog::debug("Updated process PATH for SDRplay API / SoapySDRPlay");
        }
        sdrplayPathDone = true;
    }

    // Prefer an existing SOAPY_SDR_ROOT; otherwise point at Pothos/radioconda if present.
    if (!getenv("SOAPY_SDR_ROOT") || !*getenv("SOAPY_SDR_ROOT")) {
        const char* soapyRoots[] = {
            "C:\\Program Files\\PothosSDR",
            "C:\\Program Files (x86)\\PothosSDR",
            "C:\\ProgramData\\radioconda\\Library",
            nullptr
        };
        for (int i = 0; soapyRoots[i]; ++i) {
            std::string modPath = std::string(soapyRoots[i]) + "\\lib\\SoapySDR\\modules";
            std::string modPath08 = std::string(soapyRoots[i]) + "\\lib\\SoapySDR\\modules0.8";
            if (GetFileAttributesA(modPath.c_str()) != INVALID_FILE_ATTRIBUTES ||
                GetFileAttributesA(modPath08.c_str()) != INVALID_FILE_ATTRIBUTES) {
                _putenv_s("SOAPY_SDR_ROOT", soapyRoots[i]);
                break;
            }
        }
    }
#endif

    bool apiPresent = false;
    bool moduleLoaded = false;
    bool deviceEnumerated = false;
    std::string modulePath;
    std::string moduleError;
#ifdef _WIN32
    const auto apiCandidates = SdrplayProfile::windowsApiCandidates(appDir);
    for (const auto& candidate : apiCandidates) {
        if (GetFileAttributesA(candidate.c_str()) != INVALID_FILE_ATTRIBUTES) {
            apiPresent = true;
            spdlog::debug("SDRplay API candidate found: {}", candidate);
            break;
        }
    }
#endif

#ifdef HAVE_SOAPYSDR
    static bool sdrplayModuleLoaded = false;
    static std::string sdrplayModulePath;
    static std::string sdrplayModuleError;
    if (!sdrplayModuleLoaded) {
        std::string appDir = QCoreApplication::applicationDirPath().toStdString();
        sdrplayModuleError.clear();
#ifdef _WIN32
        for (const auto& bundled : SdrplayProfile::windowsSoapyModuleCandidates(appDir)) {
            if (GetFileAttributesA(bundled.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
            try {
                const std::string loadError = SoapySDR::loadModule(bundled);
                if (loadError.empty()) {
                    sdrplayModuleLoaded = true;
                    sdrplayModulePath = bundled;
                    sdrplayModuleError.clear();
                    spdlog::info("Loaded SDRplay Soapy module: {}", bundled);
                    break;
                }
                sdrplayModuleError = bundled + ": " + loadError;
                spdlog::warn("SDRplay Soapy module rejected {}: {}", bundled, loadError);
            } catch (const std::exception& ex) {
                sdrplayModuleError = bundled + ": " + ex.what();
                spdlog::warn("SDRplay Soapy module load failed for {}: {}", bundled, ex.what());
            } catch (...) {
                sdrplayModuleError = bundled + ": unknown loader error";
                spdlog::warn("SDRplay Soapy module load failed for {}: unknown error", bundled);
            }
        }
#else
        (void)appDir;
#endif
    }
    moduleLoaded = sdrplayModuleLoaded;
    modulePath = sdrplayModulePath;
    moduleError = sdrplayModuleError;

    // Confirm driver registration if possible.
    try {
        auto results = SoapySDR::Device::enumerate({{"driver", "sdrplay"}});
        deviceEnumerated = !results.empty();
        if (deviceEnumerated) {
            moduleLoaded = true;
            sdrplayModuleLoaded = true;
            if (sdrplayModulePath.empty())
                sdrplayModulePath = "registered SoapySDRPlay driver";
            modulePath = sdrplayModulePath;
            moduleError.clear();
        }
        if (moduleLoaded && !deviceEnumerated) {
            spdlog::warn("SDRplay Soapy module is loaded, but no RSP was enumerated; "
                         "check the SDRplay API service, USB connection, and exclusive access");
        }
    } catch (const std::exception& ex) {
        spdlog::warn("SDRplay enumeration failed after module load: {}", ex.what());
    } catch (...) {
        spdlog::warn("SDRplay enumeration failed after module load: unknown error");
    }
#endif

#ifndef _WIN32
    // Non-Windows hosts still report status from Soapy enumeration above.
    if (!apiPresent) apiPresent = moduleLoaded;
#endif

    if (apiPresent && moduleLoaded && deviceEnumerated) {
        sdrplaySetupStatus_ = "SDRplay: API and SoapySDRPlay module ready; RSP detected";
        if (!modulePath.empty()) sdrplaySetupStatus_ += " (" + modulePath + ")";
    } else if (apiPresent && moduleLoaded) {
        sdrplaySetupStatus_ = "SDRplay: API and SoapySDRPlay module loaded, but no RSP detected "
                               "— check the API service, USB connection, and other SDR software";
    } else if (apiPresent && !moduleLoaded && !moduleError.empty()) {
        sdrplaySetupStatus_ = "SDRplay: SoapySDRPlay module failed to load — " + moduleError +
                               " — install a matching 64-bit module, then Rescan";
    } else if (!apiPresent && !moduleLoaded) {
        sdrplaySetupStatus_ = "SDRplay: install SDRplay API 3.x and SoapySDRPlay3 (e.g. PothosSDR), then restart";
    } else if (!apiPresent) {
        sdrplaySetupStatus_ = "SDRplay: Soapy module present but SDRplay API DLL not found — install API 3.x from sdrplay.com";
    } else {
        sdrplaySetupStatus_ = "SDRplay: API found but SoapySDRPlay module missing — install PothosSDR/radioconda SoapySDRPlay3";
    }
    spdlog::info("{}", sdrplaySetupStatus_);
}

uint64_t DeviceManager::setCenterFreq(size_t index, double freqHz) {
    if (!std::isfinite(freqHz) || freqHz <= 0.0)
        throw std::invalid_argument("Center frequency must be finite and positive");
    StreamState* stPtr = nullptr;
    size_t diversityA = static_cast<size_t>(-1);
    size_t diversityB = static_cast<size_t>(-1);
    bool isComposite = false;
    {
        std::lock_guard<std::mutex> lk(devicesMutex);
        if (index >= devices.size()) return 0;
        if (streams.size() <= index) streams.resize(index + 1);
        if (!streams[index]) streams[index] = std::make_unique<StreamState>();
        stPtr = streams[index].get();
        if (devices[index].isDiversityComposite) {
            isComposite = true;
            diversityA = devices[index].diversitySourceA;
            diversityB = devices[index].diversitySourceB;
        }
    }
    auto& st = *stPtr;
    {
        std::lock_guard<std::mutex> lk(st.queueMutex);
        st.currentCenter = freqHz;
    }
    const uint64_t seq = st.centerTuneRequestSeq.fetch_add(1, std::memory_order_acq_rel) + 1;

    // Keep Dual Tuner sources locked to the same LO for coherent diversity.
    if (isComposite) {
        if (diversityA != static_cast<size_t>(-1)) setCenterFreq(diversityA, freqHz);
        if (diversityB != static_cast<size_t>(-1)) setCenterFreq(diversityB, freqHz);
        st.centerTuneAppliedSeq.store(seq, std::memory_order_release);
        return seq;
    }

    bool active = false;
#ifdef HAVE_SOAPYSDR
    bool realHardwareReady = false;
#endif
    {
        std::lock_guard<std::mutex> lk(st.stateMutex);
        active = st.active;
#ifdef HAVE_SOAPYSDR
        realHardwareReady = (st.soapyDev != nullptr);
#endif
    }

#ifdef HAVE_SOAPYSDR
    if (active && realHardwareReady) {
        spdlog::debug("Queued center freq {} for device {} (retune seq {})", freqHz, index, seq);
    } else {
        spdlog::debug("Recorded center freq {} for device {} (active={}, realReady={}, seq {})", freqHz, index, active, realHardwareReady, seq);
    }
#else
    spdlog::debug("Recorded center freq {} for device {} (active={}, seq {})", freqHz, index, active, seq);
#endif
    return seq;
}

uint64_t DeviceManager::getCenterTuneRequestSeq(size_t index) const {
    auto* stPtr = streamState(index);
    if (!stPtr) return 0;
    return stPtr->centerTuneRequestSeq.load(std::memory_order_acquire);
}

uint64_t DeviceManager::getCenterTuneAppliedSeq(size_t index) const {
    auto* stPtr = streamState(index);
    if (!stPtr) return 0;
    return stPtr->centerTuneAppliedSeq.load(std::memory_order_acquire);
}

bool DeviceManager::waitForCenterTuneApplied(size_t index, uint64_t requestSeq, int timeoutMs) const {
    if (requestSeq == 0) return false;
    auto* stPtr = streamState(index);
    if (!stPtr) return false;
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(std::max(0, timeoutMs));
    do {
        if (stPtr->centerTuneFailedSeq.load(std::memory_order_acquire) == requestSeq) return false;
        if (stPtr->centerTuneAppliedSeq.load(std::memory_order_acquire) >= requestSeq) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    } while (std::chrono::steady_clock::now() < deadline);
    return stPtr->centerTuneFailedSeq.load(std::memory_order_acquire) != requestSeq &&
           stPtr->centerTuneAppliedSeq.load(std::memory_order_acquire) >= requestSeq;
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

void DeviceManager::rxThreadFunc(size_t index, uint64_t expectedGeneration) {
    auto* stPtr = streamState(index);
    if (!stPtr) return;
    auto& st = *stPtr;
    st.rxThreadRunning.store(true, std::memory_order_release);
    struct RunningGuard {
        StreamState& st;
        ~RunningGuard() { st.rxThreadRunning.store(false, std::memory_order_release); }
    } runningGuard{st};
    const uint64_t myGen = expectedGeneration;
    const size_t blockSize = 32768; // much larger blocks to sustain 2MS/s+ without overflow. 2048 was only ~1ms of RF at 2.048MS/s.

    // Host diversity combiner: pull aligned new IQ from Dual Tuner A/B rings and append.
    {
        size_t srcA = static_cast<size_t>(-1), srcB = static_cast<size_t>(-1);
        bool isComposite = false;
        {
            std::lock_guard<std::mutex> lk(devicesMutex);
            if (index < devices.size() && devices[index].isDiversityComposite) {
                isComposite = true;
                srcA = devices[index].diversitySourceA;
                srcB = devices[index].diversitySourceB;
            }
        }
        if (isComposite) {
            {
                std::lock_guard<std::mutex> lk(st.stateMutex);
                st.isReal = true; // soft-real: combined from hardware (or stub) sources
                st.runtimeState = "diversity combine";
            }
            auto copyRingFrom = [](StreamState& src, uint64_t startAbs, size_t count,
                                   std::vector<std::complex<float>>& out) {
                std::lock_guard<std::mutex> ringLock(src.ringMutex);
                const size_t cap = src.ringCapacity;
                const uint64_t total = src.totalSamplesWritten.load(std::memory_order_acquire);
                if (cap == 0 || src.iqRing.empty() || count == 0 || startAbs + count > total) {
                    out.clear();
                    return;
                }
                out.resize(count);
                const bool powerOfTwoCap = (cap & (cap - 1)) == 0;
                const size_t startIdx = powerOfTwoCap
                    ? static_cast<size_t>(startAbs) & (cap - 1)
                    : static_cast<size_t>(startAbs % static_cast<uint64_t>(cap));
                const size_t firstPart = std::min(count, cap - startIdx);
                std::copy(src.iqRing.begin() + static_cast<std::ptrdiff_t>(startIdx),
                          src.iqRing.begin() + static_cast<std::ptrdiff_t>(startIdx + firstPart),
                          out.begin());
                if (firstPart < count) {
                    std::copy(src.iqRing.begin(),
                              src.iqRing.begin() + static_cast<std::ptrdiff_t>(count - firstPart),
                              out.begin() + static_cast<std::ptrdiff_t>(firstPart));
                }
            };

            uint64_t cursorA = 0, cursorB = 0;
            bool primed = false;
            auto lastSpectrumTime = std::chrono::steady_clock::now();
            while (!st.stopFlag && st.sessionGen.load(std::memory_order_acquire) == myGen) {
                // Pick up live phase/mode changes without restarting the stream.
                SdrplayDiversity::Config cfg = diversityConfig_;
                if (cfg.mode == SdrplayDiversity::Mode::Off) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    continue;
                }
                auto* sa = streamState(srcA);
                auto* sb = streamState(srcB);
                if (!sa || !sb) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    continue;
                }
                uint64_t totA = sa->totalSamplesWritten.load(std::memory_order_acquire);
                uint64_t totB = sb->totalSamplesWritten.load(std::memory_order_acquire);
                if (!primed) {
                    cursorA = totA;
                    cursorB = totB;
                    primed = true;
                    continue;
                }
                const size_t availA = (totA > cursorA) ? static_cast<size_t>(totA - cursorA) : 0;
                const size_t availB = (totB > cursorB) ? static_cast<size_t>(totB - cursorB) : 0;
                size_t n = std::min({availA, availB, blockSize});
                if (n == 0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                    continue;
                }
                std::vector<std::complex<float>> wa, wb;
                copyRingFrom(*sa, cursorA, n, wa);
                copyRingFrom(*sb, cursorB, n, wb);
                cursorA += n;
                cursorB += n;
                if (wa.size() != n || wb.size() != n) continue;
                auto combined = SdrplayDiversity::combine(wa, wb, cfg);
                if (combined.empty()) continue;

                // Mirror source rate/center onto composite for spectrum consumers.
                {
                    std::lock_guard<std::mutex> qa(sa->queueMutex);
                    std::lock_guard<std::mutex> qb(sb->queueMutex);
                    std::lock_guard<std::mutex> qc(st.queueMutex);
                    st.currentRate = std::min(sa->currentRate, sb->currentRate);
                    if (st.currentRate <= 0) st.currentRate = 2e6;
                    st.currentCenter = sa->currentCenter;
                }
                appendIQBlock(index, st, std::move(combined));

                auto now = std::chrono::steady_clock::now();
                if (now - lastSpectrumTime > std::chrono::milliseconds(50)) {
                    lastSpectrumTime = now;
                    auto window = getRecentIQWindow(index, st.spectrumBins);
                    if (window.size() >= 64) {
                        auto power = computeRealFFTPower(window, st.spectrumBins, true);
                        std::lock_guard<std::mutex> lk(st.queueMutex);
                        st.latestPower = power;
                        if (st.spectrumAvg.size() != power.size()) {
                            st.spectrumAvg = power;
                            st.spectrumPeak = power;
                        } else {
                            for (size_t i = 0; i < power.size(); ++i) {
                                st.spectrumAvg[i] = 0.85f * st.spectrumAvg[i] + 0.15f * power[i];
                                st.spectrumPeak[i] = std::max(st.spectrumPeak[i] * 0.995f, power[i]);
                            }
                        }
                    }
                }
            }
            return;
        }
    }

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
            auto lastReadErrorLogTime = std::chrono::steady_clock::now() - std::chrono::seconds(10);
            int consecutiveReadTimeouts = 0;
            int consecutiveReadErrors = 0;
            while (!st.stopFlag && st.sessionGen.load(std::memory_order_acquire) == myGen) {
                const uint64_t requestedTuneSeq = st.centerTuneRequestSeq.load(std::memory_order_acquire);
                const uint64_t appliedTuneSeq = st.centerTuneAppliedSeq.load(std::memory_order_acquire);
                const uint64_t failedTuneSeq = st.centerTuneFailedSeq.load(std::memory_order_acquire);
                if (requestedTuneSeq > std::max(appliedTuneSeq, failedTuneSeq)) {
                    double logicalCenter = 0.0;
                    {
                        std::lock_guard<std::mutex> lk(st.queueMutex);
                        logicalCenter = st.currentCenter;
                    }

                    double ppm = 0.0;
                    bool nativePpm = false;
                    size_t rxCh = 0;
                    {
                        std::lock_guard<std::mutex> lk(st.stateMutex);
                        ppm = st.frequencyCorrectionPpm;
                        nativePpm = st.nativeFrequencyCorrectionActive;
                    }
                    {
                        std::lock_guard<std::mutex> lk(devicesMutex);
                        if (index < devices.size() && devices[index].isSdrplay)
                            rxCh = devices[index].rxChannel;
                    }

                    if (std::isfinite(logicalCenter) && logicalCenter > 0.0) {
                        const double tuneHz = nativePpm ? logicalCenter : correctedTuneFrequencyHz(logicalCenter, ppm);
                        const auto tuneStart = std::chrono::steady_clock::now();
                        try {
                            std::lock_guard<std::mutex> soapyLiveLock(gSoapyLiveIoMutex);
                            dev->setFrequency(SOAPY_SDR_RX, rxCh, tuneHz);
                            markStreamRetune(st, logicalCenter);
                            st.centerTuneAppliedSeq.store(requestedTuneSeq, std::memory_order_release);
                            lastSpectrumTime = std::chrono::steady_clock::now();
                            const auto tuneMs = std::chrono::duration_cast<std::chrono::milliseconds>(lastSpectrumTime - tuneStart).count();
                            if (tuneMs > 75) {
                                spdlog::warn("Applied queued center freq {} for device {} in {} ms (hardware tune {}, ppm {}). UI thread was not blocked.", logicalCenter, index, tuneMs, tuneHz, ppm);
                            } else {
                                spdlog::debug("Applied queued center freq {} for device {} (hardware tune {}, ppm {}, seq {})", logicalCenter, index, tuneHz, ppm, requestedTuneSeq);
                            }
                        } catch (const std::exception& ex) {
                            st.centerTuneFailedSeq.store(requestedTuneSeq, std::memory_order_release);
                            spdlog::warn("Queued center retune failed for device {} to {} Hz: {}", index, logicalCenter, ex.what());
                        } catch (...) {
                            st.centerTuneFailedSeq.store(requestedTuneSeq, std::memory_order_release);
                            spdlog::warn("Queued center retune failed for device {} to {} Hz with unknown native error.", index, logicalCenter);
                        }
                    } else {
                        st.centerTuneFailedSeq.store(requestedTuneSeq, std::memory_order_release);
                        spdlog::warn("Ignoring invalid queued center freq {} for device {} (seq {})", logicalCenter, index, requestedTuneSeq);
                    }
                }

                int flags = 0;
                long long timeNs = 0;
                void* buffs[] = { buff.data() };
                int numElems = 0;
                {
                    std::lock_guard<std::mutex> soapyLiveLock(gSoapyLiveIoMutex);
                    numElems = dev->readStream(stream, buffs, blockSize, flags, timeNs, 100000);
                    // A mode switch holds the same lock and invalidates the ring.
                    // Publish before releasing it so pre-switch IQ cannot enter
                    // the new epoch after resetStreamBuffers().
                    if (numElems > 0) {
                        const auto count = std::min(static_cast<size_t>(numElems), blockSize);
                        appendIQBlock(index, st, std::vector<std::complex<float>>(buff.begin(), buff.begin() + count));
                    }
                }
                if (numElems < 0) {
                    ++consecutiveReadErrors;
                    const auto errorNow = std::chrono::steady_clock::now();
                    const auto msSinceReadErrorLog = std::chrono::duration_cast<std::chrono::milliseconds>(
                        errorNow - lastReadErrorLogTime).count();
                    if (numElems == -1) { // SOAPY_SDR_TIMEOUT: common during retune/stop or USB stalls.
                        ++consecutiveReadTimeouts;
                        if (consecutiveReadTimeouts == 1 || msSinceReadErrorLog >= 5000) {
                            lastReadErrorLogTime = errorNow;
                            if (consecutiveReadTimeouts >= 50) {
                                spdlog::warn("readStream timeout on device {} repeated {} times; keeping stream alive", index, consecutiveReadTimeouts);
                            } else {
                                spdlog::debug("readStream timeout on device {} (code -1); keeping stream alive", index);
                            }
                        }
                    } else {
                        consecutiveReadTimeouts = 0;
                        if (consecutiveReadErrors == 1 || msSinceReadErrorLog >= 1000) {
                            lastReadErrorLogTime = errorNow;
                            spdlog::warn("readStream error code on device {}: {}", index, numElems);
                        }
                    }
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
                    consecutiveReadTimeouts = 0;
                    consecutiveReadErrors = 0;
                    if (numRead > blockSize) numRead = blockSize;

                    // === State-of-the-art spectrum pipeline (P1 audit + this stabilization) ===
                    // - Real radix-2 FFT, selectable 4K/8K/16K/64K bins
                    // - Hann + Blackman-Harris windows (Blackman-Harris for main viz)
                    // - Window taken from high-quality per-device ring (overlap friendly via ring)
                    // - Exponential averaging + peak hold (slow decay) maintained in StreamState
                    // - Throttled (~16-30 Hz) in RX thread to keep readStream lean (future: can move to dedicated spectrum worker thread)
                    // - Published as high-res latestPower so SpectrumWidget can do true-resolution zoomed waterfall from source history.
                    auto now = std::chrono::steady_clock::now();
                    // 80 ms: keep Soapy readStream + P25 ring pulls ahead of FFT.
                    // try_lock: voice worker memcpy wins if the ring is busy.
                    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastSpectrumTime).count() > 80) {
                        size_t fftN = 8192;
                        {
                            std::lock_guard<std::mutex> lk(st.queueMutex);
                            fftN = st.spectrumBins;
                        }
                        std::vector<float> localPower;

                        std::vector<std::complex<float>> samples;
                        {
                            std::unique_lock<std::mutex> ringLock(st.ringMutex, std::try_to_lock);
                            if (ringLock.owns_lock()) {
                                const uint64_t total = st.totalSamplesWritten.load(std::memory_order_acquire);
                                const size_t cap = st.ringCapacity;
                                if (cap > 0 && total >= fftN) {
                                    samples.resize(fftN);
                                    const uint64_t start = total - fftN;
                                    const bool powerOfTwoCap = (cap & (cap - 1)) == 0;
                                    const size_t startIdx = powerOfTwoCap
                                        ? static_cast<size_t>(start) & (cap - 1)
                                        : static_cast<size_t>(start % static_cast<uint64_t>(cap));
                                    const size_t firstPart = std::min(fftN, cap - startIdx);
                                    std::copy(st.iqRing.begin() + static_cast<std::ptrdiff_t>(startIdx),
                                              st.iqRing.begin() + static_cast<std::ptrdiff_t>(startIdx + firstPart),
                                              samples.begin());
                                    if (firstPart < fftN) {
                                        std::copy(st.iqRing.begin(),
                                                  st.iqRing.begin() + static_cast<std::ptrdiff_t>(fftN - firstPart),
                                                  samples.begin() + static_cast<std::ptrdiff_t>(firstPart));
                                    }
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
            std::lock_guard<std::mutex> soapyLiveLock(gSoapyLiveIoMutex);
            if (stream && dev) {
                dev->deactivateStream(stream);
                dev->closeStream(stream);
            }
            if (dev) SoapySDR::Device::unmake(dev);
        } catch (const std::exception& ex) {
            spdlog::warn("Recoverable native issue while cleaning faulted Soapy device {}: {}", index, ex.what());
        } catch (...) {
            spdlog::warn("Recoverable non-standard native issue while cleaning faulted Soapy device {}", index);
        }
        resetStreamBuffers(st);
    }
#endif

    // Stub/no-hardware fallback. Keep the original stable start/handoff behavior, but do
    // NOT draw a fake moving RF carrier. If the real SDR does not open, the waterfall must
    // look flat and logs must say stub/no-hardware instead of hiding the problem.
    double stubFs = 0.0;
    double stubCenterHz = 0.0;
    {
        std::lock_guard<std::mutex> lk(st.queueMutex);
        stubFs = (std::isfinite(st.currentRate) && st.currentRate > 0.0) ? st.currentRate : 2.4e6;
        stubCenterHz = (std::isfinite(st.currentCenter) && st.currentCenter > 0.0) ? st.currentCenter : 100e6;
    }
    spdlog::warn("Device {} is in STUB/no-hardware IQ mode: synthetic carrier disabled; no real RF is being displayed until Soapy hardware opens.", index);
    auto nextStubBlockTime = std::chrono::steady_clock::now();
    uint64_t stubBlocks = 0;
    while (!st.stopFlag && st.sessionGen.load(std::memory_order_acquire) == myGen) {
        {
            std::lock_guard<std::mutex> lk(st.queueMutex);
            if (std::isfinite(st.currentRate) && st.currentRate > 0.0) stubFs = st.currentRate;
            if (std::isfinite(st.currentCenter) && st.currentCenter > 0.0) stubCenterHz = st.currentCenter;
        }
        const auto stubBlockPeriod = std::chrono::duration<double>((double)blockSize / std::max(1.0, stubFs));
        std::vector<std::complex<float>> block(blockSize);
        for (size_t i = 0; i < blockSize; ++i) {
            const float re = (rand() % 1000 - 500) * 0.00005f;
            const float im = (rand() % 1000 - 500) * 0.00005f;
            block[i] = {re, im};
        }
        appendIQBlock(index, st, std::move(block));

        {
            std::lock_guard<std::mutex> lk(st.queueMutex);
            const size_t bins = std::max<size_t>(64, st.spectrumBins);
            st.latestPower.assign(bins, -120.0f);
            st.spectrumAvg.assign(bins, -120.0f);
            st.spectrumPeak.assign(bins, -120.0f);
            st.currentRate = stubFs;
            st.currentCenter = stubCenterHz;
        }

        if ((++stubBlocks % 300) == 0) {
            spdlog::warn("Device {} still in STUB/no-hardware IQ mode after {} blocks; check Soapy make/activate/readStream logs.", index, stubBlocks);
        }
        nextStubBlockTime += std::chrono::duration_cast<std::chrono::steady_clock::duration>(stubBlockPeriod);
        auto now = std::chrono::steady_clock::now();
        if (nextStubBlockTime > now) {
            std::this_thread::sleep_until(nextStubBlockTime);
        } else if (now - nextStubBlockTime > std::chrono::milliseconds(100)) {
            nextStubBlockTime = now;
        }
    }
}

// ---------------------------------------------------------------------------
// Sprint 1: TX tone stream + optional CF32 dump (hardware optional)
// ---------------------------------------------------------------------------

bool DeviceManager::startToneTx(size_t index, const TxParams& params)
{
    {
        std::lock_guard<std::mutex> lk(devicesMutex);
        if (index >= devices.size()) {
            spdlog::error("startToneTx: invalid device index {}", index);
            return false;
        }
    }

    TxParams p = params;
    if (!std::isfinite(p.sampleRate) || p.sampleRate < 1e5) p.sampleRate = 2.0e6;
    if (!std::isfinite(p.toneHz) || p.toneHz < 0.0) p.toneHz = 1000.0;
    if (!std::isfinite(p.amplitude) || p.amplitude <= 0.0) p.amplitude = 0.25;
    p.amplitude = std::min(1.0, std::max(0.01, p.amplitude));
    if (!std::isfinite(p.centerHz) || p.centerHz <= 0.0) {
        // Default: keep current RX center if streaming, else 0 (file-only ok).
        if (auto* rx = streamState(index)) {
            std::lock_guard<std::mutex> lk(rx->stateMutex);
            if (rx->currentCenter > 0.0) p.centerHz = rx->currentCenter;
        }
    }

    const bool wantDump = !p.dumpPath.empty();
    bool deviceCanTx = false;
    std::string driver;
    {
        std::lock_guard<std::mutex> lk(devicesMutex);
        if (index < devices.size()) {
            deviceCanTx = devices[index].canTx;
            driver = devices[index].driver;
            if (driver == "hackrf" || driver == "plutosdr" || driver == "lime" ||
                driver == "uhd" || driver == "bladerf") {
                deviceCanTx = true;
            }
            if (driver == "rtlsdr") deviceCanTx = false;
        }
    }

    stopTx(index);
    ensureTxStreamSlot(index);
    auto* txPtr = txStreamState(index);
    if (!txPtr) return false;
    auto& tx = *txPtr;

    std::unique_lock<std::mutex> life(tx.lifecycleMutex);
    tx.params = p;
    tx.stopFlag = false;
    tx.samplesWritten.store(0, std::memory_order_relaxed);
    tx.hardwareActive.store(false, std::memory_order_relaxed);
    tx.runtimeState = "starting";

    bool hardwareOk = false;
#ifdef HAVE_SOAPYSDR
    if (p.attemptHardware && deviceCanTx) {
        try {
            std::map<std::string, std::string> args;
            {
                std::lock_guard<std::mutex> lk(devicesMutex);
                if (index < devices.size()) {
                    args["driver"] = devices[index].driver;
                    if (!devices[index].serial.empty()) args["serial"] = devices[index].serial;
                }
            }
            std::lock_guard<std::mutex> soapyLiveLock(gSoapyLiveIoMutex);
            SoapySDR::Device* dev = SoapySDR::Device::make(args);
            if (!dev) throw std::runtime_error("Soapy Device::make returned null for TX");

            const double useRate = p.sampleRate;
            try { dev->setSampleRate(SOAPY_SDR_TX, 0, useRate); } catch (...) {}
            if (p.centerHz > 0.0) {
                try { dev->setFrequency(SOAPY_SDR_TX, 0, p.centerHz); } catch (const std::exception& ex) {
                    spdlog::warn("TX setFrequency failed: {}", ex.what());
                }
            }
            try {
                if (!devices.empty()) {
                    // Prefer TX antenna when listed
                    auto ants = dev->listAntennas(SOAPY_SDR_TX, 0);
                    if (!ants.empty()) {
                        try { dev->setAntenna(SOAPY_SDR_TX, 0, ants.front()); } catch (...) {}
                    }
                }
            } catch (...) {}
            try {
                dev->setGainMode(SOAPY_SDR_TX, 0, false);
                dev->setGain(SOAPY_SDR_TX, 0, p.gainDb);
            } catch (...) {
                try { dev->setGain(SOAPY_SDR_TX, 0, p.gainDb); } catch (...) {}
            }

            SoapySDR::Stream* stream = dev->setupStream(SOAPY_SDR_TX, "CF32");
            if (!stream) {
                SoapySDR::Device::unmake(dev);
                throw std::runtime_error("setupStream(TX) returned null");
            }
            const int act = dev->activateStream(stream);
            if (act != 0) {
                dev->closeStream(stream);
                SoapySDR::Device::unmake(dev);
                throw std::runtime_error("activateStream(TX) failed code=" + std::to_string(act));
            }
            tx.soapyDev = dev;
            tx.txStream = stream;
            hardwareOk = true;
            tx.hardwareActive.store(true, std::memory_order_release);
            spdlog::info("TX hardware stream open on device {} @ {:.6f} MHz sr={:.3f} Msps gain={:.1f} dB tone={:.0f} Hz",
                         index, p.centerHz / 1e6, useRate / 1e6, p.gainDb, p.toneHz);
        } catch (const std::exception& ex) {
            spdlog::warn("startToneTx hardware path failed on device {}: {}", index, ex.what());
            hardwareOk = false;
            tx.soapyDev = nullptr;
            tx.txStream = nullptr;
            tx.hardwareActive.store(false, std::memory_order_release);
        }
    }
#else
    (void)deviceCanTx;
    (void)driver;
#endif

    if (!hardwareOk && !wantDump && !p.allowFileOnlyFallback) {
        tx.runtimeState = "failed";
        spdlog::error("startToneTx: no hardware TX and no dump path");
        return false;
    }
    if (!hardwareOk && !wantDump) {
        // Null sink still runs so SM/CLI can exercise the path (logs only).
        tx.runtimeState = "null-sink";
        spdlog::info("startToneTx: running null sink on device {} (no hardware, no dump path)", index);
    } else if (!hardwareOk && wantDump) {
        tx.runtimeState = "file-only";
    } else if (hardwareOk && wantDump) {
        tx.runtimeState = "hardware+file";
    } else {
        tx.runtimeState = "hardware";
    }

    const uint64_t gen = tx.sessionGen.fetch_add(1, std::memory_order_acq_rel) + 1;
    tx.active.store(true, std::memory_order_release);
    tx.txThread = std::thread(&DeviceManager::txThreadFunc, this, index, gen);
    return true;
}

void DeviceManager::stopTx(size_t index)
{
    auto* txPtr = txStreamState(index);
    if (!txPtr) return;
    auto& tx = *txPtr;
    std::unique_lock<std::mutex> life(tx.lifecycleMutex);
    if (!tx.active.load(std::memory_order_acquire) && !tx.txThread.joinable()) {
#ifdef HAVE_SOAPYSDR
        if (!tx.soapyDev) return;
#else
        return;
#endif
    }

    tx.sessionGen.fetch_add(1, std::memory_order_acq_rel);
    tx.stopFlag.store(true, std::memory_order_release);

    if (tx.txThread.joinable()) {
        auto start = std::chrono::steady_clock::now();
        while (tx.threadRunning.load(std::memory_order_acquire)) {
            if (std::chrono::steady_clock::now() - start > std::chrono::milliseconds(500)) {
                spdlog::warn("txThread device {} still running - detaching", index);
                try { tx.txThread.detach(); } catch (...) {}
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
        }
        if (tx.txThread.joinable()) {
            try { tx.txThread.join(); } catch (...) {
                try { tx.txThread.detach(); } catch (...) {}
            }
        }
    }

#ifdef HAVE_SOAPYSDR
    SoapySDR::Device* dev = nullptr;
    SoapySDR::Stream* stream = nullptr;
    {
        dev = tx.soapyDev;
        stream = tx.txStream;
        tx.soapyDev = nullptr;
        tx.txStream = nullptr;
    }
    try {
        if (dev && stream) {
            std::lock_guard<std::mutex> soapyLiveLock(gSoapyLiveIoMutex);
            try { dev->deactivateStream(stream); } catch (...) {}
            try { dev->closeStream(stream); } catch (...) {}
            SoapySDR::Device::unmake(dev);
        } else if (dev) {
            std::lock_guard<std::mutex> soapyLiveLock(gSoapyLiveIoMutex);
            SoapySDR::Device::unmake(dev);
        }
    } catch (const std::exception& ex) {
        spdlog::warn("stopTx Soapy teardown: {}", ex.what());
    }
#endif
    tx.hardwareActive.store(false, std::memory_order_release);
    tx.active.store(false, std::memory_order_release);
    tx.runtimeState = "idle";
    spdlog::info("TX stopped on device {} (samples written={})",
                 index, tx.samplesWritten.load(std::memory_order_relaxed));
}

void DeviceManager::stopAllTx()
{
    size_t n = 0;
    {
        std::lock_guard<std::mutex> lk(txStreamsMutex);
        n = txStreams.size();
    }
    for (size_t i = 0; i < n; ++i) stopTx(i);
}

bool DeviceManager::isTransmitting(size_t index) const
{
    const auto* tx = txStreamState(index);
    return tx && tx->active.load(std::memory_order_acquire);
}

bool DeviceManager::isHardwareTxActive(size_t index) const
{
    const auto* tx = txStreamState(index);
    return tx && tx->hardwareActive.load(std::memory_order_acquire);
}

std::string DeviceManager::getTxRuntimeState(size_t index) const
{
    const auto* tx = txStreamState(index);
    if (!tx) return "none";
    std::lock_guard<std::mutex> life(const_cast<TxStreamState*>(tx)->lifecycleMutex);
    return tx->runtimeState;
}

uint64_t DeviceManager::getTxSamplesWritten(size_t index) const
{
    const auto* tx = txStreamState(index);
    return tx ? tx->samplesWritten.load(std::memory_order_relaxed) : 0;
}

void DeviceManager::txThreadFunc(size_t index, uint64_t expectedGeneration)
{
    auto* txPtr = txStreamState(index);
    if (!txPtr) return;
    auto& tx = *txPtr;
    tx.threadRunning.store(true, std::memory_order_release);

    TxParams p;
    {
        std::lock_guard<std::mutex> life(tx.lifecycleMutex);
        p = tx.params;
    }

    const double sr = p.sampleRate > 0.0 ? p.sampleRate : 2.0e6;
    const double tone = p.toneHz;
    const float amp = static_cast<float>(p.amplitude);
    const double twoPi = 6.28318530717958647692;
    double phase = 0.0;
    const double dphi = twoPi * tone / sr;

    // ~5 ms blocks for low latency / smooth underrun margin
    const size_t blockSize = std::max<size_t>(256, static_cast<size_t>(sr * 0.005));
    std::vector<std::complex<float>> block(blockSize);

    std::ofstream dump;
    if (!p.dumpPath.empty()) {
        dump.open(p.dumpPath, std::ios::binary | std::ios::trunc);
        if (!dump) {
            spdlog::warn("TX dump open failed: {}", p.dumpPath);
        } else {
            spdlog::info("TX CF32 dump -> {}", p.dumpPath);
        }
    }

    auto nextDeadline = std::chrono::steady_clock::now();
    const auto blockPeriod = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double>(static_cast<double>(blockSize) / sr));

    while (!tx.stopFlag.load(std::memory_order_acquire)) {
        if (tx.sessionGen.load(std::memory_order_acquire) != expectedGeneration) break;

        for (size_t i = 0; i < blockSize; ++i) {
            const float c = static_cast<float>(std::cos(phase));
            const float s = static_cast<float>(std::sin(phase));
            block[i] = {amp * c, amp * s};
            phase += dphi;
            if (phase > twoPi) phase -= twoPi;
        }

        if (dump) {
            dump.write(reinterpret_cast<const char*>(block.data()),
                       static_cast<std::streamsize>(block.size() * sizeof(std::complex<float>)));
        }

#ifdef HAVE_SOAPYSDR
        if (tx.hardwareActive.load(std::memory_order_acquire) && tx.soapyDev && tx.txStream) {
            try {
                std::lock_guard<std::mutex> soapyLiveLock(gSoapyLiveIoMutex);
                void* buffs[] = { block.data() };
                int flags = 0;
                long long timeNs = 0;
                const int wrote = tx.soapyDev->writeStream(tx.txStream, buffs, blockSize, flags, timeNs, 100000);
                if (wrote < 0) {
                    spdlog::warn("writeStream error {} on device {}", wrote, index);
                    // keep trying unless stop
                }
            } catch (const std::exception& ex) {
                spdlog::warn("writeStream exception device {}: {}", index, ex.what());
            }
        }
#endif

        tx.samplesWritten.fetch_add(static_cast<uint64_t>(blockSize), std::memory_order_relaxed);

        nextDeadline += blockPeriod;
        auto now = std::chrono::steady_clock::now();
        if (nextDeadline > now) {
            std::this_thread::sleep_until(nextDeadline);
        } else if (now - nextDeadline > std::chrono::milliseconds(50)) {
            nextDeadline = now;
        }
    }

    if (dump) dump.close();
    tx.threadRunning.store(false, std::memory_order_release);
}
