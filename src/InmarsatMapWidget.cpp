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
#include <QStyle>
#include <algorithm>
#include <cmath>

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
    auto* bar=new QHBoxLayout(this); bar->setAlignment(Qt::AlignTop|Qt::AlignRight);
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
    const uint32_t active=report.value("voiceActive",false)?report.value("voiceAesId",uint32_t{0}):0;
    if(report.contains("positions") && report["positions"].is_array()) {
        for(const auto& p:report["positions"]) {
            const double lat=p.value("latDeg",999.0),lon=p.value("lonDeg",999.0);
            const auto aes=p.value("aesId",uint32_t{0});
            if(!aes || !std::isfinite(lat) || !std::isfinite(lon) || std::abs(lat)>90 || std::abs(lon)>180)continue;
            const auto hex=QString("%1").arg(aes,6,16,QChar('0')).toUpper();
            const auto reg=QString::fromStdString(p.value("registration",std::string{}));
            const auto call=QString::fromStdString(p.value("callsign",std::string{}));
            QString detail=QString("AES %1\nRegistration: %2\nCallsign: %3\n%4, %5\nAltitude: %6 ft\nReport: %7 s past hour\n%8 ADS-C (CRC valid)")
                .arg(hex,reg,call).arg(lat,0,'f',5).arg(lon,0,'f',5).arg(p.value("altitudeFt",0.0),0,'f',0)
                .arg(p.value("secondsPastHour",0.0),0,'f',3).arg(replay?"Replay":"Live");
            tracks_.push_back({aes,{lon,-lat},reg.isEmpty()?hex:reg,detail,aes==active});
            if(tracks_.size()==256) break;
        }
    }
    update();
}
void InmarsatMapWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.fillRect(rect(),QColor("#15252b"));
    p.save();p.translate(width()/2.0,height()/2.0);p.scale(scale(),scale());p.translate(-center_);
    p.setPen(QPen(QColor("#65766e"),0));p.setBrush(QColor("#3b5148"));p.drawPath(land_);p.restore();
    p.setPen(QColor("#33454b"));
    for(int lon=-180;lon<=180;lon+=30)p.drawLine(screen({double(lon),-90}),screen({double(lon),90}));
    for(int lat=-90;lat<=90;lat+=30)p.drawLine(screen({-180,double(lat)}),screen({180,double(lat)}));
    // No heading is inferred: the upright aircraft is an identity marker only.
    const QPolygonF aircraft{QPointF(0,-10),QPointF(3,-2),QPointF(9,2),QPointF(9,4),QPointF(2,2),QPointF(2,7),QPointF(4,9),QPointF(-4,9),QPointF(-2,7),QPointF(-2,2),QPointF(-9,4),QPointF(-9,2),QPointF(-3,-2)};
    for(const auto& t:tracks_) {
        const auto point=screen(t.point);p.save();p.translate(point);
        p.setPen(QPen(Qt::black,1.5));p.setBrush(t.active?QColor("#52ef88"):QColor("#f5f7f8"));p.drawPolygon(aircraft);p.restore();
        p.setPen(Qt::black);p.drawText(point+QPointF(14,5),t.label);
        p.setPen(Qt::white);p.drawText(point+QPointF(13,4),t.label);
    }
    p.setPen(Qt::white);p.drawText(12,23,replay_?"REPLAY | ADS-C":"LIVE | ADS-C");
    p.drawText(12,height()-12,QString("%1 aircraft | Green: decoded voice | Natural Earth").arg(tracks_.size()));
}
void InmarsatMapWidget::wheelEvent(QWheelEvent* e) {zoom_=std::clamp(zoom_*std::pow(1.5,e->angleDelta().y()/120.0),1.0,32.0);update();e->accept();}
void InmarsatMapWidget::mousePressEvent(QMouseEvent* e) {
    if(e->button()==Qt::LeftButton){dragging_=true;dragStart_=e->position();dragCenter_=center_;}
}
void InmarsatMapWidget::mouseMoveEvent(QMouseEvent* e) {
    if(dragging_){center_=dragCenter_-(e->position()-dragStart_)/scale();center_.setX(std::clamp(center_.x(),-180.0,180.0));center_.setY(std::clamp(center_.y(),-90.0,90.0));update();return;}
    for(const auto& t:tracks_)if(QLineF(screen(t.point),e->position()).length()<16){QToolTip::showText(e->globalPosition().toPoint(),t.details,this);return;}
    QToolTip::hideText();
}
void InmarsatMapWidget::mouseReleaseEvent(QMouseEvent* e) {if(e->button()==Qt::LeftButton)dragging_=false;}
