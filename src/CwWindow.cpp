#include "CwWindow.h"
#include <QCloseEvent>
#include <QHideEvent>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
#include <QClipboard>
#include <QApplication>
#include <QTextCursor>
#include <atomic>
#include <mutex>
#include <spdlog/spdlog.h>

struct CwWindow::Work {
    std::atomic<bool> stop{false}, done{false};
    std::mutex mutex;
    CwProgress progress;
};

CwWindow::CwWindow(LiveFactory live, QWidget* parent) : QDialog(parent), live_(std::move(live)) {
    setWindowTitle("CW / Morse Decoder (Experimental)");
    setObjectName("cwWindow"); resize(720, 430); setMinimumSize(440, 320);
    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    source_ = new QComboBox(this); source_->setObjectName("cwSource");
    source_->addItem("Receiver tap", "live"); source_->addItem("Audio recording", "file");
    form->addRow("Source", source_);
    auto* row = new QHBoxLayout;
    file_ = new QLineEdit(this); file_->setObjectName("cwFile");
    open_ = new QPushButton(style()->standardIcon(QStyle::SP_DialogOpenButton), "", this);
    open_->setToolTip("Open audio recording"); open_->setAccessibleName("Open audio recording");
    row->addWidget(file_); row->addWidget(open_); form->addRow("Recording", row);
    pitch_ = new QDoubleSpinBox(this); pitch_->setObjectName("cwPitch");
    pitch_->setRange(0, 1200); pitch_->setDecimals(0); pitch_->setSingleStep(10); pitch_->setSpecialValueText("Auto"); pitch_->setSuffix(" Hz");
    speed_ = new QDoubleSpinBox(this); speed_->setObjectName("cwSpeed");
    speed_->setRange(0, 55); speed_->setDecimals(0); speed_->setSpecialValueText("Auto"); speed_->setSuffix(" WPM");
    pitch_->setToolTip("Auto, or a tone between 200 and 1200 Hz. FM needs keyed audio tones, not an unmodulated carrier.");
    speed_->setToolTip("Auto, or 5 to 55 words per minute. Estimates are not proof of valid Morse.");
    pitch_->setValue(QSettings().value("cw/pitch", 0).toDouble());
    speed_->setValue(QSettings().value("cw/speed", 0).toDouble());
    form->addRow("Tone", pitch_); form->addRow("Speed", speed_); layout->addLayout(form);
    auto* commands = new QHBoxLayout;
    start_ = new QPushButton(style()->standardIcon(QStyle::SP_MediaPlay), "Start", this); start_->setObjectName("cwStart");
    stop_ = new QPushButton(style()->standardIcon(QStyle::SP_MediaStop), "Stop", this); stop_->setObjectName("cwStop");
    clear_ = new QPushButton(style()->standardIcon(QStyle::SP_DialogResetButton), "", this);
    clear_->setToolTip("Clear transcript"); clear_->setAccessibleName("Clear transcript");
    auto* copy = new QPushButton(style()->standardIcon(QStyle::SP_FileDialogContentsView), "", this);
    copy->setToolTip("Copy transcript"); copy->setAccessibleName("Copy transcript");
    auto* save = new QPushButton(style()->standardIcon(QStyle::SP_DialogSaveButton), "", this);
    save->setToolTip("Save transcript"); save->setAccessibleName("Save transcript");
    commands->addWidget(start_); commands->addWidget(stop_); commands->addStretch();
    commands->addWidget(clear_); commands->addWidget(copy); commands->addWidget(save); layout->addLayout(commands);
    origin_ = new QLabel("No source", this); origin_->setWordWrap(true); layout->addWidget(origin_);
    text_ = new QPlainTextEdit(this); text_->setObjectName("cwText"); text_->setReadOnly(true);
    text_->setMaximumBlockCount(1000); layout->addWidget(text_, 1);
    status_ = new QLabel("Idle", this); status_->setObjectName("cwStatus"); status_->setWordWrap(true); layout->addWidget(status_);
    details_ = new QLabel(this); details_->setWordWrap(true); layout->addWidget(details_);
    connect(open_, &QPushButton::clicked, this, [this] {
        const auto file = QFileDialog::getOpenFileName(this, "Open Morse audio", {}, "Audio (*.wav *.flac *.mp3);;All files (*)");
        if (!file.isEmpty()) file_->setText(file);
    });
    connect(source_, &QComboBox::currentIndexChanged, this, [this] { setBusy(false); });
    connect(start_, &QPushButton::clicked, this, [this] { start(); });
    connect(stop_, &QPushButton::clicked, this, [this] { if (work_) {work_->stop = true; status_->setText("Stopping");} });
    connect(clear_, &QPushButton::clicked, text_, &QPlainTextEdit::clear);
    connect(copy, &QPushButton::clicked, this, [this] {QApplication::clipboard()->setText(text_->toPlainText());});
    connect(save, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getSaveFileName(this, "Save Morse transcript", {}, "Text (*.txt)");
        if (path.isEmpty()) return;
        QSaveFile file(path);
        const auto bytes = text_->toPlainText().toUtf8();
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
            status_->setText("Could not save transcript");
    });
    auto* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this] {refresh();});
    timer->start(100); // DEC-0167: UI snapshot policy, not a decoder clock.
    setBusy(false);
}
CwWindow::~CwWindow() { if (work_) work_->stop = true; if (worker_.joinable()) worker_.join(); }
void CwWindow::reject() {
    // DEC-0185: Escape closes a session; hiding/minimizing only changes its view.
    if (work_ && !work_->done) {closePending_ = true; work_->stop = true; status_->setText("Stopping"); return;}
    QDialog::reject();
}
void CwWindow::closeEvent(QCloseEvent* event) {
    if (work_ && !work_->done) {closePending_ = true; work_->stop = true; status_->setText("Stopping"); event->ignore(); return;}
    QDialog::closeEvent(event);
}
void CwWindow::setBusy(bool busy) {
    if (auto* picker = findChild<QComboBox*>("receiverSource")) picker->setEnabled(!busy && source_->currentData() == "live");
    source_->setEnabled(!busy); pitch_->setEnabled(!busy); speed_->setEnabled(!busy);
    file_->setEnabled(!busy && source_->currentData() == "file"); open_->setEnabled(file_->isEnabled());
    start_->setEnabled(!busy); stop_->setEnabled(busy); clear_->setEnabled(!busy);
}
void CwWindow::start() {
    if (work_ && !work_->done) return;
    if (worker_.joinable()) worker_.join();
    try {
        const CwOptions options{float(pitch_->value()), float(speed_->value())};
        CwDecoder validateOptions(options);
        CwRun run;
        if (source_->currentData() == "live") {
            if (!live_) throw std::runtime_error("Main receiver is unavailable");
            run = live_();
        } else {
            const auto path = file_->text();
            run = [path](auto options, const auto& cancel, const auto& publish) {decodeCwFile(path, options, cancel, publish);};
        }
        if (!run) throw std::runtime_error("No CW audio source");
        QSettings().setValue("cw/pitch", pitch_->value()); QSettings().setValue("cw/speed", speed_->value());
        work_ = std::make_shared<Work>();
        text_->clear(); details_->clear(); status_->setText("Starting");
        worker_ = std::thread([work = work_, run = std::move(run), options] {
            try {
                run(options, [work] {return work->stop.load();}, [work](const CwProgress& p) {
                    std::lock_guard lock(work->mutex); work->progress = p;
                });
            } catch (const std::exception& e) {
                std::lock_guard lock(work->mutex); work->progress.status = QString::fromUtf8(e.what());
                spdlog::warn("CW receive stopped: {}", e.what());
            } catch (...) {
                std::lock_guard lock(work->mutex); work->progress.status = "CW worker failed";
            }
            work->done = true;
        });
        setBusy(true);
    } catch (const std::exception& e) { if (!worker_.joinable()) work_.reset(); status_->setText(QString::fromUtf8(e.what())); setBusy(false); }
}
void CwWindow::refresh() {
    if (!work_) return;
    CwProgress p;
    {std::lock_guard lock(work_->mutex); p = work_->progress;}
    if (!p.status.isEmpty()) status_->setText(p.status);
    origin_->setText(p.source);
    const auto text = QString::fromStdString(p.decoder.text);
    if (text_->toPlainText() != text) {text_->setPlainText(text); text_->moveCursor(QTextCursor::End);}
    details_->setText(QString("Estimated %1 Hz / %2 WPM | %3 samples | %4 gaps | %5 resets | %6 rejected")
        .arg(p.decoder.pitchHz, 0, 'f', 0).arg(p.decoder.speedWpm, 0, 'f', 1)
        .arg(p.decoder.samples).arg(p.iqGaps).arg(p.decoder.resets).arg(p.decoder.rejected));
    if (work_->done) {
        if (worker_.joinable()) worker_.join();
        work_.reset(); setBusy(false);
        if (closePending_) {closePending_ = false; close();}
    }
}
