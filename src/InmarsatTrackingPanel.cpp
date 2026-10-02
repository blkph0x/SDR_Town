#include "InmarsatTrackingPanel.h"
#include "InmarsatMapWidget.h"
#include <QCheckBox>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSettings>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QStyle>
#include <QShowEvent>
#include <QHideEvent>

InmarsatTrackingPanel::InmarsatTrackingPanel(QWidget* parent):QWidget(parent),lookup_(model_,this) {
    setObjectName("inmarsatTrackingPanel");
    auto* layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);
    auto* controls=new QHBoxLayout;
    online_=new QCheckBox("Online positions (ADSB.lol)");online_->setObjectName("inmarsatOnlinePositions");
    online_->setToolTip("Optional lookup of recently received Classic Aero ICAO addresses. Turning off removes all online positions.");
    estimates_=new QCheckBox("Estimated movement");estimates_->setObjectName("inmarsatEstimatedMovement");
    estimates_->setToolTip("Labelled estimates from valid ground track and speed, for at most two minutes. Not navigation data.");
    controls->addWidget(online_);controls->addWidget(estimates_);controls->addStretch();
    auto* clear=new QToolButton;clear->setIcon(style()->standardIcon(QStyle::SP_TrashIcon));
    clear->setToolTip("Clear received aircraft and online positions");clear->setAccessibleName("Clear aircraft");
    controls->addWidget(clear);layout->addLayout(controls);
    status_=new QLabel;status_->setObjectName("inmarsatTrackingStatus");status_->setWordWrap(true);status_->setTextFormat(Qt::PlainText);
    layout->addWidget(status_);map_=new InmarsatMapWidget;layout->addWidget(map_,1);
    QSettings settings;
    online_->setChecked(settings.value("inmarsat/mapOnlineConsentV1",false).toBool());
    estimates_->setChecked(settings.value("inmarsat/mapEstimates",false).toBool());
    lookup_.setEnabled(online_->isChecked());
    connect(online_,&QCheckBox::toggled,this,[this](bool enabled) {
        if(enabled && QMessageBox::question(this,"Enable online aircraft positions?",
            "ADSB.lol will receive the ICAO addresses of aircraft received by this app and your normal HTTPS connection metadata, including your IP address. "
            "No messages, audio, IQ or receiver location are sent. Internet and RF positions remain separately labelled. "
            "This choice is remembered; disabling removes online positions immediately. Enable?",
            QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes) {
            QSignalBlocker block(online_);online_->setChecked(false);return;
        }
        QSettings().setValue("inmarsat/mapOnlineConsentV1",enabled);
        lookup_.setEnabled(enabled);refresh();
    });
    connect(estimates_,&QCheckBox::toggled,this,[this](bool enabled){QSettings().setValue("inmarsat/mapEstimates",enabled);refresh();});
    connect(clear,&QToolButton::clicked,this,[this] {
        InmarsatMessageStore::instance().clearAircraft();
        lookup_.setEnabled(false);lookup_.setEnabled(online_->isChecked());refresh();
    });
    lookup_.changed=[this]{refresh();};
    timer_.setInterval(1000);connect(&timer_,&QTimer::timeout,this,&InmarsatTrackingPanel::refresh);
}
void InmarsatTrackingPanel::setReceiverReport(const nlohmann::json& report) {
    activity_=report;lastActivity_=inmarsatMonotonicSeconds();refresh();
}
void InmarsatTrackingPanel::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);lookup_.setActive(true);timer_.start();refresh();
}
void InmarsatTrackingPanel::hideEvent(QHideEvent* event) {
    if(inmarsatMonotonicSeconds()>=webObserverUntil_) {timer_.stop();lookup_.setActive(false);}
    QWidget::hideEvent(event);
}
nlohmann::json InmarsatTrackingPanel::webReport(const nlohmann::json& report) {
    // DEC-0165: a bounded web observer shares the native cache and consent.
    webObserverUntil_=inmarsatMonotonicSeconds()+6;
    timer_.start();lookup_.setActive(true);setReceiverReport(report);
    auto result=lastReport_;
    // The control DLL requires an explicit success flag even for an empty map.
    result["ok"]=true;
    result["onlineEnabled"]=online_->isChecked();
    result["estimatesEnabled"]=estimates_->isChecked();
    return result;
}
void InmarsatTrackingPanel::refresh() {
    const auto aircraft=InmarsatMessageStore::instance().trackingAircraft();
    const double now=inmarsatMonotonicSeconds();
    const bool observed=isVisible() || now<webObserverUntil_;
    lookup_.setActive(observed);
    if(!observed)timer_.stop();
    model_.setRf(aircraft,now);
    lookup_.poll();
    auto activity=activity_;
    // Presentation heartbeat only. A closed receiver panel must not preserve
    // a green call marker forever; four missed 500 ms UI snapshots are stale.
    if(now-lastActivity_>2)activity["receptionRunning"]=false;
    auto report=model_.report(activity,now,estimates_->isChecked());
    auto counts=report["tracking"];counts.update(lookup_.report());
    report["tracking"]=counts;
    lastReport_=report;
    map_->setReport(report,false);
    const auto state=QString::fromStdString(counts.value("lookupState",std::string{})).replace('_',' ');
    status_->setText(QString("%1 aircraft | RF %2 | Online %3 | Estimated %4 | Unlocated %5 | %6")
        .arg(counts.value("mapAircraft",0)).arg(counts.value("mapRfPositions",0)).arg(counts.value("mapOnlinePositions",0))
        .arg(counts.value("mapEstimatedPositions",0)).arg(counts.value("mapUnlocated",0)).arg(state));
    if(!diagnosticError_.isEmpty())status_->setText(status_->text()+" | "+diagnosticError_);
    if(now-lastLog_>=5 && observed) {
        lastLog_=now;
        try {
            if(diagnostics_.path().isEmpty())diagnostics_.open({},"inmarsat-map");
            auto details=counts;
            // Local evidence includes bounded identity/source decisions. The
            // remote allowlist below transmits counters only, never this array.
            details["aircraft"]=report["aircraft"];
            diagnostics_.write("tracking",details);
            diagnosticError_=diagnostics_.error();
        } catch(const std::exception&) {diagnosticError_="Map diagnostic log unavailable";}
    }
    status_->setToolTip("ADSB.lol online position data | Not for navigation\n"+diagnostics_.path());
}
