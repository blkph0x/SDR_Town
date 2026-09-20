#include "InmarsatWidget.h"
#include "InmarsatEngine.h"
#include "InmarsatBandPlan.h"
#include "InmarsatMessageStore.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QHeaderView>
#include <QHideEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShowEvent>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

InmarsatWidget::InmarsatWidget(QWidget* parent)
    : QWidget(parent)
{
    buildUi();
    applyNeonStyle();
    reloadBandPlans();
    refreshTimer_ = new QTimer(this);
    connect(refreshTimer_, &QTimer::timeout, this, &InmarsatWidget::refreshUi);
    // Timer starts only while visible — must not poll on the main listening path.
    InmarsatEngine::instance().setUpdateCallback([this]() {
        QMetaObject::invokeMethod(this, "refreshUi", Qt::QueuedConnection);
    });
}

void InmarsatWidget::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
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
        QCheckBox { color: #39FF14; }
    )");
}

void InmarsatWidget::buildUi() {
    auto* root = new QVBoxLayout(this);
    auto* title = new QLabel("INMARSAT AERO / EGC");
    title->setStyleSheet("font-size: 22px; font-weight: 900; letter-spacing: 2px;");
    root->addWidget(title);
    root->addWidget(new QLabel("Experimental prototype: no unique-word/FEC. ADS-C parse + band plans only. Not RF-qualified."));

    auto* top = new QHBoxLayout();
    top->addWidget(new QLabel("BAND PLAN"));
    planCombo_ = new QComboBox();
    top->addWidget(planCombo_, 1);
    voiceFollowCheck_ = new QCheckBox("Voice follow");
    voiceFollowCheck_->setChecked(true);
    recordCheck_ = new QCheckBox("Record voice");
    top->addWidget(voiceFollowCheck_);
    top->addWidget(recordCheck_);
    startBtn_ = new QPushButton("Start");
    stopBtn_ = new QPushButton("Stop");
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

    lockLabel_ = new QLabel("Lock: —");
    statusLabel_ = new QLabel("Idle");
    root->addWidget(lockLabel_);
    root->addWidget(statusLabel_);

    msgView_ = new QPlainTextEdit();
    msgView_->setReadOnly(true);
    root->addWidget(msgView_, 1);

    connect(startBtn_, &QPushButton::clicked, this, &InmarsatWidget::onStart);
    connect(stopBtn_, &QPushButton::clicked, this, &InmarsatWidget::onStop);
    connect(planCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &InmarsatWidget::onBandPlanChanged);
    connect(channelTable_, &QTableWidget::cellDoubleClicked, this, &InmarsatWidget::onChannelActivated);
    connect(voiceFollowCheck_, &QCheckBox::toggled, this, &InmarsatWidget::onVoiceFollowToggled);
    connect(recordCheck_, &QCheckBox::toggled, this, &InmarsatWidget::onRecordToggled);
}

void InmarsatWidget::reloadBandPlans() {
    planCombo_->blockSignals(true);
    planCombo_->clear();
    const auto& plans = InmarsatBandPlanStore::instance().plans();
    const auto cfg = InmarsatEngine::instance().config();
    int sel = 0;
    for (int i = 0; i < static_cast<int>(plans.size()); ++i) {
        planCombo_->addItem(QString::fromStdString(plans[i].name + " (" + plans[i].id + ")"),
                            QString::fromStdString(plans[i].id));
        if (plans[i].id == cfg.bandPlanId) sel = i;
    }
    planCombo_->setCurrentIndex(sel);
    planCombo_->blockSignals(false);
    onBandPlanChanged(sel);
}

void InmarsatWidget::onBandPlanChanged(int index) {
    if (index < 0) return;
    const QString id = planCombo_->itemData(index).toString();
    InmarsatEngine::instance().selectBandPlan(id.toStdString());
    const auto* p = InmarsatBandPlanStore::instance().findById(id.toStdString());
    channelTable_->setRowCount(0);
    if (!p) return;
    channelTable_->setRowCount(static_cast<int>(p->channels.size()));
    for (int r = 0; r < static_cast<int>(p->channels.size()); ++r) {
        const auto& c = p->channels[static_cast<size_t>(r)];
        channelTable_->setItem(r, 0, new QTableWidgetItem(QString::fromStdString(c.label)));
        channelTable_->setItem(r, 1, new QTableWidgetItem(QString::number(c.freqHz / 1e6, 'f', 3)));
        channelTable_->setItem(r, 2, new QTableWidgetItem(QString::fromStdString(c.mode)));
        channelTable_->setItem(r, 3, new QTableWidgetItem(QString::number(c.baud)));
    }
}

void InmarsatWidget::onChannelActivated(int row, int) {
    if (row < 0) return;
    const QString id = planCombo_->currentData().toString();
    const auto* p = InmarsatBandPlanStore::instance().findById(id.toStdString());
    if (!p || row >= static_cast<int>(p->channels.size())) return;
    const auto& c = p->channels[static_cast<size_t>(row)];
    InmarsatEngine::instance().selectChannel(c.freqHz, c.mode, c.baud);
}

void InmarsatWidget::onVoiceFollowToggled(bool on) {
    auto cfg = InmarsatEngine::instance().config();
    cfg.voiceFollow = on;
    InmarsatEngine::instance().setConfig(cfg);
}

void InmarsatWidget::onRecordToggled(bool on) {
    auto cfg = InmarsatEngine::instance().config();
    cfg.recordVoice = on;
    InmarsatEngine::instance().setConfig(cfg);
}

void InmarsatWidget::onStart() { InmarsatEngine::instance().start(); }
void InmarsatWidget::onStop() { InmarsatEngine::instance().stop(); }

void InmarsatWidget::refreshUi() {
    const auto snap = InmarsatEngine::instance().snapshot();
    statusLabel_->setText(QString::fromStdString(snap.lastStatus));
    lockLabel_->setText(QString("State=%1  Lock=%2  Eb/N0=%3 dB  Tuned=%4 MHz  Msgs=%5  VoiceFrames=%6%7")
                            .arg(QString::fromStdString(InmarsatEngine::instance().stateName()))
                            .arg(snap.locked ? "YES" : "no")
                            .arg(snap.ebnoDb, 0, 'f', 1)
                            .arg(snap.tunedHz / 1e6, 0, 'f', 3)
                            .arg(snap.messages)
                            .arg(snap.voiceFrames)
                            .arg(snap.followingVoice ? "  [VOICE FOLLOW]" : ""));
    voiceFollowCheck_->blockSignals(true);
    voiceFollowCheck_->setChecked(snap.config.voiceFollow);
    voiceFollowCheck_->blockSignals(false);
    recordCheck_->blockSignals(true);
    recordCheck_->setChecked(snap.config.recordVoice);
    recordCheck_->blockSignals(false);

    const auto msgs = InmarsatMessageStore::instance().recent(40);
    QString text;
    for (auto it = msgs.rbegin(); it != msgs.rend(); ++it) {
        text += QString("[%1] %2 %3\n")
                    .arg(InmarsatMessage::kindName(it->kind))
                    .arg(QString::fromStdString(it->label))
                    .arg(QString::fromStdString(it->text).left(160));
    }
    if (msgView_->toPlainText() != text) msgView_->setPlainText(text);
}
