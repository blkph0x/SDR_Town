#pragma once

#include <QObject>
#include <QString>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonObject>
#include <functional>

struct UpdateInfo {
    QString version;
    QString tag;
    QString installerUrl;
    QString sha256;
    qint64  size = 0;
    QString notesUrl;
};

class UpdateManager : public QObject
{
    Q_OBJECT
public:
    explicit UpdateManager(QObject* parent = nullptr);

    // Check (rate-limited unless manual == true)
    void checkForUpdates(bool manual = false);

    // The main flow the user described
    void downloadAndApplyUpdate(const UpdateInfo& info);

signals:
    void updateAvailable(const UpdateInfo& info);
    void upToDate();
    void error(const QString& message);
    void downloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void downloadFinished(const QString& localPath, bool verified);

private:
    void fetchManifest();
    void onManifestReply(QNetworkReply* reply);
    bool parseUpdateJson(const QByteArray& data, UpdateInfo& out);
    void doDownload(const QString& url, const QString& expectedSha256);
    bool verifySha256(const QString& path, const QString& expectedHex);
    void launchInstaller(const QString& path, bool silent = false);

    QNetworkAccessManager* m_nam = nullptr;
    QDateTime m_lastCheck;
    QString   m_skippedVersion;
    QString   m_updatesDir;   // %LOCALAPPDATA%\SDR_Town\updates
    bool      m_lastCheckWasManual = false;
};