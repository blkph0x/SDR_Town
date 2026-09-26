#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>
#include "ProcessPerformance.h"

#include <functional>

struct RemoteDiagnosticsConfig {
    bool enabled = false;
    QUrl endpoint = QUrl(QStringLiteral("https://gearsqueens.online/sdr-town-diag/ingest"));
    QString bearerToken;
    QString mode = "unknown";
    QString sessionId;
    QString configSource = "built-in HTTPS default";
    int maxBytesPerMinute = 64 * 1024;
    int maxPayloadBytes = 48 * 1024;
    int minIntervalMs = 1000;
    int maxQueue = 128;
    int requestTimeoutMs = 10000; // DEC-0139: bounded transport, not an RF timeout.
};

class RemoteDiagnosticsClient : public QObject {
public:
    explicit RemoteDiagnosticsClient(QObject* parent = nullptr);

    void configure(const RemoteDiagnosticsConfig& cfg);
    bool enabled() const;
    QString sessionId() const;
    QString clientId() const;
    QString hardwareHash() const;
    void submit(QString type, QString severity, QJsonObject payload);
    void checkClientStatus(QObject* context, std::function<void(const QJsonObject&)> callback);
    void flushNow();
    void drainForMs(int maxMs);
    void stopWithoutSending();
    QJsonObject deliveryStatistics() const; // Owner-thread snapshot.

private:
    struct PendingEvent {
        QByteArray body;
        int bytes = 0;
    };

    void submitOnOwnerThread(QString type, QString severity, QJsonObject payload);
    void pump();
    void schedulePump(int delayMs = 0);
    QString ensureClientId();

    RemoteDiagnosticsConfig m_cfg;
    class QNetworkAccessManager* m_network = nullptr;
    class QTimer* m_timer = nullptr;
    class QTimer* m_performanceTimer = nullptr;
    ProcessPerformance m_performance;
    QList<PendingEvent> m_queue;
    bool m_inFlight = false;
    qint64 m_windowStartMs = 0;
    int m_windowBytes = 0;
    qint64 m_lastSendMs = 0;
    quint64 m_seq = 0;
    quint64 m_queueDropped = 0;
    quint64 m_budgetDropped = 0;
    quint64 m_networkDropped = 0;
    quint64 m_oversizeDropped = 0;
    quint64 m_acknowledged = 0;
    int m_lastHttpStatus = 0;
    QString m_clientId;
    QString m_hardwareHash;
};

RemoteDiagnosticsConfig remoteDiagnosticsConfigFromProcess(int argc, char* argv[], const QString& mode);
RemoteDiagnosticsClient* remoteDiagnosticsConfigureFromProcess(int argc, char* argv[], QObject* parent, const QString& mode);
void remoteDiagnosticsSubmit(const QString& type, const QString& severity, const QJsonObject& payload);
void remoteDiagnosticsCheckClientStatus(QObject* context, std::function<void(const QJsonObject&)> callback);
bool remoteDiagnosticsEnabled();
QString remoteDiagnosticsSessionId();
QString remoteDiagnosticsClientId();
void remoteDiagnosticsShutdown(bool flushPending = true);
