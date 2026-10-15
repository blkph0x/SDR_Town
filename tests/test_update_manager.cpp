#include "UpdateManager.h"
#include "UpdateManifestSignature.h"
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QNetworkRequest>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QThread>
#include <QTimer>
#include <cstring>
#include <functional>

// The real Ed25519 verifier is linked. These public release bytes are embedded
// as base64 to preserve their signed CRLFs regardless of checkout conversion.
static QByteArray signedManifest()
{
    return QByteArray::fromBase64(
        "ew0KICAidGFnIjogInYwLjIuMTM3IiwNCiAgInZlcnNpb24iOiAiMC4yLjEzNyIsDQogICJub3Rlc191cmwiOiAiaHR0cHM6Ly9naXRodWIuY29tL0Jsa3BoMHgvU0RSX1Rvd24vcmVsZWFzZXMvdGFnL3YwLjIuMTM3IiwNCiAgImNoYW5uZWwiOiAiZXhwZXJpbWVudGFsIiwNCiAgIm1pbl9zdXBwb3J0ZWRfdmVyc2lvbiI6ICIwLjIuMCIsDQogICJpbnN0YWxsZXIiOiB7DQogICAgInVybCI6ICJodHRwczovL2dpdGh1Yi5jb20vQmxrcGgweC9TRFJfVG93bi9yZWxlYXNlcy9kb3dubG9hZC92MC4yLjEzNy9TRFJfVG93bi0wLjIuMTM3LXdpbjY0LXNldHVwLmV4ZSIsDQogICAgInNoYTI1NiI6ICIxOGZjZGE3MjUwYWE4ZWUyZTQxYTEzZDZlNGFjOTgyOTkyMGI4NmI0MjRlNWIxYmYwYzhmZDA5ZjI2OTlhMjliIiwNCiAgICAic2l6ZSI6IDE0ODI0MTE5Nw0KICB9LA0KICAicHVibGlzaGVkIjogIjIwMjYtMTAtMTEiDQp9DQo=");
}
static const QByteArray signature = "oWTLE977am1BkCAn7gmMceeVFNPrEx78Pzvor+K0NZhOn9wK3ATITky6pV/nAyPXXDIqBEus7+ptIZ4wZ+aUBQ==";

class FakeReply final : public QNetworkReply {
public:
    FakeReply(const QNetworkRequest& request, QObject* parent) : QNetworkReply(parent)
    {
        setRequest(request); setUrl(request.url());
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
    }
    void headers(int status = 200, qint64 length = -1, const QUrl& redirect = {})
    {
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        if (length >= 0) setHeader(QNetworkRequest::ContentLengthHeader, length);
        if (!redirect.isEmpty()) setAttribute(QNetworkRequest::RedirectionTargetAttribute, redirect);
        emit metaDataChanged();
    }
    void push(const QByteArray& bytes)
    {
        buffer += bytes;
        QPointer<FakeReply> guard(this);
        emit readyRead();
        if (guard && !isFinished()) emit downloadProgress(readBytes, -1);
    }
    void finish() { setFinished(true); emit finished(); }
    void abort() override
    {
        ++aborts;
        setError(OperationCanceledError, "fake cancellation");
        setFinished(true);
        emit finished(); // Deliberately synchronous to exercise retirement.
    }
    qint64 bytesAvailable() const override { return buffer.size() + QNetworkReply::bytesAvailable(); }
    qint64 readBytes = 0;
    int aborts = 0;
protected:
    qint64 readData(char* data, qint64 maxSize) override
    {
        const auto count = std::min<qint64>(maxSize, buffer.size());
        if (!count) return 0;
        std::memcpy(data, buffer.constData(), static_cast<size_t>(count));
        buffer.remove(0, count); readBytes += count;
        return count;
    }
private:
    QByteArray buffer;
};

class FakeNetwork final : public QNetworkAccessManager {
public:
    using QNetworkAccessManager::QNetworkAccessManager;
    QList<QPointer<FakeReply>> replies;
    FakeReply* last() const { return replies.last().data(); }
protected:
    QNetworkReply* createRequest(Operation, const QNetworkRequest& request, QIODevice*) override
    {
        auto* reply = new FakeReply(request, this);
        replies.append(reply);
        return reply; // Never delegates to a real network backend.
    }
};

struct UpdateManagerTestAccess {
    static FakeNetwork* network(UpdateManager& manager, const QString& directory)
    {
        delete manager.m_nam;
        auto* network = new FakeNetwork(&manager);
        manager.m_nam = network; manager.m_updatesDir = directory;
        manager.m_idleTimeoutMs = 500;
        manager.m_metadataDeadlineMs = 1000;
        manager.m_installerDeadlineMs = 1000;
        return network;
    }
    static void budgets(UpdateManager& manager, int idle, int absolute)
    {
        manager.m_idleTimeoutMs = idle;
        manager.m_metadataDeadlineMs = absolute;
        manager.m_installerDeadlineMs = absolute;
    }
    // Unit-test only the installer transport without a production private key.
    // Public entry-point authentication is exercised separately below.
    static void download(UpdateManager& manager, const UpdateInfo& info) { manager.doDownload(info); }
    static qint64 buffered(const UpdateManager& manager) { return manager.m_payload.size(); }
    static QString partial(const UpdateManager& manager) { return manager.m_tempFile->fileName(); }
    static void closePartial(UpdateManager& manager) { manager.m_tempFile->close(); }
};

class TestUpdater final : public UpdateManager {
public:
    int launches = 0;
    bool launchSucceeds = true;
    QByteArray launchedBytes;
protected:
    bool launchInstaller(const QString& path, bool) override
    {
        ++launches;
        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) launchedBytes = file.readAll();
        return launchSucceeds; // No executable is ever launched by this suite.
    }
};

struct Fixture {
    QTemporaryDir directory;
    TestUpdater manager;
    FakeNetwork* network = UpdateManagerTestAccess::network(manager, directory.path());
    QStringList errors;
    int available = 0;
    int finished = 0;
    UpdateInfo offered;
    Fixture()
    {
        REQUIRE(directory.isValid());
        QObject::connect(&manager, &UpdateManager::error, &manager, [this](const auto& message) { errors.append(message); });
        QObject::connect(&manager, &UpdateManager::updateAvailable, &manager, [this](const auto& info) { ++available; offered = info; });
        QObject::connect(&manager, &UpdateManager::downloadFinished, &manager, [this](const auto&, bool ok) { CHECK(ok); ++finished; });
    }
    ~Fixture() { manager.cancelUpdate(); }
    FakeReply* manifest(const QByteArray& bytes = signedManifest())
    {
        manager.checkForUpdates(true);
        auto* reply = network->last(); reply->headers(); reply->push(bytes); reply->finish();
        REQUIRE(network->replies.size() == 2);
        return network->last();
    }
    UpdateInfo installer(const QByteArray& bytes)
    {
        UpdateInfo info;
        info.version = "1.0.0";
        info.installerUrl = "https://github.com/Blkph0x/SDR_Town/releases/download/test/fixture.exe";
        info.sha256 = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
        info.size = bytes.size();
        UpdateManagerTestAccess::download(manager, info);
        return info;
    }
    bool noFiles() const { return QDir(directory.path()).entryList(QDir::Files).isEmpty(); }
};

static bool waitFor(const std::function<bool()>& predicate)
{
    QElapsedTimer elapsed; elapsed.start();
    while (!predicate() && elapsed.elapsed() < 2000) { QCoreApplication::processEvents(); QThread::msleep(1); }
    return predicate();
}

TEST_CASE("Updater authenticates exact public manifest bytes asynchronously", "[update]")
{
    Fixture f;
    REQUIRE(verifyUpdateManifestSignature(signedManifest(), signature));
    auto* reply = f.manifest();
    CHECK(f.manager.isBusy()); CHECK(f.available == 0);
    CHECK(reply->request().transferTimeout() > 0);
    CHECK(reply->readBufferSize() <= 4096);
    SECTION("valid signature") {
        reply->headers(); reply->push(signature); reply->finish();
        CHECK(f.errors.isEmpty()); REQUIRE(f.available == 1); CHECK_FALSE(f.manager.isBusy());
        auto altered = f.offered; ++altered.size;
        f.manager.downloadAndApplyUpdate(altered);
        CHECK(f.network->replies.size() == 2); CHECK(f.errors.size() == 1);
        f.manager.downloadAndApplyUpdate(f.offered);
        REQUIRE(f.network->replies.size() == 3); CHECK(f.manager.isBusy());
        f.manager.cancelUpdate(); CHECK(f.noFiles());
    }
    SECTION("invalid signature") {
        reply->headers(); reply->push(QByteArray(88, 'A')); reply->finish();
        CHECK(f.available == 0); CHECK(f.errors.size() == 1); CHECK_FALSE(f.manager.isBusy());
    }
    SECTION("missing signature") {
        reply->headers(); reply->finish();
        CHECK(f.available == 0); CHECK(f.errors.size() == 1);
    }
}

TEST_CASE("Updater rejects tampering and unsigned direct downloads", "[update]")
{
    Fixture f;
    UpdateInfo untrusted; untrusted.installerUrl = "https://github.com/Blkph0x/SDR_Town/releases/download/test/x.exe";
    f.manager.downloadAndApplyUpdate(untrusted);
    CHECK(f.network->replies.isEmpty());
    auto data = signedManifest(); data.replace("0.2.137", "9.9.999");
    auto* reply = f.manifest(data); reply->headers(); reply->push(signature); reply->finish();
    CHECK(f.available == 0); CHECK(f.errors.size() == 2); CHECK_FALSE(f.manager.isBusy());
}

TEST_CASE("Updater rejects oversize metadata before reading or appending it", "[update]")
{
    Fixture f;
    FakeReply* reply = nullptr;
    qint64 limit = 0;
    SECTION("manifest") { f.manager.checkForUpdates(true); reply = f.network->last(); limit = 64 * 1024; }
    SECTION("signature") { reply = f.manifest(); limit = 4096; }
    REQUIRE(reply);
    reply->headers(); reply->push(QByteArray(limit, 'x'));
    CHECK(UpdateManagerTestAccess::buffered(f.manager) == limit);
    reply->push("x");
    CHECK(reply->readBytes == limit); CHECK(reply->aborts == 1);
    CHECK(f.errors.size() == 1); CHECK_FALSE(f.manager.isBusy()); CHECK(f.available == 0);
}

TEST_CASE("Updater rejects oversized headers and bad HTTP without reading the body", "[update]")
{
    Fixture f; f.manager.checkForUpdates(true); auto* reply = f.network->last();
    SECTION("length cap") { reply->headers(200, 64 * 1024 + 1); }
    SECTION("error status") { reply->headers(404); }
    CHECK(reply->readBytes == 0); CHECK(f.errors.size() == 1); CHECK_FALSE(f.manager.isBusy());
}

TEST_CASE("Updater deadlines bound stalls trickles and unterminated exact responses", "[update]")
{
    Fixture f; UpdateManagerTestAccess::budgets(f.manager, 80, 180);
    FakeReply* reply = nullptr;
    QTimer trickle;
    SECTION("manifest idle") { f.manager.checkForUpdates(true); reply = f.network->last(); }
    SECTION("signature idle") { reply = f.manifest(); }
    SECTION("signature trickle") {
        reply = f.manifest(); reply->headers();
        QObject::connect(&trickle, &QTimer::timeout, &f.manager, [guard = QPointer<FakeReply>(reply)] {
            if (guard && !guard->isFinished()) guard->push(" ");
        }); trickle.start(10);
    }
    SECTION("installer trickle") {
        f.installer(QByteArray(1024, ' ')); reply = f.network->last(); reply->headers();
        QObject::connect(&trickle, &QTimer::timeout, &f.manager, [guard = QPointer<FakeReply>(reply)] {
            if (guard && !guard->isFinished()) guard->push(" ");
        }); trickle.start(10);
    }
    SECTION("installer has exact bytes but never finishes") {
        f.installer("fixture"); reply = f.network->last(); reply->headers(); reply->push("fixture");
    }
    REQUIRE(reply); QPointer<FakeReply> guardedReply(reply);
    REQUIRE(waitFor([&] { return !f.manager.isBusy(); })); trickle.stop();
    CHECK(f.errors.size() == 1); CHECK(f.manager.launches == 0); CHECK(f.noFiles());
    if (guardedReply) CHECK(guardedReply->aborts == 1);
}

TEST_CASE("Updater redirects preserve bounds and authenticated metadata", "[update]")
{
    Fixture f; f.manager.checkForUpdates(true);
    CHECK(f.network->last()->request().attribute(QNetworkRequest::RedirectPolicyAttribute).toInt() == QNetworkRequest::ManualRedirectPolicy);
    SECTION("HTTPS asset redirect") {
        f.network->last()->headers(302, -1, QUrl("https://release-assets.githubusercontent.com/fixture"));
        REQUIRE(f.network->replies.size() == 2);
        auto* reply = f.network->last(); reply->headers(); reply->push(signedManifest()); reply->finish();
        reply = f.network->last(); reply->headers(); reply->push(signature); reply->finish();
        CHECK(f.available == 1); CHECK(f.errors.isEmpty());
    }
    SECTION("HTTP downgrade") {
        f.network->last()->headers(302, -1, QUrl("http://collector.invalid/update"));
        CHECK(f.network->replies.size() == 1); CHECK(f.errors.size() == 1);
    }
    SECTION("redirect loop") {
        for (int i = 0; i < 6; ++i) f.network->last()->headers(302, -1, QUrl("/loop"));
        CHECK(f.network->replies.size() == 6); CHECK(f.errors.size() == 1);
    }
    CHECK_FALSE(f.manager.isBusy());
}

TEST_CASE("Updater elapsed deadline also rejects callbacks before timers are dispatched", "[update]")
{
    Fixture f; UpdateManagerTestAccess::budgets(f.manager, 1000, 80);
    f.manager.checkForUpdates(true);
    QThread::msleep(50);
    f.network->last()->headers(302, -1, QUrl("https://release-assets.githubusercontent.com/fixture"));
    REQUIRE(f.network->replies.size() == 2);
    QThread::msleep(50); // No event processing: a redirect must not renew the budget.
    auto* reply = f.network->last(); reply->push("x");
    CHECK(reply->readBytes == 0); CHECK(reply->aborts == 1);
    CHECK(f.errors.size() == 1); CHECK_FALSE(f.manager.isBusy());
}

TEST_CASE("Updater streams installer bytes and requires exact size and hash", "[update]")
{
    Fixture f; const QByteArray payload(200000, 'x'); f.installer(payload);
    auto* reply = f.network->last();
    CHECK(reply->readBufferSize() <= 64 * 1024);
    bool success = false;
    int launches = 0;
    SECTION("valid chunks") {
        success = true; launches = 1;
        reply->headers(200, payload.size());
        for (int offset = 0; offset < payload.size(); offset += 10000) {
            reply->push(payload.mid(offset, 10000));
            CHECK(UpdateManagerTestAccess::buffered(f.manager) == 0);
        }
        reply->finish();
        CHECK(f.finished == 1); CHECK(f.manager.launches == 1); CHECK(f.manager.launchedBytes == payload);
        CHECK(f.errors.isEmpty()); CHECK_FALSE(f.noFiles());
    }
    SECTION("truncated") { reply->headers(); reply->push(payload.left(199999)); reply->finish(); }
    SECTION("hash mismatch") { reply->headers(); reply->push(QByteArray(payload.size(), 'y')); reply->finish(); }
    SECTION("oversize without length") { reply->headers(); reply->push(payload); reply->push("x"); CHECK(reply->readBytes == payload.size()); }
    SECTION("dishonest length") { reply->headers(200, payload.size()); reply->push(payload + "x"); CHECK(reply->readBytes == 0); }
    SECTION("signed size disagrees with header") { reply->headers(200, payload.size() - 1); CHECK(reply->readBytes == 0); }
    SECTION("network failure") { reply->headers(); reply->push(payload.left(100)); reply->abort(); }
    SECTION("disk write failure") {
        // Close the actual temporary device, exercising the real write-failure path.
        UpdateManagerTestAccess::closePartial(f.manager);
        reply->headers(); reply->push("x");
    }
    SECTION("launch failure removes verified file") {
        launches = 1;
        f.manager.launchSucceeds = false;
        reply->headers(); reply->push(payload); reply->finish();
        CHECK(f.manager.launches == 1); CHECK(f.noFiles());
    }
    CHECK_FALSE(f.manager.isBusy());
    CHECK(f.manager.launches == launches);
    if (!success) { CHECK(f.errors.size() == 1); CHECK(f.noFiles()); }
}

TEST_CASE("Updater single flight cancellation and destruction retire all stages", "[update]")
{
    Fixture f; FakeReply* reply = nullptr;
    SECTION("manifest") { f.manager.checkForUpdates(true); reply = f.network->last(); }
    SECTION("signature") { reply = f.manifest(); }
    SECTION("installer") { f.installer("fixture"); reply = f.network->last(); reply->headers(); reply->push("fix"); }
    REQUIRE(reply); const auto requests = f.network->replies.size();
    f.manager.checkForUpdates(true); CHECK(f.network->replies.size() == requests);
    Fixture second; second.manager.checkForUpdates(true); CHECK(second.network->replies.isEmpty());
    f.manager.cancelUpdate(); CHECK(f.noFiles()); CHECK(reply->aborts == 1); CHECK(f.errors.isEmpty());
    reply->finish(); // Late callbacks must be inert even after the lease is reused.
    second.manager.checkForUpdates(true); CHECK(second.network->replies.size() == 1);
    CHECK(second.manager.isBusy()); CHECK_FALSE(f.manager.isBusy());
}

TEST_CASE("Updater destruction aborts a pending signature and releases the lease", "[update]")
{
    auto f = std::make_unique<Fixture>();
    QPointer<FakeReply> reply;
    SECTION("manifest") { f->manager.checkForUpdates(true); reply = f->network->last(); }
    SECTION("signature") { reply = f->manifest(); }
    SECTION("installer") { f->installer("fixture"); reply = f->network->last(); reply->headers(); reply->push("fix"); }
    f.reset();
    REQUIRE(reply); CHECK(reply->aborts == 1);
    Fixture next; next.manager.checkForUpdates(true); CHECK(next.network->replies.size() == 1);
}

TEST_CASE("Updater error and progress slots can destroy the owner safely", "[update]")
{
    auto f = std::make_unique<Fixture>();
    SECTION("error") {
        QObject::connect(&f->manager, &UpdateManager::error, QCoreApplication::instance(), [&](const auto&) { f.reset(); });
        f->manager.checkForUpdates(true);
        f->network->last()->headers(404);
    }
    SECTION("progress") {
        QObject::connect(&f->manager, &UpdateManager::downloadProgress, QCoreApplication::instance(), [&](qint64, qint64) { f.reset(); });
        f->installer("fixture"); auto* reply = f->network->last(); reply->headers(); reply->push("fix");
    }
    CHECK_FALSE(f);
    Fixture next; next.manager.checkForUpdates(true); CHECK(next.network->replies.size() == 1);
}

TEST_CASE("Updater completion slots may cancel or destroy the owner without launching", "[update]")
{
    auto f = std::make_unique<Fixture>(); const auto directory = f->directory.path();
    f->installer("fixture"); auto* reply = f->network->last();
    SECTION("cancel") {
        QObject::connect(&f->manager, &UpdateManager::downloadFinished, &f->manager, [&](const auto&, bool) { f->manager.cancelUpdate(); });
        reply->headers(); reply->push("fixture"); reply->finish();
        CHECK(f->manager.launches == 0); CHECK(f->noFiles());
    }
    SECTION("destroy") {
        QObject::connect(&f->manager, &UpdateManager::downloadFinished, QCoreApplication::instance(), [&](const auto&, bool) { f.reset(); });
        reply->headers(); reply->push("fixture"); reply->finish();
        CHECK_FALSE(f); CHECK_FALSE(QDir(directory).exists());
    }
    Fixture next; next.manager.checkForUpdates(true); CHECK(next.network->replies.size() == 1);
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    app.setOrganizationName("SDR_Town_Tests"); app.setApplicationName("updater-fixtures");
    app.setApplicationVersion("0.0.0");
    QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    return Catch::Session().run(argc, argv);
}
