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
#include <QSettings>   // for updater skipped version + last check persistence (best practice)
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
#include <QDesktopServices>
#include <QUrl>
#include <complex>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <nlohmann/json.hpp>

#include "DeviceManager.h"
#include "SpectrumWidget.h"
#include "AudioEngine.h"
#include "Demod.h"
#include "Receiver.h"  // Phase 0: per-receiver foundation
#include "UpdateManager.h"

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
#include <cctype>

#ifdef _WIN32
#include <windows.h>
#endif

using json = nlohmann::json;

#ifndef SDR_TOWN_VERSION
#define SDR_TOWN_VERSION "0.0.0"
#endif

int runCLI(int argc, char* argv[]);

// Shared live diagnostic updated by demod calls from GUI worker and CLI monitor thread (P1 audit diags)
static std::atomic<long long> gLastDspMicros{0};
static std::atomic<double> gLastRmsDb{-100.0};   // live channel level used by squelch calibration/Auto/indicator

// DSP implementation now lives in src/Demod.cpp (Demodulator class owns all state per Receiver).
// classifyMode, detectChannelBandwidth, and demodulateToAudio are provided via Demod.h + Demod.cpp.
// Phase 0: no more global gGuiDemod/gCliDemod - each Receiver owns its Demodulator instance (state isolation).

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
            syncMonitorVarsToReceiver(0);
            setReceiverActive(0, true);
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
                    if (auto* eng = getOrCreateAudioEngine()) {
                        if (eng->activeOutputCount() == 0) {
                            try {
                                auto outs = eng->enumeratePlaybackDevices();
                                if (!outs.empty()) {
                                    std::vector<size_t> idxs{0};
                                    if (outs.size() > 1) idxs.push_back(1);
                                    eng->setActiveOutputs(idxs);
                                }
                            } catch (...) {}
                        }
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

        // S0-7 (P2): removed hardcoded APT/DMR/NFM demo rows (was claiming live receiver table).
        // The vector<Receiver> + snapshot in DSP is the real foundation; full live QTable + per-rx persistence
        // (receivers.json) + rich editor comes in the receiver management work after stabilization.
        QGroupBox* rxBox = new QGroupBox("Active Receivers (Phase 0 vector foundation — live table + persistence next)");
        rxBox->setStyleSheet("QGroupBox { font-size: 11px; }");
        QVBoxLayout* rxLay = new QVBoxLayout(rxBox);

        QTableWidget* rxTable = new QTableWidget(0, 6, this);
        rxTable->setHorizontalHeaderLabels({"Freq (MHz)", "Mode", "Squelch", "Level", "Monitor", "Record"});
        rxTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        rxTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        rxTable->horizontalHeader()->setStretchLastSection(true);
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
            if (!mgr.getDevices().empty()) {
                mgr.setEnabled(0, true);
                try { mgr.startStreaming(0); } catch (...) { spdlog::warn("startStreaming(0) fault in Add Receiver (guarded)"); }

                // S0-2 (P0): protect vector mutation under mutex (GUI thread). Worker will snapshot.
                // Use shared_ptr so reallocation never invalidates live Demodulator instances.
                {
                    std::lock_guard<std::mutex> lk(receiversMutex);
                    ensureReceiver();
                    auto newRx = std::make_shared<Receiver>();
                    newRx->deviceIndex = 0;
                    newRx->freqHz = currentMonitorFreq;
                    newRx->mode = currentMonitorMode;
                    newRx->channelBwHz = monitorChannelBwHz;
                    newRx->lpfHz = monitorLpfHz;
                    newRx->squelchDb = monitorSquelchDb;
                    newRx->rfGainDb = monitorRfGainDb;
                    newRx->audioGain = monitorGain;
                    newRx->gain = monitorGain;
                    newRx->wfmDeTauUs = monitorWfmDeTauUs;
                    newRx->wfmPilotNotchR = monitorWfmPilotNotchR;
                    newRx->active = true;
                    receivers.push_back(std::move(newRx));
                }

                statusBar()->showMessage(QString("Added receiver #%1 (dev 0, %.3f MHz, %2) - streaming + audio")
                    .arg(receivers.size()).arg(currentMonitorFreq/1e6, 0, 'f', 3), 3000);

                // Defer audio activation (lazy engine) - same pattern
                QTimer::singleShot(100, this, [this]() {
                    AudioEngine* eng = getOrCreateAudioEngine();
                    if (eng && eng->activeOutputCount() == 0) {
                        try {
                            auto outs = eng->enumeratePlaybackDevices();
                            if (!outs.empty()) {
                                std::vector<size_t> idxs = {0};
                                if (outs.size() > 1) idxs.push_back(1);
                                eng->setActiveOutputs(idxs);
                            }
                        } catch (...) {
                            spdlog::warn("Deferred audio auto-activate in Add Receiver failed (non-fatal)");
                        }
                    }
                });
            }
        });
        connect(removeRxBtn, &QPushButton::clicked, this, [this]() {
            // S0-7: actually remove a receiver from the live vector (under lock for snapshot safety).
            // Stop a stream if present (existing behavior). Full per-rx stop + rich UI later.
            {
                std::lock_guard<std::mutex> lk(receiversMutex);
                if (!receivers.empty()) {
                    receivers.pop_back();
                }
            }
            auto& mgr = DeviceManager::instance();
            for (size_t i = 0; i < mgr.getDevices().size(); ++i) {
                if (mgr.isStreaming(i)) { mgr.stopStreaming(i); break; }
            }
            statusBar()->showMessage("Removed receiver + stopped a stream", 2000);
        });
        connect(scanBtn, &QPushButton::clicked, this, [this]() {
            auto& mgr = DeviceManager::instance();
            // S0-7 (P2): "Smart scan" button currently enables + starts *real* streaming on all devices
            // (direct from SDR, no simulation). The old comment claimed "safe stub" — now honest.
            // Full energy-based smart scanner + hits table + promote-to-receiver is post-stabilization work.
            for (size_t i = 0; i < mgr.getDevices().size(); ++i) {
                mgr.setEnabled(i, true);
                try { mgr.startStreaming(i, true /* real SDR - no simulation, direct from hardware */); } catch (...) { spdlog::warn("startStreaming fault in scan (guarded)"); }
            }
            statusBar()->showMessage("Scan: real streaming started on all devices (full smart scanner + hits table later)", 4000);
            spdlog::info("Scan: real streaming started on all devices");
            // Defer audio activation (and lazy engine creation) – see Add Receiver.
            QTimer::singleShot(100, this, [this]() {
                AudioEngine* eng = getOrCreateAudioEngine();
                if (eng && eng->activeOutputCount() == 0) {
                    try {
                        auto outs = eng->enumeratePlaybackDevices();
                        if (!outs.empty()) {
                            std::vector<size_t> idxs = {0};
                            if (outs.size() > 1) idxs.push_back(1);
                            eng->setActiveOutputs(idxs);
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

        // New UI for sensitivity (RF gain / noise floor), squelch, and heat map color range (for waterfall/spectrum).
        // These directly address user request for adjusting SDR sensitivity, squelch, and good heat map / noise floor visualization.
        QHBoxLayout* gainLay = new QHBoxLayout();
        gainLay->addWidget(new QLabel("RF Gain (dB):"));
        QDoubleSpinBox* gainSpin = new QDoubleSpinBox();
        gainSpin->setRange(0, 50); gainSpin->setDecimals(1); gainSpin->setValue(20);
        gainSpin->setToolTip("Manual SDR RF gain / sensitivity. 0 = minimum gain; higher values increase sensitivity and overload risk. This writes directly to the SDR hardware when a real device is active.");
        gainLay->addWidget(gainSpin);
        gainLay->addSpacing(12);
        gainLay->addWidget(new QLabel("Squelch (dB):"));
        QDoubleSpinBox* squelchSpin = new QDoubleSpinBox();
        squelchSpin->setRange(-130, 40); squelchSpin->setDecimals(0); squelchSpin->setValue(-80);
        squelchSpin->setToolTip("Channel-level squelch threshold (dB). Put SQ above the current Level marker to mute; set below it to open. Values below -115 disable squelch.");
        gainLay->addWidget(squelchSpin);

        QPushButton* autoSquelchBtn = new QPushButton("Auto");
        autoSquelchBtn->setToolTip("Set squelch from the current channel noise floor (Level + 6 dB).");
        gainLay->addWidget(autoSquelchBtn);

        QLabel* rmsLabel = new QLabel("Level: --- dB");
        rmsLabel->setToolTip("Live channel level used by the squelch gate, updated from the DSP worker.");
        gainLay->addWidget(rmsLabel);
        rxLay->addLayout(gainLay);

        // Live channel-level readout (polled lightly from the main UI timer)
        QTimer* rmsUpdate = new QTimer(this);
        connect(rmsUpdate, &QTimer::timeout, this, [rmsLabel, spectrum]() {
            double r = gLastRmsDb.load();
            if (r > -150) rmsLabel->setText(QString("Level: %1 dB").arg(r, 0, 'f', 1));
            if (spectrum) spectrum->setLiveRms(r);   // update the reference marker on the spectrum plot
        });
        rmsUpdate->start(400);

        connect(autoSquelchBtn, &QPushButton::clicked, this, [this, squelchSpin]() {
            double recent = gLastRmsDb.load();
            // Set squelch threshold a bit above current channel level/noise floor.
            double target = (recent > -140.0) ? (recent + 6.0) : -82.0;
            target = std::clamp(target, -130.0, 40.0);
            squelchSpin->setValue(target);
        });

        QHBoxLayout* colorLay = new QHBoxLayout();
        colorLay->addWidget(new QLabel("WF Color Min (dB):"));
        QDoubleSpinBox* colorMinSpin = new QDoubleSpinBox();
        colorMinSpin->setRange(-150, -20); colorMinSpin->setValue(-120);
        colorMinSpin->setToolTip("Lower end of waterfall/spectrum heat map (noise floor). Lower values make weak signals visible.");
        colorLay->addWidget(colorMinSpin);
        colorLay->addWidget(new QLabel("Max:"));
        QDoubleSpinBox* colorMaxSpin = new QDoubleSpinBox();
        colorMaxSpin->setRange(-60, 20); colorMaxSpin->setValue(-10);
        colorMaxSpin->setToolTip("Upper end of heat map. Adjust to make strong signals 'hot' red.");
        colorLay->addWidget(colorMaxSpin);
        rxLay->addLayout(colorLay);

        // Wire the new controls to per-receiver state and backend (gain goes to device, others to demod/display).
        connect(gainSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v) {
            std::lock_guard<std::mutex> lk(monitorParamsMutex);
            monitorRfGainDb = v;
            syncMonitorVarsToReceiver(0);
            auto& mgr = DeviceManager::instance();
            if (!mgr.getDevices().empty()) {
                // Use the new live path — this actually calls setGain on a running device when possible.
                mgr.setLiveGain(0, v);
            }
        });

        connect(squelchSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, spectrum](double v) {
            std::lock_guard<std::mutex> lk(monitorParamsMutex);
            monitorSquelchDb = v;
            syncMonitorVarsToReceiver(0);
            // Propagate main-GUI squelch change to *all* active receivers so the main GUI squelch control
            // actually affects the audio the user is hearing (transitional multi-rx; per-rx squelch in table later).
            {
                std::lock_guard<std::mutex> lk2(receiversMutex);
                for (auto& r : receivers) {
                    if (r && r->active) {
                        std::lock_guard<std::mutex> rxLock(r->stateMutex);
                        r->squelchDb = v;
                        r->resetSquelchGate();  // ensure raise of threshold bypasses hang immediately (fixes "not live until freq click")
                    }
                }
            }
            if (spectrum) spectrum->setSquelchThreshold(v);  // keep the interactive line in sync (bidirectional)
        });

        // Interactive squelch line/bar on the spectrum widget (right side + horizontal threshold line).
        // Dragging it updates the main squelch spin + all active receivers live (visual "cut" aid linked to the real gate).
        connect(spectrum, &SpectrumWidget::squelchThresholdChanged, this, [this, squelchSpin](double v) {
            {
                std::lock_guard<std::mutex> lk(monitorParamsMutex);
                monitorSquelchDb = v;
            }
            // Update spin without causing a re-entrant valueChanged (prevents feedback loop).
            squelchSpin->blockSignals(true);
            squelchSpin->setValue(v);
            squelchSpin->blockSignals(false);

            syncMonitorVarsToReceiver(0);
            {
                std::lock_guard<std::mutex> lk2(receiversMutex);
                for (auto& r : receivers) {
                    if (r && r->active) {
                        std::lock_guard<std::mutex> rxLock(r->stateMutex);
                        r->squelchDb = v;
                        r->resetSquelchGate();
                    }
                }
            }
        });

        connect(colorMinSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, colorMinSpin, colorMaxSpin, spectrum](double) {
            spectrum->setColorRange(colorMinSpin->value(), colorMaxSpin->value());
        });
        connect(colorMaxSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, colorMinSpin, colorMaxSpin, spectrum](double) {
            spectrum->setColorRange(colorMinSpin->value(), colorMaxSpin->value());
        });

        // Initialize from current state
        gainSpin->setValue(monitorRfGainDb);
        squelchSpin->setValue(monitorSquelchDb);
        spectrum->setColorRange(colorMinSpin->value(), colorMaxSpin->value());
        spectrum->setSquelchThreshold(monitorSquelchDb);  // initial position of the interactive sq line + dB scale context

        connect(bwSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v){
            std::lock_guard<std::mutex> lk(monitorParamsMutex);
            monitorChannelBwHz = v * 1000.0;
            syncMonitorVarsToReceiver(0);
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
            syncMonitorVarsToReceiver(0);
            if (bwSpin) {
                bwSpin->blockSignals(true);
                bwSpin->setValue(newBwK);
                bwSpin->blockSignals(false);
            }
        });

        connect(setMonBtn, &QPushButton::clicked, this, [this, monFreq]() {
            currentMonitorFreq = monFreq->value() * 1e6;
            syncMonitorVarsToReceiver(0);
            setReceiverActive(0, true);
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
                    AudioEngine* eng = getOrCreateAudioEngine();
                    if (eng && eng->activeOutputCount() == 0) {
                        try {
                            auto outs = eng->enumeratePlaybackDevices();
                            if (!outs.empty()) {
                                std::vector<size_t> idxs = {0}; if (outs.size()>1) idxs.push_back(1);
                                eng->setActiveOutputs(idxs);
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
        if (!initialDevs.empty()) {
            const auto& d0 = initialDevs.front();
            const double gMin = d0.gainMax > d0.gainMin ? d0.gainMin : 0.0;
            const double gMax = d0.gainMax > d0.gainMin ? d0.gainMax : 80.0;
            gainSpin->blockSignals(true);
            gainSpin->setRange(gMin, gMax);
            gainSpin->setValue(std::clamp(d0.gain, gMin, gMax));
            gainSpin->setToolTip(QString("Manual SDR RF gain / sensitivity for %1. Range: %2 to %3 dB. 0/min = least sensitive; high values can overload strong local signals.")
                .arg(QString::fromStdString(d0.label))
                .arg(gMin, 0, 'f', 1)
                .arg(gMax, 0, 'f', 1));
            gainSpin->blockSignals(false);
            monitorRfGainDb = gainSpin->value();
        }
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
                                syncMonitorVarsToReceiver(0);
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
                // S0-2 (P0 audit): short lock to snapshot the current receivers (shared_ptrs — cheap, stable).
                // Then process without holding lock. shared_ptr keeps the Demodulator alive even if vector reallocates.
                std::vector<std::shared_ptr<Receiver>> rxSnapshot;
                {
                    std::lock_guard<std::mutex> lk(receiversMutex);
                    ensureReceiver();
                    rxSnapshot.reserve(receivers.size());
                    for (auto& r : receivers) if (r && r->active) rxSnapshot.push_back(r);
                }
                for (size_t r = 0; r < rxSnapshot.size() && !stopDspWorker && updateTimer; ++r) {
                    auto& rxPtr = rxSnapshot[r];
                    if (!rxPtr) continue;
                    Receiver& rx = *rxPtr;  // reference to the stable object
                    size_t i = 0;

                    {
                        std::lock_guard<std::mutex> rxLock(rx.stateMutex);
                        if (!rx.active) continue;
                        i = rx.deviceIndex;
                    }
                    if (i >= mgr.getDevices().size() || !mgr.isStreaming(i)) continue;

                    std::vector<float> pwr; double cf = 0.0, sr = 0.0;
                    if (!mgr.getLatestSpectrum(i, pwr, cf, sr) || sr <= 0.0) continue;

                    std::vector<float> ch;
                    double rms = -100;
                    long long dspMicros = 0;
                    {
                        double monFreq = 100e6, monLpf = 15000.0, monSquelch = -80.0;
                        double monGain = 1.0, monWfmDe = 75.0, monWfmNotch = 0.96, monBw = 180000.0;
                        DemodMode monMode = DemodMode::AUTO;

                        std::lock_guard<std::mutex> rxLock(rx.stateMutex);
                        if (!rx.active || rx.deviceIndex != i) continue;
                        monFreq = rx.freqHz;
                        monMode = rx.mode;
                        monBw = rx.channelBwHz;
                        monLpf = rx.lpfHz;
                        monSquelch = rx.squelchDb;
                        monGain = rx.audioGain;
                        monWfmDe = rx.wfmDeTauUs;
                        monWfmNotch = rx.wfmPilotNotchR;

                        // audit-followup-2: use per-rx cursor consumption. Each rx pulls only its own *new* chronological samples.
                        // No more "demod the latest 25ms overlapping window" for every rx.
                        size_t tgt = (sr > 0) ? (size_t)(sr * 0.025) : 8192;
                        auto iq = mgr.getNewSamplesForReceiver(i, rx, tgt);  // updates the live rx's lastConsumedAbsolute
                        size_t got = iq.size();
                        if (got == 0) continue;

                        double t = got / sr;
                        double orate = (engineForAudio ? engineForAudio->getSampleRate() : 48000.0);
                        size_t need = (size_t)std::round(t * orate);
                        auto t0 = std::chrono::steady_clock::now();
                        ch = rx.demod.demodulateToAudio(iq, sr, cf, monFreq, monMode,
                            rms, monLpf, monSquelch, monGain, monWfmDe,
                            monWfmNotch, monBw, need, orate);
                        auto t1 = std::chrono::steady_clock::now();
                        dspMicros = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
                    }

                    gLastDspMicros.store(dspMicros);
                    gLastRmsDb.store(rms);   // for live channel-level readout and Auto Squelch
                    if (!ch.empty()) {
                        if (auto* eng = getOrCreateAudioEngine()) {
                            static std::vector<float> pend;
                            pend.insert(pend.end(), ch.begin(), ch.end());
                            const size_t psz = 240;
                            while (pend.size() >= psz) {
                                eng->pushAudio(pend.data(), psz);
                                pend.erase(pend.begin(), pend.begin() + psz);
                            }
                        }
                    }
                    didWork = true;
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

        // Phase 0: initialize per-receiver list
        ensureReceiver();
        syncMonitorVarsToReceiver(0);
        setReceiverActive(0, false);  // will be set true on first tune/start

        // === Professional in-app updater (state-of-the-art, consent-based, safe) ===
        m_updateManager = new UpdateManager(this);
        connect(m_updateManager, &UpdateManager::updateAvailable, this, [this](const UpdateInfo& info) {
            // Per spec dialog: version, release notes link, size, buttons Download and Install / Later / Skip this version.
            // No auto-install. User must explicitly choose.
            QString sizeStr = info.size > 0 ? QString("Size: %1 MB").arg(info.size / (1024.0*1024.0), 0, 'f', 1) : QString();
            QString msg = QString("SDR Town %1 is available.\n\n%2\n%3\n\n%4")
                              .arg(info.version)
                              .arg(sizeStr)
                              .arg(info.notesUrl.isEmpty() ? "" : "Release notes: " + info.notesUrl)
                              .arg("The installer will be downloaded, SHA256 verified, then launched. Your settings in %APPDATA%\\SDR_Town are preserved.");
            QMessageBox box(this);
            box.setWindowTitle("Update Available");
            box.setText(msg);
            QPushButton* viewNotes = info.notesUrl.isEmpty() ? nullptr : box.addButton("View Notes", QMessageBox::HelpRole);
            QPushButton* download = box.addButton("Download and Install", QMessageBox::AcceptRole);
            QPushButton* later = box.addButton("Later", QMessageBox::RejectRole);
            QPushButton* skip = box.addButton("Skip this version", QMessageBox::DestructiveRole);
            box.exec();
            if (box.clickedButton() == download) {
                m_updateManager->downloadAndApplyUpdate(info);
            } else if (box.clickedButton() == skip) {
                QSettings s;
                s.setValue("updates/skippedVersion", info.version);
            } else if (viewNotes && box.clickedButton() == viewNotes) {
                QDesktopServices::openUrl(QUrl(info.notesUrl));
            }
        });
        connect(m_updateManager, &UpdateManager::upToDate, this, [this]() {
            // Only shown for explicit manual "Check for Updates" (startup checks are silent).
            QMessageBox::information(this, "SDR Town", "You are up to date.");
        });
        connect(m_updateManager, &UpdateManager::error, this, [](const QString& msg) {
            qWarning() << "Updater error:" << msg;
        });

        // Rate-limited background check on startup (never auto-installs anything)
        QTimer::singleShot(7500, this, [this]() {
            if (m_updateManager) m_updateManager->checkForUpdates(false);
        });
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
        auto* engine = getOrCreateAudioEngine();
        if (!engine) return;

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
            double gainMin = d.gainMax > d.gainMin ? d.gainMin : 0.0;
            double gainMax = d.gainMax > d.gainMin ? d.gainMax : 80.0;
            gain->setRange(gainMin, gainMax);
            gain->setDecimals(1);
            gain->setSingleStep(1);
            gain->setSuffix(" dB");
            gain->setValue(std::clamp(d.gain, gainMin, gainMax));
            table->setCellWidget(row, 5, gain);
            gainSpins.push_back(gain);

            // Make per-device gain changes in the dialog live while the device is running.
            // Previously only took effect on "Apply" + restart for many users.
            connect(gain, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [i](double gval) {
                auto& mgr = DeviceManager::instance();
                mgr.setLiveGain(i, gval);
            });

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
                        mgr.setLiveGain(i, g);
                    } else {
                        mgr.stopStreaming(i);
                    }
                }
                mgr.saveSettings();
                statusBar()->showMessage(QString("Applied settings to %1 device(s)").arg(devs.size()), 3000);
                spdlog::info("Device settings applied from dialog.");

                // Defer audio activation (and lazy engine creation) to prevent hanging on Apply.
                QTimer::singleShot(100, this, [this]() {
                    AudioEngine* eng = getOrCreateAudioEngine();
                    if (eng && eng->activeOutputCount() == 0) {
                        try {
                            auto outs = eng->enumeratePlaybackDevices();
                            if (!outs.empty()) {
                                std::vector<size_t> idxs = {0};
                                if (outs.size() > 1) idxs.push_back(1); // e.g. speakers + VB-Audio Cable
                                eng->setActiveOutputs(idxs);
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

    UpdateManager* m_updateManager = nullptr;   // professional GitHub release + in-app updater (state-of-the-art, safe)
    std::atomic<bool> stopDspWorker{false};
    std::mutex monitorParamsMutex;  // protects currentMonitor* / monitor* vars between GUI DSP worker and UI thread (P2) -- transitional during per-receiver refactor

    // S0-2 (P0 audit): mutex + snapshot for receivers vector. GUI thread mutates (Add, Remove, tune etc).
    // DSP worker takes short snapshot then processes without holding lock or & across yields.
    // Same pattern applied to CLI cliReceivers.
    std::mutex receiversMutex;

    // S0-5 (P1): centralize AudioEngine creation. DSP worker and hot paths call this;
    // creation happens at most once (call_once). UI config also uses it.
    std::once_flag audioEngineInitFlag;
    AudioEngine* getOrCreateAudioEngine() {
        std::call_once(audioEngineInitFlag, [this]() {
            if (!engineForAudio) engineForAudio = std::make_unique<AudioEngine>();
        });
        return engineForAudio.get();
    }

    // Phase 0 / S0-2: per-receiver foundation using shared_ptr so that vector reallocation (Add) never
    // invalidates live Receiver/Demodulator instances held by DSP worker snapshots or other references.
    // This is the clean, SOTA way to solve the P0 cross-thread vector mutation race.
    std::vector<std::shared_ptr<Receiver>> receivers;

    // Transitional single-monitor state (will be replaced by receivers[i] fields)
    double currentMonitorFreq = 100e6;
    DemodMode currentMonitorMode = DemodMode::AUTO;
    bool autoDetectMode = true;
    double monitorLpfHz = 15000;
    double monitorSquelchDb = -80;
    double monitorRfGainDb = 20.0;
    double monitorGain = 1.0; // audio gain, not RF gain
    double monitorWfmDeTauUs = 75.0;
    double monitorWfmPilotNotchR = 0.96;
    double monitorChannelBwHz = 200000.0; // default for WFM; set narrower for voice channels e.g. 25000
    QDoubleSpinBox* bwSpin = nullptr;
    // spectrumWidget kept for future if needed

    // Helper to ensure at least one receiver exists (transitional Phase 0)
    void ensureReceiver() {
        if (receivers.empty()) {
            auto r = std::make_shared<Receiver>();
            r->deviceIndex = 0;
            r->freqHz = currentMonitorFreq;
            r->mode = currentMonitorMode;
            r->channelBwHz = monitorChannelBwHz;
            r->lpfHz = monitorLpfHz;
            r->squelchDb = monitorSquelchDb;
            r->rfGainDb = monitorRfGainDb;
            r->audioGain = monitorGain;
            r->gain = monitorGain;
            r->wfmDeTauUs = monitorWfmDeTauUs;
            r->wfmPilotNotchR = monitorWfmPilotNotchR;
            r->active = false;
            receivers.push_back(std::move(r));
        }
    }

    // Phase 0 sync: keep receivers[0] in sync with the live monitor* control vars (UI writes here)
    void syncMonitorVarsToReceiver(size_t idx = 0) {
        std::lock_guard<std::mutex> lk(receiversMutex);
        ensureReceiver();
        if (idx >= receivers.size()) return;
        auto& rx = *receivers[idx];  // deref the shared_ptr (S0-2 vector of shared_ptr)
        std::lock_guard<std::mutex> rxLock(rx.stateMutex);
        const bool rfOrModeChanged =
            std::abs(rx.freqHz - currentMonitorFreq) > 1.0 ||
            rx.mode != currentMonitorMode ||
            std::abs(rx.channelBwHz - monitorChannelBwHz) > 1.0;

        if (rfOrModeChanged) {
            rx.resetDemodState();
        }

        rx.freqHz = currentMonitorFreq;
        rx.mode = currentMonitorMode;
        rx.channelBwHz = monitorChannelBwHz;
        rx.lpfHz = monitorLpfHz;
        rx.squelchDb = monitorSquelchDb;
        rx.rfGainDb = monitorRfGainDb;
        rx.audioGain = monitorGain;
        rx.gain = monitorGain;
        rx.wfmDeTauUs = monitorWfmDeTauUs;
        rx.wfmPilotNotchR = monitorWfmPilotNotchR;
    }

    void setReceiverActive(size_t idx, bool active) {
        std::lock_guard<std::mutex> lk(receiversMutex);
        ensureReceiver();
        if (idx < receivers.size() && receivers[idx]) {
            auto& rx = *receivers[idx];
            std::lock_guard<std::mutex> rxLock(rx.stateMutex);
            if (active && !rx.active) {
                rx.resetDemodState();
            }
            rx.active = active;
        }
    }

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
        helpMenu->addAction("Check for &Updates...", [this]() {
            if (m_updateManager) {
                // Manual check — will show "up to date" or the update dialog
                m_updateManager->checkForUpdates(true);
            }
        });
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
    std::cout << "SDR Town CLI (Phase 0 complete - per-receiver foundation + full monitor thread)\n";
    std::cout << "Type 'help' for commands. 'quit' to exit.\n";

    // S0-1: App identity for CLI (and tests). Must create QCoreApplication *before* any
    // setupLogging() or DeviceManager that calls QStandardPaths::writableLocation(AppDataLocation).
    // This ensures logs go to %APPDATA%\SDR_Town\logs\sdr_town.log and devices.json/receivers.json
    // live under the proper organization/app subdir instead of root Roaming (P1 audit).
    int dummyArgc = argc > 0 ? argc : 1;
    char* dummyArgv0 = (char*)"sdr_town_cli";
    char** dummyArgv = (argc > 0 ? argv : &dummyArgv0);
    QCoreApplication cliApp(dummyArgc, dummyArgv);
    cliApp.setApplicationName("SDR Town");
    cliApp.setOrganizationName("SDR_Town");
    cliApp.setApplicationVersion(SDR_TOWN_VERSION);

    setupLogging();
    spdlog::info("CLI mode started");

    auto& mgr = DeviceManager::instance();
    mgr.setupSoapyForRTLSDR();
    auto devs = mgr.enumerateDevices(false);
    std::cout << "Devices enumerated: " << devs.size() << "\n";
    for (size_t i=0; i<devs.size(); ++i) {
        const auto& d = devs[i];
        std::cout << "  [" << i << "] " << d.driver << " " << d.label << " enabled=" << d.enabled << " gain=" << d.gain << "\n";
    }

    // Use shared_ptr<Receiver> for CLI too (consistent with GUI, enables stable cursor updates across snapshots, cheap to snapshot pointers).
    std::vector<std::shared_ptr<Receiver>> cliReceivers;
    std::mutex cliRxMutex;  // S0-2 (P0): protect CLI receiver vector mutations (command thread) vs mon thread iteration
    std::unique_ptr<AudioEngine> cliAudio;
    std::atomic<bool> cliStop{false};
    std::atomic<bool> cliAudioEnabled{false};

    // S0 / audit-followup-1: non-recursive mutex discipline.
    // ensureCliRxLocked assumes the caller already holds cliRxMutex.
    // ensureCliRx acquires the lock then calls the locked version.
    auto ensureCliRxLocked = [&](size_t idx = 0) {
        while (cliReceivers.size() <= idx) {
            auto rptr = std::make_shared<Receiver>();
            size_t r = cliReceivers.size();
            rptr->deviceIndex = r;
            rptr->freqHz = 100e6;
            rptr->mode = DemodMode::NFM;
            rptr->channelBwHz = 25000.0;
            rptr->lpfHz = 3500.0;
            rptr->squelchDb = -90.0;
            rptr->rfGainDb = 20.0;
            rptr->audioGain = 1.0;
            rptr->gain = 1.0;
            rptr->wfmDeTauUs = 75.0;
            rptr->wfmPilotNotchR = 0.96;
            rptr->active = false;
            cliReceivers.push_back(std::move(rptr));
        }
    };

    auto ensureCliRx = [&](size_t idx = 0) {
        std::lock_guard<std::mutex> lk(cliRxMutex);
        ensureCliRxLocked(idx);
    };

    // CLI DSP monitor thread (mirrors guiDspWorker but standalone, uses Receiver vector + own demod instances)
    std::thread cliMonThread([&]() {
        while (!cliStop) {
            bool did = false;
            // S0-2 (P0): snapshot under lock (shared_ptrs — cheap, stable objects for cursor + demod state).
            std::vector<std::shared_ptr<Receiver>> rxSnap;
            {
                std::lock_guard<std::mutex> lk(cliRxMutex);
                rxSnap.reserve(cliReceivers.size());
                for (auto& r : cliReceivers) if (r && r->active) rxSnap.push_back(r);
            }
            for (size_t r = 0; r < rxSnap.size() && !cliStop; ++r) {
                auto& rxPtr = rxSnap[r];
                if (!rxPtr) continue;
                Receiver& rx = *rxPtr;  // live object (cursor updates will stick)
                size_t di = 0;
                {
                    std::lock_guard<std::mutex> rxLock(rx.stateMutex);
                    if (!rx.active) continue;
                    di = rx.deviceIndex;
                }
                if (di >= mgr.getDevices().size() || !mgr.isStreaming(di)) continue;

                std::vector<float> pwr; double cf=0, sr=0;
                if (!mgr.getLatestSpectrum(di, pwr, cf, sr) || sr <= 0.0) continue;

                std::vector<float> ch;
                double rms = -100;
                long long dspMicros = 0;
                {
                    double rxFreq = 100e6, rxLpf = 3500.0, rxSquelch = -90.0;
                    double rxAudioGain = 1.0, rxWfmDe = 75.0, rxWfmNotch = 0.96, rxBw = 25000.0;
                    DemodMode rxMode = DemodMode::NFM;

                    std::lock_guard<std::mutex> rxLock(rx.stateMutex);
                    if (!rx.active || rx.deviceIndex != di) continue;
                    rxFreq = rx.freqHz;
                    rxMode = rx.mode;
                    rxBw = rx.channelBwHz;
                    rxLpf = rx.lpfHz;
                    rxSquelch = rx.squelchDb;
                    rxAudioGain = rx.audioGain;
                    rxWfmDe = rx.wfmDeTauUs;
                    rxWfmNotch = rx.wfmPilotNotchR;

                    // audit-followup-2: cursor-based new samples only for this rx (chronological, no overlap with other rxs on same dev).
                    size_t tgt = (sr > 0 ? (size_t)(sr * 0.025) : 8192);
                    auto iq = mgr.getNewSamplesForReceiver(di, rx, tgt);  // updates live rx cursor
                    size_t got = iq.size();
                    if (got == 0) continue;

                    // audit-followup-7 (P2): if the rx is in AUTO, let the CLI monitor classify using latest spectrum
                    // and pick a concrete mode (NFM/WFM/AM etc) before demod. GUI timer already does this.
                    if (rxMode == DemodMode::AUTO && !pwr.empty()) {
                        DemodMode classified = classifyMode(pwr, sr, cf);
                        if (classified != DemodMode::AUTO) {
                            rxMode = classified;
                            rx.mode = classified;
                            if (classified == DemodMode::WFM && rx.channelBwHz < 100000.0) {
                                rx.channelBwHz = 180000.0;
                                rx.lpfHz = 15000.0;
                                rxBw = rx.channelBwHz;
                                rxLpf = rx.lpfHz;
                            }
                        }
                    }
                    double t = got / sr;
                    double orate = (cliAudio ? cliAudio->getSampleRate() : 48000.0);
                    size_t need = (size_t)std::round(t * orate);
                    auto t0 = std::chrono::steady_clock::now();
                    ch = rx.demod.demodulateToAudio(iq, sr, cf, rxFreq, rxMode,
                        rms, rxLpf, rxSquelch, rxAudioGain, rxWfmDe,
                        rxWfmNotch, rxBw, need, orate);
                    auto t1 = std::chrono::steady_clock::now();
                    dspMicros = std::chrono::duration_cast<std::chrono::microseconds>(t1-t0).count();
                }

                gLastDspMicros.store(dspMicros);
                if (!ch.empty() && cliAudio && cliAudioEnabled) {
                    static std::vector<float> pend;
                    pend.insert(pend.end(), ch.begin(), ch.end());
                    const size_t psz = 240;
                    while (pend.size() >= psz) {
                        cliAudio->pushAudio(pend.data(), psz);
                        pend.erase(pend.begin(), pend.begin() + psz);
                    }
                }
                did = true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(did ? 4 : 15));
        }
    });

    auto printStats = [&](size_t r = 0) {
        size_t di = 0;
        double freqHz = 100e6;
        double bwHz = 25000.0;
        DemodMode mode = DemodMode::NFM;
        {
            std::lock_guard<std::mutex> lk(cliRxMutex);
            ensureCliRxLocked(r);
            Receiver& rx = *cliReceivers[r];
            std::lock_guard<std::mutex> rxLock(rx.stateMutex);
            di = rx.deviceIndex;
            freqHz = rx.freqHz;
            bwHz = rx.channelBwHz;
            mode = rx.mode;
        }
        double g = (di < mgr.getDevices().size() ? mgr.getCurrentGain(di) : 0.0);
        size_t qd = (di < mgr.getDevices().size() ? mgr.getIQQueueDepth(di) : 0);
        double dsp = gLastDspMicros.load() / 1000.0;
        double level = gLastRmsDb.load();
        double ring = 0, underr = 0;
        if (cliAudio) {
            ring = cliAudio->getRingFillPercent();
            underr = (double)cliAudio->getUnderrunCount();
        }
        std::cout << "RX" << r << " dev=" << di << " f=" << (freqHz/1e6) << "MHz mode=" << (int)mode
                  << " bw=" << (bwHz/1000) << "kHz gain=" << g << "dB IQdepth=" << qd
                  << " level=" << level << "dB DSP=" << dsp << "ms ring=" << ring << "% underruns=" << underr << "\n";
    };

    std::string line;
    try {
        while (true) {
            std::cout << "sdr> " << std::flush;
            if (!std::getline(std::cin, line)) break;
            std::istringstream iss(line);
            std::string cmd; iss >> cmd;
            if (cmd.empty()) continue;
            for (auto& c : cmd) c = (char)std::tolower(c);

            if (cmd == "quit" || cmd == "exit" || cmd == "q") {
                cliStop = true;
                break;
            } else if (cmd == "help" || cmd == "h" || cmd == "?") {
            std::cout << "Commands:\n"
                      << "  list | devices          - show enumerated devices\n"
                      << "  enable <i>              - enable + start real streaming on device i\n"
                      << "  disable <i>             - stop streaming on device i\n"
                      << "  tune <mhz> [rx]         - tune (e.g. tune 98.9 or tune 0 98.9)\n"
                      << "  mode <auto|wfm|nfm|am|usb|lsb> [rx]\n"
                      << "  set bw <khz> [rx]       - set channel BW (e.g. set bw 150)\n"
                      << "  gain <i> <db>           - set live device RF gain\n"
                      << "  squelch <db> [rx]       - set squelch\n"
                      << "  stats | status [rx]     - live diagnostics (gain/mode/BW/IQ/DSP/ring/underrun)\n"
                      << "  audio list              - list playback devices\n"
                      << "  audio enable <out0> <out1?>\n"
                      << "  audio disable           - stop audio outputs\n"
                      << "  rx add                  - add another receiver entry\n"
                      << "  quit / exit\n";
        } else if (cmd == "list" || cmd == "devices") {
            auto dlist = mgr.getDevices();
            for (size_t i=0; i<dlist.size(); ++i) {
                const auto& d = dlist[i];
                std::cout << "[" << i << "] " << d.driver << "/" << d.label << " en=" << d.enabled << " sr=" << d.sampleRate << " gain=" << d.gain << "\n";
            }
        } else if (cmd == "enable") {
            int i = 0; iss >> i;
            if (i >= 0 && (size_t)i < mgr.getDevices().size()) {
                mgr.setEnabled(i, true);
                bool ok = mgr.startStreaming(i, true /* real */);
                {
                    std::lock_guard<std::mutex> lk(cliRxMutex);
                    ensureCliRxLocked(0);
                    Receiver& rx = *cliReceivers[0];
                    std::lock_guard<std::mutex> rxLock(rx.stateMutex);
                    rx.deviceIndex = i;
                    if (!rx.active) rx.resetDemodState();
                    rx.active = true;
                }
                std::cout << "Enabled+streaming device " << i << " (real=" << (ok?"yes":"no") << ")\n";
            } else std::cout << "bad device index\n";
        } else if (cmd == "disable") {
            int i = 0; iss >> i;
            if (i >= 0 && (size_t)i < mgr.getDevices().size()) {
                mgr.stopStreaming(i);
                {
                    std::lock_guard<std::mutex> lk(cliRxMutex);
                    for (auto& rxPtr : cliReceivers) {
                        if (!rxPtr) continue;
                        std::lock_guard<std::mutex> rxLock(rxPtr->stateMutex);
                        if (rxPtr->deviceIndex == (size_t)i) rxPtr->active = false;
                    }
                }
                std::cout << "Stopped device " << i << "\n";
            }
        } else if (cmd == "tune") {
            double f = 0; int rxidx = 0;
            if (!(iss >> f)) {
                std::cout << "bad freq (e.g. tune 98.9 or tune 0 98.9)\n";
                continue;
            }
            if (iss >> rxidx) {} // optional
            size_t di = 0;
            {
                std::lock_guard<std::mutex> lk(cliRxMutex);
                ensureCliRxLocked(rxidx);
                Receiver& rx = *cliReceivers[rxidx];
                std::lock_guard<std::mutex> rxLock(rx.stateMutex);
                if (std::abs(rx.freqHz - (f * 1e6)) > 1.0 || !rx.active) {
                    rx.resetDemodState();
                }
                rx.freqHz = f * 1e6;
                rx.active = true;
                di = rx.deviceIndex;
            }
            if (mgr.isStreaming(di)) mgr.setCenterFreq(di, f * 1e6);
            std::cout << "Tuned RX" << rxidx << " to " << f << " MHz (dev " << di << ")\n";
        } else if (cmd == "mode") {
            std::string m; int rxidx=0; iss >> m;
            if (iss >> rxidx) {}
            {
                std::lock_guard<std::mutex> lk(cliRxMutex);
                ensureCliRxLocked(rxidx);
                auto& rx = *cliReceivers[rxidx];
                std::lock_guard<std::mutex> rxLock(rx.stateMutex);
                for (auto& c : m) c = (char)std::tolower((unsigned char)c);
                DemodMode newMode = rx.mode;
                double newBw = rx.channelBwHz;
                double newLpf = rx.lpfHz;
                if (m=="auto") { newMode=DemodMode::AUTO; newBw=180000.0; newLpf=15000.0; }
                else if (m=="wfm") { newMode=DemodMode::WFM; newBw=180000.0; newLpf=15000.0; }
                else if (m=="nfm") { newMode=DemodMode::NFM; newBw=25000.0; newLpf=3500.0; }
                else if (m=="am") { newMode=DemodMode::AM; newBw=10000.0; newLpf=5000.0; }
                else if (m=="usb") { newMode=DemodMode::USB; newBw=3000.0; newLpf=3000.0; }
                else if (m=="lsb") { newMode=DemodMode::LSB; newBw=3000.0; newLpf=3000.0; }
                if (newMode != rx.mode || std::abs(newBw - rx.channelBwHz) > 1.0) {
                    rx.resetDemodState();
                    rx.mode = newMode;
                    rx.channelBwHz = newBw;
                    rx.lpfHz = newLpf;
                }
            }
            std::cout << "RX" << rxidx << " mode -> " << m << "\n";
        } else if (cmd == "set" ) {
            std::string sub; iss >> sub; for(auto& c : sub) c = (char)std::tolower((unsigned char)c);
            if (sub == "bw") {
                double khz; int rxidx=0; iss >> khz; if(iss>>rxidx){}
                {
                    std::lock_guard<std::mutex> lk(cliRxMutex);
                    ensureCliRxLocked(rxidx);
                    Receiver& rx = *cliReceivers[rxidx];
                    std::lock_guard<std::mutex> rxLock(rx.stateMutex);
                    const double newBw = khz * 1000.0;
                    if (std::abs(rx.channelBwHz - newBw) > 1.0) {
                        rx.resetDemodState();
                        rx.channelBwHz = newBw;
                    }
                }
                std::cout << "RX" << rxidx << " BW -> " << khz << " kHz\n";
            } else {
                std::cout << "set what? bw\n";
            }
        } else if (cmd == "gain") {
            int di; double db; iss >> di >> db;
            if (di >= 0 && (size_t)di < mgr.getDevices().size()) {
                mgr.setLiveGain(di, db);
                {
                    std::lock_guard<std::mutex> lk(cliRxMutex);
                    ensureCliRxLocked(0);
                    Receiver& rx = *cliReceivers[0];
                    std::lock_guard<std::mutex> rxLock(rx.stateMutex);
                    if (rx.deviceIndex == (size_t)di) rx.rfGainDb = db;
                }
                std::cout << "Gain device " << di << " -> " << db << " dB\n";
            }
        } else if (cmd == "squelch") {
            double db; int rxidx=0;
            if (!(iss >> db)) { std::cout << "bad squelch value\n"; continue; }
            if (iss >> rxidx) {}
            {
                std::lock_guard<std::mutex> lk(cliRxMutex);
                ensureCliRxLocked(rxidx);
                Receiver& rx = *cliReceivers[rxidx];
                std::lock_guard<std::mutex> rxLock(rx.stateMutex);
                rx.squelchDb = db;
                rx.resetSquelchGate();  // live reaction in CLI too
            }
            std::cout << "RX" << rxidx << " squelch -> " << db << " dB\n";
        } else if (cmd == "stats" || cmd == "status") {
            int rxidx = 0; if (iss >> rxidx) {}
            printStats(rxidx);
        } else if (cmd == "audio") {
            std::string sub; iss >> sub; for(auto& c : sub) c = (char)std::tolower((unsigned char)c);
            if (sub == "list") {
                if (!cliAudio) cliAudio = std::make_unique<AudioEngine>();
                auto outs = cliAudio->enumeratePlaybackDevices();
                for (size_t i=0; i<outs.size(); ++i) {
                    std::cout << "  [" << i << "] " << outs[i].name << (outs[i].isDefault ? " (default)" : "") << "\n";
                }
            } else if (sub == "enable") {
                int a=-1, b=-1; iss >> a; iss >> b;
                if (!cliAudio) cliAudio = std::make_unique<AudioEngine>();
                std::vector<size_t> idxs;
                if (a >= 0) idxs.push_back(a);
                if (b >= 0) idxs.push_back(b);
                if (idxs.empty() && !cliAudio->enumeratePlaybackDevices().empty()) idxs = {0};
                cliAudio->setActiveOutputs(idxs);
                cliAudioEnabled = true;
                std::cout << "Audio outputs activated: " << idxs.size() << "\n";
            } else if (sub == "disable") {
                cliAudioEnabled = false;
                if (cliAudio) cliAudio->setActiveOutputs({});
                std::cout << "Audio outputs disabled for CLI.\n";
            } else if (sub == "test") {
                int which = -1; iss >> which;
                std::cout << "Test tone requested for output " << which << " (CLI path - tone handled by engine if supported in session; see GUI for live tone buttons).\n";
            }
        } else if (cmd == "rx") {
            std::string sub; iss >> sub;
            if (sub == "add") {
                std::lock_guard<std::mutex> lk(cliRxMutex);
                size_t newi = cliReceivers.size();
                ensureCliRxLocked(newi);
                cliReceivers.back()->active = false;
                std::cout << "Added RX" << newi << "\n";
            }
        } else if (cmd == "spectrum" || cmd == "spec") {
            // lightweight: just report latest for primary dev 0 if any
            std::vector<float> p; double cf,sr;
            if (mgr.getLatestSpectrum(0, p, cf, sr) && !p.empty()) {
                std::cout << "Spec dev0 cf=" << (cf/1e6) << " sr=" << (sr/1e6) << " bins=" << p.size() << " peak~ " << *std::max_element(p.begin(),p.end()) << "dB\n";
            } else std::cout << "no spectrum yet (enable + stream first)\n";
        } else if (cmd == "scan") {
            std::cout << "scan: (PR6 stub) use 'enable 0; tune <f>; stats' for now. Smart scanner in later phase.\n";
        } else if (cmd == "status") {
            printStats(0);
            std::cout << "Streaming: " << (mgr.isStreaming(0) ? "active" : "idle") << "\n";
        } else {
            std::cout << "Unknown cmd '" << cmd << "'. 'help' for list.\n";
        }
        } // end while
    } catch (const std::exception& ex) {
        spdlog::error("Unhandled exception in CLI command loop: {}", ex.what());
        std::cout << "CLI error (see log): " << ex.what() << "\n";
        cliStop = true;
    } catch (...) {
        spdlog::error("Unknown exception in CLI command loop");
        std::cout << "CLI unknown error\n";
        cliStop = true;
    }

    cliStop = true;
    if (cliMonThread.joinable()) cliMonThread.join();
    // stop any streams we started in this CLI session (best effort)
    for (size_t i=0; i<mgr.getDevices().size(); ++i) {
        if (mgr.isStreaming(i)) { try { mgr.stopStreaming(i); } catch(...) {} }
    }
    spdlog::info("CLI exiting");
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
        app.setApplicationVersion(SDR_TOWN_VERSION);

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
