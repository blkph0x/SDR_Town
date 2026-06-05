#include "DeviceManager.h"

#include <spdlog/spdlog.h>
#include <fstream>
#include <QStandardPaths>
#include <QDir>

#ifdef HAVE_SOAPYSDR
#include <SoapySDR/Logger.hpp>
#endif

DeviceManager& DeviceManager::instance() {
    static DeviceManager mgr;
    return mgr;
}

std::vector<DeviceInfo> DeviceManager::enumerateDevices() {
    devices.clear();

#ifdef HAVE_SOAPYSDR
    try {
        SoapySDR::setLogLevel(SOAPY_SDR_INFO); // reduce noise
        auto results = SoapySDR::Device::enumerate();
        spdlog::info("SoapySDR enumerate returned {} device(s)", results.size());

        for (const auto& result : results) {
            DeviceInfo di;
            di.driver = result.count("driver") ? result.at("driver") : "unknown";
            di.label = result.count("label") ? result.at("label") : di.driver;
            di.serial = result.count("serial") ? result.at("serial") : "";
            di.hardware = result.count("hardware") ? result.at("hardware") : "";

            // Try to open to query capabilities (gains, antennas, rates). This can be slow for some devices.
            try {
                auto dev = SoapySDR::Device::make(result);
                if (dev) {
                    // antennas
                    auto ants = dev->listAntennas(SOAPY_SDR_RX, 0);
                    di.antennas.assign(ants.begin(), ants.end());
                    if (!di.antennas.empty()) di.antenna = di.antennas[0];

                    // sample rates (get a few)
                    auto rates = dev->listSampleRates(SOAPY_SDR_RX, 0);
                    // store a reasonable subset or first few
                    for (size_t i = 0; i < rates.size() && i < 8; ++i) {
                        di.sampleRates.push_back(rates[i]);
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
                        auto gr = dev->getGainRange(SOAPY_SDR_RX, 0, gains[0]);
                        di.gain = gr.maximum() * 0.6; // sensible starting point
                    }

                    SoapySDR::Device::unmake(dev);
                }
            } catch (const std::exception& ex) {
                spdlog::warn("Could not fully probe device {}: {}", di.label, ex.what());
            }

            devices.push_back(di);
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

    // After enumerate, try to overlay saved settings
    loadSettings();

    return devices;
}

bool DeviceManager::setEnabled(size_t index, bool enabled) {
    if (index >= devices.size()) return false;
    devices[index].enabled = enabled;
    spdlog::info("Device {} '{}': {}", index, devices[index].label, enabled ? "ENABLED" : "disabled");
    saveSettings();
    return true;
}

DeviceInfo* DeviceManager::getDevice(size_t index) {
    if (index >= devices.size()) return nullptr;
    return &devices[index];
}

void DeviceManager::updateDeviceParams(size_t index, double sampleRate, double gain, const std::string& antenna) {
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
                break;
            }
        }
    }
}