#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

struct SdrplayPortCapabilities {
    std::string driverName;
    std::string displayName;
    std::string connector;
    std::vector<std::string> aliases;
    double minFrequencyHz = 0.0;
    double maxFrequencyHz = 0.0;
    bool biasTAllowed = false;
    bool highImpedance = false;
};

struct SdrplayModelCapabilities {
    std::string model;
    std::vector<SdrplayPortCapabilities> ports;
    bool hardwareApiSupported = true;
    bool websocketOnly = false;
    int tunerCount = 1;
};

struct SdrplayCapabilities {
    bool isSdrplay = false;
    // True only when the driver returned at least one concrete capability.
    // Static model knowledge must never be confused with successful device I/O.
    bool probeVerified = false;
    bool hasAgc = false;
    bool hasIfgr = false;
    bool hasRfgr = false;
    std::string model;
    std::vector<std::string> gainElements;
    std::vector<std::string> antennas;
    std::vector<double> bandwidthsHz;
    std::vector<std::string> settingKeys;
    std::map<std::string, std::vector<std::string>> settingOptions;
};

struct SdrplaySettings {
    bool agcEnabled = false;
    double ifgrDb = 40.0;
    double rfgrDb = 4.0;
    double bandwidthHz = 0.0;
    std::map<std::string, std::string> soapySettings;
    std::string duoMode;
    size_t rxChannel = 0;

    static const char* kIqCorr;
    static const char* kAgcSetpoint;
    static const char* kBiasT;
    static const char* kRfNotch;
    static const char* kDabNotch;
    static const char* kExtRef;
    static const char* kHdr;
    static const char* kRfGainSel;
};

namespace SdrplayProfile {

bool isSdrplayDriver(const std::string& driver);
std::string normalizeModel(const std::string& hardware, const std::string& label);
std::string duoModeFromKwargs(const std::map<std::string, std::string>& kwargs);
std::string duoModeDisplayName(const std::string& mode);

SdrplayCapabilities capabilitiesFromProbe(
    const std::string& driver,
    const std::string& hardware,
    const std::string& label,
    const std::vector<std::string>& gainElements,
    const std::vector<std::string>& antennas,
    const std::vector<double>& bandwidthsHz,
    const std::vector<std::string>& settingKeys,
    const std::map<std::string, std::vector<std::string>>& settingOptions);

SdrplaySettings defaultSettings(const SdrplayCapabilities& caps);
bool settingSupported(const SdrplayCapabilities& caps, const std::string& key);
const std::vector<std::string>& knownSettingKeys();
std::string boolSetting(bool on);
bool parseBoolSetting(const std::string& value, bool fallback = false);

// Static physical model data.  These helpers describe connector/range safety;
// live controls are still exposed only when the installed driver reports them.
SdrplayModelCapabilities modelCapabilities(const std::string& model);
std::optional<SdrplayPortCapabilities> antennaCapabilities(
    const std::string& model, const std::string& antenna);
bool frequencyAllowedForAntenna(
    const std::string& model, const std::string& antenna, double frequencyHz);
std::string antennaFrequencyError(
    const std::string& model, const std::string& antenna, double frequencyHz);
bool usesHardwareApi(const std::string& model);
bool usesWebsocketApi(const std::string& model);
std::string backendDescription(const std::string& model);

// Model/port safety and UI helpers.
std::string antennaPortDescription(const std::string& model, const std::string& antenna);
bool biasTAllowedForAntenna(const std::string& model, const std::string& antenna);
double maxSampleRateHz(const std::string& duoMode);
double clampSampleRateHz(const std::string& duoMode, double requestedHz);
std::string rfNotchUiLabel();
std::string rfNotchUiTooltip();
std::string extRefUiLabel(const std::string& model);
std::string extRefUiTooltip(const std::string& model);

// Ordered Windows candidates. Runtime validates load/architecture/registration.
std::vector<std::string> windowsApiCandidates(const std::string& appDir);
std::vector<std::string> windowsSoapyModuleCandidates(const std::string& appDir);

} // namespace SdrplayProfile
