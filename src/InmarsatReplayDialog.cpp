#include "InmarsatReplayDialog.h"
#include "InmarsatMapWidget.h"
#include <QTabWidget>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSlider>
#include <QScrollBar>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace {
QString clockText(double seconds) {
    const auto ms = static_cast<qint64>(seconds * 1000);
    return QString("%1:%2:%3.%4").arg(ms / 3600000, 2, 10, QChar('0'))
        .arg((ms / 60000) % 60, 2, 10, QChar('0')).arg((ms / 1000) % 60, 2, 10, QChar('0'))
        .arg(ms % 1000, 3, 10, QChar('0'));
}
}
InmarsatReplayDialog::InmarsatReplayDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("Inmarsat IQ replay");
    resize(940, 850);
    auto* root = new QVBoxLayout(this);
    auto* fileRow = new QHBoxLayout;
    file_ = new QLineEdit;
    file_->setObjectName("iqPath");
    file_->setPlaceholderText("IQ recording");
    fileRow->addWidget(file_, 1);
    auto button = [this](QStyle::StandardPixmap icon, const char* label, const char* name) {
        auto* b = new QToolButton;
        b->setIcon(style()->standardIcon(icon));
        b->setToolTip(label); b->setAccessibleName(label); b->setObjectName(name);
        b->setFixedSize(32, 32);
        return b;
    };
    open_ = button(QStyle::SP_DialogOpenButton, "Open IQ recording", "iqOpen");
    fileRow->addWidget(open_);
    root->addLayout(fileRow);
    auto* form = new QFormLayout;
    format_ = new QComboBox;
    format_->setObjectName("iqFormat");
    format_->addItem("SigMF / IQ WAV", "auto");
    format_->addItem("IQ WAV (I = left, Q = right)", "wav_iq");
    for (const auto& f : InmarsatIqFile::supportedFormats()) format_->addItem(QString("Raw %1").arg(f), f);
    form->addRow("Input format", format_);
    auto frequency = [](const char* name, double max, const char* suffix) {
        auto* s = new QDoubleSpinBox;
        s->setObjectName(name); s->setDecimals(6); s->setRange(0, max); s->setSuffix(suffix);
        s->setKeyboardTracking(false);
        return s;
    };
    rate_ = frequency("iqRate", 40e6, " Hz");
    rate_->setDecimals(0); rate_->setValue(2048000);
    form->addRow("Raw sample rate", rate_);
    center_ = frequency("iqCenter", 100000, " MHz");
    center_->setSpecialValueText("Required for WAV / raw");
    form->addRow("WAV / raw center", center_);
    channel_ = frequency("iqChannel", 100000, " MHz");
    channel_->setSpecialValueText("Capture center");
    form->addRow("Channel", channel_);
    mode_ = new QComboBox;
    mode_->setObjectName("iqMode");
    mode_->addItem("Aero OQPSK 10500", static_cast<int>(InmarsatDemodMode::AeroOqpsk10500));
    mode_->addItem("Aero C-channel 8400", static_cast<int>(InmarsatDemodMode::AeroVoice8400));
    mode_->addItem("Aero MSK 1200", static_cast<int>(InmarsatDemodMode::AeroMsk1200));
    mode_->addItem("Aero MSK 600", static_cast<int>(InmarsatDemodMode::AeroMsk600));
    mode_->addItem("Aero R/T burst MSK 1200", static_cast<int>(InmarsatDemodMode::AeroBurstMsk1200));
    mode_->addItem("Aero R/T burst OQPSK 10500", static_cast<int>(InmarsatDemodMode::AeroBurstOqpsk10500));
    mode_->addItem("EGC BPSK 1200", static_cast<int>(InmarsatDemodMode::EgcBpsk1200));
    form->addRow("Decoder", mode_);
    realTime_ = new QCheckBox("Real-time pacing");
    realTime_->setChecked(true); realTime_->setObjectName("iqRealTime");
    form->addRow({}, realTime_);
    playAudio_=new QCheckBox("Speaker audio"); playAudio_->setObjectName("iqSpeaker");
    playAudio_->setChecked(true);form->addRow({},playAudio_);
    connect(playAudio_,&QCheckBox::toggled,this,[this](bool on){if(on)realTime_->setChecked(true);});
    connect(realTime_,&QCheckBox::toggled,this,[this](bool on){if(!on)playAudio_->setChecked(false);});
    wav_=new QLineEdit;wav_->setObjectName("iqWav");wav_->setPlaceholderText("Optional new WAV output path");
    form->addRow("Decoded WAV",wav_);
    share_ = new QCheckBox("Share diagnostic counters (no IQ or audio)");
    share_->setObjectName("iqShareDiagnostics");
    share_->setEnabled(false);
    form->addRow({}, share_);
    connect(share_, &QCheckBox::clicked, this, [this](bool on) {
        if (on && (!enableSharing_ || !enableSharing_())) share_->setChecked(false);
    });
    root->addLayout(form);
    auto* transport = new QHBoxLayout;
    play_ = button(QStyle::SP_MediaPlay, "Play / resume", "iqPlay");
    pause_ = button(QStyle::SP_MediaPause, "Pause", "iqPause");
    stop_ = button(QStyle::SP_MediaStop, "Stop", "iqStop");
    transport->addWidget(play_); transport->addWidget(pause_); transport->addWidget(stop_);
    position_ = new QSlider(Qt::Horizontal);
    position_->setObjectName("iqPosition"); position_->setRange(0, 1000000);
    position_->setToolTip("Seek recording; decoder state resets at the selected sample");
    transport->addWidget(position_, 1);
    root->addLayout(transport);
    time_ = new QLabel("00:00:00.000 / 00:00:00.000"); root->addWidget(time_);
    status_ = new QLabel("Idle"); status_->setTextFormat(Qt::PlainText); status_->setWordWrap(true); root->addWidget(status_);
    auto* capability = new QLabel("Classic Aero experimental | ADS-C | C-channel voice | EGC probe only");
    capability->setWordWrap(true); root->addWidget(capability);
    details_ = new QPlainTextEdit;
    details_->setObjectName("iqDiagnostics"); details_->setReadOnly(true);
    details_->setMaximumBlockCount(150);
    auto* tabs=new QTabWidget;map_=new InmarsatMapWidget;
    tabs->addTab(map_,"Aircraft map");tabs->addTab(details_,"Diagnostics");root->addWidget(tabs,1);
    auto* logRow = new QHBoxLayout;
    log_ = new QLabel; log_->setTextFormat(Qt::PlainText); log_->setWordWrap(true);
    log_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    logRow->addWidget(log_, 1);
    auto* folder = button(QStyle::SP_DirOpenIcon, "Open diagnostic folder", "iqLogFolder");
    logRow->addWidget(folder); root->addLayout(logRow);
    connect(folder, &QToolButton::clicked, this, [this] {
        const auto path = replay_.snapshot().logPath;
        if (!path.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
    });
    connect(open_, &QToolButton::clicked, this, [this] {
        const auto path = QFileDialog::getOpenFileName(this, "Open Inmarsat IQ", {},
            "IQ recordings (*.sigmf-meta *.sigmf-data *.wav *.iq *.raw *.bin);;All files (*)");
        if (!path.isEmpty()) file_->setText(path);
    });
    connect(play_, &QToolButton::clicked, this, &InmarsatReplayDialog::play);
    connect(pause_, &QToolButton::clicked, this, [this] { replay_.pause(true); });
    connect(stop_, &QToolButton::clicked, this, [this] { replay_.stop(); refresh(); });
    connect(position_, &QSlider::sliderReleased, this, [this] {
        const auto s = replay_.snapshot();
        if (s.info.sampleRateHz > 0) replay_.seek((s.info.sampleCount / s.info.sampleRateHz) * position_->value() / 1000000.0);
    });
    auto* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &InmarsatReplayDialog::refresh);
    timer->start(200);
    refresh();
}
InmarsatReplayDialog::~InmarsatReplayDialog() { replay_.stop(); }
void InmarsatReplayDialog::setSharingControl(bool enabled, std::function<bool()> enable) {
    enableSharing_ = std::move(enable);
    share_->setChecked(enabled);
    share_->setEnabled(static_cast<bool>(enableSharing_));
}
void InmarsatReplayDialog::startReplay(const InmarsatReplayOptions& options) {
    file_->setText(options.path);
    format_->setCurrentIndex(std::max(0, format_->findData(options.input.format)));
    rate_->setValue(options.input.sampleRateHz);
    center_->setValue(options.input.centerHz / 1e6);
    channel_->setValue(options.channelHz / 1e6);
    mode_->setCurrentIndex(mode_->findData(static_cast<int>(options.mode)));
    realTime_->setChecked(options.realTime);
    playAudio_->setChecked(options.playAudio);wav_->setText(options.wavPath);
    share_->setChecked(options.shareDiagnostics);
    replay_.start(options);
    refresh();
}
void InmarsatReplayDialog::play() {
    const auto snapshot = replay_.snapshot();
    if (snapshot.running) { replay_.pause(false); return; }
    InmarsatReplayOptions options;
    options.path = file_->text(); options.input.format = format_->currentData().toString();
    options.input.sampleRateHz = rate_->value(); options.input.centerHz = center_->value() * 1e6;
    options.channelHz = channel_->value() * 1e6;
    options.mode = static_cast<InmarsatDemodMode>(mode_->currentData().toInt());
    options.realTime = realTime_->isChecked();
    options.playAudio=playAudio_->isChecked(); options.wavPath=wav_->text();
    options.shareDiagnostics = share_->isChecked();
    replay_.start(options);
    refresh();
}
void InmarsatReplayDialog::refresh() {
    const auto s = replay_.snapshot();
    const bool active = s.running;
    for (auto* w : {static_cast<QWidget*>(file_), static_cast<QWidget*>(open_), static_cast<QWidget*>(format_),
         static_cast<QWidget*>(rate_), static_cast<QWidget*>(center_), static_cast<QWidget*>(channel_),
         static_cast<QWidget*>(mode_), static_cast<QWidget*>(realTime_),static_cast<QWidget*>(playAudio_),static_cast<QWidget*>(wav_)}) w->setEnabled(!active);
    play_->setEnabled(!active || s.state == "paused");
    pause_->setEnabled(active && s.state == "playing"); stop_->setEnabled(active);
    share_->setEnabled(!active && static_cast<bool>(enableSharing_));
    position_->setEnabled(active && s.info.sampleCount > 0);
    if (!position_->isSliderDown() && s.info.sampleCount)
        position_->setValue(static_cast<int>(s.position * 1000000.0 / s.info.sampleCount));
    time_->setText(clockText(s.info.sampleRateHz ? s.position / s.info.sampleRateHz : 0) + " / " +
        clockText(s.info.sampleRateHz ? s.info.sampleCount / s.info.sampleRateHz : 0));
    status_->setText(s.state + (s.error.isEmpty() ? "" : ": " + s.error));
    log_->setText(s.logError.isEmpty() ? s.logPath : s.logPath + "\n" + s.logError);
    auto report=s.pipeline;
    if(!active || s.state=="paused") report["voiceActive"]=false;
    map_->setReport(report,true);
    const auto text = QString::fromStdString(s.toJson().dump(2));
    if (details_->toPlainText() != text) {
        const int scroll = details_->verticalScrollBar()->value();
        details_->setPlainText(text);
        details_->verticalScrollBar()->setValue(scroll);
    }
}
