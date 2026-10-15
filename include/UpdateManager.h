#pragma once

#include <QObject>
#include <QString>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonObject>
#include <QDateTime>
#include <QElapsedTimer>
#include <QPointer>
#include <memory>

class QTimer;
class QTemporaryFile;
class QCryptographicHash;

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
    ~UpdateManager() override;

    // Check (rate-limited unless manual == true)
    void checkForUpdates(bool manual = false);

    // The main flow the user described
    void downloadAndApplyUpdate(const UpdateInfo& info);
    void cancelUpdate();
    bool isBusy() const;

signals:
    void updateAvailable(const UpdateInfo& info);
    void upToDate();
    void error(const QString& message);
    void downloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void downloadFinished(const QString& localPath, bool verified);

protected:
    virtual bool launchInstaller(const QString& path, bool silent = false);

private:
    friend struct UpdateManagerTestAccess;
    enum class Stage { Idle, Manifest, Signature, Installer, ReadyToApply };
    bool beginOperation();
    void resetOperation();
    void fail(const QString& message);
    void fetchManifest();
    void startTransfer(Stage stage, const QUrl& url, qint64 limit);
    void startRequest(const QUrl& url);
    bool checkReply(QNetworkReply* reply);
    bool consumeReply(QNetworkReply* reply);
    void finishReply(QNetworkReply* reply);
    void retireReply();
    void acceptManifest();
    bool parseUpdateJson(const QByteArray& data, UpdateInfo& out);
    void doDownload(const UpdateInfo& info);

    QNetworkAccessManager* m_nam = nullptr;
    QPointer<QNetworkReply> m_reply;
    QTimer* m_deadline = nullptr;
    QTimer* m_idleTimer = nullptr;
    QElapsedTimer m_elapsed;
    QElapsedTimer m_idleElapsed;
    Stage m_stage = Stage::Idle;
    bool m_ownsFlight = false;
    QByteArray m_payload;
    QByteArray m_manifest;
    qint64 m_received = 0;
    qint64 m_limit = 0;
    int m_redirects = 0;
    // DEC-0223 / F09: resource budgets, not RF timing. The idle budget matches
    // QNetworkRequest's default; absolute budgets also stop trickling peers.
    int m_idleTimeoutMs = 30000;
    int m_metadataDeadlineMs = 60000;
    int m_installerDeadlineMs = 30 * 60 * 1000;
    int m_stageDeadlineMs = 0;
    UpdateInfo m_verifiedInfo;
    UpdateInfo m_downloadInfo;
    std::unique_ptr<QTemporaryFile> m_tempFile;
    std::unique_ptr<QCryptographicHash> m_hash;
    QDateTime m_lastCheck;
    QString   m_skippedVersion;
    QString   m_updatesDir;   // %LOCALAPPDATA%\SDR_Town\updates
    bool      m_lastCheckWasManual = false;
};
