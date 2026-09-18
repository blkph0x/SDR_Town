#include "SdrTownControlServer.h"

#include <QHostAddress>
#include <QJsonDocument>
#include <QTcpSocket>

#include <algorithm>

namespace {

QString headerValue(const QJsonObject& headers, const QString& key)
{
    return headers.value(key.toLower()).toString();
}

int statusCodeFromBody(const QJsonObject& body, int fallback)
{
    const int code = body.value(QStringLiteral("status")).toInt(fallback);
    return std::clamp(code, 100, 599);
}

const char* reasonForStatus(int status)
{
    switch (status) {
        case 200: return "OK";
        case 204: return "No Content";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 413: return "Payload Too Large";
        case 500: return "Internal Server Error";
        case 503: return "Service Unavailable";
        default: return "Error";
    }
}

} // namespace

SdrTownControlServer::SdrTownControlServer(QObject* parent)
    : QObject(parent)
{
    connect(&m_server, &QTcpServer::newConnection, this, [this]() {
        handleIncomingConnection();
    });
}

bool SdrTownControlServer::start(const Config& config, QString* error)
{
    stop();
    m_config = config;
    if (!m_config.allowUnauthenticated && m_config.token.trimmed().isEmpty()) {
        if (error) *error = QStringLiteral("control token is required when unauthenticated control is disabled");
        return false;
    }
    if (!m_server.listen(QHostAddress::LocalHost, m_config.port)) {
        if (error) *error = m_server.errorString();
        return false;
    }
    return true;
}

void SdrTownControlServer::stop()
{
    if (m_server.isListening()) m_server.close();
}

bool SdrTownControlServer::running() const noexcept
{
    return m_server.isListening();
}

quint16 SdrTownControlServer::port() const noexcept
{
    return m_server.serverPort();
}

void SdrTownControlServer::setRequestHandler(RequestHandler handler)
{
    m_handler = std::move(handler);
}

void SdrTownControlServer::handleIncomingConnection()
{
    while (QTcpSocket* socket = m_server.nextPendingConnection()) {
        socket->setParent(this);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
            handleSocketReadyRead(socket);
        });
        connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
    }
}

void SdrTownControlServer::handleSocketReadyRead(QTcpSocket* socket)
{
    if (!socket) return;
    QByteArray buffer = socket->property("sdrTownRequestBuffer").toByteArray();
    buffer += socket->readAll();
    if (buffer.size() > 65536) {
        socket->write(makeHttpResponse(413, "Payload Too Large",
                                       errorResponse(QStringLiteral("request too large"))));
        socket->disconnectFromHost();
        return;
    }

    const int headerEnd = buffer.indexOf("\r\n\r\n");
    if (headerEnd < 0) {
        socket->setProperty("sdrTownRequestBuffer", buffer);
        return;
    }

    const QByteArray headerBytes = buffer.left(headerEnd);
    const QList<QByteArray> headerLines = headerBytes.split('\n');
    if (headerLines.isEmpty()) {
        socket->write(makeHttpResponse(400, "Bad Request",
                                       errorResponse(QStringLiteral("missing request line"))));
        socket->disconnectFromHost();
        return;
    }

    QList<QByteArray> requestParts = headerLines.front().trimmed().split(' ');
    if (requestParts.size() < 2) {
        socket->write(makeHttpResponse(400, "Bad Request",
                                       errorResponse(QStringLiteral("invalid request line"))));
        socket->disconnectFromHost();
        return;
    }

    const QString method = QString::fromLatin1(requestParts[0]).toUpper();
    QString path = QString::fromUtf8(requestParts[1]);
    const int queryAt = path.indexOf('?');
    if (queryAt >= 0) path.truncate(queryAt);

    QJsonObject headers;
    int contentLength = 0;
    for (int i = 1; i < headerLines.size(); ++i) {
        const QByteArray line = headerLines[i].trimmed();
        const int colon = line.indexOf(':');
        if (colon <= 0) continue;
        const QString key = QString::fromLatin1(line.left(colon)).trimmed().toLower();
        const QString value = QString::fromUtf8(line.mid(colon + 1)).trimmed();
        headers.insert(key, value);
        if (key == QStringLiteral("content-length")) {
            contentLength = std::clamp(value.toInt(), 0, 65536);
        }
    }

    const int totalNeeded = headerEnd + 4 + contentLength;
    if (buffer.size() < totalNeeded) {
        socket->setProperty("sdrTownRequestBuffer", buffer);
        return;
    }

    const QByteArray bodyBytes = buffer.mid(headerEnd + 4, contentLength);
    QJsonObject body;
    if (!bodyBytes.trimmed().isEmpty()) {
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(bodyBytes, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            socket->write(makeHttpResponse(400, "Bad Request",
                                           errorResponse(QStringLiteral("request body must be a JSON object"))));
            socket->disconnectFromHost();
            return;
        }
        body = doc.object();
    }

    QJsonObject response;
    int status = 200;
    if (path != QStringLiteral("/v1/health") && !requestAuthorized(headers)) {
        response = errorResponse(QStringLiteral("unauthorized"));
        response.insert(QStringLiteral("status"), 401);
        status = 401;
    } else if (!m_handler) {
        response = errorResponse(QStringLiteral("control handler is not installed"));
        response.insert(QStringLiteral("status"), 503);
        status = 503;
    } else {
        try {
            response = m_handler(method, path, body);
            status = statusCodeFromBody(response, 200);
        } catch (const std::exception& ex) {
            response = errorResponse(QStringLiteral("handler exception: %1").arg(QString::fromLocal8Bit(ex.what())));
            response.insert(QStringLiteral("status"), 500);
            status = 500;
        } catch (...) {
            response = errorResponse(QStringLiteral("handler exception"));
            response.insert(QStringLiteral("status"), 500);
            status = 500;
        }
    }

    socket->write(makeHttpResponse(status, reasonForStatus(status), response));
    socket->disconnectFromHost();
}

bool SdrTownControlServer::requestAuthorized(const QJsonObject& headers) const
{
    if (m_config.allowUnauthenticated || m_config.token.isEmpty()) return true;
    const QString bearer = headerValue(headers, QStringLiteral("authorization"));
    if (bearer.startsWith(QStringLiteral("Bearer "), Qt::CaseInsensitive) &&
        bearer.mid(7).trimmed() == m_config.token) {
        return true;
    }
    return headerValue(headers, QStringLiteral("x-sdrtown-token")) == m_config.token;
}

QByteArray SdrTownControlServer::makeHttpResponse(int status,
                                                  const char* reason,
                                                  const QJsonObject& body)
{
    const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
    QByteArray response;
    response += "HTTP/1.1 " + QByteArray::number(status) + " " + QByteArray(reason) + "\r\n";
    response += "Content-Type: application/json\r\n";
    response += "Cache-Control: no-store\r\n";
    response += "Access-Control-Allow-Origin: http://127.0.0.1\r\n";
    response += "Connection: close\r\n";
    response += "Content-Length: " + QByteArray::number(payload.size()) + "\r\n\r\n";
    response += payload;
    return response;
}

QJsonObject SdrTownControlServer::errorResponse(const QString& message)
{
    QJsonObject response;
    response.insert(QStringLiteral("ok"), false);
    response.insert(QStringLiteral("error"), message);
    return response;
}
