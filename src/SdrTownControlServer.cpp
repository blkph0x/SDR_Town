#include "SdrTownControlServer.h"
#include <QTimer>
#include <QPointer>

#include <QHostAddress>
#include <QJsonDocument>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>

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

bool tokensEqualConstantTime(const QByteArray& a, const QByteArray& b)
{
    const qsizetype n = std::max(a.size(), b.size());
    unsigned int acc = static_cast<unsigned int>(a.size()) ^
                       static_cast<unsigned int>(b.size());
    for (qsizetype i = 0; i < n; ++i) {
        const unsigned char av = i < a.size() ? static_cast<unsigned char>(a.at(i)) : 0;
        const unsigned char bv = i < b.size() ? static_cast<unsigned char>(b.at(i)) : 0;
        acc |= static_cast<unsigned int>(av ^ bv);
    }
    return acc == 0;
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
    m_config.token = m_config.token.trimmed();
    if (config.maxConnections < 1 || config.maxConnections > 128 ||
        config.requestTimeoutMs < 10 || config.requestTimeoutMs > 60000) {
        if (error) *error = QStringLiteral("invalid control connection limits");
        return false;
    }
    if (!m_config.allowUnauthenticated && m_config.token.isEmpty()) {
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
    const auto clients = m_clients;
    m_clients.clear();
    for (auto* socket : clients) {
        socket->setProperty("handled", true);
        socket->abort();
        socket->deleteLater();
    }
}

SdrTownControlServer::~SdrTownControlServer() { stop(); }

QJsonObject SdrTownControlServer::statistics() const {
    return {{"activeConnections", m_clients.size()}, {"accepted", double(m_accepted)},
        {"rejected", double(m_rejected)}, {"timeouts", double(m_timeouts)},
        {"dispatched", double(m_dispatched)}, {"handlerErrors", double(m_handlerErrors)}};
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
        if (m_clients.size() >= m_config.maxConnections) {
            ++m_rejected;
            socket->abort(); socket->deleteLater();
            continue;
        }
        ++m_accepted;
        m_clients.insert(socket);
        socket->setReadBufferSize(65537);
        // Absolute lifetime also bounds a peer that trickles headers or never
        // reads its response. ReadyRead cannot extend this deadline (DEC-0171).
        QTimer::singleShot(m_config.requestTimeoutMs, socket, [this, socket] {
            if (socket->state() != QAbstractSocket::UnconnectedState) {
                ++m_timeouts; socket->setProperty("handled", true); socket->abort();
            }
        });
        // DEC-0173: handlers may stop the server and flush deferred deletion.
        // Leave Qt's native read notification before invoking application code.
        const auto dispatch = [this, alive = QPointer<QTcpSocket>(socket)] {
            if (alive) handleSocketReadyRead(alive.data());
        };
        connect(socket, &QTcpSocket::readyRead, this, dispatch, Qt::QueuedConnection);
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
            m_clients.remove(socket); socket->deleteLater();
        });
        if (socket->bytesAvailable()) QMetaObject::invokeMethod(this, dispatch, Qt::QueuedConnection);
    }
}

void SdrTownControlServer::handleSocketReadyRead(QTcpSocket* socket)
{
    if (!socket || !running() || socket->property("handled").toBool()) return;
    const auto reject = [this, socket](int status, const char* reason, const QString& message) {
        ++m_rejected;
        socket->setProperty("handled", true);
        socket->write(makeHttpResponse(status, reason, errorResponse(message)));
        socket->disconnectFromHost();
    };
    QByteArray buffer = socket->property("sdrTownRequestBuffer").toByteArray();
    buffer += socket->read(65537 - buffer.size());
    if (buffer.size() > 65536) {
        reject(413, "Payload Too Large", QStringLiteral("request too large"));
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
    if (requestParts.size() != 3 || !requestParts[1].startsWith('/') ||
        (requestParts[2] != "HTTP/1.1" && requestParts[2] != "HTTP/1.0")) {
        reject(400, "Bad Request", QStringLiteral("invalid request line"));
        return;
    }

    const QString method = QString::fromLatin1(requestParts[0]).toUpper();
    QString path = QString::fromUtf8(requestParts[1]);
    QJsonObject queryParams;
    const int queryAt = path.indexOf('?');
    if (queryAt >= 0) {
        const QUrl url(QStringLiteral("http://local") + path);
        QUrlQuery uq(url);
        const auto items = uq.queryItems();
        for (const auto& item : items)
            queryParams.insert(item.first, item.second);
        path.truncate(queryAt);
    }

    QJsonObject headers;
    int contentLength = 0;
    for (int i = 1; i < headerLines.size(); ++i) {
        const QByteArray line = headerLines[i].trimmed();
        const int colon = line.indexOf(':');
        if (colon <= 0 || headerLines[i].startsWith(' ') || headerLines[i].startsWith('\t')) {
            reject(400, "Bad Request", QStringLiteral("invalid header")); return;
        }
        const QString key = QString::fromLatin1(line.left(colon)).trimmed().toLower();
        const QString value = QString::fromUtf8(line.mid(colon + 1)).trimmed();
        if (headers.contains(key) || key == QStringLiteral("transfer-encoding")) {
            reject(400, "Bad Request", QStringLiteral("ambiguous or unsupported framing")); return;
        }
        headers.insert(key, value);
        if (key == QStringLiteral("content-length")) {
            bool ok = false;
            const auto length = value.toULongLong(&ok);
            const bool digits = !value.isEmpty() && std::all_of(value.begin(), value.end(),
                [](QChar c) { return c >= QChar('0') && c <= QChar('9'); });
            if (!ok || !digits) { reject(400, "Bad Request", QStringLiteral("invalid content length")); return; }
            if (length > 65536u) { reject(413, "Payload Too Large", QStringLiteral("request too large")); return; }
            contentLength = static_cast<int>(length);
        }
    }

    const int totalNeeded = headerEnd + 4 + contentLength;
    if (totalNeeded > 65536) { reject(413, "Payload Too Large", QStringLiteral("request too large")); return; }
    if (path != QStringLiteral("/v1/health") && !requestAuthorized(headers)) {
        reject(401, "Unauthorized", QStringLiteral("unauthorized")); return;
    }
    if (buffer.size() < totalNeeded) {
        socket->setProperty("sdrTownRequestBuffer", buffer);
        return;
    }

    const QByteArray bodyBytes = buffer.mid(headerEnd + 4, contentLength);
    // Exactly one command per connection, including reentrant handlers.
    socket->setProperty("handled", true);
    socket->setProperty("sdrTownRequestBuffer", QVariant());
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
    for (auto it = queryParams.begin(); it != queryParams.end(); ++it) {
        if (!body.contains(it.key())) body.insert(it.key(), it.value());
    }

    QJsonObject response;
    int status = 200;
    const QPointer<QTcpSocket> alive(socket);
    if (!m_handler) {
        response = errorResponse(QStringLiteral("control handler is not installed"));
        response.insert(QStringLiteral("status"), 503);
        status = 503;
    } else {
        try {
            ++m_dispatched;
            response = m_handler(method, path, body);
            status = statusCodeFromBody(response, 200);
        } catch (const std::exception&) {
            ++m_handlerErrors;
            response = errorResponse(QStringLiteral("control operation failed"));
            response.insert(QStringLiteral("status"), 500);
            status = 500;
        } catch (...) {
            ++m_handlerErrors;
            response = errorResponse(QStringLiteral("handler exception"));
            response.insert(QStringLiteral("status"), 500);
            status = 500;
        }
    }

    // A handler may stop the server while processing a nested GUI event, even
    // if it subsequently throws. Never reply through a retired connection.
    if (!alive || !running() || socket->state() == QAbstractSocket::UnconnectedState) return;
    socket->write(makeHttpResponse(status, reasonForStatus(status), response));
    socket->disconnectFromHost();
}

bool SdrTownControlServer::requestAuthorized(const QJsonObject& headers) const
{
    // DEC-0206: empty token is unauthorized. Always compare both presented
    // headers so length/value mismatch does not short-circuit.
    if (m_config.allowUnauthenticated)
        return true;
    const QByteArray expected = m_config.token.toUtf8();
    if (expected.isEmpty())
        return false;

    QString auth = headerValue(headers, QStringLiteral("authorization"));
    QByteArray bearer;
    if (auth.startsWith(QStringLiteral("Bearer "), Qt::CaseInsensitive))
        bearer = auth.mid(7).trimmed().toUtf8();
    const QByteArray headerTok =
        headerValue(headers, QStringLiteral("x-sdrtown-token")).toUtf8();
    const bool bearerOk = tokensEqualConstantTime(expected, bearer);
    const bool headerOk = tokensEqualConstantTime(expected, headerTok);
    return bearerOk || headerOk;
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
