#include <QApplication>
#include <QJsonObject>
#include <QString>

#include <spdlog/spdlog.h>

#include "AppBootstrap.h"
#include "CliApp.h"
#include "MainWindow.h"
#include "P25DebugStage.h"
#include "RemoteDiagnostics.h"

#include <iostream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

#ifndef SDR_TOWN_VERSION
#define SDR_TOWN_VERSION "0.0.0"
#endif
#ifndef SDR_TOWN_P25_AUDIO_BASELINE
#define SDR_TOWN_P25_AUDIO_BASELINE "p25-clear-continuous-20260810"
#endif

// Leftover helpers extracted ISS-0004 Phase A:
//   P25VoiceSession, P25DecodeConfig, DemodModeUtils, SavedFrequencies, CliApp (GUI runtime).

int main(int argc, char *argv[])
{
    writeEarlyCrashLog("main-entry");

#ifdef _WIN32
    // Register SEH filter early so even access violations / driver faults inside Qt ctor or
    // first paint / timers produce a useful message + marker instead of the generic
    // "program error" blank-GUI-then-crash the user sees.
    SetUnhandledExceptionFilter(sehTopLevelFilter);
#endif

    const bool wantsHelp = startupHasArg(argc, argv, {"--help", "-h", "/?", "help"}, true);
    const bool wantsVersion = startupHasArg(argc, argv, {"--version", "-v", "version"}, true);
    const bool wantsCli = startupHasArg(argc, argv, {"--cli", "-c", "--console"});
    const bool allowMultiple = startupHasArg(argc, argv, {"--allow-multiple", "--multi-instance"});

    if (wantsHelp) {
        printStartupUsage();
        return 0;
    }
    if (wantsVersion) {
        std::cout << "SDR Town " << SDR_TOWN_VERSION
                  << " p25_baseline=" << SDR_TOWN_P25_AUDIO_BASELINE << "\n";
        return 0;
    }

#ifdef _WIN32
    ProcessInstanceGuard instanceGuard;
    if (!allowMultiple && !instanceGuard.acquire()) {
        const std::string msg =
            "Another SDR Town instance is already running. Close it before starting a new GUI or CLI capture, "
            "otherwise both processes can compete for the same RTL-SDR/Soapy device and P25 grants/audio may disappear.";
        writeEarlyCrashLog("single-instance-blocked", msg.c_str());
        if (wantsCli) {
            std::cout << msg << "\n";
        } else {
            MessageBoxA(nullptr, msg.c_str(), "SDR Town - Already Running", MB_ICONWARNING | MB_OK);
        }
        return 2;
    }
    if (!allowMultiple && instanceGuard.error() != ERROR_SUCCESS &&
        instanceGuard.error() != ERROR_ALREADY_EXISTS &&
        instanceGuard.error() != ERROR_ACCESS_DENIED) {
        writeEarlyCrashLog("single-instance-mutex-warning");
    }
#endif

    if (wantsCli) {
        writeEarlyCrashLog("cli-path");
        return runCLI(argc, argv);
    }
    const GuiRuntimeConfig guiConfig = parseGuiRuntimeConfig(argc, argv);
    if (!guiConfig.debugStage.empty()) {
        gP25DebugStageFilter = p25ParseDebugStage(guiConfig.debugStage);
        spdlog::info("P25 debug stage filter: {}", p25DebugStageLabel(gP25DebugStageFilter));
    }

    int ret = 1;
    bool guiEventLoopReturned = false;
    try {
        writeEarlyCrashLog("before-qapp");
        QApplication app(argc, argv);
        app.setApplicationName("SDR Town");
        app.setOrganizationName("SDR_Town");
        app.setApplicationVersion(SDR_TOWN_VERSION);
        remoteDiagnosticsConfigureFromProcess(argc, argv, &app, "gui");

        setupLogging();
        applyDarkTheme(app);

        spdlog::info("Starting SDR Town v{}.", app.applicationVersion().toStdString());
        if (remoteDiagnosticsEnabled()) {
            QJsonObject payload;
            payload["mode"] = "gui";
            payload["version"] = SDR_TOWN_VERSION;
            payload["startupWork"] = guiConfig.hasStartupWork();
            payload["autoFollow"] = guiConfig.autoFollow;
            payload["p25Monitor"] = guiConfig.p25Monitor;
            payload["p25GrantTest"] = guiConfig.p25GrantTest;
            payload["defaultAudio"] = guiConfig.defaultAudio;
            remoteDiagnosticsSubmit("app.start", "info", payload);
            submitPreviousLaunchCrashIfAny();
        }
        spdlog::default_logger()->flush();
        writeEarlyCrashLog("before-mainwindow");

        MainWindow w(guiConfig);

        w.show();

        spdlog::info("Main window shown. Entering Qt event loop.");
        spdlog::default_logger()->flush();
        writeEarlyCrashLog("entering-exec");
        ret = app.exec();
        guiEventLoopReturned = true;

        spdlog::info("Application exiting with code {}.", ret);
    } catch (const std::exception& ex) {
        if (guiEventLoopReturned) {
            writeEarlyCrashLog("shutdown-std-exception", ex.what());
            try {
                spdlog::warn("Non-fatal exception during GUI shutdown after Qt event loop returned: {}", ex.what());
            } catch (...) {}
            if (remoteDiagnosticsEnabled()) {
                QJsonObject payload;
                payload["mode"] = "gui";
                payload["stage"] = "shutdown";
                payload["exceptionType"] = "std";
                payload["message"] = QString::fromLocal8Bit(ex.what()).left(500);
                payload["returnCode"] = ret;
                remoteDiagnosticsSubmit("app.exception", "warn", payload);
            }
            remoteDiagnosticsShutdown();
            try { spdlog::default_logger()->flush(); } catch (...) {}
            try { spdlog::shutdown(); } catch (...) {}
            return ret;
        }
        writeEarlyCrashLog("std-exception", ex.what());
        try { spdlog::error("Fatal exception in main: {}", ex.what()); } catch (...) {}
        if (remoteDiagnosticsEnabled()) {
            QJsonObject payload;
            payload["mode"] = "gui";
            payload["stage"] = "startup-or-runtime";
            payload["exceptionType"] = "std";
            payload["message"] = QString::fromLocal8Bit(ex.what()).left(500);
            remoteDiagnosticsSubmit("app.exception", "error", payload);
        }
        MessageBoxA(nullptr,
            (std::string("SDR Town failed to start or crashed.\n\nDetails: ") + ex.what() +
             "\n\nSee %TEMP%\\sdr_town_launch.log and the sdr_town log in AppData for more.\n"
             "Run from the Release folder next to its DLLs/plugins. Re-run windeployqt after rebuilds.").c_str(),
            "SDR Town - Startup Error", MB_ICONERROR | MB_OK);
    } catch (...) {
        if (guiEventLoopReturned) {
            writeEarlyCrashLog("shutdown-unknown-exception");
            try {
                spdlog::warn("Non-fatal unknown exception during GUI shutdown after Qt event loop returned.");
            } catch (...) {}
            if (remoteDiagnosticsEnabled()) {
                QJsonObject payload;
                payload["mode"] = "gui";
                payload["stage"] = "shutdown";
                payload["exceptionType"] = "unknown";
                payload["returnCode"] = ret;
                remoteDiagnosticsSubmit("app.exception", "warn", payload);
            }
            remoteDiagnosticsShutdown();
            try { spdlog::default_logger()->flush(); } catch (...) {}
            try { spdlog::shutdown(); } catch (...) {}
            return ret;
        }
        writeEarlyCrashLog("unknown-exception");
        try { spdlog::error("Unknown fatal exception in main GUI path."); } catch (...) {}
        if (remoteDiagnosticsEnabled()) {
            QJsonObject payload;
            payload["mode"] = "gui";
            payload["stage"] = "startup-or-runtime";
            payload["exceptionType"] = "unknown";
            remoteDiagnosticsSubmit("app.exception", "error", payload);
        }
        MessageBoxA(nullptr,
            "SDR Town failed with an unknown exception during startup.\n\n"
            "Check %TEMP%\\sdr_town_launch.log . Ensure you are running the exe from build\\bin\\Release "
            "(with all the copied Qt6*.dll + platforms\\qwindows.dll + pthreadVC2.dll + Soapy/RTL bits present).",
            "SDR Town - Program Error", MB_ICONERROR | MB_OK);
    }

    remoteDiagnosticsShutdown();
    try { spdlog::shutdown(); } catch (...) {}
    return ret;
}
