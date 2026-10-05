#pragma once

#include <QWidget>
#include <QPixmap>
#include <QHash>
#include <QPointF>
#include <QPointer>
#include <QJsonObject>
#include <atomic>
#include <thread>
#include <memory>

class QTimer;
class QLabel;
class QPushButton;
class QCheckBox;
class QNetworkAccessManager;
class QNetworkReply;
class QDoubleSpinBox;
class QComboBox;
class AdsBTrackStore;

class AircraftMapWidget : public QWidget {
    Q_OBJECT
public:
    explicit AircraftMapWidget(QWidget* parent = nullptr, const QString& sessionId = {});
    ~AircraftMapWidget() override;
    const QString& sessionId() const { return sessionId_; }
    AdsBTrackStore& trackStore() { return store_; }
    static void stopAll();

    // When embedded in a dock, do not force a top-level window.
    void setEmbedded(bool embedded);
    QJsonObject webControl(const QJsonObject& body);
    QJsonObject webStatus() const;

public slots:
    void refreshUi();
    void onTune1090();
    void onRefreshNetwork();
    void onLocalAdsbToggled(bool on);
    void onInternetAircraftToggled(bool on);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    const QString sessionId_;
    const QString settingsPrefix_;
    std::unique_ptr<AdsBTrackStore> ownedStore_;
    AdsBTrackStore& store_;
    void ensureTiles();
    void fetchOpenSky();
    void stopLocalWorker();
    void startLocalWorker(const std::string& key, double rateHz, double bandwidthHz);
    QPointF latLonToPixel(double lat, double lon) const;
    bool pixelToLatLon(const QPointF& pt, double* lat, double* lon) const;
    void showPopout(const QString& icaoHex);

    double centerLat_ = -33.87;
    double centerLon_ = 151.21;
    int zoom_ = 9;
    QTimer* uiTimer_ = nullptr;
    QTimer* netTimer_ = nullptr;
    QLabel* status_ = nullptr;
    QPushButton* tuneBtn_ = nullptr;
    QPushButton* stopBtn_ = nullptr;
    QComboBox* deviceCombo_ = nullptr;
    QPushButton* netBtn_ = nullptr;
    QCheckBox* localAdsbCheck_ = nullptr;
    QCheckBox* internetCheck_ = nullptr;
    QDoubleSpinBox* captureBandwidth_ = nullptr;
    QWidget* toolbar_ = nullptr;
    QPointer<QNetworkReply> aircraftReply_;
    QString tuneStatus_;
    QNetworkAccessManager* nam_ = nullptr;
    QHash<QString, QPixmap> tiles_;
    QString followIcao_;

    std::atomic<bool> localRun_{false};
    std::atomic<bool> radioBusy_{false};
    std::atomic<bool> radioReady_{false};
    std::atomic<double> appliedSampleRateHz_{0};
    std::atomic<bool> decodeLocal_{false};
    std::thread localThread_;
    bool remoteLocal_=false;
    bool tuneSucceeded_=false;
};
