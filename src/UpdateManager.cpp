#include "UpdateManager.h"
#include "UpdateManifestSignature.h"

#include <QCoreApplication>
#include <QStandardPaths>
#include <QDir>
#include <QSettings>
#include <QMessageBox>
#include <QProcess>
#include <QFile>
#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkRequest>
#include <QDesktopServices>
#include <QUrl>
#include <QEventLoop>
#include <QStringList>
#include <QVector>
#include <QRegularExpression>
#include <algorithm>
#include <atomic>

static const QString MANIFEST_URL = "https://github.com/Blkph0x/SDR_Town/releases/latest/download/update.json";
static const int CHECK_INTERVAL_HOURS = 24;

static std::atomic_bool gUpdateDownloadInFlight{false};

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
}

void UpdateManager::checkForUpdates(bool manual)
{
    QDateTime now = QDateTime::currentDateTime();
    if (!manual && m_lastCheck.isValid() &&
        m_lastCheck.secsTo(now) < CHECK_INTERVAL_HOURS * 3600)
    {
        return; // rate limit (startup / background)
    }

    m_lastCheckWasManual = manual;
    fetchManifest();
}

void UpdateManager::fetchManifest()
{
    QNetworkRequest req(MANIFEST_URL);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onManifestReply(reply);
        reply->deleteLater();
    });
}

void UpdateManager::onManifestReply(QNetworkReply* reply)
{
    if (reply->error() != QNetworkReply::NoError) {
        emit error("Failed to check for updates: " + reply->errorString());
        return;
    }

    QByteArray data = reply->readAll();

    const QString sigUrl = updateManifestSignatureUrlFor(MANIFEST_URL);
    QByteArray signature;
    if (!sigUrl.isEmpty()) {
        QNetworkRequest sigReq(sigUrl);
        sigReq.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        QNetworkReply* sigReply = m_nam->get(sigReq);
        QEventLoop loop;
        connect(sigReply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
        if (sigReply->error() == QNetworkReply::NoError) {
            signature = sigReply->readAll().trimmed();
        }
        sigReply->deleteLater();
    }
    if (!verifyUpdateManifestSignature(data, signature)) {
        emit error("Update manifest signature verification failed.");
        return;
    }

    UpdateInfo info;
    if (!parseUpdateJson(data, info)) {
        emit error("Update manifest was invalid or empty.");
        return;
    }

    m_lastCheck = QDateTime::currentDateTime();
    QSettings s;
    s.setValue("updates/lastCheck", m_lastCheck);

    QString current = QCoreApplication::applicationVersion();
    if (compareVersions(info.version, current) <= 0 || info.version == m_skippedVersion) {
        if (m_lastCheckWasManual) {
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
    constexpr qint64 kMaxInstallerBytes = 512ll * 1024ll * 1024ll;
    const bool sizeOk = out.size > 0 && out.size <= kMaxInstallerBytes;
    return !out.version.isEmpty() &&
           installer.isValid() &&
           installer.scheme().compare("https", Qt::CaseInsensitive) == 0 &&
           isAllowedUpdateUrl(installer) &&
           notesOk &&
           sizeOk &&
           isValidSha256Hex(out.sha256);
}

void UpdateManager::downloadAndApplyUpdate(const UpdateInfo& info)
{
    if (info.installerUrl.isEmpty()) {
        emit error("No installer URL in update manifest.");
        return;
    }

    doDownload(info.installerUrl, info.sha256);
}

void UpdateManager::doDownload(const QString& url, const QString& expectedSha256)
{
    bool expected = false;
    if (!gUpdateDownloadInFlight.compare_exchange_strong(expected, true)) {
        emit error("An update download is already in progress.");
        return;
    }

    auto clearInFlight = [] { gUpdateDownloadInFlight.store(false); };

    QUrl updateUrl(url);
    if (!updateUrl.isValid() || updateUrl.scheme().compare("https", Qt::CaseInsensitive) != 0) {
        emit error("Update download URL must be valid HTTPS.");
        clearInFlight();
        return;
    }
    if (!isAllowedUpdateUrl(updateUrl)) {
        emit error("Update download URL is not a trusted SDR_Town GitHub release asset.");
        clearInFlight();
        return;
    }
    if (!isValidSha256Hex(expectedSha256)) {
        emit error("Update manifest has an invalid SHA256 digest.");
        clearInFlight();
        return;
    }

    QNetworkRequest req(updateUrl);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = m_nam->get(req);

    connect(reply, &QNetworkReply::downloadProgress, this, &UpdateManager::downloadProgress);

    connect(reply, &QNetworkReply::finished, this, [this, reply, expectedSha256, url]() {
        if (reply->error() != QNetworkReply::NoError) {
            emit error("Download failed: " + reply->errorString());
            reply->deleteLater();
            gUpdateDownloadInFlight.store(false);
            return;
        }

        QByteArray payload = reply->readAll();
        constexpr qint64 kMaxDownloadedInstallerBytes = 512ll * 1024ll * 1024ll;
        if (payload.size() <= 0 || static_cast<qint64>(payload.size()) > kMaxDownloadedInstallerBytes) {
            emit error("Downloaded update payload size is invalid or too large.");
            reply->deleteLater();
            gUpdateDownloadInFlight.store(false);
            return;
        }

        // Stable downloaded name (versioned preferred; we derive a safe one from URL if possible)
        QString base = "SDR_Town-Update-Setup";
        QUrl u(url);
        QString pathPart = u.path();
        if (pathPart.contains("SDR_Town-") && pathPart.endsWith(".exe", Qt::CaseInsensitive)) {
            int idx = pathPart.lastIndexOf('/');
            if (idx >= 0) base = pathPart.mid(idx + 1);
            if (base.endsWith(".exe", Qt::CaseInsensitive)) base.chop(4);
        }
        QString fileName = m_updatesDir + "/" + base + ".exe";
        if (QFile::exists(fileName)) QFile::remove(fileName);
        QFile f(fileName);
        if (!f.open(QIODevice::WriteOnly)) {
            emit error("Could not write update file.");
            reply->deleteLater();
            gUpdateDownloadInFlight.store(false);
            return;
        }
        const qint64 written = f.write(payload);
        f.close();
        reply->deleteLater();
        if (written != payload.size()) {
            emit error("Update file write was incomplete.");
            QFile::remove(fileName);
            gUpdateDownloadInFlight.store(false);
            return;
        }

        bool ok = verifySha256(fileName, expectedSha256);
        emit downloadFinished(fileName, ok);

        if (ok) {
            launchInstaller(fileName, /*silent*/ false);
        } else {
            emit error("SHA256 verification failed. Update aborted for safety.");
            QFile::remove(fileName);
        }
        gUpdateDownloadInFlight.store(false);
    });
}

bool UpdateManager::verifySha256(const QString& path, const QString& expectedHex)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;

    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&f)) return false;
    QString actual = hash.result().toHex();
    return actual.compare(expectedHex, Qt::CaseInsensitive) == 0;
}

void UpdateManager::launchInstaller(const QString& path, bool silent)
{
    // Best practice (exact per release pipeline spec): launch the NSIS installer (GUI or /S silent),
    // then immediately quit our process so the installer can overwrite SDR_Town.exe + DLLs etc.
    // NSIS /S is silent install (no prompts, auto "next" through pages, preserves APPDATA\SDR_Town settings).
    QStringList args;
    if (silent) args << "/S";
    if (!QProcess::startDetached(path, args)) {
        emit error("Could not launch the verified update installer.");
        return;
    }
    QCoreApplication::quit();
}
