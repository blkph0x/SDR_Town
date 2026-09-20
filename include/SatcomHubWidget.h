#pragma once

#include <QWidget>

class QTabWidget;
class QShowEvent;
class QHideEvent;
class SatcomScannerWidget;
class InmarsatWidget;
class AircraftMapWidget;

// Single SDR Town dock panel: Satcom | Inmarsat | Aircraft (no floating windows).
// Child tabs are created lazily on first show so normal WFM listening is untouched.
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

private:
    void ensureTabs();

    QTabWidget* tabs_ = nullptr;
    SatcomScannerWidget* satcom_ = nullptr;
    InmarsatWidget* inmarsat_ = nullptr;
    AircraftMapWidget* aircraft_ = nullptr;
};
