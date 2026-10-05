#include "AircraftMapWidget.h"
#include "AdsBTrackStore.h"
#include "DeviceManager.h"
#include "SatPassPlanner.h"
#include "AircraftReceivePlan.h"
#include "AircraftMagnitudeStream.h"
#include "Receiver.h"
#include "WorkflowRadioSession.h"
#include "WorkflowDeviceCombo.h"
#include "WorkflowSessionId.h"
#include <QCoreApplication>
#include <QSet>

#include <QCheckBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QJsonDocument>
#include <QSettings>
#include <QSignalBlocker>
#include <spdlog/spdlog.h>
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
QSet<AircraftMapWidget*> aircraftControllers; // GUI-thread ownership only.

constexpr int kTile = 256;

void latLonToTileXY(double lat, double lon, int z, double* x, double* y) {
    const double n = std::pow(2.0, z);
    *x = (lon + 180.0) / 360.0 * n;
    const double latRad = lat * 3.14159265358979323846 / 180.0;
    *y = (1.0 - std::log(std::tan(latRad) + 1.0 / std::cos(latRad)) / M_PI) / 2.0 * n;
}

} // namespace

AircraftMapWidget::AircraftMapWidget(QWidget* parent, const QString& sessionId)
    : QWidget(parent),
      sessionId_(QString::fromStdString(normalizedWorkflowSessionId(sessionId.toStdString()))),
      settingsPrefix_(sessionId_.isEmpty() ? "aircraft/" : "aircraft/sessions/" + sessionId_ + "/"),
      ownedStore_(sessionId_.isEmpty() ? nullptr : std::make_unique<AdsBTrackStore>(sessionId_.toStdString())),
      store_(ownedStore_ ? *ownedStore_ : AdsBTrackStore::instance())
{
    if (!sessionId_.isEmpty()) setObjectName("aircraftSession." + sessionId_);
    setMinimumSize(560, 360);
    setWindowTitle("Aircraft Map");

    status_ = new QLabel("Aircraft map — OpenSky (local 1090 off)", this);
    tuneBtn_ = new QPushButton("Tune 1090", this);
    tuneBtn_->setObjectName("aircraftTune");
    stopBtn_ = new QPushButton("Stop radio", this);
    stopBtn_->setObjectName("aircraftStop");
    stopBtn_->setEnabled(false);
    deviceCombo_ = new QComboBox(this);
    deviceCombo_->setObjectName("aircraftDevice");
    deviceCombo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    deviceCombo_->setMinimumContentsLength(18);
    refreshWorkflowDeviceCombo(*deviceCombo_, DeviceOwnership::Owner::Aircraft,
        QSettings().value(settingsPrefix_ + "deviceKey").toString());
    netBtn_ = new QPushButton("Refresh net", this);
    localAdsbCheck_ = new QCheckBox("Local 1090 decode", this);
    localAdsbCheck_->setObjectName("aircraftLocalDecode");
    localAdsbCheck_->setChecked(false);
    toolbar_ = new QWidget(this);
    auto* layout = new QGridLayout(toolbar_);
    layout->setContentsMargins(6, 6, 6, 6);
    internetCheck_ = new QCheckBox("Internet aircraft", toolbar_);
    internetCheck_->setObjectName("aircraftInternetEnabled");
    internetCheck_->setChecked(store_.networkEnabled());
    internetCheck_->setToolTip("Turn off to stop aircraft lookups and remove their data. Local ADS-B/ADS-C remain; map tiles are separate.");
    captureBandwidth_ = new QDoubleSpinBox(toolbar_);
    captureBandwidth_->setObjectName("aircraftCaptureBandwidthMHz");
    captureBandwidth_->setRange(2, 20);
    captureBandwidth_->setDecimals(3);
    captureBandwidth_->setSuffix(" MS/s");
    captureBandwidth_->setValue(QSettings().value(settingsPrefix_ + "captureBandwidthMHz", 2.4).toDouble());
    captureBandwidth_->setToolTip("Requested IQ sample rate. 2.4 MS/s is the default; the applied device rate is shown after tuning. Existing saved requests are preserved.");
    layout->addWidget(status_, 0, 0, 1, 4);
    status_->setWordWrap(true);
    layout->addWidget(localAdsbCheck_, 1, 0, 1, 2);
    layout->addWidget(internetCheck_, 1, 2);
    layout->addWidget(netBtn_, 1, 3);
    layout->addWidget(new QLabel("Sample rate", toolbar_), 2, 0);
    layout->addWidget(captureBandwidth_, 2, 1, 1, 2);
    layout->addWidget(tuneBtn_, 2, 3);
    layout->addWidget(new QLabel("Radio", toolbar_), 3, 0);
    layout->addWidget(deviceCombo_, 3, 1, 1, 2);
    layout->addWidget(stopBtn_, 3, 3);
    stopBtn_->setEnabled(false);
    toolbar_->setStyleSheet("QWidget { background: #202428; color: #eeeeee; }");
    netBtn_->setEnabled(internetCheck_->isChecked());
    localAdsbCheck_->setToolTip("Decode new IQ in order on the selected receiver at 1090 MHz. Processing gaps are logged.");
    status_->setStyleSheet("padding:6px;");

    nam_ = new QNetworkAccessManager(this);
    uiTimer_ = new QTimer(this);
    connect(uiTimer_, &QTimer::timeout, this, &AircraftMapWidget::refreshUi);
    netTimer_ = new QTimer(this);
    connect(netTimer_, &QTimer::timeout, this, &AircraftMapWidget::fetchOpenSky);
    // Timers start only in showEvent — constructing the dock must not poll OpenSky/tiles
    // or touch DeviceManager while the user is on normal WFM listening.

    connect(tuneBtn_, &QPushButton::clicked, this, &AircraftMapWidget::onTune1090);
    connect(stopBtn_, &QPushButton::clicked, this, [this] { localRun_.store(false); tuneStatus_ = "Stopping aircraft radio"; refreshUi(); });
    connect(deviceCombo_, &QComboBox::currentIndexChanged, this, [this] {
        QSettings().setValue(settingsPrefix_ + "deviceKey", deviceCombo_->currentData());
    });
    connect(captureBandwidth_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        QSettings().setValue(settingsPrefix_ + "captureBandwidthMHz", value);
    });
    connect(netBtn_, &QPushButton::clicked, this, &AircraftMapWidget::onRefreshNetwork);
    connect(localAdsbCheck_, &QCheckBox::toggled, this, &AircraftMapWidget::onLocalAdsbToggled);
    connect(internetCheck_, &QCheckBox::toggled, this, &AircraftMapWidget::onInternetAircraftToggled);

    const auto obs = SatPassPlanner::instance().observer();
    centerLat_ = obs.latDeg != 0 ? obs.latDeg : -33.87;
    centerLon_ = obs.lonDeg != 0 ? obs.lonDeg : 151.21;
    store_.setObserver(centerLat_, centerLon_);
    // UI timer reads snapshots; no callback may race QWidget destruction.
    aircraftControllers.insert(this);
}

void AircraftMapWidget::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    if (uiTimer_ && !uiTimer_->isActive()) uiTimer_->start(1500);
    if (store_.networkEnabled() && !netTimer_->isActive()) netTimer_->start(30000);
    if (!radioBusy_.load()) refreshWorkflowDeviceCombo(*deviceCombo_, DeviceOwnership::Owner::Aircraft,
        deviceCombo_->currentData().toString());
    fetchOpenSky();
    refreshUi();
}

void AircraftMapWidget::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    if (uiTimer_) uiTimer_->stop();
    if (netTimer_) netTimer_->stop();
    if (aircraftReply_) aircraftReply_->abort();
    // DEC-0189: navigation pauses rendering/network only, never a radio worker.
}

AircraftMapWidget::~AircraftMapWidget() {
    stopLocalWorker();
    aircraftControllers.remove(this);
}

void AircraftMapWidget::stopAll() {
    for (auto* controller : aircraftControllers) controller->stopLocalWorker();
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

void AircraftMapWidget::startLocalWorker(const std::string& key, double rateHz, double bandwidthHz) {
    stopLocalWorker();
    radioBusy_.store(true);
    localRun_.store(true);
    decodeLocal_.store(localAdsbCheck_->isChecked());
    deviceCombo_->setEnabled(false); captureBandwidth_->setEnabled(false); tuneBtn_->setEnabled(false); stopBtn_->setEnabled(true);
    localThread_ = std::thread([this, key, rateHz, bandwidthHz]() {
        std::string failure;
        try {
        auto& mgr = DeviceManager::instance();
        WorkflowRadioSession radio(mgr, key, DeviceOwnership::Owner::Aircraft, 1090e6,
            [this] { return !localRun_.load(); }, rateHz, bandwidthHz);
        const auto i = radio.deviceIndex();
        const double appliedRate=mgr.getCurrentSampleRate(i);
        appliedSampleRateHz_.store(appliedRate);
        spdlog::info("Aircraft session={} radio={} key={} requestedRateHz={} appliedRateHz={} ready", sessionId_.toStdString(), i, key, rateHz, appliedRate);
        radioReady_.store(true);
        QMetaObject::invokeMethod(this, [this, appliedRate] {
            tuneStatus_ = QString("1090 MHz ready | Applied %1 MS/s").arg(appliedRate / 1e6, 0, 'f', 3);
            refreshUi();
        }, Qt::QueuedConnection);
        Receiver cursor;
        AircraftMagnitudeStream decoder;
        size_t selected = static_cast<size_t>(-1);
        uint64_t next = 0, epoch = 0, samples = 0, gaps = 0, decoded = 0;
        auto reportAt = std::chrono::steady_clock::now();
        while (localRun_.load() && radio.valid()) {
            if (!mgr.isHardwareStreaming(i)) {
                failure = "Aircraft receiver lost live hardware";
                break;
            }
            bool received = false;
            try {
                const double rate = mgr.getCurrentSampleRate(i);
                if (decodeLocal_.load() && mgr.isHardwareStreaming(i) &&
                    mgr.getCurrentCenterFreq(i) == 1090e6 && rate >= 2e6 && rate <= 20e6) {
                    if (selected != i) { mgr.setReceiverCursorToLiveEdge(i, cursor); decoder.reset(); selected = i; next = 0; }
                    // Bounded work units, chronological cursor. One-second backlog
                    // budget protects interactive use; any skipped samples reset the tail.
                    auto iq = mgr.getNewIQWindowForReceiver(i, cursor, 8192, static_cast<size_t>(rate));
                    received = !iq.samples.empty();
                    if (received) {
                        if (iq.cursorDiscontinuity || (next && (epoch != iq.streamEpoch || next != iq.startAbsolute))) {
                            ++gaps; decoder.reset();
                        }
                        epoch = iq.streamEpoch; next = iq.endAbsolute;
                        if (!iq.cursorDiscontinuity) {
                            std::vector<float> mag(iq.samples.size());
                            for (size_t k = 0; k < mag.size(); ++k) mag[k] = std::abs(iq.samples[k]);
                            const auto frames = decoder.process(mag, iq.startAbsolute, iq.streamEpoch, rate);
                            for (const auto& frame : frames) store_.ingestModeSFrame(frame.data());
                            samples += mag.size(); decoded += frames.size();
                        }
                    }
                } else {
                    decoder.reset(); selected = static_cast<size_t>(-1); next = 0;
                }
                if (std::chrono::steady_clock::now() - reportAt >= std::chrono::seconds(30)) {
                    spdlog::info("Aircraft IQ dev={} rateHz={} processedSamples={} gaps={} decodedFrames={}", i, rate, samples, gaps, decoded);
                    reportAt = std::chrono::steady_clock::now();
                }
            } catch (const std::exception& e) {
                spdlog::warn("Aircraft IQ worker stopped: {}", e.what());
                failure = e.what();
                localRun_.store(false);
            }
            if (!received) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        if (localRun_.load() && !radio.valid()) failure = "Aircraft radio ownership expired";
        } catch (const std::exception& e) {
            if (localRun_.load()) failure = e.what();
        }
        localRun_.store(false); radioReady_.store(false); appliedSampleRateHz_.store(0);
        QMetaObject::invokeMethod(this, [this, failure] {
            radioBusy_.store(false);
            tuneStatus_ = failure.empty() ? "Aircraft radio stopped" : "Aircraft radio: " + QString::fromStdString(failure);
            deviceCombo_->setEnabled(true); captureBandwidth_->setEnabled(true); tuneBtn_->setEnabled(true); stopBtn_->setEnabled(false);
            refreshWorkflowDeviceCombo(*deviceCombo_, DeviceOwnership::Owner::Aircraft, deviceCombo_->currentData().toString());
            refreshUi();
        }, Qt::QueuedConnection);
    });
}

void AircraftMapWidget::onLocalAdsbToggled(bool on) {
    decodeLocal_.store(on);
    if (on && !radioBusy_.load()) tuneStatus_ = "Select a radio and Tune 1090 to start local decoding";
    refreshUi();
}

void AircraftMapWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    toolbar_->setGeometry(8, 8, width() - 16, toolbar_->sizeHint().height());
    ensureTiles();
}

void AircraftMapWidget::refreshUi() {
    const auto snap = store_.snapshot();
    { const QSignalBlocker block(internetCheck_); internetCheck_->setChecked(snap.networkEnabled); }
    netBtn_->setEnabled(snap.networkEnabled);
    if (!followIcao_.isEmpty()) {
        // keep center if following — optional
    }
    centerLat_ = snap.centerLat;
    centerLon_ = snap.centerLon;
    status_->setText(
        QString("Tracks %1 · local CRC %2 · net %3 · localIQ %4 · %5")
            .arg(snap.tracks.size())
            .arg(snap.localCrcOk)
            .arg(!snap.networkEnabled ? QString("disabled") : snap.networkOnline ? QString("OK age %1s").arg(snap.networkAgeSec) : QString("offline"))
            .arg(radioReady_.load() && decodeLocal_.load() ? "on" : "off")
            .arg(QString::fromStdString(snap.lastStatus)) + (tuneStatus_.isEmpty() ? QString{} : "\n" + tuneStatus_));
    toolbar_->setGeometry(8, 8, width() - 16, toolbar_->sizeHint().height());
    ensureTiles();
    update();
}

void AircraftMapWidget::fetchOpenSky() {
    if (!isVisible() || !store_.networkEnabled() || aircraftReply_) return;
    const auto generation = store_.networkGeneration();
    double lat = centerLat_, lon = centerLon_, nm = 120.0;
    store_.observer(&lat, &lon, &nm);
    const double dlat = nm / 60.0;
    const double dlon = nm / (60.0 * std::max(0.2, std::cos(lat * 3.14159265358979323846 / 180.0)));
    const QUrl url(QString("https://opensky-network.org/api/states/all?lamin=%1&lomin=%2&lamax=%3&lomax=%4")
                       .arg(lat - dlat, 0, 'f', 4)
                       .arg(lon - dlon, 0, 'f', 4)
                       .arg(lat + dlat, 0, 'f', 4)
                       .arg(lon + dlon, 0, 'f', 4));
    QNetworkRequest req(url);
    req.setTransferTimeout(20000);
    req.setHeader(QNetworkRequest::UserAgentHeader, "SDR-Town-AircraftMap/0.2.71");
    QNetworkReply* reply = nam_->get(req);
    aircraftReply_ = reply;
    connect(reply, &QNetworkReply::readyRead, this, [reply]() {
        if (reply->bytesAvailable() > 2 * 1024 * 1024) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation]() {
        if (aircraftReply_ == reply) aircraftReply_.clear();
        if (reply->error() != QNetworkReply::NoError) {
            if (reply->error() != QNetworkReply::OperationCanceledError)
                store_.setNetworkError(reply->errorString().toStdString(), generation);
        } else {
            store_.mergeNetworkJson(reply->readAll().toStdString(), generation);
        }
        reply->deleteLater();
    });
}

void AircraftMapWidget::onTune1090() {
    tuneSucceeded_=false;
    if (QCoreApplication::instance()->property("sdrtown.guiDryRun").toBool()) {
        tuneStatus_ = "RF disabled in GUI dry-run"; refreshUi(); return;
    }
    if (radioBusy_.load()) { tuneStatus_ = "Stop the aircraft radio before tuning again"; refreshUi(); return; }
    if (!sessionId_.isEmpty() && deviceCombo_->currentData().toString().isEmpty()) {
        tuneStatus_ = "Select a radio for this Aircraft session"; refreshUi(); return;
    }
    auto& mgr = DeviceManager::instance();
    const auto devs = mgr.getDevices();
    std::string err;
    const size_t idx = mgr.resolveWorkflowDevice(DeviceOwnership::Owner::Aircraft,
        deviceCombo_->currentData().toString().toStdString(), mgr.preferredListenDeviceIndex(), &err);
    if (idx >= devs.size()) { tuneStatus_ = QString::fromStdString(err); refreshUi(); return; }
    const auto plan = aircraftReceivePlan(captureBandwidth_->value() * 1e6,
        devs[idx].sampleRates, devs[idx].bandwidthsHz);
    if (plan.sampleRateHz == 0) {
        tuneStatus_ = "No advertised Mode-S capture rate. Rescan the receiver in Devices.";
        refreshUi(); return;
    }
    if (devs[idx].maxFreq > 0 && (devs[idx].maxFreq < 1090e6 || devs[idx].minFreq > 1090e6)) {
        tuneStatus_ = "Selected receiver cannot tune 1090 MHz"; refreshUi(); return;
    }
    if (mgr.isStreaming(idx)) { tuneStatus_ = "Selected radio is already receiving; stop its workflow or choose another SDR"; refreshUi(); return; }
    const auto key = devs[idx].stableKey;
    QSettings().setValue(settingsPrefix_ + "deviceKey", QString::fromStdString(key));
    refreshWorkflowDeviceCombo(*deviceCombo_, DeviceOwnership::Owner::Aircraft, QString::fromStdString(key));
    tuneStatus_ = "Opening selected aircraft radio";
    startLocalWorker(key, plan.sampleRateHz, plan.hardwareBandwidthHz);
    tuneSucceeded_=true; // Accepted, not proof of hardware ready; webStatus exposes both.
    refreshUi();
}

void AircraftMapWidget::onRefreshNetwork() {
    fetchOpenSky();
}

void AircraftMapWidget::onInternetAircraftToggled(bool on) {
    store_.setNetworkEnabled(on);
    netBtn_->setEnabled(on);
    if (!on) {
        netTimer_->stop();
        if (aircraftReply_) aircraftReply_->abort();
        for (auto* dialog : findChildren<QDialog*>()) dialog->close();
    } else if (isVisible()) {
        netTimer_->start(30000);
        fetchOpenSky();
    }
    spdlog::info("Aircraft internet source enabled={} (network tracks cleared)", on);
    refreshUi();
}

QJsonObject AircraftMapWidget::webStatus() const {
    auto result=QJsonDocument::fromJson(QByteArray::fromStdString(
        store_.statusJson().dump())).object();
    for(const auto* key:{"centerLat","centerLon","radiusNm"})result.remove(key);
    result.insert("ok",true);result.insert("localDecodeEnabled",localAdsbCheck_->isChecked());
    result.insert("sessionId",sessionId_);
    result.insert("localDecodeRunning",radioReady_.load() && localRun_.load() && decodeLocal_.load());
    result.insert("radioReady",radioReady_.load());
    result.insert("radioBusy",radioBusy_.load());
    result.insert("deviceKey",deviceCombo_->currentData().toString());
    result.insert("captureBandwidthMHz",captureBandwidth_->value());
    result.insert("appliedSampleRateHz",appliedSampleRateHz_.load());
    result.insert("tuneStatus",tuneStatus_);
    return result;
}

QJsonObject AircraftMapWidget::webControl(const QJsonObject& body) {
    const auto action=body.value("action").toString();
    if(action=="tune" || action=="configure") {
        if (radioBusy_.load()) return {{"ok",false},{"status",409},{"error","Stop the aircraft radio before changing source"}};
        if (body.contains("captureBandwidthMHz") && !body.value("captureBandwidthMHz").isDouble())
            return {{"ok",false},{"status",400},{"error","captureBandwidthMHz must be numeric"}};
        const double mhz=body.value("captureBandwidthMHz").toDouble(captureBandwidth_->value());
        if(!std::isfinite(mhz) || mhz<2 || mhz>20)
            return {{"ok",false},{"status",400},{"error","captureBandwidthMHz must be 2..20"}};
        if (body.contains("deviceKey")) {
            if (!body.value("deviceKey").isString()) return {{"ok",false},{"status",400},{"error","deviceKey must be a string"}};
            refreshWorkflowDeviceCombo(*deviceCombo_, DeviceOwnership::Owner::Aircraft, body.value("deviceKey").toString());
            QSettings().setValue(settingsPrefix_ + "deviceKey", deviceCombo_->currentData());
        }
        captureBandwidth_->setValue(mhz);
        QSettings().setValue(settingsPrefix_ + "captureBandwidthMHz", mhz);
        if (action == "configure") return webStatus();
        onTune1090();
        if(!tuneSucceeded_)return {{"ok",false},{"status",409},{"error",tuneStatus_}};
    } else if(action=="stop") {
        localRun_.store(false);
    } else if(action=="local") {
        if(!body.value("enabled").isBool())return {{"ok",false},{"status",400},{"error","enabled boolean required"}};
        remoteLocal_=body.value("enabled").toBool();
        {QSignalBlocker block(localAdsbCheck_);localAdsbCheck_->setChecked(remoteLocal_);}
        onLocalAdsbToggled(remoteLocal_);
    } else if(action=="network-off") {
        onInternetAircraftToggled(false);
    } else if(action=="refresh") {
        // Existing local privacy choice is authoritative. No web opt-in.
        if(!store_.networkEnabled())
            return {{"ok",false},{"status",403},{"error","Internet aircraft disabled by local operator"}};
        onRefreshNetwork();
    } else return {{"ok",false},{"status",400},{"error","unsupported aircraft action"}};
    return webStatus();
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

    const auto snap = store_.snapshot();
    for (const auto& t : snap.tracks) {
        if (!t.positionValid) continue;
        const QPointF pt = latLonToPixel(t.latDeg, t.lonDeg);
        p.save();
        p.translate(pt);
        if(t.trackValid)p.rotate(t.trackDeg);
        p.setBrush(t.fromAdsc ? QColor(255, 180, 40)
                              : (t.fromLocal ? QColor(57, 255, 20) : QColor(80, 160, 255)));
        p.setPen(Qt::black);
        QPolygon poly;
        poly << QPoint(0, -8) << QPoint(5, 8) << QPoint(0, 4) << QPoint(-5, 8);
        if(t.trackValid)p.drawPolygon(poly);
        else p.drawEllipse(QPointF(0,0),5,5);
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
            if (sessionId_.isEmpty()) SatPassPlanner::instance().setObserver(o);
            store_.setObserver(lat, lon);
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
        store_.setObserver(lat, lon);
        ensureTiles();
        update();
        return;
    }
    const auto snap = store_.snapshot();
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
    const auto t = store_.trackByIcao(icao);
    auto* dlg = new QDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(QString::fromStdString(t.callsign.empty() ? t.icaoHex : t.callsign));
    auto* lay = new QVBoxLayout(dlg);
    lay->addWidget(new QLabel(
        QString("<b>%1</b><br>ICAO %2<br>Alt %3 ft · %4 kt<br>Source: %5%6%7")
            .arg(QString::fromStdString(t.callsign.empty() ? "(no callsign)" : t.callsign).toHtmlEscaped())
            .arg(QString::fromStdString(t.icaoHex).toHtmlEscaped())
            .arg(t.altFt, 0, 'f', 0)
            .arg(t.gsKt, 0, 'f', 0)
            .arg(t.fromLocal ? "local " : "")
            .arg(t.fromNetwork ? "OpenSky " : "")
            .arg(t.fromAdsc ? "ADS-C" : "")));
    dlg->show();
}
