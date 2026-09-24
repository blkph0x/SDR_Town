#include "InmarsatWidget.h"
#include "InmarsatEngine.h"
#include "InmarsatBandPlan.h"
#include "InmarsatMessageStore.h"
#include "DeviceManager.h"
#include "SdrDeviceCandidate.h"
#include "InmarsatReplayDialog.h"
#include "InmarsatDiagnostics.h"

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
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <limits>

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
    refreshTimer_ = new QTimer(this);
    connect(refreshTimer_, &QTimer::timeout, this, &InmarsatWidget::refreshUi);
    InmarsatEngine::instance().setUpdateCallback([this]() {
        QMetaObject::invokeMethod(this, "refreshUi", Qt::QueuedConnection);
    });
}

void InmarsatWidget::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    refreshDevices();
    if (refreshTimer_ && !refreshTimer_->isActive()) refreshTimer_->start(500);
    refreshUi();
}

void InmarsatWidget::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    if (refreshTimer_) refreshTimer_->stop();
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
        "Experimental physical-layer monitor: receiver/tuning are real; validated unique-word/FEC and voice follow are not implemented."));

    auto* receiverRow = new QHBoxLayout();
    receiverRow->addWidget(new QLabel("INMARSAT RX DEVICE"));
    deviceCombo_ = new QComboBox();
    deviceCombo_->setToolTip(
        "Choose the SDR used by Inmarsat. START / TAKE OVER reuses an already-live Listen "
        "receiver without reopening it, or starts the selected idle receiver. P25 is never interrupted.");
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
    top->addWidget(planCombo_, 1);
    voiceFollowCheck_ = new QCheckBox("Voice follow (not implemented)");
    voiceFollowCheck_->setChecked(false);
    voiceFollowCheck_->setEnabled(false);
    voiceFollowCheck_->setToolTip("Needs unique-word sync and verified C-assign. Not in this release.");
    recordCheck_ = new QCheckBox("Record voice (not implemented)");
    recordCheck_->setEnabled(false);
    top->addWidget(voiceFollowCheck_);
    top->addWidget(recordCheck_);
    startBtn_ = new QPushButton("START / TAKE OVER");
    startBtn_->setToolTip(
        "Start on the selected real SDR and tune the chosen channel. An ordinary Listen session "
        "is temporarily retuned and restored after Stop.");
    stopBtn_ = new QPushButton("STOP / RESTORE");
    top->addWidget(startBtn_);
    top->addWidget(stopBtn_);
    root->addLayout(top);

    channelTable_ = new QTableWidget(0, 4);
    channelTable_->setHorizontalHeaderLabels({"Label", "MHz", "Mode", "Baud"});
    channelTable_->horizontalHeader()->setStretchLastSection(true);
    channelTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    channelTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    channelTable_->setMaximumHeight(220);
    root->addWidget(channelTable_);

    lockLabel_ = new QLabel("Receiver: —");
    statusLabel_ = new QLabel("Idle");
    statusLabel_->setWordWrap(true);
    root->addWidget(lockLabel_);
    root->addWidget(statusLabel_);

    msgView_ = new QPlainTextEdit();
    msgView_->setReadOnly(true);
    root->addWidget(msgView_, 1);

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
    onBandPlanChanged(selected);
}

void InmarsatWidget::onBandPlanChanged(int index) {
    if (index < 0) return;
    const QString id = planCombo_->itemData(index).toString();
    if (!InmarsatEngine::instance().selectBandPlan(id.toStdString())) {
        statusLabel_->setText("Could not select/restart the requested Inmarsat band plan");
    }
    const auto* plan = InmarsatBandPlanStore::instance().findById(id.toStdString());
    channelTable_->setRowCount(0);
    if (!plan) return;
    channelTable_->setRowCount(static_cast<int>(plan->channels.size()));
    for (int row = 0; row < static_cast<int>(plan->channels.size()); ++row) {
        const auto& channel = plan->channels[static_cast<size_t>(row)];
        channelTable_->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(channel.label)));
        channelTable_->setItem(row, 1, new QTableWidgetItem(QString::number(channel.freqHz / 1e6, 'f', 3)));
        channelTable_->setItem(row, 2, new QTableWidgetItem(QString::fromStdString(channel.mode)));
        channelTable_->setItem(row, 3, new QTableWidgetItem(QString::number(channel.baud)));
    }
}

void InmarsatWidget::onChannelActivated(int row, int) {
    if (row < 0) return;
    const QString id = planCombo_->currentData().toString();
    const auto* plan = InmarsatBandPlanStore::instance().findById(id.toStdString());
    if (!plan || row >= static_cast<int>(plan->channels.size())) return;
    const auto& channel = plan->channels[static_cast<size_t>(row)];
    if (!InmarsatEngine::instance().selectChannel(channel.freqHz, channel.mode, channel.baud)) {
        QMessageBox::warning(this, "Inmarsat", "Could not tune/restart the selected channel.");
    }
}

void InmarsatWidget::onVoiceFollowToggled(bool enabled) {
    auto config = InmarsatEngine::instance().config();
    config.voiceFollow = enabled;
    InmarsatEngine::instance().setConfig(config);
}

void InmarsatWidget::onRecordToggled(bool enabled) {
    auto config = InmarsatEngine::instance().config();
    config.recordVoice = enabled;
    InmarsatEngine::instance().setConfig(config);
}

void InmarsatWidget::onStart() {
    refreshDevices();
    if (!InmarsatEngine::instance().start(true)) {
        QMessageBox::warning(
            this, "Inmarsat",
            QString::fromStdString(InmarsatEngine::instance().snapshot().lastStatus));
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
    startBtn_->setEnabled(!running);
    stopBtn_->setEnabled(running);
    deviceCombo_->setEnabled(!running && deviceCombo_->count() > 0);

    voiceFollowCheck_->blockSignals(true);
    voiceFollowCheck_->setChecked(snapshot.config.voiceFollow);
    voiceFollowCheck_->blockSignals(false);
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
