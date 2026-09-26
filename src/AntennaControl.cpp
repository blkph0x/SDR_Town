#include "AntennaControl.h"
#include <QLocale>
#include <QSignalBlocker>
#include <cmath>

HamlibSession::HamlibSession(QObject* parent):QObject(parent) {
    socket_.setReadBufferSize(8193);
    deadline_.setSingleShot(true);deadline_.setInterval(1500);
    connect(&deadline_,&QTimer::timeout,this,[this]{fail("Controller response timed out; physical stop may be required");});
    connect(&socket_,&QTcpSocket::connected,this,[this]{deadline_.stop();emit connected();});
    connect(&socket_,&QTcpSocket::readyRead,this,&HamlibSession::read);
    connect(&socket_,&QTcpSocket::errorOccurred,this,[this](auto){fail("Controller connection failed");});
    connect(&socket_,&QTcpSocket::disconnected,this,[this]{if(!expected_.isEmpty())fail("Controller disconnected during command");else emit failed("Controller disconnected");});
}
bool HamlibSession::ready() const{return socket_.state()==QAbstractSocket::ConnectedState;}
HamlibSession::~HamlibSession(){QObject::disconnect(&socket_,nullptr,this,nullptr);deadline_.stop();socket_.abort();}
void HamlibSession::close(){deadline_.stop();expected_.clear();input_.clear();fields_.clear();QSignalBlocker block(&socket_);socket_.abort();}
void HamlibSession::fail(const QString& reason){close();emit failed(reason);}
void HamlibSession::open(const QString& host,quint16 port){
    close();
    if(host.trimmed().isEmpty()||host.size()>253||host.contains('\n')||host.contains('\r')||port==0){emit failed("Invalid controller address");return;}
    socket_.connectToHost(host.trimmed(),port);deadline_.start();
}
bool HamlibSession::request(const QByteArray& command,const QString& expected){
    if(!ready()||busy())return false;
    expected_=expected;input_.clear();fields_.clear();echoed_=false;bytes_=0;
    if(socket_.write(command)!=command.size()){fail("Controller write failed");return false;}
    deadline_.start();return true;
}
void HamlibSession::read(){
    const auto data=socket_.readAll();bytes_+=int(data.size());
    if(expected_.isEmpty()||bytes_>8192){fail("Unexpected or oversized controller response");return;}
    input_+=data;
    while(input_.contains('\n')){
        const int end=input_.indexOf('\n');const QString line=QString::fromLatin1(input_.left(end)).trimmed();input_.remove(0,end+1);
        if(!echoed_){if(!line.startsWith(expected_+":")){fail("Controller response does not match command");return;}echoed_=true;continue;}
        if(line.startsWith("RPRT ")){
            bool ok=false;const int code=line.mid(5).toInt(&ok);
            if(!ok||!input_.isEmpty()){fail("Malformed controller acknowledgement");return;}
            deadline_.stop();const auto command=expected_;const auto fields=fields_;expected_.clear();
            emit response(command,code,fields);return;
        }
        const int colon=line.indexOf(':');
        if(colon<=0||fields_.contains(line.left(colon))){fail("Malformed controller fields");return;}
        fields_.insert(line.left(colon),line.mid(colon+1).trimmed());
    }
}
bool RotorLimits::valid() const{
    return std::isfinite(minAz)&&std::isfinite(maxAz)&&std::isfinite(minEl)&&std::isfinite(maxEl)&&
        minAz>=-180&&maxAz<=540&&minEl>=-20&&maxEl<=210&&minAz<maxAz&&minEl<=maxEl;
}
bool RotorLimits::contains(double az,double el) const{
    return valid()&&std::isfinite(az)&&std::isfinite(el)&&az>=minAz&&az<=maxAz&&el>=minEl&&el<=maxEl;
}
namespace {
bool number(const QMap<QString,QString>& fields,const QString& key,double& value){
    bool ok=false;value=QLocale::c().toDouble(fields.value(key),&ok);return ok&&std::isfinite(value);
}
}
RotatorController::RotatorController(QObject* parent):QObject(parent){
    poll_.setInterval(1000);
    connect(&poll_,&QTimer::timeout,this,&RotatorController::poll);
    connect(&link_,&HamlibSession::connected,this,[this]{emit state("Connected; reading position");poll_.start();poll();});
    connect(&link_,&HamlibSession::failed,this,[this](const QString& error){poll_.stop();positionAge_.invalidate();stopPending_=false;disconnectPending_=false;disarm();emit state(error);});
    connect(&link_,&HamlibSession::response,this,[this](const QString& command,int status,const QMap<QString,QString>& fields){
        if(status!=0){positionAge_.invalidate();if(armed_&&command!="stop")stopPending_=true;disarm();emit state(QString("Controller rejected %1 (Hamlib %2)").arg(command).arg(status));}
        else if(command=="get_pos"){
            double az=0,el=0;
            if(!number(fields,"Azimuth",az)||!number(fields,"Elevation",el)||!limits_.contains(az,el)){
                positionAge_.invalidate();if(armed_)stopPending_=true;disarm();emit state("Position invalid or outside configured limits");
            }else{positionAge_.restart();emit position(az,el);}
        }else if(command=="set_pos")emit state("Move acknowledged; checking actual position");
        else if(command=="stop"){emit state("Stop acknowledged by controller");emit stopped();}
        if(command=="stop"&&disconnectPending_){disconnectPending_=false;poll_.stop();positionAge_.invalidate();link_.close();emit state(status==0?"Disconnected after Stop acknowledgement":"Disconnected; Stop failed, use physical stop");return;}
        if(stopPending_){stopPending_=false;link_.request("+S\n","stop");}
    });
}
void RotatorController::disarm(){if(armed_){armed_=false;emit armedChanged(false);}}
bool RotatorController::fresh() const{return link_.ready()&&positionAge_.isValid()&&positionAge_.elapsed()<=2500;}
void RotatorController::connectTo(const QString& host,quint16 port,const RotorLimits& limits){
    if(link_.ready()){emit state("Disconnect before changing controller settings");return;}
    disarm();positionAge_.invalidate();limits_=limits;
    if(!limits.valid()){emit state("Invalid motion limits");return;}
    link_.open(host,port);
}
void RotatorController::disconnectFromController(){
    if(link_.ready()){disconnectPending_=true;stop();}
    else{poll_.stop();disarm();positionAge_.invalidate();stopPending_=false;link_.close();}
}
bool RotatorController::arm(){if(!fresh()||stopPending_||link_.busy()){emit state("Cannot arm: waiting for fresh, idle controller feedback");return false;}armed_=true;emit armedChanged(true);emit state("Motion armed");return true;}
void RotatorController::stop(){disarm();if(!link_.ready()){emit state("No controller connection; use the physical stop");return;}stopPending_=true;if(!link_.busy()){stopPending_=false;link_.request("+S\n","stop");}}
bool RotatorController::moveTo(double az,double el){
    if(!armed_||!fresh()||stopPending_||!limits_.contains(az,el)){emit state("Move blocked: arm, position freshness or motion limits");return false;}
    const auto cmd=QString("+P %1 %2\n").arg(az,0,'f',3).arg(el,0,'f',3).toLatin1();
    if(!link_.request(cmd,"set_pos")){emit state("Controller busy; move was not sent");return false;}
    return true;
}
void RotatorController::poll(){if(armed_&&!fresh())stop();if(!link_.busy()&&!stopPending_)link_.request("+p\n","get_pos");}

SwrMonitor::SwrMonitor(QObject* parent):QObject(parent){
    poll_.setInterval(1000);
    connect(&poll_,&QTimer::timeout,this,[this]{if(!link_.busy()){emit status("Reading hardware");link_.request("+t\n","get_ptt");}});
    connect(&link_,&HamlibSession::connected,this,[this]{poll_.start();emit status("Connected; waiting for hardware reading");});
    connect(&link_,&HamlibSession::failed,this,[this](const QString& error){poll_.stop();emit status(error);});
    connect(&link_,&HamlibSession::response,this,[this](const QString& cmd,int code,const QMap<QString,QString>& fields){
        if(code!=0){emit status(QString("Meter unavailable (Hamlib %1)").arg(code));return;}
        double value=0;
        if(cmd=="get_ptt"){
            if(number(fields,"PTT",value)&&(value==1||value==2||value==3))link_.request("+l SWR\n","get_level");
            else emit status("No active transmission; SWR unavailable");
        }else if(number(fields,"Level Value",value)&&value>=1&&value<=1000)emit reading(value);
        else emit status("Invalid hardware SWR reading");
    });
}
void SwrMonitor::connectTo(const QString& host,quint16 port){poll_.stop();emit status("Connecting");link_.open(host,port);}
void SwrMonitor::disconnectMeter(){poll_.stop();link_.close();emit status("Meter disconnected");}
