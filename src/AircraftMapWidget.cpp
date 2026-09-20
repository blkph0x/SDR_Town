#include "AircraftMapWidget.h"
#include "AdsBTrackStore.h"
#include "DeviceManager.h"
#include "SatPassPlanner.h"

#include <QCheckBox>
#include <QDialog>
#include <QHideEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineF>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPushButton>
#include <QShowEvent>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtMath>

#include <chrono>
#include <cmath>
#include <vector>

namespace {

constexpr int kTile = 256;

void latLonToTileXY(double lat, double lon, int z, double* x, double* y) {
    const double n = std::pow(2.0, z);
    *x = (lon + 180.0) / 360.0 * n;
    const double latRad = lat * 3.14159265358979323846 / 180.0;
    *y = (1.0 - std::log(std::tan(latRad) + 1.0 / std::cos(latRad)) / M_PI) / 2.0 * n;
}

} // namespace

AircraftMapWidget::AircraftMapWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(480, 360);
    setWindowTitle("Aircraft Map");

    status_ = new QLabel("Aircraft map — OpenSky (local 1090 off)", this);
    tuneBtn_ = new QPushButton("Tune 1090", this);
    netBtn_ = new QPushButton("Refresh net", this);
    localAdsbCheck_ = new QCheckBox("Local 1090 decode", this);
    localAdsbCheck_->setChecked(false);
    localAdsbCheck_->setToolTip("Off by default. Enable only when tuned to 1090; runs a light background worker so the main demod stays responsive.");
    status_->setStyleSheet("background:rgba(0,0,0,180);color:#9dff9d;padding:6px;");
    tuneBtn_->setStyleSheet("background:#0c160c;color:#39FF14;border:1px solid #39FF14;padding:6px;");
    netBtn_->setStyleSheet("background:#0c160c;color:#39FF14;border:1px solid #39FF14;padding:6px;");
    localAdsbCheck_->setStyleSheet("background:rgba(0,0,0,160);color:#39FF14;padding:4px;");

    nam_ = new QNetworkAccessManager(this);
    uiTimer_ = new QTimer(this);
    connect(uiTimer_, &QTimer::timeout, this, &AircraftMapWidget::refreshUi);
    netTimer_ = new QTimer(this);
    connect(netTimer_, &QTimer::timeout, this, &AircraftMapWidget::fetchOpenSky);
    // Timers start only in showEvent — constructing the dock must not poll OpenSky/tiles
    // or touch DeviceManager while the user is on normal WFM listening.

    connect(tuneBtn_, &QPushButton::clicked, this, &AircraftMapWidget::onTune1090);
    connect(netBtn_, &QPushButton::clicked, this, &AircraftMapWidget::onRefreshNetwork);
    connect(localAdsbCheck_, &QCheckBox::toggled, this, &AircraftMapWidget::onLocalAdsbToggled);

    const auto obs = SatPassPlanner::instance().observer();
    centerLat_ = obs.latDeg != 0 ? obs.latDeg : -33.87;
    centerLon_ = obs.lonDeg != 0 ? obs.lonDeg : 151.21;
    AdsBTrackStore::instance().setObserver(centerLat_, centerLon_);
    AdsBTrackStore::instance().setUpdateCallback([this]() {
        QMetaObject::invokeMethod(this, "refreshUi", Qt::QueuedConnection);
    });
}

void AircraftMapWidget::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    if (uiTimer_ && !uiTimer_->isActive()) uiTimer_->start(1500);
    if (netTimer_ && !netTimer_->isActive()) netTimer_->start(30000);
    if (localAdsbCheck_ && localAdsbCheck_->isChecked()) startLocalWorker();
    fetchOpenSky();
    refreshUi();
}

void AircraftMapWidget::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    if (uiTimer_) uiTimer_->stop();
    if (netTimer_) netTimer_->stop();
    stopLocalWorker();
}

AircraftMapWidget::~AircraftMapWidget() {
    stopLocalWorker();
    AdsBTrackStore::instance().setUpdateCallback({});
}

void AircraftMapWidget::setEmbedded(bool embedded) {
    if (embedded) {
        setWindowFlags(Qt::Widget);
    } else {
        setWindowFlags(windowFlags() | Qt::Window);
    }
}

void AircraftMapWidget::stopLocalWorker() {
    localRun_.store(false);
    if (localThread_.joinable()) localThread_.join();
}

void AircraftMapWidget::startLocalWorker() {
    stopLocalWorker();
    localRun_.store(true);
    localThread_ = std::thread([this]() {
        while (localRun_.load()) {
            try {
                auto& mgr = DeviceManager::instance();
                const auto devs = mgr.getDevices();
                for (size_t i = 0; i < devs.size(); ++i) {
                    if (!devs[i].enabled) continue;
                    std::vector<float> power;
                    double cf = 0.0, sr = 0.0;
                    if (!mgr.getLatestSpectrum(i, power, cf, sr)) continue;
                    if (cf < 1085e6 || cf > 1095e6) continue;
                    // Small window only — Mode-S scan is O(n) and must not starve the demod ring.
                    auto iq = mgr.getRecentIQWindow(i, 8192);
                    if (iq.size() < 2048) break;
                    std::vector<float> mag(iq.size());
                    for (size_t k = 0; k < iq.size(); ++k) mag[k] = std::abs(iq[k]);
                    const double rate = (sr > 0.0) ? sr : (devs[i].sampleRate > 0 ? devs[i].sampleRate : 2.048e6);
                    AdsBTrackStore::instance().processMagnitude(mag.data(), mag.size(), rate);
                    break;
                }
            } catch (...) {
            }
            for (int i = 0; i < 20 && localRun_.load(); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(100)); // ~2 Hz
        }
    });
}

void AircraftMapWidget::onLocalAdsbToggled(bool on) {
    if (on && isVisible()) startLocalWorker();
    else stopLocalWorker();
    refreshUi();
}

void AircraftMapWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    status_->setGeometry(8, 8, width() - 340, 28);
    localAdsbCheck_->setGeometry(8, 40, 160, 24);
    netBtn_->setGeometry(width() - 210, 8, 100, 28);
    tuneBtn_->setGeometry(width() - 100, 8, 92, 28);
    ensureTiles();
}

void AircraftMapWidget::refreshUi() {
    const auto snap = AdsBTrackStore::instance().snapshot();
    if (!followIcao_.isEmpty()) {
        // keep center if following — optional
    }
    centerLat_ = snap.centerLat;
    centerLon_ = snap.centerLon;
    status_->setText(
        QString("Tracks %1 · local CRC %2 · net %3 · localIQ %4 · %5")
            .arg(snap.tracks.size())
            .arg(snap.localCrcOk)
            .arg(snap.networkOnline ? QString("OK age %1s").arg(snap.networkAgeSec) : QString("offline"))
            .arg(localRun_.load() ? "on" : "off")
            .arg(QString::fromStdString(snap.lastStatus)));
    ensureTiles();
    update();
}

void AircraftMapWidget::fetchOpenSky() {
    double lat = centerLat_, lon = centerLon_, nm = 120.0;
    AdsBTrackStore::instance().observer(&lat, &lon, &nm);
    const double dlat = nm / 60.0;
    const double dlon = nm / (60.0 * std::max(0.2, std::cos(lat * 3.14159265358979323846 / 180.0)));
    const QUrl url(QString("https://opensky-network.org/api/states/all?lamin=%1&lomin=%2&lamax=%3&lomax=%4")
                       .arg(lat - dlat, 0, 'f', 4)
                       .arg(lon - dlon, 0, 'f', 4)
                       .arg(lat + dlat, 0, 'f', 4)
                       .arg(lon + dlon, 0, 'f', 4));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, "SDR-Town-AircraftMap/0.2.71");
    QNetworkReply* reply = nam_->get(req);
    connect(reply, &QNetworkReply::finished, this, [reply]() {
        if (reply->error() != QNetworkReply::NoError) {
            AdsBTrackStore::instance().setNetworkError(reply->errorString().toStdString());
        } else {
            AdsBTrackStore::instance().mergeNetworkJson(reply->readAll().toStdString());
        }
        reply->deleteLater();
    });
}

void AircraftMapWidget::onTune1090() {
    auto& mgr = DeviceManager::instance();
    const auto devs = mgr.getDevices();
    size_t idx = 0;
    for (size_t i = 0; i < devs.size(); ++i) {
        if (devs[i].enabled) {
            idx = i;
            break;
        }
    }
    if (devs.empty()) return;
    std::string err;
    mgr.setEnabled(idx, true);
    if (!mgr.acquireDeviceLease(idx, DeviceManager::DeviceLeaseOwner::Aircraft, false, &err)) {
        status_->setText(QString::fromStdString(err));
        return;
    }
    mgr.startStreaming(idx, true);
    mgr.retuneWithLease(idx, 1090e6, DeviceManager::DeviceLeaseOwner::Aircraft, true, nullptr);
    status_->setText("Tuned to 1090 MHz — enable Local 1090 decode if you want Mode-S markers");
}

void AircraftMapWidget::onRefreshNetwork() {
    fetchOpenSky();
    std::thread([]() {
        std::string err;
        AdsBTrackStore::instance().refreshNetwork(&err);
    }).detach();
}

QPointF AircraftMapWidget::latLonToPixel(double lat, double lon) const {
    double cx, cy, x, y;
    latLonToTileXY(centerLat_, centerLon_, zoom_, &cx, &cy);
    latLonToTileXY(lat, lon, zoom_, &x, &y);
    return QPointF(width() * 0.5 + (x - cx) * kTile, height() * 0.5 + (y - cy) * kTile);
}

bool AircraftMapWidget::pixelToLatLon(const QPointF& pt, double* lat, double* lon) const {
    double cx, cy;
    latLonToTileXY(centerLat_, centerLon_, zoom_, &cx, &cy);
    const double x = cx + (pt.x() - width() * 0.5) / kTile;
    const double y = cy + (pt.y() - height() * 0.5) / kTile;
    const double n = std::pow(2.0, zoom_);
    *lon = x / n * 360.0 - 180.0;
    const double latRad = std::atan(std::sinh(3.14159265358979323846 * (1.0 - 2.0 * y / n)));
    *lat = latRad * 180.0 / 3.14159265358979323846;
    return true;
}

void AircraftMapWidget::ensureTiles() {
    double cx, cy;
    latLonToTileXY(centerLat_, centerLon_, zoom_, &cx, &cy);
    const int tx0 = int(std::floor(cx - width() / double(kTile) / 2.0)) - 1;
    const int ty0 = int(std::floor(cy - height() / double(kTile) / 2.0)) - 1;
    const int tx1 = tx0 + int(width() / kTile) + 3;
    const int ty1 = ty0 + int(height() / kTile) + 3;
    const int n = 1 << zoom_;
    int pending = 0;
    for (int ty = ty0; ty <= ty1; ++ty) {
        for (int tx = tx0; tx <= tx1; ++tx) {
            const int txx = ((tx % n) + n) % n;
            if (ty < 0 || ty >= n) continue;
            const QString key = QString("%1/%2/%3").arg(zoom_).arg(txx).arg(ty);
            if (tiles_.contains(key)) continue;
            if (++pending > 8) return; // rate-limit tile storms
            tiles_.insert(key, QPixmap());
            const QUrl url(QString("https://tile.openstreetmap.org/%1/%2/%3.png").arg(zoom_).arg(txx).arg(ty));
            QNetworkRequest req(url);
            req.setHeader(QNetworkRequest::UserAgentHeader, "SDR-Town-AircraftMap/0.2.71");
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

void AircraftMapWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(20, 30, 40));
    double cx, cy;
    latLonToTileXY(centerLat_, centerLon_, zoom_, &cx, &cy);
    for (auto it = tiles_.begin(); it != tiles_.end(); ++it) {
        const QStringList parts = it.key().split('/');
        if (parts.size() != 3) continue;
        if (parts[0].toInt() != zoom_) continue;
        const int tx = parts[1].toInt();
        const int ty = parts[2].toInt();
        const double px = width() * 0.5 + (tx + 0.0 - cx) * kTile;
        const double py = height() * 0.5 + (ty + 0.0 - cy) * kTile;
        if (it.value().isNull()) p.fillRect(QRectF(px, py, kTile, kTile), QColor(40, 50, 60));
        else p.drawPixmap(QPointF(px, py), it.value());
    }

    const auto snap = AdsBTrackStore::instance().snapshot();
    for (const auto& t : snap.tracks) {
        if (!t.positionValid) continue;
        const QPointF pt = latLonToPixel(t.latDeg, t.lonDeg);
        p.save();
        p.translate(pt);
        p.rotate(t.trackDeg);
        p.setBrush(t.fromAdsc ? QColor(255, 180, 40)
                              : (t.fromLocal ? QColor(57, 255, 20) : QColor(80, 160, 255)));
        p.setPen(Qt::black);
        QPolygon poly;
        poly << QPoint(0, -8) << QPoint(5, 8) << QPoint(0, 4) << QPoint(-5, 8);
        p.drawPolygon(poly);
        p.restore();
        p.setPen(Qt::white);
        p.drawText(pt + QPointF(8, -4),
                   QString::fromStdString(t.callsign.empty() ? t.icaoHex : t.callsign));
    }
}

void AircraftMapWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && event->modifiers() & Qt::ShiftModifier) {
        double lat, lon;
        if (pixelToLatLon(event->position(), &lat, &lon)) {
            SatObserverConfig o = SatPassPlanner::instance().observer();
            o.latDeg = lat;
            o.lonDeg = lon;
            SatPassPlanner::instance().setObserver(o);
            AdsBTrackStore::instance().setObserver(lat, lon);
            centerLat_ = lat;
            centerLon_ = lon;
            status_->setText(QString("Home set %1, %2 (ISS/Doppler/passes)").arg(lat, 0, 'f', 5).arg(lon, 0, 'f', 5));
            ensureTiles();
            update();
        }
        return;
    }
    if (event->button() == Qt::MiddleButton ||
        (event->button() == Qt::LeftButton && event->modifiers() & Qt::ControlModifier)) {
        double lat, lon;
        pixelToLatLon(event->position(), &lat, &lon);
        centerLat_ = lat;
        centerLon_ = lon;
        AdsBTrackStore::instance().setObserver(lat, lon);
        ensureTiles();
        update();
        return;
    }
    const auto snap = AdsBTrackStore::instance().snapshot();
    QString best;
    double bestD = 20.0;
    for (const auto& t : snap.tracks) {
        if (!t.positionValid) continue;
        const QPointF pt = latLonToPixel(t.latDeg, t.lonDeg);
        const double d = QLineF(pt, event->position()).length();
        if (d < bestD) {
            bestD = d;
            best = QString::fromStdString(t.icaoHex);
        }
    }
    if (!best.isEmpty()) showPopout(best);
}

void AircraftMapWidget::wheelEvent(QWheelEvent* event) {
    if (event->angleDelta().y() > 0 && zoom_ < 16) ++zoom_;
    else if (event->angleDelta().y() < 0 && zoom_ > 4) --zoom_;
    ensureTiles();
    update();
}

void AircraftMapWidget::showPopout(const QString& icaoHex) {
    bool ok = false;
    const uint32_t icao = icaoHex.toUInt(&ok, 16);
    if (!ok) return;
    const auto t = AdsBTrackStore::instance().trackByIcao(icao);
    auto* dlg = new QDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(QString::fromStdString(t.callsign.empty() ? t.icaoHex : t.callsign));
    auto* lay = new QVBoxLayout(dlg);
    lay->addWidget(new QLabel(
        QString("<b>%1</b><br>ICAO %2<br>Alt %3 ft · %4 kt<br>Source: %5%6%7")
            .arg(QString::fromStdString(t.callsign.empty() ? "(no callsign)" : t.callsign))
            .arg(QString::fromStdString(t.icaoHex))
            .arg(t.altFt, 0, 'f', 0)
            .arg(t.gsKt, 0, 'f', 0)
            .arg(t.fromLocal ? "local " : "")
            .arg(t.fromNetwork ? "OpenSky " : "")
            .arg(t.fromAdsc ? "ADS-C" : "")));
    dlg->show();
}
