#include "SatcomScannerWidget.h"
#include "SatcomScannerEngine.h"
#include "SatCatalogueDialog.h"
#include "SatPassPlanner.h"
#include "SpectrumWidget.h"
#include "ObserverMapWidget.h"
#include "AdsBTrackStore.h"

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
        QMetaObject::invokeMethod(this, "refreshUi", Qt::QueuedConnection);
    });
}

void SatcomScannerWidget::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    // Light UI only — do NOT run SGP4 pass prediction here (that stalls the GUI
    // thread and starves WFM). Pass table refreshes on demand / throttled tick.
    if (refreshTimer_ && !refreshTimer_->isActive()) refreshTimer_->start(500);
    refreshUi();
}

void SatcomScannerWidget::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    if (refreshTimer_) refreshTimer_->stop();
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
    stepSpin_ = new QDoubleSpinBox();
    stepSpin_->setDecimals(3);
    stepSpin_->setSuffix(" kHz");
    presetCombo_ = new QComboBox();
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
    observerMap_->setMinimumHeight(180);
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
    auto* armBtn = new QPushButton("Arm pass");
    auto* sstvBtn = new QPushButton("Arm ISS SSTV");
    auto* disarmBtn = new QPushButton("Disarm");
    armCol->addWidget(armBtn);
    armCol->addWidget(sstvBtn);
    armCol->addWidget(disarmBtn);
    passStatusLabel_ = new QLabel("No pass armed");
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
    sqLay->addWidget(squelchSpin_);
    sqLay->addWidget(squelchSlider_, 1);
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
    SatcomScannerEngine::instance().setConfig(cfg);
}

void SatcomScannerWidget::onStart() {
    applyFieldsToConfig();
    if (!SatcomScannerEngine::instance().start(false)) {
        QMessageBox::warning(this, "Satcom",
            QString::fromStdString(SatcomScannerEngine::instance().snapshot().lastStatus));
    }
}

void SatcomScannerWidget::onStop() {
    SatcomScannerEngine::instance().stopRecording();
    SatcomScannerEngine::instance().stop();
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
    SatObserverConfig o = SatPassPlanner::instance().observer();
    double lat = 0, lon = 0;
    if (!SatObserverConfig::parseLatLonToken(latEdit_->text().toStdString(), true, &lat) ||
        !SatObserverConfig::parseLatLonToken(lonEdit_->text().toStdString(), false, &lon)) {
        QMessageBox::warning(this, "Observer", "Enter lat/lon as signed degrees or with N/S E/W (both hemispheres).");
        return;
    }
    o.latDeg = lat;
    o.lonDeg = lon;
    o.altM = altSpin_->value();
    o.minElevationDeg = minElSpin_->value();
    SatPassPlanner::instance().setObserver(o);
    AdsBTrackStore::instance().setObserver(o.latDeg, o.lonDeg);
    if (observerMap_) observerMap_->setMarker(o.latDeg, o.lonDeg);
}

void SatcomScannerWidget::onSelectSats() {
    SatCatalogueDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;
    SatPassPlanner::instance().setCatalogueSelection(dlg.selectedIds());
}

void SatcomScannerWidget::onRefreshTle() {
    if (tleBusy_) return;
    tleBusy_ = true;
    if (refreshTleBtn_) refreshTleBtn_->setEnabled(false);
    tleAgeLabel_->setText("TLE: downloading…");
    SatPassPlanner::instance().refreshTleAsync([this](bool ok, std::string err) {
        QMetaObject::invokeMethod(this, [this, ok, err]() {
            tleBusy_ = false;
            if (refreshTleBtn_) refreshTleBtn_->setEnabled(true);
            if (!ok) {
                tleAgeLabel_->setText("TLE: failed");
                QMessageBox::warning(this, "TLE",
                                     QString::fromStdString(err.empty() ? "Refresh failed" : err));
            }
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
    QString dlId = downlinkCombo_->currentData().toString();
    if (dlId.isEmpty()) dlId = passTable_->item(row, 2)->data(Qt::UserRole).toString();
    std::string err;
    if (!SatcomScannerEngine::instance().armPass(satId.toStdString(), dlId.toStdString(),
                                                 autoTrackCheck_->isChecked(), false, &err)) {
        QMessageBox::warning(this, "Arm", QString::fromStdString(err.empty() ? "Arm failed" : err));
        return;
    }
    if (!SatcomScannerEngine::instance().snapshot().passArmed) return;
    // Ensure streaming for audio/decode
    SatcomScannerEngine::instance().start();
}

void SatcomScannerWidget::onDisarm() {
    SatcomScannerEngine::instance().disarmPass();
}

void SatcomScannerWidget::onArmSstv() {
    std::string err;
    if (!SatcomScannerEngine::instance().armPass("iss", "iss-sstv", autoTrackCheck_->isChecked(), false, &err)) {
        QMessageBox::warning(this, "ISS SSTV", QString::fromStdString(err.empty() ? "Arm failed" : err));
        return;
    }
    SatcomScannerEngine::instance().start();
    emit requestOpenSstvLive();
}

void SatcomScannerWidget::refreshPassesTable() {
    const auto plan = SatPassPlanner::instance().snapshot();
    // Downlink combo from catalogue
    if (downlinkCombo_->count() == 0) {
        for (const auto& e : plan.catalogue.entries()) {
            for (const auto& d : e.downlinks) {
                downlinkCombo_->addItem(
                    QString("%1 — %2").arg(QString::fromStdString(e.name), QString::fromStdString(d.label)),
                    QString::fromStdString(d.id));
            }
        }
    }

    passTable_->setRowCount(static_cast<int>(plan.passes.size()));
    for (int i = 0; i < static_cast<int>(plan.passes.size()); ++i) {
        const auto& p = plan.passes[static_cast<size_t>(i)];
        const QDateTime aos = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(p.aosUnix), Qt::LocalTime);
        auto* aosItem = new QTableWidgetItem(aos.toString("dd MMM HH:mm"));
        aosItem->setData(Qt::UserRole, QString::fromStdString(p.satId));
        passTable_->setItem(i, 0, aosItem);
        auto* satItem = new QTableWidgetItem(QString::fromStdString(p.satName));
        satItem->setData(Qt::UserRole, QString::fromStdString(p.satId));
        passTable_->setItem(i, 1, satItem);
        auto* dlItem = new QTableWidgetItem(QString::fromStdString(p.downlinkLabel));
        dlItem->setData(Qt::UserRole, QString::fromStdString(p.downlinkId));
        passTable_->setItem(i, 2, dlItem);
        passTable_->setItem(i, 3, new QTableWidgetItem(QString::number(p.maxElDeg, 'f', 1)));
        passTable_->setItem(i, 4, new QTableWidgetItem(QString::number(p.durationSec / 60.0, 'f', 1) + "m"));
        passTable_->setItem(i, 5, new QTableWidgetItem(QString::number(p.freqHz / 1e6, 'f', 4)));
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
    } else {
        passStatusLabel_->setText(QString::fromStdString(plan.lastStatus.empty() ? "No pass armed" : plan.lastStatus));
    }
}

void SatcomScannerWidget::refreshUi() {
    // Pass-track retune is NOT driven from this UI timer. A 200–500 ms
    // tickPassTrack from the docked hub was yanking the live RX off WFM stations
    // (buzz/chop on known-good broadcast). Doppler track only runs from armPass
    // and the scanner worker while a pass is explicitly armed.
    const auto snap = SatcomScannerEngine::instance().snapshot();
    const auto& cfg = snap.config;

    static int passTableThrottle = 0;
    const bool refreshPasses = snap.passArmed && (++passTableThrottle % 8) == 0; // ~4 s when armed

    if (!lowSpin_->hasFocus()) lowSpin_->setValue(cfg.lowHz / 1e6);
    if (!highSpin_->hasFocus()) highSpin_->setValue(cfg.highHz / 1e6);
    if (!stepSpin_->hasFocus()) stepSpin_->setValue(cfg.stepHz / 1e3);
    if (!bwSpin_->hasFocus()) bwSpin_->setValue(cfg.bandwidthHz / 1e3);
    if (!modeCombo_->hasFocus()) {
        const int idx = modeCombo_->findText(QString::fromStdString(cfg.mode));
        if (idx >= 0) modeCombo_->setCurrentIndex(idx);
    }
    if (!squelchSpin_->hasFocus()) {
        squelchSpin_->setValue(cfg.squelchDb);
        squelchSlider_->setValue(static_cast<int>(cfg.squelchDb * 10.0));
    }

    if (presetCombo_->count() == 0) {
        for (const auto& p : cfg.presets)
            presetCombo_->addItem(QString::fromStdString(p.name));
    }

    if (!snap.spectrumDb.empty()) {
        spectrum_->updateSpectrum(snap.spectrumDb, snap.spectrumCenterHz, snap.spectrumRateHz);
        spectrum_->setFreqRange(cfg.lowHz, cfg.highHz);
    }

    statusDevice_->setText(snap.deviceConnected
        ? QString("● %1").arg(QString::fromStdString(snap.deviceLabel))
        : "○ NO DEVICE");
    if (snap.passArmed)
        statusScan_->setText(QString("PASS %1 MHz (doppler %+2.0f Hz)")
                                 .arg(snap.tunedHz / 1e6, 0, 'f', 4)
                                 .arg(snap.dopplerHz));
    else if (snap.state == SatcomScannerState::Scanning)
        statusScan_->setText(QString("Scanning… %1 MHz").arg(snap.currentHz / 1e6, 0, 'f', 3));
    else if (snap.state == SatcomScannerState::Locked || snap.state == SatcomScannerState::Recording)
        statusScan_->setText(QString("Locked %1 MHz").arg(snap.lockHz / 1e6, 0, 'f', 3));
    else
        statusScan_->setText(QString::fromStdString(snap.lastStatus.empty() ? "Idle" : snap.lastStatus));

    if (snap.state == SatcomScannerState::Recording)
        statusRec_->setText(QString("● Recording %1 MHz").arg(snap.recordHz / 1e6, 0, 'f', 3));
    else
        statusRec_->setText("○ Not recording");

    const double norm = std::clamp((snap.audioRmsDb + 100.0) / 60.0, 0.0, 1.0);
    const int bars = static_cast<int>(norm * 10.0);
    QString meter = "[";
    for (int i = 0; i < 10; ++i) meter += (i < bars ? "#" : "-");
    meter += "]";
    audioMeter_->setText(meter);

    if (!snap.recentDecodes.empty()) {
        QString all;
        const size_t n = snap.recentDecodes.size();
        const size_t start = n > 40 ? n - 40 : 0;
        for (size_t i = start; i < n; ++i)
            all += QString::fromStdString(snap.recentDecodes[i]) + "\n";
        if (logView_->toPlainText() != all) logView_->setPlainText(all);
    }

    const bool active = snap.state != SatcomScannerState::Idle || snap.passArmed;
    startBtn_->setEnabled(!active || snap.passArmed);
    stopBtn_->setEnabled(active);
    skipBtn_->setEnabled(active && !snap.passArmed);
    recordBtn_->setEnabled(snap.state == SatcomScannerState::Locked || snap.state == SatcomScannerState::Recording || snap.passArmed);
    recordBtn_->setText(recordingUi_ || snap.state == SatcomScannerState::Recording ? "STOP REC" : "RECORD");

    if (refreshPasses) refreshPassesTable();
}
