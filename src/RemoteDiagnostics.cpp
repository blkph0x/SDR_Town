#include "RemoteDiagnostics.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>
#include <QSysInfo>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <QUrlQuery>

#include <algorithm>

#ifndef SDR_TOWN_VERSION
#define SDR_TOWN_VERSION "0.0.0"
#endif

namespace {
QPointer<RemoteDiagnosticsClient> g_remoteDiagnosticsClient;
QPointer<QThread> g_remoteDiagnosticsThread;
QMutex g_remoteDiagnosticsMutex;
bool g_remoteDiagnosticsEnabled = false;
QString g_remoteDiagnosticsSessionId;
QString g_remoteDiagnosticsClientId;

QString envString(const char* name)
{
    return QString::fromLocal8Bit(qgetenv(name)).trimmed();
}

int parsePositiveInt(const QString& text, int fallback, int minimum, int maximum)
{
    bool ok = false;
    const int value = text.trimmed().toInt(&ok);
    if (!ok) return fallback;
    return std::clamp(value, minimum, maximum);
}

QString argValue(int& i, int argc, char* argv[], const QString& arg)
{
    const int eq = arg.indexOf('=');
    if (eq >= 0) return arg.mid(eq + 1);
    if (i + 1 < argc && argv[i + 1]) {
        const QString next = QString::fromLocal8Bit(argv[i + 1]);
        if (!next.startsWith('-')) {
            ++i;
            return next;
        }
    }
    return {};
}

bool endpointLooksUsable(const QUrl& url)
{
    return url.isValid() &&
        (url.scheme().compare("http", Qt::CaseInsensitive) == 0 ||
         url.scheme().compare("https", Qt::CaseInsensitive) == 0) &&
        !url.host().isEmpty();
}

bool jsonBool(const QJsonObject& obj, const QString& key, bool fallback)
{
    const QJsonValue value = obj.value(key);
    if (value.isBool()) return value.toBool();
    if (value.isString()) {
        const QString text = value.toString().trimmed().toLower();
        if (text == "1" || text == "true" || text == "yes" || text == "on") return true;
        if (text == "0" || text == "false" || text == "no" || text == "off") return false;
    }
    return fallback;
}

int jsonInt(const QJsonObject& obj, const QString& key, int fallback, int minimum, int maximum)
{
    const QJsonValue value = obj.value(key);
    if (value.isDouble()) return std::clamp(value.toInt(fallback), minimum, maximum);
    if (value.isString()) return parsePositiveInt(value.toString(), fallback, minimum, maximum);
    return fallback;
}

bool readJsonObjectFile(const QString& path, QJsonObject* out)
{
    if (!out || path.trimmed().isEmpty()) return false;
    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) return false;
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return false;
    *out = doc.object();
    return true;
}

void applyDiagnosticsJsonConfig(RemoteDiagnosticsConfig& cfg,
                                const QJsonObject& obj,
                                const QString& source,
                                bool* requested)
{
    const bool enabled = jsonBool(obj, "enabled", false);
    if (enabled && requested) *requested = true;

    const QString url = obj.value("url").toString(obj.value("endpoint").toString()).trimmed();
    if (!url.isEmpty()) cfg.endpoint = QUrl(url);

    const QString token = obj.value("token").toString(obj.value("bearerToken").toString()).trimmed();
    if (!token.isEmpty()) cfg.bearerToken = token;

    cfg.maxBytesPerMinute = jsonInt(obj, "maxBytesPerMinute", cfg.maxBytesPerMinute, 4096, 1024 * 1024);
    cfg.maxPayloadBytes = jsonInt(obj, "maxPayloadBytes", cfg.maxPayloadBytes, 2048, 128 * 1024);
    cfg.minIntervalMs = jsonInt(obj, "minIntervalMs", cfg.minIntervalMs, 100, 60 * 1000);
    cfg.maxQueue = jsonInt(obj, "maxQueue", cfg.maxQueue, 4, 1024);
    if (!source.isEmpty()) cfg.configSource = source;
}

QStringList defaultDiagnosticsConfigPaths()
{
    QStringList paths;
    if (QCoreApplication::instance()) {
        paths << QCoreApplication::applicationDirPath() + "/remote_diagnostics.json";
    }
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!appData.isEmpty()) {
        paths << appData + "/remote_diagnostics.json";
    }
    const QString roaming = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (!roaming.isEmpty() && roaming != appData) {
        paths << roaming + "/remote_diagnostics.json";
    }
    paths.removeDuplicates();
    return paths;
}

QJsonObject sanitizedPayload(QJsonObject payload)
{
    // Remote diagnostics are for compact state, never bulk captures.
    payload.remove("iq");
    payload.remove("iqSamples");
    payload.remove("audio");
    payload.remove("pcm");
    payload.remove("samples");
    payload.remove("rawSymbols");
    payload.remove("rawDibits");
    return payload;
}

QString computeHardwareHashMaterial()
{
    QStringList parts;
    parts << QString::fromUtf8(QSysInfo::machineUniqueId().toHex());
    parts << QSysInfo::currentCpuArchitecture();
    parts << QSysInfo::kernelType();
    parts << QSysInfo::productType();
    parts << QSysInfo::productVersion();
    parts << QSysInfo::machineHostName();
    parts.removeAll(QString());
    return parts.join('|');
}

QString ensureHardwareHash()
{
    QSettings settings;
    QString existing = settings.value("remoteDiagnostics/hardwareHash").toString().trimmed();
    if (!existing.isEmpty()) return existing;

    QString material = computeHardwareHashMaterial();
    if (material.trimmed().isEmpty()) {
        material = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    existing = QString::fromLatin1(QCryptographicHash::hash(material.toUtf8(), QCryptographicHash::Sha256).toHex()).left(32);
    settings.setValue("remoteDiagnostics/hardwareHash", existing);
    return existing;
}

QUrl clientStatusUrlForEndpoint(const QUrl& endpoint, const QString& clientId)
{
    QUrl url(endpoint);
    QString path = url.path();
    if (path.endsWith(QStringLiteral("/ingest"))) {
        path.chop(QStringLiteral("/ingest").size());
    }
    if (path.isEmpty()) path = QStringLiteral("/");
    if (!path.endsWith('/')) path += '/';
    path += QStringLiteral("client-status");
    url.setPath(path);
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("clientId"), clientId);
    query.addQueryItem(QStringLiteral("version"), QStringLiteral(SDR_TOWN_VERSION));
    url.setQuery(query);
    return url;
}
}

RemoteDiagnosticsClient::RemoteDiagnosticsClient(QObject* parent)
    : QObject(parent)
{
}

void RemoteDiagnosticsClient::configure(const RemoteDiagnosticsConfig& cfg)
{
    if (!m_network) m_network = new QNetworkAccessManager(this);
    if (!m_timer) {
        m_timer = new QTimer(this);
        m_timer->setSingleShot(true);
        connect(m_timer, &QTimer::timeout, this, [this]() { pump(); });
    }
    m_cfg = cfg;
    if (m_cfg.sessionId.trimmed().isEmpty()) {
        m_cfg.sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    m_cfg.maxBytesPerMinute = std::clamp(m_cfg.maxBytesPerMinute, 4096, 1024 * 1024);
    m_cfg.maxPayloadBytes = std::clamp(m_cfg.maxPayloadBytes, 2048, 128 * 1024);
    m_cfg.minIntervalMs = std::clamp(m_cfg.minIntervalMs, 100, 60 * 1000);
    m_cfg.maxQueue = std::clamp(m_cfg.maxQueue, 4, 1024);
    m_clientId = ensureClientId();
    m_hardwareHash = ensureHardwareHash();
    m_windowStartMs = QDateTime::currentMSecsSinceEpoch();
}

bool RemoteDiagnosticsClient::enabled() const
{
    return m_cfg.enabled && endpointLooksUsable(m_cfg.endpoint);
}

QString RemoteDiagnosticsClient::sessionId() const
{
    return m_cfg.sessionId;
}

QString RemoteDiagnosticsClient::clientId() const
{
    return m_clientId;
}

QString RemoteDiagnosticsClient::hardwareHash() const
{
    return m_hardwareHash;
}

QString RemoteDiagnosticsClient::ensureClientId()
{
    QSettings settings;
    QString id = settings.value("remoteDiagnostics/clientId").toString().trimmed();
    if (id.isEmpty()) {
        id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        settings.setValue("remoteDiagnostics/clientId", id);
    }
    return id;
}

void RemoteDiagnosticsClient::submit(QString type, QString severity, QJsonObject payload)
{
    if (QThread::currentThread() != thread()) {
        const QPointer<RemoteDiagnosticsClient> self(this);
        QMetaObject::invokeMethod(this, [self, type = std::move(type), severity = std::move(severity), payload = std::move(payload)]() mutable {
            if (self) self->submitOnOwnerThread(std::move(type), std::move(severity), std::move(payload));
        }, Qt::QueuedConnection);
        return;
    }
    submitOnOwnerThread(std::move(type), std::move(severity), std::move(payload));
}

void RemoteDiagnosticsClient::submitOnOwnerThread(QString type, QString severity, QJsonObject payload)
{
    if (!enabled()) return;

    payload = sanitizedPayload(std::move(payload));
    const quint64 nextSeq = ++m_seq;

    QJsonObject envelope;
    envelope["schema"] = "sdr-town-remote-diagnostics-v1";
    envelope["app"] = "SDR_Town";
    envelope["version"] = SDR_TOWN_VERSION;
    envelope["mode"] = m_cfg.mode;
    envelope["sessionId"] = m_cfg.sessionId;
    envelope["clientId"] = m_clientId;
    envelope["installId"] = m_clientId;
    envelope["hardwareHash"] = m_hardwareHash;
    envelope["seq"] = QString::number(nextSeq);
    envelope["timeUtc"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    envelope["type"] = type.left(80);
    envelope["severity"] = severity.left(24);
    envelope["payload"] = payload;
    envelope["queueDropped"] = QString::number(m_queueDropped);
    envelope["budgetDropped"] = QString::number(m_budgetDropped);
    envelope["networkDropped"] = QString::number(m_networkDropped);
    envelope["oversizeDropped"] = QString::number(m_oversizeDropped);

    const QByteArray body = QJsonDocument(envelope).toJson(QJsonDocument::Compact);
    if (body.size() > m_cfg.maxPayloadBytes) {
        ++m_oversizeDropped;
        return;
    }

    while (m_queue.size() >= m_cfg.maxQueue) {
        m_queue.removeFirst();
        ++m_queueDropped;
    }
    PendingEvent pending;
    pending.body = body;
    pending.bytes = body.size();
    m_queue.push_back(std::move(pending));
    schedulePump();
}

void RemoteDiagnosticsClient::checkClientStatus(QObject* context, std::function<void(const QJsonObject&)> callback)
{
    if (!enabled() || m_clientId.isEmpty() || !callback) return;
    if (QThread::currentThread() != thread()) {
        const QPointer<RemoteDiagnosticsClient> self(this);
        QPointer<QObject> ctx(context);
        QMetaObject::invokeMethod(this, [self, ctx, callback = std::move(callback)]() mutable {
            if (self) self->checkClientStatus(ctx, std::move(callback));
        }, Qt::QueuedConnection);
        return;
    }

    QNetworkRequest request(clientStatusUrlForEndpoint(m_cfg.endpoint, m_clientId));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("User-Agent", QByteArray("SDR_Town/") + QByteArray(SDR_TOWN_VERSION));
    if (!m_cfg.bearerToken.trimmed().isEmpty()) {
        request.setRawHeader("Authorization", QByteArray("Bearer ") + m_cfg.bearerToken.toUtf8());
    }

    QPointer<QObject> ctx(context);
    QNetworkReply* reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [reply, ctx, callback = std::move(callback)]() mutable {
        QJsonObject result;
        result["ok"] = false;
        if (reply->error() == QNetworkReply::NoError) {
            QJsonParseError err{};
            const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &err);
            if (err.error == QJsonParseError::NoError && doc.isObject()) {
                result = doc.object();
            } else {
                result["error"] = "invalid-json";
            }
        } else {
            result["error"] = reply->errorString();
        }
        reply->deleteLater();
        if (ctx) {
            QMetaObject::invokeMethod(ctx, [callback = std::move(callback), result]() mutable {
                callback(result);
            }, Qt::QueuedConnection);
        }
    });
}

void RemoteDiagnosticsClient::flushNow()
{
    if (!enabled()) return;
    pump();
}

void RemoteDiagnosticsClient::drainForMs(int maxMs)
{
    if (!enabled()) return;
    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + std::clamp(maxMs, 0, 5000);
    while ((!m_queue.isEmpty() || m_inFlight) && QDateTime::currentMSecsSinceEpoch() < deadline) {
        pump();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(10);
    }
}

void RemoteDiagnosticsClient::schedulePump(int delayMs)
{
    if (!m_timer) return;
    if (m_timer->isActive() && delayMs > 0) return;
    m_timer->start(std::max(0, delayMs));
}

void RemoteDiagnosticsClient::pump()
{
    if (!enabled() || m_inFlight || m_queue.isEmpty()) return;

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (m_windowStartMs <= 0 || nowMs - m_windowStartMs >= 60 * 1000) {
        m_windowStartMs = nowMs;
        m_windowBytes = 0;
    }

    const qint64 elapsedSinceSend = nowMs - m_lastSendMs;
    if (m_lastSendMs > 0 && elapsedSinceSend < m_cfg.minIntervalMs) {
        schedulePump(static_cast<int>(m_cfg.minIntervalMs - elapsedSinceSend));
        return;
    }

    if (m_windowBytes + m_queue.front().bytes > m_cfg.maxBytesPerMinute) {
        schedulePump(static_cast<int>(std::max<qint64>(250, 60 * 1000 - (nowMs - m_windowStartMs))));
        ++m_budgetDropped;
        while (m_queue.size() > std::max(1, m_cfg.maxQueue / 2)) {
            m_queue.removeFirst();
            ++m_queueDropped;
        }
        return;
    }

    PendingEvent pending = std::move(m_queue.front());
    m_queue.removeFirst();
    m_windowBytes += pending.bytes;
    m_lastSendMs = nowMs;
    m_inFlight = true;

    QNetworkRequest request(m_cfg.endpoint);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("User-Agent", QByteArray("SDR_Town/") + QByteArray(SDR_TOWN_VERSION));
    if (!m_cfg.bearerToken.trimmed().isEmpty()) {
        request.setRawHeader("Authorization", QByteArray("Bearer ") + m_cfg.bearerToken.toUtf8());
    }

    QNetworkReply* reply = m_network->post(request, pending.body);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() != QNetworkReply::NoError) ++m_networkDropped;
        reply->deleteLater();
        m_inFlight = false;
        if (!m_queue.isEmpty()) schedulePump(m_cfg.minIntervalMs);
    });
}

RemoteDiagnosticsConfig remoteDiagnosticsConfigFromProcess(int argc, char* argv[], const QString& mode)
{
    RemoteDiagnosticsConfig cfg;
    cfg.mode = mode;
    bool requested = false;
    bool forcedOff = false;

    auto applyConfigPath = [&](const QString& path) {
        QJsonObject obj;
        if (readJsonObjectFile(path, &obj)) {
            applyDiagnosticsJsonConfig(cfg, obj, QFileInfo(path).absoluteFilePath(), &requested);
        }
    };
    for (const QString& path : defaultDiagnosticsConfigPaths()) {
        applyConfigPath(path);
    }
    const QString envConfig = envString("SDR_TOWN_DIAG_CONFIG");
    if (!envConfig.isEmpty()) {
        applyConfigPath(envConfig);
    }

    const QString envUrl = envString("SDR_TOWN_DIAG_URL");
    if (!envUrl.isEmpty()) {
        cfg.endpoint = QUrl(envUrl);
        requested = true;
        cfg.configSource = "env:SDR_TOWN_DIAG_URL";
    }
    const QString envToken = envString("SDR_TOWN_DIAG_TOKEN");
    if (!envToken.isEmpty()) cfg.bearerToken = envToken;
    cfg.maxBytesPerMinute = parsePositiveInt(envString("SDR_TOWN_DIAG_MAX_BYTES_PER_MIN"), cfg.maxBytesPerMinute, 4096, 1024 * 1024);
    cfg.maxPayloadBytes = parsePositiveInt(envString("SDR_TOWN_DIAG_MAX_PAYLOAD_BYTES"), cfg.maxPayloadBytes, 2048, 128 * 1024);
    cfg.minIntervalMs = parsePositiveInt(envString("SDR_TOWN_DIAG_MIN_INTERVAL_MS"), cfg.minIntervalMs, 100, 60 * 1000);
    cfg.maxQueue = parsePositiveInt(envString("SDR_TOWN_DIAG_MAX_QUEUE"), cfg.maxQueue, 4, 1024);

    for (int i = 1; i < argc; ++i) {
        if (!argv[i]) continue;
        const QString arg = QString::fromLocal8Bit(argv[i]);
        QString key = arg;
        const int eq = key.indexOf('=');
        if (eq >= 0) key = key.left(eq);
        key = key.toLower();

        if (key == "--diag-config" || key == "--diagnostics-config" || key == "--remote-diagnostics-config") {
            applyConfigPath(argValue(i, argc, argv, arg).trimmed());
        } else if (key == "--diag-url" || key == "--diagnostics-url" || key == "--remote-diagnostics-url") {
            cfg.endpoint = QUrl(argValue(i, argc, argv, arg).trimmed());
            requested = true;
            cfg.configSource = "argv:" + key;
        } else if (key == "--diag-token" || key == "--diagnostics-token" || key == "--remote-diagnostics-token") {
            cfg.bearerToken = argValue(i, argc, argv, arg).trimmed();
        } else if (key == "--diag-max-bytes-per-min" || key == "--diagnostics-max-bytes-per-min") {
            cfg.maxBytesPerMinute = parsePositiveInt(argValue(i, argc, argv, arg), cfg.maxBytesPerMinute, 4096, 1024 * 1024);
        } else if (key == "--diag-max-payload" || key == "--diagnostics-max-payload") {
            cfg.maxPayloadBytes = parsePositiveInt(argValue(i, argc, argv, arg), cfg.maxPayloadBytes, 2048, 128 * 1024);
        } else if (key == "--diag-min-ms" || key == "--diagnostics-min-ms") {
            cfg.minIntervalMs = parsePositiveInt(argValue(i, argc, argv, arg), cfg.minIntervalMs, 100, 60 * 1000);
        } else if (key == "--diag-queue" || key == "--diagnostics-queue") {
            cfg.maxQueue = parsePositiveInt(argValue(i, argc, argv, arg), cfg.maxQueue, 4, 1024);
        } else if (key == "--diag-off" || key == "--no-remote-diagnostics" || key == "--diagnostics-off") {
            forcedOff = true;
        }
    }

    cfg.enabled = !forcedOff && requested && endpointLooksUsable(cfg.endpoint);
    return cfg;
}

RemoteDiagnosticsClient* remoteDiagnosticsConfigureFromProcess(int argc, char* argv[], QObject* parent, const QString& mode)
{
    const RemoteDiagnosticsConfig cfg = remoteDiagnosticsConfigFromProcess(argc, argv, mode);
    if (!cfg.enabled) return nullptr;

    auto* thread = new QThread(parent);
    auto* client = new RemoteDiagnosticsClient();
    client->moveToThread(thread);
    QObject::connect(thread, &QThread::finished, client, &QObject::deleteLater);
    thread->start();
    QMetaObject::invokeMethod(client, [client, cfg]() {
        client->configure(cfg);
    }, Qt::BlockingQueuedConnection);
    QString configuredSessionId;
    QString configuredClientId;
    QString configuredHardwareHash;
    QMetaObject::invokeMethod(client, [client, &configuredSessionId]() {
        configuredSessionId = client->sessionId();
    }, Qt::BlockingQueuedConnection);
    QMetaObject::invokeMethod(client, [client, &configuredClientId, &configuredHardwareHash]() {
        configuredClientId = client->clientId();
        configuredHardwareHash = client->hardwareHash();
    }, Qt::BlockingQueuedConnection);
    {
        QMutexLocker locker(&g_remoteDiagnosticsMutex);
        g_remoteDiagnosticsClient = client;
        g_remoteDiagnosticsThread = thread;
        g_remoteDiagnosticsEnabled = true;
        g_remoteDiagnosticsSessionId = configuredSessionId;
        g_remoteDiagnosticsClientId = configuredClientId;
    }

    QJsonObject payload;
    payload["endpointHost"] = cfg.endpoint.host();
    payload["endpointPath"] = cfg.endpoint.path().left(120);
    payload["configSource"] = cfg.configSource.left(240);
    payload["clientId"] = configuredClientId;
    payload["hardwareHash"] = configuredHardwareHash;
    payload["maxBytesPerMinute"] = cfg.maxBytesPerMinute;
    payload["maxPayloadBytes"] = cfg.maxPayloadBytes;
    payload["minIntervalMs"] = cfg.minIntervalMs;
    payload["maxQueue"] = cfg.maxQueue;
    client->submit("diagnostics.enabled", "info", payload);
    return client;
}

void remoteDiagnosticsSubmit(const QString& type, const QString& severity, const QJsonObject& payload)
{
    QPointer<RemoteDiagnosticsClient> client;
    {
        QMutexLocker locker(&g_remoteDiagnosticsMutex);
        client = g_remoteDiagnosticsClient;
    }
    if (!client) return;
    client->submit(type, severity, payload);
}

void remoteDiagnosticsCheckClientStatus(QObject* context, std::function<void(const QJsonObject&)> callback)
{
    QPointer<RemoteDiagnosticsClient> client;
    {
        QMutexLocker locker(&g_remoteDiagnosticsMutex);
        client = g_remoteDiagnosticsClient;
    }
    if (!client) return;
    client->checkClientStatus(context, std::move(callback));
}

bool remoteDiagnosticsEnabled()
{
    QMutexLocker locker(&g_remoteDiagnosticsMutex);
    return g_remoteDiagnosticsEnabled && !g_remoteDiagnosticsClient.isNull();
}

QString remoteDiagnosticsSessionId()
{
    QMutexLocker locker(&g_remoteDiagnosticsMutex);
    return g_remoteDiagnosticsSessionId;
}

QString remoteDiagnosticsClientId()
{
    QMutexLocker locker(&g_remoteDiagnosticsMutex);
    return g_remoteDiagnosticsClientId;
}

void remoteDiagnosticsShutdown()
{
    QPointer<RemoteDiagnosticsClient> client;
    QPointer<QThread> thread;
    {
        QMutexLocker locker(&g_remoteDiagnosticsMutex);
        client = g_remoteDiagnosticsClient;
        thread = g_remoteDiagnosticsThread;
        g_remoteDiagnosticsClient.clear();
        g_remoteDiagnosticsThread.clear();
        g_remoteDiagnosticsEnabled = false;
        g_remoteDiagnosticsSessionId.clear();
        g_remoteDiagnosticsClientId.clear();
    }
    if (client) {
        if (QThread::currentThread() == client->thread()) {
            client->drainForMs(1500);
        } else {
            QMetaObject::invokeMethod(client, [client]() {
                client->drainForMs(1500);
            }, Qt::BlockingQueuedConnection);
        }
    }
    if (thread) {
        thread->quit();
        thread->wait(2000);
        if (!thread->parent()) delete thread;
    }
}
