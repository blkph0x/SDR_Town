#pragma once

#include <QString>
#include <QWidget>
#include <QJsonObject>

#include <cstddef>

class QTabWidget;
class QTimer;
class QShowEvent;
class QHideEvent;
class SatcomScannerWidget;
class InmarsatWidget;
class AircraftMapWidget;

// Single SDR Town dock panel: Satcom | Inmarsat | Aircraft (no floating windows).
// The background timer calls the shared default/named scheduling policy so an
// armed pass stays active while hidden (DEC-0218). It does not start from a
// saved autoCapture flag; the operator must check Auto capture this session.
class SatcomHubWidget : public QWidget {
    Q_OBJECT
public:
    explicit SatcomHubWidget(QWidget* parent = nullptr);

    SatcomScannerWidget* satcom() const { return satcom_; }
    InmarsatWidget* inmarsat() const { return inmarsat_; }
    AircraftMapWidget* aircraft() const { return aircraft_; }
    void prepareRemoteWorkspace() { ensureTabs(); }

    void showSatcomTab();
    void showInmarsatTab();
    void showAircraftTab();
    InmarsatWidget* openInmarsatSession(const QString& sessionId);
    QJsonObject controlInmarsatSessions(const QString& method, const QJsonObject& body);
    QWidget* openReceiverSession(const QString& workflow, const QString& sessionId);
    QJsonObject controlReceiverSessions(const QString& workflow, const QString& method, const QJsonObject& body);

signals:
    void requestOpenSstvLive();

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private slots:
    void autoCaptureTick();

private:
    void ensureTabs();
    void saveInmarsatSessions();
    void saveReceiverSessions();

    QTabWidget* tabs_ = nullptr;
    QTimer* autoCaptureTimer_ = nullptr;
    bool automaticCaptureAllowed_ = true;
    SatcomScannerWidget* satcom_ = nullptr;
    InmarsatWidget* inmarsat_ = nullptr;
    AircraftMapWidget* aircraft_ = nullptr;

};
