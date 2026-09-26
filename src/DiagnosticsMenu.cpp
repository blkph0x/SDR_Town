#include "DiagnosticsMenu.h"
#include "RemoteDiagnostics.h"
#include <QAction>
#include <QCoreApplication>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QSignalBlocker>
#include <vector>

void installDiagnosticsMenu(QMainWindow& window) {
    QMenu* menu = nullptr;
    for (auto* action : window.menuBar()->actions()) {
        if (action->menu() && action->text().remove('&') == "Help") {
            menu = action->menu(); break;
        }
    }
    if (!menu) menu = window.menuBar()->addMenu("&Help");
    auto* action = menu->addAction("Share Diagnostic &Reports");
    action->setObjectName("diagnosticsConsentAction");
    action->setCheckable(true);
    action->setChecked(remoteDiagnosticsEnabled());
    const auto args = QCoreApplication::arguments();
    const bool forcedOff = args.contains("--no-remote-diagnostics") ||
        args.contains("--diag-off") || args.contains("--diagnostics-off");
    action->setEnabled(!forcedOff);
    if (forcedOff) action->setToolTip("Disabled by the launch options");
    QObject::connect(action, &QAction::triggered, &window, [&window, action](bool enabled) {
        if (enabled && QMessageBox::question(&window, "Share diagnostic reports?",
            "Send technical error reports, decoder counters, CPU/memory usage, thread counts, "
            "app/OS and radio details, and pseudonymous installation/device IDs to the SDR Town diagnostics server? "
            "Automatic reports do not upload IQ recordings or audio. Manual issue reports may include text you choose. "
            "Reports are bandwidth-limited. This choice is saved; you can turn it off here at any time.",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
            QSignalBlocker block(action); action->setChecked(false); return;
        }
        QSettings settings;
        settings.setValue("remoteDiagnostics/consent", enabled);
        settings.sync();
        if (settings.status() != QSettings::NoError) {
            QMessageBox::warning(&window, "Diagnostics", "The sharing preference could not be saved.");
            QSignalBlocker block(action); action->setChecked(remoteDiagnosticsEnabled()); return;
        }
        if (!enabled) {
            // Consent withdrawal discards pending reports instead of flushing them.
            remoteDiagnosticsShutdown(false);
            return;
        }
        if (!remoteDiagnosticsEnabled()) {
            std::vector<QByteArray> bytes;
            for (const auto& arg : QCoreApplication::arguments()) bytes.push_back(arg.toLocal8Bit());
            std::vector<char*> argv;
            for (auto& arg : bytes) argv.push_back(arg.data());
            if (!remoteDiagnosticsConfigureFromProcess(int(argv.size()), argv.data(), QCoreApplication::instance(), "gui")) {
                QMessageBox::warning(&window, "Diagnostics", "Diagnostics could not be enabled with the current server configuration.");
                QSignalBlocker block(action); action->setChecked(false);
            }
        }
    });
}
