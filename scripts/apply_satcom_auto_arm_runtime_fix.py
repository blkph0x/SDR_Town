from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected exactly one match, found {count}\n--- needle ---\n{old}")
    p.write_text(text.replace(old, new, 1), encoding="utf-8", newline="\n")


# DeviceManager: expose the logical/current RF centre so the satcom demodulator
# does not assume a queued tune has already reached hardware.
replace_once(
    "include/DeviceManager.h",
    "    double getCurrentSampleRate(size_t index) const;\n"
    "    void applyLiveSampleRate(size_t index, double sampleRateHz);",
    "    double getCurrentCenterFreq(size_t index) const;\n"
    "    double getCurrentSampleRate(size_t index) const;\n"
    "    void applyLiveSampleRate(size_t index, double sampleRateHz);",
)
replace_once(
    "src/DeviceManager.cpp",
    "double DeviceManager::getCurrentSampleRate(size_t index) const {",
    "double DeviceManager::getCurrentCenterFreq(size_t index) const {\n"
    "    auto* st = streamState(index);\n"
    "    if (!st) return 0.0;\n"
    "    std::lock_guard<std::mutex> lk(st->queueMutex);\n"
    "    return st->currentCenter;\n"
    "}\n\n"
    "double DeviceManager::getCurrentSampleRate(size_t index) const {",
)

# Satcom config/engine: persist automatic capture and stable device identity,
# resolve away stale/placeholder indices, and make armPass own stream startup.
replace_once(
    "include/SatcomScannerEngine.h",
    "    size_t deviceIndex = 0;\n"
    "    std::string recordDir;\n"
    "    std::string logDir;\n"
    "    bool enableAx25 = true;\n"
    "    bool enableApt = true;",
    "    size_t deviceIndex = 0;\n"
    "    std::string deviceStableKey;\n"
    "    std::string recordDir;\n"
    "    std::string logDir;\n"
    "    bool enableAx25 = true;\n"
    "    bool enableApt = true;\n"
    "    bool autoCapture = true;",
)
replace_once(
    "include/SatcomScannerEngine.h",
    "    void setConfig(const SatcomScannerConfig& cfg);\n"
    "    SatcomScannerConfig config() const;\n\n"
    "    bool start(bool force = false);",
    "    void setConfig(const SatcomScannerConfig& cfg);\n"
    "    SatcomScannerConfig config() const;\n"
    "    void setAutoCaptureEnabled(bool on);\n"
    "    bool autoCaptureEnabled() const;\n"
    "    size_t resolveDeviceIndex(std::string* error = nullptr);\n\n"
    "    bool start(bool force = false);",
)
replace_once(
    "src/SatcomScannerEngine.cpp",
    "std::string safeToken(std::string value) {\n"
    "    for (char& c : value) {\n"
    "        const unsigned char u = static_cast<unsigned char>(c);\n"
    "        if (!std::isalnum(u) && c != '-' && c != '_') c = '_';\n"
    "    }\n"
    "    if (value.empty()) value = \"unknown\";\n"
    "    return value;\n"
    "}\n",
    "std::string safeToken(std::string value) {\n"
    "    for (char& c : value) {\n"
    "        const unsigned char u = static_cast<unsigned char>(c);\n"
    "        if (!std::isalnum(u) && c != '-' && c != '_') c = '_';\n"
    "    }\n"
    "    if (value.empty()) value = \"unknown\";\n"
    "    return value;\n"
    "}\n\n"
    "bool isPlaceholderDevice(const DeviceInfo& device) {\n"
    "    std::string label = device.label;\n"
    "    std::transform(label.begin(), label.end(), label.begin(), [](unsigned char c) {\n"
    "        return static_cast<char>(std::tolower(c));\n"
    "    });\n"
    "    return label.find(\"placeholder\") != std::string::npos ||\n"
    "           label.find(\"(stub)\") != std::string::npos;\n"
    "}\n",
)
replace_once(
    "src/SatcomScannerEngine.cpp",
    "    j[\"deviceIndex\"] = deviceIndex;\n"
    "    j[\"recordDir\"] = recordDir;\n"
    "    j[\"logDir\"] = logDir;\n"
    "    j[\"enableAx25\"] = enableAx25;\n"
    "    j[\"enableApt\"] = enableApt;",
    "    j[\"deviceIndex\"] = deviceIndex;\n"
    "    j[\"deviceStableKey\"] = deviceStableKey;\n"
    "    j[\"recordDir\"] = recordDir;\n"
    "    j[\"logDir\"] = logDir;\n"
    "    j[\"enableAx25\"] = enableAx25;\n"
    "    j[\"enableApt\"] = enableApt;\n"
    "    j[\"autoCapture\"] = autoCapture;",
)
replace_once(
    "src/SatcomScannerEngine.cpp",
    "    c.deviceIndex = j.value(\"deviceIndex\", c.deviceIndex);\n"
    "    c.recordDir = j.value(\"recordDir\", c.recordDir);\n"
    "    c.logDir = j.value(\"logDir\", c.logDir);\n"
    "    c.enableAx25 = j.value(\"enableAx25\", c.enableAx25);\n"
    "    c.enableApt = j.value(\"enableApt\", c.enableApt);",
    "    c.deviceIndex = j.value(\"deviceIndex\", c.deviceIndex);\n"
    "    c.deviceStableKey = j.value(\"deviceStableKey\", c.deviceStableKey);\n"
    "    c.recordDir = j.value(\"recordDir\", c.recordDir);\n"
    "    c.logDir = j.value(\"logDir\", c.logDir);\n"
    "    c.enableAx25 = j.value(\"enableAx25\", c.enableAx25);\n"
    "    c.enableApt = j.value(\"enableApt\", c.enableApt);\n"
    "    c.autoCapture = j.value(\"autoCapture\", c.autoCapture);",
)
replace_once(
    "src/SatcomScannerEngine.cpp",
    "SatcomScannerConfig SatcomScannerEngine::config() const {\n"
    "    std::lock_guard<std::mutex> lk(mutex_);\n"
    "    return config_;\n"
    "}\n\n"
    "void SatcomScannerEngine::setUpdateCallback(std::function<void()> cb) {",
    "SatcomScannerConfig SatcomScannerEngine::config() const {\n"
    "    std::lock_guard<std::mutex> lk(mutex_);\n"
    "    return config_;\n"
    "}\n\n"
    "void SatcomScannerEngine::setAutoCaptureEnabled(bool on) {\n"
    "    std::lock_guard<std::mutex> lk(mutex_);\n"
    "    config_.autoCapture = on;\n"
    "    config_.save();\n"
    "}\n\n"
    "bool SatcomScannerEngine::autoCaptureEnabled() const {\n"
    "    std::lock_guard<std::mutex> lk(mutex_);\n"
    "    return config_.autoCapture;\n"
    "}\n\n"
    "size_t SatcomScannerEngine::resolveDeviceIndex(std::string* error) {\n"
    "    auto& manager = DeviceManager::instance();\n"
    "    const auto devices = manager.getDevices();\n"
    "    if (devices.empty()) {\n"
    "        if (error) *error = \"No SDR devices are available; rescan devices first\";\n"
    "        return static_cast<size_t>(-1);\n"
    "    }\n\n"
    "    SatcomScannerConfig cfg = config();\n"
    "    size_t chosen = static_cast<size_t>(-1);\n"
    "    if (!cfg.deviceStableKey.empty()) {\n"
    "        for (size_t i = 0; i < devices.size(); ++i) {\n"
    "            if (devices[i].stableKey == cfg.deviceStableKey) { chosen = i; break; }\n"
    "        }\n"
    "    }\n"
    "    if (chosen == static_cast<size_t>(-1) && cfg.deviceIndex < devices.size() &&\n"
    "        !isPlaceholderDevice(devices[cfg.deviceIndex])) {\n"
    "        chosen = cfg.deviceIndex;\n"
    "    }\n"
    "    auto choose = [&](auto predicate) {\n"
    "        if (chosen != static_cast<size_t>(-1)) return;\n"
    "        for (size_t i = 0; i < devices.size(); ++i) {\n"
    "            if (!isPlaceholderDevice(devices[i]) && predicate(i, devices[i])) {\n"
    "                chosen = i;\n"
    "                return;\n"
    "            }\n"
    "        }\n"
    "    };\n"
    "    choose([&](size_t i, const DeviceInfo&) { return manager.isStreaming(i); });\n"
    "    choose([](size_t, const DeviceInfo& d) { return d.enabled; });\n"
    "    choose([](size_t, const DeviceInfo& d) { return d.isSdrplay; });\n"
    "    choose([](size_t, const DeviceInfo&) { return true; });\n"
    "    if (chosen == static_cast<size_t>(-1))\n"
    "        chosen = cfg.deviceIndex < devices.size() ? cfg.deviceIndex : 0;\n\n"
    "    const std::string stableKey = devices[chosen].stableKey;\n"
    "    if (cfg.deviceIndex != chosen || cfg.deviceStableKey != stableKey) {\n"
    "        cfg.deviceIndex = chosen;\n"
    "        cfg.deviceStableKey = stableKey;\n"
    "        setConfig(cfg);\n"
    "    }\n"
    "    return chosen;\n"
    "}\n\n"
    "void SatcomScannerEngine::setUpdateCallback(std::function<void()> cb) {",
)
replace_once(
    "src/SatcomScannerEngine.cpp",
    "bool SatcomScannerEngine::start(bool force) {\n"
    "    std::string err;\n"
    "    const size_t dev = config().deviceIndex;\n"
    "    if (!DeviceManager::instance().acquireDeviceLease(dev, DeviceManager::DeviceLeaseOwner::Satcom, force, &err)) {",
    "bool SatcomScannerEngine::start(bool force) {\n"
    "    std::string err;\n"
    "    const size_t dev = resolveDeviceIndex(&err);\n"
    "    if (dev == static_cast<size_t>(-1)) {\n"
    "        std::lock_guard<std::mutex> lk(mutex_);\n"
    "        lastStatus_ = err.empty() ? \"No receiver selected\" : err;\n"
    "        return false;\n"
    "    }\n"
    "    if (!DeviceManager::instance().acquireDeviceLease(dev, DeviceManager::DeviceLeaseOwner::Satcom, force, &err)) {",
)
replace_once(
    "src/SatcomScannerEngine.cpp",
    "    auto& deviceManager = DeviceManager::instance();\n"
    "    std::string err;\n"
    "    const size_t dev = config().deviceIndex;\n"
    "    if (!deviceManager.acquireDeviceLease(dev, DeviceManager::DeviceLeaseOwner::Satcom, force, &err)) {",
    "    auto& deviceManager = DeviceManager::instance();\n"
    "    std::string err;\n"
    "    const size_t dev = resolveDeviceIndex(&err);\n"
    "    if (dev == static_cast<size_t>(-1)) {\n"
    "        if (error) *error = err.empty() ? \"No receiver selected\" : err;\n"
    "        return false;\n"
    "    }\n"
    "    const bool streamWasRunning = deviceManager.isStreaming(dev);\n"
    "    if (!deviceManager.acquireDeviceLease(dev, DeviceManager::DeviceLeaseOwner::Satcom, force, &err)) {",
)
replace_once(
    "src/SatcomScannerEngine.cpp",
    "    demodResetRequested_ = true;\n\n"
    "    // Always issue the base-frequency tune, even when Doppler auto-track is",
    "    demodResetRequested_ = true;\n\n"
    "    // Arm Pass owns the complete receiver transition. Start the selected\n"
    "    // stream before queuing the tune so both manual and automatic paths enter\n"
    "    // the same locked/decode state instead of merely changing planner flags.\n"
    "    if (!deviceManager.setEnabled(dev, true) || !deviceManager.startStreaming(dev, true)) {\n"
    "        const std::string startError = \"Could not start selected satellite receiver\";\n"
    "        {\n"
    "            std::lock_guard<std::mutex> lk(mutex_);\n"
    "            passTrackActive_ = false;\n"
    "            armedRole_.clear();\n"
    "            state_ = SatcomScannerState::Idle;\n"
    "            lastStatus_ = startError;\n"
    "        }\n"
    "        SatPassPlanner::instance().disarm();\n"
    "        deviceManager.releaseDeviceLease(DeviceManager::DeviceLeaseOwner::Satcom);\n"
    "        if (!streamWasRunning) deviceManager.stopStreaming(dev);\n"
    "        if (error) *error = startError;\n"
    "        return false;\n"
    "    }\n\n"
    "    // Always issue the base-frequency tune, even when Doppler auto-track is",
)
replace_once(
    "src/SatcomScannerEngine.cpp",
    "        SatPassPlanner::instance().disarm();\n"
    "        deviceManager.releaseDeviceLease(DeviceManager::DeviceLeaseOwner::Satcom);\n"
    "        if (error) *error = tuneError;\n"
    "        return false;",
    "        SatPassPlanner::instance().disarm();\n"
    "        deviceManager.releaseDeviceLease(DeviceManager::DeviceLeaseOwner::Satcom);\n"
    "        if (!streamWasRunning) deviceManager.stopStreaming(dev);\n"
    "        if (error) *error = tuneError;\n"
    "        return false;",
)
replace_once(
    "src/SatcomScannerEngine.cpp",
    "        SatPassPlanner::instance().disarm();\n"
    "        deviceManager.releaseDeviceLease(DeviceManager::DeviceLeaseOwner::Satcom);\n"
    "        if (error) *error = startError.empty() ? \"Could not start pass receiver\" : startError;\n"
    "        return false;",
    "        SatPassPlanner::instance().disarm();\n"
    "        deviceManager.releaseDeviceLease(DeviceManager::DeviceLeaseOwner::Satcom);\n"
    "        if (!streamWasRunning) deviceManager.stopStreaming(dev);\n"
    "        if (error) *error = startError.empty() ? \"Could not start pass receiver\" : startError;\n"
    "        return false;",
)
replace_once(
    "src/SatcomScannerEngine.cpp",
    "    if (demodResetRequested_.exchange(false)) demod_->resetState();\n"
    "    double rms = -120.0;\n",
    "    if (demodResetRequested_.exchange(false)) demod_->resetState();\n"
    "    const double reportedCenter = mgr.getCurrentCenterFreq(dev);\n"
    "    const double iqCenterHz = reportedCenter > 0.0 ? reportedCenter : lockHz;\n"
    "    double rms = -120.0;\n",
)
replace_once(
    "src/SatcomScannerEngine.cpp",
    "    auto audio = demod_->demodulateToAudio(iq, sr, lockHz, lockHz, modeFromString(mode),",
    "    auto audio = demod_->demodulateToAudio(iq, sr, iqCenterHz, lockHz, modeFromString(mode),",
)
replace_once(
    "src/SatcomScannerEngine.cpp",
    "    mgr.setEnabled(config().deviceIndex, true);\n"
    "    mgr.startStreaming(config().deviceIndex, true);\n\n"
    "    while (run_.load(std::memory_order_acquire)) {",
    "    if (!mgr.setEnabled(config().deviceIndex, true) ||\n"
    "        !mgr.startStreaming(config().deviceIndex, true)) {\n"
    "        std::lock_guard<std::mutex> lk(mutex_);\n"
    "        deviceConnected_ = false;\n"
    "        lastStatus_ = \"Could not start selected receiver\";\n"
    "        state_ = SatcomScannerState::Idle;\n"
    "        run_ = false;\n"
    "        mgr.releaseDeviceLease(DeviceManager::DeviceLeaseOwner::Satcom);\n"
    "        return;\n"
    "    }\n\n"
    "    while (run_.load(std::memory_order_acquire)) {",
)

# Satcom UI: explicit receiver selector, persisted automatic-capture setting,
# and no duplicate foreground-only auto-arm state machine.
replace_once(
    "include/SatcomScannerWidget.h",
    "    QComboBox* modeCombo_ = nullptr;\n"
    "    QComboBox* presetCombo_ = nullptr;",
    "    QComboBox* modeCombo_ = nullptr;\n"
    "    QComboBox* deviceCombo_ = nullptr;\n"
    "    QComboBox* presetCombo_ = nullptr;",
)
replace_once(
    "src/SatcomScannerWidget.cpp",
    "#include \"AdsBTrackStore.h\"\n",
    "#include \"AdsBTrackStore.h\"\n"
    "#include \"DeviceManager.h\"\n",
)
replace_once(
    "src/SatcomScannerWidget.cpp",
    "    modeCombo_ = new QComboBox();\n"
    "    modeCombo_->addItems({\"NFM\", \"WFM\", \"AM\", \"USB\", \"APT\", \"APRS\"});\n"
    "    stepSpin_ = new QDoubleSpinBox();",
    "    modeCombo_ = new QComboBox();\n"
    "    modeCombo_->addItems({\"NFM\", \"WFM\", \"AM\", \"USB\", \"APT\", \"APRS\"});\n"
    "    deviceCombo_ = new QComboBox();\n"
    "    stepSpin_ = new QDoubleSpinBox();",
)
replace_once(
    "src/SatcomScannerWidget.cpp",
    "    optsLay->addRow(\"BANDWIDTH\", bwSpin_);\n"
    "    optsLay->addRow(\"MODE\", modeCombo_);",
    "    optsLay->addRow(\"RECEIVER\", deviceCombo_);\n"
    "    optsLay->addRow(\"BANDWIDTH\", bwSpin_);\n"
    "    optsLay->addRow(\"MODE\", modeCombo_);",
)
replace_once(
    "src/SatcomScannerWidget.cpp",
    "    autoCaptureCheck_ = new QCheckBox(\"Auto capture selected sats in range\");\n"
    "    autoCaptureCheck_->setChecked(true);\n"
    "    autoCaptureCheck_->setToolTip(\n"
    "        \"While this Satcom panel is visible, arm the best selected in-range satellite, \"\n"
    "        \"track Doppler, save detected audio, and run available APRS/APT/SSTV paths. \"\n"
    "        \"It will not force the tuner away from another active owner.\");",
    "    autoCaptureCheck_ = new QCheckBox(\"Auto capture selected sats in range\");\n"
    "    autoCaptureCheck_->setChecked(SatcomScannerEngine::instance().autoCaptureEnabled());\n"
    "    autoCaptureCheck_->setToolTip(\n"
    "        \"Runs in the background: when a selected, supported satellite reaches the configured \"\n"
    "        \"minimum elevation, SDR Town takes the selected receiver, tunes with Doppler, records \"\n"
    "        \"when a signal is present, and runs the available APRS/APT/SSTV decoder.\");",
)
replace_once(
    "src/SatcomScannerWidget.cpp",
    "    connect(autoCaptureCheck_, &QCheckBox::toggled, this, [this](bool on) {\n"
    "        if (!on && autoCaptureOwned_) stopAutoCapture(false);\n"
    "        if (on) refreshPassesTable();\n"
    "    });",
    "    connect(autoCaptureCheck_, &QCheckBox::toggled, this, [this](bool on) {\n"
    "        SatcomScannerEngine::instance().setAutoCaptureEnabled(on);\n"
    "        if (!on && autoCaptureOwned_) stopAutoCapture(false);\n"
    "        refreshPassesTable();\n"
    "    });\n"
    "    connect(deviceCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int row) {\n"
    "        if (row < 0) return;\n"
    "        bool ok = false;\n"
    "        const size_t index = static_cast<size_t>(deviceCombo_->itemData(row).toString().toULongLong(&ok));\n"
    "        const auto devices = DeviceManager::instance().getDevices();\n"
    "        if (!ok || index >= devices.size()) return;\n"
    "        auto cfg = SatcomScannerEngine::instance().config();\n"
    "        cfg.deviceIndex = index;\n"
    "        cfg.deviceStableKey = devices[index].stableKey;\n"
    "        SatcomScannerEngine::instance().setConfig(cfg);\n"
    "    });",
)
replace_once(
    "src/SatcomScannerWidget.cpp",
    "    cfg.mode = modeCombo_->currentText().toStdString();\n"
    "    cfg.squelchDb = squelchSpin_->value();\n"
    "    SatcomScannerEngine::instance().setConfig(cfg);",
    "    cfg.mode = modeCombo_->currentText().toStdString();\n"
    "    cfg.squelchDb = squelchSpin_->value();\n"
    "    if (deviceCombo_ && deviceCombo_->currentIndex() >= 0) {\n"
    "        bool ok = false;\n"
    "        const size_t index = static_cast<size_t>(deviceCombo_->currentData().toString().toULongLong(&ok));\n"
    "        const auto devices = DeviceManager::instance().getDevices();\n"
    "        if (ok && index < devices.size()) {\n"
    "            cfg.deviceIndex = index;\n"
    "            cfg.deviceStableKey = devices[index].stableKey;\n"
    "        }\n"
    "    }\n"
    "    SatcomScannerEngine::instance().setConfig(cfg);",
)
replace_once(
    "src/SatcomScannerWidget.cpp",
    "    auto& engine = SatcomScannerEngine::instance();\n"
    "    std::string err;\n"
    "    if (!engine.armPass(satId.toStdString(), dlId.toStdString(),",
    "    applyFieldsToConfig();\n"
    "    auto& engine = SatcomScannerEngine::instance();\n"
    "    std::string err;\n"
    "    if (!engine.armPass(satId.toStdString(), dlId.toStdString(),",
)
replace_once(
    "src/SatcomScannerWidget.cpp",
    "void SatcomScannerWidget::onArmSstv() {\n"
    "    auto& engine = SatcomScannerEngine::instance();",
    "void SatcomScannerWidget::onArmSstv() {\n"
    "    applyFieldsToConfig();\n"
    "    auto& engine = SatcomScannerEngine::instance();",
)
replace_once(
    "src/SatcomScannerWidget.cpp",
    "\n    updateAutoCapture(plan);\n}\n\nvoid SatcomScannerWidget::refreshUi() {",
    "\n}\n\nvoid SatcomScannerWidget::refreshUi() {",
)
replace_once(
    "src/SatcomScannerWidget.cpp",
    "void SatcomScannerWidget::refreshUi() {\n"
    "    // Doppler retune is driven by the scanner worker only while a pass is armed.\n"
    "    const auto snap = SatcomScannerEngine::instance().snapshot();",
    "void SatcomScannerWidget::refreshUi() {\n"
    "    // Resolve stale saved indices (for example an RTL placeholder at index 0)\n"
    "    // before presenting or starting the satcom receiver.\n"
    "    SatcomScannerEngine::instance().resolveDeviceIndex(nullptr);\n"
    "    const auto snap = SatcomScannerEngine::instance().snapshot();",
)
replace_once(
    "src/SatcomScannerWidget.cpp",
    "    static int passTableThrottle = 0;\n"
    "    const bool refreshPasses = (++passTableThrottle % 4) == 0; // ~2 s while visible\n\n"
    "    if (!lowSpin_->hasFocus()) lowSpin_->setValue(cfg.lowHz / 1e6);",
    "    static int passTableThrottle = 0;\n"
    "    const bool refreshPasses = (++passTableThrottle % 4) == 0; // ~2 s while visible\n\n"
    "    const auto devices = DeviceManager::instance().getDevices();\n"
    "    bool rebuildDevices = deviceCombo_->count() != static_cast<int>(devices.size());\n"
    "    if (!rebuildDevices) {\n"
    "        for (int i = 0; i < deviceCombo_->count(); ++i) {\n"
    "            const QString expected = QString(\"%1 — %2\")\n"
    "                .arg(i).arg(QString::fromStdString(devices[static_cast<size_t>(i)].label));\n"
    "            if (deviceCombo_->itemText(i) != expected) { rebuildDevices = true; break; }\n"
    "        }\n"
    "    }\n"
    "    deviceCombo_->blockSignals(true);\n"
    "    if (rebuildDevices) {\n"
    "        deviceCombo_->clear();\n"
    "        for (size_t i = 0; i < devices.size(); ++i) {\n"
    "            deviceCombo_->addItem(\n"
    "                QString(\"%1 — %2\").arg(static_cast<qulonglong>(i))\n"
    "                    .arg(QString::fromStdString(devices[i].label)),\n"
    "                QString::number(static_cast<qulonglong>(i)));\n"
    "        }\n"
    "    }\n"
    "    const int selectedDevice = deviceCombo_->findData(\n"
    "        QString::number(static_cast<qulonglong>(cfg.deviceIndex)));\n"
    "    if (selectedDevice >= 0) deviceCombo_->setCurrentIndex(selectedDevice);\n"
    "    deviceCombo_->blockSignals(false);\n\n"
    "    if (!lowSpin_->hasFocus()) lowSpin_->setValue(cfg.lowHz / 1e6);",
)

# Background controller lives in the always-present hub, not in a visible tab.
Path("include/SatcomHubWidget.h").write_text(r'''#pragma once

#include <QString>
#include <QWidget>

#include <cstddef>

class QTabWidget;
class QTimer;
class QShowEvent;
class QHideEvent;
class SatcomScannerWidget;
class InmarsatWidget;
class AircraftMapWidget;

// Single SDR Town dock panel: Satcom | Inmarsat | Aircraft (no floating windows).
// Satellite auto-capture is intentionally owned here so it remains active while
// the tab/dock is hidden and before the heavy child tabs are first constructed.
class SatcomHubWidget : public QWidget {
    Q_OBJECT
public:
    explicit SatcomHubWidget(QWidget* parent = nullptr);

    SatcomScannerWidget* satcom() const { return satcom_; }
    InmarsatWidget* inmarsat() const { return inmarsat_; }
    AircraftMapWidget* aircraft() const { return aircraft_; }

    void showSatcomTab();
    void showInmarsatTab();
    void showAircraftTab();

signals:
    void requestOpenSstvLive();

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private slots:
    void autoCaptureTick();

private:
    void ensureTabs();
    void stopAutoCapture(bool keepHandledKey);

    QTabWidget* tabs_ = nullptr;
    QTimer* autoCaptureTimer_ = nullptr;
    SatcomScannerWidget* satcom_ = nullptr;
    InmarsatWidget* inmarsat_ = nullptr;
    AircraftMapWidget* aircraft_ = nullptr;

    QString autoCapturePassKey_;
    qint64 autoCaptureRetryAfter_ = 0;
    bool autoCaptureOwned_ = false;
    bool autoEngineWasRunning_ = false;
    size_t autoDeviceIndex_ = static_cast<size_t>(-1);
    double autoPreviousCenterHz_ = 0.0;
    int autoPreviousLeaseOwner_ = 0;
};
''', encoding="utf-8", newline="\n")

Path("src/SatcomHubWidget.cpp").write_text(r'''#include "SatcomHubWidget.h"
#include "SatcomScannerWidget.h"
#include "SatcomScannerEngine.h"
#include "SatPassPlanner.h"
#include "DeviceManager.h"
#include "InmarsatWidget.h"
#include "InmarsatEngine.h"
#include "AircraftMapWidget.h"

#include <QDateTime>
#include <QHideEvent>
#include <QShowEvent>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>
#include <limits>

namespace {

int autoCapturePriority(const std::string& role) {
    if (role == "sstv") return 0;
    if (role == "apt") return 1;
    if (role == "aprs") return 2;
    if (role == "data") return 3;
    if (role == "voice") return 4;
    return 5;
}

} // namespace

SatcomHubWidget::SatcomHubWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    tabs_ = new QTabWidget(this);
    tabs_->setDocumentMode(true);
    root->addWidget(tabs_);

    // A lightweight controller runs even before the visual tabs are created.
    // It only starts hardware when a selected, supported satellite is in range.
    autoCaptureTimer_ = new QTimer(this);
    autoCaptureTimer_->setInterval(1000);
    connect(autoCaptureTimer_, &QTimer::timeout, this, &SatcomHubWidget::autoCaptureTick);
    autoCaptureTimer_->start();
}

void SatcomHubWidget::ensureTabs() {
    if (satcom_) return;

    satcom_ = new SatcomScannerWidget(tabs_);
    satcom_->setWindowFlags(Qt::Widget);
    inmarsat_ = new InmarsatWidget(tabs_);
    inmarsat_->setWindowFlags(Qt::Widget);
    aircraft_ = new AircraftMapWidget(tabs_);
    aircraft_->setEmbedded(true);

    tabs_->addTab(satcom_, "Satcom");
    tabs_->addTab(inmarsat_, "Inmarsat");
    tabs_->addTab(aircraft_, "Aircraft");

    connect(satcom_, &SatcomScannerWidget::requestOpenSstvLive,
            this, &SatcomHubWidget::requestOpenSstvLive);
}

void SatcomHubWidget::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    ensureTabs();
}

void SatcomHubWidget::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    auto& engine = SatcomScannerEngine::instance();
    const auto snap = engine.snapshot();
    // Keep an armed manual or automatic pass alive in the background. Ordinary
    // band scanning still stops when the hub is hidden.
    if (!autoCaptureOwned_ && !snap.passArmed) {
        engine.stopRecording();
        engine.stop();
        engine.disarmPass();
    }
    InmarsatEngine::instance().stop();
}

void SatcomHubWidget::stopAutoCapture(bool keepHandledKey) {
    auto& engine = SatcomScannerEngine::instance();
    auto& manager = DeviceManager::instance();
    engine.stopRecording();
    engine.disarmPass();
    if (autoEngineWasRunning_) engine.skip();
    else engine.stop();

    // If auto-capture temporarily took a live listening receiver, put it back
    // on the exact centre frequency it had before AOS and restore listen lease
    // semantics. Never restore over a P25/Inmarsat/Aircraft owner.
    if (!autoEngineWasRunning_ && autoDeviceIndex_ != static_cast<size_t>(-1) &&
        std::isfinite(autoPreviousCenterHz_) && autoPreviousCenterHz_ > 0.0) {
        auto previous = static_cast<DeviceManager::DeviceLeaseOwner>(autoPreviousLeaseOwner_);
        if (previous == DeviceManager::DeviceLeaseOwner::None ||
            previous == DeviceManager::DeviceLeaseOwner::Satcom) {
            previous = DeviceManager::DeviceLeaseOwner::Listen;
        }
        std::string ignored;
        manager.retuneWithLease(autoDeviceIndex_, autoPreviousCenterHz_, previous, true, &ignored);
    }

    autoCaptureOwned_ = false;
    autoEngineWasRunning_ = false;
    autoDeviceIndex_ = static_cast<size_t>(-1);
    autoPreviousCenterHz_ = 0.0;
    autoPreviousLeaseOwner_ = 0;
    if (!keepHandledKey) autoCapturePassKey_.clear();
}

void SatcomHubWidget::autoCaptureTick() {
    auto& engine = SatcomScannerEngine::instance();
    if (!engine.autoCaptureEnabled()) {
        if (autoCaptureOwned_) stopAutoCapture(false);
        return;
    }

    const auto plan = SatPassPlanner::instance().snapshot();
    const qint64 now = QDateTime::currentSecsSinceEpoch();

    bool handledStillInRange = false;
    for (const auto& position : plan.positions) {
        const QString key = QString::fromStdString(position.satId + "/" + position.downlinkId);
        if (position.tleValid && position.inRange && key == autoCapturePassKey_) {
            handledStillInRange = true;
            break;
        }
    }
    if (!handledStillInRange && !plan.armed.armed && !autoCaptureOwned_)
        autoCapturePassKey_.clear();

    if (plan.armed.armed) {
        if (!autoCaptureOwned_) return; // never replace a manual arm
        const auto scan = engine.snapshot();
        const bool signalPresent = std::isfinite(scan.audioRmsDb) &&
                                   scan.audioRmsDb >= scan.config.squelchDb;
        if (signalPresent && scan.state != SatcomScannerState::Recording)
            engine.startRecording();
        return;
    }

    if (autoCaptureOwned_) {
        stopAutoCapture(true);
        return;
    }
    if (now < autoCaptureRetryAfter_) return;

    const SatCurrentPosition* best = nullptr;
    double bestScore = -std::numeric_limits<double>::infinity();
    for (const auto& position : plan.positions) {
        if (!position.tleValid || !position.inRange || !position.armable ||
            position.downlinkId.empty() || position.freqHz <= 0.0) {
            continue;
        }
        const QString key = QString::fromStdString(position.satId + "/" + position.downlinkId);
        if (key == autoCapturePassKey_) continue;
        const double score = 10000.0 - 1000.0 * autoCapturePriority(position.role) +
                             position.elevationDeg;
        if (!best || score > bestScore) {
            best = &position;
            bestScore = score;
        }
    }
    if (!best) return;

    auto& manager = DeviceManager::instance();
    const auto currentOwner = manager.deviceLeaseOwner();
    if (currentOwner == DeviceManager::DeviceLeaseOwner::P25 ||
        currentOwner == DeviceManager::DeviceLeaseOwner::Inmarsat ||
        currentOwner == DeviceManager::DeviceLeaseOwner::Aircraft) {
        autoCaptureRetryAfter_ = now + 10;
        return;
    }

    std::string deviceError;
    const size_t deviceIndex = engine.resolveDeviceIndex(&deviceError);
    if (deviceIndex == static_cast<size_t>(-1)) {
        autoCaptureRetryAfter_ = now + 15;
        return;
    }

    const auto before = engine.snapshot();
    autoEngineWasRunning_ = before.state != SatcomScannerState::Idle;
    autoDeviceIndex_ = deviceIndex;
    autoPreviousCenterHz_ = manager.getCurrentCenterFreq(deviceIndex);
    autoPreviousLeaseOwner_ = static_cast<int>(currentOwner);

    std::string error;
    if (!engine.armPass(best->satId, best->downlinkId, true, true, &error)) {
        autoEngineWasRunning_ = false;
        autoDeviceIndex_ = static_cast<size_t>(-1);
        autoPreviousCenterHz_ = 0.0;
        autoPreviousLeaseOwner_ = 0;
        autoCaptureRetryAfter_ = now + 15;
        return;
    }

    autoCaptureOwned_ = true;
    autoCapturePassKey_ = QString::fromStdString(best->satId + "/" + best->downlinkId);
}

void SatcomHubWidget::showSatcomTab() {
    ensureTabs();
    tabs_->setCurrentWidget(satcom_);
}

void SatcomHubWidget::showInmarsatTab() {
    ensureTabs();
    tabs_->setCurrentWidget(inmarsat_);
}

void SatcomHubWidget::showAircraftTab() {
    ensureTabs();
    tabs_->setCurrentWidget(aircraft_);
}
''', encoding="utf-8", newline="\n")

# Regression: a lease retune must immediately expose the logical centre used by
# downstream demodulators, even while a real hardware open/tune is asynchronous.
replace_once(
    "tests/test_devicemanager.cpp",
    "    REQUIRE(mgr.acquireDeviceLease(0, DeviceManager::DeviceLeaseOwner::Satcom, true, &err));\n"
    "    mgr.releaseDeviceLease(DeviceManager::DeviceLeaseOwner::Satcom);",
    "    REQUIRE(mgr.acquireDeviceLease(0, DeviceManager::DeviceLeaseOwner::Satcom, true, &err));\n"
    "    REQUIRE(mgr.retuneWithLease(0, 145.8e6, DeviceManager::DeviceLeaseOwner::Satcom, true, &err));\n"
    "    REQUIRE(mgr.getCurrentCenterFreq(0) == 145.8e6);\n"
    "    mgr.releaseDeviceLease(DeviceManager::DeviceLeaseOwner::Satcom);",
)

print("satcom automatic/manual arm runtime patch applied")
