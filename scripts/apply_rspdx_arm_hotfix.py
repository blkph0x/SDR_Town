from pathlib import Path
import re


def sub(path: str, pattern: str, replacement: str, label: str) -> None:
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    updated, count = re.subn(pattern, replacement, text, count=1, flags=re.S)
    if count != 1:
        raise SystemExit(f"{path}: {label}: expected one match, found {count}")
    p.write_text(updated, encoding="utf-8", newline="\n")
    print(f"patched {label}")


sub(
    "src/SdrplayProfile.cpp",
    r"(std::vector<std::string> windowsSoapyRoots\(\) \{\n    std::vector<std::string> roots;\n)",
    r'''\1
#ifdef _WIN32
    // The official API installer records its real install directory here.
    // Check both registry views so a 64-bit portable build also finds API
    // installations written by a 32-bit installer helper.
    const auto appendRegistryInstall = [&](REGSAM view) {
        HKEY key = nullptr;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                          "SOFTWARE\\SDRplay\\Service\\API",
                          0, KEY_READ | view, &key) != ERROR_SUCCESS || !key) return;
        char value[32768]{};
        DWORD type = 0;
        DWORD bytes = static_cast<DWORD>(sizeof(value));
        const LSTATUS status = RegQueryValueExA(
            key, "Install_Dir", nullptr, &type,
            reinterpret_cast<LPBYTE>(value), &bytes);
        RegCloseKey(key);
        if (status != ERROR_SUCCESS || bytes <= 1 ||
            (type != REG_SZ && type != REG_EXPAND_SZ)) return;
        std::string installDir(value);
        if (type == REG_EXPAND_SZ) {
            char expanded[32768]{};
            const DWORD n = ExpandEnvironmentStringsA(
                installDir.c_str(), expanded, static_cast<DWORD>(sizeof(expanded)));
            if (n > 0 && n <= sizeof(expanded)) installDir.assign(expanded);
        }
        appendRoot(roots, installDir);
    };
    appendRegistryInstall(KEY_WOW64_64KEY);
    appendRegistryInstall(KEY_WOW64_32KEY);
#endif
''',
    "SDRplay registry root",
)

sub(
    "src/DeviceManager.cpp",
    r"    bool apiPresent = false;\n    bool moduleLoaded = false;\n    bool deviceEnumerated = false;\n",
    '''    bool apiPresent = false;
    bool moduleLoaded = false;
    bool deviceEnumerated = false;
    std::string modulePath;
    std::string moduleError;
''',
    "SDRplay loader diagnostics locals",
)

sub(
    "src/DeviceManager.cpp",
    r"    static bool sdrplayModuleLoadAttempted = false;\n    static bool sdrplayModuleLoaded = false;\n    if \(!sdrplayModuleLoadAttempted\) \{.*?\n    \}\n    moduleLoaded = sdrplayModuleLoaded;",
    '''    static bool sdrplayModuleLoaded = false;
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
    moduleError = sdrplayModuleError;''',
    "SDRplay loader return handling",
)

sub(
    "src/DeviceManager.cpp",
    r"        deviceEnumerated = !results.empty\(\);\n        if \(deviceEnumerated\) moduleLoaded = true;",
    '''        deviceEnumerated = !results.empty();
        if (deviceEnumerated) {
            moduleLoaded = true;
            sdrplayModuleLoaded = true;
            if (sdrplayModulePath.empty())
                sdrplayModulePath = "registered SoapySDRPlay driver";
            modulePath = sdrplayModulePath;
            moduleError.clear();
        }''',
    "SDRplay enumeration success cache",
)

sub(
    "src/DeviceManager.cpp",
    r'''    if \(apiPresent && moduleLoaded && deviceEnumerated\) \{\n        sdrplaySetupStatus_ = "SDRplay: API and SoapySDRPlay module ready; RSP detected";\n    \} else if \(apiPresent && moduleLoaded\) \{\n        sdrplaySetupStatus_ = "SDRplay: API and SoapySDRPlay module loaded, but no RSP detected "\n                               "— check the API service, USB connection, and other SDR software";\n    \} else if \(!apiPresent && !moduleLoaded\) \{''',
    '''    if (apiPresent && moduleLoaded && deviceEnumerated) {
        sdrplaySetupStatus_ = "SDRplay: API and SoapySDRPlay module ready; RSP detected";
        if (!modulePath.empty()) sdrplaySetupStatus_ += " (" + modulePath + ")";
    } else if (apiPresent && moduleLoaded) {
        sdrplaySetupStatus_ = "SDRplay: API and SoapySDRPlay module loaded, but no RSP detected "
                               "— check the API service, USB connection, and other SDR software";
    } else if (apiPresent && !moduleLoaded && !moduleError.empty()) {
        sdrplaySetupStatus_ = "SDRplay: SoapySDRPlay module failed to load — " + moduleError +
                               " — install a matching 64-bit module, then Rescan";
    } else if (!apiPresent && !moduleLoaded) {''',
    "SDRplay actionable status",
)

sub(
    "src/SatcomScannerWidget.cpp",
    r"void SatcomScannerWidget::onArmSelected\(\) \{.*?\n\}\n\nvoid SatcomScannerWidget::onDisarm",
    '''void SatcomScannerWidget::onArmSelected() {
    const int row = passTable_->currentRow();
    if (row < 0) {
        QMessageBox::information(this, "Arm", "Select a pass row first.");
        return;
    }
    const QString satId = passTable_->item(row, 1)->data(Qt::UserRole).toString();
    const QString dlId = passTable_->item(row, 2)->data(Qt::UserRole).toString();

    auto& engine = SatcomScannerEngine::instance();
    std::string err;
    if (!engine.armPass(satId.toStdString(), dlId.toStdString(),
                        autoTrackCheck_->isChecked(), true, &err)) {
        QMessageBox::warning(this, "Arm", QString::fromStdString(err.empty() ? "Arm failed" : err));
        return;
    }
    refreshUi();
}

void SatcomScannerWidget::onDisarm''',
    "manual Arm Pass flow",
)

sub(
    "src/SatcomScannerWidget.cpp",
    r"void SatcomScannerWidget::onArmSstv\(\) \{.*?\n\}\n\nvoid SatcomScannerWidget::stopAutoCapture",
    '''void SatcomScannerWidget::onArmSstv() {
    auto& engine = SatcomScannerEngine::instance();
    std::string err;
    if (!engine.armPass("iss", "iss-sstv", autoTrackCheck_->isChecked(), true, &err)) {
        QMessageBox::warning(this, "ISS SSTV", QString::fromStdString(err.empty() ? "Arm failed" : err));
        return;
    }
    engine.startRecording();
    recordingUi_ = true;
    emit requestOpenSstvLive();
    refreshUi();
}

void SatcomScannerWidget::stopAutoCapture''',
    "manual ISS SSTV arm flow",
)

sub(
    "src/SatcomScannerEngine.cpp",
    r"bool SatcomScannerEngine::armPass\(const std::string& satId, const std::string& downlinkId,\n                                  bool autoTrack, bool force, std::string\* error\) \{.*?\n\}\n\nvoid SatcomScannerEngine::disarmPass",
    '''bool SatcomScannerEngine::armPass(const std::string& satId, const std::string& downlinkId,
                                  bool autoTrack, bool force, std::string* error) {
    finishSstvCapture(false);
    auto& deviceManager = DeviceManager::instance();
    std::string err;
    const size_t dev = config().deviceIndex;
    if (!deviceManager.acquireDeviceLease(dev, DeviceManager::DeviceLeaseOwner::Satcom, force, &err)) {
        if (error) *error = err;
        return false;
    }
    if (!SatPassPlanner::instance().arm(satId, downlinkId, autoTrack, &err)) {
        deviceManager.releaseDeviceLease(DeviceManager::DeviceLeaseOwner::Satcom);
        if (error) *error = err;
        return false;
    }
    const auto snap = SatPassPlanner::instance().snapshot();
    const std::string stem = makeCaptureStem(satId, snap.armed.downlinkId);
    {
        std::lock_guard<std::mutex> lk(mutex_);
        passTrackActive_ = true;
        armedRole_ = snap.armed.role;
        config_.mode = snap.armed.mode.empty() ? "NFM" : snap.armed.mode;
        if (config_.mode == "sstv" || snap.armed.role == "sstv") config_.mode = "NFM";
        if (snap.armed.role == "apt") {
            config_.mode = "APT";
            config_.bandwidthHz = 40e3;
        } else if (snap.armed.role == "aprs" || snap.armed.role == "sstv" ||
                   snap.armed.role == "voice") {
            config_.bandwidthHz = 15e3;
        } else {
            config_.bandwidthHz = std::min(config_.bandwidthHz, 25e3);
        }
        currentHz_ = snap.armed.freqHz;
        lockHz_ = snap.armed.freqHz;
        lastTrackHz_ = 0.0;
        recordSatId_ = satId;
        recordDownlinkId_ = snap.armed.downlinkId;
        QDir().mkpath(QString::fromStdString(config_.recordDir));
        recordPath_ = config_.recordDir + "/" + stem + ".f32";
        aptPreviewPath_.clear();
        if (snap.armed.role == "apt") {
            const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
                                 "/satcom_apt";
            QDir().mkpath(base);
            aptPreviewPath_ = QDir(base).filePath(QString::fromStdString(stem) + ".pgm").toStdString();
        }
        sstvOutputDir_.clear();
        lastStatus_ = "Pass armed";
        if (run_.load(std::memory_order_acquire)) state_ = SatcomScannerState::Locked;
        config_.save();
    }
    demodResetRequested_ = true;

    // Enter the locked pass path before the worker starts. This avoids a race
    // where the old UI started a band scan first and only armed the pass later.
    if (!run_.load(std::memory_order_acquire) && !start(force)) {
        std::string startError;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            startError = lastStatus_;
            passTrackActive_ = false;
            armedRole_.clear();
            state_ = SatcomScannerState::Idle;
            lastStatus_ = startError.empty() ? "Could not start pass receiver" : startError;
        }
        SatPassPlanner::instance().disarm();
        deviceManager.releaseDeviceLease(DeviceManager::DeviceLeaseOwner::Satcom);
        if (error) *error = startError.empty() ? "Could not start pass receiver" : startError;
        return false;
    }

    pushLog(SatcomLog::EventType::Lock, snap.armed.freqHz, "pass arm");
    tickPassTrack();
    if (snap.armed.role == "sstv") startSstvCapture(satId, snap.armed.downlinkId);
    return true;
}

void SatcomScannerEngine::disarmPass''',
    "engine self-starting arm transaction",
)

for temporary in (
    ".github/workflows/apply-rspdx-arm-hotfix.yml",
    ".github/workflows/apply-rspdx-arm-hotfix-v2.yml",
    ".github/workflows/apply-rspdx-arm-hotfix-v3.yml",
    "scripts/apply_rspdx_arm_hotfix.py",
):
    p = Path(temporary)
    if p.exists():
        p.unlink()
