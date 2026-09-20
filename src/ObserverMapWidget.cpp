#include "ObserverMapWidget.h"

#include <QLabel>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QUrl>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace {
constexpr int kTile = 256;

void latLonToTileXY(double lat, double lon, int z, double* x, double* y) {
    const double n = std::pow(2.0, z);
    *x = (lon + 180.0) / 360.0 * n;
    const double latRad = lat * 3.14159265358979323846 / 180.0;
    const double s = std::sin(latRad);
    const double clamped = std::min(0.9999, std::max(-0.9999, s));
    *y = (1.0 - std::log((1.0 + clamped) / (1.0 - clamped)) / (2.0 * 3.14159265358979323846)) / 2.0 * n;
}
} // namespace

ObserverMapWidget::ObserverMapWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(280, 180);
    setCursor(Qt::CrossCursor);
    nam_ = new QNetworkAccessManager(this);
    hint_ = new QLabel("Click map to set home lat/lon (ISS / Doppler / passes)", this);
    hint_->setStyleSheet("background:rgba(0,0,0,160);color:#9dff9d;padding:4px;");
}

void ObserverMapWidget::setMarker(double latDeg, double lonDeg) {
    markerLat_ = std::clamp(latDeg, -85.0, 85.0);
    markerLon_ = std::clamp(lonDeg, -180.0, 180.0);
    centerLat_ = markerLat_;
    centerLon_ = markerLon_;
    hasMarker_ = true;
    ensureTiles();
    update();
}

void ObserverMapWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    hint_->setGeometry(6, 6, width() - 12, 22);
    ensureTiles();
}

QPointF ObserverMapWidget::latLonToPixel(double lat, double lon) const {
    double cx, cy, x, y;
    latLonToTileXY(centerLat_, centerLon_, zoom_, &cx, &cy);
    latLonToTileXY(lat, lon, zoom_, &x, &y);
    return QPointF(width() * 0.5 + (x - cx) * kTile, height() * 0.5 + (y - cy) * kTile);
}

bool ObserverMapWidget::pixelToLatLon(const QPointF& pt, double* lat, double* lon) const {
    double cx, cy;
    latLonToTileXY(centerLat_, centerLon_, zoom_, &cx, &cy);
    const double x = cx + (pt.x() - width() * 0.5) / kTile;
    const double y = cy + (pt.y() - height() * 0.5) / kTile;
    const double n = std::pow(2.0, zoom_);
    *lon = x / n * 360.0 - 180.0;
    const double latRad = std::atan(std::sinh(3.14159265358979323846 * (1.0 - 2.0 * y / n)));
    *lat = latRad * 180.0 / 3.14159265358979323846;
    if (*lon > 180.0) *lon -= 360.0;
    if (*lon < -180.0) *lon += 360.0;
    return std::isfinite(*lat) && std::isfinite(*lon);
}

void ObserverMapWidget::ensureTiles() {
    if (width() < 32 || height() < 32) return;
    double cx, cy;
    latLonToTileXY(centerLat_, centerLon_, zoom_, &cx, &cy);
    const int n = 1 << zoom_;
    const int tx0 = int(std::floor(cx - width() / double(kTile) / 2.0)) - 1;
    const int ty0 = int(std::floor(cy - height() / double(kTile) / 2.0)) - 1;
    const int tx1 = tx0 + int(width() / kTile) + 3;
    const int ty1 = ty0 + int(height() / kTile) + 3;
    int pending = 0;
    for (int ty = ty0; ty <= ty1; ++ty) {
        for (int tx = tx0; tx <= tx1; ++tx) {
            const int txx = ((tx % n) + n) % n;
            if (ty < 0 || ty >= n) continue;
            const QString key = QString("%1/%2/%3").arg(zoom_).arg(txx).arg(ty);
            if (tiles_.contains(key)) continue;
            if (++pending > 8) return;
            tiles_.insert(key, QPixmap());
            const QUrl url(QString("https://tile.openstreetmap.org/%1/%2/%3.png").arg(zoom_).arg(txx).arg(ty));
            QNetworkRequest req(url);
            req.setHeader(QNetworkRequest::UserAgentHeader, "SDR-Town-ObserverMap/0.2.74");
            QNetworkReply* reply = nam_->get(req);
            connect(reply, &QNetworkReply::finished, this, [this, reply, key]() {
                if (reply->error() == QNetworkReply::NoError) {
                    QPixmap pm;
                    pm.loadFromData(reply->readAll());
                    tiles_[key] = pm;
                    update();
                }
                reply->deleteLater();
            });
        }
    }
}

void ObserverMapWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(8, 16, 8));
    double cx, cy;
    latLonToTileXY(centerLat_, centerLon_, zoom_, &cx, &cy);
    for (auto it = tiles_.begin(); it != tiles_.end(); ++it) {
        if (it->isNull()) continue;
        const QStringList parts = it.key().split('/');
        if (parts.size() != 3) continue;
        const int z = parts[0].toInt();
        if (z != zoom_) continue;
        const int tx = parts[1].toInt();
        const int ty = parts[2].toInt();
        const double px = width() * 0.5 + (tx + 0.5 - cx) * kTile - kTile / 2.0;
        const double py = height() * 0.5 + (ty + 0.5 - cy) * kTile - kTile / 2.0;
        p.drawPixmap(QRectF(px, py, kTile, kTile).toRect(), *it);
    }
    if (hasMarker_) {
        const QPointF m = latLonToPixel(markerLat_, markerLon_);
        p.setPen(QPen(QColor(57, 255, 20), 2));
        p.setBrush(QColor(57, 255, 20, 180));
        p.drawEllipse(m, 7, 7);
        p.drawLine(QPointF(m.x(), 0), QPointF(m.x(), height()));
        p.drawLine(QPointF(0, m.y()), QPointF(width(), m.y()));
    }
}

void ObserverMapWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::MiddleButton ||
        (event->button() == Qt::LeftButton && event->modifiers() & Qt::ControlModifier)) {
        double lat = 0, lon = 0;
        if (pixelToLatLon(event->position(), &lat, &lon)) {
            centerLat_ = lat;
            centerLon_ = lon;
            ensureTiles();
            update();
        }
        return;
    }
    if (event->button() != Qt::LeftButton) return;
    double lat = 0, lon = 0;
    if (!pixelToLatLon(event->position(), &lat, &lon)) return;
    setMarker(lat, lon);
    emit locationPicked(lat, lon);
}

void ObserverMapWidget::wheelEvent(QWheelEvent* event) {
    if (event->angleDelta().y() > 0 && zoom_ < 16) ++zoom_;
    else if (event->angleDelta().y() < 0 && zoom_ > 2) --zoom_;
    ensureTiles();
    update();
}
