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
};
