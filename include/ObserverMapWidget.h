#pragma once

#include <QHash>
#include <QPixmap>
#include <QPointF>
#include <QWidget>

class QNetworkAccessManager;
class QLabel;

// Click-to-pick OSM map for the satcom/ISS home observer (both hemispheres).
class ObserverMapWidget : public QWidget {
    Q_OBJECT
public:
    explicit ObserverMapWidget(QWidget* parent = nullptr);

    void setMarker(double latDeg, double lonDeg);
    double markerLat() const { return markerLat_; }
    double markerLon() const { return markerLon_; }

signals:
    void locationPicked(double latDeg, double lonDeg);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    void ensureTiles();
    QPointF latLonToPixel(double lat, double lon) const;
    bool pixelToLatLon(const QPointF& pt, double* lat, double* lon) const;

    double centerLat_ = -33.87;
    double centerLon_ = 151.21;
    double markerLat_ = -33.87;
    double markerLon_ = 151.21;
    bool hasMarker_ = false;
    int zoom_ = 6;
    QNetworkAccessManager* nam_ = nullptr;
    QHash<QString, QPixmap> tiles_;
    QLabel* hint_ = nullptr;
};
