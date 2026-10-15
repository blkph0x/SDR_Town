#include "InmarsatReplayDialog.h"
#include "InmarsatDiagnostics.h"
#include "InmarsatDiagnosticRecording.h"
#include "RemoteDiagnostics.h"
#include <catch2/catch_test_macros.hpp>
#include <QApplication>
#include <QAbstractButton>
#include <QCheckBox>
#include <QMessageBox>
#include <QSettings>
#include <QTimer>

// Link into sdr_town_workspace_tests with InmarsatRemoteDiagnostics.cpp, NOT
// RemoteDiagnostics.cpp. Real configuration precedence is covered in
// test_remote_diagnostics.cpp; this fake transport tests the actual dialog
// and adapter without creating a network manager or sending telemetry.
namespace {
bool transportEnabled = false;
bool transportAvailable = true;
int configureCalls = 0;
RemoteDiagnosticsConsent requestedConsent = RemoteDiagnosticsConsent::ProcessDefault;
}

RemoteDiagnosticsClient::RemoteDiagnosticsClient(QObject* parent) : QObject(parent) {}
RemoteDiagnosticsClient::~RemoteDiagnosticsClient() = default;
bool remoteDiagnosticsEnabled() { return transportEnabled; }
void remoteDiagnosticsSubmit(const QString&, const QString&, const QJsonObject&) {}
RemoteDiagnosticsConfig remoteDiagnosticsConfigFromProcess(int, char**, const QString&,
    RemoteDiagnosticsConsent consent)
{
    requestedConsent = consent;
    RemoteDiagnosticsConfig cfg;
    cfg.endpoint = QUrl("https://collector.invalid/ingest");
    return cfg;
}
RemoteDiagnosticsClient* remoteDiagnosticsConfigureFromProcess(int, char**, QObject*, const QString&,
    RemoteDiagnosticsConsent consent)
{
    ++configureCalls; requestedConsent = consent;
    transportEnabled = transportAvailable;
    static RemoteDiagnosticsClient fakeClient;
    return transportEnabled ? &fakeClient : nullptr;
}

TEST_CASE("Replay sharing asks explicit one-run consent and reflects activation failure", "[inmarsat][gui][consent]")
{
    QSettings settings; settings.setValue("remoteDiagnostics/consent", false);
    const auto recording = InmarsatDiagnosticRecording::status();
    transportEnabled = false; transportAvailable = true; configureCalls = 0;
    requestedConsent = RemoteDiagnosticsConsent::ProcessDefault;
    InmarsatReplayDialog dialog;
    configureInmarsatReplaySharing(dialog);
    auto* share = dialog.findChild<QCheckBox*>("iqShareDiagnostics");
    REQUIRE(share); REQUIRE(share->isEnabled()); CHECK_FALSE(share->isChecked());
    bool yes = false;
    SECTION("No leaves all consent unchanged") {}
    SECTION("Yes carries typed one-run consent despite stored false") { yes = true; }
    SECTION("Yes with unavailable transport leaves sharing unchecked") { yes = true; transportAvailable = false; }
    bool promptSeen = false;
    QTimer::singleShot(0, [yes, &promptSeen] {
        auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        promptSeen = message != nullptr;
        if (message) message->button(yes ? QMessageBox::Yes : QMessageBox::No)->click();
    });
    share->click();
    CHECK(promptSeen);
    CHECK(configureCalls == (yes ? 1 : 0));
    CHECK(requestedConsent == (yes ? RemoteDiagnosticsConsent::ThisRun : RemoteDiagnosticsConsent::ProcessDefault));
    CHECK(share->isChecked() == (yes && transportAvailable));
    CHECK_FALSE(settings.value("remoteDiagnostics/consent").toBool());
    CHECK(InmarsatDiagnosticRecording::status() == recording);
    transportEnabled = false;
    settings.remove("remoteDiagnostics/consent");
}

#ifdef SDR_TOWN_CONSENT_TEST_MAIN
#include <catch2/catch_session.hpp>
#include <QStandardPaths>
#include <QTemporaryDir>
int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    app.setOrganizationName("SDR_Town_Tests"); app.setApplicationName("inmarsat-consent-fixtures");
    QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    return Catch::Session().run(argc, argv);
}
#endif
