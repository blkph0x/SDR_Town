#include "SdrplayRuntime.h"
#include "SdrplayProfile.h"
#include <SoapySDR/Modules.hpp>
#include <SoapySDR/Registry.hpp>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <array>
#include <algorithm>

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        const auto args = app.arguments();
        require(args.size() == 6, "expected scenario, API, good, bad and empty module paths");
        const auto scenario = args[1];
        const auto api = args[2].toStdString();
        const auto good = args[3].toStdString();
        const auto bad = args[4].toStdString();
        const auto empty = args[5].toStdString();
        require(!SdrplayRuntime::ensureCandidates({}, {}).registered, "empty search claimed a driver");
        if (scenario == "legacy-abi") {
            require(SoapySDR::loadModule(bad).empty(), "fixture did not reproduce empty DLL-load success");
            require(!SoapySDR::getLoaderResult(bad).at("sdrplay").empty(), "ABI failure not recorded");
            require(SoapySDR::Registry::listFindFunctions().count("sdrplay") == 0, "bad ABI registered");
            require(!SdrplayRuntime::ensureCandidates({api}, {bad}).registered, "accepted bad ABI");
        } else if (scenario == "retry") {
            auto rejected = SdrplayRuntime::ensureCandidates({api}, {bad, empty});
            require(rejected.apiLoaded && !rejected.registered, "accepted module without valid factory");
            bool abi = false;
            for (const auto& error : rejected.errors) abi |= error.find("ABI") != std::string::npos;
            require(abi, "lost ABI failure diagnostic");
            auto accepted = SdrplayRuntime::ensureCandidates({api}, {good});
            require(accepted.registered && accepted.modulePath == good, "Rescan did not recover");
            auto repeated = SdrplayRuntime::ensureCandidates({api}, {bad, empty});
            require(repeated.registered && repeated.modulePath == good, "replaced working driver");
            std::array<bool, 8> results{};
            std::array<std::thread, 8> workers;
            for (size_t i = 0; i < workers.size(); ++i)
                workers[i] = std::thread([&, i] { results[i] = SdrplayRuntime::ensureCandidates({api}, {good}).registered; });
            for (auto& worker : workers) worker.join();
            for (bool result : results) require(result, "concurrent registration changed");
        } else if (scenario == "invalid-api") {
            QTemporaryFile invalidImage;
            require(invalidImage.open(), "invalid image fixture");
            require(invalidImage.write("not a PE library") == 16, "invalid image write");
            invalidImage.close();
            const auto image = SdrplayRuntime::ensureCandidates({invalidImage.fileName().toStdString()}, {good});
            require(!image.apiLoaded && !image.registered && !image.errors.empty(), "invalid image accepted");
            auto rejected = SdrplayRuntime::ensureCandidates({empty}, {good});
            require(!rejected.apiLoaded && !rejected.registered, "non-API DLL accepted");
            require(SdrplayRuntime::ensureCandidates({api}, {good}).registered, "failed API retry poisoned process");
        } else if (scenario == "unicode-api") {
            QTemporaryDir directory;
            require(directory.isValid(), "temporary directory");
            const QString path = directory.path() + QString::fromUtf8("/\xD0\xA0\xD0\xA1\xD0\x9F");
            require(QDir().mkpath(path), "unicode directory");
            const QString target = path + "/sdrplay_api.dll";
            require(QFile::copy(args[2], target), "unicode API copy");
            auto report = SdrplayRuntime::ensureCandidates({target.toStdString()}, {good});
            require(report.apiLoaded && report.registered, "Unicode API path did not load");
            // Loaded libraries intentionally live until process exit.
            directory.setAutoRemove(false);
            std::cout << "Fixture directory retained for parent cleanup: " << directory.path().toStdString() << '\n';
        } else if (scenario == "path-layout") {
            // This process owns these overrides; no machine/user environment is changed.
            qputenv("SDRPLAY_API_DIR", "C:\\explicit API");
            qputenv("CONDA_PREFIX", "C:\\custom conda");
            qputenv("USERPROFILE", "C:\\Users\\RSP tester");
            qputenv("LOCALAPPDATA", "C:\\Users\\RSP tester\\Local");
            qputenv("SOAPY_SDR_PLUGIN_PATH", "\"C:\\plugins one\";C:\\plugins two");
            const auto modules = SdrplayProfile::windowsSoapyModuleCandidates("C:\\portable");
            const auto apis = SdrplayProfile::windowsApiCandidates("C:\\portable");
            for (const auto* expected : {
                    "C:\\plugins one\\sdrPlaySupport.dll",
                    "C:\\plugins two\\SoapySDRPlay3.dll",
                    "C:\\custom conda\\Library\\lib\\SoapySDR\\modules0.8\\sdrPlaySupport.dll",
                    "C:\\Users\\RSP tester\\radioconda\\Library\\bin\\sdrPlaySupport.dll",
                    "C:\\Users\\RSP tester\\Local\\radioconda\\Library\\bin\\sdrPlaySupport.dll",
                    "C:\\portable\\lib\\SoapySDR\\modules0.8\\sdrPlaySupport.dll"})
                require(std::find(modules.begin(), modules.end(), expected) != modules.end(),
                        "missing explicit/conda/user/portable module layout");
            require(apis.front() == "C:\\explicit API\\sdrplay_api.dll", "API override lost precedence");

            QTemporaryDir directory;
            require(directory.isValid(), "portable fixture directory");
            const auto root = directory.path();
            const auto nested = root + "/lib/SoapySDR/modules0.8";
            require(QDir().mkpath(nested), "nested portable modules");
            require(QFile::copy(args[2], root + "/sdrplay_api.dll"), "portable API copy");
            require(QFile::copy(args[3], nested + "/sdrPlaySupport.dll"), "portable module copy");
            qputenv("SDRPLAY_API_DIR", root.toUtf8());
            qputenv("SOAPY_SDR_PLUGIN_PATH", "");
            const auto report = SdrplayRuntime::ensure(root.toStdString());
            require(report.registered, "public loader missed nested portable module");
            require(report.moduleVersion == "loader-test-only", "loaded installed module instead of portable fixture");
            directory.setAutoRemove(false);
            std::cout << "Fixture directory retained for parent cleanup: " << root.toStdString() << '\n';
        } else throw std::runtime_error("unknown scenario");
        std::cout << "PASS " << scenario.toStdString() << " (loader only, no physical RSP)\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}
