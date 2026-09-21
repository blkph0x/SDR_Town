#pragma once

#include <QString>
#include <QWidget>

#include <cstddef>

class QTabWidget;
class QTimer;
class QShowEvent;
class QHideEvent;
class SatcomScannerWidget;
class InmarsatWidget;
class AircraftMapWidget;

// Single SDR Town dock panel: Satcom | Inmarsat | Aircraft (no floating windows).
// Satellite auto-capture is intentionally owned here so it remains active while
// the tab/dock is hidden and before the heavy child tabs are first constructed.
class SatcomHubWidget : public QWidget {
    Q_OBJECT
public:
    explicit SatcomHubWidget(QWidget* parent = nullptr);

    SatcomScannerWidget* satcom() const { return satcom_; }
    InmarsatWidget* inmarsat() const { return inmarsat_; }
    AircraftMapWidget* aircraft() const { return aircraft_; }

    void showSatcomTab();
    void showInmarsatTab();
    void showAircraftTab();

signals:
    void requestOpenSstvLive();

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private slots:
    void autoCaptureTick();

private:
    void ensureTabs();
    void stopAutoCapture(bool keepHandledKey);

    QTabWidget* tabs_ = nullptr;
    QTimer* autoCaptureTimer_ = nullptr;
    SatcomScannerWidget* satcom_ = nullptr;
    InmarsatWidget* inmarsat_ = nullptr;
    AircraftMapWidget* aircraft_ = nullptr;

    QString autoCapturePassKey_;
    qint64 autoCaptureRetryAfter_ = 0;
    bool autoCaptureOwned_ = false;
    bool autoEngineWasRunning_ = false;
    size_t autoDeviceIndex_ = static_cast<size_t>(-1);
    double autoPreviousCenterHz_ = 0.0;
    int autoPreviousLeaseOwner_ = 0;
};
