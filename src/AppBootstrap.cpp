#include "AppBootstrap.h"

#include "DeviceManager.h"
#include "RemoteDiagnostics.h"

#include <QColor>
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonObject>
#include <QPalette>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QStyleFactory>
#include <QString>
#include <QtGlobal>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#ifdef _WIN32
#include <psapi.h>
#endif

#ifndef SDR_TOWN_VERSION
#define SDR_TOWN_VERSION "0.0.0"
#endif
#ifndef SDR_TOWN_P25_AUDIO_BASELINE
#define SDR_TOWN_P25_AUDIO_BASELINE "p25-clear-continuous-20260810"
#endif

void setupLogging()
{
    // Always install at least a console sink so spdlog never ends up with a completely
    // broken default logger (previous file-only failure could leave later spdlog::info
    // in bad state and contribute to mysterious "program error" after blank window).
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    std::vector<spdlog::sink_ptr> sinks {console_sink};
    std::string logPath;

    try {
        const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        if (!appData.isEmpty()) {
            QDir().mkpath(appData + "/logs");
            logPath = (appData + "/logs/sdr_town.log").toStdString();
            try {
                auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(logPath, 1024*1024*5, 3);
                sinks.push_back(file_sink);
            } catch (const std::exception& ex) {
                qWarning() << "File log sink failed (will use console only):" << ex.what();
            }
        }
    } catch (const std::exception& ex) {
        qWarning() << "AppData log path setup failed:" << ex.what();
    }

    try {
        auto logger = std::make_shared<spdlog::logger>("sdr_town", sinks.begin(), sinks.end());
        spdlog::set_default_logger(logger);
        spdlog::set_level(spdlog::level::info);
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
        spdlog::flush_on(spdlog::level::warn);  // avoid per-info flushes (P25 appends etc) blocking GUI; explicit or periodic for important msgs
        if (!logPath.empty()) {
            spdlog::info("SDR Town logging initialized. Log file: {}", logPath);
        } else {
            spdlog::info("SDR Town logging initialized (console only).");
        }
    } catch (const std::exception& ex) {
        // Last resort: at least try to get something.
        qWarning() << "spdlog setup completely failed:" << ex.what();
    }
}

// Very early crash marker (plain stdio, no Qt, no spdlog) so we can see how far launch got
// even if everything after blows up (helps diagnose blank-GUI + "program error").
void writeEarlyCrashLog(const std::string& stage, const char* extra)
{
    try {
#ifdef _WIN32
        char tmp[MAX_PATH] = {0};
        DWORD len = GetTempPathA(MAX_PATH, tmp);
        std::string p = (len > 0 ? std::string(tmp) : "C:\\Windows\\Temp\\") + "sdr_town_launch.log";
#else
        std::string p = "/tmp/sdr_town_launch.log";
#endif
        std::ofstream f(p, std::ios::app);
        if (f.is_open()) {
            auto now = std::chrono::system_clock::now().time_since_epoch().count();
            f << "[" << now << "] stage=" << stage;
            if (extra) f << " extra=" << extra;
            f << "\n";
        }
    } catch (...) {}
}

static QString diagnosticsLaunchLogPath()
{
#ifdef _WIN32
    char tmp[MAX_PATH] = {0};
    DWORD len = GetTempPathA(MAX_PATH, tmp);
    return QString::fromLocal8Bit(len > 0 ? tmp : "C:\\Windows\\Temp\\") + "sdr_town_launch.log";
#else
    return QStringLiteral("/tmp/sdr_town_launch.log");
#endif
}

QByteArray readFileTailCapped(const QString& path, qint64 maxBytes)
{
    QFile file(path);
    if (path.trimmed().isEmpty() || maxBytes <= 0 || !file.open(QIODevice::ReadOnly)) return {};
    const qint64 size = file.size();
    if (size > maxBytes) file.seek(size - maxBytes);
    return file.read(maxBytes);
}

static QByteArray readFilePrefixCapped(const QString& path, qint64 maxBytes)
{
    QFile file(path);
    if (path.trimmed().isEmpty() || maxBytes <= 0 || !file.open(QIODevice::ReadOnly)) return {};
    return file.read(maxBytes);
}

static QString diagnosticsTextHash(const QString& text, int chars = 16)
{
    return QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256).toHex()).left(chars);
}

QString diagnosticsBytesHash(const QByteArray& bytes, int chars)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()).left(chars);
}

static QString diagnosticsRedactIdentifierLikeText(QString text)
{
    text = text.left(180);
    text.replace(QRegularExpression("\\b[0-9a-fA-F]{8,}\\b"), "<hex>");
    text.replace(QRegularExpression("\\b\\d{6,}\\b"), "<num>");
    text.replace(QRegularExpression("\\s+"), " ");
    return text.trimmed();
}

static QString diagnosticsHashSensitive(const QString& text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) return {};
    return diagnosticsTextHash(trimmed, 20);
}

static bool diagnosticsLooksTextFile(const QFileInfo& info)
{
    const QString suffix = info.suffix().toLower();
    return suffix == "txt" || suffix == "log" || suffix == "json" || suffix == "jsonl" ||
        suffix == "csv" || suffix == "md" || suffix == "sigmf-meta" || suffix == "xml";
}

QJsonObject diagnosticsAttachmentSummary(const QString& path, qint64 maxContentBytes)
{
    QJsonObject out;
    QFileInfo info(path);
    out["pathSelected"] = !path.trimmed().isEmpty();
    if (path.trimmed().isEmpty()) return out;

    out["name"] = info.fileName().left(160);
    out["suffix"] = info.suffix().left(24);
    out["sizeBytes"] = QString::number(info.exists() ? info.size() : 0);
    out["maxContentBytes"] = static_cast<int>(std::clamp<qint64>(maxContentBytes, 0, 64 * 1024));
    out["exists"] = info.exists();
    out["readable"] = info.isReadable();
    if (!info.exists() || !info.isReadable() || info.isDir()) return out;

    const bool text = diagnosticsLooksTextFile(info);
    const QByteArray sample = text
        ? readFileTailCapped(path, maxContentBytes)
        : readFilePrefixCapped(path, maxContentBytes);
    out["sampleBytes"] = sample.size();
    out["truncated"] = info.size() > sample.size();
    out["sampleSha256"] = diagnosticsBytesHash(sample, 32);
    out["contentEncoding"] = text ? "utf8-tail" : "base64-prefix";
    if (text) {
        out["content"] = QString::fromUtf8(sample).left(12000);
    } else {
        out["content"] = QString::fromLatin1(sample.toBase64()).left(24000);
    }
    return out;
}

QJsonArray diagnosticsDeviceInventory(bool includeRuntimeState)
{
    QJsonArray rows;
    try {
        auto& mgr = DeviceManager::instance();
        const auto devices = mgr.getDevices();
        for (size_t i = 0; i < devices.size() && i < 12; ++i) {
            const auto& d = devices[i];
            QJsonObject row;
            row["index"] = static_cast<int>(i);
            row["driver"] = QString::fromStdString(d.driver).left(64);
            row["label"] = diagnosticsRedactIdentifierLikeText(QString::fromStdString(d.label));
            row["hardware"] = diagnosticsRedactIdentifierLikeText(QString::fromStdString(d.hardware));
            row["stableKeyHash"] = diagnosticsHashSensitive(QString::fromStdString(d.stableKey));
            row["serialHash"] = diagnosticsHashSensitive(QString::fromStdString(d.serial));
            row["enabled"] = d.enabled;
            row["sampleRateHz"] = d.sampleRate;
            row["gainDb"] = d.gain;
            row["gainMinDb"] = d.gainMin;
            row["gainMaxDb"] = d.gainMax;
            row["ppm"] = d.frequencyCorrectionPpm;
            row["antenna"] = QString::fromStdString(d.antenna).left(80);
            if (includeRuntimeState) {
                row["streaming"] = mgr.isStreaming(i);
                row["runtimeState"] = QString::fromStdString(mgr.getRuntimeStateLabel(i)).left(160);
            }
            rows.append(row);
        }
    } catch (...) {
        QJsonObject row;
        row["error"] = "device inventory unavailable";
        rows.append(row);
    }
    return rows;
}

QJsonObject diagnosticsSystemHealthPayload()
{
    QJsonObject payload;
    payload["os"] = QSysInfo::prettyProductName().left(120);
    payload["cpuArch"] = QSysInfo::currentCpuArchitecture().left(40);
    payload["kernel"] = (QSysInfo::kernelType() + " " + QSysInfo::kernelVersion()).left(80);
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!appData.isEmpty()) {
        QStorageInfo storage(appData);
        payload["appDataBytesAvailable"] = QString::number(storage.bytesAvailable());
        payload["appDataBytesTotal"] = QString::number(storage.bytesTotal());
        payload["appDataVolumeReady"] = storage.isReady();
    }
#ifdef _WIN32
    MEMORYSTATUSEX mem{};
    mem.dwLength = sizeof(mem);
    if (GlobalMemoryStatusEx(&mem)) {
        payload["memoryLoadPercent"] = static_cast<int>(mem.dwMemoryLoad);
        payload["physicalAvailableMb"] = static_cast<double>(mem.ullAvailPhys) / (1024.0 * 1024.0);
        payload["physicalTotalMb"] = static_cast<double>(mem.ullTotalPhys) / (1024.0 * 1024.0);
    }
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(),
                             reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
                             sizeof(pmc))) {
        payload["processWorkingSetMb"] = static_cast<double>(pmc.WorkingSetSize) / (1024.0 * 1024.0);
        payload["processPrivateUsageMb"] = static_cast<double>(pmc.PrivateUsage) / (1024.0 * 1024.0);
    }
#endif
    return payload;
}

QJsonArray diagnosticsSystemHealthPressureReasons(const QJsonObject& payload)
{
    QJsonArray reasons;
    const double memLoad = payload.value("memoryLoadPercent").toDouble(0.0);
    const double privateMb = payload.value("processPrivateUsageMb").toDouble(0.0);
    const double availMb = payload.value("physicalAvailableMb").toDouble(4096.0);
    if (memLoad >= 94.0) reasons.append("high_system_memory_load");
    if (privateMb >= 2500.0) reasons.append("high_process_private_memory");
    if (availMb <= 256.0) reasons.append("low_physical_memory");

    const QString storageText = payload.value("appDataBytesAvailable").toString();
    if (!storageText.isEmpty()) {
        bool ok = false;
        const qlonglong available = storageText.toLongLong(&ok);
        if (ok && available <= 512ll * 1024ll * 1024ll) {
            reasons.append("low_appdata_free_space");
        }
    }
    return reasons;
}

QString diagnosticsSystemHealthPressureSummary(const QJsonArray& reasons)
{
    QStringList parts;
    for (const QJsonValue& value : reasons) {
        const QString text = value.toString().trimmed();
        if (!text.isEmpty()) parts << text;
    }
    return parts.isEmpty() ? QStringLiteral("none") : parts.join(',');
}

bool diagnosticsSystemHealthLooksBad(const QJsonObject& payload)
{
    return !diagnosticsSystemHealthPressureReasons(payload).isEmpty();
}

void submitPreviousLaunchCrashIfAny()
{
    if (!remoteDiagnosticsEnabled()) return;
    const QString path = diagnosticsLaunchLogPath();
    const QByteArray tail = readFileTailCapped(path, 8192);
    if (tail.isEmpty()) return;
    const QString text = QString::fromUtf8(tail);
    const bool hasCrashMarker =
        text.contains("seh-unhandled", Qt::CaseInsensitive) ||
        text.contains("std-exception", Qt::CaseInsensitive) ||
        text.contains("unknown-exception", Qt::CaseInsensitive);
    if (!hasCrashMarker) return;

    const QString signature = diagnosticsBytesHash(tail, 32);
    QSettings settings;
    if (settings.value("remoteDiagnostics/lastLaunchCrashSignature").toString() == signature) return;
    settings.setValue("remoteDiagnostics/lastLaunchCrashSignature", signature);

    QJsonObject payload;
    payload["stage"] = "previous-launch";
    payload["message"] = "Previous launch log contains crash/exception marker";
    payload["launchLogTail"] = text.left(7000);
    payload["launchLogTailHash"] = signature;
    payload["system"] = diagnosticsSystemHealthPayload();
    payload["devices"] = diagnosticsDeviceInventory(true);
    remoteDiagnosticsSubmit("app.crash", "error", payload);
}

std::string startupLowerArg(const char* raw)
{
    std::string out = raw ? std::string(raw) : std::string();
    const size_t eq = out.find('=');
    if (eq != std::string::npos) out = out.substr(0, eq);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

bool startupHasArg(int argc, char* argv[], std::initializer_list<const char*> names,
                          bool ignoreCommandPayload)
{
    for (int i = 1; i < argc; ++i) {
        const std::string arg = startupLowerArg(argv[i]);
        if (ignoreCommandPayload &&
            (arg == "--cmd" || arg == "--command" || arg == "--exec")) {
            const std::string raw = argv[i] ? std::string(argv[i]) : std::string();
            if (raw.find('=') == std::string::npos) break;
            continue;
        }
        for (const char* name : names) {
            if (arg == name) return true;
        }
    }
    return false;
}

void printStartupUsage()
{
    std::cout
        << "SDR Town " << SDR_TOWN_VERSION << "\n\n"
        << "Usage:\n"
        << "  SDR_Town.exe                         Start the GUI\n"
        << "  SDR_Town.exe --cli                   Start interactive CLI mode\n"
        << "  SDR_Town.exe --cli --cmd \"p25 ...\"   Run one CLI command, then exit\n"
        << "  SDR_Town.exe --version               Print version and exit\n"
        << "  --gui-workspace listening|trunking|hf|analysis  Select layout only\n"
        << "  --gui-bandplan AU|GB|US|LOCAL_ID      Select receive plan for this run\n"
        << "  --gui-window-size WIDTHxHEIGHT --gui-screenshot FILE.png  GUI layout QA\n"
        << "  SDR_Town.exe --help                  Print this help and exit\n\n"
        << "Inmarsat Classic Aero replay (experimental voice / ADS-C map):\n"
        << "  --inmarsat-replay                    Open isolated IQ replay window\n"
        << "  [--cli] --inmarsat-iq FILE           Replay in GUI or CLI without SDR hardware\n"
        << "  --inmarsat-format TYPE --inmarsat-rate HZ --inmarsat-center HZ  Raw IQ settings\n"
        << "  --inmarsat-channel HZ --inmarsat-mode 600|1200|8400|10500|egc\n"
        << "  Burst data modes: 1200-burst | 10500-burst\n"
        << "  --inmarsat-fast --inmarsat-result FILE.json --inmarsat-log-dir DIRECTORY\n"
        << "  --inmarsat-play-audio               Default speaker (real-time only)\n"
        << "  --inmarsat-wav NEW.wav              Save decoded 8 kHz mono PCM\n"
        << "  --inmarsat-exit-complete             Exit GUI replay at EOF/error\n\n"
        << "GUI automation examples:\n"
        << "  SDR_Town.exe --p25-cc 420.350 --gui-auto-follow --gui-default-audio\n"
        << "  SDR_Town.exe --freq 476.4625 --start-device --default-audio\n"
        << "  SDR_Town.exe --control-port 8765\n\n"
        << "Local app control bridge:\n"
        << "  Local loopback JSON control starts by default for SdrTownControl.dll\n"
        << "  --no-control-server              Disable the loopback control server\n"
        << "  --control-server                 Explicitly enable loopback JSON control\n"
        << "  --control-port N                 Loopback control port, default 8765\n"
        << "  --control-token TOKEN            Require bearer token from local clients\n\n"
        << "Remote diagnostics, when explicitly enabled, compact JSON only:\n"
        << "  SDR_Town.exe --diag-url https://host/ingest --diag-token <token>\n"
        << "  --no-remote-diagnostics              Force all remote reporting off\n"
        << "  env: SDR_TOWN_DIAG_URL, SDR_TOWN_DIAG_TOKEN, SDR_TOWN_DIAG_MAX_BYTES_PER_MIN\n\n"
        << "Safety:\n"
        << "  SDR Town allows one running instance by default so two copies cannot fight\n"
        << "  over the same RTL-SDR/Soapy device. Use --allow-multiple only for lab work.\n";
}

#ifdef _WIN32
LONG WINAPI sehTopLevelFilter(EXCEPTION_POINTERS* info)
{
    std::string codeStr = "code=" + std::to_string(info ? info->ExceptionRecord->ExceptionCode : 0);
    writeEarlyCrashLog("seh-unhandled", codeStr.c_str());
    MessageBoxA(nullptr, "SDR Town encountered a fatal error during startup and will close.\n\nA launch log was written to %TEMP%\\sdr_town_launch.log and the normal sdr_town.log (if any).\n\nTry running from the build\\bin\\Release folder, re-install VC++ runtimes, ensure Zadig WinUSB for RTL if using hardware, and check that no other SDR app has the dongle.", "SDR Town - Program Error", MB_ICONERROR | MB_OK);
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

void applyDarkTheme(QApplication& app)
{
    app.setStyle(QStyleFactory::create("Fusion"));

    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, QColor(45, 45, 48));
    darkPalette.setColor(QPalette::WindowText, Qt::white);
    darkPalette.setColor(QPalette::Base, QColor(30, 30, 32));
    darkPalette.setColor(QPalette::AlternateBase, QColor(45, 45, 48));
    darkPalette.setColor(QPalette::ToolTipBase, Qt::white);
    darkPalette.setColor(QPalette::ToolTipText, Qt::white);
    darkPalette.setColor(QPalette::Text, Qt::white);
    darkPalette.setColor(QPalette::Button, QColor(60, 60, 63));
    darkPalette.setColor(QPalette::ButtonText, Qt::white);
    darkPalette.setColor(QPalette::BrightText, Qt::red);
    darkPalette.setColor(QPalette::Link, QColor(42, 130, 218));
    darkPalette.setColor(QPalette::Highlight, QColor(42, 130, 218));
    darkPalette.setColor(QPalette::HighlightedText, Qt::black);

    app.setPalette(darkPalette);

    app.setStyleSheet(
        "QMenuBar { background-color: #2d2d30; color: white; }"
        "QMenuBar::item:selected { background-color: #3e3e42; }"
        "QMenu { background-color: #2d2d30; color: white; border: 1px solid #3e3e42; }"
        "QMenu::item:selected { background-color: #3e3e42; }"
        "QStatusBar { background-color: #2d2d30; color: #cccccc; }"
        "QToolTip { color: #ffffff; background-color: #2d2d30; border: 1px solid #3e3e42; }"
    );
}

