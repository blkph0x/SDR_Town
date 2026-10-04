#include "DtmfWindow.h"
#include "DtmfReport.h"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHideEvent>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QStyle>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <atomic>
#include <chrono>

struct DtmfWindow::Work {
    std::atomic<bool> cancel{false}, done{false};
    DtmfSnapshot result;
    std::string error;
};

DtmfWindow::DtmfWindow(Source source, QWidget* parent) : QDialog(parent), live_(std::move(source)) {
    setObjectName("dtmfWindow"); setWindowTitle("DTMF Analysis (Experimental)");
    resize(820, 520); setMinimumSize(540, 480);
    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    source_ = new QComboBox(this); source_->setObjectName("dtmfSource");
    source_->addItems({"Repeater Tones: tuned / output", "Repeater Tones: input", "Audio recording"});
    source_->setToolTip("Live sources require an active NFM receiver with Repeater Tones enabled; input also requires dual-watch.");
    form->addRow("Source", source_);
    profile_ = new QComboBox(this); profile_->setObjectName("dtmfProfile");
    profile_->addItems({"Conservative (40 ms)", "Fast bursts (20 ms, experimental)"});
    profile_->setToolTip("Fast mode can mistake speech for tones more easily. Neither profile is certified for telephone switching.");
    form->addRow("Detection", profile_);
    transform_ = new QComboBox(this); transform_->setObjectName("dtmfTransform");
    transform_->addItems({"Normal spectrum", "Frequency inverted"});
    transform_->setToolTip("Polarity reversal needs no setting. Frequency inversion needs the known inversion frequency.");
    form->addRow("Spectrum", transform_);
    pivot_ = new QDoubleSpinBox(this); pivot_->setObjectName("dtmfPivot");
    pivot_->setRange(2000, 6000); pivot_->setDecimals(1); pivot_->setSuffix(" Hz");
    form->addRow("Inversion frequency", pivot_);
    scale_ = new QDoubleSpinBox(this); scale_->setObjectName("dtmfScale");
    scale_->setRange(.8, 1.2); scale_->setDecimals(4); scale_->setSingleStep(.001);
    form->addRow("Pitch multiplier", scale_);
    shift_ = new QDoubleSpinBox(this); shift_->setObjectName("dtmfShift");
    shift_->setRange(-500,500); shift_->setDecimals(1); shift_->setSuffix(" Hz");
    shift_->setToolTip("Observed normal frequency = nominal x multiplier + shift. Inverted = inversion frequency minus that value.");
    form->addRow("Frequency shift", shift_);
    QSettings settings;
    profile_->setCurrentIndex(settings.value("dtmf/fast",false).toBool() ? 1 : 0);
    transform_->setCurrentIndex(settings.value("dtmf/inverted",false).toBool() ? 1 : 0);
    pivot_->setValue(settings.value("dtmf/pivot",3300).toDouble());
    scale_->setValue(settings.value("dtmf/scale",1).toDouble());
    shift_->setValue(settings.value("dtmf/shift",0).toDouble());
    auto* recording = new QHBoxLayout;
    file_ = new QLineEdit(this); file_->setObjectName("dtmfFile");
    open_ = new QPushButton(style()->standardIcon(QStyle::SP_DialogOpenButton), "", this);
    open_->setToolTip("Open mono audio recording, up to 120 seconds"); open_->setAccessibleName("Open recording");
    recording->addWidget(file_); recording->addWidget(open_); form->addRow("Recording",recording);
    layout->addLayout(form);
    auto* commands = new QHBoxLayout;
    apply_ = new QPushButton("Apply to Repeater Tones",this); apply_->setObjectName("dtmfApply");
    analyze_ = new QPushButton(style()->standardIcon(QStyle::SP_MediaPlay),"Analyze recording",this); analyze_->setObjectName("dtmfAnalyze");
    stop_ = new QPushButton(style()->standardIcon(QStyle::SP_MediaStop),"Stop",this); stop_->setObjectName("dtmfStop");
    auto* save = new QPushButton(style()->standardIcon(QStyle::SP_DialogSaveButton),"",this);
    save->setToolTip("Save detections and diagnostics as JSON"); save->setAccessibleName("Save detections");
    commands->addWidget(apply_); commands->addWidget(analyze_); commands->addWidget(stop_); commands->addStretch(); commands->addWidget(save);
    layout->addLayout(commands);
    history_ = new QTableWidget(0,9,this); history_->setObjectName("dtmfHistory");
    history_->setMinimumHeight(120);
    history_->setHorizontalHeaderLabels({"Key","Epoch","Start sample","Confirm sample","RF MHz","Row Hz","Column Hz","Purity","Twist dB"});
    history_->setEditTriggers(QAbstractItemView::NoEditTriggers); history_->setSelectionBehavior(QAbstractItemView::SelectRows);
    history_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    layout->addWidget(history_,1);
    status_ = new QLabel("Inactive",this); status_->setObjectName("dtmfStatus"); status_->setWordWrap(true);
    details_ = new QLabel(this); details_->setWordWrap(true);
    layout->addWidget(status_); layout->addWidget(details_);
    connect(open_,&QPushButton::clicked,this,[this] {
        const auto path = QFileDialog::getOpenFileName(this,"Open DTMF recording",{},"Audio (*.wav *.flac *.mp3);;All files (*)");
        if (!path.isEmpty()) file_->setText(path);
    });
    connect(apply_,&QPushButton::clicked,this,[this] {
        auto a = live_ ? live_(false) : nullptr, b = live_ ? live_(true) : nullptr;
        if (!a || !b) {status_->setText("No main receiver available");return;}
        const auto options = selectedOptions(); a->setOptions(options); b->setOptions(options);
        QSettings settings;
        settings.setValue("dtmf/fast",options.fast); settings.setValue("dtmf/inverted",options.inverted);
        settings.setValue("dtmf/pivot",options.inversionHz); settings.setValue("dtmf/scale",options.pitchScale);
        settings.setValue("dtmf/shift",options.shiftHz);
        status_->setText("Applied; waiting for the next NFM data block");
    });
    connect(analyze_,&QPushButton::clicked,this,[this] {analyze();});
    connect(stop_,&QPushButton::clicked,this,[this] {if (work_) work_->cancel = true;});
    connect(source_,&QComboBox::currentIndexChanged,this,[this] {busy(bool(work_)); refresh();});
    connect(transform_,&QComboBox::currentIndexChanged,this,[this] {busy(bool(work_));});
    connect(save,&QPushButton::clicked,this,[this] {
        const auto path = QFileDialog::getSaveFileName(this,"Save DTMF diagnostics",{},"JSON (*.json)");
        if (path.isEmpty()) return;
        auto snapshot=fileResult_;
        if (source_->currentIndex()!=2) {
            const auto decoder=live_?live_(source_->currentIndex()==1):nullptr;
            if (!decoder) {status_->setText("No main receiver available");return;}
            snapshot=decoder->snapshot();
        }
        const auto data=QByteArray::fromStdString(dtmfReport(snapshot).dump(2));
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(data)!=data.size() || !file.commit()) status_->setText("Could not save detections");
    });
    auto* timer = new QTimer(this);
    connect(timer,&QTimer::timeout,this,[this] {refresh();}); timer->start(150); // UI-only, DEC-0168.
    busy(false);
}
DtmfWindow::~DtmfWindow() {if (work_) work_->cancel=true; if (worker_.joinable()) worker_.join();}
void DtmfWindow::closeEvent(QCloseEvent* event) {if (work_) work_->cancel=true; QDialog::closeEvent(event);}
void DtmfWindow::reject() {if (work_) work_->cancel=true; QDialog::reject();}
DtmfOptions DtmfWindow::selectedOptions() const {
    return {profile_->currentIndex()==1,transform_->currentIndex()==1,pivot_->value(),scale_->value(),shift_->value()};
}
void DtmfWindow::busy(bool working) {
    const bool file = source_->currentIndex()==2;
    source_->setEnabled(!working); profile_->setEnabled(!working); transform_->setEnabled(!working);
    pivot_->setEnabled(!working && transform_->currentIndex()==1);
    scale_->setEnabled(!working); shift_->setEnabled(!working);
    file_->setEnabled(!working && file); open_->setEnabled(file_->isEnabled());
    analyze_->setEnabled(!working && file); apply_->setEnabled(!working && !file); stop_->setEnabled(working);
}
void DtmfWindow::analyze() {
    if (work_) return;
    if (worker_.joinable()) worker_.join();
    const auto path = file_->text().toUtf8().toStdString(); const auto options = selectedOptions();
    fileResult_={};history_->setRowCount(0);details_->clear();
    work_ = std::make_shared<Work>(); status_->setText("Analyzing");
    try {worker_ = std::thread([work=work_,path,options] {
        try {work->result=decodeDtmfFile(path,4096,options,[work]{return work->cancel.load();});}
        catch(const std::exception& e) {work->error=e.what();}
        catch(...) {work->error="DTMF analysis failed";}
        work->done=true;
    });} catch(const std::exception& e) {
        work_.reset();status_->setText(QString::fromUtf8(e.what()));busy(false);return;
    }
    busy(true);
}
void DtmfWindow::display(const DtmfSnapshot& s) {
    history_->setRowCount(static_cast<int>(s.history.size()));
    int row=0;
    for (const auto& e:s.history) {
        const QStringList values{QString(QChar(e.digit)),QString::number(e.epoch),QString::number(e.firstSample),
            QString::number(e.confirmedSample),QString::number(e.targetHz/1e6,'f',6),QString::number(e.rowHz,'f',1),
            QString::number(e.columnHz,'f',1),QString::number(e.purity,'f',3),QString::number(e.twistDb,'f',2)};
        for (int col=0;col<values.size();++col) {
            auto* item=history_->item(row,col);
            if (!item) {item=new QTableWidgetItem; history_->setItem(row,col,item);}
            item->setText(values[col]);
        }
        ++row;
    }
    status_->setText(QString::fromStdString(s.status)+" | "+QString::fromStdString(s.sequence.empty()?s.lastSequence:s.sequence));
    details_->setText(QString("%1 / %2 | %3 samples at %4 Hz | %5 rejected frames | %6 lost events | %7 omitted digits | %8 ms processing | %9")
        .arg(s.options.fast?"Fast":"Conservative",s.options.inverted?"Inverted":"Normal")
        .arg(s.samples).arg(s.sampleRate,0,'f',0).arg(s.rejectedFrames).arg(s.droppedEvents).arg(s.truncatedDigits)
        .arg(s.processingUs/1000.0,0,'f',1).arg(QString::fromStdString(s.rejection)));
}
void DtmfWindow::refresh() {
    if (work_) {
        if (!work_->done) return;
        if (worker_.joinable()) worker_.join();
        fileResult_=work_->result;
        if (!work_->error.empty()) fileResult_.status=work_->error;
        work_.reset(); busy(false);
    }
    if (!isVisible()) return;
    if (source_->currentIndex()==2) {display(fileResult_);return;}
    const auto source=live_?live_(source_->currentIndex()==1):nullptr;
    if (!source) {history_->setRowCount(0);details_->clear();status_->setText("No main receiver available");return;}
    const auto snapshot=source->snapshot(); display(snapshot);
    const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if (!snapshot.updatedMs || now-snapshot.updatedMs>2000)
        status_->setText("No recent NFM samples from Repeater Tones");
}
