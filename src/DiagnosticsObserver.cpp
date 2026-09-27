#include "DiagnosticsObserver.h"
#include <QApplication>
#include <QAbstractButton>
#include <QAbstractSlider>
#include <QAction>
#include <QComboBox>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QEvent>
#include <QFileDialog>
#include <QInputDialog>
#include <QPointer>
#include <QTimer>
#include <chrono>

namespace {
bool excluded(QObject* object) {
    for(auto* p=object;p;p=p->parent())
        if(qobject_cast<QFileDialog*>(p) || qobject_cast<QInputDialog*>(p)) return true;
    return false;
}
QString controlPath(QObject* object) {
    QStringList parts;
    for(auto* p=object;p && parts.size()<10;p=p->parent()) {
        int ordinal=0;
        if(p->parent()) for(auto* sibling:p->parent()->children()) {
            if(sibling==p) break;
            if(sibling->metaObject()==p->metaObject()) ++ordinal;
        }
        // No objectName/windowTitle: plugins and user-supplied labels can contain secrets.
        parts.prepend(QString::fromLatin1(p->metaObject()->className())+"["+QString::number(ordinal)+"]");
    }
    return parts.join('/');
}
QString safeCaption(QObject* object) {
    QString text;
    if(auto* button=qobject_cast<QAbstractButton*>(object)) text=button->text();
    if(auto* action=qobject_cast<QAction*>(object)) text=action->text();
    text.remove('&'); text=text.simplified();
    static const QSet<QString> allowed={"Apply","OK","Cancel","Close","Refresh","Rescan",
        "Add Receiver","Remove","Set/Tune Device","Tune","Auto BW","LPF","Auto",
        "Outputs...","Monitor CC","Follow TG","Auto Follow Grants","Traffic Source",
        "Start IQ Capture","Stop IQ Capture","Receive RF","Finish","Decode","Stop",
        "Bias-T","Bias T","AGC","Connect","Disconnect","Play","Pause","Mute",
        "Configure Output Devices...","Device Manager...","Share Diagnostic Reports",
        "Set Tune Device","Detect BW","Band Plan...","Automatic IF gain","Bias-T power",
        "Broadcast notch","DAB notch","Reference clock output","HDR (RSPdx below 2 MHz)",
        "IQ correction","Apply Changes","Apply (Live)","Rescan Devices","Refresh Device List",
        "Test 1kHz","Scan P25 CC","Grant Test","Add CC...","P25 Log","Add TG...",
        "Aliases...","Verify","Add to Scanner","Set Priority...","Delete TG","Refresh TGs",
        "SSTV Images...","Satcom / Inmarsat...","Inmarsat Aero...","IQ Replay...",
        "P25 Decoder Log...","Report Issue...","My Submitted Issues...","About SDR Town"};
    return allowed.contains(text)?text:QString();
}
}

DiagnosticsObserver::DiagnosticsObserver(QObject* owner, Snapshot snapshot,
    std::function<QString()> session, Submit submit)
    :QObject(owner),snapshot_(std::move(snapshot)),session_(std::move(session)),submit_(std::move(submit)) {
    qApp->installEventFilter(this);
    auto* timer=new QTimer(this);
    connect(timer,&QTimer::timeout,this,[this]{tick();});
    timer->start(1000); // DEC-0160: telemetry cadence only.
}

bool DiagnosticsObserver::eventFilter(QObject* watched,QEvent* event) {
    if(event->type()==QEvent::Show && !session_().isEmpty() && !excluded(watched)) observe(watched);
    return false;
}

void DiagnosticsObserver::observe(QObject* root) {
    if(!root || excluded(root)) return;
    auto objects=root->findChildren<QObject*>(); objects.prepend(root);
    for(auto* object:objects) {
        if(wired_.contains(object) || excluded(object)) continue;
        bool supported=true;
        if(auto* b=qobject_cast<QAbstractButton*>(object))
            connect(b,&QAbstractButton::pressed,this,[this,b]{record(b,"press",b->isCheckable()?QJsonValue(b->isChecked()):QJsonValue());});
        else if(auto* a=qobject_cast<QAction*>(object))
            connect(a,&QAction::triggered,this,[this,a](bool checked){record(a,"action",a->isCheckable()?QJsonValue(checked):QJsonValue());});
        else if(auto* c=qobject_cast<QComboBox*>(object))
            connect(c,&QComboBox::activated,this,[this,c](int index){record(c,"selectIndex",index);});
        else if(auto* s=qobject_cast<QDoubleSpinBox*>(object))
            connect(s,&QDoubleSpinBox::editingFinished,this,[this,s]{record(s,"number",s->value());});
        else if(auto* s=qobject_cast<QSpinBox*>(object))
            connect(s,&QSpinBox::editingFinished,this,[this,s]{record(s,"number",s->value());});
        else if(auto* s=qobject_cast<QAbstractSlider*>(object))
            connect(s,&QAbstractSlider::valueChanged,this,[this,s](int value){record(s,"sliderValue",value);});
        else supported=false;
        if(supported) {
            wired_.insert(object);
            connect(object,&QObject::destroyed,this,[this,object]{wired_.remove(object);});
        }
    }
}

void DiagnosticsObserver::record(QObject* object,const QString& kind,QJsonValue value) {
    const auto session=session_();
    if(session.isEmpty()) {actions_={};dropped_=0;currentSession_.clear();return;}
    if(currentSession_!=session) {actions_={};dropped_=0;currentSession_=session;ticks_=0;}
    if(actions_.size()>=32) {++dropped_;return;}
    QJsonObject action{{"control",controlPath(object)}, {"kind",kind},
        {"timeUtc",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}};
    const auto caption=safeCaption(object);
    if(!caption.isEmpty()) action["command"]=caption;
    static const QSet<QString> knownIds={"sdrplayAntenna","sdrplayAgc","sdrplayIfgr","sdrplayRfgr",
        "sdrplayBandwidth","sdrplayAgcSetpoint","sdrplayRfSelect","rtlBiasT","autoBandwidthCheck",
        "sstvSource","sstvRfMode"};
    if(knownIds.contains(object->objectName())) action["controlId"]=object->objectName();
    if(value.isBool() || value.isDouble()) action["value"]=value;
    actions_.append(action);
    const auto now=std::chrono::steady_clock::now();
    if(kind=="press" && now-lastIntent_>=std::chrono::seconds(1)) {
        // Queue intent before the clicked handler can block. Values are before
        // the click; the subsequent runtime snapshot records applied state.
        submit_("ui.intent",{{"action",action},{"semantics","button pressed; checked value before activation"}});
        lastIntent_=now;
    }
}

void DiagnosticsObserver::tick() {
    const auto session=session_();
    if(session.isEmpty()) {actions_={};dropped_=0;ticks_=0;currentSession_.clear();return;}
    if(currentSession_!=session) {actions_={};dropped_=0;ticks_=0;currentSession_=session;}
    if(ticks_==0) for(auto* widget:QApplication::topLevelWidgets()) observe(widget);
    const bool actionBatch=!actions_.isEmpty() && ticks_%2==0;
    if(actionBatch) {
        submit_("ui.actions",{{"actions",actions_},{"dropped",dropped_},{"batch",QString::number(++batch_)},
            {"semantics","control activation/value observed; not an operation-success acknowledgement"}});
        actions_={};dropped_=0;
    }
    if(ticks_%30==0 || actionBatch) {
        const auto started=std::chrono::steady_clock::now();
        auto snapshot=snapshot_();
        snapshot["reason"]=actionBatch?"after-controls":"periodic";
        snapshot["lastActionBatch"]=QString::number(batch_);
        snapshot["observedAtUtc"]=QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        snapshot["samplingMs"]=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
        submit_("app.runtime",snapshot);
    }
    ++ticks_;
}
