#include "InmarsatWidget.h"
#include "InmarsatEngine.h"
#include "InmarsatBandPlan.h"
#include "InmarsatMessageStore.h"
#include "DeviceManager.h"
#include "SdrDeviceCandidate.h"
#include "InmarsatReplayDialog.h"
#include "InmarsatDiagnostics.h"
#include "InmarsatMapWidget.h"
#include "InmarsatWatchSpectrum.h"
#include <QTabWidget>
#include <QDoubleSpinBox>
#include <QDateTime>

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QHeaderView>
#include <QHideEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <limits>
#include <cmath>

namespace {

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

InmarsatWidget::InmarsatWidget(QWidget* parent)
    : QWidget(parent)
{
    connectInmarsatRemoteDiagnostics();
    buildUi();
    applyNeonStyle();
    refreshDevices();
    reloadBandPlans();
    syncTuningControls();
    refreshTimer_ = new QTimer(this);
    connect(refreshTimer_, &QTimer::timeout, this, &InmarsatWidget::refreshUi);
    visualTimer_=new QTimer(this);visualTimer_->setObjectName("inmarsatVisualTimer");
    visualTimer_->setTimerType(Qt::PreciseTimer);
    connect(visualTimer_,&QTimer::timeout,this,&InmarsatWidget::refreshVisuals);
    // UI owns the refresh timer. A worker-copied callback must not retain this
    // widget while a dock is being destroyed.
}

void InmarsatWidget::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    refreshDevices();
    if (refreshTimer_ && !refreshTimer_->isActive()) refreshTimer_->start(500);
    if (visualTimer_) visualTimer_->start(50); // DEC-0127: 20 Hz presentation budget.
    refreshUi();
    refreshVisuals();
}

void InmarsatWidget::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    if (refreshTimer_) refreshTimer_->stop();
    if (visualTimer_) visualTimer_->stop();
}

InmarsatWidget::~InmarsatWidget() {
    InmarsatEngine::instance().setUpdateCallback({});
}

void InmarsatWidget::applyNeonStyle() {
    setStyleSheet(R"(
        InmarsatWidget {
            background: #000000; color: #39FF14;
            font-family: Consolas, "Courier New", monospace;
        }
        QLabel { color: #39FF14; }
        QComboBox, QPlainTextEdit, QTableWidget {
            background: #0a0f0a; color: #39FF14; border: 1px solid #1f3d1f; border-radius: 6px;
            padding: 4px; font-weight: 700;
        }
        QHeaderView::section { background: #0a0f0a; color: #39FF14; border: 1px solid #1f3d1f; }
        QPushButton {
            background: #0c160c; color: #39FF14; border: 2px solid #39FF14; border-radius: 10px;
            padding: 8px 12px; font-weight: 800; min-width: 90px;
        }
        QPushButton:hover { background: #132213; }
        QPushButton:disabled { color: #2a5a2a; border-color: #1a331a; }
        QCheckBox { color: #39FF14; }
    )");
}

void InmarsatWidget::buildUi() {
    auto* root = new QVBoxLayout(this);
    auto* title = new QLabel("INMARSAT AERO / EGC");
    title->setStyleSheet("font-size: 22px; font-weight: 900; letter-spacing: 2px;");
    root->addWidget(title);
    root->addWidget(new QLabel(
        "Classic Aero experimental | ADS-C | C-channel voice | EGC probe only"));

    auto* receiverRow = new QHBoxLayout();
    receiverRow->addWidget(new QLabel("INMARSAT RX DEVICE"));
    deviceCombo_ = new QComboBox();
    deviceCombo_->setToolTip(
        "Choose the SDR used by Inmarsat. START / TAKE OVER reuses an already-live Listen "
                  "receiver without reopening it, or starts the selected idle receiver. Switching from P25 requires confirmation.");
    receiverRow->addWidget(deviceCombo_, 1);
    root->addLayout(receiverRow);
    auto* replayButton = new QPushButton("Open IQ replay...");
    replayButton->setObjectName("inmarsatReplay");
    connect(replayButton, &QPushButton::clicked, this, [this] {
        auto* dialog = new InmarsatReplayDialog(this);
        configureInmarsatReplaySharing(*dialog);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
    });
    root->addWidget(replayButton);

    auto* top = new QHBoxLayout();
    top->addWidget(new QLabel("BAND PLAN"));
    planCombo_ = new QComboBox();
    planCombo_->setObjectName("inmarsatBandPlan");
    top->addWidget(planCombo_, 1);
    voiceFollowCheck_ = new QCheckBox("Automatic data / voice watch");
    voiceFollowCheck_->setObjectName("inmarsatAutoWatch");
    voiceFollowCheck_->setChecked(InmarsatEngine::instance().config().watch.enabled);
    voiceFollowCheck_->setToolTip("Cycle saved channels for fresh aircraft positions and decoded voice. Starts only when START is pressed.");
    recordCheck_ = new QCheckBox("Record WAV");
    recordCheck_->setChecked(InmarsatEngine::instance().config().recordVoice);
    speakerCheck_=new QCheckBox("Speaker audio (system default)");
    speakerCheck_->setChecked(InmarsatEngine::instance().config().playAudio);
    top->addWidget(speakerCheck_);
    connect(speakerCheck_,&QCheckBox::toggled,this,[](bool on){auto cfg=InmarsatEngine::instance().config();cfg.playAudio=on;InmarsatEngine::instance().setConfig(cfg);});
    top->addWidget(voiceFollowCheck_);
    top->addWidget(recordCheck_);
    startBtn_ = new QPushButton("START / TAKE OVER");
    startBtn_->setObjectName("inmarsatStart");
    startBtn_->setToolTip(
        "Start on the selected real SDR and tune the chosen channel. An ordinary Listen session "
        "is temporarily retuned and restored after Stop.");
    stopBtn_ = new QPushButton("STOP / RESTORE");
    root->addLayout(top);
    auto* actions=new QHBoxLayout;actions->addWidget(startBtn_);actions->addWidget(stopBtn_);actions->addStretch();
    root->addLayout(actions);

    channelTable_ = new QTableWidget(0, 4);
    channelTable_->setObjectName("inmarsatPresetChannels");
    channelTable_->setHorizontalHeaderLabels({"Label", "MHz", "Mode", "bit/s"});
    channelTable_->horizontalHeader()->setStretchLastSection(true);
    channelTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    channelTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    channelTable_->setMaximumHeight(220);
    auto* tuneRow=new QHBoxLayout;
    frequency_=new QDoubleSpinBox;frequency_->setRange(1,100000);frequency_->setDecimals(6);
    frequency_->setObjectName("inmarsatFrequencyMHz");
    frequency_->setSuffix(" MHz");frequency_->setKeyboardTracking(false);
    decoderCombo_=new QComboBox;
    decoderCombo_->setObjectName("inmarsatDecoder");
    decoderCombo_->addItem("Aero data 10500",10500);decoderCombo_->addItem("Aero voice 8400",8400);
    decoderCombo_->addItem("Aero data 1200",1200);decoderCombo_->addItem("Aero data 600",600);
    decoderCombo_->addItem("Aero burst 1200",-1200);decoderCombo_->addItem("Aero burst 10500",-10500);
    decoderCombo_->addItem("EGC probe (no voice)",0);
    auto* tune=new QPushButton("Tune");
    tune->setObjectName("inmarsatTune");
    tuneRow->addWidget(frequency_);tuneRow->addWidget(decoderCombo_);tuneRow->addWidget(tune);root->addLayout(tuneRow);
    presetHint_ = new QLabel;
    presetHint_->setObjectName("inmarsatRatePresetStatus");
    presetHint_->setWordWrap(true);
    root->addWidget(presetHint_);
    connect(frequency_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            presetHint_, &QLabel::clear);
    // DEC-0133: activated is user intent; currentIndexChanged also fires on restore.
    connect(decoderCombo_, QOverload<int>::of(&QComboBox::activated), this, [this](int) {
        const auto* plan = InmarsatBandPlanStore::instance().findById(
            planCombo_->currentData().toString().toStdString());
        const int rate = decoderCombo_->currentData().toInt();
        const std::string mode = rate == 8400 ? "aero_voice" :
            rate == 10500 ? "aero_oqpsk" : "aero_msk";
        const InmarsatChannel* selected = nullptr;
        if (plan && rate > 0) {
            frequency_->interpretText();
            for (const auto& channel : plan->channels) {
                if (channel.baud != rate || channel.mode != mode) continue;
                if (!selected) selected = &channel;
                if (std::abs(channel.freqHz - frequency_->value() * 1e6) < 0.5) {
                    selected = &channel;
                    break;
                }
            }
        }
        if (!selected) {
            presetHint_->setText("No surveyed preset for this rate/mode; frequency unchanged.");
            return;
        }
        frequency_->setValue(selected->freqHz / 1e6);
        presetHint_->setText("Survey preset (not yet tuned): " + QString::fromStdString(selected->label));
    });
    connect(tune,&QPushButton::clicked,this,[this]{
        if(voiceFollowCheck_->isChecked())voiceFollowCheck_->setChecked(false);
        if(!applyTuningControls())
            QMessageBox::warning(this,"Inmarsat","Could not tune the selected channel");
    });

    lockLabel_ = new QLabel("Receiver: —");
    statusLabel_ = new QLabel("Idle");
    statusLabel_->setWordWrap(true);
    root->addWidget(lockLabel_);
    root->addWidget(statusLabel_);
    audioLabel_ = new QLabel;
    audioLabel_->setObjectName("inmarsatAudioStatus");
    audioLabel_->setWordWrap(true);
    lockLabel_->setWordWrap(true);
    root->addWidget(audioLabel_);

    msgView_ = new QPlainTextEdit();
    msgView_->setReadOnly(true);
    auto* tabs=new QTabWidget;map_=new InmarsatMapWidget;
    tabs->addTab(buildWatchUi(),"Watch channels");tabs->addTab(map_,"Aircraft map");
    tabs->addTab(msgView_,"Messages");tabs->addTab(channelTable_,"Band plan");root->addWidget(tabs,1);

    connect(startBtn_, &QPushButton::clicked, this, &InmarsatWidget::onStart);
    connect(stopBtn_, &QPushButton::clicked, this, &InmarsatWidget::onStop);
    connect(planCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &InmarsatWidget::onBandPlanChanged);
    connect(channelTable_, &QTableWidget::cellDoubleClicked,
            this, &InmarsatWidget::onChannelActivated);
    connect(voiceFollowCheck_, &QCheckBox::toggled,
            this, &InmarsatWidget::onVoiceFollowToggled);
    connect(recordCheck_, &QCheckBox::toggled,
            this, &InmarsatWidget::onRecordToggled);
    connect(deviceCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int row) {
        if (row < 0) return;
        bool ok = false;
        const size_t index = static_cast<size_t>(
            deviceCombo_->itemData(row).toString().toULongLong(&ok));
        const auto devices = DeviceManager::instance().getDevices();
        if (!ok || index >= devices.size() ||
            !SdrDeviceCandidate::canAttemptRealHardware(devices[index].label)) return;
        auto config = InmarsatEngine::instance().config();
        config.deviceIndex = index;
        config.deviceStableKey = devices[index].stableKey;
        InmarsatEngine::instance().setConfig(config);
        statusLabel_->setText(
            "Inmarsat receiver selected: " + QString::fromStdString(devices[index].label));
    });
}

void InmarsatWidget::refreshDevices() {
    if (!deviceCombo_) return;
    const auto devices = DeviceManager::instance().getDevices();
    const auto config = InmarsatEngine::instance().config();

    struct Item { QString text; QString data; };
    std::vector<Item> items;
    int selectedRow = -1;
    for (size_t index = 0; index < devices.size(); ++index) {
        if (!SdrDeviceCandidate::canAttemptRealHardware(devices[index].label)) continue;
        const QString data = QString::number(static_cast<qulonglong>(index));
        items.push_back({receiverDisplayText(index, devices[index],
                                             DeviceManager::instance().isStreaming(index)), data});
        if ((!config.deviceStableKey.empty() && devices[index].stableKey == config.deviceStableKey) ||
            (config.deviceStableKey.empty() && config.deviceIndex == index)) {
            selectedRow = static_cast<int>(items.size()) - 1;
        }
    }

    bool rebuild = deviceCombo_->count() != static_cast<int>(items.size());
    if (!rebuild) {
        for (int row = 0; row < deviceCombo_->count(); ++row) {
            if (deviceCombo_->itemText(row) != items[static_cast<size_t>(row)].text ||
                deviceCombo_->itemData(row).toString() != items[static_cast<size_t>(row)].data) {
                rebuild = true;
                break;
            }
        }
    }
    if (!rebuild) return;

    deviceCombo_->blockSignals(true);
    deviceCombo_->clear();
    for (const auto& item : items) deviceCombo_->addItem(item.text, item.data);
    if (selectedRow < 0 && !items.empty()) selectedRow = 0;
    deviceCombo_->setCurrentIndex(selectedRow);
    deviceCombo_->setEnabled(!items.empty());
    deviceCombo_->blockSignals(false);
}

void InmarsatWidget::reloadBandPlans() {
    planCombo_->blockSignals(true);
    planCombo_->clear();
    const auto& plans = InmarsatBandPlanStore::instance().plans();
    const auto config = InmarsatEngine::instance().config();
    int selected = 0;
    for (int index = 0; index < static_cast<int>(plans.size()); ++index) {
        planCombo_->addItem(
            QString::fromStdString(plans[index].name + " (" + plans[index].id + ")"),
            QString::fromStdString(plans[index].id));
        if (plans[index].id == config.bandPlanId) selected = index;
    }
    planCombo_->setCurrentIndex(selected);
    planCombo_->blockSignals(false);
    // DEC-0123: opening the panel must not retune/reset a saved voice decoder.
    populateChannels();
}

void InmarsatWidget::onBandPlanChanged(int index) {
    if (index < 0) return;
    voiceFollowCheck_->setChecked(false);
    const QString id = planCombo_->itemData(index).toString();
    if (!InmarsatEngine::instance().selectBandPlan(id.toStdString())) {
        statusLabel_->setText("Could not select/restart the requested Inmarsat band plan");
    }
    populateChannels();
    syncTuningControls();
}

void InmarsatWidget::populateChannels() {
    const QString id = planCombo_->currentData().toString();
    const auto* plan = InmarsatBandPlanStore::instance().findById(id.toStdString());
    channelTable_->setRowCount(0);
    channelTable_->setToolTip(plan ? QString::fromStdString(plan->source) : QString());
    if (!plan) return;
    channelTable_->setRowCount(static_cast<int>(plan->channels.size()));
    for (int row = 0; row < static_cast<int>(plan->channels.size()); ++row) {
        const auto& channel = plan->channels[static_cast<size_t>(row)];
        channelTable_->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(channel.label)));
        channelTable_->setItem(row, 1, new QTableWidgetItem(QString::number(channel.freqHz / 1e6, 'f', 4)));
        channelTable_->setItem(row, 2, new QTableWidgetItem(QString::fromStdString(channel.mode)));
        channelTable_->setItem(row, 3, new QTableWidgetItem(QString::number(channel.baud)));
    }
}

void InmarsatWidget::onChannelActivated(int row, int) {
    if (row < 0) return;
    voiceFollowCheck_->setChecked(false);
    const QString id = planCombo_->currentData().toString();
    const auto* plan = InmarsatBandPlanStore::instance().findById(id.toStdString());
    if (!plan || row >= static_cast<int>(plan->channels.size())) return;
    const auto& channel = plan->channels[static_cast<size_t>(row)];
    if (!InmarsatEngine::instance().selectChannel(channel.freqHz, channel.mode, channel.baud)) {
        QMessageBox::warning(this, "Inmarsat", "Could not tune/restart the selected channel.");
    } else {
        constellationChannel_->setCurrentIndex(0);
    }
    syncTuningControls();
}

void InmarsatWidget::syncTuningControls() {
    presetHint_->clear();
    const auto cfg = InmarsatEngine::instance().config();
    frequency_->setValue(cfg.channelHz / 1e6);
    const int selection = cfg.mode == "egc" ? 0 : cfg.mode == "aero_burst" ? -cfg.baud : cfg.baud;
    decoderCombo_->setCurrentIndex(decoderCombo_->findData(selection));
}

bool InmarsatWidget::applyTuningControls() {
    if (decoderCombo_->currentIndex() < 0) return false;
    frequency_->interpretText();
    const int rate = decoderCombo_->currentData().toInt();
    const std::string mode = rate == 0 ? "egc" : rate < 0 ? "aero_burst" :
        rate == 8400 ? "aero_voice" : rate < 8400 ? "aero_msk" : "aero_oqpsk";
    const bool ok = InmarsatEngine::instance().selectChannel(frequency_->value() * 1e6,
                                                    mode, rate == 0 ? 1200 : std::abs(rate));
    if (ok) {
        presetHint_->clear();
        // DEC-0134: manual decoder snapshots have no saved watch-channel UUID.
        constellationChannel_->setCurrentIndex(0);
    }
    return ok;
}

void InmarsatWidget::onVoiceFollowToggled(bool enabled) {
    auto config = InmarsatEngine::instance().config();
    config.watch.enabled = enabled;
    if(!InmarsatEngine::instance().setConfig(config)) {
        QSignalBlocker blocker(voiceFollowCheck_);voiceFollowCheck_->setChecked(!enabled);
        QMessageBox::warning(this,"Watch list",QString::fromStdString(InmarsatEngine::instance().snapshot().lastStatus));
    }
}

void InmarsatWidget::onRecordToggled(bool enabled) {
    auto config = InmarsatEngine::instance().config();
    config.recordVoice = enabled;
    InmarsatEngine::instance().setConfig(config);
}

void InmarsatWidget::onStart() {
    refreshDevices();
    auto& engine = InmarsatEngine::instance();
    // START and Tune use exactly the same visible frequency/decoder selection.
    if (!engine.config().watch.enabled && !applyTuningControls()) {
        QMessageBox::warning(this, "Inmarsat", QString::fromStdString(engine.snapshot().lastStatus));
        return;
    }
    auto takeover = engine.prepareTakeover();
    if (takeover.needsP25Confirmation) {
        const auto answer = QMessageBox::question(this, "Switch from P25 to Inmarsat",
            "P25 is configured on this SDR. Stop P25 monitoring and talkgroup follows "
            "and use it for Inmarsat?\n\nP25 will stay stopped until you select Monitor CC again.",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) return;
        takeover = engine.prepareTakeover(true); // Recheck after the modal dialog.
    }
    if (!takeover.ready) {
        QMessageBox::warning(this, "Inmarsat", QString::fromStdString(takeover.error));
        return;
    }
    if (!engine.start(true)) {
        QMessageBox::warning(
            this, "Inmarsat",
            QString::fromStdString(engine.snapshot().lastStatus));
    }
}

void InmarsatWidget::onStop() {
    InmarsatEngine::instance().stop();
}

void InmarsatWidget::refreshUi() {
    refreshDevices();
    const auto snapshot = InmarsatEngine::instance().snapshot();
    statusLabel_->setText(QString::fromStdString(snapshot.lastStatus));

    const QString device = snapshot.activeDeviceIndex == std::numeric_limits<size_t>::max()
        ? QString::fromStdString(snapshot.deviceLabel.empty() ? "selected receiver" : snapshot.deviceLabel)
        : QString("RX %1: %2")
              .arg(static_cast<qulonglong>(snapshot.activeDeviceIndex))
              .arg(QString::fromStdString(snapshot.deviceLabel));
    lockLabel_->setText(
        QString("%1  Stream=%2  Carrier=%3  Quality=%4  FrameLock=%5  Tuned=%6 MHz  Raw=%7  Valid=%8")
            .arg(device)
            .arg(QString::fromStdString(snapshot.streamState))
            .arg(snapshot.carrierDetected ? "YES" : "no")
            .arg(snapshot.quality, 0, 'f', 2)
            .arg(snapshot.locked ? "YES" : "no")
            .arg(snapshot.tunedHz / 1e6, 0, 'f', 3)
            .arg(snapshot.rawBlocks)
            .arg(snapshot.validatedFrames));

    const bool running = snapshot.state != InmarsatEngineState::Idle;
    const auto audio = snapshot.diagnostics.value("audio", nlohmann::json::object());
    const auto error = audio.value("speakerError", std::string{});
    QString audioState;
    if (!error.empty()) audioState = QString::fromStdString(error);
    else if (!snapshot.config.playAudio) audioState = "Speaker off";
    else if (!running) audioState = "Stopped";
    else if (snapshot.diagnostics.contains("watch") ? snapshot.diagnostics["watch"].value("phase",std::string{})!="voice" :
             snapshot.config.baud != 8400 || snapshot.config.mode == "egc" || snapshot.config.mode == "aero_burst")
        audioState = "Data channel (no voice output)";
    else if (audio.value("pcmReceived", uint64_t{0}) == 0)
        audioState = "Waiting for validated C-channel voice";
    else audioState = audio.value("speakerRunning", false)
        ? "Speaker: " + QString::fromStdString(audio.value("speakerDevice", std::string{})) : "Speaker waiting";
    audioLabel_->setText(QString("Decoder: %1 / %2  |  Voice frames: %3  PCM samples: %4  |  %5")
        .arg(QString::fromStdString(snapshot.config.mode)).arg(snapshot.config.baud)
        .arg(snapshot.voiceFrames).arg(audio.value("pcmReceived", uint64_t{0})).arg(audioState));
    if(snapshot.diagnostics.contains("watch")) {
        const auto& w=snapshot.diagnostics["watch"];
        audioLabel_->setText(QString("Auto %1 | %2 decoder(s) | Voice frames: %3 | PCM: %4 | %5")
            .arg(QString::fromStdString(w.value("phase",std::string{}))).arg(w["channels"].size())
            .arg(snapshot.voiceFrames).arg(audio.value("pcmReceived",uint64_t{0})).arg(audioState));
    }
    audioLabel_->setToolTip(QString::fromStdString(snapshot.diagnosticLog));
    auto report=snapshot.diagnostics;if(!running)report["voiceActive"]=false;
    // Live aircraft survive a manual data-to-voice retune. Only native, validated
    // ADS-C messages enter this view; replay owns an entirely separate map.
    report["positions"]=nlohmann::json::array();
    std::vector<uint32_t> seen;
    const auto mapMessages=InmarsatMessageStore::instance().recent(500);
    for(auto it=mapMessages.rbegin();it!=mapMessages.rend();++it) {
        const auto& m=*it;
        if(!m.validated || !m.hasPosition || !m.aesId || std::find(seen.begin(),seen.end(),m.aesId)!=seen.end())continue;
        seen.push_back(m.aesId);
        const double age=std::max(0.0,QDateTime::currentMSecsSinceEpoch()/1000.0-m.unixTime);
        report["positions"].push_back({{"aesId",m.aesId},{"latDeg",m.latDeg},{"lonDeg",m.lonDeg},
            {"altitudeFt",m.altitudeFt},{"registration",m.registration},{"callsign",m.callsign},
            {"secondsPastHour",m.positionSecondsPastHour},{"ageSeconds",age},
            {"stale",age>snapshot.config.watch.refreshSeconds}});
    }
    map_->setReport(report,false);
    speakerCheck_->setEnabled(!running);recordCheck_->setEnabled(!running);
    startBtn_->setEnabled(!running);
    stopBtn_->setEnabled(running);
    deviceCombo_->setEnabled(!running && deviceCombo_->count() > 0);
    updateWatchUi(running,snapshot.diagnostics);
    recordCheck_->blockSignals(true);
    recordCheck_->setChecked(snapshot.config.recordVoice);
    recordCheck_->blockSignals(false);

    const auto messages = InmarsatMessageStore::instance().recent(40);
    QString text;
    for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
        text += QString("[%1] %2 %3\n")
                    .arg(InmarsatMessage::kindName(it->kind))
                    .arg(QString::fromStdString(it->label))
                    .arg(QString::fromStdString(it->text).left(160));
    }
    if (msgView_->toPlainText() != text) msgView_->setPlainText(text);
}
