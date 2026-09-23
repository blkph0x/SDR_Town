#include "SatcomScannerWidget.h"
#include "SatcomScannerEngine.h"
#include "SatCatalogueDialog.h"
#include "SatPassPlanner.h"
#include "SpectrumWidget.h"
#include "ObserverMapWidget.h"
#include "AdsBTrackStore.h"
#include "DeviceManager.h"
#include "SdrDeviceCandidate.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHideEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QShowEvent>
#include <QSlider>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
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

QString receiverDisplayText(size_t index, const DeviceInfo& device, bool streaming) {
    const QString state = streaming ? "LIVE"
        : (device.enabled ? "READY"
           : (SdrDeviceCandidate::isDeferredHardwareProxyLabel(device.label)
                  ? "PROBE ON START" : "AVAILABLE"));
    return QString("%1 — %2 [%3]")
        .arg(static_cast<qulonglong>(index))
        .arg(QString::fromStdString(device.label), state);
}

} // namespace

SatcomScannerWidget::SatcomScannerWidget(QWidget* parent)
    : QWidget(parent)
{
    buildUi();
    applyNeonStyle();
    refreshTimer_ = new QTimer(this);
    connect(refreshTimer_, &QTimer::timeout, this, &SatcomScannerWidget::refreshUi);

    SatcomScannerEngine::instance().setUpdateCallback([this]() {
        QMetaObject::invokeMethod(this, "refreshUi", Qt::QueuedConnection);
    });
    SatPassPlanner::instance().setUpdateCallback([this]() {
        QMetaObject::invokeMethod(this, [this]() {
            refreshPassesTable();
            refreshUi();
        }, Qt::QueuedConnection);
    });
}

void SatcomScannerWidget::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    if (refreshTimer_ && !refreshTimer_->isActive()) refreshTimer_->start(500);
    SatPassPlanner::instance().ensureTleLoaded();
    refreshPassesTable();
    refreshUi();
}

void SatcomScannerWidget::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    if (refreshTimer_) refreshTimer_->stop();
    if (autoCaptureOwned_) stopAutoCapture(false);
}

SatcomScannerWidget::~SatcomScannerWidget() {
    SatcomScannerEngine::instance().setUpdateCallback({});
    SatPassPlanner::instance().setUpdateCallback({});
}

void SatcomScannerWidget::applyNeonStyle() {
    setStyleSheet(R"(
        SatcomScannerWidget, QWidget#satcomRoot {
            background: #000000; color: #39FF14;
            font-family: Consolas, "Courier New", monospace;
        }
        QLabel { color: #39FF14; }
        QGroupBox, QFrame#satcomBox {
            border: 1px solid #39FF14; border-radius: 10px; margin-top: 6px; padding: 8px;
            background: #050805;
        }
        QDoubleSpinBox, QComboBox, QPlainTextEdit, QLineEdit, QTableWidget {
            background: #0a0f0a; color: #39FF14; border: 1px solid #1f3d1f; border-radius: 6px;
            padding: 4px; font-size: 13px; font-weight: 700;
        }
        QHeaderView::section { background: #0a0f0a; color: #39FF14; border: 1px solid #1f3d1f; }
        QPushButton {
            background: #0c160c; color: #39FF14; border: 2px solid #39FF14; border-radius: 10px;
            padding: 8px 12px; font-weight: 800; min-width: 90px;
        }
        QPushButton:hover { background: #132213; }
        QPushButton:disabled { color: #2a5a2a; border-color: #1a331a; }
        QSlider::groove:horizontal { height: 6px; background: #1a331a; border-radius: 3px; }
        QSlider::handle:horizontal { background: #39FF14; width: 14px; margin: -5px 0; border-radius: 7px; }
        QCheckBox { color: #39FF14; }
    )");
}

void SatcomScannerWidget::buildUi() {
    setObjectName("satcomRoot");
    auto* root = new QVBoxLayout(this);

    auto* title = new QLabel("SATCOM SCANNER");
    title->setStyleSheet("font-size: 22px; font-weight: 900; letter-spacing: 2px;");
    root->addWidget(title);

    auto* top = new QHBoxLayout();
    auto makeFrequency = [&](const QString& label, QDoubleSpinBox*& spin) {
        auto* box = new QFrame();
        box->setObjectName("satcomBox");
        auto* layout = new QVBoxLayout(box);
        layout->addWidget(new QLabel(label));
        spin = new QDoubleSpinBox();
        spin->setDecimals(3);
        spin->setRange(0.1, 6000.0);
        spin->setSuffix(" MHz");
        layout->addWidget(spin);
        top->addWidget(box);
    };
    makeFrequency("LOW FREQUENCY", lowSpin_);
    makeFrequency("HIGH FREQUENCY", highSpin_);

    auto* options = new QFrame();
    options->setObjectName("satcomBox");
    auto* optionsLayout = new QFormLayout(options);
    bwSpin_ = new QDoubleSpinBox();
    bwSpin_->setDecimals(1);
    bwSpin_->setRange(1.0, 2000.0);
    bwSpin_->setSuffix(" kHz");
    modeCombo_ = new QComboBox();
    modeCombo_->addItems({"NFM", "WFM", "AM", "USB", "LSB", "APT", "APRS"});
    deviceCombo_ = new QComboBox();
    deviceCombo_->setToolTip(
        "Choose the SDR used by Satcom. Select a second receiver to keep the main Listen receiver "
        "running. If you choose the active Listen receiver, START/ARM automatically takes it over "
        "and restores it when Satcom stops. P25 is never interrupted.");
    stepSpin_ = new QDoubleSpinBox();
    stepSpin_->setDecimals(3);
    stepSpin_->setSuffix(" kHz");
    presetCombo_ = new QComboBox();
    optionsLayout->addRow("SATCOM RX DEVICE", deviceCombo_);
    optionsLayout->addRow("BANDWIDTH", bwSpin_);
    optionsLayout->addRow("MODE", modeCombo_);
    optionsLayout->addRow("STEP", stepSpin_);
    optionsLayout->addRow("PRESET", presetCombo_);
    top->addWidget(options, 1);
    root->addLayout(top);

    auto* observerBox = new QFrame();
    observerBox->setObjectName("satcomBox");
    auto* observerLayout = new QGridLayout(observerBox);
    observerLayout->addWidget(new QLabel("HOME LAT (+N/-S or 33.8S)"), 0, 0);
    latEdit_ = new QLineEdit("-33.87");
    observerLayout->addWidget(latEdit_, 0, 1);
    observerLayout->addWidget(new QLabel("HOME LON (+E/-W or 151.2E)"), 0, 2);
    lonEdit_ = new QLineEdit("151.21");
    observerLayout->addWidget(lonEdit_, 0, 3);
    observerLayout->addWidget(new QLabel("ALT m"), 1, 0);
    altSpin_ = new QDoubleSpinBox();
    altSpin_->setRange(-500.0, 9000.0);
    altSpin_->setValue(50.0);
    observerLayout->addWidget(altSpin_, 1, 1);
    observerLayout->addWidget(new QLabel("MIN EL °"), 1, 2);
    minElSpin_ = new QDoubleSpinBox();
    minElSpin_->setRange(0.0, 90.0);
    minElSpin_->setValue(10.0);
    observerLayout->addWidget(minElSpin_, 1, 3);
    auto* applyObserver = new QPushButton("Apply location");
    applyObserver->setObjectName("applySatcomLocationButton");
    applyObserver->setToolTip("Save the observer location and recalculate satellite passes immediately.");
    auto* selectSatellites = new QPushButton("Select satellites…");
    refreshTleBtn_ = new QPushButton("Refresh TLE");
    observerLayout->addWidget(applyObserver, 2, 0);
    observerLayout->addWidget(selectSatellites, 2, 1);
    observerLayout->addWidget(refreshTleBtn_, 2, 2);
    tleAgeLabel_ = new QLabel("TLE: —");
    observerLayout->addWidget(tleAgeLabel_, 2, 3);
    observerMap_ = new ObserverMapWidget(this);
    observerMap_->setMinimumHeight(280);
    observerLayout->addWidget(observerMap_, 3, 0, 1, 4);
    root->addWidget(observerBox);
    connect(applyObserver, &QPushButton::clicked, this, &SatcomScannerWidget::onApplyObserver);
    connect(selectSatellites, &QPushButton::clicked, this, &SatcomScannerWidget::onSelectSats);
    connect(refreshTleBtn_, &QPushButton::clicked, this, &SatcomScannerWidget::onRefreshTle);

    auto* passRow = new QHBoxLayout();
    passTable_ = new QTableWidget(0, 6);
    passTable_->setHorizontalHeaderLabels({"AOS local", "Sat", "Downlink", "Max el", "Dur", "MHz"});
    passTable_->horizontalHeader()->setStretchLastSection(true);
    passTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    passTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    passTable_->setMaximumHeight(160);
    passRow->addWidget(passTable_, 1);
    auto* armColumn = new QVBoxLayout();
    downlinkCombo_ = new QComboBox();
    armColumn->addWidget(new QLabel("Downlink"));
    armColumn->addWidget(downlinkCombo_);
    autoTrackCheck_ = new QCheckBox("Auto-track Doppler");
    autoTrackCheck_->setChecked(true);
    armColumn->addWidget(autoTrackCheck_);
    autoCaptureCheck_ = new QCheckBox("Auto capture selected sats in range");
    autoCaptureCheck_->setChecked(SatcomScannerEngine::instance().autoCaptureEnabled());
    autoCaptureCheck_->setToolTip(
        "Runs in the background: when a selected, supported satellite reaches the configured "
        "minimum elevation, SDR Town takes the selected receiver, tunes with Doppler, records "
        "when a signal is present, and runs the available APRS/APT/SSTV decoder.");
    armColumn->addWidget(autoCaptureCheck_);
    auto* armButton = new QPushButton("Arm pass");
    auto* sstvButton = new QPushButton("Arm ISS SSTV");
    auto* disarmButton = new QPushButton("Disarm");
    armColumn->addWidget(armButton);
    armColumn->addWidget(sstvButton);
    armColumn->addWidget(disarmButton);
    passStatusLabel_ = new QLabel("No pass armed");
    passStatusLabel_->setWordWrap(true);
    armColumn->addWidget(passStatusLabel_);
    armColumn->addStretch(1);
    passRow->addLayout(armColumn);
    root->addLayout(passRow);
    connect(armButton, &QPushButton::clicked, this, &SatcomScannerWidget::onArmSelected);
    connect(sstvButton, &QPushButton::clicked, this, &SatcomScannerWidget::onArmSstv);
    connect(disarmButton, &QPushButton::clicked, this, &SatcomScannerWidget::onDisarm);
    connect(autoTrackCheck_, &QCheckBox::toggled, this, [](bool enabled) {
        SatcomScannerEngine::instance().setAutoTrack(enabled);
    });
    connect(autoCaptureCheck_, &QCheckBox::toggled, this, [this](bool enabled) {
        SatcomScannerEngine::instance().setAutoCaptureEnabled(enabled);
        if (!enabled && autoCaptureOwned_) stopAutoCapture(false);
        refreshPassesTable();
    });
    connect(deviceCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int row) {
        if (row < 0) return;
        bool ok = false;
        const size_t index = static_cast<size_t>(
            deviceCombo_->itemData(row).toString().toULongLong(&ok));
        const auto devices = DeviceManager::instance().getDevices();
        if (!ok || index >= devices.size()) return;
        auto config = SatcomScannerEngine::instance().config();
        config.deviceIndex = index;
        config.deviceStableKey = devices[index].stableKey;
        SatcomScannerEngine::instance().setConfig(config);
        if (passStatusLabel_) {
            passStatusLabel_->setText(
                "Satcom receiver selected: " + QString::fromStdString(devices[index].label));
        }
    });

    spectrum_ = new SpectrumWidget(this);
    spectrum_->setMinimumHeight(240);
    spectrum_->setColorRange(-120.0, -20.0);
    root->addWidget(spectrum_, 1);

    auto* transport = new QHBoxLayout();
    startBtn_ = new QPushButton("START / TAKE OVER");
    startBtn_->setToolTip(
        "Start Satcom on the selected receiver. An ordinary active Listen session is taken over "
        "automatically and restored after STOP. P25 ownership is never interrupted.");
    skipBtn_ = new QPushButton("FREQUENCY SKIP");
    recordBtn_ = new QPushButton("RECORD");
    stopBtn_ = new QPushButton("STOP");
    transport->addWidget(startBtn_);
    transport->addWidget(skipBtn_);
    transport->addWidget(recordBtn_);
    transport->addWidget(stopBtn_);

    auto* squelchBox = new QFrame();
    squelchBox->setObjectName("satcomBox");
    auto* squelchLayout = new QHBoxLayout(squelchBox);
    squelchLayout->addWidget(new QLabel("SQUELCH"));
    squelchSpin_ = new QDoubleSpinBox();
    squelchSpin_->setRange(-140.0, 0.0);
    squelchSpin_->setDecimals(1);
    squelchSlider_ = new QSlider(Qt::Horizontal);
    squelchSlider_->setRange(-1400, 0);
    monitorAudioCheck_ = new QCheckBox("MONITOR AUDIO");
    monitorAudioCheck_->setToolTip(
        "Route the active satellite demodulator through the configured SDR Town playback output. "
        "If Satcom selected the active Listen receiver, ordinary Listen audio is parked and restored "
        "when Satcom stops; a second selected SDR can run independently.");
    squelchLayout->addWidget(squelchSpin_);
    squelchLayout->addWidget(squelchSlider_, 1);
    squelchLayout->addWidget(monitorAudioCheck_);
    transport->addWidget(squelchBox, 1);
    root->addLayout(transport);

    auto* status = new QHBoxLayout();
    statusDevice_ = new QLabel("DEVICE —");
    statusScan_ = new QLabel("Idle");
    statusRec_ = new QLabel("Not recording");
    audioMeter_ = new QLabel("[----------]");
    status->addWidget(statusDevice_);
    status->addWidget(statusScan_, 1);
    status->addWidget(statusRec_);
    status->addWidget(audioMeter_);
    root->addLayout(status);

    statusHealth_ = new QLabel("STREAM: stopped   AUDIO: off   IQ DISCONTINUITIES: 0");
    statusHealth_->setWordWrap(true);
    root->addWidget(statusHealth_);

    logView_ = new QPlainTextEdit();
    logView_->setReadOnly(true);
    logView_->setMaximumBlockCount(500);
    logView_->setPlaceholderText("Satcom decode / scan log");
    logView_->setMaximumHeight(120);
    root->addWidget(logView_);

    connect(startBtn_, &QPushButton::clicked, this, &SatcomScannerWidget::onStart);
    connect(stopBtn_, &QPushButton::clicked, this, &SatcomScannerWidget::onStop);
    connect(skipBtn_, &QPushButton::clicked, this, &SatcomScannerWidget::onSkip);
    connect(recordBtn_, &QPushButton::clicked, this, &SatcomScannerWidget::onRecord);
    connect(monitorAudioCheck_, &QCheckBox::toggled, this, [](bool enabled) {
        SatcomScannerEngine::instance().setMonitorAudioEnabled(enabled);
    });
    connect(squelchSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        squelchSlider_->blockSignals(true);
        squelchSlider_->setValue(static_cast<int>(value * 10.0));
        squelchSlider_->blockSignals(false);
    });
    connect(squelchSlider_, &QSlider::valueChanged, this, [this](int value) {
        squelchSpin_->blockSignals(true);
        squelchSpin_->setValue(value / 10.0);
        squelchSpin_->blockSignals(false);
    });
    connect(presetCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        const QString name = presetCombo_->currentText();
        if (name.isEmpty()) return;
        SatcomScannerEngine::instance().applyPreset(name.toStdString());
        refreshUi();
    });

    const auto observer = SatPassPlanner::instance().observer();
    latEdit_->setText(QString::number(observer.latDeg, 'f', 5));
    lonEdit_->setText(QString::number(observer.lonDeg, 'f', 5));
    altSpin_->setValue(observer.altM);
    minElSpin_->setValue(observer.minElevationDeg);
    if (observerMap_) observerMap_->setMarker(observer.latDeg, observer.lonDeg);
    connect(observerMap_, &ObserverMapWidget::locationPicked, this, [this](double lat, double lon) {
        latEdit_->setText(QString::number(lat, 'f', 5));
        lonEdit_->setText(QString::number(lon, 'f', 5));
        onApplyObserver();
    });
}

void SatcomScannerWidget::applyFieldsToConfig() {
    auto config = SatcomScannerEngine::instance().config();
    config.lowHz = lowSpin_->value() * 1e6;
    config.highHz = highSpin_->value() * 1e6;
    config.stepHz = stepSpin_->value() * 1e3;
    config.bandwidthHz = bwSpin_->value() * 1e3;
    config.mode = modeCombo_->currentText().toStdString();
    config.squelchDb = squelchSpin_->value();
    config.monitorAudio = monitorAudioCheck_ && monitorAudioCheck_->isChecked();
    if (deviceCombo_ && deviceCombo_->currentIndex() >= 0) {
        bool ok = false;
        const size_t index = static_cast<size_t>(
            deviceCombo_->currentData().toString().toULongLong(&ok));
        const auto devices = DeviceManager::instance().getDevices();
        if (ok && index < devices.size()) {
            config.deviceIndex = index;
            config.deviceStableKey = devices[index].stableKey;
        }
    }
    SatcomScannerEngine::instance().setConfig(config);
}

void SatcomScannerWidget::onStart() {
    applyFieldsToConfig();
    if (!SatcomScannerEngine::instance().start(true)) {
        QMessageBox::warning(
            this, "Satcom",
            QString::fromStdString(SatcomScannerEngine::instance().snapshot().lastStatus));
    }
}

void SatcomScannerWidget::onStop() {
    if (autoCaptureOwned_) {
        stopAutoCapture(false);
    } else {
        SatcomScannerEngine::instance().stopRecording();
        SatcomScannerEngine::instance().stop();
        SatcomScannerEngine::instance().disarmPass();
    }
    recordingUi_ = false;
}

void SatcomScannerWidget::onSkip() {
    SatcomScannerEngine::instance().skip();
}

void SatcomScannerWidget::onRecord() {
    if (!recordingUi_) {
        if (SatcomScannerEngine::instance().startRecording()) recordingUi_ = true;
    } else {
        SatcomScannerEngine::instance().stopRecording();
        recordingUi_ = false;
    }
}

void SatcomScannerWidget::onApplyObserver() {
    SatObserverConfig observer = SatPassPlanner::instance().observer();
    double latitude = 0.0;
    double longitude = 0.0;
    if (!SatObserverConfig::parseLatLonToken(latEdit_->text().toStdString(), true, &latitude) ||
        !SatObserverConfig::parseLatLonToken(lonEdit_->text().toStdString(), false, &longitude)) {
        QMessageBox::warning(
            this, "Observer",
            "Enter lat/lon as signed degrees or with N/S E/W (both hemispheres).");
        return;
    }
    observer.latDeg = latitude;
    observer.lonDeg = longitude;
    observer.altM = altSpin_->value();
    observer.minElevationDeg = minElSpin_->value();
    SatPassPlanner::instance().setObserver(observer);
    AdsBTrackStore::instance().setObserver(observer.latDeg, observer.lonDeg);
    if (observerMap_) observerMap_->setMarker(observer.latDeg, observer.lonDeg);

    const QString confirmation = QString("Location saved: %1, %2 — passes recalculated")
                                     .arg(observer.latDeg, 0, 'f', 5)
                                     .arg(observer.lonDeg, 0, 'f', 5);
    if (passStatusLabel_) passStatusLabel_->setText(confirmation);
    if (auto* button = findChild<QPushButton*>("applySatcomLocationButton")) {
        button->setText("Location saved ✓");
        QTimer::singleShot(1800, button, [button]() {
            button->setText("Apply location");
        });
    }
    refreshPassesTable();
}

void SatcomScannerWidget::onSelectSats() {
    SatCatalogueDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) return;
    SatPassPlanner::instance().setCatalogueSelection(dialog.selectedIds());
    autoCapturePassKey_.clear();
    refreshPassesTable();
}

void SatcomScannerWidget::onRefreshTle() {
    if (tleBusy_) return;

    auto& planner = SatPassPlanner::instance();
    planner.ensureTleLoaded();
    const auto before = planner.snapshot();
    tleBusy_ = true;
    if (refreshTleBtn_) refreshTleBtn_->setEnabled(false);
    if (before.tleAgeSec >= 0) {
        tleAgeLabel_->setText("TLE: cache loaded — refreshing…");
        if (passStatusLabel_)
            passStatusLabel_->setText("Existing cached TLEs are active while CelesTrak is checked");
    } else {
        tleAgeLabel_->setText("TLE: downloading…");
    }

    const QPointer<SatcomScannerWidget> guard(this);
    QTimer::singleShot(25000, this, [guard]() {
        if (!guard || !guard->tleBusy_) return;
        guard->tleBusy_ = false;
        if (guard->refreshTleBtn_) guard->refreshTleBtn_->setEnabled(true);
        guard->refreshPassesTable();
        if (guard->passStatusLabel_) {
            guard->passStatusLabel_->setText(
                "TLE refresh timed out; cached TLEs remain active");
        }
    });

    planner.refreshTleAsync([guard](bool usable, std::string error) {
        if (!guard) return;
        QMetaObject::invokeMethod(guard.data(), [guard, usable, error]() {
            if (!guard) return;
            guard->tleBusy_ = false;
            if (guard->refreshTleBtn_) guard->refreshTleBtn_->setEnabled(true);
            if (!usable) {
                guard->tleAgeLabel_->setText("TLE: unavailable");
                QMessageBox::warning(
                    guard.data(), "TLE",
                    QString::fromStdString(error.empty() ? "Refresh failed and no valid cache was found" : error));
            }
            guard->refreshPassesTable();
            guard->refreshUi();
        }, Qt::QueuedConnection);
    });
}

void SatcomScannerWidget::onArmSelected() {
    const int row = passTable_->currentRow();
    if (row < 0) {
        QMessageBox::information(this, "Arm", "Select a pass row first.");
        return;
    }
    const QString satId = passTable_->item(row, 1)->data(Qt::UserRole).toString();
    const QString downlinkId = passTable_->item(row, 2)->data(Qt::UserRole).toString();

    applyFieldsToConfig();
    auto& engine = SatcomScannerEngine::instance();
    std::string error;
    if (!engine.armPass(satId.toStdString(), downlinkId.toStdString(),
                        autoTrackCheck_->isChecked(), true, &error)) {
        QMessageBox::warning(
            this, "Arm", QString::fromStdString(error.empty() ? "Arm failed" : error));
        return;
    }
    refreshUi();
}

void SatcomScannerWidget::onDisarm() {
    if (autoCaptureOwned_) {
        stopAutoCapture(false);
        return;
    }
    SatcomScannerEngine::instance().stopRecording();
    SatcomScannerEngine::instance().disarmPass();
    recordingUi_ = false;
}

void SatcomScannerWidget::onArmSstv() {
    applyFieldsToConfig();
    auto& engine = SatcomScannerEngine::instance();
    std::string error;
    if (!engine.armPass("iss", "iss-sstv", autoTrackCheck_->isChecked(), true, &error)) {
        QMessageBox::warning(
            this, "ISS SSTV", QString::fromStdString(error.empty() ? "Arm failed" : error));
        return;
    }
    engine.startRecording();
    recordingUi_ = true;
    emit requestOpenSstvLive();
    refreshUi();
}

void SatcomScannerWidget::stopAutoCapture(bool keepHandledKey) {
    auto& engine = SatcomScannerEngine::instance();
    engine.stopRecording();
    engine.disarmPass();
    if (autoEngineWasRunning_) engine.skip();
    else engine.stop();
    recordingUi_ = false;
    autoCaptureOwned_ = false;
    autoEngineWasRunning_ = false;
    autoSstvRequested_ = false;
    if (!keepHandledKey) autoCapturePassKey_.clear();
}

void SatcomScannerWidget::updateAutoCapture(const SatPassPlannerSnapshot& plan) {
    if (!autoCaptureCheck_ || !autoCaptureCheck_->isChecked()) {
        if (autoCaptureOwned_) stopAutoCapture(false);
        return;
    }

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

    auto& engine = SatcomScannerEngine::instance();
    if (plan.armed.armed) {
        if (!autoCaptureOwned_) return;
        const auto scan = engine.snapshot();
        const bool signalPresent = std::isfinite(scan.audioRmsDb) &&
                                   scan.audioRmsDb >= scan.config.squelchDb;
        if (signalPresent && scan.state != SatcomScannerState::Recording) {
            if (engine.startRecording()) recordingUi_ = true;
        }
        if (plan.armed.role == "sstv" && !autoSstvRequested_) {
            autoSstvRequested_ = true;
            emit requestOpenSstvLive();
        }
        if (!signalPresent && passStatusLabel_) {
            passStatusLabel_->setText(
                QString("Auto armed %1 — waiting for signal (el %2°)")
                    .arg(QString::fromStdString(plan.armed.downlinkId))
                    .arg(plan.armed.elevationDeg, 0, 'f', 1));
        }
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
        if (!position.tleValid || !position.inRange || position.downlinkId.empty() ||
            position.freqHz <= 0.0) {
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

    const auto before = engine.snapshot();
    autoEngineWasRunning_ = before.state != SatcomScannerState::Idle;
    if (!engine.start(true)) {
        autoEngineWasRunning_ = false;
        autoCaptureRetryAfter_ = now + 30;
        if (passStatusLabel_) {
            passStatusLabel_->setText(
                "Auto capture waiting: " + QString::fromStdString(engine.snapshot().lastStatus));
        }
        return;
    }

    std::string error;
    if (!engine.armPass(best->satId, best->downlinkId, true, true, &error)) {
        if (!autoEngineWasRunning_) engine.stop();
        autoEngineWasRunning_ = false;
        autoCaptureRetryAfter_ = now + 30;
        if (passStatusLabel_)
            passStatusLabel_->setText("Auto arm failed: " + QString::fromStdString(error));
        return;
    }

    autoCaptureOwned_ = true;
    autoCapturePassKey_ = QString::fromStdString(best->satId + "/" + best->downlinkId);
    autoSstvRequested_ = false;
    if (passStatusLabel_) {
        passStatusLabel_->setText(
            QString("Auto armed %1 — %2 MHz — waiting for signal")
                .arg(QString::fromStdString(best->satName))
                .arg(best->freqHz / 1e6, 0, 'f', 4));
    }
}

void SatcomScannerWidget::refreshPassesTable() {
    const auto plan = SatPassPlanner::instance().snapshot();
    if (downlinkCombo_->count() == 0) {
        for (const auto& entry : plan.catalogue.entries()) {
            for (const auto& downlink : entry.downlinks) {
                downlinkCombo_->addItem(
                    QString("%1 — %2").arg(QString::fromStdString(entry.name),
                                            QString::fromStdString(downlink.label)),
                    QString::fromStdString(downlink.id));
            }
        }
    }

    passTable_->setRowCount(static_cast<int>(plan.passes.size()));
    for (int i = 0; i < static_cast<int>(plan.passes.size()); ++i) {
        const auto& pass = plan.passes[static_cast<size_t>(i)];
        const QDateTime aos = QDateTime::fromSecsSinceEpoch(
            static_cast<qint64>(pass.aosUnix), Qt::LocalTime);
        auto* aosItem = new QTableWidgetItem(aos.toString("dd MMM HH:mm"));
        aosItem->setData(Qt::UserRole, QString::fromStdString(pass.satId));
        passTable_->setItem(i, 0, aosItem);
        auto* satItem = new QTableWidgetItem(QString::fromStdString(pass.satName));
        satItem->setData(Qt::UserRole, QString::fromStdString(pass.satId));
        passTable_->setItem(i, 1, satItem);
        auto* downlinkItem = new QTableWidgetItem(QString::fromStdString(pass.downlinkLabel));
        downlinkItem->setData(Qt::UserRole, QString::fromStdString(pass.downlinkId));
        passTable_->setItem(i, 2, downlinkItem);
        passTable_->setItem(i, 3, new QTableWidgetItem(QString::number(pass.maxElDeg, 'f', 1)));
        passTable_->setItem(i, 4, new QTableWidgetItem(
            QString::number(pass.durationSec / 60.0, 'f', 1) + "m"));
        passTable_->setItem(i, 5, new QTableWidgetItem(
            QString::number(pass.freqHz / 1e6, 'f', 4)));
    }

    if (observerMap_) {
        std::vector<SatelliteMapMarker> markers;
        markers.reserve(plan.positions.size());
        for (const auto& position : plan.positions) {
            SatelliteMapMarker marker;
            marker.id = QString::fromStdString(position.satId);
            marker.name = QString::fromStdString(position.satName);
            marker.latDeg = position.latitudeDeg;
            marker.lonDeg = position.longitudeDeg;
            marker.altitudeKm = position.altitudeKm;
            marker.elevationDeg = position.elevationDeg;
            marker.valid = position.tleValid;
            marker.inRange = position.inRange;
            marker.armed = plan.armed.armed && plan.armed.satId == position.satId;
            markers.push_back(std::move(marker));
        }
        observerMap_->setSatellites(markers);
    }

    if (!tleBusy_) {
        if (plan.tleAgeSec < 0) tleAgeLabel_->setText("TLE: none — Refresh");
        else if (plan.tleAgeSec < 3600)
            tleAgeLabel_->setText(QString("TLE age: %1m").arg(plan.tleAgeSec / 60));
        else
            tleAgeLabel_->setText(
                QString("TLE age: %1h").arg(plan.tleAgeSec / 3600.0, 0, 'f', 1));
    }

    if (plan.armed.armed) {
        passStatusLabel_->setText(
            QString("Armed %1  el %2°  doppler %3 Hz  tuned %4 MHz")
                .arg(QString::fromStdString(plan.armed.downlinkId))
                .arg(plan.armed.elevationDeg, 0, 'f', 1)
                .arg(plan.armed.dopplerHz, 0, 'f', 0)
                .arg(plan.armed.tunedHz / 1e6, 0, 'f', 4));
    } else if (!autoCaptureOwned_) {
        passStatusLabel_->setText(
            QString::fromStdString(plan.lastStatus.empty() ? "No pass armed" : plan.lastStatus));
    }
}

void SatcomScannerWidget::refreshUi() {
    const auto currentSnapshot = SatcomScannerEngine::instance().snapshot();
    const bool currentlyActive = currentSnapshot.state != SatcomScannerState::Idle ||
                                 currentSnapshot.passArmed;
    if (!currentlyActive) SatcomScannerEngine::instance().resolveDeviceIndex(nullptr);

    const auto snapshot = SatcomScannerEngine::instance().snapshot();
    const auto& config = snapshot.config;
    static int passTableThrottle = 0;
    const bool refreshPasses = (++passTableThrottle % 4) == 0;

    auto& manager = DeviceManager::instance();
    const auto devices = manager.getDevices();
    bool rebuildDevices = deviceCombo_->count() != static_cast<int>(devices.size());
    if (!rebuildDevices) {
        for (int i = 0; i < deviceCombo_->count(); ++i) {
            const auto index = static_cast<size_t>(i);
            const QString expected = receiverDisplayText(index, devices[index], manager.isStreaming(index));
            if (deviceCombo_->itemText(i) != expected) {
                rebuildDevices = true;
                break;
            }
        }
    }
    deviceCombo_->blockSignals(true);
    if (rebuildDevices) {
        deviceCombo_->clear();
        for (size_t i = 0; i < devices.size(); ++i) {
            deviceCombo_->addItem(
                receiverDisplayText(i, devices[i], manager.isStreaming(i)),
                QString::number(static_cast<qulonglong>(i)));
        }
    }
    const int selectedDevice = deviceCombo_->findData(
        QString::number(static_cast<qulonglong>(config.deviceIndex)));
    if (selectedDevice >= 0) deviceCombo_->setCurrentIndex(selectedDevice);
    deviceCombo_->blockSignals(false);

    if (!lowSpin_->hasFocus()) lowSpin_->setValue(config.lowHz / 1e6);
    if (!highSpin_->hasFocus()) highSpin_->setValue(config.highHz / 1e6);
    if (!stepSpin_->hasFocus()) stepSpin_->setValue(config.stepHz / 1e3);
    if (!bwSpin_->hasFocus()) bwSpin_->setValue(config.bandwidthHz / 1e3);
    if (!modeCombo_->hasFocus()) {
        const int index = modeCombo_->findText(QString::fromStdString(config.mode));
        if (index >= 0) modeCombo_->setCurrentIndex(index);
    }
    if (!squelchSpin_->hasFocus()) {
        squelchSpin_->setValue(config.squelchDb);
        squelchSlider_->setValue(static_cast<int>(config.squelchDb * 10.0));
    }
    if (monitorAudioCheck_) {
        monitorAudioCheck_->blockSignals(true);
        monitorAudioCheck_->setChecked(config.monitorAudio);
        monitorAudioCheck_->blockSignals(false);
    }

    if (presetCombo_->count() == 0) {
        for (const auto& preset : config.presets)
            presetCombo_->addItem(QString::fromStdString(preset.name));
    }

    if (!snapshot.spectrumDb.empty()) {
        spectrum_->updateSpectrum(
            snapshot.spectrumDb, snapshot.spectrumCenterHz, snapshot.spectrumRateHz);
        spectrum_->setFreqRange(config.lowHz, config.highHz);
    }

    const QString streamState = QString::fromStdString(
        snapshot.streamState.empty() ? "stopped" : snapshot.streamState);
    statusDevice_->setText(snapshot.deviceConnected
        ? QString("● RX %1: %2 — %3")
              .arg(static_cast<qulonglong>(snapshot.activeDeviceIndex))
              .arg(QString::fromStdString(snapshot.deviceLabel), streamState)
        : QString("○ %1 — %2")
              .arg(snapshot.deviceLabel.empty()
                       ? "NO DEVICE"
                       : QString::fromStdString(snapshot.deviceLabel),
                   streamState));
    if (snapshot.passArmed) {
        statusScan_->setText(QString("PASS %1 MHz (doppler %2 Hz)")
                                 .arg(snapshot.tunedHz / 1e6, 0, 'f', 4)
                                 .arg(snapshot.dopplerHz, 0, 'f', 0));
    } else if (snapshot.state == SatcomScannerState::Scanning) {
        statusScan_->setText(
            QString("Scanning… %1 MHz").arg(snapshot.currentHz / 1e6, 0, 'f', 3));
    } else if (snapshot.state == SatcomScannerState::Locked ||
               snapshot.state == SatcomScannerState::Recording) {
        statusScan_->setText(
            QString("Locked %1 MHz").arg(snapshot.lockHz / 1e6, 0, 'f', 3));
    } else {
        statusScan_->setText(
            QString::fromStdString(snapshot.lastStatus.empty() ? "Idle" : snapshot.lastStatus));
    }

    recordingUi_ = snapshot.state == SatcomScannerState::Recording;
    if (recordingUi_)
        statusRec_->setText(
            QString("● Recording %1 MHz").arg(snapshot.recordHz / 1e6, 0, 'f', 3));
    else
        statusRec_->setText("○ Not recording");

    const double normalized = std::clamp((snapshot.audioRmsDb + 100.0) / 60.0, 0.0, 1.0);
    const int bars = static_cast<int>(normalized * 10.0);
    QString meter = "[";
    for (int i = 0; i < 10; ++i) meter += (i < bars ? "#" : "-");
    meter += "]";
    audioMeter_->setText(meter);

    const QString outputDetail = !snapshot.sstvOutputDir.empty()
        ? QString("   SSTV: %1").arg(QString::fromStdString(snapshot.sstvOutputDir))
        : (!snapshot.aptPreviewPath.empty()
            ? QString("   APT: %1").arg(QString::fromStdString(snapshot.aptPreviewPath))
            : QString());
    statusHealth_->setText(
        QString("STREAM: %1   AUDIO: %2   IQ DISCONTINUITIES: %3   LOG DROPS: %4%5")
            .arg(streamState)
            .arg(snapshot.audioMonitoring
                ? (snapshot.sharedMainAudio ? "ON (SDR TOWN)" : "ON (STANDALONE)")
                : (config.monitorAudio ? "UNAVAILABLE" : "OFF"))
            .arg(static_cast<qulonglong>(snapshot.iqDiscontinuities))
            .arg(static_cast<qulonglong>(snapshot.logDropped))
            .arg(outputDetail));
    const bool active = snapshot.state != SatcomScannerState::Idle || snapshot.passArmed;
    const bool healthy = !active ||
                         (snapshot.deviceConnected && snapshot.streamState == "live hardware");
    statusHealth_->setStyleSheet(healthy
        ? "color: #39FF14; font-weight: 700;"
        : "color: #ff4d4d; font-weight: 900;");

    if (!snapshot.recentDecodes.empty()) {
        QString all;
        const size_t count = snapshot.recentDecodes.size();
        const size_t begin = count > 40 ? count - 40 : 0;
        for (size_t i = begin; i < count; ++i)
            all += QString::fromStdString(snapshot.recentDecodes[i]) + "\n";
        if (logView_->toPlainText() != all) logView_->setPlainText(all);
    }

    deviceCombo_->setEnabled(!active);
    startBtn_->setEnabled(!active);
    stopBtn_->setEnabled(active);
    skipBtn_->setEnabled(active && !snapshot.passArmed);
    recordBtn_->setEnabled(snapshot.state == SatcomScannerState::Locked ||
                           snapshot.state == SatcomScannerState::Recording || snapshot.passArmed);
    recordBtn_->setText(recordingUi_ ? "STOP REC" : "RECORD");

    if (refreshPasses) refreshPassesTable();
}
