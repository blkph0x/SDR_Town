#include "SatcomScannerWidget.h"
#include "SatcomScannerEngine.h"
#include "SatCatalogueDialog.h"
#include "SatPassPlanner.h"
#include "SpectrumWidget.h"
#include "ObserverMapWidget.h"
#include "AdsBTrackStore.h"
#include "DeviceManager.h"

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

} // namespace

SatcomScannerWidget::SatcomScannerWidget(QWidget* parent)
    : QWidget(parent)
{
    buildUi();
    applyNeonStyle();
    refreshTimer_ = new QTimer(this);
    connect(refreshTimer_, &QTimer::timeout, this, &SatcomScannerWidget::refreshUi);
    // Do NOT start the timer here — only while visible. Constructing this widget
    // inside the main dock must not steal the GUI/DSP path during normal WFM listening.

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
    auto mkFreq = [&](const QString& label, QDoubleSpinBox*& spin) {
        auto* box = new QFrame();
        box->setObjectName("satcomBox");
        auto* lay = new QVBoxLayout(box);
        lay->addWidget(new QLabel(label));
        spin = new QDoubleSpinBox();
        spin->setDecimals(3);
        spin->setRange(0.1, 6000.0);
        spin->setSuffix(" MHz");
        lay->addWidget(spin);
        top->addWidget(box);
    };
    mkFreq("LOW FREQUENCY", lowSpin_);
    mkFreq("HIGH FREQUENCY", highSpin_);

    auto* opts = new QFrame();
    opts->setObjectName("satcomBox");
    auto* optsLay = new QFormLayout(opts);
    bwSpin_ = new QDoubleSpinBox();
    bwSpin_->setDecimals(1);
    bwSpin_->setRange(1.0, 2000.0);
    bwSpin_->setSuffix(" kHz");
    modeCombo_ = new QComboBox();
    modeCombo_->addItems({"NFM", "WFM", "AM", "USB", "APT", "APRS"});
    deviceCombo_ = new QComboBox();
    stepSpin_ = new QDoubleSpinBox();
    stepSpin_->setDecimals(3);
    stepSpin_->setSuffix(" kHz");
    presetCombo_ = new QComboBox();
    optsLay->addRow("RECEIVER", deviceCombo_);
    optsLay->addRow("BANDWIDTH", bwSpin_);
    optsLay->addRow("MODE", modeCombo_);
    optsLay->addRow("STEP", stepSpin_);
    optsLay->addRow("PRESET", presetCombo_);
    top->addWidget(opts, 1);
    root->addLayout(top);

    // Observer + catalogue
    auto* obs = new QFrame();
    obs->setObjectName("satcomBox");
    auto* obsLay = new QGridLayout(obs);
    obsLay->addWidget(new QLabel("HOME LAT (+N/-S or 33.8S)"), 0, 0);
    latEdit_ = new QLineEdit("-33.87");
    obsLay->addWidget(latEdit_, 0, 1);
    obsLay->addWidget(new QLabel("HOME LON (+E/-W or 151.2E)"), 0, 2);
    lonEdit_ = new QLineEdit("151.21");
    obsLay->addWidget(lonEdit_, 0, 3);
    obsLay->addWidget(new QLabel("ALT m"), 1, 0);
    altSpin_ = new QDoubleSpinBox();
    altSpin_->setRange(-500.0, 9000.0);
    altSpin_->setValue(50.0);
    obsLay->addWidget(altSpin_, 1, 1);
    obsLay->addWidget(new QLabel("MIN EL °"), 1, 2);
    minElSpin_ = new QDoubleSpinBox();
    minElSpin_->setRange(0.0, 90.0);
    minElSpin_->setValue(10.0);
    obsLay->addWidget(minElSpin_, 1, 3);
    auto* applyObs = new QPushButton("Apply location");
    auto* selSats = new QPushButton("Select satellites…");
    refreshTleBtn_ = new QPushButton("Refresh TLE");
    obsLay->addWidget(applyObs, 2, 0);
    obsLay->addWidget(selSats, 2, 1);
    obsLay->addWidget(refreshTleBtn_, 2, 2);
    tleAgeLabel_ = new QLabel("TLE: —");
    obsLay->addWidget(tleAgeLabel_, 2, 3);
    observerMap_ = new ObserverMapWidget(this);
    observerMap_->setMinimumHeight(280);
    obsLay->addWidget(observerMap_, 3, 0, 1, 4);
    root->addWidget(obs);
    connect(applyObs, &QPushButton::clicked, this, &SatcomScannerWidget::onApplyObserver);
    connect(selSats, &QPushButton::clicked, this, &SatcomScannerWidget::onSelectSats);
    connect(refreshTleBtn_, &QPushButton::clicked, this, &SatcomScannerWidget::onRefreshTle);

    auto* passRow = new QHBoxLayout();
    passTable_ = new QTableWidget(0, 6);
    passTable_->setHorizontalHeaderLabels({"AOS local", "Sat", "Downlink", "Max el", "Dur", "MHz"});
    passTable_->horizontalHeader()->setStretchLastSection(true);
    passTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    passTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    passTable_->setMaximumHeight(160);
    passRow->addWidget(passTable_, 1);
    auto* armCol = new QVBoxLayout();
    downlinkCombo_ = new QComboBox();
    armCol->addWidget(new QLabel("Downlink"));
    armCol->addWidget(downlinkCombo_);
    autoTrackCheck_ = new QCheckBox("Auto-track Doppler");
    autoTrackCheck_->setChecked(true);
    armCol->addWidget(autoTrackCheck_);
    autoCaptureCheck_ = new QCheckBox("Auto capture selected sats in range");
    autoCaptureCheck_->setChecked(SatcomScannerEngine::instance().autoCaptureEnabled());
    autoCaptureCheck_->setToolTip(
        "Runs in the background: when a selected, supported satellite reaches the configured "
        "minimum elevation, SDR Town takes the selected receiver, tunes with Doppler, records "
        "when a signal is present, and runs the available APRS/APT/SSTV decoder.");
    armCol->addWidget(autoCaptureCheck_);
    auto* armBtn = new QPushButton("Arm pass");
    auto* sstvBtn = new QPushButton("Arm ISS SSTV");
    auto* disarmBtn = new QPushButton("Disarm");
    armCol->addWidget(armBtn);
    armCol->addWidget(sstvBtn);
    armCol->addWidget(disarmBtn);
    passStatusLabel_ = new QLabel("No pass armed");
    passStatusLabel_->setWordWrap(true);
    armCol->addWidget(passStatusLabel_);
    armCol->addStretch(1);
    passRow->addLayout(armCol);
    root->addLayout(passRow);
    connect(armBtn, &QPushButton::clicked, this, &SatcomScannerWidget::onArmSelected);
    connect(sstvBtn, &QPushButton::clicked, this, &SatcomScannerWidget::onArmSstv);
    connect(disarmBtn, &QPushButton::clicked, this, &SatcomScannerWidget::onDisarm);
    connect(autoTrackCheck_, &QCheckBox::toggled, this, [](bool on) {
        SatcomScannerEngine::instance().setAutoTrack(on);
    });
    connect(autoCaptureCheck_, &QCheckBox::toggled, this, [this](bool on) {
        SatcomScannerEngine::instance().setAutoCaptureEnabled(on);
        if (!on && autoCaptureOwned_) stopAutoCapture(false);
        refreshPassesTable();
    });
    connect(deviceCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int row) {
        if (row < 0) return;
        bool ok = false;
        const size_t index = static_cast<size_t>(deviceCombo_->itemData(row).toString().toULongLong(&ok));
        const auto devices = DeviceManager::instance().getDevices();
        if (!ok || index >= devices.size()) return;
        auto cfg = SatcomScannerEngine::instance().config();
        cfg.deviceIndex = index;
        cfg.deviceStableKey = devices[index].stableKey;
        SatcomScannerEngine::instance().setConfig(cfg);
    });

    spectrum_ = new SpectrumWidget(this);
    spectrum_->setMinimumHeight(240);
    spectrum_->setColorRange(-120.0, -20.0);
    root->addWidget(spectrum_, 1);

    auto* transport = new QHBoxLayout();
    startBtn_ = new QPushButton("START SCAN");
    skipBtn_ = new QPushButton("FREQUENCY SKIP");
    recordBtn_ = new QPushButton("RECORD");
    stopBtn_ = new QPushButton("STOP");
    transport->addWidget(startBtn_);
    transport->addWidget(skipBtn_);
    transport->addWidget(recordBtn_);
    transport->addWidget(stopBtn_);

    auto* sqBox = new QFrame();
    sqBox->setObjectName("satcomBox");
    auto* sqLay = new QHBoxLayout(sqBox);
    sqLay->addWidget(new QLabel("SQUELCH"));
    squelchSpin_ = new QDoubleSpinBox();
    squelchSpin_->setRange(-140.0, 0.0);
    squelchSpin_->setDecimals(1);
    squelchSlider_ = new QSlider(Qt::Horizontal);
    squelchSlider_->setRange(-1400, 0);
    monitorAudioCheck_ = new QCheckBox("MONITOR AUDIO");
    monitorAudioCheck_->setToolTip(
        "Route the active satellite demodulator to the default playback device. "
        "This is an isolated Satcom output and does not alter the P25 audio path.");
    sqLay->addWidget(squelchSpin_);
    sqLay->addWidget(squelchSlider_, 1);
    sqLay->addWidget(monitorAudioCheck_);
    transport->addWidget(sqBox, 1);
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
    connect(monitorAudioCheck_, &QCheckBox::toggled, this, [](bool on) {
        SatcomScannerEngine::instance().setMonitorAudioEnabled(on);
    });
    connect(squelchSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v) {
        squelchSlider_->blockSignals(true);
        squelchSlider_->setValue(static_cast<int>(v * 10.0));
        squelchSlider_->blockSignals(false);
    });
    connect(squelchSlider_, &QSlider::valueChanged, this, [this](int v) {
        squelchSpin_->blockSignals(true);
        squelchSpin_->setValue(v / 10.0);
        squelchSpin_->blockSignals(false);
    });
    connect(presetCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        const QString name = presetCombo_->currentText();
        if (name.isEmpty()) return;
        SatcomScannerEngine::instance().applyPreset(name.toStdString());
        refreshUi();
    });

    const auto obsCfg = SatPassPlanner::instance().observer();
    latEdit_->setText(QString::number(obsCfg.latDeg, 'f', 5));
    lonEdit_->setText(QString::number(obsCfg.lonDeg, 'f', 5));
    altSpin_->setValue(obsCfg.altM);
    minElSpin_->setValue(obsCfg.minElevationDeg);
    if (observerMap_) observerMap_->setMarker(obsCfg.latDeg, obsCfg.lonDeg);
    connect(observerMap_, &ObserverMapWidget::locationPicked, this, [this](double lat, double lon) {
        latEdit_->setText(QString::number(lat, 'f', 5));
        lonEdit_->setText(QString::number(lon, 'f', 5));
        onApplyObserver();
    });
}

void SatcomScannerWidget::applyFieldsToConfig() {
    auto cfg = SatcomScannerEngine::instance().config();
    cfg.lowHz = lowSpin_->value() * 1e6;
    cfg.highHz = highSpin_->value() * 1e6;
    cfg.stepHz = stepSpin_->value() * 1e3;
    cfg.bandwidthHz = bwSpin_->value() * 1e3;
    cfg.mode = modeCombo_->currentText().toStdString();
    cfg.squelchDb = squelchSpin_->value();
    cfg.monitorAudio = monitorAudioCheck_ && monitorAudioCheck_->isChecked();
    if (deviceCombo_ && deviceCombo_->currentIndex() >= 0) {
        bool ok = false;
        const size_t index = static_cast<size_t>(deviceCombo_->currentData().toString().toULongLong(&ok));
        const auto devices = DeviceManager::instance().getDevices();
        if (ok && index < devices.size()) {
            cfg.deviceIndex = index;
            cfg.deviceStableKey = devices[index].stableKey;
        }
    }
    SatcomScannerEngine::instance().setConfig(cfg);
}

void SatcomScannerWidget::onStart() {
    applyFieldsToConfig();
    if (!SatcomScannerEngine::instance().start(true)) {
        QMessageBox::warning(this, "Satcom",
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
    double lat = 0.0;
    double lon = 0.0;
    if (!SatObserverConfig::parseLatLonToken(latEdit_->text().toStdString(), true, &lat) ||
        !SatObserverConfig::parseLatLonToken(lonEdit_->text().toStdString(), false, &lon)) {
        QMessageBox::warning(this, "Observer", "Enter lat/lon as signed degrees or with N/S E/W (both hemispheres).");
        return;
    }
    observer.latDeg = lat;
    observer.lonDeg = lon;
    observer.altM = altSpin_->value();
    observer.minElevationDeg = minElSpin_->value();
    SatPassPlanner::instance().setObserver(observer);
    AdsBTrackStore::instance().setObserver(observer.latDeg, observer.lonDeg);
    if (observerMap_) observerMap_->setMarker(observer.latDeg, observer.lonDeg);
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
    tleBusy_ = true;
    if (refreshTleBtn_) refreshTleBtn_->setEnabled(false);
    tleAgeLabel_->setText("TLE: downloading…");
    SatPassPlanner::instance().refreshTleAsync([this](bool ok, std::string error) {
        QMetaObject::invokeMethod(this, [this, ok, error]() {
            tleBusy_ = false;
            if (refreshTleBtn_) refreshTleBtn_->setEnabled(true);
            if (!ok) {
                tleAgeLabel_->setText("TLE: failed");
                QMessageBox::warning(this, "TLE",
                                     QString::fromStdString(error.empty() ? "Refresh failed" : error));
            }
            refreshPassesTable();
            refreshUi();
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
        QMessageBox::warning(this, "Arm", QString::fromStdString(error.empty() ? "Arm failed" : error));
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
        QMessageBox::warning(this, "ISS SSTV", QString::fromStdString(error.empty() ? "Arm failed" : error));
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
    if (autoEngineWasRunning_) {
        engine.skip();
    } else {
        engine.stop();
    }
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
        if (!autoCaptureOwned_) return; // respect manual arm
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
        // Planner reached LOS and disarmed itself. Finalise recording and leave
        // this pass handled until it has gone below the configured elevation.
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
        if (passStatusLabel_)
            passStatusLabel_->setText("Auto capture waiting: " + QString::fromStdString(engine.snapshot().lastStatus));
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
    // Downlink combo from catalogue
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
        passTable_->setItem(i, 4, new QTableWidgetItem(QString::number(pass.durationSec / 60.0, 'f', 1) + "m"));
        passTable_->setItem(i, 5, new QTableWidgetItem(QString::number(pass.freqHz / 1e6, 'f', 4)));
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
            tleAgeLabel_->setText(QString("TLE age: %1h").arg(plan.tleAgeSec / 3600.0, 0, 'f', 1));
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
    // Resolve stale saved indices (for example an RTL placeholder at index 0)
    // before presenting or starting the satcom receiver.
    SatcomScannerEngine::instance().resolveDeviceIndex(nullptr);
    const auto snap = SatcomScannerEngine::instance().snapshot();
    const auto& cfg = snap.config;

    static int passTableThrottle = 0;
    const bool refreshPasses = (++passTableThrottle % 4) == 0; // ~2 s while visible

    const auto devices = DeviceManager::instance().getDevices();
    bool rebuildDevices = deviceCombo_->count() != static_cast<int>(devices.size());
    if (!rebuildDevices) {
        for (int i = 0; i < deviceCombo_->count(); ++i) {
            const QString expected = QString("%1 — %2")
                .arg(i).arg(QString::fromStdString(devices[static_cast<size_t>(i)].label));
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
                QString("%1 — %2").arg(static_cast<qulonglong>(i))
                    .arg(QString::fromStdString(devices[i].label)),
                QString::number(static_cast<qulonglong>(i)));
        }
    }
    const int selectedDevice = deviceCombo_->findData(
        QString::number(static_cast<qulonglong>(cfg.deviceIndex)));
    if (selectedDevice >= 0) deviceCombo_->setCurrentIndex(selectedDevice);
    deviceCombo_->blockSignals(false);

    if (!lowSpin_->hasFocus()) lowSpin_->setValue(cfg.lowHz / 1e6);
    if (!highSpin_->hasFocus()) highSpin_->setValue(cfg.highHz / 1e6);
    if (!stepSpin_->hasFocus()) stepSpin_->setValue(cfg.stepHz / 1e3);
    if (!bwSpin_->hasFocus()) bwSpin_->setValue(cfg.bandwidthHz / 1e3);
    if (!modeCombo_->hasFocus()) {
        const int index = modeCombo_->findText(QString::fromStdString(cfg.mode));
        if (index >= 0) modeCombo_->setCurrentIndex(index);
    }
    if (!squelchSpin_->hasFocus()) {
        squelchSpin_->setValue(cfg.squelchDb);
        squelchSlider_->setValue(static_cast<int>(cfg.squelchDb * 10.0));
    }
    if (monitorAudioCheck_) {
        monitorAudioCheck_->blockSignals(true);
        monitorAudioCheck_->setChecked(cfg.monitorAudio);
        monitorAudioCheck_->blockSignals(false);
    }

    if (presetCombo_->count() == 0) {
        for (const auto& preset : cfg.presets)
            presetCombo_->addItem(QString::fromStdString(preset.name));
    }

    if (!snap.spectrumDb.empty()) {
        spectrum_->updateSpectrum(snap.spectrumDb, snap.spectrumCenterHz, snap.spectrumRateHz);
        spectrum_->setFreqRange(cfg.lowHz, cfg.highHz);
    }

    const QString streamState = QString::fromStdString(
        snap.streamState.empty() ? "stopped" : snap.streamState);
    statusDevice_->setText(snap.deviceConnected
        ? QString("● %1 — %2").arg(QString::fromStdString(snap.deviceLabel), streamState)
        : QString("○ %1 — %2").arg(
              snap.deviceLabel.empty() ? "NO DEVICE" : QString::fromStdString(snap.deviceLabel),
              streamState));
    if (snap.passArmed) {
        statusScan_->setText(QString("PASS %1 MHz (doppler %2 Hz)")
                                 .arg(snap.tunedHz / 1e6, 0, 'f', 4)
                                 .arg(snap.dopplerHz, 0, 'f', 0));
    } else if (snap.state == SatcomScannerState::Scanning) {
        statusScan_->setText(QString("Scanning… %1 MHz").arg(snap.currentHz / 1e6, 0, 'f', 3));
    } else if (snap.state == SatcomScannerState::Locked ||
               snap.state == SatcomScannerState::Recording) {
        statusScan_->setText(QString("Locked %1 MHz").arg(snap.lockHz / 1e6, 0, 'f', 3));
    } else {
        statusScan_->setText(QString::fromStdString(snap.lastStatus.empty() ? "Idle" : snap.lastStatus));
    }

    recordingUi_ = snap.state == SatcomScannerState::Recording;
    if (recordingUi_)
        statusRec_->setText(QString("● Recording %1 MHz").arg(snap.recordHz / 1e6, 0, 'f', 3));
    else
        statusRec_->setText("○ Not recording");

    const double normalized = std::clamp((snap.audioRmsDb + 100.0) / 60.0, 0.0, 1.0);
    const int bars = static_cast<int>(normalized * 10.0);
    QString meter = "[";
    for (int i = 0; i < 10; ++i) meter += (i < bars ? "#" : "-");
    meter += "]";
    audioMeter_->setText(meter);

    const QString outputDetail = !snap.sstvOutputDir.empty()
        ? QString("   SSTV: %1").arg(QString::fromStdString(snap.sstvOutputDir))
        : (!snap.aptPreviewPath.empty()
            ? QString("   APT: %1").arg(QString::fromStdString(snap.aptPreviewPath))
            : QString());
    statusHealth_->setText(
        QString("STREAM: %1   AUDIO: %2   IQ DISCONTINUITIES: %3   LOG DROPS: %4%5")
            .arg(streamState)
            .arg(snap.audioMonitoring ? "ON" : (cfg.monitorAudio ? "UNAVAILABLE" : "OFF"))
            .arg(static_cast<qulonglong>(snap.iqDiscontinuities))
            .arg(static_cast<qulonglong>(snap.logDropped))
            .arg(outputDetail));
    const bool active = snap.state != SatcomScannerState::Idle || snap.passArmed;
    const bool healthy = !active || (snap.deviceConnected && snap.streamState == "live hardware");
    statusHealth_->setStyleSheet(healthy
        ? "color: #39FF14; font-weight: 700;"
        : "color: #ff4d4d; font-weight: 900;");

    if (!snap.recentDecodes.empty()) {
        QString all;
        const size_t count = snap.recentDecodes.size();
        const size_t start = count > 40 ? count - 40 : 0;
        for (size_t i = start; i < count; ++i)
            all += QString::fromStdString(snap.recentDecodes[i]) + "\n";
        if (logView_->toPlainText() != all) logView_->setPlainText(all);
    }

    startBtn_->setEnabled(!active || snap.passArmed);
    stopBtn_->setEnabled(active);
    skipBtn_->setEnabled(active && !snap.passArmed);
    recordBtn_->setEnabled(snap.state == SatcomScannerState::Locked ||
                           snap.state == SatcomScannerState::Recording || snap.passArmed);
    recordBtn_->setText(recordingUi_ ? "STOP REC" : "RECORD");

    if (refreshPasses) refreshPassesTable();
}
