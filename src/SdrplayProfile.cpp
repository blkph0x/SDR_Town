#include "SdrplayProfile.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <sstream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

const char* SdrplaySettings::kIqCorr = "iqcorr_ctrl";
const char* SdrplaySettings::kAgcSetpoint = "agc_setpoint";
const char* SdrplaySettings::kBiasT = "biasT_ctrl";
const char* SdrplaySettings::kRfNotch = "rfnotch_ctrl";
const char* SdrplaySettings::kDabNotch = "dabnotch_ctrl";
const char* SdrplaySettings::kExtRef = "extref_ctrl";
const char* SdrplaySettings::kHdr = "hdr_ctrl";
const char* SdrplaySettings::kRfGainSel = "rfgain_sel";

namespace SdrplayProfile {
namespace {

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

std::string compactToken(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (unsigned char c : value) {
        if (std::isalnum(c)) out.push_back(static_cast<char>(std::tolower(c)));
    }
    return out;
}

bool containsInsensitive(const std::string& haystack, const std::string& needle) {
    return toLower(haystack).find(toLower(needle)) != std::string::npos;
}

void appendUnique(std::vector<std::string>& values, const std::string& value) {
    if (value.empty() || std::find(values.begin(), values.end(), value) != values.end()) return;
    values.push_back(value);
}

void appendRoot(std::vector<std::string>& roots, const std::string& base,
                const std::string& suffix = {}) {
    if (base.empty()) return;
    std::string value = base;
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
        value = value.substr(1, value.size() - 2);
    while (!value.empty() && (value.back() == '\\' || value.back() == '/')) value.pop_back();
    if (value.empty()) return;
    if (!suffix.empty()) value += suffix;
    appendUnique(roots, value);
}

std::string environmentPath(const char* name) {
#ifdef _WIN32
    const std::wstring wideName(name, name + std::char_traits<char>::length(name));
    const wchar_t* value = _wgetenv(wideName.c_str());
    if (!value) return {};
    const int count = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (count <= 1) return {};
    std::string out(size_t(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, -1, out.data(), count, nullptr, nullptr);
    out.pop_back();
    return out;
#else
    const char* value = std::getenv(name);
    return value ? value : "";
#endif
}

std::vector<std::string> windowsApiRoots() {
    std::vector<std::string> roots;
    appendRoot(roots, environmentPath("SDRPLAY_API_DIR"));
    appendRoot(roots, environmentPath("SDRPLAY_ROOT"));
#ifdef _WIN32
    // DEC-0122: official Install_Dir in both views, including per-user installs.
    for (HKEY hive : {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER}) {
        for (REGSAM view : {KEY_WOW64_64KEY, KEY_WOW64_32KEY}) {
            HKEY key = nullptr;
            if (RegOpenKeyExW(hive, L"SOFTWARE\\SDRplay\\Service\\API",
                             0, KEY_READ | view, &key) != ERROR_SUCCESS) continue;
            wchar_t value[32768]{};
            DWORD type = 0, bytes = sizeof(value) - sizeof(wchar_t);
            const auto result = RegQueryValueExW(key, L"Install_Dir", nullptr, &type,
                                                reinterpret_cast<BYTE*>(value), &bytes);
            RegCloseKey(key);
            if (result != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) continue;
            std::wstring path(value);
            if (type == REG_EXPAND_SZ) {
                wchar_t expanded[32768]{};
                const DWORD n = ExpandEnvironmentStringsW(path.c_str(), expanded, 32768);
                if (n == 0 || n > 32768) continue;
                path = expanded;
            }
            const int n = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, nullptr, 0, nullptr, nullptr);
            if (n <= 1) continue;
            std::string utf8(size_t(n), '\0');
            WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, utf8.data(), n, nullptr, nullptr);
            utf8.pop_back();
            appendRoot(roots, utf8);
        }
    }
#endif
    return roots;
}

std::vector<std::string> windowsSoapyRoots() {
    auto roots = windowsApiRoots();
    appendRoot(roots, environmentPath("SOAPY_SDR_ROOT"));
    appendRoot(roots, environmentPath("POTHOS_ROOT"));
    appendRoot(roots, environmentPath("CONDA_PREFIX"), "\\Library");
    appendRoot(roots, environmentPath("USERPROFILE"), "\\radioconda\\Library");
    appendRoot(roots, environmentPath("LOCALAPPDATA"), "\\radioconda\\Library");
    for (const char* name : {"ProgramW6432", "ProgramFiles", "ProgramFiles(x86)"}) {
        const auto base = environmentPath(name);
        appendRoot(roots, base, "\\SDRplay");
        appendRoot(roots, base, "\\SDRplay\\API");
        appendRoot(roots, base, "\\PothosSDR");
        // DEC-0132: SkyRoof ships an ABI-0.8 SoapySDRPlay3 module in this
        // standard per-machine layout. The module is still validated by the
        // Soapy loader and factory-registration checks before it can be used.
        appendRoot(roots, base, "\\Afreet\\SkyRoof");
    }
    appendRoot(roots, environmentPath("ProgramData"), "\\radioconda\\Library");
    for (const char* base : {"C:\\Program Files\\SDRplay", "C:\\Program Files\\SDRplay\\API",
             "C:\\Program Files\\PothosSDR", "C:\\Program Files\\Afreet\\SkyRoof",
             "C:\\Program Files (x86)\\SDRplay\\API", "C:\\Program Files (x86)\\PothosSDR",
             "C:\\Program Files (x86)\\Afreet\\SkyRoof", "C:\\ProgramData\\radioconda\\Library"})
        appendRoot(roots, base);
    return roots;
}

SdrplayPortCapabilities port(
    std::string driverName,
    std::string displayName,
    std::string connector,
    std::vector<std::string> aliases,
    double minHz,
    double maxHz,
    bool biasT,
    bool highImpedance = false)
{
    SdrplayPortCapabilities value;
    value.driverName = std::move(driverName);
    value.displayName = std::move(displayName);
    value.connector = std::move(connector);
    value.aliases = std::move(aliases);
    value.minFrequencyHz = minHz;
    value.maxFrequencyHz = maxHz;
    value.biasTAllowed = biasT;
    value.highImpedance = highImpedance;
    return value;
}

bool portMatches(const SdrplayPortCapabilities& candidate, const std::string& antenna) {
    const std::string wanted = compactToken(antenna);
    if (wanted.empty()) return false;
    if (compactToken(candidate.driverName) == wanted || compactToken(candidate.displayName) == wanted)
        return true;
    return std::any_of(candidate.aliases.begin(), candidate.aliases.end(), [&](const std::string& alias) {
        return compactToken(alias) == wanted;
    });
}

std::string formatFrequency(double hz) {
    std::ostringstream out;
    out << std::setprecision(6);
    if (hz >= 1.0e9) out << (hz / 1.0e9) << " GHz";
    else if (hz >= 1.0e6) out << (hz / 1.0e6) << " MHz";
    else if (hz >= 1.0e3) out << (hz / 1.0e3) << " kHz";
    else out << hz << " Hz";
    return out.str();
}



} // namespace

bool isSdrplayDriver(const std::string& driver) {
    const auto d = toLower(driver);
    return d == "sdrplay" || d == "sdrplay3" || d.find("sdrplay") != std::string::npos;
}

std::string normalizeModel(const std::string& hardware, const std::string& label) {
    const std::string h = toLower(hardware);
    const std::string l = toLower(label);
    const auto match = [&](const char* token, const char* model) -> std::string {
        if (containsInsensitive(h, token) || containsInsensitive(l, token)) return model;
        return {};
    };
    if (auto m = match("nrsp-st", "nRSP-ST"); !m.empty()) return m;
    if (auto m = match("nrspst", "nRSP-ST"); !m.empty()) return m;
    if (auto m = match("rspdx-r2", "RSPdx-R2"); !m.empty()) return m;
    if (auto m = match("rspdxr2", "RSPdx-R2"); !m.empty()) return m;
    if (auto m = match("rspdx", "RSPdx"); !m.empty()) return m;
    if (auto m = match("rspduo", "RSPduo"); !m.empty()) return m;
    if (auto m = match("rsp1b", "RSP1B"); !m.empty()) return m;
    if (auto m = match("rsp1a", "RSP1A"); !m.empty()) return m;
    if (auto m = match("rsp2", "RSP2"); !m.empty()) return m;
    if (auto m = match("rsp1", "RSP1"); !m.empty()) return m;
    if (!hardware.empty()) return hardware;
    if (!label.empty()) return label;
    return "SDRplay";
}

std::string duoModeFromKwargs(const std::map<std::string, std::string>& kwargs) {
    auto it = kwargs.find("mode");
    if (it == kwargs.end()) return {};
    return it->second;
}

std::string duoModeDisplayName(const std::string& mode) {
    if (mode == "ST") return "Single Tuner";
    if (mode == "DT") return "Dual Tuner";
    if (mode == "MA") return "Master";
    if (mode == "MA8") return "Master (8 MHz)";
    if (mode == "SL") return "Slave";
    return mode.empty() ? "n/a" : mode;
}

const std::vector<std::string>& knownSettingKeys() {
    static const std::vector<std::string> keys = {
        SdrplaySettings::kIqCorr,
        SdrplaySettings::kAgcSetpoint,
        SdrplaySettings::kBiasT,
        SdrplaySettings::kRfNotch,
        SdrplaySettings::kDabNotch,
        SdrplaySettings::kExtRef,
        SdrplaySettings::kHdr,
        SdrplaySettings::kRfGainSel,
    };
    return keys;
}

SdrplayCapabilities capabilitiesFromProbe(
    const std::string& driver,
    const std::string& hardware,
    const std::string& label,
    const std::vector<std::string>& gainElements,
    const std::vector<std::string>& antennas,
    const std::vector<double>& bandwidthsHz,
    const std::vector<std::string>& settingKeys,
    const std::map<std::string, std::vector<std::string>>& settingOptions)
{
    SdrplayCapabilities caps;
    caps.isSdrplay = isSdrplayDriver(driver);
    if (!caps.isSdrplay) return caps;

    caps.model = normalizeModel(hardware, label);
    caps.gainElements = gainElements;
    caps.antennas = antennas;
    caps.bandwidthsHz = bandwidthsHz;
    caps.settingKeys = settingKeys;
    caps.settingOptions = settingOptions;
    caps.probeVerified = !gainElements.empty() || !antennas.empty() ||
                         !bandwidthsHz.empty() || !settingKeys.empty() ||
                         !settingOptions.empty();
    caps.hasAgc = caps.probeVerified;

    for (const auto& g : gainElements) {
        const auto gl = toLower(g);
        if (gl == "ifgr" || gl.find("ifgr") != std::string::npos) caps.hasIfgr = true;
        if (gl == "rfgr" || gl.find("rfgr") != std::string::npos) caps.hasRfgr = true;
    }
    // Do not invent IFGR/RFGR or setting support when an installed module did
    // not return them.  The static model table describes physical safety only.
    return caps;
}

SdrplaySettings defaultSettings(const SdrplayCapabilities& caps) {
    SdrplaySettings s;
    if (!caps.isSdrplay) return s;
    s.agcEnabled = false;
    s.ifgrDb = 40.0;
    s.rfgrDb = 4.0;
    s.bandwidthHz = 0.0;
    s.rxChannel = 0;
    if (settingSupported(caps, SdrplaySettings::kIqCorr))
        s.soapySettings[SdrplaySettings::kIqCorr] = "true";
    if (settingSupported(caps, SdrplaySettings::kAgcSetpoint))
        s.soapySettings[SdrplaySettings::kAgcSetpoint] = "-30";
    if (settingSupported(caps, SdrplaySettings::kBiasT))
        s.soapySettings[SdrplaySettings::kBiasT] = "false";
    if (settingSupported(caps, SdrplaySettings::kRfNotch))
        s.soapySettings[SdrplaySettings::kRfNotch] = "false";
    if (settingSupported(caps, SdrplaySettings::kDabNotch))
        s.soapySettings[SdrplaySettings::kDabNotch] = "false";
    if (settingSupported(caps, SdrplaySettings::kExtRef))
        s.soapySettings[SdrplaySettings::kExtRef] = "false";
    if (settingSupported(caps, SdrplaySettings::kHdr))
        s.soapySettings[SdrplaySettings::kHdr] = "false";
    if (settingSupported(caps, SdrplaySettings::kRfGainSel))
        s.soapySettings[SdrplaySettings::kRfGainSel] = "4";
    return s;
}

bool settingSupported(const SdrplayCapabilities& caps, const std::string& key) {
    if (!caps.isSdrplay || !caps.probeVerified || caps.settingKeys.empty()) return false;
    return std::find(caps.settingKeys.begin(), caps.settingKeys.end(), key) != caps.settingKeys.end();
}

std::string boolSetting(bool on) { return on ? "true" : "false"; }

bool parseBoolSetting(const std::string& value, bool fallback) {
    const auto v = toLower(value);
    if (v == "true" || v == "1" || v == "yes" || v == "on") return true;
    if (v == "false" || v == "0" || v == "no" || v == "off") return false;
    return fallback;
}

SdrplayModelCapabilities modelCapabilities(const std::string& model) {
    const std::string normalized = normalizeModel(model, model);
    SdrplayModelCapabilities caps;
    caps.model = normalized;

    if (normalized == "nRSP-ST") {
        caps.hardwareApiSupported = false;
        caps.websocketOnly = true;
        caps.tunerCount = 1;
        return caps;
    }
    if (normalized == "RSPdx" || normalized == "RSPdx-R2") {
        caps.ports = {
            // DEC-0128 / RSPdx datasheet: only B supplies Bias-T power.
            port("Antenna A", "Port A", "SMA", {"A", "Port A", "ANT A"}, 1.0e3, 2.0e9, false),
            port("Antenna B", "Port B", "SMA", {"B", "Port B", "ANT B"}, 1.0e3, 2.0e9, true),
            port("Antenna C", "Port C", "BNC", {"C", "Port C", "ANT C"}, 1.0e3, 200.0e6, false),
        };
        return caps;
    }
    if (normalized == "RSPduo") {
        caps.tunerCount = 2;
        caps.ports = {
            port("Tuner 1 50 ohm", "Tuner 1", "SMA 50 ohm",
                 {"Tuner 1 Port A", "Tuner 1 50", "RX1", "Antenna A"},
                 1.0e3, 2.0e9, false),
            port("Tuner 2 50 ohm", "Tuner 2", "SMA 50 ohm",
                 {"Tuner 2 Port B", "Tuner 2 50", "RX2", "Antenna B"},
                 1.0e3, 2.0e9, true),
            port("Tuner 1 Hi-Z", "Tuner 1 Hi-Z", "balanced high impedance",
                 {"Hi-Z", "HiZ", "High Z", "Tuner 1 HiZ"},
                 1.0e3, 30.0e6, false, true),
        };
        return caps;
    }
    if (normalized == "RSP2") {
        caps.ports = {
            port("Antenna A", "Antenna A", "SMA 50 ohm", {"A", "Port A"},
                 1.0e3, 2.0e9, false),
            port("Antenna B", "Antenna B", "SMA 50 ohm", {"B", "Port B"},
                 1.0e3, 2.0e9, true),
            port("Hi-Z", "Hi-Z", "balanced high impedance", {"HiZ", "High Z"},
                 1.0e3, 30.0e6, false, true),
        };
        return caps;
    }
    if (normalized == "RSP1A" || normalized == "RSP1B") {
        caps.ports = {
            port("RX", "RF input", "SMA", {"Antenna", "Antenna A", "Port A"},
                 1.0e3, 2.0e9, true),
        };
        return caps;
    }
    if (normalized == "RSP1") {
        caps.ports = {
            port("RX", "RF input", "SMA", {"Antenna", "Antenna A", "Port A"},
                 10.0e3, 2.0e9, false),
        };
        return caps;
    }
    return caps;
}

std::optional<SdrplayPortCapabilities> antennaCapabilities(
    const std::string& model, const std::string& antenna)
{
    const auto caps = modelCapabilities(model);
    if (caps.ports.empty()) return std::nullopt;
    if (antenna.empty() && caps.ports.size() == 1) return caps.ports.front();
    for (const auto& candidate : caps.ports) {
        if (portMatches(candidate, antenna)) return candidate;
    }
    return std::nullopt;
}

bool frequencyAllowedForAntenna(
    const std::string& model, const std::string& antenna, double frequencyHz)
{
    if (!std::isfinite(frequencyHz) || frequencyHz <= 0.0) return false;
    const auto caps = antennaCapabilities(model, antenna);
    if (!caps) return false;
    return frequencyHz >= caps->minFrequencyHz && frequencyHz <= caps->maxFrequencyHz;
}

std::string antennaFrequencyError(
    const std::string& model, const std::string& antenna, double frequencyHz)
{
    if (frequencyAllowedForAntenna(model, antenna, frequencyHz)) return {};
    const auto caps = antennaCapabilities(model, antenna);
    if (!caps) {
        return "No verified physical port profile is available for " +
               (model.empty() ? std::string("this SDRplay model") : model) +
               " / " + (antenna.empty() ? std::string("the selected antenna") : antenna) + ".";
    }
    return formatFrequency(frequencyHz) + " is outside " + caps->displayName +
           " (" + caps->connector + ") range " +
           formatFrequency(caps->minFrequencyHz) + " to " +
           formatFrequency(caps->maxFrequencyHz) + ".";
}

bool usesHardwareApi(const std::string& model) {
    return modelCapabilities(model).hardwareApiSupported;
}

bool usesWebsocketApi(const std::string& model) {
    return modelCapabilities(model).websocketOnly;
}

std::string backendDescription(const std::string& model) {
    const auto caps = modelCapabilities(model);
    if (caps.websocketOnly)
        return "SDRconnect WebSocket API (network receiver; not SDRplay Hardware API / SoapySDRPlay3)";
    if (caps.hardwareApiSupported)
        return "SDRplay Hardware API 3.x via SoapySDRPlay3";
    return "Unknown SDRplay backend";
}

std::string antennaPortDescription(const std::string& model, const std::string& antenna) {
    const auto caps = antennaCapabilities(model, antenna);
    if (!caps) {
        if (usesWebsocketApi(model)) return backendDescription(model);
        return antenna.empty() ? "Unknown port (not verified)" : antenna + " (not verified)";
    }
    std::string text = caps->displayName + " " + caps->connector + " (" +
                       formatFrequency(caps->minFrequencyHz) + " to " +
                       formatFrequency(caps->maxFrequencyHz) + ")";
    if (caps->highImpedance) text += ", high impedance";
    text += caps->biasTAllowed ? ", Bias-T capable" : ", no Bias-T";
    return text;
}

bool biasTAllowedForAntenna(const std::string& model, const std::string& antenna) {
    const auto caps = antennaCapabilities(model, antenna);
    return caps.has_value() && caps->biasTAllowed;
}

double maxSampleRateHz(const std::string& duoMode) {
    // Dual Tuner: 2 MSPS per tuner (SoapySDRPlay / SDRplay dual-mode limit).
    if (duoMode == "DT") return 2.0e6;
    // Single / Master / Slave: up to 10 MSPS instantaneous.
    return 10.0e6;
}

double clampSampleRateHz(const std::string& duoMode, double requestedHz) {
    if (!std::isfinite(requestedHz) || requestedHz <= 0.0) return requestedHz;
    const double maxHz = maxSampleRateHz(duoMode);
    if (requestedHz > maxHz) return maxHz;
    return requestedHz;
}

std::string rfNotchUiLabel() {
    // SoapySDRPlay3 exposes one rfnotch_ctrl covering broadcast MW/FM notch behaviour.
    return "Broadcast MW/FM notch (combined)";
}

std::string rfNotchUiTooltip() {
    return "SoapySDRPlay rfnotch_ctrl -> API rfNotchEnable. One hardware broadcast notch "
           "(MW + FM band regions together). Separate MW vs FM toggles are not exposed by "
           "SoapySDRPlay3 or the public API structs.";
}

std::string extRefUiLabel(const std::string& model) {
    const auto m = toLower(model);
    if (m.find("rspduo") != std::string::npos || m.find("rsp2") != std::string::npos) {
        return "Reference clock OUT";
    }
    if (m.find("rspdx") != std::string::npos) {
        return "Ext ref (Soapy extref_ctrl)";
    }
    return "Reference clock OUT (extref)";
}

std::string extRefUiTooltip(const std::string& model) {
    const auto m = toLower(model);
    if (m.find("rspduo") != std::string::npos) {
        return "Soapy extref_ctrl -> API extRefOutputEn. Enables RSPduo reference clock "
               "OUTPUT for daisy-chain / second unit sync. External clock INPUT / GPSDO "
               "lock is not a separate Soapy setting.";
    }
    if (m.find("rsp2") != std::string::npos) {
        return "Soapy extref_ctrl -> API extRefOutputEn (RSP2 reference clock output enable).";
    }
    if (m.find("rspdx") != std::string::npos) {
        return "Soapy exposes extref_ctrl when the module lists it. RSPdx MCX is typically "
               "an external reference input at the hardware level; confirm behaviour with "
               "your SoapySDRPlay3 / API build. No separate clock-in vs clock-out keys.";
    }
    return "SoapySDRPlay extref_ctrl maps to API extRefOutputEn (clock OUT enable).";
}

std::vector<std::string> windowsApiCandidates(const std::string& appDir) {
    std::vector<std::string> paths;
    const auto add = [&](const std::string& root) {
        if (root.empty()) return;
        appendUnique(paths, root + "\\sdrplay_api.dll");
        appendUnique(paths, root + "\\bin\\sdrplay_api.dll");
        // The official API installer keeps the 64-bit DLL under API\\x64.
        // Keep the other architecture layouts for developer and future builds.
        appendUnique(paths, root + "\\x64\\sdrplay_api.dll");
        appendUnique(paths, root + "\\x86\\sdrplay_api.dll");
        appendUnique(paths, root + "\\arm64\\sdrplay_api.dll");
    };
    // Explicit/vendor installation wins over a stale DLL from another SDR bundle.
    for (const auto& root : windowsApiRoots()) add(root);
    add(appDir);
    for (const auto& root : windowsSoapyRoots()) add(root);
    const auto systemRoot = environmentPath("SystemRoot");
    if (!systemRoot.empty()) appendUnique(paths, systemRoot + "\\System32\\sdrplay_api.dll");
    appendUnique(paths, "C:\\Windows\\System32\\sdrplay_api.dll");
    return paths;
}

std::vector<std::string> windowsSoapyModuleCandidates(const std::string& appDir) {
    std::vector<std::string> paths;
    const auto addDirectory = [&](const std::string& directory) {
        if (directory.empty()) return;
        appendUnique(paths, directory + "\\SoapySDRPlay3.dll");
        appendUnique(paths, directory + "\\sdrPlaySupport.dll");
    };
    std::istringstream pluginPaths(environmentPath("SOAPY_SDR_PLUGIN_PATH"));
    for (std::string directory; std::getline(pluginPaths, directory, ';');) {
        std::vector<std::string> normalized;
        appendRoot(normalized, directory);
        if (!normalized.empty()) addDirectory(normalized.front());
    }
    auto roots = windowsSoapyRoots();
    roots.insert(roots.begin(), appDir);
    for (const auto& root : roots) {
        if (root.empty()) continue;
        for (const char* suffix : {"", "\\bin", "\\lib\\SoapySDR\\modules0.8",
                                  "\\lib\\SoapySDR\\modules", "\\lib64\\SoapySDR\\modules0.8"})
            addDirectory(root + suffix);
    }
    return paths;
}



} // namespace SdrplayProfile
