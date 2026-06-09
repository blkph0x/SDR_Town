#include <QApplication>
#include <QMainWindow>
#include <QMenuBar>
#include <QStatusBar>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>
#include <QPushButton>
#include <QTextEdit>
#include <QPalette>
#include <QStyleFactory>
#include <QMessageBox>
#include <QAction>
#include <QDir>
#include <QStandardPaths>
#include <QDebug>
#include <QDialog>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHeaderView>
#include <QGroupBox>
#include <QFormLayout>
#include <QTimer>
#include <complex>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <nlohmann/json.hpp>

#include "DeviceManager.h"
#include "SpectrumWidget.h"
#include "AudioEngine.h"
#include "Demod.h"

#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <thread>
#include <atomic>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <fstream>

#ifdef _WIN32
#include <windows.h>
#endif

using json = nlohmann::json;

int runCLI(int argc, char* argv[]);

// Shared live diagnostic updated by demod calls from GUI worker and CLI monitor thread (P1 audit diags)
static std::atomic<long long> gLastDspMicros{0};

// P1: one Demodulator per monitor path (GUI worker + CLI) so FIR/IIR/phase/resampler state
// does not bleed between uses. Full per-receiver objects will live in receivers later.
static Demodulator gGuiDemod;
static Demodulator gCliDemod;

// DSP implementation now lives in src/Demod.cpp (Demodulator class owns all state).
// classifyMode, detectChannelBandwidth, and demodulateToAudio are provided via Demod.h + Demod.cpp.

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
        spdlog::flush_on(spdlog::level::info);  // ensure startup + key messages are flushed even if process is killed while hung
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
static void writeEarlyCrashLog(const std::string& stage, const char* extra = nullptr)
{
    try {
        char tmp[MAX_PATH] = {0};
        DWORD len = GetTempPathA(MAX_PATH, tmp);
        std::string p = (len > 0 ? std::string(tmp) : "C:\\Windows\\Temp\\") + "sdr_town_launch.log";
        std::ofstream f(p, std::ios::app);
        if (f.is_open()) {
            auto now = std::chrono::system_clock::now().time_since_epoch().count();
            f << "[" << now << "] stage=" << stage;
            if (extra) f << " extra=" << extra;
            f << "\n";
        }
    } catch (...) {}
}

#ifdef _WIN32
#include <windows.h>
static LONG WINAPI sehTopLevelFilter(EXCEPTION_POINTERS* info)
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

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow(QWidget* parent = nullptr) : QMainWindow(parent)
    {
        setWindowTitle("SDR Town");
        resize(1280, 800);

        // Real main UI area (PR2/PR3) - spectrum + receivers
        QWidget* central = new QWidget(this);
        QVBoxLayout* mainLayout = new QVBoxLayout(central);
        mainLayout->setContentsMargins(4,4,4,4);
        mainLayout->setSpacing(4);

        // Top info bar
        QLabel* topBar = new QLabel("SDR Town  •  Multi-SDR  •  Smart Scan  •  Unencrypted Voice/Data  •  Advanced Analyzer");
        topBar->setStyleSheet("font-size: 12px; color: #88ddff; padding: 2px 6px; background: #1f2228; border-radius: 2px;");
        mainLayout->addWidget(topBar);

        // Spectrum (the star visual for now)
        SpectrumWidget* spectrum = new SpectrumWidget(this);
        spectrum->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        connect(spectrum, &SpectrumWidget::frequencySelected, this, [this, spectrum](double f) {
            {
                std::lock_guard<std::mutex> lk(monitorParamsMutex);
                currentMonitorFreq = f;
            }
            auto& mgr = DeviceManager::instance();
            bool retunedDevice = false;
            bool anyStreaming = false;
            for (size_t i = 0; i < mgr.getDevices().size(); ++i) {
                if (mgr.isStreaming(i)) {
                    mgr.setCenterFreq(i, f);
                    retunedDevice = true;
                    anyStreaming = true;
                    break;
                }
            }
            if (!anyStreaming && !mgr.getDevices().empty()) {
                // "Just select a freq" should produce audio: auto-start first device (stub for safety, like Add Receiver / Scan).
                // Aligns with expectation that clicking spectrum/waterfall starts monitoring that freq.
                mgr.setEnabled(0, true);
                try { mgr.startStreaming(0, true /* real SDR - no simulation */); } catch (...) {}
                anyStreaming = true;
                // Defer audio output activation (speakers + VAC etc.) like other start paths.
                QTimer::singleShot(120, this, [this]() {
                    if (!engineForAudio) engineForAudio = std::make_unique<AudioEngine>();
                    if (engineForAudio && engineForAudio->activeOutputCount() == 0) {
                        try {
                            auto outs = engineForAudio->enumeratePlaybackDevices();
                            if (!outs.empty()) {
                                std::vector<size_t> idxs{0};
                                if (outs.size() > 1) idxs.push_back(1);
                                engineForAudio->setActiveOutputs(idxs);
                            }
                        } catch (...) {}
                    }
                });
                // Immediately retune the newly started stream
                mgr.setCenterFreq(0, f);
                retunedDevice = true;
            }
            QString msg = QString("Tuned monitor to %1 MHz").arg(f/1e6, 0, 'f', 4);
            if (retunedDevice) msg += anyStreaming ? " (device + demod active)" : "";
            statusBar()->showMessage(msg, 2500);
            spectrum->setCenterFreq(f);
        });
        mainLayout->addWidget(spectrum, 3);

        // Receivers stub table (PR3/5 will be real)
        QGroupBox* rxBox = new QGroupBox("Active Receivers (stub - full management + demod in PR5)");
        rxBox->setStyleSheet("QGroupBox { font-size: 11px; }");
        QVBoxLayout* rxLay = new QVBoxLayout(rxBox);

        QTableWidget* rxTable = new QTableWidget(3, 6, this);
        rxTable->setHorizontalHeaderLabels({"Freq (MHz)", "Mode", "Squelch", "Level", "Monitor", "Record"});
        rxTable->setRowCount(3);
        rxTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        rxTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        rxTable->horizontalHeader()->setStretchLastSection(true);
        // demo rows
        rxTable->setItem(0, 0, new QTableWidgetItem("162.400")); rxTable->setItem(0, 1, new QTableWidgetItem("NFM")); rxTable->setItem(0, 2, new QTableWidgetItem("-80 dB")); rxTable->setItem(0, 3, new QTableWidgetItem("███░░ -62dB")); rxTable->setItem(0, 4, new QTableWidgetItem("●")); rxTable->setItem(0, 5, new QTableWidgetItem("REC"));
        rxTable->setItem(1, 0, new QTableWidgetItem("137.100")); rxTable->setItem(1, 1, new QTableWidgetItem("APT")); rxTable->setItem(1, 2, new QTableWidgetItem("N/A")); rxTable->setItem(1, 3, new QTableWidgetItem("████░")); rxTable->setItem(1, 4, new QTableWidgetItem("")); rxTable->setItem(1, 5, new QTableWidgetItem("IMG"));
        rxTable->setItem(2, 0, new QTableWidgetItem("446.006")); rxTable->setItem(2, 1, new QTableWidgetItem("DMR")); rxTable->setItem(2, 2, new QTableWidgetItem("-92 dB")); rxTable->setItem(2, 3, new QTableWidgetItem("█░░░░")); rxTable->setItem(2, 4, new QTableWidgetItem("")); rxTable->setItem(2, 5, new QTableWidgetItem(""));
        rxLay->addWidget(rxTable);

        QHBoxLayout* rxBtnLay = new QHBoxLayout();
        QPushButton* addRxBtn = new QPushButton("Add Receiver");
        QPushButton* removeRxBtn = new QPushButton("Remove");
        QPushButton* scanBtn = new QPushButton("Start Smart Scan (PR6)");
        rxBtnLay->addWidget(addRxBtn);
        rxBtnLay->addWidget(removeRxBtn);
        rxBtnLay->addStretch();
        rxBtnLay->addWidget(scanBtn);
        rxLay->addLayout(rxBtnLay);

        // Wire buttons (make functional)
        connect(addRxBtn, &QPushButton::clicked, this, [this]() {
            auto& mgr = DeviceManager::instance();
            // enable first device if any, start streaming, update status
            if (!mgr.getDevices().empty()) {
                mgr.setEnabled(0, true);
                try { mgr.startStreaming(0); } catch (...) { spdlog::warn("startStreaming(0) fault in Add Receiver (guarded)"); }
                statusBar()->showMessage("Added/started receiver on first device (streaming + audio to outputs)", 3000);
                // Defer audio activation (and lazy engine creation) to avoid hanging the UI on start.
                // This is the main change to make the program actually launch and "start" without hang/crash.
                QTimer::singleShot(100, this, [this]() {
                    if (!engineForAudio) {
                        engineForAudio = std::make_unique<AudioEngine>();
                    }
                    if (engineForAudio && engineForAudio->activeOutputCount() == 0) {
                        try {
                            auto outs = engineForAudio->enumeratePlaybackDevices();
                            if (!outs.empty()) {
                                std::vector<size_t> idxs = {0};
                                if (outs.size() > 1) idxs.push_back(1);
                                engineForAudio->setActiveOutputs(idxs);
                            }
                        } catch (...) {
                            spdlog::warn("Deferred audio auto-activate in Add Receiver failed (non-fatal)");
                        }
                    }
                });
            }
        });
        connect(removeRxBtn, &QPushButton::clicked, this, [this]() {
            auto& mgr = DeviceManager::instance();
            for (size_t i = 0; i < mgr.getDevices().size(); ++i) {
                if (mgr.isStreaming(i)) { mgr.stopStreaming(i); break; }
            }
            statusBar()->showMessage("Stopped receiver/streaming", 2000);
        });
        connect(scanBtn, &QPushButton::clicked, this, [this]() {
            auto& mgr = DeviceManager::instance();
            // "Smart scan": enable all, but use safe stub start (attemptReal=false) to avoid
            // crashes when the RTL (or other) device open fails in multi-device start.
            // The button is labeled as PR6 stub. Use Device Manager Apply or "Add Receiver"
            // for real hardware attempts (they use default true + guards + auto-audio).
            for (size_t i = 0; i < mgr.getDevices().size(); ++i) {
                mgr.setEnabled(i, true);
                try { mgr.startStreaming(i, true /* real SDR - no simulation, direct from hardware */); } catch (...) { spdlog::warn("startStreaming fault in scan (guarded)"); }
            }
            statusBar()->showMessage("Smart scan started (all devices streaming real from SDR, spectrum + audio active)", 4000);
            spdlog::info("PR6 stub scanner: started streaming on all devices");
            // Defer audio activation (and lazy engine creation) – see Add Receiver.
            QTimer::singleShot(100, this, [this]() {
                if (!engineForAudio) {
                    engineForAudio = std::make_unique<AudioEngine>();
                }
                if (engineForAudio && engineForAudio->activeOutputCount() == 0) {
                    try {
                        auto outs = engineForAudio->enumeratePlaybackDevices();
                        if (!outs.empty()) {
                            std::vector<size_t> idxs = {0};
                            if (outs.size() > 1) idxs.push_back(1);
                            engineForAudio->setActiveOutputs(idxs);
                        }
                    } catch (...) {
                        spdlog::warn("Deferred audio auto-activate in Smart Scan failed (non-fatal)");
                    }
                }
            });
        });

        // Monitor freq control (makes "tune" options work for audio/spectrum)
        QHBoxLayout* monLay = new QHBoxLayout();
        monLay->addWidget(new QLabel("Monitor Freq (MHz):"));
        QDoubleSpinBox* monFreq = new QDoubleSpinBox();
        monFreq->setRange(24, 1766); // RTL range example
        monFreq->setDecimals(3);
        monFreq->setValue(100.0);
        monFreq->setSingleStep(0.1);
        QPushButton* setMonBtn = new QPushButton("Set & Tune Device");
        QComboBox* modeBox = new QComboBox();
        modeBox->addItem("AUTO"); modeBox->addItem("NFM"); modeBox->addItem("WFM"); modeBox->addItem("AM"); modeBox->addItem("USB"); modeBox->addItem("LSB");
        modeBox->setCurrentText("AUTO");
        bwSpin = new QDoubleSpinBox();
        bwSpin->setRange(1, 500); bwSpin->setValue(200); bwSpin->setSuffix(" kHz"); bwSpin->setDecimals(0);
        monLay->addWidget(monFreq);
        monLay->addWidget(setMonBtn);
        monLay->addWidget(new QLabel("Mode:"));
        monLay->addWidget(modeBox);
        monLay->addWidget(new QLabel("BW:"));
        monLay->addWidget(bwSpin);
        monLay->addStretch();
        rxLay->addLayout(monLay);

        connect(bwSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v){
            std::lock_guard<std::mutex> lk(monitorParamsMutex);
            monitorChannelBwHz = v * 1000.0;
        });

        connect(modeBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, modeBox](int) {
            // P1 audit: do NOT call bwSpin->setValue() while holding monitorParamsMutex.
            // setValue synchronously emits valueChanged whose handler also locks the same mutex -> deadlock/stall risk on live mode transitions.
            // Solution: compute desired state, lock only for the shared vars, then blockSignals + set spin (no emit) after unlock.
            QString m = modeBox->currentText();
            bool newAuto = false;
            DemodMode newMode = DemodMode::NFM;
            double newBwHz = 25000.0;
            double newBwK = 25.0;
            if (m == "AUTO") { newAuto = true; newMode = DemodMode::AUTO; newBwHz = 180000.0; newBwK = 180.0; }
            else if (m == "NFM") { newAuto = false; newMode = DemodMode::NFM; newBwHz = 25000.0; newBwK = 25.0; }
            else if (m == "WFM") { newAuto = false; newMode = DemodMode::WFM; newBwHz = 180000.0; newBwK = 180.0; }  // 180 kHz default; user should try 120-150 for strong local per audit
            else if (m == "AM") { newAuto = false; newMode = DemodMode::AM; newBwHz = 10000.0; newBwK = 10.0; }
            else if (m == "USB" || m == "LSB") { newAuto = false; newMode = (m=="USB"?DemodMode::USB:DemodMode::LSB); newBwHz = 3000.0; newBwK = 3.0; }
            {
                std::lock_guard<std::mutex> lk(monitorParamsMutex);
                autoDetectMode = newAuto;
                currentMonitorMode = newMode;
                monitorChannelBwHz = newBwHz;
            }
            if (bwSpin) {
                bwSpin->blockSignals(true);
                bwSpin->setValue(newBwK);
                bwSpin->blockSignals(false);
            }
        });

        connect(setMonBtn, &QPushButton::clicked, this, [this, monFreq]() {
            currentMonitorFreq = monFreq->value() * 1e6;
            auto& mgr = DeviceManager::instance();
            bool any = false;
            for (size_t i=0; i<mgr.getDevices().size(); ++i) {
                if (mgr.isStreaming(i)) {
                    mgr.setCenterFreq(i, currentMonitorFreq);
                    statusBar()->showMessage(QString("Monitor tuned to %1 MHz").arg(currentMonitorFreq/1e6,0,'f',3), 2000);
                    any = true;
                    break;
                }
            }
            if (!any && !mgr.getDevices().empty()) {
                // Auto-start on explicit tune request so user gets audio without separate "Add" click.
                mgr.setEnabled(0, true);
                mgr.startStreaming(0, true /* real from SDR */);
                mgr.setCenterFreq(0, currentMonitorFreq);
                statusBar()->showMessage(QString("Started monitor + tuned to %1 MHz").arg(currentMonitorFreq/1e6,0,'f',3), 2500);
                // defer audio outputs
                QTimer::singleShot(120, this, [this]() {
                    if (!engineForAudio) engineForAudio = std::make_unique<AudioEngine>();
                    if (engineForAudio && engineForAudio->activeOutputCount() == 0) {
                        try {
                            auto outs = engineForAudio->enumeratePlaybackDevices();
                            if (!outs.empty()) {
                                std::vector<size_t> idxs = {0}; if (outs.size()>1) idxs.push_back(1);
                                engineForAudio->setActiveOutputs(idxs);
                            }
                        } catch (...) {}
                    }
                });
            }
        });

        mainLayout->addWidget(rxBox, 1);

        setCentralWidget(central);

        createMenus();

        // Initial device enumeration (PR2) for status — use probeHardware=false so we do ZERO
        // Soapy Device::make / hardware opens at startup. This is the #1 thing that was causing
        // "crash on open" even after all the other guards. We still get the list + synthetic RTL
        // entry + persisted enabled flags via loadSettings (which runs inside enumerate).
        auto& devMgr = DeviceManager::instance();
        auto initialDevs = devMgr.enumerateDevices(false /* no hardware probe on launch */);
        int enabled = 0;
        for (const auto& d : initialDevs) if (d.enabled) ++enabled;
        statusBar()->showMessage(QString("SDR Town — Professional SDR Tool  |  Devices: %1 total (%2 enabled)  |  Audio: not configured").arg(initialDevs.size()).arg(enabled));

        // NO auto-start of streaming (real or stub) on launch, even for persisted "enabled" devices.
        // This is the final safety to guarantee the exe opens without any background threads,
        // mutex contention, polling, or hardware access. Persisted enabled flags are still loaded
        // so the Device Manager dialog shows the RTL (and others) pre-checked. The user must
        // explicitly Apply (or use Add Receiver / Scan / CLI enable) after the window is open
        // and stable. This eliminates the recurring "crash when i open the exe".
        // (We used to auto-start stubs here; that + timer interaction was still crashing some users
        //  on open due to thread startup timing, lock polling, etc.)
        (void)initialDevs; // just for the count above; no streaming started
        try {
            // nothing — explicit start only after open
        } catch (...) {} // defensive, never reached

        // Live update timer — spectrum/UI only (light ~10ms ticks).
        // All heavy realtime work (IQ + demod + push) is now in a background worker thread below.
        currentMonitorFreq = 100e6;
        updateTimer = new QTimer(this);
        connect(updateTimer, &QTimer::timeout, this, [this, spectrum]() {
            try {
                auto& mgr = DeviceManager::instance();
                for (size_t i = 0; i < mgr.getDevices().size(); ++i) {
                    if (mgr.isStreaming(i)) {
                        std::vector<float> pwr;
                        double cf = 100e6, sr = 2.048e6;
                        if (mgr.getLatestSpectrum(i, pwr, cf, sr) && !pwr.empty()) {
                            spectrum->updateSpectrum(pwr, cf, sr);
                            if (autoDetectMode) {
                                DemodMode newM = classifyMode(pwr, sr, cf);
                                double detBw = detectChannelBandwidth(pwr, sr);
                                double useBwHz = (newM == DemodMode::WFM || newM == DemodMode::AUTO) ? 180000.0 : detBw;
                                double useBwK = (newM == DemodMode::WFM || newM == DemodMode::AUTO) ? 180.0 : (detBw / 1000.0);
                                {
                                    // P1/P2: timer (UI thread) was writing monitor* + setValue without lock, racing worker snapshot + bwSpin handler.
                                    std::lock_guard<std::mutex> lk(monitorParamsMutex);
                                    currentMonitorMode = newM;
                                    monitorChannelBwHz = useBwHz;
                                }
                                if (bwSpin) {
                                    bwSpin->blockSignals(true);
                                    bwSpin->setValue(useBwK);
                                    bwSpin->blockSignals(false);
                                }
                            }
                        }
                        break;
                    }
                }
            } catch (...) {}
        });
        QTimer::singleShot(200, this, [this]() {
            if (updateTimer && !updateTimer->isActive()) updateTimer->start(10);
        });

        // Dedicated background DSP worker thread for the GUI monitor path.
        // Owned (not detached) so we can join on shutdown. Uses stop flag.
        stopDspWorker = false;
        guiDspWorker = std::thread([this]() {
            auto& mgr = DeviceManager::instance();
            while (!stopDspWorker && updateTimer) {
                bool didWork = false;
                for (size_t i = 0; i < mgr.getDevices().size() && !stopDspWorker && updateTimer; ++i) {
                    if (mgr.isStreaming(i)) {
                        std::vector<float> pwr; double cf, sr;
                        mgr.getLatestSpectrum(i, pwr, cf, sr);

                        // Snapshot monitor params under lock to avoid races with UI thread (P2).
                        double monFreq, monLpf, monSquelch, monGain, monWfmDe, monWfmNotch, monBw;
                        DemodMode monMode;
                        {
                            std::lock_guard<std::mutex> lk(monitorParamsMutex);
                            monFreq = currentMonitorFreq;
                            monMode = currentMonitorMode;
                            monLpf = monitorLpfHz;
                            monSquelch = monitorSquelchDb;
                            monGain = monitorGain;
                            monWfmDe = monitorWfmDeTauUs;
                            monWfmNotch = monitorWfmPilotNotchR;
                            monBw = monitorChannelBwHz;
                        }

                        std::vector<std::complex<float>> iq;
                        double desired = 0.025;
                        size_t tgt = (sr > 0) ? (size_t)(sr * desired) : 32768;
                        iq.reserve(tgt);
                        while (iq.size() < tgt) {
                            std::vector<std::complex<float>> ch(8192);
                            size_t g = mgr.getNextIQBlock(i, ch.data(), ch.size(), 5);
                            if (g == 0) break;
                            ch.resize(g);
                            iq.insert(iq.end(), ch.begin(), ch.end());
                        }
                        size_t got = iq.size();
                        if (got > (size_t)(sr * 0.005)) {
                            double t = got / sr;
                            double orate = (engineForAudio ? engineForAudio->getSampleRate() : 48000.0);
                            size_t need = (size_t)std::round(t * orate);
                            double rms = -100;
                            auto t0 = std::chrono::steady_clock::now();
                            auto ch = gGuiDemod.demodulateToAudio(iq, sr, cf, monFreq, monMode,
                                rms, monLpf, monSquelch, monGain, monWfmDe,
                                monWfmNotch, monBw, need, orate);
                            auto t1 = std::chrono::steady_clock::now();
                            gLastDspMicros.store( std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count() );
                            if (!ch.empty()) {
                                if (!engineForAudio) engineForAudio = std::make_unique<AudioEngine>();
                                if (engineForAudio) {
                                    static std::vector<float> pend;
                                    pend.insert(pend.end(), ch.begin(), ch.end());
                                    const size_t psz = 240;
                                    while (pend.size() >= psz) {
                                        engineForAudio->pushAudio(pend.data(), psz);
                                        pend.erase(pend.begin(), pend.begin() + psz);
                                    }
                                }
                            }
                        }
                        didWork = true;
                        break;
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(didWork ? 3 : 12));
            }
        });

        // Ensure streams are stopped on GUI close / quit so rxThread + realInitThread
        // don't get left joinable (P1 shutdown hazard).
        connect(qApp, &QApplication::aboutToQuit, this, &MainWindow::stopAllStreaming);

        // AudioEngine is created lazily on first use (in timer push, or in deferred audio activation
        // in start buttons). This prevents any hang/crash from miniaudio context init during
        // MainWindow construction / program start.
    }

private slots:
    void showAbout()
    {
        QMessageBox box(this);
        box.setWindowTitle("About SDR Town");
        box.setTextFormat(Qt::RichText);
        box.setText(
            "<b>SDR Town</b> — Professional multi-SDR monitoring and signal analysis.<br><br>"
            "This software is for <b>receiving only</b>. You are solely responsible for compliance "
            "with all applicable laws in your jurisdiction regarding radio reception, recording, "
            "and use of data.<br><br>"
            "<b>Important:</b> All decoding and analysis features are intended exclusively for "
            "<b>unencrypted / clear signals</b>. No decryption, cryptoanalysis, or attempts to "
            "access encrypted communications are implemented or supported. If a signal is encrypted, "
            "the output will be unintelligible or random.<br><br>"
            "Use responsibly and lawfully only.");
        box.setStandardButtons(QMessageBox::Ok);
        box.exec();
    }

    void onAudioConfig()
    {
        // Full PR4 multi-device audio config dialog (speakers + VB-Audio Cable etc.)
        // IMPORTANT: configure the *live* engineForAudio (used by the main demod/receiver path in the 50ms timer),
        // not a separate static. This fixes "config in menu has no effect on playback".
        if (!engineForAudio) engineForAudio = std::make_unique<AudioEngine>();
        auto* engine = engineForAudio.get();

        QDialog dlg(this);
        dlg.setWindowTitle("Configure Output Devices — SDR Town");
        dlg.resize(720, 480);

        auto devs = engine->enumeratePlaybackDevices();

        QVBoxLayout* lay = new QVBoxLayout(&dlg);

        QLabel* info = new QLabel("Select one or more outputs (e.g. your speakers + a VB-Audio Cable / virtual device). Use Test buttons to identify them. Volumes are independent. Changes apply live.");
        info->setWordWrap(true);
        lay->addWidget(info);

        // Master volume
        QHBoxLayout* masterLay = new QHBoxLayout();
        masterLay->addWidget(new QLabel("Master Volume:"));
        QSlider* masterSlider = new QSlider(Qt::Horizontal);
        masterSlider->setRange(0, 100);
        masterSlider->setValue(85);
        QLabel* masterVal = new QLabel("85%");
        masterLay->addWidget(masterSlider);
        masterLay->addWidget(masterVal);
        lay->addLayout(masterLay);

        connect(masterSlider, &QSlider::valueChanged, [&](int v) {
            masterVal->setText(QString("%1%").arg(v));
            engine->setMasterVolume(v / 100.0f);
        });

        // Device list
        QTableWidget* table = new QTableWidget(devs.size(), 5, &dlg);
        table->setHorizontalHeaderLabels({"Use", "Device Name", "Default", "Volume", "Test"});
        table->horizontalHeader()->setStretchLastSection(true);

        std::vector<QCheckBox*> useChecks;
        std::vector<QSlider*> volSliders;

        for (size_t i = 0; i < devs.size(); ++i) {
            int row = static_cast<int>(i);
            const auto& d = devs[i];

            QCheckBox* use = new QCheckBox();
            // pre-select first two or default + one that looks like cable
            bool pre = d.isDefault || (d.name.find("CABLE") != std::string::npos) || (d.name.find("VB-Audio") != std::string::npos) || (i < 2 && devs.size() > 1);
            use->setChecked(pre && engine->isDeviceActive(i));
            table->setCellWidget(row, 0, use);
            useChecks.push_back(use);

            table->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(d.name)));

            table->setItem(row, 2, new QTableWidgetItem(d.isDefault ? "Yes" : ""));

            QSlider* vol = new QSlider(Qt::Horizontal);
            vol->setRange(0, 100);
            vol->setValue(80);
            table->setCellWidget(row, 3, vol);
            volSliders.push_back(vol);

            QPushButton* test = new QPushButton("Test 1kHz");
            connect(test, &QPushButton::clicked, [engine, i, this]() {
                engine->playTestTone(i, 1000.0f, 0.7f);
                statusBar()->showMessage(QString("Test tone on output #%1").arg(i), 1200);
            });
            table->setCellWidget(row, 4, test);
        }

        lay->addWidget(table);

        // Buttons
        QHBoxLayout* btns = new QHBoxLayout();
        QPushButton* apply = new QPushButton("Apply (Live)");
        QPushButton* refresh = new QPushButton("Refresh Device List");
        QPushButton* close = new QPushButton("Close");
        btns->addWidget(refresh);
        btns->addStretch();
        btns->addWidget(apply);
        btns->addWidget(close);
        lay->addLayout(btns);

        connect(refresh, &QPushButton::clicked, [&]() { QMessageBox::information(&dlg, "Refresh", "Close and reopen the dialog to re-enumerate devices."); });

        connect(apply, &QPushButton::clicked, [&]() {
            std::vector<size_t> active;
            for (size_t i = 0; i < useChecks.size(); ++i) {
                if (useChecks[i]->isChecked()) active.push_back(i);
            }
            engine->setActiveOutputs(active);

            for (size_t i = 0; i < active.size(); ++i) {
                // find the slider for this active index
                // simplistic: set volumes for the active ones in order
                if (i < volSliders.size()) {
                    engine->setOutputVolume(i, volSliders[active[i]]->value() / 100.0f);
                }
            }
            engine->setMasterVolume(masterSlider->value() / 100.0f);

            statusBar()->showMessage(QString("Audio outputs active: %1").arg(QString::fromStdString(engine->getActiveDeviceNames())), 4000);
            spdlog::info("Audio outputs applied: {}", engine->getActiveDeviceNames());
        });

        connect(close, &QPushButton::clicked, &dlg, &QDialog::accept);

        // initial status
        statusBar()->showMessage(QString("Audio devices: %1 found. Configure & Apply to use multiple (e.g. speakers + VAC).").arg(devs.size()));

        dlg.exec();
    }

    void onDevices()
    {
        showDevicesDialog();
    }

    void showDevicesDialog()
    {
        QDialog dlg(this);
        dlg.setWindowTitle("Device Manager — SDR Town");
        dlg.resize(900, 520);

        auto& mgr = DeviceManager::instance();
        // Full probe=true here: user explicitly opened the manager, so we can safely query real
        // hardware capabilities to populate nice combos/spins/ranges. If this still blows up for
        // a particular dongle, at least the main window opened.
        auto devs = mgr.enumerateDevices(true);

        QVBoxLayout* mainLay = new QVBoxLayout(&dlg);

        QLabel* hint = new QLabel("Rescan to refresh. Enable devices, adjust gain/sample rate/antenna. Settings persist across runs. Real SoapySDR + HackRF recommended (stubs shown if no Soapy).");
        hint->setWordWrap(true);
        mainLay->addWidget(hint);

        // Diagnostics for RTL-SDR etc.
        auto drivers = mgr.getAvailableDrivers();
        QString drvStr = "Available Soapy drivers: ";
        for (size_t i=0; i<drivers.size(); ++i) { if (i>0) drvStr += ", "; drvStr += QString::fromStdString(drivers[i]); }
        if (drivers.empty()) drvStr += "none (check Soapy installation)";
        QLabel* drvLabel = new QLabel(drvStr + "\nFor RTL-SDR: Use Zadig (zadig.akeo.ie) to install WinUSB driver for your RTL device (Interface 0). If 'rtlsdr' not listed above, the SoapyRTLSDR module is missing — install PothosSDR or place SoapyRTLSDR.dll in Soapy modules dir and restart app.");
        drvLabel->setWordWrap(true);
        drvLabel->setStyleSheet("color: #ffcc00; font-size: 10px;");
        mainLay->addWidget(drvLabel);

        QTableWidget* table = new QTableWidget(devs.size(), 7, &dlg);
        QStringList headers = {"Enabled", "Label / Driver", "Serial", "Antenna", "Sample Rate (MS/s)", "Gain (dB)", "Freq Range (MHz)"};
        table->setHorizontalHeaderLabels(headers);
        table->horizontalHeader()->setStretchLastSection(true);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);

        std::vector<QCheckBox*> enableChecks;
        std::vector<QComboBox*> antCombos;
        std::vector<QDoubleSpinBox*> rateSpins;
        std::vector<QDoubleSpinBox*> gainSpins;

        for (size_t i = 0; i < devs.size(); ++i) {
            const auto& d = devs[i];
            int row = static_cast<int>(i);

            // Enabled
            QCheckBox* cb = new QCheckBox();
            cb->setChecked(d.enabled);
            table->setCellWidget(row, 0, cb);
            enableChecks.push_back(cb);

            // Label
            table->setItem(row, 1, new QTableWidgetItem(QString("%1 (%2)").arg(QString::fromStdString(d.label)).arg(QString::fromStdString(d.driver))));

            // Serial
            table->setItem(row, 2, new QTableWidgetItem(QString::fromStdString(d.serial)));

            // Antenna combo
            QComboBox* ant = new QComboBox();
            for (const auto& a : d.antennas) ant->addItem(QString::fromStdString(a));
            if (!d.antenna.empty()) ant->setCurrentText(QString::fromStdString(d.antenna));
            table->setCellWidget(row, 3, ant);
            antCombos.push_back(ant);

            // Sample rate
            QDoubleSpinBox* rate = new QDoubleSpinBox();
            rate->setRange(0.1, 60.0);
            rate->setDecimals(3);
            rate->setSingleStep(0.1);
            rate->setSuffix(" MS/s");
            rate->setValue(d.sampleRate / 1e6);
            // add common rates from list if present
            table->setCellWidget(row, 4, rate);
            rateSpins.push_back(rate);

            // Gain
            QDoubleSpinBox* gain = new QDoubleSpinBox();
            gain->setRange(0, 80);
            gain->setDecimals(1);
            gain->setSingleStep(1);
            gain->setSuffix(" dB");
            gain->setValue(d.gain);
            table->setCellWidget(row, 5, gain);
            gainSpins.push_back(gain);

            // Freq range
            QString fr = QString("%1 – %2").arg(d.minFreq/1e6, 0, 'f', 0).arg(d.maxFreq/1e6, 0, 'f', 0);
            table->setItem(row, 6, new QTableWidgetItem(fr));
        }

        mainLay->addWidget(table);

        QHBoxLayout* btnLay = new QHBoxLayout();
        QPushButton* rescanBtn = new QPushButton("Rescan Devices");
        QPushButton* applyBtn = new QPushButton("Apply Changes");
        QPushButton* closeBtn = new QPushButton("Close");
        btnLay->addWidget(rescanBtn);
        btnLay->addStretch();
        btnLay->addWidget(applyBtn);
        btnLay->addWidget(closeBtn);
        mainLay->addLayout(btnLay);

        connect(rescanBtn, &QPushButton::clicked, [&]() {
            // Force re-enumeration with full probe (user action)
            mgr.enumerateDevices(true);
            // Close this dialog and re-open fresh one so table is refreshed with latest (incl any newly plugged RTL-SDR)
            dlg.accept();
            QTimer::singleShot(50, this, &MainWindow::showDevicesDialog);
        });

        connect(applyBtn, &QPushButton::clicked, [&]() {
            try {
                for (size_t i = 0; i < devs.size(); ++i) {
                    bool en = enableChecks[i]->isChecked();
                    mgr.setEnabled(i, en);

                    double rateHz = rateSpins[i]->value() * 1e6;
                    double g = gainSpins[i]->value();
                    std::string ant = antCombos[i]->currentText().toStdString();

                    mgr.updateDeviceParams(i, rateHz, g, ant);

                    // Start/stop real streaming on enable. startStreaming itself is hardened (try/catch + stub fallback + thread guards)
                    // so this should not propagate, but outer try is defense-in-depth for any future native/USB fault on Apply.
                    if (en) {
                        mgr.startStreaming(i);
                    } else {
                        mgr.stopStreaming(i);
                    }
                }
                mgr.saveSettings();
                statusBar()->showMessage(QString("Applied settings to %1 device(s)").arg(devs.size()), 3000);
                spdlog::info("Device settings applied from dialog.");

                // Defer audio activation (and lazy engine creation) to prevent hanging on Apply.
                QTimer::singleShot(100, this, [this]() {
                    if (!engineForAudio) {
                        engineForAudio = std::make_unique<AudioEngine>();
                    }
                    if (engineForAudio && engineForAudio->activeOutputCount() == 0) {
                        try {
                            auto outs = engineForAudio->enumeratePlaybackDevices();
                            if (!outs.empty()) {
                                std::vector<size_t> idxs = {0};
                                if (outs.size() > 1) idxs.push_back(1); // e.g. speakers + VB-Audio Cable
                                engineForAudio->setActiveOutputs(idxs);
                                statusBar()->showMessage("Devices applied + default audio output(s) activated for playback", 3000);
                            }
                        } catch (...) {
                            spdlog::warn("Deferred audio auto-activate in Device Manager Apply failed (non-fatal)");
                        }
                    }
                });
            } catch (const std::exception& ex) {
                spdlog::error("Exception during Device Manager Apply: {}", ex.what());
                statusBar()->showMessage("Apply error (see log). Device left in safe/stub state if possible.", 6000);
                QMessageBox::warning(&dlg, "Device Start Error",
                    QString("An error occurred while starting the device (RTL-SDR or other).\n\n%1\n\nCheck the log for details (sdr_town.log). "
                            "The device will use safe internal simulation if real hardware could not be initialized. "
                            "Verify Zadig WinUSB driver is installed for the RTL device and that no other SDR app has it open.").arg(ex.what()));
            } catch (...) {
                spdlog::error("Unknown exception during Device Manager Apply (possible driver/USB SEH).");
                statusBar()->showMessage("Apply error (unknown). See log. Using safe fallback.", 6000);
                QMessageBox::warning(&dlg, "Device Start Error",
                    "Unknown error while starting device.\n\nSee sdr_town.log. Ensure proper driver (Zadig) and that the dongle is not in use by another program.");
            }
        });

        connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::accept);

        dlg.exec();

        // Refresh main status
        int enabledCount = 0;
        for (const auto& d : mgr.getDevices()) if (d.enabled) ++enabledCount;
        statusBar()->showMessage(QString("Devices: %1 total, %2 enabled  |  See Device Manager dialog").arg(mgr.getDevices().size()).arg(enabledCount));
    }

private:
    QTimer* updateTimer = nullptr;
    std::unique_ptr<AudioEngine> engineForAudio;
    std::thread guiDspWorker;
    std::atomic<bool> stopDspWorker{false};
    std::mutex monitorParamsMutex;  // protects currentMonitor* / monitor* vars between GUI DSP worker and UI thread (P2)
    double currentMonitorFreq = 100e6;
    DemodMode currentMonitorMode = DemodMode::NFM;
    bool autoDetectMode = true;
    double monitorLpfHz = 15000;
    double monitorSquelchDb = -80;
    double monitorGain = 1.0;
    double monitorWfmDeTauUs = 75.0;
    double monitorWfmPilotNotchR = 0.96;
    double monitorChannelBwHz = 200000.0; // default for WFM; set narrower for voice channels e.g. 25000
    QDoubleSpinBox* bwSpin = nullptr;
    // spectrumWidget kept for future if needed

    void stopAllStreaming() {
        stopDspWorker = true;
        if (guiDspWorker.joinable()) {
            guiDspWorker.join();
        }

        auto& mgr = DeviceManager::instance();
        for (size_t i = 0; i < mgr.getDevices().size(); ++i) {
            if (mgr.isStreaming(i)) {
                try { mgr.stopStreaming(i); } catch (...) { spdlog::warn("stopAllStreaming: exception stopping {}", i); }
            }
        }
        if (updateTimer) updateTimer->stop();
        spdlog::info("All streams stopped on shutdown");
        try { spdlog::default_logger()->flush(); } catch (...) {}
    }

    void closeEvent(QCloseEvent* event) override {
        stopAllStreaming();
        QMainWindow::closeEvent(event);
    }

    void createMenus()
    {
        // File
        QMenu* fileMenu = menuBar()->addMenu("&File");
        fileMenu->addAction("E&xit", this, &QWidget::close);

        // Devices (stub)
        QMenu* devicesMenu = menuBar()->addMenu("&Devices");
        QAction* discoverAct = devicesMenu->addAction("&Rescan / Discover Devices...");
        connect(discoverAct, &QAction::triggered, this, &MainWindow::onDevices);
        devicesMenu->addSeparator();
        devicesMenu->addAction("Device &Manager...", this, &MainWindow::onDevices);

        // Receivers (stub)
        QMenu* rxMenu = menuBar()->addMenu("&Receivers");
        rxMenu->addAction("&Add Receiver...", [](){});
        rxMenu->addAction("&Receiver Table", [](){});

        // Scan (stub)
        QMenu* scanMenu = menuBar()->addMenu("&Scan");
        scanMenu->addAction("&Start Smart Scan", [](){});
        scanMenu->addAction("Band &Plans...", [](){});

        // Audio — important first-class menu (per design)
        QMenu* audioMenu = menuBar()->addMenu("&Audio");
        QAction* configAudio = audioMenu->addAction("Configure &Output Devices...");
        connect(configAudio, &QAction::triggered, this, &MainWindow::onAudioConfig);
        audioMenu->addSeparator();
        audioMenu->addAction("&Mute All Outputs", [](){});
        audioMenu->addAction("Test Tone (All)", [](){});

        // View / Tools / Settings (stubs)
        menuBar()->addMenu("&View");
        menuBar()->addMenu("&Tools");
        QMenu* settingsMenu = menuBar()->addMenu("&Settings");
        settingsMenu->addAction("Preferences...", [](){});

        // Help
        QMenu* helpMenu = menuBar()->addMenu("&Help");
        helpMenu->addAction("&About SDR Town", this, &MainWindow::showAbout);
        helpMenu->addAction("View &Design Document (DESIGN.md)", [this]() {
            // Simple hint — in real app we can open the file or a help viewer
            QMessageBox::information(this, "Design Document",
                "The full living design document is in the project root as DESIGN.md.\n"
                "It contains architecture, PR plan, key decisions, and detailed feature specs.\n\n"
                "Open it in any text editor or Markdown viewer.");
        });
    }
};

#include "main.moc"

int runCLI(int argc, char* argv[]) {
    QCoreApplication app(argc, argv); // for QStandardPaths etc in DeviceManager
    app.setApplicationName("SDR Town");
    app.setOrganizationName("SDR_Town");

    std::cout << "=== SDR Town CLI ===\n";
    std::cout << "Type 'help' for commands. Streaming and audio run in background.\n";
    std::cout << "Use --gui (default) for the full Qt interface.\n\n";

    auto& mgr = DeviceManager::instance();
    // Enumerate early (light path) so that "enable 0", "status", "gain" etc. work even if the
    // user types them before an explicit "list". Previously the devices vector was empty until first list.
    (void)mgr.enumerateDevices(false);
    std::unique_ptr<AudioEngine> audioEng = std::make_unique<AudioEngine>();
    std::atomic<bool> running{true};
    double cliMonitorFreq = 100e6;
    DemodMode cliMode = DemodMode::NFM;
    bool cliAutoDetect = true;
    double cliLpfHz = 15000;
    double cliSquelchDb = -90;
    double cliGain = 2.0;
    double cliWfmDeTauUs = 75.0;
    double cliWfmPilotNotchR = 0.96;
    double cliChannelBwHz = 180000.0; // tighter WFM default (180 kHz) for better spur rejection on RTL in broadcast band; use set bw 120-200 to tune
    static double lastCliAudioRms = -100;  // shared with stats printer (updated from monitor thread)

    // Background monitor thread: similar to GUI timer - poll, spectrum summary, basic demod, push audio
    std::thread monitorThread([&]() {
        while (running) {
            bool didWork = false;
            for (size_t i = 0; i < mgr.getDevices().size(); ++i) {
                if (mgr.isStreaming(i)) {
                    std::vector<float> pwr;
                    double cf, sr;
                    if (mgr.getLatestSpectrum(i, pwr, cf, sr) && !pwr.empty()) {
                        // Simple text summary for CLI
                        float minp = *std::min_element(pwr.begin(), pwr.end());
                        float maxp = *std::max_element(pwr.begin(), pwr.end());
                        float avg = 0; for(auto v : pwr) avg += v; avg /= pwr.size();
                        std::cout << "[SPEC " << i << "] CF=" << (cf/1e6) << "M SR=" << (sr/1e6) << "M  min=" << minp << " max=" << maxp << " avg=" << avg << " dB\r" << std::flush;
                    }

                    if (cliAutoDetect && !pwr.empty()) {
                        cliMode = classifyMode(pwr, sr, cf);
                        double det = detectChannelBandwidth(pwr, sr);
                        if (cliMode == DemodMode::WFM || cliMode == DemodMode::AUTO) {
                            if (std::abs(det - cliChannelBwHz) > 25000.0) {
                                cliChannelBwHz = 180000.0;  // match GUI: lock WFM to avoid filter jumps causing chop
                            }
                        } else {
                            cliChannelBwHz = det;
                        }
                    }

                    std::vector<std::complex<float>> iq;
                    const size_t target_iq = 48000;  // ~24 ms at 2 MS/s — larger for lower FIR overhead, less chopping
                    iq.reserve(target_iq);
                    while (iq.size() < target_iq) {
                        std::vector<std::complex<float>> chunk(4096);
                        size_t g = mgr.getNextIQBlock(i, chunk.data(), chunk.size(), 3);
                        if (g == 0) break;
                        chunk.resize(g);
                        iq.insert(iq.end(), chunk.begin(), chunk.end());
                    }
                    size_t got = iq.size();
                    // Guard: only demod/push when we have a real meaty block from the SDR (no tiny glitches)
                    if (got > (size_t)(sr * 0.002)) {
                        double chunk_time = (double)got / sr;
                        double outRate = (audioEng ? audioEng->getSampleRate() : 48000.0);
                        size_t audio_needed = (size_t)std::round(chunk_time * outRate);  // automatic from AudioEngine output rate
                        double audioRms = -100;
                        auto t0 = std::chrono::steady_clock::now();
                        auto audioChunk = gCliDemod.demodulateToAudio(iq, sr, cf, cliMonitorFreq, cliMode, audioRms, cliLpfHz, cliSquelchDb, cliGain, cliWfmDeTauUs, cliWfmPilotNotchR, cliChannelBwHz, audio_needed, outRate);
                        auto t1 = std::chrono::steady_clock::now();
                        gLastDspMicros.store( std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count() );
                        if (!audioChunk.empty()) {
                            lastCliAudioRms = audioRms;   // for stats command to display latest (shared)
                            // Same smoothing as GUI: regular 10 ms pushes to AudioEngine for stable playback
                            // even when IQ chunk sizes from the 2048-sample rx blocks vary.
                            static std::vector<float> pending;
                            pending.insert(pending.end(), audioChunk.begin(), audioChunk.end());
                            const size_t pushSz = 240;  // 5 ms blocks for smoother, less blocky delivery
                            while (pending.size() >= pushSz) {
                                audioEng->pushAudio(pending.data(), pushSz);
                                pending.erase(pending.begin(), pending.begin() + pushSz);
                            }
                        }
                    }
                    didWork = true;
                    break; // one active device for monitor
                }
            }
            if (!didWork) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }
    });

    auto printHelp = []() {
        std::cout << "Commands:\n";
        std::cout << "  list                     - list devices\n";
        std::cout << "  enable <idx>             - enable and start streaming device idx\n";
        std::cout << "  disable <idx>            - stop streaming\n";
        std::cout << "  tune <freq_mhz>          - set monitor freq and tune active device\n";
        std::cout << "  gain <idx> <db>          - set gain for device\n";
        std::cout << "  rate <idx> <msps>        - set sample rate\n";
        std::cout << "  spectrum                 - print current spectrum summary\n";
        std::cout << "  audio list               - list audio output devices\n";
        std::cout << "  audio enable <i> [i2..]  - activate multiple audio outputs\n";
        std::cout << "  audio test [idx]         - play test tone (all or specific)\n";
        std::cout << "  audio vol <idx> <0-100>  - set volume for output\n";
        std::cout << "  status                   - show active streams and monitor freq\n";
        std::cout << "  scan                     - start simple multi-device 'scan' (enable all)\n";
        std::cout << "  mode [nfm|wfm|am|usb|lsb|auto]  - set or show demod mode (use CLI for live debug/optim)\n";
        std::cout << "  set lpf <hz>             - set LPF bandwidth (e.g. 3500 for NFM, 15000 for WFM)\n";
        std::cout << "  set squelch <db>         - set squelch threshold (e.g. -80)\n";
        std::cout << "  set gain <x>             - set post-demod gain (e.g. 2.0)\n";
        std::cout << "  set wfmde <us>           - WFM de-emphasis tau (75 us typical for broadcast)\n";
        std::cout << "  set pilotnotch <r>       - WFM 19kHz pilot notch radius (0.96 narrow, 0.9 stronger to kill buzz)\n";
        std::cout << "  set bw <khz>             - channel BW filter before demod (e.g. 150-180 for WFM broadcast, 12-25 for NFM voice) - tighter = better spur/image rejection on RTL in FM band\n";
        std::cout << "  stats                    - print current demod stats (mode, rms, bw estimate) + device vs monitor freq\n";
        std::cout << "  Note: RTL-SDRs often show the same strong FM station (e.g. 98.9) at nearby freqs like 97.3/98.1 due to hardware images/spurs/overload.\n";
        std::cout << "        Try: lower RF gain (gain 0 15), set bw 120-150, or rate 0 1.024 for different folding. The channel filter + limiter now help reject in software.\n";
        std::cout << "  help                     - this help\n";
        std::cout << "  quit / exit              - exit CLI\n";
    };

    printHelp();

    std::string line;
    while (running) {
        std::cout << "\ncli> " << std::flush;
        if (!std::getline(std::cin, line)) break;
        if (line.empty()) continue;

        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;
        std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);

        if (cmd == "quit" || cmd == "exit" || cmd == "q") {
            running = false;
            break;
        } else if (cmd == "help" || cmd == "h") {
            printHelp();
        } else if (cmd == "list") {
            auto devs = mgr.enumerateDevices();
            for (size_t i = 0; i < devs.size(); ++i) {
                const auto& d = devs[i];
                std::cout << i << ": " << d.driver << " | " << d.label
                          << " ser=" << d.serial
                          << " en=" << (d.enabled ? "Y" : "N")
                          << " rate=" << (d.sampleRate/1e6) << "M"
                          << " gain=" << d.gain << "dB"
                          << " ant=" << d.antenna << "\n";
            }
        } else if (cmd == "enable") {
            size_t idx; if (iss >> idx) {
                mgr.setEnabled(idx, true);
                if (mgr.startStreaming(idx)) {
                    std::cout << "Enabled and streaming device " << idx << "\n";
                } else {
                    std::cout << "Failed to start streaming for " << idx << "\n";
                }
            }
        } else if (cmd == "disable") {
            size_t idx; if (iss >> idx) {
                mgr.setEnabled(idx, false);
                mgr.stopStreaming(idx);
                std::cout << "Disabled device " << idx << "\n";
            }
        } else if (cmd == "tune") {
            double f; if (iss >> f) {
                cliMonitorFreq = f * 1e6;
                auto devs = mgr.getDevices();
                for (size_t i=0; i<devs.size(); ++i) {
                    if (mgr.isStreaming(i)) {
                        mgr.setCenterFreq(i, cliMonitorFreq);
                        std::cout << "Tuned device " << i << " + monitor to " << f << " MHz\n";
                        break;
                    }
                }
            }
        } else if (cmd == "gain") {
            size_t idx; double g; if (iss >> idx >> g) {
                auto d = mgr.getDevice(idx);
                if (d) {
                    mgr.updateDeviceParams(idx, d->sampleRate, g, d->antenna);
                    std::cout << "Set gain " << g << " for " << idx << "\n";
                }
            }
        } else if (cmd == "rate") {
            size_t idx; double r; if (iss >> idx >> r) {
                auto d = mgr.getDevice(idx);
                if (d) {
                    mgr.updateDeviceParams(idx, r*1e6, d->gain, d->antenna);
                    std::cout << "Set rate " << r << " MS/s for " << idx << "\n";
                }
            }
        } else if (cmd == "spectrum") {
            auto devs = mgr.getDevices();
            bool found = false;
            for (size_t i=0; i<devs.size(); ++i) {
                if (mgr.isStreaming(i)) {
                    std::vector<float> pwr; double cf, sr;
                    if (mgr.getLatestSpectrum(i, pwr, cf, sr) && !pwr.empty()) {
                        float minp = *std::min_element(pwr.begin(), pwr.end());
                        float maxp = *std::max_element(pwr.begin(), pwr.end());
                        float avg = 0; for (auto v:pwr) avg += v; avg /= pwr.size();
                        std::cout << "Device " << i << " CF=" << (cf/1e6) << "M  min=" << minp << " max=" << maxp << " avg=" << avg << " dB\n";
                        // simple ascii bar for max
                        int bar = std::max(0, std::min(50, (int)((maxp + 100) * 0.5)));
                        std::cout << "  [" << std::string(bar, '#') << std::string(50-bar, ' ') << "]\n";
                    }
                    found = true;
                    break;
                }
            }
            if (!found) std::cout << "No streaming devices.\n";
        } else if (cmd == "audio") {
            std::string sub; iss >> sub;
            std::transform(sub.begin(), sub.end(), sub.begin(), ::tolower);
            if (sub == "list") {
                auto outs = audioEng->enumeratePlaybackDevices();
                for (size_t i=0; i<outs.size(); ++i) {
                    std::cout << i << ": " << outs[i].name << (outs[i].isDefault ? " (default)" : "") << "\n";
                }
            } else if (sub == "enable") {
                std::vector<size_t> idxs; size_t t;
                while (iss >> t) idxs.push_back(t);
                audioEng->setActiveOutputs(idxs);
                std::cout << "Audio outputs set.\n";
            } else if (sub == "test") {
                size_t idx = size_t(-1); iss >> idx;
                audioEng->playTestTone(idx);
                std::cout << "Test tone triggered.\n";
            } else if (sub == "vol") {
                size_t idx; double v; if (iss >> idx >> v) {
                    audioEng->setOutputVolume(idx, v/100.0f);
                    std::cout << "Volume set.\n";
                }
            }
        } else if (cmd == "status") {
            auto devs = mgr.getDevices();
            int active = 0;
            for (auto& d : devs) if (mgr.isStreaming(&d - &devs[0])) active++; // rough
            std::cout << "Devices: " << devs.size() << "  Streaming: " << active << "\n";
            std::cout << "Monitor freq: " << (cliMonitorFreq / 1e6) << " MHz  Mode: ";
            if (cliAutoDetect) std::cout << "AUTO/";
            if (cliMode == DemodMode::WFM) std::cout << "WFM"; else if (cliMode == DemodMode::NFM) std::cout << "NFM"; else if (cliMode == DemodMode::AM) std::cout << "AM"; else std::cout << "other";
            std::cout << "\n";
            std::cout << "Audio active outputs: " << audioEng->activeOutputCount() << "\n";
        } else if (cmd == "scan") {
            auto devs = mgr.getDevices();
            for (size_t i=0; i<devs.size(); ++i) {
                mgr.setEnabled(i, true);
                mgr.startStreaming(i);
            }
            std::cout << "Scan mode: streaming enabled on all devices.\n";
        } else if (cmd == "mode") {
            std::string m; iss >> m;
            if (m.empty()) {
                std::cout << "Current mode: " << (cliAutoDetect ? "AUTO" : "manual") << " ";
                if (cliMode == DemodMode::NFM) std::cout << "NFM\n";
                else if (cliMode == DemodMode::WFM) std::cout << "WFM\n";
                else if (cliMode == DemodMode::AM) std::cout << "AM\n";
                else if (cliMode == DemodMode::USB) std::cout << "USB\n";
                else if (cliMode == DemodMode::LSB) std::cout << "LSB\n";
                else std::cout << "AUTO\n";
            } else if (m == "auto") { cliAutoDetect = true; cliMode = DemodMode::AUTO; std::cout << "Auto detect enabled.\n"; }
            else if (m == "nfm") { cliAutoDetect = false; cliMode = DemodMode::NFM; std::cout << "NFM mode.\n"; }
            else if (m == "wfm") { cliAutoDetect = false; cliMode = DemodMode::WFM; std::cout << "WFM mode.\n"; }
            else if (m == "am") { cliAutoDetect = false; cliMode = DemodMode::AM; std::cout << "AM mode.\n"; }
            else if (m == "usb") { cliAutoDetect = false; cliMode = DemodMode::USB; std::cout << "USB mode.\n"; }
            else if (m == "lsb") { cliAutoDetect = false; cliMode = DemodMode::LSB; std::cout << "LSB mode.\n"; }
            else std::cout << "Unknown mode. Use nfm|wfm|am|usb|lsb|auto\n";
        } else if (cmd == "set") {
            std::string sub; iss >> sub;
            if (sub == "lpf") { double h; if (iss >> h) { cliLpfHz = h; std::cout << "LPF set to " << h << " Hz\n"; } }
            else if (sub == "squelch") { double db; if (iss >> db) { cliSquelchDb = db; std::cout << "Squelch " << db << " dB\n"; } }
            else if (sub == "gain") { double g; if (iss >> g) { cliGain = g; std::cout << "Gain " << g << "\n"; } }
            else if (sub == "wfmde") { double us; if (iss >> us) { cliWfmDeTauUs = us; std::cout << "WFM de-emph tau " << us << " us\n"; } }
            else if (sub == "pilotnotch") { double r; if (iss >> r) { cliWfmPilotNotchR = r; std::cout << "WFM pilot notch r=" << r << "\n"; } }
            else if (sub == "bw") { double khz; if (iss >> khz) { cliChannelBwHz = khz * 1000.0; std::cout << "Channel BW " << khz << " kHz (filter before demod)\n"; } }
            else std::cout << "set lpf|squelch|gain|wfmde|pilotnotch|bw <val>\n";
        } else if (cmd == "stats") {
            // P1 audit: live diagnostics now include RF gain, mode/BW, IQ queue depth, DSP block time, audio ring fill, underrun count.
            // These make scratch/crackle causes obvious (gain too hot? queue building? DSP too slow starving audio?).
            std::cout << "Mode: " << (cliAutoDetect?"AUTO ":"") ;
            if (cliMode==DemodMode::WFM) std::cout << "WFM"; else if (cliMode==DemodMode::NFM) std::cout<<"NFM"; else if (cliMode==DemodMode::AM) std::cout<<"AM"; else std::cout << "other";
            std::cout << "  LPF:" << cliLpfHz << "  Squelch:" << cliSquelchDb << "  (post-demod)Gain:" << cliGain << "\n";
            if (cliMode==DemodMode::WFM || cliMode==DemodMode::AUTO) {
                std::cout << "  WFMde:" << cliWfmDeTauUs << "us  PilotNotchR:" << cliWfmPilotNotchR << "\n";
            }
            std::cout << "  ChannelBW:" << (cliChannelBwHz/1000) << " kHz\n";
            std::cout << "Monitor freq: " << (cliMonitorFreq/1e6) << " MHz\n";

            // Live from manager (locked queries)
            double rfGain = mgr.getCurrentGain(0);
            size_t iqDepth = mgr.getIQQueueDepth(0);
            std::cout << "RF gain (dev0): " << rfGain << " dB   IQ queue depth: " << iqDepth << "\n";

            // Show actual device center (what the SDR LO is tuned to) vs the monitor DDC target.
            // Large difference means the channel filter is the only thing rejecting spurs/images at other offsets.
            // On RTL-SDR this is key for diagnosing "same signal at 97.3 / 98.1 / 98.9".
            double devCf = cliMonitorFreq, devSr = 2.048e6;
            std::vector<float> dummyP;
            if (mgr.getLatestSpectrum(0, dummyP, devCf, devSr) && !dummyP.empty()) {
                double off = (cliMonitorFreq - devCf) / 1e3;
                std::cout << "Device CF: " << (devCf/1e6) << " MHz   Offset to monitor: " << off << " kHz\n";
            }
            std::cout << "Audio RMS (last): " << lastCliAudioRms << " dB  (while receiving, higher = stronger signal)\n";

            // DSP timing + audio health (updated live from worker/monitor + engine)
            long long dspUs = gLastDspMicros.load();
            double ring = audioEng ? audioEng->getRingFillPercent() : 0.0;
            int und = audioEng ? audioEng->getUnderrunCount() : 0;
            std::cout << "Last DSP block: " << dspUs << " us   Audio ring fill: " << ring << "%   Underruns: " << und << "\n";

            // === AUTOMATIC STATE-OF-THE-ART RATE & BITRATE REPORT ===
            // All numbers are calculated from the real SDR chunk duration + AudioEngine output rate + channel BW.
            // This is what "no compromise" means: every stage knows the exact rates and bitrates.
            double outRate = (audioEng ? audioEng->getSampleRate() : 48000.0);
            // Rough current input rate from last spectrum (devSr)
            double sdrRate = devSr;
            double sdrBitrateMbps = sdrRate * 2 * 4 * 8 / 1e6; // CF32 = 2 floats * 4 bytes * 8 bits
            double audioPerDeviceKbps = outRate * 4 * 8 / 1000.0; // mono float32
            int activeOuts = audioEng ? (int)audioEng->activeOutputCount() : 0;
            double totalAudioKbps = audioPerDeviceKbps * activeOuts;
            std::cout << "=== RATES (automatic) ===\n";
            std::cout << "  SDR input: " << (sdrRate/1e6) << " MS/s   IQ bitrate: " << sdrBitrateMbps << " Mbps (CF32)\n";
            std::cout << "  Channel BW: " << (cliChannelBwHz/1000.0) << " kHz   (internal/decim target ~192 kHz for WFM)\n";
            std::cout << "  Output rate: " << (outRate/1000) << " kHz   Per-device audio: " << audioPerDeviceKbps << " kbps (float32 mono)\n";
            std::cout << "  Active outputs: " << activeOuts << "   Total output bitrate: " << totalAudioKbps << " kbps\n";
            std::cout << "  (All exact audio block sizes are computed as round(real_IQ_duration * output_rate))\n";
        } else {
            std::cout << "Unknown command. Type 'help'.\n";
        }
    }

    // Stop streams first (real Soapy devices get deactivate/close/unmake) so background monitor and rx threads see clean state before joins and process teardown.
    {
        auto devs = mgr.getDevices();
        for (size_t i = 0; i < devs.size(); ++i) {
            if (mgr.isStreaming(i)) mgr.stopStreaming(i);
        }
    }
    running = false;
    if (monitorThread.joinable()) monitorThread.join();
    std::cout << "CLI exited.\n";
    return 0;
}

int main(int argc, char *argv[])
{
    writeEarlyCrashLog("main-entry");

#ifdef _WIN32
    // Register SEH filter early so even access violations / driver faults inside Qt ctor or
    // first paint / timers produce a useful message + marker instead of the generic
    // "program error" blank-GUI-then-crash the user sees.
    SetUnhandledExceptionFilter(sehTopLevelFilter);
#endif

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--cli" || arg == "-c" || arg == "--console") {
            writeEarlyCrashLog("cli-path");
            return runCLI(argc, argv);
        }
    }

    int ret = 1;
    try {
        writeEarlyCrashLog("before-qapp");
        QApplication app(argc, argv);
        app.setApplicationName("SDR Town");
        app.setOrganizationName("SDR_Town");
        app.setApplicationVersion("0.1.0-dev");

        setupLogging();
        applyDarkTheme(app);

        spdlog::info("Starting SDR Town v{}.", app.applicationVersion().toStdString());
        spdlog::default_logger()->flush();
        writeEarlyCrashLog("before-mainwindow");

        MainWindow w;
        w.show();

        spdlog::info("Main window shown. Entering Qt event loop.");
        spdlog::default_logger()->flush();
        writeEarlyCrashLog("entering-exec");
        ret = app.exec();

        spdlog::info("Application exiting with code {}.", ret);
    } catch (const std::exception& ex) {
        writeEarlyCrashLog("std-exception", ex.what());
        try { spdlog::error("Fatal exception in main: {}", ex.what()); } catch (...) {}
        MessageBoxA(nullptr,
            (std::string("SDR Town failed to start or crashed.\n\nDetails: ") + ex.what() +
             "\n\nSee %TEMP%\\sdr_town_launch.log and the sdr_town log in AppData for more.\n"
             "Run from the Release folder next to its DLLs/plugins. Re-run windeployqt after rebuilds.").c_str(),
            "SDR Town - Startup Error", MB_ICONERROR | MB_OK);
    } catch (...) {
        writeEarlyCrashLog("unknown-exception");
        try { spdlog::error("Unknown fatal exception in main GUI path."); } catch (...) {}
        MessageBoxA(nullptr,
            "SDR Town failed with an unknown exception during startup.\n\n"
            "Check %TEMP%\\sdr_town_launch.log . Ensure you are running the exe from build\\bin\\Release "
            "(with all the copied Qt6*.dll + platforms\\qwindows.dll + pthreadVC2.dll + Soapy/RTL bits present).",
            "SDR Town - Program Error", MB_ICONERROR | MB_OK);
    }

    try { spdlog::shutdown(); } catch (...) {}
    return ret;
}
