#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>

struct RemoteDiagnosticsConfig {
    bool enabled = false;
    QUrl endpoint;
    QString bearerToken;
    QString mode = "unknown";
    QString sessionId;
    QString configSource;
    int maxBytesPerMinute = 64 * 1024;
    int maxPayloadBytes = 16 * 1024;
    int minIntervalMs = 1000;
    int maxQueue = 128;
};

class RemoteDiagnosticsClient : public QObject {
public:
    explicit RemoteDiagnosticsClient(QObject* parent = nullptr);

    void configure(const RemoteDiagnosticsConfig& cfg);
    bool enabled() const;
    QString sessionId() const;
    void submit(QString type, QString severity, QJsonObject payload);
    void flushNow();
    void drainForMs(int maxMs);

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
    QString m_clientId;
};

RemoteDiagnosticsConfig remoteDiagnosticsConfigFromProcess(int argc, char* argv[], const QString& mode);
RemoteDiagnosticsClient* remoteDiagnosticsConfigureFromProcess(int argc, char* argv[], QObject* parent, const QString& mode);
void remoteDiagnosticsSubmit(const QString& type, const QString& severity, const QJsonObject& payload);
bool remoteDiagnosticsEnabled();
QString remoteDiagnosticsSessionId();
void remoteDiagnosticsShutdown();
