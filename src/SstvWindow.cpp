#include "SstvWindow.h"
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
#include <QPushButton>
#include <QSplitter>
#include <QStyle>
#include <QThread>
#include <QUrl>
#include <QVBoxLayout>
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
    mode_->addItem("Automatic","auto"); mode_->addItem("Robot 36","robot36"); mode_->addItem("Martin M1","martin1");
    form->addRow("Mode",mode_); layout->addLayout(form);
    auto* controls=new QHBoxLayout;
    decodeButton_=new QPushButton(style()->standardIcon(QStyle::SP_MediaPlay),"Decode",this);
    cancelButton_=new QPushButton(style()->standardIcon(QStyle::SP_MediaStop),"Cancel",this);
    folder_=new QPushButton(style()->standardIcon(QStyle::SP_DirOpenIcon),"Open output",this);
    decodeButton_->setObjectName("sstvDecode"); cancelButton_->setObjectName("sstvCancel");
    controls->addWidget(decodeButton_); controls->addWidget(cancelButton_); controls->addStretch(); controls->addWidget(folder_);
    layout->addLayout(controls);
    auto* split=new QSplitter(this);
    images_=new QListWidget(split); images_->setObjectName("sstvImages");
    images_->setWordWrap(true);
    preview_=new QLabel(split); preview_->setObjectName("sstvPreview");
    preview_->setAlignment(Qt::AlignCenter); preview_->setMinimumSize(200,160);
    preview_->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Ignored);
    split->setStretchFactor(0,0); split->setStretchFactor(1,1); split->setSizes({220,580});
    layout->addWidget(split,1);
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
    connect(folder_,&QPushButton::clicked,this,[this] {QDesktopServices::openUrl(QUrl::fromLocalFile(resultDirectory_));});
    connect(images_,&QListWidget::currentRowChanged,this,[this](int index) {
        original_=QImage();
        if(index>=0) original_.load(images_->item(index)->data(Qt::UserRole).toString());
        updatePreview();
    });
    connect(split,&QSplitter::splitterMoved,this,[this] {updatePreview();});
    setBusy(false);
}

SstvWindow::~SstvWindow() {
    // Parent teardown also joins; queued GUI callbacks use this as their context.
    if(worker_) {worker_->requestInterruption(); worker_->wait(); delete worker_;}
}

bool SstvWindow::startDecode(const QString& input,const QString& output,const QString& mode) {
    if(busy()) return false;
    if(input.trimmed().isEmpty() || output.trimmed().isEmpty() || mode_->findData(mode)<0) {
        status_->setText("Choose a recording, a new output folder and a supported mode."); return false;
    }
    input_->setText(input); output_->setText(output); mode_->setCurrentIndex(mode_->findData(mode));
    images_->clear(); original_=QImage(); resultDirectory_.clear(); updatePreview();
    status_->setText("Decoding..."); setBusy(true);
    struct Result {
        nlohmann::json report; QString error;
        std::mutex mutex;
        QImage preview; QString mode; int rows=0; bool pending=false;
    };
    auto result=std::make_shared<Result>();
    const auto decode=decode_;
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
        result->pending=false; original_=result->preview;
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
    cancelButton_->setEnabled(value); folder_->setEnabled(!value && !resultDirectory_.isEmpty());
}
void SstvWindow::cancel() {if(worker_) {worker_->requestInterruption(); cancelButton_->setEnabled(false); status_->setText("Cancelling...");}}
void SstvWindow::closeEvent(QCloseEvent* event) {
    if(busy()) {closePending_=true; cancel(); event->ignore();} else QDialog::closeEvent(event);
}
void SstvWindow::reject() {
    if(busy()) {closePending_=true; cancel();} else QDialog::reject();
}
void SstvWindow::resizeEvent(QResizeEvent* event) {QDialog::resizeEvent(event); updatePreview();}
void SstvWindow::updatePreview() {
    if(original_.isNull()) preview_->clear();
    else preview_->setPixmap(QPixmap::fromImage(original_).scaled(preview_->size(),Qt::KeepAspectRatio,Qt::SmoothTransformation));
}
