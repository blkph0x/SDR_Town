#pragma once

#include <vector>
#include <string>
#include <optional>
#include <nlohmann/json.hpp>

#ifdef HAVE_SOAPYSDR
#include <SoapySDR/Device.hpp>
#include <SoapySDR/Types.hpp>
#endif

struct DeviceInfo {
    std::string driver;           // e.g. "hackrf", "rtlsdr"
    std::string label;            // friendly name
    std::string serial;           // serial or unique id (important for multiple HackRFs)
    std::string hardware;         // hardware key
    std::vector<std::string> antennas;
    std::vector<double> sampleRates; // some common or full range later
    double minFreq = 0;
    double maxFreq = 0;
    // runtime
    bool enabled = false;
    double sampleRate = 2.4e6;
    double gain = 30.0;           // simplified master gain for now
    std::string antenna;
    // more per-device settings can be added
};

class DeviceManager {
public:
    static DeviceManager& instance();

    // Enumerate using SoapySDR (or stub)
    std::vector<DeviceInfo> enumerateDevices();

    // Activate / deactivate (for now just toggle flag + future stream start)
    bool setEnabled(size_t index, bool enabled);

    DeviceInfo* getDevice(size_t index);
    const std::vector<DeviceInfo>& getDevices() const { return devices; }

    // Persistence (JSON in AppData)
    void loadSettings();
    void saveSettings() const;

    // Update a device's runtime params (gain, rate, antenna)
    void updateDeviceParams(size_t index, double sampleRate, double gain, const std::string& antenna);

private:
    DeviceManager() = default;
    std::vector<DeviceInfo> devices;

    nlohmann::json toJson() const;
    void fromJson(const nlohmann::json& j);
};