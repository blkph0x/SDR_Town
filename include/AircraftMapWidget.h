#pragma once

#include <QWidget>
#include <QPixmap>
#include <QHash>
#include <QPointF>
#include <atomic>
#include <thread>

class QTimer;
class QLabel;
class QPushButton;
class QCheckBox;
class QNetworkAccessManager;

class AircraftMapWidget : public QWidget {
    Q_OBJECT
public:
    explicit AircraftMapWidget(QWidget* parent = nullptr);
    ~AircraftMapWidget() override;

    // When embedded in a dock, do not force a top-level window.
    void setEmbedded(bool embedded);

public slots:
    void refreshUi();
    void onTune1090();
    void onRefreshNetwork();
    void onLocalAdsbToggled(bool on);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void ensureTiles();
    void fetchOpenSky();
    void stopLocalWorker();
    void startLocalWorker();
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
    QPushButton* netBtn_ = nullptr;
    QCheckBox* localAdsbCheck_ = nullptr;
    QNetworkAccessManager* nam_ = nullptr;
    QHash<QString, QPixmap> tiles_;
    QString followIcao_;

    std::atomic<bool> localRun_{false};
    std::thread localThread_;
};
