#include "SdrplayProfile.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

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

void appendUnique(std::vector<std::string>& values, const std::string& value) {
    if (value.empty() || std::find(values.begin(), values.end(), value) != values.end()) return;
    values.push_back(value);
}

void appendRoot(std::vector<std::string>& roots, const std::string& base,
                const std::string& suffix = {}) {
    if (base.empty()) return;
    std::string value = base;
    while (!value.empty() && (value.back() == '\\' || value.back() == '/')) value.pop_back();
    if (!suffix.empty()) value += suffix;
    appendUnique(roots, value);
}

std::vector<std::string> windowsSoapyRoots() {
    std::vector<std::string> roots;

    // Explicit overrides are first so portable and managed installations win.
    if (const char* api = std::getenv("SDRPLAY_API_DIR"); api && *api)
        appendRoot(roots, api);
    if (const char* root = std::getenv("SOAPY_SDR_ROOT"); root && *root)
        appendRoot(roots, root);

    const char* programFiles64 = std::getenv("ProgramW6432");
    if (!programFiles64 || !*programFiles64) programFiles64 = std::getenv("ProgramFiles");
    if (programFiles64 && *programFiles64) {
        appendRoot(roots, programFiles64, "\\SDRplay");
        appendRoot(roots, programFiles64, "\\SDRplay\\API");
        appendRoot(roots, programFiles64, "\\PothosSDR");
    }

    if (const char* programFiles = std::getenv("ProgramFiles"); programFiles && *programFiles) {
        appendRoot(roots, programFiles, "\\SDRplay");
        appendRoot(roots, programFiles, "\\SDRplay\\API");
        appendRoot(roots, programFiles, "\\PothosSDR");
    }
    if (const char* programFilesX86 = std::getenv("ProgramFiles(x86)");
        programFilesX86 && *programFilesX86) {
        appendRoot(roots, programFilesX86, "\\PothosSDR");
    }
    if (const char* programData = std::getenv("ProgramData"); programData && *programData)
        appendRoot(roots, programData, "\\radioconda\\Library");

    // Deterministic fallbacks retain compatibility with existing installations
    // and make candidate generation testable on non-Windows CI hosts.
    appendRoot(roots, "C:\\Program Files\\SDRplay");
    appendRoot(roots, "C:\\Program Files\\SDRplay\\API");
    appendRoot(roots, "C:\\Program Files\\PothosSDR");
    appendRoot(roots, "C:\\Program Files (x86)\\PothosSDR");
    appendRoot(roots, "C:\\ProgramData\\radioconda\\Library");
    return roots;
}

bool containsInsensitive(const std::string& hay, const char* needle) {
    return toLower(hay).find(toLower(needle)) != std::string::npos;
}

#ifdef _WIN32
std::string parentDirectory(const std::string& path) {
    const auto split = path.find_last_of("\\/");
    return split == std::string::npos ? std::string{} : path.substr(0, split);
}

std::string normalizedPath(std::string path) {
    if (path.size() >= 2 && path.front() == '"' && path.back() == '"')
        path = path.substr(1, path.size() - 2);
    std::replace(path.begin(), path.end(), '/', '\\');
    while (path.size() > 3 && path.back() == '\\') path.pop_back();
    return toLower(path);
}

bool pathContainsDirectory(const std::string& pathList, const std::string& directory) {
    const std::string wanted = normalizedPath(directory);
    size_t begin = 0;
    while (begin <= pathList.size()) {
        const size_t end = pathList.find(';', begin);
        const std::string entry = pathList.substr(
            begin, end == std::string::npos ? std::string::npos : end - begin);
        if (normalizedPath(entry) == wanted) return true;
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return false;
}

bool directoryExists(const std::string& path) {
    if (path.empty()) return false;
    const DWORD attrs = GetFileAttributesA(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool fileExists(const std::string& path) {
    if (path.empty()) return false;
    const DWORD attrs = GetFileAttributesA(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

void prependRuntimeDirectory(std::string& processPath, const std::string& directory) {
    if (!directoryExists(directory) || pathContainsDirectory(processPath, directory)) return;
    processPath = processPath.empty() ? directory : directory + ";" + processPath;
}

std::string executableDirectory() {
    std::string path(32768, '\0');
    const DWORD count = GetModuleFileNameA(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (count == 0 || count >= path.size()) return {};
    path.resize(count);
    return parentDirectory(path);
}
#endif

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
    caps.hasAgc = true;

    for (const auto& g : gainElements) {
        const auto gl = toLower(g);
        if (gl == "ifgr" || gl.find("if") != std::string::npos) caps.hasIfgr = true;
        if (gl == "rfgr" || gl.find("rf") != std::string::npos) caps.hasRfgr = true;
    }
    // SoapySDRPlay always exposes IFGR/RFGR when probe succeeds; fill defaults if empty.
    if (caps.gainElements.empty()) {
        caps.gainElements = {"IFGR", "RFGR"};
        caps.hasIfgr = caps.hasRfgr = true;
    }
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
    s.soapySettings[SdrplaySettings::kIqCorr] = "true";
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
    if (!caps.isSdrplay) return false;
    if (caps.settingKeys.empty()) {
        // Before probe, allow known keys so UI can still persist; apply will no-op if unsupported.
        const auto& known = knownSettingKeys();
        return std::find(known.begin(), known.end(), key) != known.end();
    }
    return std::find(caps.settingKeys.begin(), caps.settingKeys.end(), key) != caps.settingKeys.end();
}

std::string boolSetting(bool on) { return on ? "true" : "false"; }

bool parseBoolSetting(const std::string& value, bool fallback) {
    const auto v = toLower(value);
    if (v == "true" || v == "1" || v == "yes" || v == "on") return true;
    if (v == "false" || v == "0" || v == "no" || v == "off") return false;
    return fallback;
}

std::string antennaPortDescription(const std::string& model, const std::string& antenna) {
    const auto m = toLower(model);
    const auto a = toLower(antenna);
    if (a == "rx") return "Single SMA (1 kHz–2 GHz)";
    if (a == "antenna a") {
        if (m.find("rspdx") != std::string::npos) return "Port A SMA (1 kHz–2 GHz), Bias-T capable";
        if (m.find("rsp2") != std::string::npos) return "Antenna A SMA, Bias-T capable";
        return "Antenna A SMA";
    }
    if (a == "antenna b") {
        if (m.find("rspdx") != std::string::npos) return "Port B SMA (1 kHz–2 GHz), Bias-T capable";
        return "Antenna B SMA";
    }
    if (a == "antenna c") {
        return "Port C BNC (1 kHz–200 MHz) — no Bias-T";
    }
    if (a == "hi-z" || a.find("hi-z") != std::string::npos || a.find("hiz") != std::string::npos) {
        return "Hi-Z balanced HF input (long wire) — no Bias-T";
    }
    if (a.find("tuner 1 50") != std::string::npos) return "Tuner 1 Port A SMA 50 Ω, Bias-T capable";
    if (a.find("tuner 2 50") != std::string::npos) return "Tuner 2 Port B SMA 50 Ω, Bias-T capable";
    if (a.find("tuner 1 hi") != std::string::npos) return "Tuner 1 Hi-Z — no Bias-T";
    return antenna.empty() ? "Unknown port" : antenna;
}

bool biasTAllowedForAntenna(const std::string& /*model*/, const std::string& antenna) {
    const auto a = toLower(antenna);
    if (a.empty() || a == "rx") return true; // RSP1A/1B single SMA
    if (a == "antenna c") return false;
    if (a == "hi-z" || a.find("hi-z") != std::string::npos || a.find("hiz") != std::string::npos)
        return false;
    if (a.find("tuner 1 hi") != std::string::npos) return false;
    // Antenna A/B and Tuner 1/2 50 ohm allow Bias-T on models that expose the setting.
    return true;
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
    return "SoapySDRPlay rfnotch_ctrl → API rfNotchEnable. One hardware broadcast notch "
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
        return "Soapy extref_ctrl → API extRefOutputEn. Enables RSPduo reference clock "
               "OUTPUT for daisy-chain / second unit sync. External clock INPUT / GPSDO "
               "lock is not a separate Soapy setting.";
    }
    if (m.find("rsp2") != std::string::npos) {
        return "Soapy extref_ctrl → API extRefOutputEn (RSP2 reference clock output enable).";
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
    appendUnique(paths, appDir + "\\sdrplay_api.dll");
    for (const auto& root : windowsSoapyRoots()) {
        appendUnique(paths, root + "\\sdrplay_api.dll");
        appendUnique(paths, root + "\\bin\\sdrplay_api.dll");
        // The official API installer keeps the 64-bit DLL under API\x64.
        // Keep the other architecture layouts for developer and future builds.
        appendUnique(paths, root + "\\x64\\sdrplay_api.dll");
        appendUnique(paths, root + "\\x86\\sdrplay_api.dll");
        appendUnique(paths, root + "\\arm64\\sdrplay_api.dll");
    }
    if (const char* systemRoot = std::getenv("SystemRoot"); systemRoot && *systemRoot)
        appendUnique(paths, std::string(systemRoot) + "\\System32\\sdrplay_api.dll");
    appendUnique(paths, "C:\\Windows\\System32\\sdrplay_api.dll");
    return paths;
}

std::vector<std::string> windowsSoapyModuleCandidates(const std::string& appDir) {
    std::vector<std::string> paths;
    appendUnique(paths, appDir + "\\SoapySDRPlay3.dll");
    appendUnique(paths, appDir + "\\sdrPlaySupport.dll");
    for (const auto& root : windowsSoapyRoots()) {
        appendUnique(paths, root + "\\sdrPlaySupport.dll");
        appendUnique(paths, root + "\\bin\\sdrPlaySupport.dll");
        appendUnique(paths, root + "\\lib\\SoapySDR\\modules0.8\\sdrPlaySupport.dll");
        appendUnique(paths, root + "\\lib\\SoapySDR\\modules\\sdrPlaySupport.dll");
        appendUnique(paths, root + "\\lib64\\SoapySDR\\modules0.8\\sdrPlaySupport.dll");
    }
    return paths;
}

#ifdef _WIN32
namespace {

// DeviceManager asks Soapy to load sdrPlaySupport.dll during its first scan.
// Preload the vendor API here so Windows can resolve that module's dependency
// even when the official installer placed it in SDRplay\API\x64, which is not
// normally on PATH. The handle intentionally lives for the process lifetime.
HMODULE gSdrplayApiHandle = nullptr;

struct WindowsSdrplayRuntimeBootstrap {
    WindowsSdrplayRuntimeBootstrap() {
        const std::string appDir = executableDirectory();
        std::string processPath = std::getenv("PATH") ? std::getenv("PATH") : "";

        prependRuntimeDirectory(processPath, appDir);
        for (const auto& root : windowsSoapyRoots()) {
            prependRuntimeDirectory(processPath, root);
            prependRuntimeDirectory(processPath, root + "\\bin");
            prependRuntimeDirectory(processPath, root + "\\x64");
            prependRuntimeDirectory(processPath, root + "\\x86");
            prependRuntimeDirectory(processPath, root + "\\arm64");
        }
        for (const auto& candidate : windowsApiCandidates(appDir)) {
            if (fileExists(candidate))
                prependRuntimeDirectory(processPath, parentDirectory(candidate));
        }
        _putenv_s("PATH", processPath.c_str());

        gSdrplayApiHandle = GetModuleHandleA("sdrplay_api.dll");
        if (gSdrplayApiHandle) return;

        for (const auto& candidate : windowsApiCandidates(appDir)) {
            if (!fileExists(candidate)) continue;
            gSdrplayApiHandle = LoadLibraryExA(
                candidate.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
            if (gSdrplayApiHandle) return;

            const std::string message =
                "SDR Town: unable to preload " + candidate +
                " (Win32 " + std::to_string(GetLastError()) + ")\n";
            OutputDebugStringA(message.c_str());
        }
    }
};

const WindowsSdrplayRuntimeBootstrap gWindowsSdrplayRuntimeBootstrap;

} // namespace
#endif

} // namespace SdrplayProfile
