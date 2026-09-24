#pragma once
#include <QWidget>
#include <QPainterPath>
#include <nlohmann/json.hpp>

// Session-local ADS-C map. Replays never contaminate the live aircraft store.
class InmarsatMapWidget : public QWidget {
    Q_OBJECT
public:
    explicit InmarsatMapWidget(QWidget* parent=nullptr);
    void setReport(const nlohmann::json& report, bool replay);
    size_t aircraftCount() const { return tracks_.size(); }
protected:
    void paintEvent(QPaintEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
private:
    struct Track { uint32_t aes; QPointF point; QString label, details; bool active, stale; };
    QPainterPath land_;
    std::vector<Track> tracks_;
    QPointF center_{0,0}, dragStart_, dragCenter_;
    bool dragging_=false, replay_=false;
    double zoom_=1;
    double scale() const;
    QPointF screen(QPointF location) const;
};
