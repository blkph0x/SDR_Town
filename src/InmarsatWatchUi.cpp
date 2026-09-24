#include "InmarsatWidget.h"
#include "InmarsatEngine.h"
#include "InmarsatWatchSpectrum.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStyle>
#include <QTableWidget>
#include <QUuid>
#include <QVBoxLayout>

QWidget* InmarsatWidget::buildWatchUi() {
    auto* page=new QWidget;
    auto* root=new QVBoxLayout(page);
    watchSpectrum_=new InmarsatWatchSpectrum;
    root->addWidget(watchSpectrum_,2);
    connect(watchSpectrum_,&InmarsatWatchSpectrum::frequencySelected,this,[this](double hz){frequency_->setValue(hz/1e6);});
    watchStatus_=new QLabel("Manual channel");watchStatus_->setWordWrap(true);
    watchStatus_->setObjectName("inmarsatWatchStatus");root->addWidget(watchStatus_);
    watchEditors_=new QWidget;auto* editors=new QVBoxLayout(watchEditors_);editors->setContentsMargins(0,0,0,0);
    auto* row=new QHBoxLayout;
    watchName_=new QLineEdit;watchName_->setPlaceholderText("Channel name");watchName_->setMaxLength(100);
    watchName_->setObjectName("inmarsatWatchName");row->addWidget(watchName_,1);
    auto* add=new QPushButton(style()->standardIcon(QStyle::SP_FileDialogNewFolder),"Add channel");
    add->setObjectName("inmarsatWatchAdd");row->addWidget(add);
    auto* remove=new QPushButton(style()->standardIcon(QStyle::SP_TrashIcon),"Remove");
    remove->setObjectName("inmarsatWatchRemove");row->addWidget(remove);editors->addLayout(row);
    watchTable_=new QTableWidget(0,4);watchTable_->setObjectName("inmarsatWatchTable");
    watchTable_->setHorizontalHeaderLabels({"Enabled","Name","MHz","Decoder rate"});
    watchTable_->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);
    watchTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    watchTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    watchTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);watchTable_->setMinimumHeight(100);
    editors->addWidget(watchTable_,1);
    auto* grid=new QGridLayout;
    auto spin=[&](const char* text,const char* name,int lo,int hi,int index){
        auto* s=new QSpinBox;s->setRange(lo,hi);s->setObjectName(name);
        grid->addWidget(new QLabel(text),index/3*2,index%3);
        grid->addWidget(s,index/3*2+1,index%3);return s;
    };
    dataMin_=spin("Data minimum (s)","inmarsatDataMin",1,600,0);
    dataDwell_=spin("Data dwell / group (s)","inmarsatDataDwell",1,600,1);
    positionTarget_=spin("Position target","inmarsatPositionTarget",1,256,2);
    voiceAcquire_=spin("Voice acquisition (s)","inmarsatVoiceAcquire",1,120,3);
    voiceIdle_=spin("Voice idle hold (s)","inmarsatVoiceIdle",1,120,4);
    refreshInterval_=spin("Refresh interval (s)","inmarsatRefresh",10,3600,5);
    maxVoice_=spin("Maximum voice visit (s)","inmarsatMaxVoice",10,7200,6);
    watchConcurrent_=spin("Concurrent decoders","inmarsatWatchConcurrent",1,4,7);
    auto* save=new QPushButton(style()->standardIcon(QStyle::SP_DialogSaveButton),"Save timing");
    save->setObjectName("inmarsatWatchSave");grid->addWidget(save,5,2);editors->addLayout(grid);
    root->addWidget(watchEditors_,3);
    connect(save,&QPushButton::clicked,this,&InmarsatWidget::saveWatchPolicy);
    connect(add,&QPushButton::clicked,this,[this]{
        try {
            auto c=InmarsatEngine::instance().config();frequency_->interpretText();
            c.watch.channels.push_back({QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString(),
                watchName_->text().trimmed().toStdString(),frequency_->value()*1e6,decoderCombo_->currentData().toInt(),true});
            if(!InmarsatEngine::instance().setConfig(c))throw std::runtime_error(InmarsatEngine::instance().snapshot().lastStatus);
            reloadWatchUi();
        } catch(const std::exception& e){QMessageBox::warning(this,"Watch list",e.what());}
    });
    connect(remove,&QPushButton::clicked,this,[this]{
        const int row=watchTable_->currentRow();auto c=InmarsatEngine::instance().config();
        if(row<0 || size_t(row)>=c.watch.channels.size())return;
        c.watch.channels.erase(c.watch.channels.begin()+row);
        try {
            if(!InmarsatEngine::instance().setConfig(c))throw std::runtime_error(InmarsatEngine::instance().snapshot().lastStatus);
            reloadWatchUi();}
        catch(const std::exception& e){QMessageBox::warning(this,"Watch list",e.what());}
    });
    connect(watchTable_,&QTableWidget::itemChanged,this,[this](QTableWidgetItem* item){
        if(item->column()!=0)return;
        auto c=InmarsatEngine::instance().config();
        if(size_t(item->row())>=c.watch.channels.size())return;
        c.watch.channels[item->row()].enabled=item->checkState()==Qt::Checked;
        try {if(!InmarsatEngine::instance().setConfig(c))throw std::runtime_error(InmarsatEngine::instance().snapshot().lastStatus);}
        catch(const std::exception& e){QMessageBox::warning(this,"Watch list",e.what());reloadWatchUi();}
    });
    connect(watchTable_,&QTableWidget::cellClicked,this,[this](int row,int){
        const auto c=InmarsatEngine::instance().config();
        if(row<0 || size_t(row)>=c.watch.channels.size())return;
        const auto& channel=c.watch.channels[row];
        frequency_->setValue(channel.frequencyHz/1e6);decoderCombo_->setCurrentIndex(decoderCombo_->findData(channel.rate));
        watchName_->setText(QString::fromStdString(channel.label));
    });
    reloadWatchUi();return page;
}
void InmarsatWidget::reloadWatchUi() {
    const auto c=InmarsatEngine::instance().config().watch;
    QSignalBlocker block(watchTable_);watchTable_->setRowCount(int(c.channels.size()));
    for(size_t i=0;i<c.channels.size();++i) {
        const auto& channel=c.channels[i];auto* on=new QTableWidgetItem;
        on->setFlags(Qt::ItemIsEnabled|Qt::ItemIsSelectable|Qt::ItemIsUserCheckable);
        on->setCheckState(channel.enabled?Qt::Checked:Qt::Unchecked);watchTable_->setItem(int(i),0,on);
        watchTable_->setItem(int(i),1,new QTableWidgetItem(QString::fromStdString(channel.label)));
        watchTable_->setItem(int(i),2,new QTableWidgetItem(QString::number(channel.frequencyHz/1e6,'f',6)));
        watchTable_->setItem(int(i),3,new QTableWidgetItem(QString::number(std::abs(channel.rate))+(channel.rate<0?" burst":channel.voice()?" voice":" data")));
    }
    dataMin_->setValue(c.dataMinSeconds);dataDwell_->setValue(c.dataDwellSeconds);positionTarget_->setValue(c.positionTarget);
    voiceAcquire_->setValue(c.voiceAcquireSeconds);voiceIdle_->setValue(c.voiceIdleSeconds);
    refreshInterval_->setValue(c.refreshSeconds);maxVoice_->setValue(c.maxVoiceSeconds);
    watchConcurrent_->setValue(c.maxConcurrentChannels);
}
void InmarsatWidget::saveWatchPolicy() {
    try {
        auto c=InmarsatEngine::instance().config();auto& w=c.watch;
        w.dataMinSeconds=dataMin_->value();w.dataDwellSeconds=dataDwell_->value();w.positionTarget=positionTarget_->value();
        w.voiceAcquireSeconds=voiceAcquire_->value();w.voiceIdleSeconds=voiceIdle_->value();
        w.refreshSeconds=refreshInterval_->value();w.maxVoiceSeconds=maxVoice_->value();
        w.maxConcurrentChannels=watchConcurrent_->value();
        if(!InmarsatEngine::instance().setConfig(c))throw std::runtime_error(InmarsatEngine::instance().snapshot().lastStatus);
        watchStatus_->setText("Watch timing saved");
    }catch(const std::exception& e){QMessageBox::warning(this,"Watch timing",e.what());}
}
void InmarsatWidget::updateWatchUi(bool running,const nlohmann::json& report) {
    const auto cfg=InmarsatEngine::instance().config();
    // Manual live spectrum is also the watch-list editor. Only a running
    // automatic session freezes its immutable worker-owned configuration.
    watchEditors_->setEnabled(!running || !cfg.watch.enabled);voiceFollowCheck_->setEnabled(!running);
    QSignalBlocker block(voiceFollowCheck_);voiceFollowCheck_->setChecked(cfg.watch.enabled);
    if(!running)watchStatus_->setText(cfg.watch.enabled?"Automatic watch ready":"Manual channel");
    else if(report.contains("watch")) {
        const auto& w=report["watch"];
        watchStatus_->setText(QString("%1 | Group %2/%3 | Positions this visit: %4/%5 | %6 | %7")
            .arg(QString::fromStdString(w.value("phase",std::string{})))
            .arg(w.value("group",0)).arg(w.value("groups",0)).arg(w.value("visitPositions",0)).arg(w.value("positionTarget",0))
            .arg(QString::fromStdString(w.value("collection",std::string{})))
            .arg(w.value("refreshDue",false)?"Refresh pending while voice active":QString::fromStdString(w.value("reason",std::string{}))));
        const auto load=w.value("loadRatio",0.0);
        watchStatus_->setText(watchStatus_->text()+QString(" | Processing: %1x RF time%2")
            .arg(load,0,'f',2).arg(load>=1?" - OVERLOADED":""));
    }
}
