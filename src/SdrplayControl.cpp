#include "SdrplayControl.h"
#include "DeviceManager.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace SdrplayControl {
namespace {
template<class T> bool contains(const std::vector<T>& values, const T& value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
bool supported(const DeviceInfo& d, const std::string& key) {
    return contains(d.sdrplaySettingKeys, key);
}
void syncRfSelection(DeviceInfo& d) {
    if (supported(d, SdrplaySettings::kRfGainSel))
        d.soapySettings[SdrplaySettings::kRfGainSel] = std::to_string(static_cast<int>(d.rfgrDb));
}
std::string booleanValue(const std::string& value) {
    if (value == "true" || value == "1" || value == "on") return "true";
    if (value == "false" || value == "0" || value == "off") return "false";
    throw std::runtime_error("Expected true or false");
}
}

void mergeCapabilities(const DeviceInfo& p, DeviceInfo& d) {
    d.hardware = p.hardware;
    d.sdrplayModel = p.sdrplayModel;
    d.antennas = p.antennas;
    d.gainElements = p.gainElements;
    d.gainName = p.gainName;
    d.gainMin = p.gainMin;
    d.gainMax = p.gainMax;
    d.ifgrMin = p.ifgrMin;
    d.ifgrMax = p.ifgrMax;
    d.bandwidthsHz = p.bandwidthsHz;
    d.sdrplaySettingKeys = p.sdrplaySettingKeys;
    d.sdrplaySettingOptions = p.sdrplaySettingOptions;
    d.sdrplayHasAgc = p.sdrplayHasAgc;
    d.sdrplayProbed = p.sdrplayProbed;
    if (!contains(d.antennas, d.antenna)) d.antenna = p.antenna;
    d.ifgrDb = std::clamp(d.ifgrDb, d.ifgrMin, d.ifgrMax);
    d.rfgrDb = std::clamp(d.rfgrDb, d.gainMin, d.gainMax);
    d.gain = d.rfgrDb;
    if (!d.sdrplayHasAgc) d.agcEnabled = false;
    auto caps = SdrplayProfile::capabilitiesFromProbe(d.driver, d.hardware, d.label,
        d.gainElements, d.antennas, d.bandwidthsHz, d.sdrplaySettingKeys, d.sdrplaySettingOptions);
    for (const auto& setting : SdrplayProfile::defaultSettings(caps).soapySettings)
        d.soapySettings.emplace(setting);
    for (auto it = d.soapySettings.begin(); it != d.soapySettings.end();) {
        if (!supported(d, it->first)) it = d.soapySettings.erase(it);
        else ++it;
    }
    if (supported(d, SdrplaySettings::kBiasT) &&
        !SdrplayProfile::biasTAllowedForAntenna(d.sdrplayModel, d.antenna))
        d.soapySettings[SdrplaySettings::kBiasT] = "false";
    syncRfSelection(d);
}

void prepare(DeviceInfo& d, const Change& c) {
    require(d.isSdrplay && d.sdrplayProbed, "Open the SDRplay receiver to discover its controls first");
    require(!d.isDiversityComposite, "Select a physical SDRplay tuner, not the diversity output");
    // The upstream API's unchannelled settings use chParams. Do not silently
    // claim that they address tuner B in simultaneous dual-tuner mode.
    require(!(d.sdrplayDuoMode == "DT" && d.rxChannel != 0 && c.kind == Kind::Setting),
        "This Soapy module does not expose channel-scoped settings for RSPduo tuner B");
    switch (c.kind) {
    case Kind::Antenna:
        require(contains(d.antennas, c.value), "Antenna is not advertised by the driver: " + c.value);
        d.antenna = c.value;
        if (supported(d, SdrplaySettings::kBiasT) &&
            !SdrplayProfile::biasTAllowedForAntenna(d.sdrplayModel, d.antenna))
            d.soapySettings[SdrplaySettings::kBiasT] = "false";
        break;
    case Kind::Agc:
        require(d.sdrplayHasAgc, "AGC is not supported by this driver");
        d.agcEnabled = c.number != 0;
        break;
    case Kind::Gain: {
        require(contains(d.gainElements, c.key), "Gain control is not advertised: " + c.key);
        const bool rf = c.key == "RFGR";
        require(rf || c.key == "IFGR", "Unsupported SDRplay gain control");
        const double minimum = rf ? d.gainMin : d.ifgrMin;
        const double maximum = rf ? d.gainMax : d.ifgrMax;
        require(std::isfinite(c.number) && c.number >= minimum && c.number <= maximum &&
            std::floor(c.number) == c.number, "Gain must be an integer in the driver range");
        if (rf) {
            d.rfgrDb = d.gain = c.number;
            syncRfSelection(d);
        } else {
            d.ifgrDb = c.number;
            d.agcEnabled = false;
        }
        break;
    }
    case Kind::Bandwidth:
        require(std::isfinite(c.number) && c.number >= 0 &&
            (c.number == 0 || contains(d.bandwidthsHz, c.number)), "Bandwidth is not advertised by this driver");
        d.bandwidthHz = c.number;
        break;
    case Kind::Setting: {
        require(supported(d, c.key), "Setting is not advertised by this driver: " + c.key);
        std::string value = c.value;
        if (c.key == SdrplaySettings::kRfGainSel || c.key == SdrplaySettings::kAgcSetpoint) {
            size_t used = 0;
            const int number = std::stoi(value, &used);
            require(used == value.size(), "Invalid integer setting");
            if (c.key == SdrplaySettings::kAgcSetpoint)
                require(number >= -60 && number <= 0, "AGC setpoint must be -60 to 0 dBFS");
            else {
                require(number >= d.gainMin && number <= d.gainMax, "RF state outside driver range");
                const auto options = d.sdrplaySettingOptions.find(c.key);
                require(options == d.sdrplaySettingOptions.end() || contains(options->second, value),
                    "RF state is not advertised by this driver");
                d.gain = d.rfgrDb = number;
            }
            value = std::to_string(number);
        } else {
            require(contains(SdrplayProfile::knownSettingKeys(), c.key), "Unsupported SDRplay setting");
            value = booleanValue(value);
        }
        if (c.key == SdrplaySettings::kBiasT && value == "true")
            require(SdrplayProfile::biasTAllowedForAntenna(d.sdrplayModel, d.antenna),
                "Bias-T is unavailable on this antenna. RSPdx requires Antenna B.");
        d.soapySettings[c.key] = value;
        break;
    }
    }
}

#ifdef HAVE_SOAPYSDR
void probe(SoapySDR::Device& dev, DeviceInfo& d) {
    const auto ch = d.rxChannel;
    d.hardware = dev.getHardwareKey();
    d.sdrplayModel = SdrplayProfile::normalizeModel(d.hardware, d.label);
    d.antennas = dev.listAntennas(SOAPY_SDR_RX, ch);
    const auto current = dev.getAntenna(SOAPY_SDR_RX, ch);
    if (!contains(d.antennas, d.antenna)) {
        require(contains(d.antennas, current), "Driver returned no valid antenna input");
        d.antenna = current;
    }
    d.gainElements = dev.listGains(SOAPY_SDR_RX, ch);
    d.sdrplayHasAgc = dev.hasGainMode(SOAPY_SDR_RX, ch);
    d.bandwidthsHz = dev.listBandwidths(SOAPY_SDR_RX, ch);
    d.sdrplaySettingKeys.clear();
    d.sdrplaySettingOptions.clear();
    for (const auto& setting : dev.getSettingInfo()) {
        d.sdrplaySettingKeys.push_back(setting.key);
        if (!setting.options.empty()) d.sdrplaySettingOptions[setting.key] = setting.options;
    }
    if (contains(d.gainElements, std::string("RFGR"))) {
        const auto range = dev.getGainRange(SOAPY_SDR_RX, ch, "RFGR");
        d.gainName = "RFGR"; d.gainMin = range.minimum(); d.gainMax = range.maximum();
    }
    if (contains(d.gainElements, std::string("IFGR"))) {
        const auto range = dev.getGainRange(SOAPY_SDR_RX, ch, "IFGR");
        d.ifgrMin = range.minimum(); d.ifgrMax = range.maximum();
    }
    d.sdrplayProbed = true;
    const auto probed = d;
    mergeCapabilities(probed, d);
}

namespace {
void setting(SoapySDR::Device& dev, const std::string& key, const std::string& value) {
    dev.writeSetting(key, value);
    require(dev.readSetting(key) == value, "Driver readback did not confirm " + key + "=" + value);
}
void antenna(SoapySDR::Device& dev, DeviceInfo& d) {
    // Remove DC before switching away. Never infer electrical success from an
    // accepted call: this confirms driver state only, not measured pin voltage.
    if (supported(d, SdrplaySettings::kBiasT) &&
        !SdrplayProfile::biasTAllowedForAntenna(d.sdrplayModel, d.antenna))
        setting(dev, SdrplaySettings::kBiasT, "false");
    dev.setAntenna(SOAPY_SDR_RX, d.rxChannel, d.antenna);
    require(dev.getAntenna(SOAPY_SDR_RX, d.rxChannel) == d.antenna, "Antenna readback mismatch");
}
void gain(SoapySDR::Device& dev, DeviceInfo& d, const std::string& key) {
    const auto value = key == "RFGR" ? d.rfgrDb : d.ifgrDb;
    dev.setGain(SOAPY_SDR_RX, d.rxChannel, key, value);
    require(dev.getGain(SOAPY_SDR_RX, d.rxChannel, key) == value, "Gain readback mismatch: " + key);
}
void agc(SoapySDR::Device& dev, DeviceInfo& d) {
    dev.setGainMode(SOAPY_SDR_RX, d.rxChannel, d.agcEnabled);
    require(dev.getGainMode(SOAPY_SDR_RX, d.rxChannel) == d.agcEnabled, "AGC readback mismatch");
}
void bandwidth(SoapySDR::Device& dev, DeviceInfo& d) {
    // Upstream setBandwidth(0) means automatic selection from sample rate.
    dev.setBandwidth(SOAPY_SDR_RX, d.rxChannel, d.bandwidthHz);
    const double actual = dev.getBandwidth(SOAPY_SDR_RX, d.rxChannel);
    require(std::isfinite(actual) && actual > 0 &&
        (d.bandwidthHz == 0 || std::abs(actual - d.bandwidthHz) < 1), "Bandwidth readback mismatch");
}
}
void applyChange(SoapySDR::Device& dev, DeviceInfo& d, const Change& c) {
    switch (c.kind) {
    case Kind::Antenna: antenna(dev, d); break;
    case Kind::Agc:
        agc(dev, d);
        if (!d.agcEnabled) gain(dev, d, "IFGR");
        break;
    case Kind::Gain:
        if (c.key == "IFGR") agc(dev, d);
        gain(dev, d, c.key);
        break;
    case Kind::Bandwidth: bandwidth(dev, d); break;
    case Kind::Setting: setting(dev, c.key, d.soapySettings.at(c.key)); break;
    }
}
void apply(SoapySDR::Device& dev, DeviceInfo& d) {
    require(d.sdrplayProbed, "SDRplay controls were not probed");
    antenna(dev, d);
    bandwidth(dev, d);
    if (d.sdrplayHasAgc) agc(dev, d);
    if (!d.agcEnabled && contains(d.gainElements, std::string("IFGR"))) gain(dev, d, "IFGR");
    if (contains(d.gainElements, std::string("RFGR"))) gain(dev, d, "RFGR");
    for (const auto& [key, value] : d.soapySettings) {
        // RFGR and rfgain_sel are aliases of the same LNA state, not two gains.
        if (key == SdrplaySettings::kRfGainSel && contains(d.gainElements, std::string("RFGR"))) continue;
        setting(dev, key, value);
    }
}
#endif
}
