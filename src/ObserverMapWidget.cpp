#include "ObserverMapWidget.h"

#include <QLabel>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QStringList>
#include <QUrl>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace {
constexpr int kTile = 256;
constexpr double kPi = 3.14159265358979323846;

void latLonToTileXY(double lat, double lon, int z, double* x, double* y) {
    const double n = std::pow(2.0, z);
    *x = (lon + 180.0) / 360.0 * n;
    const double latRad = lat * kPi / 180.0;
    const double s = std::sin(latRad);
    const double clamped = std::min(0.9999, std::max(-0.9999, s));
    *y = (1.0 - std::log((1.0 + clamped) / (1.0 - clamped)) / (2.0 * kPi)) / 2.0 * n;
}

double mercatorYUnit(double lat) {
    double x = 0.0, y = 0.0;
    latLonToTileXY(std::clamp(lat, -85.0, 85.0), 0.0, 0, &x, &y);
    return y;
}

double latitudeFromMercatorY(double y) {
    const double latRad = std::atan(std::sinh(kPi * (1.0 - 2.0 * y)));
    return std::clamp(latRad * 180.0 / kPi, -85.0, 85.0);
}

double wrappedUnitDelta(double value, double center) {
    double d = value - center;
    while (d > 0.5) d -= 1.0;
    while (d < -0.5) d += 1.0;
    return d;
}

} // namespace

ObserverMapWidget::ObserverMapWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(280, 220);
    setCursor(Qt::CrossCursor);
    nam_ = new QNetworkAccessManager(this);
    hint_ = new QLabel("Click map to set home. Selected satellites appear when TLE data is loaded.", this);
    hint_->setStyleSheet("background:rgba(0,0,0,180);color:#9dff9d;padding:4px;");
}

void ObserverMapWidget::setMarker(double latDeg, double lonDeg) {
    markerLat_ = std::clamp(latDeg, -85.0, 85.0);
    markerLon_ = std::clamp(lonDeg, -180.0, 180.0);
    hasMarker_ = true;
    if (satellites_.empty()) {
        centerLat_ = markerLat_;
        centerLon_ = markerLon_;
    } else {
        fitSatelliteOverview();
    }
    ensureTiles();
    update();
}

void ObserverMapWidget::setSatellites(const std::vector<SatelliteMapMarker>& satellites) {
    QStringList ids;
    ids.reserve(static_cast<qsizetype>(satellites.size()));
    int valid = 0;
    for (const auto& sat : satellites) {
        ids.push_back(sat.id);
        if (sat.valid) ++valid;
    }
    ids.sort(Qt::CaseInsensitive);
    const QString signature = ids.join('|');
    const bool selectionChanged = signature != satelliteSignature_;
    satelliteSignature_ = signature;
    satellites_ = satellites;

    const int missing = static_cast<int>(satellites_.size()) - valid;
    hint_->setText(QString("%1 selected • green=in range • yellow=armed%2")
                       .arg(satellites_.size())
                       .arg(missing > 0 ? QString(" • %1 missing TLE").arg(missing) : QString()));

    if (selectionChanged || !satelliteMarkersVisible()) fitSatelliteOverview();
    ensureTiles();
    update();
}

void ObserverMapWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    hint_->setGeometry(6, 6, std::max(10, width() - 12), 24);
    if (!satelliteMarkersVisible()) fitSatelliteOverview();
    ensureTiles();
}

QPointF ObserverMapWidget::latLonToPixel(double lat, double lon) const {
    double cx, cy, x, y;
    latLonToTileXY(centerLat_, centerLon_, zoom_, &cx, &cy);
    latLonToTileXY(lat, lon, zoom_, &x, &y);
    const double n = std::pow(2.0, zoom_);
    double dx = x - cx;
    if (dx > n * 0.5) dx -= n;
    if (dx < -n * 0.5) dx += n;
    return QPointF(width() * 0.5 + dx * kTile, height() * 0.5 + (y - cy) * kTile);
}

bool ObserverMapWidget::pixelToLatLon(const QPointF& pt, double* lat, double* lon) const {
    double cx, cy;
    latLonToTileXY(centerLat_, centerLon_, zoom_, &cx, &cy);
    const double x = cx + (pt.x() - width() * 0.5) / kTile;
    const double y = cy + (pt.y() - height() * 0.5) / kTile;
    const double n = std::pow(2.0, zoom_);
    *lon = x / n * 360.0 - 180.0;
    const double latRad = std::atan(std::sinh(kPi * (1.0 - 2.0 * y / n)));
    *lat = latRad * 180.0 / kPi;
    while (*lon > 180.0) *lon -= 360.0;
    while (*lon < -180.0) *lon += 360.0;
    return std::isfinite(*lat) && std::isfinite(*lon);
}

bool ObserverMapWidget::satelliteMarkersVisible() const {
    if (width() < 64 || height() < 64) return false;
    const QRectF safe = QRectF(rect()).adjusted(28.0, 32.0, -28.0, -28.0);
    bool haveValid = false;
    for (const auto& sat : satellites_) {
        if (!sat.valid) continue;
        haveValid = true;
        if (!safe.contains(latLonToPixel(sat.latDeg, sat.lonDeg))) return false;
    }
    if (hasMarker_ && haveValid && !safe.contains(latLonToPixel(markerLat_, markerLon_))) return false;
    return true;
}

void ObserverMapWidget::fitSatelliteOverview() {
    std::vector<double> xs;
    std::vector<double> ys;
    xs.reserve(satellites_.size() + 1);
    ys.reserve(satellites_.size() + 1);

    auto addPoint = [&](double lat, double lon) {
        double x = std::fmod((lon + 180.0) / 360.0, 1.0);
        if (x < 0.0) x += 1.0;
        xs.push_back(x);
        ys.push_back(mercatorYUnit(lat));
    };

    if (hasMarker_) addPoint(markerLat_, markerLon_);
    for (const auto& sat : satellites_) {
        if (sat.valid) addPoint(sat.latDeg, sat.lonDeg);
    }

    if (xs.empty()) return;
    if (xs.size() == 1) {
        centerLon_ = xs.front() * 360.0 - 180.0;
        centerLat_ = latitudeFromMercatorY(ys.front());
        zoom_ = 6;
        return;
    }

    std::sort(xs.begin(), xs.end());
    double largestGap = -1.0;
    size_t largestGapIndex = 0;
    for (size_t i = 0; i < xs.size(); ++i) {
        const double next = i + 1 < xs.size() ? xs[i + 1] : xs.front() + 1.0;
        const double gap = next - xs[i];
        if (gap > largestGap) {
            largestGap = gap;
            largestGapIndex = i;
        }
    }
    const size_t startIndex = (largestGapIndex + 1) % xs.size();
    double start = xs[startIndex];
    if (startIndex == 0 && largestGapIndex == xs.size() - 1) start += 1.0;
    const double span = std::max(0.0, 1.0 - largestGap);
    double centerX = std::fmod(start + span * 0.5, 1.0);
    if (centerX < 0.0) centerX += 1.0;

    const auto [minYIt, maxYIt] = std::minmax_element(ys.begin(), ys.end());
    const double centerY = (*minYIt + *maxYIt) * 0.5;
    centerLon_ = centerX * 360.0 - 180.0;
    centerLat_ = latitudeFromMercatorY(centerY);

    const double availableW = std::max(64.0, width() - 72.0);
    const double availableH = std::max(64.0, height() - 72.0);
    int bestZoom = 1;
    for (int z = 10; z >= 1; --z) {
        const double scale = std::pow(2.0, z) * kTile;
        double maxDx = 0.0;
        double maxDy = 0.0;
        for (size_t i = 0; i < xs.size(); ++i) {
            maxDx = std::max(maxDx, std::abs(wrappedUnitDelta(xs[i], centerX)) * scale);
            maxDy = std::max(maxDy, std::abs(ys[i] - centerY) * scale);
        }
        if (maxDx * 2.0 <= availableW && maxDy * 2.0 <= availableH) {
            bestZoom = z;
            break;
        }
    }
    zoom_ = bestZoom;
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
            req.setHeader(QNetworkRequest::UserAgentHeader, "SDR-Town-SatMap/0.2.81");
            QNetworkReply* reply = nam_->get(req);
            connect(reply, &QNetworkReply::finished, this, [this, reply, key]() {
                if (reply->error() == QNetworkReply::NoError) {
                    QPixmap pm;
                    pm.loadFromData(reply->readAll());
                    tiles_[key] = pm;
                    update();
                } else {
                    tiles_.remove(key); // allow a later retry
                }
                reply->deleteLater();
                ensureTiles();
            });
        }
    }
}

void ObserverMapWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), QColor(8, 16, 8));
    double cx, cy;
    latLonToTileXY(centerLat_, centerLon_, zoom_, &cx, &cy);
    const double n = std::pow(2.0, zoom_);
    for (auto it = tiles_.begin(); it != tiles_.end(); ++it) {
        if (it->isNull()) continue;
        const QStringList parts = it.key().split('/');
        if (parts.size() != 3) continue;
        const int z = parts[0].toInt();
        if (z != zoom_) continue;
        const int tx = parts[1].toInt();
        const int ty = parts[2].toInt();
        double dx = (tx + 0.5) - cx;
        if (dx > n * 0.5) dx -= n;
        if (dx < -n * 0.5) dx += n;
        const double px = width() * 0.5 + dx * kTile - kTile / 2.0;
        const double py = height() * 0.5 + (ty + 0.5 - cy) * kTile - kTile / 2.0;
        p.drawPixmap(QRectF(px, py, kTile, kTile).toRect(), *it);
    }

    const QPointF home = hasMarker_ ? latLonToPixel(markerLat_, markerLon_) : QPointF();
    if (hasMarker_) {
        p.setPen(QPen(QColor(57, 255, 20, 105), 1, Qt::DashLine));
        for (const auto& sat : satellites_) {
            if (!sat.valid || !sat.inRange) continue;
            p.drawLine(home, latLonToPixel(sat.latDeg, sat.lonDeg));
        }
    }

    if (hasMarker_) {
        p.setPen(QPen(QColor(57, 255, 20), 2));
        p.setBrush(QColor(57, 255, 20, 180));
        p.drawEllipse(home, 7, 7);
        p.drawLine(QPointF(home.x(), home.y() - 12), QPointF(home.x(), home.y() + 12));
        p.drawLine(QPointF(home.x() - 12, home.y()), QPointF(home.x() + 12, home.y()));
        p.drawText(home + QPointF(10, 18), "HOME");
    }

    for (const auto& sat : satellites_) {
        if (!sat.valid) continue;
        const QPointF pt = latLonToPixel(sat.latDeg, sat.lonDeg);
        QColor colour(255, 145, 40);
        if (sat.inRange) colour = QColor(57, 255, 20);
        if (sat.armed) colour = QColor(255, 235, 59);

        p.setPen(QPen(QColor(0, 0, 0, 210), 4));
        p.setBrush(QColor(0, 0, 0, 170));
        p.drawEllipse(pt, sat.armed ? 9 : 7, sat.armed ? 9 : 7);
        p.setPen(QPen(colour, sat.armed ? 3 : 2));
        p.setBrush(QColor(colour.red(), colour.green(), colour.blue(), 205));
        p.drawEllipse(pt, sat.armed ? 7 : 5, sat.armed ? 7 : 5);

        const QString label = QString("%1  el %2°  alt %3 km")
                                  .arg(sat.name)
                                  .arg(sat.elevationDeg, 0, 'f', 1)
                                  .arg(sat.altitudeKm, 0, 'f', 0);
        const QPointF textPoint = pt + QPointF(10, -9);
        p.setPen(QPen(QColor(0, 0, 0), 4));
        p.drawText(textPoint, label);
        p.setPen(QPen(colour, 1));
        p.drawText(textPoint, label);
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
    else if (event->angleDelta().y() < 0 && zoom_ > 1) --zoom_;
    ensureTiles();
    update();
}
