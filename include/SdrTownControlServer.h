#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QTcpServer>
#include <QSet>

#include <functional>

class QTcpSocket;

class SdrTownControlServer : public QObject
{
public:
    struct Config {
        quint16 port = 8765;
        QString token;
        bool allowUnauthenticated = true;
        // DEC-0171: loopback resource limits, independent of radio timing.
        int maxConnections = 16;
        int requestTimeoutMs = 10000;
    };

    using RequestHandler = std::function<QJsonObject(const QString& method,
                                                     const QString& path,
                                                     const QJsonObject& body)>;

    explicit SdrTownControlServer(QObject* parent = nullptr);
    ~SdrTownControlServer() override;

    bool start(const Config& config, QString* error = nullptr);
    void stop();

    bool running() const noexcept;
    quint16 port() const noexcept;

    void setRequestHandler(RequestHandler handler);
    QJsonObject statistics() const;

private:
    void handleIncomingConnection();
    void handleSocketReadyRead(QTcpSocket* socket);
    bool requestAuthorized(const QJsonObject& headers) const;

    static QByteArray makeHttpResponse(int status,
                                       const char* reason,
                                       const QJsonObject& body);
    static QJsonObject errorResponse(const QString& message);

    QTcpServer m_server;
    Config m_config;
    RequestHandler m_handler;
    QSet<QTcpSocket*> m_clients;
    quint64 m_accepted = 0, m_rejected = 0, m_timeouts = 0;
    quint64 m_dispatched = 0, m_handlerErrors = 0;
};
