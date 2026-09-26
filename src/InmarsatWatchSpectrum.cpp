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
    const bool changed=bins!=bins_ || center!=center_ || rate!=rate_;
    if(center!=center_ || rate!=rate_ || bins.size()!=bins_.size()) {waterfall_=QImage();head_=0;rows_=0;}
    bins_=bins;center_=center;rate_=rate;
    if(changed && !bins.empty() && std::isfinite(rate) && rate>0) {
        if(waterfall_.isNull()){waterfall_=QImage(int(bins.size()),96,QImage::Format_RGB32);waterfall_.fill(Qt::black);}
        // DEC-0127: one new row, no full-image scroll and no repeated FFT rows.
        head_=(head_+waterfall_.height()-1)%waterfall_.height();++rows_;
        auto* row=reinterpret_cast<QRgb*>(waterfall_.scanLine(head_));
        for(int x=0;x<waterfall_.width();++x) {
            const float p=std::isfinite(bins[x])?bins[x]:-120;
            const double v=std::clamp((p+120)/100.0,0.0,1.0);
            row[x]=QColor::fromHsvF((1-v)*0.66,0.85,0.12+0.88*v).rgb();
        }
    }
    update();
}
void InmarsatWatchSpectrum::paintEvent(QPaintEvent*) {
    QPainter p(this);p.fillRect(rect(),QColor(16,20,22));
    if(bins_.empty() || rate_<=0){p.setPen(Qt::lightGray);p.drawText(rect(),Qt::AlignCenter,"No live spectrum");return;}
    const auto area=plot();const double half=area.height()*0.5;
    if(!waterfall_.isNull()) {
        const int tail=waterfall_.height()-head_;
        const double h=half*tail/waterfall_.height();
        p.drawImage(QRectF(area.left(),area.top()+half,area.width(),h),waterfall_,
                    QRectF(0,head_,waterfall_.width(),tail));
        if(head_)p.drawImage(QRectF(area.left(),area.top()+half+h,area.width(),half-h),waterfall_,
                            QRectF(0,0,waterfall_.width(),head_));
    }
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

InmarsatConstellationWidget::InmarsatConstellationWidget(QWidget* parent):QWidget(parent) {
    setObjectName("inmarsatConstellation");setMinimumSize(170,180);
    setMaximumWidth(270);setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Expanding);
    setToolTip("Recovered symbols for the selected channel. Protocol lock requires validated frames, not four visible clusters.");
}
void InmarsatConstellationWidget::setChannel(const InmarsatChannelDisplay* channel) {
    points_.clear();locked_=false;ebno_=0;channel_.clear();status_="Not in active group";
    if(channel) {
        points_=channel->constellation.points;locked_=channel->locked;ebno_=channel->ebnoDb;
        status_=points_.empty()?"No fresh symbols":locked_?"Protocol lock":"Acquiring";
        channel_=QString("%1 MHz | %2 bit/s%3").arg(channel->frequencyHz/1e6,0,'f',6)
            .arg(std::abs(channel->rate)).arg(channel->rate<0?" burst":"");
        if(channel->rate==0) {
            points_.clear();locked_=false;
            status_="No native EGC constellation";
            channel_=QString("%1 MHz | EGC").arg(channel->frequencyHz/1e6,0,'f',6);
        }
    }
    setToolTip(channel_.isEmpty()?status_:channel_+"\n"+status_);
    update();
}
void InmarsatConstellationWidget::setChannels(const std::vector<InmarsatChannelDisplay>& channels,
                                             const std::string& selectedId) {
    // DEC-0134: an inactive selected watch channel must not borrow a peer's dots.
    const auto found=std::find_if(channels.begin(),channels.end(),[&](const auto& c){
        return selectedId.empty() || c.id==selectedId;
    });
    setChannel(found==channels.end()?nullptr:&*found);
    if(found==channels.end() && selectedId.empty()) {
        status_="No active decoder";
        setToolTip(status_);
    }
}
void InmarsatConstellationWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);p.fillRect(rect(),QColor(16,20,22));
    const double size=std::max(1,std::min(width()-20,height()-68));
    const QRectF box((width()-size)/2,8,size,size);
    p.setPen(QColor(71,84,89));p.drawRect(box);
    p.drawLine(QPointF(box.center().x(),box.top()),QPointF(box.center().x(),box.bottom()));
    p.drawLine(QPointF(box.left(),box.center().y()),QPointF(box.right(),box.center().y()));
    p.save();p.setClipRect(box);
    p.setPen(QPen(locked_?QColor(100,235,158):QColor(255,196,70),2));
    // The modem AGC normalizes symbols; fixed +/-2 axes do not hide loss of lock
    // by continually rescaling noise into apparently tight clusters.
    for(auto v:points_)if(std::isfinite(v.real()) && std::isfinite(v.imag()))
        p.drawPoint(QPointF(box.center().x()+v.real()*size/4,box.center().y()-v.imag()*size/4));
    p.restore();p.setPen(Qt::lightGray);
    p.drawText(QRectF(0,box.bottom()+5,width(),18),Qt::AlignCenter,status_);
    p.drawText(QRectF(4,box.bottom()+25,width()-8,18),Qt::AlignCenter,
        p.fontMetrics().elidedText(channel_,Qt::ElideRight,std::max(1,width()-8)));
}
