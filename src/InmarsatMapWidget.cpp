#include "InmarsatMapWidget.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>
#include <QWheelEvent>
#include <QToolButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QStyle>
#include <algorithm>
#include <cmath>
#include <set>

static void initMapResource() { Q_INIT_RESOURCE(inmarsat_map); }
InmarsatMapWidget::InmarsatMapWidget(QWidget* parent):QWidget(parent) {
    initMapResource();
    setObjectName("inmarsatMap"); setMinimumSize(360,220); setMouseTracking(true);
    QFile file(":/inmarsat/land.geojson");
    if(file.open(QIODevice::ReadOnly)) {
        const auto features=QJsonDocument::fromJson(file.readAll()).object()["features"].toArray();
        auto polygon=[this](const QJsonArray& rings) {
            for(const auto& ring:rings) {
                QPolygonF points;
                for(const auto& value:ring.toArray()) {
                    const auto p=value.toArray(); points<<QPointF(p[0].toDouble(),-p[1].toDouble());
                }
                land_.addPolygon(points);
            }
        };
        for(const auto& feature:features) {
            const auto g=feature.toObject()["geometry"].toObject();
            if(g["type"].toString()=="Polygon") polygon(g["coordinates"].toArray());
            else for(const auto& p:g["coordinates"].toArray()) polygon(p.toArray());
        }
    }
    auto* layout=new QVBoxLayout(this);
    layout->setContentsMargins(12,12,12,32); // Keep status text above the painted legend.
    auto* bar=new QHBoxLayout; bar->setAlignment(Qt::AlignTop|Qt::AlignRight);
    layout->addLayout(bar);layout->addStretch();
    auto* voiceStatus=new QLabel(this);voiceStatus->setObjectName("inmarsatMapVoiceStatus");
    voiceStatus->setWordWrap(true);voiceStatus->setStyleSheet("color: white; background-color: #15252b;");
    layout->addWidget(voiceStatus);voiceStatus->hide();
    auto* legend=new QLabel("Green: received call activity | White: RF | Blue: ADSB.lol | Yellow outline: estimate | Gray: stale");
    legend->setObjectName("inmarsatMapLegend");legend->setWordWrap(true);
    legend->setStyleSheet("color: white; background-color: #15252b; font-size: 11px;");layout->addWidget(legend);
    auto button=[&](QStyle::StandardPixmap icon,const char* title,auto action) {
        auto* b=new QToolButton(this); b->setIcon(style()->standardIcon(icon));
        b->setToolTip(title); b->setAccessibleName(title); b->setFixedSize(28,28);
        connect(b,&QToolButton::clicked,this,action);bar->addWidget(b);
    };
    button(QStyle::SP_ArrowUp,"Zoom in",[this]{zoom_=std::min(32.0,zoom_*1.5);update();});
    button(QStyle::SP_ArrowDown,"Zoom out",[this]{zoom_=std::max(1.0,zoom_/1.5);update();});
    button(QStyle::SP_DirHomeIcon,"World view",[this]{center_={};zoom_=1;update();});
}
double InmarsatMapWidget::scale() const { return std::min(width()/360.0,height()/180.0)*zoom_; }
QPointF InmarsatMapWidget::screen(QPointF p) const {return QPointF(width()/2.0,height()/2.0)+(p-center_)*scale();}
void InmarsatMapWidget::setReport(const nlohmann::json& report,bool replay) {
    tracks_.clear();replay_=replay;
    const bool speaking=report.value("voiceActive",false);
    const auto aesId=[](const nlohmann::json& object,const char* key)->uint32_t {
        const auto it=object.find(key);
        if(it==object.end() || !it->is_number_integer())return 0;
        if(it->is_number_unsigned()) {
            const auto value=it->get<uint64_t>();return value<=0xffffff?uint32_t(value):0;
        }
        const auto value=it->get<int64_t>();return value>0 && value<=0xffffff?uint32_t(value):0;
    };
    const uint32_t reported=aesId(report,"voiceAesId");
    const uint32_t active=speaking && reported<=0xffffff?reported:0;
    std::set<uint32_t> activeIds;
    if(report.contains("activeAesIds") && report["activeAesIds"].is_array()) {
        for(const auto& id:report["activeAesIds"]) {
            if(const auto value=aesId(nlohmann::json{{"id",id}},"id"))activeIds.insert(value);
        }
    } else if(active)activeIds.insert(active);
    const auto speaker=aesId(report,"speakerAesId");
    if(report.contains("positions") && report["positions"].is_array()) {
        for(const auto& p:report["positions"]) {
            const double lat=p.value("latDeg",999.0),lon=p.value("lonDeg",999.0);
            const auto aes=aesId(p,"aesId");
            if(!aes || aes>0xffffff || !std::isfinite(lat) || !std::isfinite(lon) || std::abs(lat)>90 || std::abs(lon)>180)continue;
            const auto hex=QString("%1").arg(aes,6,16,QChar('0')).toUpper();
            const auto reg=QString::fromStdString(p.value("registration",std::string{}));
            const auto call=QString::fromStdString(p.value("callsign",std::string{}));
            const auto icao=QString::fromStdString(p.value("icaoHex",std::string{}));
            const auto altitude=p.value("hasAltitude",true) && p.contains("altitudeFt")?
                QString::number(p.value("altitudeFt",0.0),'f',0)+" ft":QString("unavailable");
            QString detail=QString("AES %1\nRegistration: %2\nCallsign: %3\n%4, %5\nAltitude: %6\n%7")
                .arg(hex,reg,call).arg(lat,0,'f',5).arg(lon,0,'f',5).arg(altitude).arg(replay?"Replay":"Live");
            if(p.contains("secondsPastHour"))detail+=QString("\nReport: %1 s past hour").arg(p.value("secondsPastHour",0.0),0,'f',3);
            const bool online=p.value("positionSource",std::string{})=="adsb_lol";
            const bool estimated=p.value("estimated",false);
            detail+=online?"\nPosition: ADSB.lol internet data":"\nPosition: RF ADS-C (CRC valid)";
            if(estimated)detail+=QString("\nESTIMATED +%1 s; not a new measured fix\nAnchor: %2, %3")
                .arg(p.value("estimateSeconds",0.0)).arg(p.value("measuredLatDeg",lat),0,'f',5).arg(p.value("measuredLonDeg",lon),0,'f',5);
            if(activeIds.contains(aes))detail+="\nAircraft-associated received voice activity";
            if(speaker==aes)detail+="\nSelected speaker source (not sample-exact timing)";
            detail+=QString("\nICAO: %1").arg(icao.isEmpty()?"not received":icao);
            const bool stale=p.value("stale",false);
            if(p.contains("ageSeconds")) detail+=QString("\nPosition age %1 s%2").arg(p.value("ageSeconds",0.0),0,'f',0).arg(stale?" (position needs refresh)":"");
            const auto direction=p.find("groundTrackDeg");
            const double track=direction!=p.end() && direction->is_number()?direction->get<double>():-1;
            const bool hasTrack=std::isfinite(track) && track>=0 && track<360;
            detail+=hasTrack?QString("\nGround track: %1 degrees").arg(track,0,'f',1):QString("\nGround track unavailable");
            tracks_.push_back({aes,{lon,-lat},reg.isEmpty()?hex:reg,detail,activeIds.contains(aes),stale,online,estimated,track,hasTrack});
            if(tracks_.size()==512) break; // DEC-0164 bounded RF/identity union.
        }
    }
    auto* status=findChild<QLabel*>("inmarsatMapVoiceStatus");
    QString text;
    if((speaking || report.value("speechActive",false)) && !reported) text=tr("Voice active - aircraft identity unavailable");
    else if(report.contains("tracking") && report["tracking"].value("mapVoiceWithoutIdentity",0)>0)
        text=tr("Received voice activity - one or more aircraft identities unavailable");
    else if(active && std::none_of(tracks_.begin(),tracks_.end(),[&](const auto& t){return t.aes==active;}))
        text=tr("Talking AES %1 - no ADS-C position yet").arg(QString("%1").arg(active,6,16,QChar('0')).toUpper());
    else if(report.contains("tracking") && report["tracking"].value("mapVoiceWithoutPosition",0)>0)
        text=tr("Received call activity - aircraft position unavailable");
    else if(tracks_.empty() && report.contains("tracking") && report["tracking"].value("mapAircraft",0)>0)
        text=tr("Aircraft identified - no current RF or online position available");
    else if(tracks_.empty() && report.value("messages",uint64_t{0})>0)
        text=tr("Messages received - no validated ADS-C position available");
    status->setText(text);status->setVisible(!text.isEmpty());
    update();
}
void InmarsatMapWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.fillRect(rect(),QColor("#15252b"));
    p.save();p.translate(width()/2.0,height()/2.0);p.scale(scale(),scale());p.translate(-center_);
    p.setPen(QPen(QColor("#65766e"),0));p.setBrush(QColor("#3b5148"));p.drawPath(land_);p.restore();
    p.setPen(QColor("#33454b"));
    for(int lon=-180;lon<=180;lon+=30)p.drawLine(screen({double(lon),-90}),screen({double(lon),90}));
    for(int lat=-90;lat<=90;lat+=30)p.drawLine(screen({-180,double(lat)}),screen({180,double(lat)}));
    // DEC-0197: ground track is clockwise from north; unknown motion is a dot.
    const QPolygonF aircraft{QPointF(0,-10),QPointF(3,-2),QPointF(9,2),QPointF(9,4),QPointF(2,2),QPointF(2,7),QPointF(4,9),QPointF(-4,9),QPointF(-2,7),QPointF(-2,2),QPointF(-9,4),QPointF(-9,2),QPointF(-3,-2)};
    for(const auto& t:tracks_) {
        const auto point=screen(t.point);p.save();p.translate(point);
        p.setPen(QPen(t.estimated?QColor("#ffd65c"):QColor(Qt::black),t.estimated?2.5:1.5));
        p.setBrush(t.active?QColor("#52ef88"):t.stale?QColor("#a0a7b0"):t.online?QColor("#60c8ff"):QColor("#f5f7f8"));
        if(t.hasGroundTrack) {p.rotate(t.groundTrack);p.drawPolygon(aircraft);}
        else p.drawEllipse(QPointF(0,0),5,5);
        p.restore();
        p.setPen(Qt::black);p.drawText(point+QPointF(14,5),t.label);
        p.setPen(Qt::white);p.drawText(point+QPointF(13,4),t.label);
    }
    p.setPen(Qt::white);p.drawText(12,23,replay_?"REPLAY | ADS-C":"LIVE | AIRCRAFT");
    p.drawText(12,height()-12,QString("%1 aircraft | Natural Earth | Not for navigation").arg(tracks_.size()));
}
void InmarsatMapWidget::wheelEvent(QWheelEvent* e) {zoom_=std::clamp(zoom_*std::pow(1.5,e->angleDelta().y()/120.0),1.0,32.0);update();e->accept();}
void InmarsatMapWidget::mousePressEvent(QMouseEvent* e) {
    if(e->button()==Qt::LeftButton){dragging_=true;dragStart_=e->position();dragCenter_=center_;}
}
void InmarsatMapWidget::mouseMoveEvent(QMouseEvent* e) {
    if(dragging_){center_=dragCenter_-(e->position()-dragStart_)/scale();center_.setX(std::clamp(center_.x(),-180.0,180.0));center_.setY(std::clamp(center_.y(),-90.0,90.0));update();return;}
    for(const auto& t:tracks_)if(QLineF(screen(t.point),e->position()).length()<16){QToolTip::showText(e->globalPosition().toPoint(),"<qt>"+t.details.toHtmlEscaped().replace('\n',"<br>")+"</qt>",this);return;}
    QToolTip::hideText();
}
void InmarsatMapWidget::mouseReleaseEvent(QMouseEvent* e) {if(e->button()==Qt::LeftButton)dragging_=false;}
