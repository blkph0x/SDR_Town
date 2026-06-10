#include "UpdateManager.h"

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
#include <QDebug>
#include <QStringList>
#include <QVector>
#include <algorithm>

static const QString MANIFEST_URL = "https://github.com/Blkph0x/SDR_Town/releases/latest/download/update.json";
static const int CHECK_INTERVAL_HOURS = 24;

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

    m_lastCheck = now;
    m_lastCheckWasManual = manual;
    QSettings s;
    s.setValue("updates/lastCheck", m_lastCheck);

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
    UpdateInfo info;
    if (!parseUpdateJson(data, info)) {
        emit error("Update manifest was invalid or empty.");
        return;
    }

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
    return !out.version.isEmpty() &&
           installer.isValid() &&
           installer.scheme().compare("https", Qt::CaseInsensitive) == 0 &&
           !out.sha256.isEmpty();
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
    QUrl updateUrl(url);
    if (!updateUrl.isValid() || updateUrl.scheme().compare("https", Qt::CaseInsensitive) != 0) {
        emit error("Update download URL must be valid HTTPS.");
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
            return;
        }
        f.write(reply->readAll());
        f.close();
        reply->deleteLater();

        bool ok = verifySha256(fileName, expectedSha256);
        emit downloadFinished(fileName, ok);

        if (ok) {
            launchInstaller(fileName, /*silent*/ false);
        } else {
            emit error("SHA256 verification failed. Update aborted for safety.");
            QFile::remove(fileName);
        }
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
