#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QTcpServer>

#include <functional>

class QTcpSocket;

class SdrTownControlServer : public QObject
{
public:
    struct Config {
        quint16 port = 8765;
        QString token;
        bool allowUnauthenticated = true;
    };

    using RequestHandler = std::function<QJsonObject(const QString& method,
                                                     const QString& path,
                                                     const QJsonObject& body)>;

    explicit SdrTownControlServer(QObject* parent = nullptr);

    bool start(const Config& config, QString* error = nullptr);
    void stop();

    bool running() const noexcept;
    quint16 port() const noexcept;

    void setRequestHandler(RequestHandler handler);

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
};
