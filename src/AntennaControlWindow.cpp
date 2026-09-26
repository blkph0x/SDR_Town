#include "AntennaControlWindow.h"
#include <QCheckBox>
#include <QCloseEvent>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QTextDocument>
#include <QTimer>
#include <QVBoxLayout>
#include <QPointer>
#include <QPainter>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QStyle>
#include <cmath>

namespace {
class BearingDisplay : public QWidget {
public:
    BearingDisplay(){setMinimumSize(180,180);setMaximumHeight(240);}
    double az=0,el=0;bool valid=false;
    void paintEvent(QPaintEvent*) override{
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
        const QPointF c(width()/2.0,height()/2.0);const double r=std::min(width(),height())/2.0-24;
        p.setPen(palette().color(QPalette::Midlight));p.drawEllipse(c,r,r);p.drawLine(QPointF(c.x()-r,c.y()),QPointF(c.x()+r,c.y()));
        p.drawLine(QPointF(c.x(),c.y()-r),QPointF(c.x(),c.y()+r));
        p.setPen(palette().color(QPalette::Text));p.drawText(QRectF(c.x()-10,0,20,20),Qt::AlignCenter,"N");
        p.drawText(QRectF(c.x()-10,height()-20,20,20),Qt::AlignCenter,"S");
        p.drawText(QRectF(c.x()-r-22,c.y()-10,20,20),Qt::AlignCenter,"W");p.drawText(QRectF(c.x()+r+2,c.y()-10,20,20),Qt::AlignCenter,"E");
        if(valid){const double angle=az*3.14159265358979323846/180.0;p.setPen(QPen(QColor(32,180,135),3));
            p.drawLine(c,c+QPointF(std::sin(angle)*r,-std::cos(angle)*r));}
    }
};
QDoubleSpinBox* angle(QFormLayout* form,const QString& text,const QString& key,double lo,double hi,double fallback){
    auto* box=new QDoubleSpinBox;box->setObjectName(key);box->setRange(lo,hi);box->setDecimals(1);box->setSuffix(" deg");
    box->setValue(QSettings().value("antenna/"+key,fallback).toDouble());form->addRow(text,box);return box;
}
}
AntennaControlWindow::AntennaControlWindow(QWidget* parent):QDialog(parent){
    setWindowTitle("Antenna Rotator & SWR");resize(720,650);setMinimumSize(560,560);
    auto* root=new QVBoxLayout(this);auto* tabs=new QTabWidget;root->addWidget(tabs);
    auto* pointing=new QWidget;auto* layout=new QVBoxLayout(pointing);
    auto* form=new QFormLayout;layout->addLayout(form);
    host_=new QLineEdit(QSettings().value("antenna/host","127.0.0.1").toString());host_->setObjectName("rotorHost");
    port_=new QSpinBox;port_->setRange(1,65535);port_->setValue(QSettings().value("antenna/port",4533).toInt());
    port_->setObjectName("rotorPort");
    form->addRow("rotctld host",host_);form->addRow("Port",port_);
    auto* connection=new QHBoxLayout;layout->addLayout(connection);
    auto* connectButton=new QPushButton("Connect");auto* disconnectButton=new QPushButton("Disconnect");
    connection->addWidget(connectButton);connection->addWidget(disconnectButton);
    auto* bearing=new BearingDisplay;layout->addWidget(bearing);
    position_=new QLabel("Position unavailable");position_->setObjectName("rotorPosition");layout->addWidget(position_);
    auto* target=new QFormLayout;layout->addLayout(target);
    az_=angle(target,"Target azimuth","targetAz",-180,540,0);el_=angle(target,"Target elevation","targetEl",-20,210,0);
    arm_=new QCheckBox("Arm motion");arm_->setObjectName("rotorArm");layout->addWidget(arm_);
    auto* actions=new QHBoxLayout;layout->addLayout(actions);
    auto* move=new QPushButton("Move");move->setObjectName("rotorMove");auto* park=new QPushButton("Park");auto* stop=new QPushButton("STOP");
    stop->setObjectName("rotorStop");stop->setStyleSheet("QPushButton {background:#8b2433;color:white;font-weight:bold;min-height:30px;}");
    stop->setIcon(style()->standardIcon(QStyle::SP_MediaStop));
    actions->addWidget(move);actions->addWidget(park);actions->addWidget(stop);
    state_=new QLabel("Disconnected / disarmed");state_->setWordWrap(true);layout->addWidget(state_);layout->addStretch();
    tabs->addTab(pointing,"Pointing");
    auto* settings=new QWidget;auto* limits=new QFormLayout(settings);
    minAz_=angle(limits,"Minimum azimuth","minAz",-180,540,0);maxAz_=angle(limits,"Maximum azimuth","maxAz",-180,540,360);
    minEl_=angle(limits,"Minimum elevation","minEl",-20,210,0);maxEl_=angle(limits,"Maximum elevation","maxEl",-20,210,90);
    parkAz_=angle(limits,"Park azimuth","parkAz",-180,540,0);parkEl_=angle(limits,"Park elevation","parkEl",-20,210,0);
    tabs->addTab(settings,"Limits && Park");
    auto* meter=new QWidget;auto* meterLayout=new QFormLayout(meter);
    meterHost_=new QLineEdit(QSettings().value("antenna/meterHost","127.0.0.1").toString());
    meterPort_=new QSpinBox;meterPort_->setRange(1,65535);meterPort_->setValue(QSettings().value("antenna/meterPort",4532).toInt());
    meterPort_->setObjectName("swrPort");
    meterLayout->addRow("rigctld host",meterHost_);meterLayout->addRow("Port",meterPort_);
    auto* meterConnect=new QPushButton("Connect Meter");auto* meterDisconnect=new QPushButton("Disconnect Meter");
    meterLayout->addRow(meterConnect,meterDisconnect);swr_=new QLabel("SWR unavailable");swr_->setObjectName("swrReading");swr_->setWordWrap(true);meterLayout->addRow(swr_);
    tabs->addTab(meter,"SWR Meter");
    history_=new QPlainTextEdit;history_->setReadOnly(true);history_->document()->setMaximumBlockCount(300);tabs->addTab(history_,"Events");
    auto* refresh=new QTimer(this);refresh->setInterval(250);
    connect(refresh,&QTimer::timeout,this,[this,bearing,move,park,settings]{
        move->setEnabled(rotor_.armed()&&rotor_.fresh());park->setEnabled(move->isEnabled());
        settings->setEnabled(!rotor_.connected());
        if(!rotor_.fresh()){bearing->valid=false;bearing->update();position_->setText("Position unavailable / stale");}
    });refresh->start();move->setEnabled(false);park->setEnabled(false);
    connect(connectButton,&QPushButton::clicked,this,[this]{save();rotor_.connectTo(host_->text(),quint16(port_->value()),{minAz_->value(),maxAz_->value(),minEl_->value(),maxEl_->value()});});
    connect(disconnectButton,&QPushButton::clicked,&rotor_,&RotatorController::disconnectFromController);
    connect(arm_,&QCheckBox::clicked,this,[this](bool checked){
        if(!checked){rotor_.stop();return;}
        const bool accepted=QMessageBox::warning(this,"Enable physical movement",
            "Confirm the antenna path is clear, cable travel and limits are correct, and a physical stop is available. "
            "Network control cannot guarantee an emergency stop.",QMessageBox::Yes|QMessageBox::No,QMessageBox::No)==QMessageBox::Yes;
        if(!accepted||!rotor_.arm()){QSignalBlocker block(arm_);arm_->setChecked(false);}
    });
    connect(&rotor_,&RotatorController::armedChanged,this,[this](bool armed){QSignalBlocker block(arm_);arm_->setChecked(armed);});
    connect(move,&QPushButton::clicked,this,[this]{save();rotor_.moveTo(az_->value(),el_->value());});
    connect(park,&QPushButton::clicked,this,[this]{rotor_.moveTo(parkAz_->value(),parkEl_->value());});
    connect(stop,&QPushButton::clicked,&rotor_,&RotatorController::stop);
    connect(&rotor_,&RotatorController::position,this,[this,bearing](double az,double el){
        position_->setText(QString("Actual azimuth %1 deg   Elevation %2 deg").arg(az,0,'f',1).arg(el,0,'f',1));bearing->az=az;bearing->el=el;bearing->valid=true;bearing->update();
    });
    connect(&rotor_,&RotatorController::state,this,[this](const QString& value){state_->setText(value);log(value);});
    connect(meterConnect,&QPushButton::clicked,this,[this]{save();meter_.connectTo(meterHost_->text(),quint16(meterPort_->value()));});
    connect(meterDisconnect,&QPushButton::clicked,&meter_,&SwrMonitor::disconnectMeter);
    connect(&meter_,&SwrMonitor::reading,this,[this](double value){swr_->setText(QString("Hardware SWR  %1 : 1").arg(value,0,'f',2));});
    connect(&meter_,&SwrMonitor::status,this,[this](const QString& value){swr_->setText(value);});
}
void AntennaControlWindow::log(const QString& text){
    const auto row=QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)+" "+text;
    history_->appendPlainText(row);
    const auto dir=QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)+"/antenna";
    if(!QDir().mkpath(dir))return;
    QFile file(dir+"/control.log");
    // DEC-0140: two bounded local files, never a capture or credentials.
    if(file.size()>1024*1024){QFile::remove(dir+"/control.previous.log");file.rename(dir+"/control.previous.log");file.setFileName(dir+"/control.log");}
    if(file.open(QIODevice::WriteOnly|QIODevice::Append))file.write(row.toUtf8()+"\n");
}
void AntennaControlWindow::save(){
    QSettings s;s.setValue("antenna/host",host_->text());s.setValue("antenna/port",port_->value());
    s.setValue("antenna/meterHost",meterHost_->text());s.setValue("antenna/meterPort",meterPort_->value());
    for(auto* box:{az_,el_,minAz_,maxAz_,minEl_,maxEl_,parkAz_,parkEl_})s.setValue("antenna/"+box->objectName(),box->value());
}
void AntennaControlWindow::closeEvent(QCloseEvent* event){save();if(rotor_.connected())rotor_.stop();meter_.disconnectMeter();QDialog::closeEvent(event);}
void installAntennaControlMenu(QMainWindow& window){
    QMenu* menu=nullptr;for(auto* action:window.menuBar()->actions())if(action->menu()&&action->text().remove('&')=="Tools"){menu=action->menu();break;}
    if(!menu)menu=window.menuBar()->addMenu("&Tools");
    auto* panel=new AntennaControlWindow(&window);
    menu->addAction("Antenna Rotator && SWR...",panel,[panel]{panel->show();panel->raise();panel->activateWindow();});
}
