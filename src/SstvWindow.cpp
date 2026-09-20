#include "SstvWindow.h"
#include "SstvModes.h"
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QSlider>
#include <QSplitter>
#include <QStyle>
#include <QThread>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <memory>
#include <array>
#include <mutex>
#include <QTimer>

SstvWindow::SstvWindow(Decode decode,QWidget* parent):QDialog(parent),decode_(std::move(decode)) {
    setWindowTitle("SSTV Recorded Images");
    resize(820,600);
    setMinimumSize(560,420);
    auto* layout=new QVBoxLayout(this);
    auto* form=new QFormLayout;
    source_=new QComboBox(this); source_->setObjectName("sstvSource");
    source_->addItem("Recording","file"); form->addRow("Source",source_);
    auto row=[&](QLineEdit*& edit,QPushButton*& button,const QString& label,QStyle::StandardPixmap icon) {
        auto* container=new QWidget(this);
        auto* horizontal=new QHBoxLayout(container);
        horizontal->setContentsMargins(0,0,0,0);
        edit=new QLineEdit(container);
        button=new QPushButton(style()->standardIcon(icon),QString(),container);
        button->setToolTip(label); button->setAccessibleName(label);
        button->setFixedWidth(32);
        horizontal->addWidget(edit); horizontal->addWidget(button);
        form->addRow(label,container);
    };
    row(input_,open_,"Recording",QStyle::SP_DialogOpenButton);
    row(output_,destination_,"New output folder",QStyle::SP_DirIcon);
    input_->setObjectName("sstvInput"); output_->setObjectName("sstvOutput");
    mode_=new QComboBox(this);
    mode_->addItem("Automatic (VIS header)","auto");
    for (const auto& spec : kSstvModes)
        mode_->addItem(QString::fromUtf8(spec.label), QString::fromUtf8(spec.id));
    form->addRow("Mode",mode_);
    hint_=new QLabel("Tune 1200–2300 Hz. Automatic reads 7-bit VIS, then QSSTV 16-bit VIS (MP/MR/ML), then 1200 Hz line-sync period. Forced mode uses line sync (AVT has none — VIS or start of image). FAX480 has no VIS. Narrow 2172 Hz modes are not supported.",this);
    hint_->setWordWrap(true);
    form->addRow(hint_);
    layout->addLayout(form);
    auto* controls=new QHBoxLayout;
    decodeButton_=new QPushButton(style()->standardIcon(QStyle::SP_MediaPlay),"Decode",this);
    cancelButton_=new QPushButton(style()->standardIcon(QStyle::SP_MediaStop),"Cancel",this);
    finishButton_=new QPushButton(style()->standardIcon(QStyle::SP_DialogSaveButton),"Finish and save",this);
    finishButton_->setObjectName("sstvFinish");
    folder_=new QPushButton(style()->standardIcon(QStyle::SP_DirOpenIcon),"Open output",this);
    decodeButton_->setObjectName("sstvDecode"); cancelButton_->setObjectName("sstvCancel");
    controls->addWidget(decodeButton_); controls->addWidget(finishButton_); controls->addWidget(cancelButton_); controls->addStretch(); controls->addWidget(folder_);
    layout->addLayout(controls);
    auto* split=new QSplitter(this);
    images_=new QListWidget(split); images_->setObjectName("sstvImages");
    images_->setWordWrap(true);
    preview_=new QLabel(split); preview_->setObjectName("sstvPreview");
    preview_->setAlignment(Qt::AlignCenter); preview_->setMinimumSize(200,160);
    preview_->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Ignored);
    split->setStretchFactor(0,0); split->setStretchFactor(1,1); split->setSizes({220,580});
    layout->addWidget(split,1);
    auto* adj=new QHBoxLayout;
    adj->addWidget(new QLabel("Brightness",this));
    brightness_=new QSlider(Qt::Horizontal,this); brightness_->setRange(0,200); brightness_->setValue(100);
    adj->addWidget(brightness_);
    adj->addWidget(new QLabel("Contrast",this));
    contrast_=new QSlider(Qt::Horizontal,this); contrast_->setRange(25,250); contrast_->setValue(100);
    adj->addWidget(contrast_);
    layout->addLayout(adj);
    connect(brightness_,&QSlider::valueChanged,this,[this]{updatePreview();});
    connect(contrast_,&QSlider::valueChanged,this,[this]{updatePreview();});
    status_=new QLabel("Idle",this); status_->setObjectName("sstvStatus"); status_->setWordWrap(true); layout->addWidget(status_);
    connect(open_,&QPushButton::clicked,this,[this] {
        const auto path=QFileDialog::getOpenFileName(this,"Open SSTV recording",input_->text(),"Audio (*.wav *.flac)");
        if(!path.isEmpty()) input_->setText(path);
    });
    connect(destination_,&QPushButton::clicked,this,[this] {
        const auto path=QFileDialog::getSaveFileName(this,"New SSTV output directory",output_->text(),QString(),nullptr,QFileDialog::DontConfirmOverwrite);
        if(!path.isEmpty()) output_->setText(path);
    });
    connect(decodeButton_,&QPushButton::clicked,this,[this] {startDecode(input_->text(),output_->text(),mode_->currentData().toString());});
    connect(cancelButton_,&QPushButton::clicked,this,&SstvWindow::cancel);
    connect(finishButton_,&QPushButton::clicked,this,&SstvWindow::finishLive);
    connect(source_,&QComboBox::currentIndexChanged,this,[this]{setBusy(busy());});
    connect(folder_,&QPushButton::clicked,this,[this] {QDesktopServices::openUrl(QUrl::fromLocalFile(resultDirectory_));});
    connect(images_,&QListWidget::currentRowChanged,this,[this](int index) {
        original_=QImage();
        if(index>=0) original_.load(images_->item(index)->data(Qt::UserRole).toString());
        updatePreview();
    });
    connect(split,&QSplitter::splitterMoved,this,[this] {updatePreview();});
    setBusy(false);
}

void SstvWindow::setLiveSource(LiveOpen open) {
    if(busy()) return;
    liveOpen_=std::move(open);
    if(liveOpen_ && source_->findData("live")<0) source_->addItem("Live NFM - main receiver","live");
    setWindowTitle(liveOpen_?"SSTV Images":"SSTV Recorded Images");
}
bool SstvWindow::startLive(const QString& output,const QString& mode) {
    if(busy() || !liveOpen_) return false;
    source_->setCurrentIndex(source_->findData("live"));
    return startDecode(QString(),output,mode);
}
void SstvWindow::finishLive() {
    if(worker_ && finish_) {finish_->store(true);finishButton_->setEnabled(false);status_->setText("Finishing and saving...");}
}

SstvWindow::~SstvWindow() {
    // Parent teardown also joins; queued GUI callbacks use this as their context.
    if(worker_) {worker_->requestInterruption(); worker_->wait(); delete worker_;}
}

bool SstvWindow::startDecode(const QString& input,const QString& output,const QString& mode) {
    if(busy()) return false;
    const bool live=source_->currentData()=="live";
    if((!live && input.trimmed().isEmpty()) || output.trimmed().isEmpty() || mode_->findData(mode)<0) {
        status_->setText(live?"Choose a new output folder and a supported mode.":"Choose a recording, a new output folder and a supported mode."); return false;
    }
    Decode decode=decode_;
    finish_.reset();
    if(live) {
        try {finish_=std::make_shared<std::atomic<bool>>(false); decode=liveOpen_(finish_);}
        catch(const std::exception& error) {status_->setText(QString::fromUtf8(error.what())); finish_.reset();return false;}
    }
    if(!live) input_->setText(input);
    output_->setText(output); mode_->setCurrentIndex(mode_->findData(mode));
    images_->clear(); original_=QImage(); resultDirectory_.clear(); updatePreview();
    status_->setText(live?"Listening for SSTV...":"Decoding..."); setBusy(true);
    struct Result {
        nlohmann::json report; QString error;
        std::mutex mutex;
        QImage preview; QString mode; int rows=0; bool pending=false;
    };
    auto result=std::make_shared<Result>();
    worker_=QThread::create([decode,input,output,mode,result] {
        try {result->report=decode(input,output,mode,[] {return QThread::currentThread()->isInterruptionRequested();},
            [result](const QImage& image,const QString& mode,int rows) {
                std::lock_guard lock(result->mutex);
                result->preview=image; result->mode=mode; result->rows=rows; result->pending=true;
            });}
        catch(const std::exception& e) {result->error=QString::fromUtf8(e.what());}
        catch(...) {result->error="Unexpected SSTV decoder failure";}
    });
    worker_->setParent(this);
    auto* timer=new QTimer(this);
    connect(timer,&QTimer::timeout,this,[this,result] {
        if(!worker_ || worker_->isInterruptionRequested()) return;
        std::lock_guard lock(result->mutex);
        if(!result->pending) return;
        result->pending=false; original_=result->preview; scanline_=result->rows>0?result->rows-1:-1;
        status_->setText(QString("Decoding %1: %2/%3 rows").arg(result->mode).arg(result->rows).arg(original_.height()));
        updatePreview();
    });
    timer->start(50); // DEC-0094 latest-only GUI observation, not a decoder clock.
    connect(worker_,&QThread::finished,this,[this,result,timer] {
        timer->stop(); timer->deleteLater();
        auto* finished=worker_; worker_=nullptr; finished->wait(); finished->deleteLater();
        bool success=result->error.isEmpty();
        if(success) {
            try {
                resultDirectory_=QString::fromStdString(result->report.at("outputDirectory").get<std::string>());
                for(const auto& image:result->report.at("images")) {
                    const auto path=QDir(resultDirectory_).filePath(QString::fromStdString(image.at("file").get<std::string>()));
                    const auto imageMode=QString::fromStdString(image.at("mode").get<std::string>());
                    const auto description=QString("%1 | %2\n%3/%4 rows")
                        .arg(mode_->itemText(mode_->findData(imageMode)),image.at("complete").get<bool>()?"Complete":"Partial")
                        .arg(image.at("rows").get<int>()).arg(image.at("height").get<int>());
                    auto* item=new QListWidgetItem(description,images_); item->setData(Qt::UserRole,path); item->setToolTip(path);
                }
                status_->setText(images_->count()?QString("%1 image(s) saved").arg(images_->count()):"No images detected");
                if(images_->count()) images_->setCurrentRow(0);
            } catch(const std::exception& e) {success=false; result->error=QString::fromUtf8(e.what()); images_->clear(); resultDirectory_.clear();}
        }
        if(!success) {status_->setText(result->error); original_=QImage(); updatePreview();}
        setBusy(false);
        if(closePending_) {closePending_=false; close();}
        emit decodeFinished(success);
    },Qt::QueuedConnection);
    worker_->start(); return true;
}

void SstvWindow::setBusy(bool value) {
    for(auto* widget:std::array<QWidget*,6>{input_,output_,mode_,open_,destination_,decodeButton_}) widget->setEnabled(!value);
    const bool live=source_->currentData()=="live";
    source_->setEnabled(!value); input_->setEnabled(!value && !live); open_->setEnabled(!value && !live);
    decodeButton_->setText(live?"Receive":"Decode");
    finishButton_->setVisible(live); finishButton_->setEnabled(value && live);
    cancelButton_->setEnabled(value); folder_->setEnabled(!value && !resultDirectory_.isEmpty());
}
void SstvWindow::cancel() {if(worker_) {worker_->requestInterruption(); cancelButton_->setEnabled(false); finishButton_->setEnabled(false); status_->setText("Cancelling...");}}
void SstvWindow::closeEvent(QCloseEvent* event) {
    if(busy()) {closePending_=true; cancel(); event->ignore();} else QDialog::closeEvent(event);
}
void SstvWindow::reject() {
    if(busy()) {closePending_=true; cancel();} else QDialog::reject();
}
void SstvWindow::resizeEvent(QResizeEvent* event) {QDialog::resizeEvent(event); updatePreview();}
QImage SstvWindow::adjustedPreview() const {
    if(original_.isNull()) return {};
    QImage img=original_.convertToFormat(QImage::Format_RGB888);
    const double b=(brightness_?brightness_->value():100)/100.0-1.0;
    const double c=(contrast_?contrast_->value():100)/100.0;
    for(int y=0;y<img.height();++y) {
        auto* line=img.scanLine(y);
        for(int x=0;x<img.width();++x) {
            for(int k=0;k<3;++k) {
                double v=line[x*3+k]/255.0;
                v=(v-0.5)*c+0.5+b;
                line[x*3+k]=uchar(std::clamp(v,0.0,1.0)*255.0);
            }
        }
    }
    if(scanline_>=0 && scanline_<img.height()) {
        QPainter p(&img);
        p.setPen(QPen(QColor(255,48,48),2));
        p.drawLine(0,scanline_,img.width(),scanline_);
    }
    return img;
}
void SstvWindow::updatePreview() {
    if(original_.isNull()) preview_->clear();
    else preview_->setPixmap(QPixmap::fromImage(adjustedPreview()).scaled(preview_->size(),Qt::KeepAspectRatio,Qt::SmoothTransformation));
}

QString SstvWindow::statusMessage() const {
    return status_ ? status_->text() : QString();
}

QString SstvWindow::selectedMode() const {
    return mode_ ? mode_->currentData().toString() : QStringLiteral("auto");
}
bool SstvWindow::liveSelected() const {
    return source_ && source_->currentData().toString() == QStringLiteral("live");
}

QStringList SstvWindow::imagePaths() const {
    QStringList out;
    if (!images_) return out;
    for (int i = 0; i < images_->count(); ++i) {
        if (auto* item = images_->item(i)) out.append(item->data(Qt::UserRole).toString());
    }
    return out;
}

QStringList SstvWindow::imageLabels() const {
    QStringList out;
    if (!images_) return out;
    for (int i = 0; i < images_->count(); ++i) {
        if (auto* item = images_->item(i)) out.append(item->text());
    }
    return out;
}
