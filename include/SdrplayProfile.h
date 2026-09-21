#pragma once

#include <map>
#include <string>
#include <vector>

#ifdef _MSC_VER
// windowsSoapyRoots() reads the SDRplay API installer key via RegOpenKeyEx.
#pragma comment(lib, "Advapi32.lib")
#endif

// Model-aware SDRplay capability/settings layer for SoapySDRPlay3 (API 3.x).
// No native sdrplay_api linkage — capabilities come from Soapy probe results.

struct SdrplayCapabilities {
    bool isSdrplay = false;
    std::string model;              // RSP1, RSP1A, RSP1B, RSP2, RSPduo, RSPdx, RSPdx-R2, ...
    std::vector<std::string> gainElements; // typically IFGR, RFGR
    std::vector<std::string> antennas;
    std::vector<double> bandwidthsHz;
    std::vector<std::string> settingKeys;  // from getSettingInfo
    std::map<std::string, std::vector<std::string>> settingOptions; // key -> options
    bool hasAgc = true;
    bool hasIfgr = false;
    bool hasRfgr = false;
    double ifgrMin = 20.0;
    double ifgrMax = 59.0;
    double rfgrMin = 0.0;
    double rfgrMax = 27.0;
};

struct SdrplaySettings {
    bool agcEnabled = false;
    double ifgrDb = 40.0;           // IF gain reduction (higher = less gain)
    double rfgrDb = 4.0;            // RF gain reduction / LNA state as dB element
    double bandwidthHz = 0.0;       // 0 = leave driver default
    size_t rxChannel = 0;           // Dual Tuner channel 0/1
    std::string duoMode;            // ST, DT, MA, MA8, SL (from Soapy kwargs)
    std::map<std::string, std::string> soapySettings; // biasT_ctrl, hdr_ctrl, ...

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

// Build capability flags from probed Soapy lists (no live device required for unit tests).
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
std::string boolSetting(bool on);
bool parseBoolSetting(const std::string& value, bool fallback = false);

// Known SoapySDRPlay3 writeSetting keys (subset may be absent per model).
const std::vector<std::string>& knownSettingKeys();

// Hardware/port helpers for UI and safety (Soapy-reachable behaviour).
std::string antennaPortDescription(const std::string& model, const std::string& antenna);
bool biasTAllowedForAntenna(const std::string& model, const std::string& antenna);
double maxSampleRateHz(const std::string& duoMode); // DT => 2e6, else 10e6
double clampSampleRateHz(const std::string& duoMode, double requestedHz);
std::string rfNotchUiLabel(); // Soapy exposes one combined broadcast notch
std::string rfNotchUiTooltip();
// SoapySDRPlay3 extref_ctrl maps to API extRefOutputEn (reference clock OUT enable).
// There is no separate Soapy key for external clock IN / GPSDO lock.
std::string extRefUiLabel(const std::string& model);
std::string extRefUiTooltip(const std::string& model);

// Windows runtime locations used by both discovery and stream startup. These
// return candidate paths only; callers must still check existence and load
// third-party binaries explicitly. The release does not redistribute them.
std::vector<std::string> windowsApiCandidates(const std::string& appDir);
std::vector<std::string> windowsSoapyModuleCandidates(const std::string& appDir);

} // namespace SdrplayProfile