#include "RemoteDiagnostics.h"
#include "FmDiagnosticsLog.h"

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

// DEC-0160: routine decoder telemetry must not evict startup/control evidence.
int eventPriority(const QString& type, const QString& severity) {
    if(type=="app.system" || type=="app.start" || type=="diagnostics.enabled") return 3;
    if(type=="ui.actions" || type=="ui.intent" || type=="app.runtime" || type=="diagnostics.snapshot" ||
       severity=="error" || severity=="fatal" || severity=="critical") return 2;
    if(type=="app.performance.sample" || severity=="warn" || severity=="warning") return 1;
    return 0;
}

bool replaceableEvent(const QString& type, const QString& severity) {
    return eventPriority(type,severity)==0 || type=="app.performance.sample";
}

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

bool diagnosticsConfigUsable(const RemoteDiagnosticsConfig& cfg)
{
    if (!endpointLooksUsable(cfg.endpoint)) return false;
    if (!cfg.bearerToken.trimmed().isEmpty() &&
        cfg.endpoint.scheme().compare("https", Qt::CaseInsensitive) != 0) {
        return false;
    }
    return true;
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
    if (obj.contains("enabled") && requested) *requested = jsonBool(obj,"enabled",false);

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
        paths << QCoreApplication::applicationDirPath() + "/remote_diagnostics.defaults.json";
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
    m_cfg.requestTimeoutMs = std::clamp(m_cfg.requestTimeoutMs,100,60000);
    m_clientId = ensureClientId();
    m_hardwareHash = ensureHardwareHash();
    m_windowStartMs = QDateTime::currentMSecsSinceEpoch();
    if(!m_performanceTimer) {
        m_performanceTimer=new QTimer(this);
        connect(m_performanceTimer,&QTimer::timeout,this,[this] {
            auto report=m_performance.sample();report["delivery"]=deliveryStatistics();
            submit("app.performance.sample","info",report);
        });
    }
    if(enabled()) {m_performance.sample();m_performanceTimer->start(30000);} // DEC-0139: bounded sampling budget.
    else m_performanceTimer->stop();
    m_accepting.store(enabled(), std::memory_order_release);
}

QJsonObject RemoteDiagnosticsClient::deliveryStatistics() const {
    return {{"acknowledged",double(m_acknowledged)},{"networkDropped",double(m_networkDropped)},
        {"queueDropped",double(m_queueDropped)},{"budgetDeferred",double(m_budgetDropped)},
        {"oversizeDropped",double(m_oversizeDropped)},{"queued",m_queue.size()},
        {"coalesced",double(m_coalesced)},{"ingressDropped",double(m_inputDropped.load())},
        {"inFlight",m_inFlight},{"lastHttpStatus",m_lastHttpStatus}};
}

bool RemoteDiagnosticsClient::enabled() const
{
    return m_cfg.enabled && diagnosticsConfigUsable(m_cfg);
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
    if(!m_accepting.load(std::memory_order_acquire)) return;
    if (QThread::currentThread() != thread()) {
        // One queued wakeup, not one Qt event per DSP report. Both queues are bounded.
        std::lock_guard<std::mutex> lock(m_inputMutex);
        if(!m_accepting.load(std::memory_order_relaxed)) return;
        if(replaceableEvent(type,severity)) {
            for(auto& input:m_input) if(input.type==type && input.severity==severity) {
                input.payload=std::move(payload); ++m_inputDropped; return;
            }
        }
        constexpr int ingressLimit=128; // DEC-0160 transport memory budget.
        if(m_input.size()>=ingressLimit) {
            auto victim=std::min_element(m_input.begin(),m_input.end(),[](const auto& a,const auto& b) {
                return eventPriority(a.type,a.severity)<eventPriority(b.type,b.severity);
            });
            ++m_inputDropped;
            if(eventPriority(victim->type,victim->severity)>eventPriority(type,severity)) return;
            m_input.erase(victim);
        }
        m_input.push_back({std::move(type),std::move(severity),std::move(payload)});
        if(m_inputScheduled) return;
        m_inputScheduled=true;
        const QPointer<RemoteDiagnosticsClient> self(this);
        QMetaObject::invokeMethod(this, [self]() { if(self) self->drainInput(); }, Qt::QueuedConnection);
        return;
    }
    submitOnOwnerThread(std::move(type), std::move(severity), std::move(payload));
}

void RemoteDiagnosticsClient::drainInput() {
    QList<InputEvent> input;
    {
        std::lock_guard<std::mutex> lock(m_inputMutex);
        input.swap(m_input); m_inputScheduled=false;
    }
    for(auto& item:input) submitOnOwnerThread(std::move(item.type),std::move(item.severity),std::move(item.payload));
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
    if (body.size() > std::min(m_cfg.maxPayloadBytes,m_cfg.maxBytesPerMinute)) {
        ++m_oversizeDropped;
        return;
    }

    const int priority=eventPriority(type,severity);
    const QString key=type+":"+severity;
    if(replaceableEvent(type,severity)) {
        for(auto it=m_queue.begin();it!=m_queue.end();++it) if(it->key==key) {
            m_queue.erase(it); ++m_coalesced; break;
        }
    }
    if (m_queue.size() >= m_cfg.maxQueue) {
        auto victim=std::min_element(m_queue.begin(),m_queue.end(),[](const auto& a,const auto& b) {
            return a.priority<b.priority;
        });
        ++m_queueDropped;
        if(victim->priority>priority) return;
        m_queue.erase(victim);
    }
    PendingEvent pending;
    pending.body = body;
    pending.bytes = body.size();
    pending.key=key;
    pending.priority=priority;
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
    request.setTransferTimeout(m_cfg.requestTimeoutMs);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
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

    // Priority applies within the same bandwidth ceiling. A smaller report may
    // use the remaining budget even when a large report cannot fit this minute.
    auto next=m_queue.end();
    for(auto it=m_queue.begin();it!=m_queue.end();++it) {
        if(m_windowBytes+it->bytes<=m_cfg.maxBytesPerMinute &&
           (next==m_queue.end() || it->priority>next->priority)) next=it;
    }
    if (next==m_queue.end()) {
        schedulePump(static_cast<int>(std::max<qint64>(250, 60 * 1000 - (nowMs - m_windowStartMs))));
        ++m_budgetDropped;
        return;
    }

    PendingEvent pending = std::move(*next);
    m_queue.erase(next);
    m_windowBytes += pending.bytes;
    m_lastSendMs = nowMs;
    m_inFlight = true;

    QNetworkRequest request(m_cfg.endpoint);
    request.setTransferTimeout(m_cfg.requestTimeoutMs);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("User-Agent", QByteArray("SDR_Town/") + QByteArray(SDR_TOWN_VERSION));
    if (!m_cfg.bearerToken.trimmed().isEmpty()) {
        request.setRawHeader("Authorization", QByteArray("Bearer ") + m_cfg.bearerToken.toUtf8());
    }

    QNetworkReply* reply = m_network->post(request, pending.body);
    // Bound total lifetime and reply size, including a peer that trickles bytes.
    QTimer::singleShot(m_cfg.requestTimeoutMs, reply, [reply]() { if (!reply->isFinished()) reply->abort(); });
    connect(reply, &QNetworkReply::readyRead, reply, [reply]() {
        if (reply->bytesAvailable() > 4096) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        m_lastHttpStatus=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto ack=QJsonDocument::fromJson(reply->readAll()).object();
        if (reply->error()!=QNetworkReply::NoError || m_lastHttpStatus<200 || m_lastHttpStatus>=300 ||
            !ack.value("ok").toBool(false)) ++m_networkDropped;
        else ++m_acknowledged;
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

    QSettings settings;
    if (settings.contains("remoteDiagnostics/consent"))
        requested = settings.value("remoteDiagnostics/consent").toBool();
    cfg.enabled = !forcedOff && requested && diagnosticsConfigUsable(cfg);
    return cfg;
}

RemoteDiagnosticsClient* remoteDiagnosticsConfigureFromProcess(int argc, char* argv[], QObject* parent, const QString& mode)
{
    startFmDiagnostics(parent); // Local counters remain available with remote sharing disabled.
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
    payload["configSource"] = cfg.configSource.contains('/') || cfg.configSource.contains('\\')
        ? QStringLiteral("configuration file") : cfg.configSource.left(120);
    payload["clientId"] = configuredClientId;
    payload["hardwareHash"] = configuredHardwareHash;
    payload["maxBytesPerMinute"] = cfg.maxBytesPerMinute;
    payload["maxPayloadBytes"] = cfg.maxPayloadBytes;
    payload["minIntervalMs"] = cfg.minIntervalMs;
    payload["maxQueue"] = cfg.maxQueue;
    client->submit("diagnostics.enabled", "info", payload);
    QMetaObject::invokeMethod(client,[client] {
        auto system=ProcessPerformance::systemInfo();
        system["version"]=SDR_TOWN_VERSION;
        QFile provenance(QCoreApplication::applicationDirPath()+"/build-info.json");
        if(provenance.open(QIODevice::ReadOnly) && provenance.size()<=16384) {
            const auto info=QJsonDocument::fromJson(provenance.readAll()).object();
            QJsonObject build;
            for(const auto& key:{"commit","version","projectVersion","generatedUtc","workflowRun",
                                 "sdrplayModuleCommit","sdrplayApiRequired"})
                if(info.value(key).isString()) build[key]=info.value(key).toString().left(100);
            system["build"]=build;
        }
        client->submit("app.system","info",system);
    },Qt::QueuedConnection);
    return client;
}

void remoteDiagnosticsSubmit(const QString& type, const QString& severity, const QJsonObject& payload)
{
    // Keep shutdown from deleting the client between lookup and bounded enqueue.
    QMutexLocker locker(&g_remoteDiagnosticsMutex);
    if (g_remoteDiagnosticsClient) g_remoteDiagnosticsClient->submit(type, severity, payload);
}

void remoteDiagnosticsCheckClientStatus(QObject* context, std::function<void(const QJsonObject&)> callback)
{
    QMutexLocker locker(&g_remoteDiagnosticsMutex);
    if (g_remoteDiagnosticsClient) g_remoteDiagnosticsClient->checkClientStatus(context, std::move(callback));
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

void RemoteDiagnosticsClient::stopWithoutSending()
{
    m_accepting.store(false,std::memory_order_release);
    {std::lock_guard<std::mutex> lock(m_inputMutex);m_input.clear();}
    m_cfg.enabled = false;
    m_queue.clear();
    if (m_timer) m_timer->stop();
    if (m_performanceTimer) m_performanceTimer->stop();
    if (m_network) {
        for (auto* reply : m_network->findChildren<QNetworkReply*>()) reply->abort();
    }
}

void remoteDiagnosticsShutdown(bool flushPending)
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
            if (flushPending) client->drainForMs(1500);
            else client->stopWithoutSending();
        } else {
            QMetaObject::invokeMethod(client, [client, flushPending]() {
                if (flushPending) client->drainForMs(1500);
                else client->stopWithoutSending();
            }, Qt::BlockingQueuedConnection);
        }
    }
    if (thread) {
        thread->quit();
        thread->wait(2000);
        if (!thread->parent()) delete thread;
    }
}
