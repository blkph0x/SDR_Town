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
signals:
    void frequencySelected(double hz);
protected:
    void paintEvent(QPaintEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
private:
    QRectF plot() const;
    std::vector<float> bins_;
    std::vector<InmarsatWatchChannel> channels_;
    double center_=0,rate_=0;
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
