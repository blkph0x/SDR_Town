#include "UpdateManager.h"
#include "UpdateManifestSignature.h"

#include <QCoreApplication>
#include <QStandardPaths>
#include <QDir>
#include <QSettings>
#include <QProcess>
#include <QTemporaryFile>
#include <QTimer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkRequest>
#include <QUrl>
#include <QStringList>
#include <QVector>
#include <QRegularExpression>
#include <algorithm>
#include <atomic>

static const QString MANIFEST_URL = "https://github.com/Blkph0x/SDR_Town/releases/latest/download/update.json";
static const int CHECK_INTERVAL_HOURS = 24;

// DEC-0223 / F09: caps apply before application buffer growth, including when
// Content-Length is absent or dishonest. Installer size remains signed input.
static constexpr qint64 kMaxManifestBytes = 64 * 1024;
static constexpr qint64 kMaxSignatureBytes = 4 * 1024;
static constexpr qint64 kTransferChunkBytes = 64 * 1024;
static constexpr qint64 kMaxInstallerBytes = 512ll * 1024 * 1024;
static constexpr int kMaxRedirects = 5;
static std::atomic_bool gUpdateInFlight{false};

static bool isHttpsUrl(const QUrl& url)
{
    return url.isValid() && url.scheme().compare("https", Qt::CaseInsensitive) == 0 &&
           !url.host().isEmpty() && url.userInfo().isEmpty();
}

static QVector<int> parseVersionParts(QString v)
{
    if (v.startsWith('v', Qt::CaseInsensitive)) v.remove(0, 1);
    QVector<int> out;
    for (const QString& p : v.split('.', Qt::SkipEmptyParts)) {
        bool ok = false;
        int n = p.toInt(&ok);
        out.push_back(ok ? n : 0);
    }
    while (out.size() < 3) out.push_back(0);
    return out;
}

static int compareVersions(const QString& a, const QString& b)
{
    QVector<int> av = parseVersionParts(a);
    QVector<int> bv = parseVersionParts(b);
    int n = std::max(av.size(), bv.size());
    for (int i = 0; i < n; ++i) {
        int ai = i < av.size() ? av[i] : 0;
        int bi = i < bv.size() ? bv[i] : 0;
        if (ai != bi) return ai < bi ? -1 : 1;
    }
    return 0;
}

static bool isAllowedUpdateUrl(const QUrl& url)
{
    const QString host = url.host().toLower();
    if (host != "github.com") return false;

    const QString path = url.path();
    return path.startsWith("/Blkph0x/SDR_Town/releases/download/", Qt::CaseSensitive) ||
           path.startsWith("/Blkph0x/SDR_Town/releases/latest/download/", Qt::CaseSensitive);
}

static bool isValidSha256Hex(const QString& text)
{
    static const QRegularExpression re(QStringLiteral("^[0-9a-fA-F]{64}$"));
    return re.match(text).hasMatch();
}

static bool isAllowedNotesUrl(const QUrl& url)
{
    if (!url.isValid()) return false;
    if (url.scheme().compare("https", Qt::CaseInsensitive) != 0) return false;
    const QString host = url.host().toLower();
    const QString path = url.path();
    return host == "github.com" &&
           (path.startsWith("/Blkph0x/SDR_Town/releases", Qt::CaseSensitive) ||
            path.startsWith("/Blkph0x/SDR_Town/blob/", Qt::CaseSensitive));
}

UpdateManager::UpdateManager(QObject* parent)
    : QObject(parent)
{
    m_nam = new QNetworkAccessManager(this);

    // Prepare updates directory
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    m_updatesDir = base + "/updates";
    QDir().mkpath(m_updatesDir);

    // Load persisted state
    QSettings s;
    m_skippedVersion = s.value("updates/skippedVersion").toString();
    m_lastCheck = s.value("updates/lastCheck").toDateTime();

    m_deadline = new QTimer(this);
    m_deadline->setSingleShot(true);
    connect(m_deadline, &QTimer::timeout, this, [this] { fail("Update transfer deadline exceeded."); });
    m_idleTimer = new QTimer(this);
    m_idleTimer->setSingleShot(true);
    connect(m_idleTimer, &QTimer::timeout, this, [this] { fail("Update transfer stalled."); });
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, &UpdateManager::cancelUpdate);
}

UpdateManager::~UpdateManager()
{
    resetOperation();
}

bool UpdateManager::isBusy() const
{
    return m_ownsFlight;
}

bool UpdateManager::beginOperation()
{
    bool expected = false;
    if (!gUpdateInFlight.compare_exchange_strong(expected, true)) return false;
    m_ownsFlight = true;
    return true;
}

void UpdateManager::retireReply()
{
    auto* reply = m_reply.data();
    m_reply.clear();
    if (!reply) return;
    // abort() can emit finished synchronously. Retire all callbacks first.
    reply->disconnect(this);
    reply->setParent(nullptr); // Owner deletion in a signal must still defer reply deletion.
    if (!reply->isFinished()) reply->abort();
    reply->deleteLater();
}

void UpdateManager::resetOperation()
{
    m_deadline->stop();
    m_idleTimer->stop();
    retireReply();
    m_tempFile.reset(); // Auto-removes every partial/unlaunched installer.
    m_hash.reset();
    m_payload.clear();
    m_manifest.clear();
    m_received = 0;
    m_stage = Stage::Idle;
    if (m_ownsFlight) {
        m_ownsFlight = false;
        gUpdateInFlight.store(false);
    }
}

void UpdateManager::cancelUpdate()
{
    resetOperation();
}

void UpdateManager::fail(const QString& message)
{
    resetOperation();
    emit error(message); // No member access after a potentially destructive slot.
}

void UpdateManager::checkForUpdates(bool manual)
{
    QDateTime now = QDateTime::currentDateTime();
    if (!manual && m_lastCheck.isValid() &&
        m_lastCheck.secsTo(now) < CHECK_INTERVAL_HOURS * 3600)
    {
        return; // rate limit (startup / background)
    }

    if (!beginOperation()) return; // Repeated checks cannot overlap any stage.
    m_lastCheckWasManual = manual;
    m_verifiedInfo = {};
    fetchManifest();
}

void UpdateManager::fetchManifest()
{
    startTransfer(Stage::Manifest, QUrl(MANIFEST_URL), kMaxManifestBytes);
}

void UpdateManager::startTransfer(Stage stage, const QUrl& url, qint64 limit)
{
    m_stage = stage;
    m_limit = limit;
    m_received = 0;
    m_redirects = 0;
    m_payload.clear();
    m_stageDeadlineMs = stage == Stage::Installer ? m_installerDeadlineMs : m_metadataDeadlineMs;
    m_elapsed.start();
    m_idleElapsed.start();
    m_deadline->start(m_stageDeadlineMs);
    m_idleTimer->start(m_idleTimeoutMs);
    startRequest(url);
}

void UpdateManager::startRequest(const QUrl& url)
{
    QNetworkRequest request(url);
    // Manual redirects keep response bodies and redirect loops inside the same
    // stage budget. As before, HTTPS CDN redirects are allowed; signatures/hash
    // authenticate bytes independently of the redirect destination.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(m_idleTimeoutMs);
    request.setRawHeader("Accept-Encoding", "identity");
    auto* reply = m_nam->get(request);
    m_reply = reply;
    reply->setReadBufferSize(std::min(kTransferChunkBytes, m_limit));
    connect(reply, &QNetworkReply::metaDataChanged, this, [this, reply] { checkReply(reply); });
    connect(reply, &QIODevice::readyRead, this, [this, reply] { consumeReply(reply); });
    connect(reply, &QNetworkReply::finished, this, [this, reply] { finishReply(reply); });
    if (m_stage == Stage::Installer) {
        connect(reply, &QNetworkReply::downloadProgress, this, [this, reply](qint64, qint64) {
            if (m_reply != reply) return;
            emit downloadProgress(m_received, m_downloadInfo.size);
        });
    }
}

bool UpdateManager::checkReply(QNetworkReply* reply)
{
    if (m_reply != reply) return false;
    // Check monotonic time here too: queued readyRead/finished must not beat a
    // due timer after the owner thread was busy.
    if (m_elapsed.elapsed() >= m_stageDeadlineMs || m_idleElapsed.elapsed() >= m_idleTimeoutMs) {
        fail("Update transfer deadline exceeded.");
        return false;
    }
    if (reply->error() != QNetworkReply::NoError) {
        fail("Update transfer failed: " + reply->errorString());
        return false;
    }
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status >= 300 && status < 400) {
        const QUrl target = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
        const QUrl next = reply->url().resolved(target);
        if (target.isEmpty() || !isHttpsUrl(next) || ++m_redirects > kMaxRedirects) {
            fail("Unsafe or excessive update redirects.");
            return false;
        }
        retireReply();
        startRequest(next); // Does not restart either stage timer.
        return false;
    }
    if (status != 0 && status != 200) {
        fail("Unexpected update HTTP response.");
        return false;
    }
    const auto lengthHeader = reply->header(QNetworkRequest::ContentLengthHeader);
    if (lengthHeader.isValid()) {
        bool ok = false;
        const qint64 length = lengthHeader.toLongLong(&ok);
        if (!ok || length < 0 || length > m_limit ||
            (m_stage == Stage::Installer && length != m_limit)) {
            fail("Update response length does not match its permitted size.");
            return false;
        }
    }
    return true;
}

bool UpdateManager::consumeReply(QNetworkReply* reply)
{
    if (!checkReply(reply)) return false;
    while (reply->bytesAvailable() > 0) {
        if (m_elapsed.elapsed() >= m_stageDeadlineMs) {
            fail("Update transfer deadline exceeded.");
            return false;
        }
        const qint64 remaining = m_limit - m_received;
        if (reply->bytesAvailable() > remaining) {
            fail("Update response exceeds its permitted size.");
            return false;
        }
        const auto chunk = reply->read(std::min(kTransferChunkBytes, remaining));
        if (chunk.isEmpty()) {
            fail("Could not read update response.");
            return false;
        }
        if (m_stage == Stage::Installer) {
            if (m_tempFile->write(chunk) != chunk.size()) {
                fail("Update file write was incomplete.");
                return false;
            }
            m_hash->addData(chunk);
        } else {
            m_payload.append(chunk);
        }
        m_received += chunk.size();
        m_idleElapsed.restart();
        m_idleTimer->start(m_idleTimeoutMs);
    }
    return true;
}

void UpdateManager::finishReply(QNetworkReply* reply)
{
    if (!consumeReply(reply)) return;
    if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200) {
        fail("Update transfer did not return HTTP 200.");
        return;
    }
    retireReply();
    m_deadline->stop();
    m_idleTimer->stop();
    if (m_stage == Stage::Manifest) {
        m_manifest = std::move(m_payload);
        startTransfer(Stage::Signature, QUrl(updateManifestSignatureUrlFor(MANIFEST_URL)), kMaxSignatureBytes);
        return;
    }
    if (m_stage == Stage::Signature) {
        acceptManifest();
        return;
    }
    if (m_received != m_downloadInfo.size) {
        fail("Downloaded update size does not match the signed manifest.");
        return;
    }
    if (QString::fromLatin1(m_hash->result().toHex()).compare(m_downloadInfo.sha256, Qt::CaseInsensitive) != 0) {
        fail("SHA256 verification failed. Update aborted for safety.");
        return;
    }
    if (!m_tempFile->flush()) {
        fail("Could not flush update file.");
        return;
    }
    const QString path = m_tempFile->fileName() + ".exe";
    m_tempFile->close();
    if (!m_tempFile->rename(path)) {
        fail("Could not finalize verified update file.");
        return;
    }
    m_stage = Stage::ReadyToApply;
    QPointer<UpdateManager> guard(this);
    emit downloadFinished(path, true);
    // A slot may cancel, destroy us, or start another operation after cancel.
    if (!guard || m_stage != Stage::ReadyToApply || !m_tempFile || m_tempFile->fileName() != path) return;
    const bool launched = launchInstaller(path, false);
    if (!guard || m_stage != Stage::ReadyToApply || !m_tempFile || m_tempFile->fileName() != path) return;
    if (!launched) {
        fail("Could not launch the verified update installer.");
        return;
    }
    m_tempFile->setAutoRemove(false);
    resetOperation();
}

void UpdateManager::acceptManifest()
{
    if (!verifyUpdateManifestSignature(m_manifest, m_payload)) {
        fail("Update manifest signature verification failed.");
        return;
    }

    UpdateInfo info;
    if (!parseUpdateJson(m_manifest, info)) {
        fail("Update manifest was invalid or empty.");
        return;
    }

    m_lastCheck = QDateTime::currentDateTime();
    QSettings s;
    s.setValue("updates/lastCheck", m_lastCheck);

    QString current = QCoreApplication::applicationVersion();
    const bool manual = m_lastCheckWasManual;
    m_verifiedInfo = info;
    resetOperation();
    if (compareVersions(info.version, current) <= 0 || info.version == m_skippedVersion) {
        if (manual) {
            emit upToDate(); // "You are up to date" ONLY for explicit Help -> Check
        }
        return;
    }

    // Newer version available (semver string compare sufficient; full semver parse can be added later)
    emit updateAvailable(info);
}

bool UpdateManager::parseUpdateJson(const QByteArray& data, UpdateInfo& out)
{
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return false;

    QJsonObject o = doc.object();
    out.version = o.value("version").toString();
    out.tag     = o.value("tag").toString();
    out.notesUrl = o.value("notes_url").toString();

    QJsonObject inst = o.value("installer").toObject();
    out.installerUrl = inst.value("url").toString();
    out.sha256       = inst.value("sha256").toString().simplified();
    out.sha256.remove(' ');
    out.size         = inst.value("size").toInteger(0);

    QUrl installer(out.installerUrl);
    QUrl notes(out.notesUrl);
    const bool notesOk = out.notesUrl.isEmpty() || isAllowedNotesUrl(notes);
    const bool sizeOk = out.size > 0 && out.size <= kMaxInstallerBytes;
    return !out.version.isEmpty() &&
           isHttpsUrl(installer) &&
           isAllowedUpdateUrl(installer) &&
           notesOk &&
           sizeOk &&
           isValidSha256Hex(out.sha256);
}

void UpdateManager::downloadAndApplyUpdate(const UpdateInfo& info)
{
    // Only the exact size/hash/URL authenticated by this manager may download.
    if (m_verifiedInfo.version.isEmpty() || info.version != m_verifiedInfo.version ||
        info.installerUrl != m_verifiedInfo.installerUrl || info.size != m_verifiedInfo.size ||
        info.sha256 != m_verifiedInfo.sha256) {
        emit error("Update details do not match a verified signed manifest.");
        return;
    }
    doDownload(info);
}

void UpdateManager::doDownload(const UpdateInfo& info)
{
    if (!beginOperation()) {
        emit error("An update operation is already in progress.");
        return;
    }
    const QUrl updateUrl(info.installerUrl);
    if (!isHttpsUrl(updateUrl) || !isAllowedUpdateUrl(updateUrl) || !isValidSha256Hex(info.sha256) ||
        info.size <= 0 || info.size > kMaxInstallerBytes) {
        fail("Update manifest has an invalid installer URL, size or SHA256 digest.");
        return;
    }
    m_downloadInfo = info;
    m_tempFile = std::make_unique<QTemporaryFile>(m_updatesDir + "/SDR_Town-Update-XXXXXX.part");
    if (!m_tempFile->open()) {
        fail("Could not create update temporary file.");
        return;
    }
    m_hash = std::make_unique<QCryptographicHash>(QCryptographicHash::Sha256);
    startTransfer(Stage::Installer, updateUrl, info.size);
}

bool UpdateManager::launchInstaller(const QString& path, bool silent)
{
    // Best practice (exact per release pipeline spec): launch the NSIS installer (GUI or /S silent),
    // then immediately quit our process so the installer can overwrite SDR_Town.exe + DLLs etc.
    // NSIS /S is silent install (no prompts, auto "next" through pages, preserves APPDATA\SDR_Town settings).
    QStringList args;
    if (silent) args << "/S";
    if (!QProcess::startDetached(path, args)) return false;
    QCoreApplication::quit();
    return true;
}
