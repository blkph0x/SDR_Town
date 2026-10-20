#include "SatcomScannerWidget.h"
#include "WorkflowDeviceCombo.h"
#include "SatcomScannerEngine.h"
#include "SatCatalogueDialog.h"
#include "SatPassPlanner.h"
#include "SpectrumWidget.h"
#include "ObserverMapWidget.h"
#include "AdsBTrackStore.h"
#include "DeviceManager.h"
#include "SstvModes.h"
#include "TleStore.h"
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QSpinBox>
#include <QScrollArea>
#include <QUuid>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDir>

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
#include <QCoreApplication>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QToolButton>
#include <QInputDialog>
#include <QStyle>
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

QJsonObject SatcomScannerWidget::webStatus() const {
    const auto s = engine_.snapshot();
    auto j = engine_.planner().publicStatusJson();
    j["sessionId"] = engine_.sessionId();
    j["state"] = engine_.stateName();
    j["config"] = s.config.toJson();
    j["config"].erase("sstvSaveDir");
    j["config"].erase("recordDir");
    j["config"].erase("logDir");
    j["config"]["lowMHz"] = s.config.lowHz / 1e6;
    j["config"]["highMHz"] = s.config.highHz / 1e6;
    j["config"]["autoCapture"] = engine_.autoCaptureEnabled();
    j["deviceConnected"] = s.deviceConnected;
    j["deviceLabel"] = s.deviceLabel;
    j["lastStatus"] = s.lastStatus;
    j["passArmed"] = s.passArmed;
    j["autoTrack"] = s.autoTrack;
    j["currentHz"] = s.currentHz;
    j["currentMHz"] = s.currentHz / 1e6;
    j["tunedMHz"] = s.tunedHz / 1e6;
    j["dopplerHz"] = s.dopplerHz;
    j["iqDiscontinuities"] = s.iqDiscontinuities;
    j["spectrumDb"] = s.spectrumDb;
    j["spectrumCenterHz"] = s.spectrumCenterHz;
    j["spectrumRateHz"] = s.spectrumRateHz;
    j["recentDecodes"] = s.recentDecodes;
    j["images"] = nlohmann::json::array();
    // Only completed image outputs of this engine's active pass are shared.
    if (!s.sstvOutputDir.empty()) {
        const QDir directory(QString::fromStdString(s.sstvOutputDir));
        const auto files = directory.entryInfoList({"*.png"}, QDir::Files, QDir::Time);
        for (int i = 0; i < std::min(32, static_cast<int>(files.size())); ++i)
            j["images"].push_back({{"path",files[i].absoluteFilePath().toStdString()},
                                   {"name",files[i].fileName().toStdString()}});
    }
    return QJsonDocument::fromJson(QByteArray::fromStdString(j.dump())).object();
}

QJsonObject SatcomScannerWidget::webPassControl(const QJsonObject& body) {
    const auto fail = [](const QString& error) { return QJsonObject{{"ok",false},{"status",400},{"error",error}}; };
    const auto action = body.value("action").toString();
    for (const auto& key : {"autoTrack", "autoCapture", "sstvOnly"})
        if (body.contains(key) && !body.value(key).isBool()) return fail("Boolean setting required");
    if (action == "disarm") onDisarm();
    else if (action == "autotrack_only") engine_.setAutoTrack(body.value("autoTrack").toBool());
    else if (action == "automation") {
        if (QCoreApplication::instance()->property("sdrtown.guiDryRun").toBool() && body.value("autoCapture").toBool())
            return fail("RF disabled in GUI dry-run");
        if (body.contains("sstvOnly")) {
            auto cfg = engine_.config(); cfg.sstvOnly = body.value("sstvOnly").toBool(); engine_.setConfig(cfg);
        }
        if (body.contains("autoCapture")) engine_.setAutoCaptureEnabled(body.value("autoCapture").toBool());
    } else if (action == "arm") {
        if (QCoreApplication::instance()->property("sdrtown.guiDryRun").toBool()) return fail("RF disabled in GUI dry-run");
        if (!body.value("satId").isString() || !body.value("downlinkId").isString()) return fail("Satellite and downlink required");
        std::string error;
        // Remote pass controls never force another workflow off its SDR.
        if (!engine_.armPass(body.value("satId").toString().toStdString(), body.value("downlinkId").toString().toStdString(),
                             body.value("autoTrack").toBool(true), false, &error)) return fail(QString::fromStdString(error));
    } else if (action == "catalogue") {
        if (!body.value("selected").isArray()) return fail("Satellite selection array required");
        std::vector<std::string> ids;
        for (const auto& id : body.value("selected").toArray()) {
            if (!id.isString()) return fail("Satellite IDs must be strings");
            ids.push_back(id.toString().toStdString());
        }
        engine_.planner().setCatalogueSelection(ids);
    } else if (action == "tle_refresh") {
        std::string error;
        if (!engine_.planner().refreshTle(&error)) return fail(QString::fromStdString(error));
    } else return fail("Unknown pass action");
    reloadSessionControls();
    return {{"ok",true},{"satcom",webStatus()}};
}

SatcomScannerWidget::SatcomScannerWidget(QWidget* parent, const QString& sessionId)
    : QWidget(parent),
      ownedEngine_(sessionId.isEmpty() ? nullptr : std::make_unique<SatcomScannerEngine>(sessionId.toStdString())),
      engine_(ownedEngine_ ? *ownedEngine_ : SatcomScannerEngine::instance())
{
    buildUi();
    if (ownedEngine_) {
        setObjectName("satcomSession." + QString::fromStdString(engine_.sessionId()));
        auto* controller = new QTimer(this);
        controller->setInterval(1000);
        connect(controller, &QTimer::timeout, this, [this] {
            autoCaptureTick();
        });
        controller->start();
    }
    refreshTimer_ = new QTimer(this);
    connect(refreshTimer_, &QTimer::timeout, this, &SatcomScannerWidget::refreshUi);

    engine_.setUpdateCallback([this]() {
        QMetaObject::invokeMethod(this, "refreshUi", Qt::QueuedConnection);
    });
    engine_.planner().setUpdateCallback([this]() {
        QMetaObject::invokeMethod(this, [this]() {
            refreshPassesTable();
            refreshUi();
        }, Qt::QueuedConnection);
    });
}

void SatcomScannerWidget::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    if (refreshTimer_ && !refreshTimer_->isActive()) refreshTimer_->start(500);
    if (taskTabs_->currentIndex() == 1) engine_.planner().ensureTleLoaded();
    refreshPassesTable();
    refreshUi();
}

void SatcomScannerWidget::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    if (refreshTimer_) refreshTimer_->stop();
}

SatcomScannerWidget::~SatcomScannerWidget() {
    engine_.setUpdateCallback({});
    engine_.planner().setUpdateCallback({});
    if (ownedEngine_) engine_.stop();
}

void SatcomScannerWidget::reloadSessionControls() {
    // DEC-0225: polling or a remote read must not discard an unfinished local edit.
    if (draftDirty_) return;
    const auto config = engine_.config();
    const QSignalBlocker a(lowSpin_), b(highSpin_), c(stepSpin_), d(bwSpin_), e(modeCombo_), f(squelchSpin_), g(monitorAudioCheck_),
        h(dwellSpin_), i(squelchSlider_), j(deviceCombo_);
    lowSpin_->setValue(config.lowHz / 1e6);
    highSpin_->setValue(config.highHz / 1e6);
    stepSpin_->setValue(config.stepHz / 1e3);
    bwSpin_->setValue(config.bandwidthHz / 1e3);
    modeCombo_->setCurrentText(QString::fromStdString(config.mode));
    squelchSpin_->setValue(config.squelchDb);
    monitorAudioCheck_->setChecked(config.monitorAudio);
    dwellSpin_->setValue(config.dwellMs);
    squelchSlider_->setValue(static_cast<int>(config.squelchDb * 10));
    refreshWorkflowDeviceCombo(*deviceCombo_, DeviceOwnership::Owner::Satcom, QString::fromStdString(config.deviceStableKey));
    updateRangeSummary();
}


void SatcomScannerWidget::buildUi() {
    setObjectName("satcomRoot");
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* title = new QLabel("Satcom - Receive only");
    title->setStyleSheet("font-size: 16px; font-weight: 600;");
    outer->addWidget(title);
    deviceCombo_ = new QComboBox();
    deviceCombo_->setObjectName("satcomDevice");
    deviceCombo_->setAccessibleName("Satcom radio");
    deviceCombo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    deviceCombo_->setMinimumContentsLength(18);
    deviceCombo_->setToolTip("Radio for this Satcom session. Start/Arm may take over ordinary Listen and restore it on Stop. P25 is never interrupted.");
    auto* radioForm = new QFormLayout;
    radioForm->addRow("Radio", deviceCombo_);
    outer->addLayout(radioForm);
    taskTabs_ = new QTabWidget;
    taskTabs_->setObjectName("satcomTasks");
    outer->addWidget(taskTabs_, 1);
    auto* scanScroll = new QScrollArea;
    scanScroll->setWidgetResizable(true);
    auto* content = new QWidget;
    scanScroll->setWidget(content);
    taskTabs_->addTab(scanScroll, "Range Scan");
    auto* root = new QVBoxLayout(content);
    scanSettings_ = new QWidget;
    auto* settings = new QVBoxLayout(scanSettings_);
    settings->setContentsMargins(0, 0, 0, 0);
    root->addWidget(scanSettings_);
    auto* rangeRow = new QHBoxLayout;
    presetCombo_ = new QComboBox;
    presetCombo_->setObjectName("satcomRangePreset");
    presetCombo_->setAccessibleName("Saved receive range");
    presetCombo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    presetCombo_->setMinimumContentsLength(22);
    rangeRow->addWidget(presetCombo_, 1);
    saveRangeBtn_ = new QPushButton(style()->standardIcon(QStyle::SP_DialogSaveButton), "Save range...");
    saveRangeBtn_->setObjectName("satcomSaveRange");
    deleteRangeBtn_ = new QPushButton(style()->standardIcon(QStyle::SP_TrashIcon), "");
    deleteRangeBtn_->setObjectName("satcomDeleteRange");
    deleteRangeBtn_->setAccessibleName("Remove saved range");
    deleteRangeBtn_->setToolTip("Remove selected custom range");
    rangeRow->addWidget(saveRangeBtn_);
    rangeRow->addWidget(deleteRangeBtn_);
    settings->addLayout(rangeRow);
    rangeNote_ = new QLabel;
    rangeNote_->setObjectName("satcomRangeNote");
    rangeNote_->setWordWrap(true);
    settings->addWidget(rangeNote_);

    auto* top = new QHBoxLayout();
    auto makeFrequency = [&](const QString& label, QDoubleSpinBox*& spin) {
        auto* box = new QFrame();
        box->setObjectName("satcomBox");
        auto* layout = new QVBoxLayout(box);
        auto* fieldLabel = new QLabel(label);
        layout->addWidget(fieldLabel);
        spin = new QDoubleSpinBox();
        spin->setDecimals(3);
        spin->setRange(0.1, 6000.0);
        spin->setSuffix(" MHz");
        spin->setAccessibleName(label + " frequency in MHz");
        fieldLabel->setBuddy(spin);
        layout->addWidget(spin);
        layout->addStretch();
        top->addWidget(box);
    };
    makeFrequency("From", lowSpin_);
    makeFrequency("To", highSpin_);
    lowSpin_->setObjectName("satcomRangeLow");
    highSpin_->setObjectName("satcomRangeHigh");
    settings->addLayout(top);

    auto* advanced = new QToolButton;
    advanced->setText("Advanced scan settings");
    advanced->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    advanced->setArrowType(Qt::RightArrow);
    advanced->setCheckable(true);
    advanced->setObjectName("satcomAdvanced");
    settings->addWidget(advanced);
    auto* options = new QFrame();
    options->hide();
    options->setObjectName("satcomBox");
    auto* optionsLayout = new QFormLayout(options);
    bwSpin_ = new QDoubleSpinBox();
    bwSpin_->setDecimals(1);
    bwSpin_->setRange(1.0, 2000.0);
    bwSpin_->setSuffix(" kHz");
    bwSpin_->setObjectName("satcomChannelBandwidth");
    bwSpin_->setAccessibleName("Channel bandwidth in kHz");
    modeCombo_ = new QComboBox();
    modeCombo_->addItems({"NFM", "WFM", "AM", "USB", "LSB", "APT", "APRS"});
    modeCombo_->setObjectName("satcomMode");
    modeCombo_->setAccessibleName("Receive mode");
    stepSpin_ = new QDoubleSpinBox();
    stepSpin_->setDecimals(3);
    stepSpin_->setSuffix(" kHz");
    stepSpin_->setRange(0.1, 10000);
    stepSpin_->setObjectName("satcomTuneStep");
    stepSpin_->setAccessibleName("Tune step in kHz");
    stepSpin_->setToolTip("Distance between sequential tuner positions, not a guaranteed channel spacing.");
    dwellSpin_ = new QSpinBox;
    dwellSpin_->setRange(20, 5000);
    dwellSpin_->setSuffix(" ms");
    dwellSpin_->setObjectName("satcomDwell");
    dwellSpin_->setAccessibleName("Dwell per tune in milliseconds");
    optionsLayout->addRow("Channel bandwidth", bwSpin_);
    optionsLayout->addRow("Receive mode", modeCombo_);
    optionsLayout->addRow("Tune step", stepSpin_);
    optionsLayout->addRow("Dwell per tune", dwellSpin_);
    settings->addWidget(options);
    connect(advanced, &QToolButton::toggled, this, [options, advanced](bool open) {
        options->setVisible(open);
        advanced->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
    });
    rangeSummary_ = new QLabel;
    rangeSummary_->setObjectName("satcomRangeSummary");
    rangeSummary_->setWordWrap(true);
    root->addWidget(rangeSummary_);

    auto* scroll = new QScrollArea;
    scroll->setObjectName("satelliteControlsScroll");
    scroll->setWidgetResizable(true);
    auto* passPage = new QWidget;
    scroll->setWidget(passPage);
    auto* passRoot = new QVBoxLayout(passPage);
    taskTabs_->addTab(scroll, "Satellite Passes");
    connect(taskTabs_, &QTabWidget::currentChanged, this, [this](int index) {
        if (index == 1) { engine_.planner().ensureTleLoaded(); refreshPassesTable(); }
        updateRangeSummary();
    });

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
    passRoot->addWidget(observerBox);
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
    downlinkCombo_->setObjectName("satelliteDownlink");
    downlinkCombo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    downlinkCombo_->setMinimumContentsLength(35);
    armColumn->addWidget(new QLabel("Downlink"));
    armColumn->addWidget(downlinkCombo_);
    autoTrackCheck_ = new QCheckBox("Auto-track Doppler");
    autoTrackCheck_->setChecked(true);
    armColumn->addWidget(autoTrackCheck_);
    autoCaptureCheck_ = new QCheckBox("Auto capture selected sats in range");
    autoCaptureCheck_->setObjectName("satelliteAutoCapture");
    autoCaptureCheck_->setChecked(engine_.autoCaptureEnabled());
    autoCaptureCheck_->setToolTip(
        "Check this session to run in the background. Saved on from last time does not "
        "seize the radio at startup. When a selected, supported satellite reaches the "
        "configured minimum elevation, SDR Town uses the selected receiver only if free, tunes with "
        "Doppler, records when a signal is present, and runs the available APRS/APT/SSTV decoder.");
    armColumn->addWidget(autoCaptureCheck_);
    sstvOnlyCheck_ = new QCheckBox("SSTV passes only");
    sstvOnlyCheck_->setObjectName("satelliteSstvOnly");
    sstvOnlyCheck_->setChecked(engine_.config().sstvOnly);
    armColumn->addWidget(sstvOnlyCheck_);
    connect(sstvOnlyCheck_, &QCheckBox::toggled, this, [this](bool enabled) {
        auto config = engine_.config(); config.sstvOnly = enabled; engine_.setConfig(config);
    });
    auto* armButton = new QPushButton("Arm pass");
    auto* sstvButton = new QPushButton("Arm selected SSTV");
    sstvButton->setObjectName("armSatelliteSstv");
    auto* editProfile = new QPushButton("SSTV profile...");
    editProfile->setObjectName("satelliteSstvProfile");
    auto* saveFolder = new QPushButton("Image folder...");
    saveFolder->setToolTip(QString::fromStdString(engine_.config().sstvSaveDir));
    auto* loadTle = new QPushButton("Import TLE...");
    auto* disarmButton = new QPushButton("Disarm");
    armColumn->addWidget(armButton);
    armColumn->addWidget(sstvButton);
    armColumn->addWidget(editProfile);
    armColumn->addWidget(saveFolder);
    armColumn->addWidget(loadTle);
    armColumn->addWidget(disarmButton);
    passStatusLabel_ = new QLabel("No pass armed");
    passStatusLabel_->setObjectName("satellitePassStatus");
    passStatusLabel_->setWordWrap(true);
    armColumn->addWidget(passStatusLabel_);
    armColumn->addStretch(1);
    passRow->addLayout(armColumn);
    passRoot->addLayout(passRow);
    connect(armButton, &QPushButton::clicked, this, &SatcomScannerWidget::onArmSelected);
    connect(sstvButton, &QPushButton::clicked, this, &SatcomScannerWidget::onArmSstv);
    connect(editProfile, &QPushButton::clicked, this, &SatcomScannerWidget::onEditSstvProfile);
    connect(saveFolder, &QPushButton::clicked, this, [this, saveFolder] {
        auto config = engine_.config();
        const auto folder = QFileDialog::getExistingDirectory(this, "Satellite SSTV images", QString::fromStdString(config.sstvSaveDir));
        if (!folder.isEmpty()) { config.sstvSaveDir = folder.toStdString(); engine_.setConfig(config); saveFolder->setToolTip(folder); }
    });
    connect(loadTle, &QPushButton::clicked, this, [this] {
        const auto file = QFileDialog::getOpenFileName(this, "Import orbital elements", {}, "TLE files (*.tle *.txt);;All files (*)");
        if (file.isEmpty()) return;
        std::string error;
        if (!TleStore::instance().loadFromFile(file.toStdString(), &error)) QMessageBox::warning(this, "TLE", QString::fromStdString(error));
        else { TleStore::instance().saveCache(); engine_.planner().refreshPasses(); refreshPassesTable(); }
    });
    connect(passTable_, &QTableWidget::itemSelectionChanged, this, [this] {
        const int row = passTable_->currentRow();
        if (row >= 0 && passTable_->item(row, 2))
            downlinkCombo_->setCurrentIndex(downlinkCombo_->findData(passTable_->item(row, 2)->data(Qt::UserRole)));
    });
    connect(disarmButton, &QPushButton::clicked, this, &SatcomScannerWidget::onDisarm);
    connect(autoTrackCheck_, &QCheckBox::toggled, this, [this](bool enabled) {
        engine_.setAutoTrack(enabled);
    });
    connect(autoCaptureCheck_, &QCheckBox::toggled, this, [this](bool enabled) {
        engine_.setAutoCaptureEnabled(enabled);
        if (!enabled && autoCaptureOwned_) stopAutoCapture(false);
        refreshPassesTable();
    });
    connect(deviceCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int row) {
        if (row < 0) return;
        // Radio selection is shared with pass Arm, but never starts reception.
        auto config = engine_.config();
        config.deviceIndex = size_t(-1);
        config.deviceStableKey = deviceCombo_->itemData(row).toString().toStdString();
        engine_.setConfig(config);
        if (passStatusLabel_) {
            passStatusLabel_->setText(
                "Satcom receiver selected: " + deviceCombo_->itemText(row));
        }
    });

    spectrum_ = new SpectrumWidget(this);
    spectrum_->setMinimumHeight(180);
    spectrum_->setColorRange(-120.0, -20.0);
    spectrum_->hide();
    root->addWidget(spectrum_, 1);
    spectrumEmpty_ = new QLabel("No live capture");
    spectrumEmpty_->setObjectName("satcomSpectrumEmpty");
    spectrumEmpty_->setAlignment(Qt::AlignCenter);
    spectrumEmpty_->setMinimumHeight(180);
    root->addWidget(spectrumEmpty_, 1);

    auto* transport = new QHBoxLayout();
    startBtn_ = new QPushButton(style()->standardIcon(QStyle::SP_MediaPlay), "Start scan");
    startBtn_->setObjectName("satcomStartScan");
    startBtn_->setToolTip(
        "Start Satcom on the selected receiver. An ordinary active Listen session is taken over "
        "automatically and restored after STOP. P25 ownership is never interrupted.");
    skipBtn_ = new QPushButton(style()->standardIcon(QStyle::SP_MediaSkipForward), "Next signal");
    recordBtn_ = new QPushButton("Record audio");
    stopBtn_ = new QPushButton(style()->standardIcon(QStyle::SP_MediaStop), "Stop");
    stopBtn_->setObjectName("satcomStop");
    transport->addWidget(startBtn_);
    transport->addWidget(skipBtn_);
    transport->addWidget(recordBtn_);
    transport->addWidget(stopBtn_);

    auto* squelchBox = new QFrame();
    squelchBox->setObjectName("satcomBox");
    auto* squelchLayout = new QHBoxLayout(squelchBox);
    squelchLayout->addWidget(new QLabel("Signal threshold"));
    squelchSpin_ = new QDoubleSpinBox();
    squelchSpin_->setRange(-140.0, 0.0);
    squelchSpin_->setDecimals(1);
    squelchSpin_->setSuffix(" dB");
    squelchSpin_->setAccessibleName("Signal detection threshold in relative dB");
    squelchSpin_->setObjectName("satcomSquelch");
    squelchSlider_ = new QSlider(Qt::Horizontal);
    squelchSlider_->setRange(-1400, 0);
    squelchSlider_->setAccessibleName("Signal detection threshold");
    monitorAudioCheck_ = new QCheckBox("Listen");
    monitorAudioCheck_->setToolTip(
        "Route the active satellite demodulator through the configured SDR Town playback output. "
        "If Satcom selected the active Listen receiver, ordinary Listen audio is parked and restored "
        "when Satcom stops; a second selected SDR can run independently.");
    squelchLayout->addWidget(squelchSpin_);
    squelchLayout->addWidget(squelchSlider_, 1);
    squelchLayout->addWidget(monitorAudioCheck_);
    outer->addLayout(transport);
    outer->addWidget(squelchBox);

    auto* status = new QHBoxLayout();
    statusDevice_ = new QLabel("DEVICE —");
    statusScan_ = new QLabel("Idle");
    statusRec_ = new QLabel("Not recording");
    audioMeter_ = new QLabel("[----------]");
    status->addWidget(statusDevice_);
    status->addWidget(statusScan_, 1);
    status->addWidget(statusRec_);
    status->addWidget(audioMeter_);
    statusDevice_->setWordWrap(true);
    statusScan_->setWordWrap(true);
    outer->addLayout(status);

    statusHealth_ = new QLabel("STREAM: stopped   AUDIO: off   IQ DISCONTINUITIES: 0");
    statusHealth_->setWordWrap(true);
    outer->addWidget(statusHealth_);

    logView_ = new QPlainTextEdit();
    logView_->setReadOnly(true);
    logView_->setMaximumBlockCount(500);
    logView_->setPlaceholderText("Satcom decode / scan log");
    taskTabs_->addTab(logView_, "Activity");

    connect(startBtn_, &QPushButton::clicked, this, &SatcomScannerWidget::onStart);
    connect(stopBtn_, &QPushButton::clicked, this, &SatcomScannerWidget::onStop);
    connect(skipBtn_, &QPushButton::clicked, this, &SatcomScannerWidget::onSkip);
    connect(recordBtn_, &QPushButton::clicked, this, &SatcomScannerWidget::onRecord);
    connect(monitorAudioCheck_, &QCheckBox::toggled, this, [this](bool enabled) {
        engine_.setMonitorAudioEnabled(enabled);
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
        draftChanged();
    });
    connect(presetCombo_, QOverload<int>::of(&QComboBox::activated), this, [this](int) {
        const QString name = presetCombo_->currentText();
        const auto config = engine_.config();
        for (const auto& preset : config.presets) if (name.toStdString() == preset.name) {
            lowSpin_->setValue(preset.lowHz / 1e6); highSpin_->setValue(preset.highHz / 1e6);
            stepSpin_->setValue(preset.stepHz / 1e3); bwSpin_->setValue(preset.bandwidthHz / 1e3);
            modeCombo_->setCurrentText(QString::fromStdString(preset.mode));
            squelchSpin_->setValue(preset.squelchDb);
            dwellSpin_->setValue(preset.dwellMs);
            break;
        }
        draftChanged();
    });
    connect(saveRangeBtn_, &QPushButton::clicked, this, &SatcomScannerWidget::saveRange);
    connect(deleteRangeBtn_, &QPushButton::clicked, this, &SatcomScannerWidget::deleteRange);
    refreshRangeChoices();
    reloadSessionControls();
    for (auto* spin : {lowSpin_, highSpin_, stepSpin_, bwSpin_, squelchSpin_}) {
        connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &SatcomScannerWidget::draftChanged);
        connect(spin->findChild<QLineEdit*>(), &QLineEdit::textEdited, this, &SatcomScannerWidget::draftChanged);
    }
    connect(modeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SatcomScannerWidget::draftChanged);
    connect(dwellSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, &SatcomScannerWidget::draftChanged);
    connect(dwellSpin_->findChild<QLineEdit*>(), &QLineEdit::textEdited, this, &SatcomScannerWidget::draftChanged);

    const auto observer = engine_.planner().observer();
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

SatcomScannerConfig SatcomScannerWidget::fieldsConfig() const {
    auto config = engine_.config();
    config.lowHz = lowSpin_->value() * 1e6;
    config.highHz = highSpin_->value() * 1e6;
    config.stepHz = stepSpin_->value() * 1e3;
    config.bandwidthHz = bwSpin_->value() * 1e3;
    config.mode = modeCombo_->currentText().toStdString();
    config.squelchDb = squelchSpin_->value();
    config.dwellMs = dwellSpin_->value();
    config.monitorAudio = monitorAudioCheck_ && monitorAudioCheck_->isChecked();
    if (deviceCombo_ && deviceCombo_->currentIndex() >= 0) {
        const auto key = deviceCombo_->currentData().toString().toStdString();
        if (key != config.deviceStableKey) { config.deviceStableKey = key; config.deviceIndex = size_t(-1); }
    }
    return config;
}

bool SatcomScannerWidget::applyFieldsToConfig() {
    const auto config = fieldsConfig();
    if (!config.scanValidationError().empty()) { updateRangeSummary(); return false; }
    engine_.setConfig(config);
    draftDirty_ = false;
    updateRangeSummary();
    return true;
}

void SatcomScannerWidget::refreshRangeChoices() {
    const QSignalBlocker blocker(presetCombo_);
    presetCombo_->clear();
    presetCombo_->addItem("Custom range");
    for (const auto& preset : engine_.config().presets)
        presetCombo_->addItem(QString::fromStdString(preset.name));
}

void SatcomScannerWidget::draftChanged() {
    draftDirty_ = true;
    updateRangeSummary();
}

void SatcomScannerWidget::updateRangeSummary() {
    if (!rangeSummary_ || !squelchSpin_) return;
    const auto draft = fieldsConfig();
    const auto error = draft.scanValidationError();
    const auto snapshot = engine_.snapshot();
    const bool active = snapshot.state != SatcomScannerState::Idle || snapshot.passArmed;
    const auto selectedName = presetCombo_->currentText().toStdString();
    int match = 0;
    QString note;
    QString reference;
    for (const auto& p : draft.presets) {
        if (std::abs(p.lowHz - draft.lowHz) < 1 && std::abs(p.highHz - draft.highHz) < 1 &&
            std::abs(p.stepHz - draft.stepHz) < 1 && std::abs(p.bandwidthHz - draft.bandwidthHz) < 1 &&
            p.mode == draft.mode && std::abs(p.squelchDb - draft.squelchDb) < .01 &&
            p.dwellMs == draft.dwellMs && (match == 0 || p.name == selectedName)) {
            match = presetCombo_->findText(QString::fromStdString(p.name));
            note = QString::fromStdString(p.note);
            reference = QString::fromStdString(p.reference);
            if (p.name == selectedName) break;
        }
    }
    { const QSignalBlocker blocker(presetCombo_); presetCombo_->setCurrentIndex(std::max(0, match)); }
    rangeNote_->setText(note);
    rangeNote_->setVisible(!note.isEmpty());
    rangeNote_->setToolTip(reference);
    bool matchedBuiltin = false;
    for (const auto& p : SatcomScannerConfig::defaults().presets)
        if (p.name == presetCombo_->currentText().toStdString()) matchedBuiltin = true;
    deleteRangeBtn_->setEnabled(!active && match > 0 && !matchedBuiltin);
    saveRangeBtn_->setEnabled(!active && error.empty());
    startBtn_->setEnabled(!active && error.empty() && taskTabs_->currentIndex() == 0);
    if (!error.empty()) {
        rangeSummary_->setText(QString::fromStdString(error));
        rangeSummary_->setStyleSheet("color: #ee7777;");
        return;
    }
    rangeSummary_->setStyleSheet({});
    const uint64_t positions = draft.scanPositionCount();
    const double seconds = static_cast<double>(positions) * draft.dwellMs / 1000.;
    rangeSummary_->setText(QString("%1%2 MHz span | %3 tunes | Dwell only: %4 s | %5 / %6 kHz")
        .arg(draftDirty_ ? "Pending - " : "")
        .arg((draft.highHz-draft.lowHz)/1e6, 0, 'f', 3)
        .arg(static_cast<qulonglong>(positions)).arg(seconds, 0, 'f', 1)
        .arg(QString::fromStdString(draft.mode)).arg(draft.bandwidthHz/1000, 0, 'f', 1));
    rangeSummary_->setToolTip("Sequential receive scan, not simultaneous full-range coverage. Retune delays and signal holds add time. Detection holds until Next signal or Stop. The spectrum shows only the current capture. NFM does not decode MUOS WCDMA, DAMA or encrypted traffic.");
}

void SatcomScannerWidget::saveRange() {
    const auto draft = fieldsConfig();
    if (!draft.scanValidationError().empty()) { updateRangeSummary(); return; }
    bool accepted = false;
    const QString name = QInputDialog::getText(this, "Save receive range", "Name",
        QLineEdit::Normal, {}, &accepted).trimmed();
    if (!accepted || name.isEmpty()) return;
    auto config = engine_.config();
    if (name.size() > 80 || config.presets.size() >= 128) {
        QMessageBox::warning(this, "Range", "Use a name of at most 80 characters and fewer than 128 saved ranges.");
        return;
    }
    for (const auto& p : config.presets) if (QString::fromStdString(p.name).compare(name, Qt::CaseInsensitive) == 0) {
        QMessageBox::warning(this, "Range", "That name is already in use. Choose another name.");
        return;
    }
    config.presets.push_back({name.toStdString(), draft.lowHz, draft.highHz, draft.stepHz,
        draft.bandwidthHz, draft.mode, draft.squelchDb, "Custom receive range", "", draft.dwellMs});
    engine_.setConfig(config); // Save the preset without applying pending RF settings.
    refreshRangeChoices();
    presetCombo_->setCurrentText(name);
    updateRangeSummary();
}

void SatcomScannerWidget::deleteRange() {
    if (!deleteRangeBtn_->isEnabled()) return;
    const auto name = presetCombo_->currentText().toStdString();
    if (QMessageBox::question(this, "Remove saved range",
        "Remove " + QString::fromStdString(name) + "?",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
    auto config = engine_.config();
    config.presets.erase(std::remove_if(config.presets.begin(), config.presets.end(),
        [&](const SatcomPreset& p) { return p.name == name; }), config.presets.end());
    engine_.setConfig(config);
    refreshRangeChoices();
    updateRangeSummary();
}

void SatcomScannerWidget::onStart() {
    if (!applyFieldsToConfig()) return;
    if (QCoreApplication::instance()->property("sdrtown.guiDryRun").toBool()) return;
    if (!engine_.start(true)) {
        QMessageBox::warning(
            this, "Satcom",
            QString::fromStdString(engine_.snapshot().lastStatus));
    }
}

void SatcomScannerWidget::onStop() {
    // Explicit Stop also disarms the scheduler; it must not reopen this radio
    // on the next timer tick after a GUI or authenticated session command.
    { const QSignalBlocker blocker(autoCaptureCheck_); autoCaptureCheck_->setChecked(false); }
    engine_.setAutoCaptureEnabled(false);
    autoCaptureOwned_ = false;
    autoEngineWasRunning_ = false;
    autoCapturePassKey_.clear();
    engine_.stopRecording();
    engine_.stop();
    engine_.disarmPass();
    recordingUi_ = false;
}

void SatcomScannerWidget::onSkip() {
    engine_.skip();
}

void SatcomScannerWidget::onRecord() {
    if (!recordingUi_) {
        if (engine_.startRecording()) recordingUi_ = true;
    } else {
        engine_.stopRecording();
        recordingUi_ = false;
    }
}

void SatcomScannerWidget::onApplyObserver() {
    SatObserverConfig observer = engine_.planner().observer();
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
    engine_.planner().setObserver(observer);
    if (engine_.sessionId().empty())
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
    SatCatalogueDialog dialog(this, &engine_.planner());
    if (dialog.exec() != QDialog::Accepted) return;
    engine_.planner().setCatalogueSelection(dialog.selectedIds());
    autoCapturePassKey_.clear();
    refreshPassesTable();
}

void SatcomScannerWidget::onRefreshTle() {
    if (tleBusy_) return;

    auto& planner = engine_.planner();
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
    if (QCoreApplication::instance()->property("sdrtown.guiDryRun").toBool()) return;
    const QString satId = downlinkCombo_->currentData(Qt::UserRole + 1).toString();
    const QString downlinkId = downlinkCombo_->currentData().toString();
    if (satId.isEmpty()) {
        QMessageBox::information(this, "Arm", "Select a downlink first.");
        return;
    }

    // The pass uses its catalogue downlink, not an unfinished Range Scan draft.
    auto& engine = engine_;
    auto config = engine.config();
    config.squelchDb = squelchSpin_->value();
    engine.setConfig(config);
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
    // Manual disarm always wins over the background scheduler (DEC-0218).
    engine_.setAutoCaptureEnabled(false);
    { QSignalBlocker block(autoCaptureCheck_); autoCaptureCheck_->setChecked(false); }
    if (autoCaptureOwned_) {
        stopAutoCapture(false);
        return;
    }
    engine_.stopRecording();
    engine_.disarmPass();
    recordingUi_ = false;
}

void SatcomScannerWidget::onArmSstv() {
    if (QCoreApplication::instance()->property("sdrtown.guiDryRun").toBool()) return;
    if (downlinkCombo_->currentData(Qt::UserRole + 2).toString() != "sstv") {
        QMessageBox::information(this, "Satellite SSTV", "Select an SSTV downlink. FUNcube BPSK telemetry and SSDV are separate protocols.");
        return;
    }
    onArmSelected();
}

void SatcomScannerWidget::onEditSstvProfile() {
    SatCatalogueEntry profile;
    const auto catalogue = engine_.planner().catalogue();
    for (const auto& entry : catalogue.entries()) for (const auto& dl : entry.downlinks) {
        if (dl.id == downlinkCombo_->currentData().toString().toStdString() && dl.role == "sstv") profile = entry;
    }
    if (profile.downlinks.empty()) {
        profile.name = "Custom SSTV"; profile.noradId = 25544;
        SatDownlink d; d.role = "sstv"; d.freqHz = 145.8e6; d.bandwidthHz = 20e3;
        profile.downlinks.push_back(d);
    }
    if (!profile.custom) {
        profile.id = "custom-" + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
        profile.name += " (custom)";
    }
    profile.custom = true;
    profile.downlinks.resize(1);
    auto& link = profile.downlinks.front();
    link.id = profile.id + "-sstv";
    QDialog dialog(this); dialog.setWindowTitle("Satellite SSTV profile");
    auto* form = new QFormLayout(&dialog);
    auto* name = new QLineEdit(QString::fromStdString(profile.name)); name->setObjectName("sstvProfileName");
    auto* norad = new QSpinBox; norad->setRange(1,99999); norad->setValue(profile.noradId);
    auto* frequency = new QDoubleSpinBox; frequency->setDecimals(6); frequency->setRange(.1,6000); frequency->setSuffix(" MHz"); frequency->setValue(link.freqHz/1e6);
    auto* rf = new QComboBox; rf->addItems({"NFM","USB","LSB","AM"}); rf->setCurrentText(QString::fromStdString(link.mode));
    auto* bandwidth = new QDoubleSpinBox; bandwidth->setRange(6,50); bandwidth->setSuffix(" kHz"); bandwidth->setValue(link.bandwidthHz/1000);
    auto* image = new QComboBox; image->addItem("Auto", "auto");
    for (const auto& mode : kSstvModes) if (std::string_view(mode.id) != "hamdrm") image->addItem(mode.label, mode.id);
    image->setCurrentIndex(image->findData(QString::fromStdString(link.imageMode)));
    auto* reference = new QLineEdit(QString::fromStdString(link.reference));
    auto* selected = new QCheckBox("Include in automatic pass capture"); selected->setChecked(profile.selected);
    form->addRow("Name", name); form->addRow("NORAD ID", norad); form->addRow("Downlink", frequency);
    form->addRow("RF mode", rf); form->addRow("Receive bandwidth", bandwidth); form->addRow("Image mode", image);
    form->addRow("Operator / schedule reference", reference); form->addRow(selected);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel);
    form->addRow(buttons); connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept); connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;
    profile.name = name->text().trimmed().toStdString(); profile.noradId = norad->value(); profile.selected = selected->isChecked();
    link.label = profile.name; link.freqHz = frequency->value()*1e6; link.mode = rf->currentText().toStdString();
    link.bandwidthHz = bandwidth->value()*1000; link.imageMode = image->currentData().toString().toStdString();
    link.reference = reference->text().toStdString(); link.verifiedUtc.clear(); // User-entered is not independently verified.
    profile.notes = "User configured; confirm RF frequency and activation with the operator.";
    std::string error;
    if (!engine_.planner().saveSstvProfile(profile,&error)) QMessageBox::warning(this,"Profile",QString::fromStdString(error));
    else { downlinkCombo_->clear(); refreshPassesTable(); downlinkCombo_->setCurrentIndex(downlinkCombo_->findData(QString::fromStdString(link.id))); }
}

void SatcomScannerWidget::stopAutoCapture(bool keepHandledKey) {
    auto& engine = engine_;
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

void SatcomScannerWidget::autoCaptureTick() {
    if (QCoreApplication::instance()->property("sdrtown.guiDryRun").toBool()) return;
    updateAutoCapture(engine_.planner().snapshot());
}

void SatcomScannerWidget::updateAutoCapture(const SatPassPlannerSnapshot& plan) {
    if (!autoCaptureCheck_ || !engine_.autoCaptureEnabled()) {
        if (autoCaptureOwned_) stopAutoCapture(false);
        return;
    }
    if (!plan.observer.configured || !plan.observer.valid()) {
        passStatusLabel_->setText("Auto capture waiting: apply observer location");
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

    auto& engine = engine_;
    if (plan.armed.armed) {
        if (!autoCaptureOwned_) return;
        if (!handledStillInRange) { stopAutoCapture(true); return; }
        const auto scan = engine.snapshot();
        const bool signalPresent = std::isfinite(scan.audioRmsDb) &&
                                   scan.audioRmsDb >= scan.config.squelchDb;
        if (signalPresent && scan.state != SatcomScannerState::Recording) {
            if (engine.startRecording()) recordingUi_ = true;
        }
        // The engine owns continuous SSTV capture; no second main-radio decoder.
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
        if (!position.tleValid || !position.armable || !position.inRange || position.downlinkId.empty() ||
            (engine_.config().sstvOnly && position.role != "sstv") ||
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
    if (!best) {
        for (const auto& position : plan.positions) if (!position.blockedReason.empty()) {
            passStatusLabel_->setText("Auto capture waiting: " + QString::fromStdString(position.blockedReason));
            break;
        }
        return;
    }

    const auto before = engine.snapshot();
    // Do not race an existing scanner's RF processing/configuration. An explicit
    // Stop leaves the named controller idle and eligible on the next timer tick.
    if (before.state != SatcomScannerState::Idle) return;
    autoEngineWasRunning_ = before.state != SatcomScannerState::Idle;
    std::string error;
    if (!engine.armPass(best->satId, best->downlinkId, true, false, &error)) {
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
    const auto plan = engine_.planner().snapshot();
    if (downlinkCombo_->count() == 0) {
        for (const auto& entry : plan.catalogue.entries()) {
            for (const auto& downlink : entry.downlinks) {
                downlinkCombo_->addItem(
                    QString("%1 MHz | %2 | %3").arg(downlink.freqHz / 1e6, 0, 'f', 3)
                        .arg(QString::fromStdString(downlink.mode), QString::fromStdString(downlink.label)),
                    QString::fromStdString(downlink.id));
                const int row = downlinkCombo_->count()-1;
                downlinkCombo_->setItemData(row, QString::fromStdString(entry.id), Qt::UserRole+1);
                downlinkCombo_->setItemData(row, QString::fromStdString(downlink.role), Qt::UserRole+2);
                downlinkCombo_->setItemData(row, QString::fromStdString(entry.notes + "\n" + downlink.reference +
                    "\nVerified: " + downlink.verifiedUtc + " | image: " + downlink.imageMode +
                    " | BW: " + std::to_string(downlink.bandwidthHz/1000) + " kHz"), Qt::ToolTipRole);
            }
        }
        downlinkCombo_->setCurrentIndex(downlinkCombo_->findData("iss-sstv"));
    }

    const QSignalBlocker blockPassTable(passTable_);
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
    const auto snapshot = engine_.snapshot();
    const auto& config = snapshot.config;
    const bool refreshPasses = (++passTableThrottle_ % 4) == 0;

    reloadSessionControls();
    if (monitorAudioCheck_) {
        monitorAudioCheck_->blockSignals(true);
        monitorAudioCheck_->setChecked(config.monitorAudio);
        monitorAudioCheck_->blockSignals(false);
    }

    const bool showSpectrum = snapshot.deviceConnected && snapshot.state != SatcomScannerState::Idle &&
        !snapshot.spectrumDb.empty() && std::isfinite(snapshot.spectrumRateHz) && snapshot.spectrumRateHz > 0;
    spectrum_->setVisible(showSpectrum);
    spectrumEmpty_->setVisible(!showSpectrum);
    if (showSpectrum) {
        spectrum_->updateSpectrum(
            snapshot.spectrumDb, snapshot.spectrumCenterHz, snapshot.spectrumRateHz);
        // The FFT is a single tuned capture, not an 82 MHz stitched survey.
        spectrum_->setFreqRange(snapshot.spectrumCenterHz - snapshot.spectrumRateHz/2,
                                snapshot.spectrumCenterHz + snapshot.spectrumRateHz/2);
        spectrum_->setSquelchThreshold(config.squelchDb);
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
            QString("Signal hold: %1 MHz").arg(snapshot.lockHz / 1e6, 0, 'f', 3));
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
                : (snapshot.state == SatcomScannerState::Idle ? "OFF" : (config.monitorAudio ? "UNAVAILABLE" : "OFF")))
            .arg(static_cast<qulonglong>(snapshot.iqDiscontinuities))
            .arg(static_cast<qulonglong>(snapshot.logDropped))
            .arg(outputDetail));
    const bool active = snapshot.state != SatcomScannerState::Idle || snapshot.passArmed;
    const bool healthy = !active ||
                         (snapshot.deviceConnected && snapshot.streamState == "live hardware");
    statusHealth_->setStyleSheet(!active ? QString() : (healthy
        ? "color: #7cce91;"
        : "color: #ff7777;"));

    if (!snapshot.recentDecodes.empty()) {
        QString all;
        const size_t count = snapshot.recentDecodes.size();
        const size_t begin = count > 40 ? count - 40 : 0;
        for (size_t i = begin; i < count; ++i)
            all += QString::fromStdString(snapshot.recentDecodes[i]) + "\n";
        if (logView_->toPlainText() != all) logView_->setPlainText(all);
    }

    deviceCombo_->setEnabled(!active);
    scanSettings_->setEnabled(!active);
    squelchSpin_->setEnabled(!active);
    squelchSlider_->setEnabled(!active);
    stopBtn_->setEnabled(active);
    skipBtn_->setEnabled(active && !snapshot.passArmed);
    recordBtn_->setEnabled(snapshot.state == SatcomScannerState::Locked ||
                           snapshot.state == SatcomScannerState::Recording || snapshot.passArmed);
    recordBtn_->setText(recordingUi_ ? "Stop recording" : "Record audio");
    updateRangeSummary();

    if (refreshPasses) refreshPassesTable();
}
