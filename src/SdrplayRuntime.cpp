#include "SdrplayRuntime.h"
#include "SdrplayProfile.h"
#include <QFileInfo>
#include <QString>
#include <mutex>
#include <sstream>

#ifdef HAVE_SOAPYSDR
#include <SoapySDR/Modules.hpp>
#include <SoapySDR/Registry.hpp>
#endif
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winsvc.h>
#endif

namespace SdrplayRuntime {
namespace {
std::mutex runtimeMutex;
std::string selectedModule;
std::string selectedVersion;

#ifdef _WIN32
std::string soapyModulePath(const std::string& path) {
    // Soapy 0.8 Modules.in.cpp calls LoadLibraryA. Never silently replace
    // Unicode characters with '?' and accidentally load a different file.
    const auto wide = QFileInfo(QString::fromStdString(path)).absoluteFilePath().toStdWString();
    const auto encode = [](const std::wstring& value) -> std::string {
        const bool utf8 = GetACP() == CP_UTF8;
        BOOL substituted = FALSE;
        const DWORD flags = utf8 ? WC_ERR_INVALID_CHARS : WC_NO_BEST_FIT_CHARS;
        const int n = WideCharToMultiByte(CP_ACP, flags, value.c_str(), -1,
            nullptr, 0, nullptr, utf8 ? nullptr : &substituted);
        if (n <= 1 || substituted) return {};
        std::string bytes(size_t(n), '\0');
        if (!WideCharToMultiByte(CP_ACP, flags, value.c_str(), -1, bytes.data(), n,
                                nullptr, utf8 ? nullptr : &substituted) || substituted) return {};
        bytes.pop_back();
        return bytes;
    };
    auto encoded = encode(wide);
    if (!encoded.empty()) return encoded;
    std::wstring shortPath(32768, L'\0');
    const DWORD n = GetShortPathNameW(wide.c_str(), shortPath.data(), DWORD(shortPath.size()));
    if (n == 0 || n >= shortPath.size()) return {};
    shortPath.resize(n);
    return encode(shortPath);
}

std::string libraryPath(HMODULE library) {
    std::wstring path(32768, L'\0');
    const DWORD n = GetModuleFileNameW(library, path.data(), DWORD(path.size()));
    if (n == 0 || n >= path.size()) return {};
    return QString::fromWCharArray(path.data(), int(n)).toStdString();
}

std::string serviceStatus() {
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ENUMERATE_SERVICE);
    if (!manager) return "unknown (service query denied)";
    DWORD bytes = 0, count = 0, resume = 0;
    EnumServicesStatusExW(manager, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL,
                         nullptr, 0, &bytes, &count, &resume, nullptr);
    // SCM owns the count; bound allocation and report failure rather than guessing.
    if (bytes == 0 || bytes > 4 * 1024 * 1024) {
        CloseServiceHandle(manager);
        return "unknown (service query failed)";
    }
    std::vector<BYTE> data(bytes);
    resume = 0;
    const BOOL ok = EnumServicesStatusExW(manager, SC_ENUM_PROCESS_INFO, SERVICE_WIN32,
        SERVICE_STATE_ALL, data.data(), DWORD(data.size()), &bytes, &count, &resume, nullptr);
    CloseServiceHandle(manager);
    if (!ok) return "unknown (service query failed)";
    const auto* entries = reinterpret_cast<const ENUM_SERVICE_STATUS_PROCESSW*>(data.data());
    std::string found;
    for (DWORD i = 0; i < count; ++i) {
        const auto name = QString::fromWCharArray(entries[i].lpServiceName);
        const auto display = QString::fromWCharArray(entries[i].lpDisplayName);
        if (!name.contains("sdrplay", Qt::CaseInsensitive) &&
            !display.contains("sdrplay", Qt::CaseInsensitive)) continue;
        const DWORD state = entries[i].ServiceStatusProcess.dwCurrentState;
        if (state == SERVICE_RUNNING) return "running (" + name.toStdString() + ")";
        found = (state == SERVICE_STOPPED ? "stopped (" : "pending (") + name.toStdString() + ")";
    }
    return found.empty() ? "not installed" : found;
}

bool validApi(HMODULE library) {
    return GetProcAddress(library, "sdrplay_api_Open") &&
           GetProcAddress(library, "sdrplay_api_ApiVersion") &&
           GetProcAddress(library, "sdrplay_api_GetDevices");
}

void prepareApi(Report& report, const std::vector<std::string>& candidates) {
    if (const auto library = GetModuleHandleW(L"sdrplay_api.dll")) {
        report.apiPath = libraryPath(library);
        report.apiLoaded = validApi(library);
        if (!report.apiLoaded) report.errors.push_back("Loaded API lacks required API 3.x exports: " + report.apiPath);
        return;
    }
    for (const auto& path : candidates) {
        const QFileInfo file(QString::fromStdString(path));
        if (!file.isFile()) continue;
        // Full path and architecture are checked by the Windows loader. Do not
        // prepend every installed x86/ARM/conda directory to the process PATH.
        const auto wide = file.absoluteFilePath().toStdWString();
        DWORD previousMode = 0;
        const BOOL changedMode = SetThreadErrorMode(
            SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX, &previousMode);
        HMODULE library = LoadLibraryExW(wide.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        const auto error = GetLastError();
        if (changedMode) SetThreadErrorMode(previousMode, nullptr);
        if (!library) {
            report.errors.push_back(path + ": Win32 " + std::to_string(error));
            continue;
        }
        if (!validApi(library)) {
            report.errors.push_back(path + ": missing API 3.x exports");
            FreeLibrary(library);
            continue;
        }
        report.apiLoaded = true;
        report.apiPath = libraryPath(library);
        return; // Process-lifetime dependency; never unload underneath Soapy.
    }
}
#endif

#ifdef HAVE_SOAPYSDR
bool factoryRegistered() {
    const auto find = SoapySDR::Registry::listFindFunctions();
    const auto make = SoapySDR::Registry::listMakeFunctions();
    for (const auto& [name, function] : find) {
        const auto entry = make.find(name);
        if (SdrplayProfile::isSdrplayDriver(name) && function && entry != make.end() && entry->second)
            return true;
    }
    return false;
}
#endif
} // namespace

std::string Report::description() const {
    std::ostringstream out;
    out << "SDRplay: ";
    if (registered) out << "driver registered (not a hardware detection)";
    else out << "driver unavailable";
    if (!apiPath.empty()) out << "; API=" << apiPath;
#ifdef _WIN32
    else if (!apiLoaded) out << "; API 3.x DLL missing or failed to load";
#endif
    if (!modulePath.empty()) out << "; module=" << modulePath;
    if (!moduleVersion.empty()) out << " [" << moduleVersion << "]";
    if (!service.empty()) out << "; service=" << service;
    if (service == "not installed") out << "; install the official SDRplay API/service";
    else if (service.rfind("stopped", 0) == 0) out << "; start the SDRplay API service";
    if (!errors.empty()) out << "; " << errors.back();
    if (!registered) out << "; check API 3.x and a matching SoapySDRPlay3 module, then Rescan";
    return out.str();
}

Report ensureCandidates(const std::vector<std::string>& apiCandidates,
                        const std::vector<std::string>& moduleCandidates) {
    std::lock_guard<std::mutex> lock(runtimeMutex);
    Report report;
#ifdef _WIN32
    report.service = serviceStatus();
    prepareApi(report, apiCandidates);
#else
    (void)apiCandidates;
#endif
#ifdef HAVE_SOAPYSDR
    if (factoryRegistered()) {
        report.registered = true;
        report.modulePath = selectedModule.empty() ? "already registered Soapy driver" : selectedModule;
        report.moduleVersion = selectedVersion;
        return report;
    }
#ifdef _WIN32
    if (!report.apiLoaded) return report;
#endif
    for (const auto& candidate : moduleCandidates) {
        if (!QFileInfo(QString::fromStdString(candidate)).isFile()) continue;
        try {
#ifdef _WIN32
            const auto module = soapyModulePath(candidate);
            if (module.empty()) {
                report.errors.push_back(candidate + ": path cannot be represented by Soapy's Windows loader; use an ASCII install path");
                continue;
            }
#else
            const auto& module = candidate;
#endif
            const std::string error = SoapySDR::loadModule(module);
            const auto results = SoapySDR::getLoaderResult(module);
            // Already-loaded modules retain their registration result, including
            // ABI errors. Preserve that diagnostic on every Rescan.
            if (!error.empty() && results.empty()) {
                report.errors.push_back(candidate + ": " + error);
                continue;
            }
            bool driverAccepted = false;
            bool registrationRejected = false;
            for (const auto& [name, registrationError] : results) {
                if (!SdrplayProfile::isSdrplayDriver(name)) continue;
                if (registrationError.empty()) driverAccepted = true;
                else {
                    registrationRejected = true;
                    report.errors.push_back(candidate + ": " + registrationError);
                }
            }
            if (driverAccepted && factoryRegistered()) {
                report.registered = true;
                selectedModule = report.modulePath = candidate;
                selectedVersion = report.moduleVersion = SoapySDR::getModuleVersion(module);
                break;
            }
            if (!registrationRejected)
                report.errors.push_back(candidate + ": SDRplay factory not registered");
        } catch (const std::exception& ex) {
            report.errors.push_back(candidate + ": " + ex.what());
        }
    }
#else
    (void)moduleCandidates;
    report.errors.push_back("This build has no SoapySDR support");
#endif
    return report;
}

Report ensure(const std::string& appDir) {
#ifdef _WIN32
    return ensureCandidates(SdrplayProfile::windowsApiCandidates(appDir),
                            SdrplayProfile::windowsSoapyModuleCandidates(appDir));
#elif defined(HAVE_SOAPYSDR)
    // Explicit discovery is needed even after another driver disabled Soapy's
    // automatic loader. Preserve platform ABI/multiarch and plugin search paths.
    auto modules = SoapySDR::listModules();
    const auto bundled = SoapySDR::listModules(appDir + "/lib/SoapySDR/modules");
    modules.insert(modules.begin(), bundled.begin(), bundled.end());
    std::vector<std::string> candidates;
    for (const auto& module : modules) {
        if (QFileInfo(QString::fromStdString(module)).fileName().contains("sdrplay", Qt::CaseInsensitive))
            candidates.push_back(module);
    }
    return ensureCandidates({}, candidates);
#else
    (void)appDir;
    return ensureCandidates({}, {});
#endif
}
} // namespace SdrplayRuntime
