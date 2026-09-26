#pragma once
#include "InmarsatWatch.h"
#include <QWidget>
#include <QImage>

// Live RF only; unlike the main demo-capable spectrum, never fabricates input.
class InmarsatWatchSpectrum : public QWidget {
    Q_OBJECT
public:
    explicit InmarsatWatchSpectrum(QWidget* parent=nullptr);
    void setSpectrum(const std::vector<float>&,double center,double rate,
                     const std::vector<InmarsatWatchChannel>&);
    uint64_t waterfallRows() const {return rows_;}
    double visibleCenterHz() const {return center_+(viewStart_+viewSpan_/2-0.5)*rate_;}
    double visibleSpanHz() const {return viewSpan_*rate_;}
signals:
    void frequencySelected(double hz);
protected:
    void paintEvent(QPaintEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
private:
    QRectF plot() const;
    std::vector<float> bins_;
    std::vector<InmarsatWatchChannel> channels_;
    double center_=0,rate_=0;
    double viewStart_=0,viewSpan_=1,dragStart_=0;
    QPointF press_;
    bool pressed_=false,dragged_=false;
    QImage waterfall_;
    int head_=0;
    uint64_t rows_=0;
};

class InmarsatConstellationWidget : public QWidget {
    Q_OBJECT
public:
    explicit InmarsatConstellationWidget(QWidget* parent=nullptr);
    void setChannel(const InmarsatChannelDisplay* channel);
    void setChannels(const std::vector<InmarsatChannelDisplay>& channels, const std::string& selectedId);
    size_t pointCount() const {return points_.size();}
    QString statusText() const {return status_;}
    QString channelText() const {return channel_;}
protected:
    void paintEvent(QPaintEvent*) override;
private:
    std::vector<std::complex<float>> points_;
    bool locked_=false;
    double ebno_=0;
    QString status_="No active decoder";
    QString channel_;
};
