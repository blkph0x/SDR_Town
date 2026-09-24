#include "InmarsatWatchSpectrum.h"
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>

InmarsatWatchSpectrum::InmarsatWatchSpectrum(QWidget* parent):QWidget(parent) {
    setMinimumHeight(180);setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);
    setObjectName("inmarsatWatchSpectrum");
    setToolTip("Select a signal, choose its decoder rate, then add it to the watch list.");
}
QRectF InmarsatWatchSpectrum::plot() const {return QRectF(8,8,std::max(1,width()-16),std::max(1,height()-36));}
void InmarsatWatchSpectrum::setSpectrum(const std::vector<float>& bins,double center,double rate,
        const std::vector<InmarsatWatchChannel>& channels) {
    channels_=channels;
    if(center!=center_ || rate!=rate_ || bins.size()!=bins_.size()) waterfall_=QImage();
    bins_=bins;center_=center;rate_=rate;
    if(!bins.empty() && std::isfinite(rate) && rate>0) {
        if(waterfall_.isNull()){waterfall_=QImage(int(bins.size()),96,QImage::Format_RGB32);waterfall_.fill(Qt::black);}
        for(int y=waterfall_.height()-1;y>0;--y)
            std::copy_n(reinterpret_cast<const QRgb*>(waterfall_.constScanLine(y-1)),waterfall_.width(),reinterpret_cast<QRgb*>(waterfall_.scanLine(y)));
        for(int x=0;x<waterfall_.width();++x) {
            const float p=std::isfinite(bins[x])?bins[x]:-120;
            const double v=std::clamp((p+120)/100.0,0.0,1.0);
            waterfall_.setPixel(x,0,QColor::fromHsvF((1-v)*0.66,0.85,0.12+0.88*v).rgb());
        }
    }
    update();
}
void InmarsatWatchSpectrum::paintEvent(QPaintEvent*) {
    QPainter p(this);p.fillRect(rect(),QColor(16,20,22));
    if(bins_.empty() || rate_<=0){p.setPen(Qt::lightGray);p.drawText(rect(),Qt::AlignCenter,"No live spectrum");return;}
    const auto area=plot();const double half=area.height()*0.5;
    p.drawImage(QRectF(area.left(),area.top()+half,area.width(),half),waterfall_);
    QPainterPath path;
    for(size_t i=0;i<bins_.size();++i) {
        const double x=area.left()+area.width()*double(i)/bins_.size();
        const double v=std::isfinite(bins_[i])?std::clamp((bins_[i]+120)/100.0,0.0,1.0):0;
        const double y=area.top()+half*(1-v);
        if(i)path.lineTo(x,y);else path.moveTo(x,y);
    }
    p.setPen(QColor(92,204,245));p.drawPath(path);
    for(const auto& c:channels_) if(c.enabled) {
        const double x=area.left()+area.width()*(0.5+(c.frequencyHz-center_)/rate_);
        if(x<area.left() || x>area.right())continue;
        p.setPen(c.voice()?QColor(255,196,70):QColor(100,235,158));
        p.drawLine(QPointF(x,area.top()),QPointF(x,area.bottom()));
    }
    p.setPen(Qt::lightGray);
    for(int i=0;i<3;++i) {
        const auto text=QString::number((center_+rate_*(i/2.0-0.5))/1e6,'f',4)+" MHz";
        p.drawText(QRectF(area.left()+area.width()*i/3,area.bottom()+3,area.width()/3,24),
            i==0?Qt::AlignLeft:i==2?Qt::AlignRight:Qt::AlignHCenter,text);
    }
}
void InmarsatWatchSpectrum::mouseReleaseEvent(QMouseEvent* event) {
    if(event->button()!=Qt::LeftButton || bins_.empty() || !plot().contains(event->position()) || rate_<=0)return;
    emit frequencySelected(center_+((event->position().x()-plot().left())/plot().width()-0.5)*rate_);
}
