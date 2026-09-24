#include "InmarsatDiagnostics.h"
#include "RemoteDiagnostics.h"
#include "InmarsatReplayDialog.h"
#include <QCoreApplication>
#include <QMessageBox>
#include <QJsonDocument>
#include <QJsonObject>

void connectInmarsatRemoteDiagnostics() {
    setInmarsatDiagnosticSink([](const nlohmann::json& payload) {
        const bool failed = payload.contains("errorCode") && payload["errorCode"] != "none";
        remoteDiagnosticsSubmit("inmarsat.session", failed ? "warn" : "info",
            QJsonDocument::fromJson(QByteArray::fromStdString(payload.dump())).object());
    });
}

void configureInmarsatReplaySharing(InmarsatReplayDialog& dialog) {
    const bool forcedOff = QCoreApplication::arguments().contains("--no-remote-diagnostics") ||
        QCoreApplication::arguments().contains("--diag-off") ||
        QCoreApplication::arguments().contains("--diagnostics-off");
    if (forcedOff) { dialog.setSharingControl(false, {}); return; }
    dialog.setSharingControl(remoteDiagnosticsEnabled(), [&dialog]() {
        if (remoteDiagnosticsEnabled()) return true;
        const auto answer = QMessageBox::question(&dialog, "Share diagnostics for this run?",
            "Send decoder counters, timing, format/rate and error categories to the configured SDR Town server? "
            "The existing diagnostics client also sends app/OS details and pseudonymous installation/hardware IDs. "
            "Inmarsat reports exclude IQ, audio, file paths, aircraft IDs and locations. "
            "Local reports stay on this PC. This enables the app diagnostics transport for this run.",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) return false;
        auto args = QCoreApplication::arguments();
        std::vector<QByteArray> bytes;
        for (const auto& arg : args) bytes.push_back(arg.toLocal8Bit());
        std::vector<char*> argv;
        for (auto& arg : bytes) argv.push_back(arg.data());
        const auto cfg = remoteDiagnosticsConfigFromProcess(static_cast<int>(argv.size()), argv.data(), "inmarsat-replay");
        if (!cfg.endpoint.isValid() || cfg.endpoint.scheme() != "https" || cfg.endpoint.host().isEmpty()) {
            QMessageBox::warning(&dialog, "Diagnostics unavailable", "No valid HTTPS collector is configured in this build.");
            return false;
        }
        bytes.push_back("--diag-url"); bytes.push_back(cfg.endpoint.toEncoded());
        argv.clear(); for (auto& arg : bytes) argv.push_back(arg.data());
        return remoteDiagnosticsConfigureFromProcess(static_cast<int>(argv.size()), argv.data(),
            QCoreApplication::instance(), "inmarsat-replay") != nullptr;
    });
}
