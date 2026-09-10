#pragma once

// Purpose: Primary Qt MainWindow and GUI-only P25 table helper.
// Spec:    docs/DECISIONS.md DEC-0040 / ISS-0004 Phase 8 (mechanical extract from main.cpp)
// Invariants: no hop/TTL/CADENCE/feed-gate behavior changes — move-only.

#include "AppBootstrap.h"
#include "AudioCapture.h"
#include "AudioEngine.h"
#include "ClassifierModelBackend.h"
#include "CliApp.h"
#include "Demod.h"
#include "DemodModeUtils.h"
#include "DeviceManager.h"
#include "IP25AmbeEncoder.h"
#include "P25AppGlobals.h"
#include "P25AudioDropClass.h"
#include "P25Control.h"
#include "P25DebugStage.h"
#include "P25DecodeConfig.h"
#include "P25FollowStateMachine.h"
#include "P25LiveDecoder.h"
#include "P25Phase2TxFramer.h"
#include "P25RollingIq.h"
#include "P25SdrtrunkTune.h"
#include "P25TalkgroupRegistry.h"
#include "P25TranscriptSource.h"
#include "P25TxConfig.h"
#include "P25TxSession.h"
#include "P25VoiceDecode.h"
#include "P25VoiceSession.h"
#include "P25VoiceTest.h"
#include "P25VoiceTiming.h"
#include "SavedFrequencies.h"
#include "Receiver.h"
#include "RemoteDiagnostics.h"
#include "SignalClassifier.h"
#include "SpectrumWidget.h"
#include "SttEngine.h"
#include "TranscriptHub.h"
#include "TranscriptWindow.h"
#include "UpdateManager.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMetaObject>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QStandardPaths>
#include <QThread>
#include <QStatusBar>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <complex>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

using json = nlohmann::json;

inline constexpr long long kGuiP25ClearAudioMinAcceptedFrames = 10; // 200 ms at 20 ms/frame
inline constexpr long long kGuiP25ClearAudioMinSamples = 9600;      // 200 ms at 48 kHz

#ifndef SDR_TOWN_VERSION
#define SDR_TOWN_VERSION "0.0.0"
#endif
#ifndef SDR_TOWN_P25_AUDIO_BASELINE
#define SDR_TOWN_P25_AUDIO_BASELINE "p25-clear-continuous-20260810"
#endif

inline void populateP25Table(QTableWidget* table,
                             const std::vector<P25ControlCandidate>& hits,
                             const std::vector<P25KnownControlChannel>& knownChannels = loadP25KnownControlChannels())
{
    if (!table) return;
    struct Row {
        double freqHz = 0.0;
        QString snr = "-";
        QString bw = "12.5";
        QString peak = "-";
        QString nac = "-";
        QString note = "Known";
    };

    std::vector<Row> rows;
    rows.reserve(hits.size() + knownChannels.size());
    for (const auto& h : hits) {
        Row row;
        row.freqHz = h.freqHz;
        row.snr = QString::number(h.snrDb, 'f', 1);
        row.bw = QString::number(h.bandwidthHz / 1000.0, 'f', 1);
        row.peak = QString::number(h.peakDb, 'f', 1);
        row.note = "Scan";
        rows.push_back(std::move(row));
    }

    for (const auto& cc : knownChannels) {
        if (!std::isfinite(cc.freqHz) || cc.freqHz <= 0.0) continue;
        const QString label = cc.label.empty()
            ? QString("Known")
            : QString("Known: %1").arg(QString::fromStdString(cc.label));
        auto it = std::find_if(rows.begin(), rows.end(), [&](const Row& row) {
            return std::abs(row.freqHz - cc.freqHz) <= 50.0;
        });
        if (it == rows.end()) {
            Row row;
            row.freqHz = cc.freqHz;
            row.note = label;
            rows.push_back(std::move(row));
        } else if (!it->note.contains("Known")) {
            it->note += " + " + label;
        }
    }

    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
        return a.freqHz < b.freqHz;
    });

    table->setRowCount(static_cast<int>(rows.size()));
    for (int row = 0; row < static_cast<int>(rows.size()); ++row) {
        const auto& h = rows[static_cast<size_t>(row)];
        auto* freq = new QTableWidgetItem(QString::number(h.freqHz / 1e6, 'f', 5));
        freq->setData(Qt::UserRole, h.freqHz);
        table->setItem(row, 0, freq);
        table->setItem(row, 1, new QTableWidgetItem(h.snr));
        table->setItem(row, 2, new QTableWidgetItem(h.bw));
        table->setItem(row, 3, new QTableWidgetItem(h.peak));
        table->setItem(row, 4, new QTableWidgetItem(h.nac));
        table->setItem(row, 5, new QTableWidgetItem(h.note));
    }
}

// DSP implementation now lives in src/Demod.cpp (Demodulator class owns all state per Receiver).
// classifyMode, detectChannelBandwidth, and demodulateToAudio are provided via Demod.h + Demod.cpp.
// Phase 0: no more global gGuiDemod/gCliDemod - each Receiver owns its Demodulator instance (state isolation).


enum class P25VoicePublishOutcome : uint8_t {
    Deferred = 0,
    Published,
    DiscardedStale,
    ReceiverGone
};

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(const GuiRuntimeConfig& config = GuiRuntimeConfig{}, QWidget* parent = nullptr)
        : QMainWindow(parent),
          guiRuntimeConfig(config)
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
            classifierRoiBuilder.clear();
            const BandPlanEntry* plan = autoDetectMode ? findBandPlanForFrequency(f) : nullptr;
            {
                std::lock_guard<std::mutex> lk(monitorParamsMutex);
                currentMonitorFreq = f;
                if (plan) {
                    currentMonitorMode = plan->mode;
                    monitorChannelBwHz = plan->bandwidthHz;
                    monitorLpfHz = plan->lpfHz;
                }
            }
            if (plan && bwSpin) {
                bwSpin->blockSignals(true);
                bwSpin->setValue(plan->bandwidthHz / 1000.0);
                bwSpin->blockSignals(false);
            }
            if (plan && lpfSpin) {
                lpfSpin->blockSignals(true);
                lpfSpin->setValue(plan->lpfHz / 1000.0);
                lpfSpin->blockSignals(false);
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
                try { mgr.startStreaming(0, true /* real SDR, not stub */); } catch (...) { spdlog::warn("startStreaming(0,true) fault in Add Receiver (guarded)"); }

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
                    newRx->audioLpfEnabled = monitorAudioLpfEnabled;
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
        monFreq->setDecimals(5);
        monFreq->setValue(100.0);
        monFreq->setSingleStep(0.0125);
        QPushButton* setMonBtn = new QPushButton("Set & Tune Device");
        QComboBox* modeBox = new QComboBox();
        modeBox->addItem("AUTO"); modeBox->addItem("NFM"); modeBox->addItem("WFM"); modeBox->addItem("AM"); modeBox->addItem("USB"); modeBox->addItem("LSB"); modeBox->addItem("CW");
        modeBox->setCurrentText("AUTO");
        bwSpin = new QDoubleSpinBox();
        bwSpin->setRange(0.5, 500); bwSpin->setValue(180.0); bwSpin->setSuffix(" kHz"); bwSpin->setDecimals(1); bwSpin->setSingleStep(0.5);
        bwSpin->setToolTip("Receiver/channel bandwidth. NFM CB/PMR often uses 12.5 kHz; WFM broadcast uses ~180 kHz.");
        QPushButton* autoBwBtn = new QPushButton("Auto BW");
        autoBwBtn->setToolTip("Detect occupied bandwidth around the tuned frequency and snap to a sensible channel width.");
        lpfEnableCheck = new QCheckBox("LPF");
        lpfEnableCheck->setChecked(true);
        lpfEnableCheck->setToolTip("Enable/disable the post-demod audio low-pass filter. Disable for data/decoder workflows where filtering breaks symbols.");
        lpfSpin = new QDoubleSpinBox();
        lpfSpin->setRange(0.1, 200.0); lpfSpin->setValue(15.0); lpfSpin->setSuffix(" kHz"); lpfSpin->setDecimals(1); lpfSpin->setSingleStep(0.5);
        lpfSpin->setToolTip("Audio LPF cutoff. This is independent from RF/channel bandwidth and can be disabled with the LPF checkbox.");
        monLay->addWidget(monFreq);
        monLay->addWidget(setMonBtn);
        monLay->addWidget(new QLabel("Mode:"));
        monLay->addWidget(modeBox);
        monLay->addWidget(new QLabel("BW:"));
        monLay->addWidget(bwSpin);
        monLay->addWidget(autoBwBtn);
        monLay->addWidget(lpfEnableCheck);
        monLay->addWidget(lpfSpin);
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
        squelchSpin->setRange(-130, 40); squelchSpin->setDecimals(0); squelchSpin->setValue(-105);
        squelchSpin->setToolTip("RF squelch threshold in spectrum dB. Put SQ a few dB above the green noise-floor line; signals above SQ open audio. Values below -115 disable squelch.");
        gainLay->addWidget(squelchSpin);

        QPushButton* autoSquelchBtn = new QPushButton("Auto");
        autoSquelchBtn->setToolTip("Set squelch from the measured local RF noise floor. NFM uses a lighter offset so readable repeaters are not muted.");
        gainLay->addWidget(autoSquelchBtn);

        QLabel* rmsLabel = new QLabel("SIG: --- dB  NF: --- dB");
        rmsLabel->setToolTip("Live RF signal, local noise floor, and SNR used by the squelch gate.");
        gainLay->addWidget(rmsLabel);
        QLabel* classifierStatus = new QLabel("Classifier: deterministic ---");
        classifierStatus->setToolTip("Deterministic ROI classifier is active. ONNX model loading is an experimental placeholder until a trained model contract is validated.");
        gainLay->addWidget(classifierStatus);
        rxLay->addLayout(gainLay);

        QHBoxLayout* audioLay = new QHBoxLayout();
        audioLay->addWidget(new QLabel("Volume:"));
        QSlider* masterVolSlider = new QSlider(Qt::Horizontal);
        masterVolSlider->setRange(0, 100);
        masterVolSlider->setFixedWidth(160);
        masterVolSlider->setValue(static_cast<int>(std::lround(monitorMasterVolume * 100.0)));
        QLabel* masterVolValue = new QLabel(QString("%1%").arg(masterVolSlider->value()));
        QPushButton* outputsBtn = new QPushButton("Outputs...");
        QLabel* outputsLabel = new QLabel("Outputs: not configured");
        outputsLabel->setMinimumWidth(220);
        audioLay->addWidget(masterVolSlider);
        audioLay->addWidget(masterVolValue);
        audioLay->addWidget(outputsBtn);
        audioLay->addWidget(outputsLabel);
        audioLay->addStretch();
        rxLay->addLayout(audioLay);

        connect(masterVolSlider, &QSlider::valueChanged, this, [this, masterVolValue](int v) {
            monitorMasterVolume = std::clamp(v / 100.0, 0.0, 1.0);
            masterVolValue->setText(QString("%1%").arg(v));
            if (auto* eng = getOrCreateAudioEngine()) {
                eng->setMasterVolume(static_cast<float>(monitorMasterVolume));
            }
        });
        connect(outputsBtn, &QPushButton::clicked, this, &MainWindow::onAudioConfig);

        QTimer* audioStatusUpdate = new QTimer(this);
        connect(audioStatusUpdate, &QTimer::timeout, this, [this, outputsLabel, masterVolSlider, masterVolValue]() {
            if (!engineForAudio) {
                outputsLabel->setText("Outputs: not configured");
                return;
            }
            const int volPct = static_cast<int>(std::lround(engineForAudio->getMasterVolume() * 100.0f));
            monitorMasterVolume = std::clamp(volPct / 100.0, 0.0, 1.0);
            masterVolSlider->blockSignals(true);
            masterVolSlider->setValue(volPct);
            masterVolSlider->blockSignals(false);
            masterVolValue->setText(QString("%1%").arg(volPct));
            outputsLabel->setText(QString("Outputs: %1")
                .arg(QString::fromStdString(engineForAudio->getActiveDeviceNames())));
        });
        audioStatusUpdate->start(1200);

        // Live channel-level readout (polled lightly from the main UI timer)
        QTimer* rmsUpdate = new QTimer(this);
        connect(rmsUpdate, &QTimer::timeout, this, [rmsLabel, spectrum]() {
            double sig = gLastRmsDb.load();
            double nf = gLastNoiseFloorDb.load();
            double snr = gLastSnrDb.load();
            double afc = gLastAfcOffsetHz.load();
            double ppmDelta = gLastAfcPpmDelta.load();
            double afcConf = gLastAfcConfidence.load();
            if (sig > -180 && nf > -180) {
                QString ppmText;
                if (std::isfinite(ppmDelta) && std::abs(afc) >= 25.0) {
                    ppmText = QString("  PPM corr: %1  C:%2")
                        .arg(ppmDelta, 0, 'f', 2)
                        .arg(afcConf, 0, 'f', 2);
                }
                rmsLabel->setText(QString("SIG: %1 dB  NF: %2 dB  SNR: %3  AFC: %4 kHz%5")
                    .arg(sig, 0, 'f', 1)
                    .arg(nf, 0, 'f', 1)
                    .arg(snr, 0, 'f', 1)
                    .arg(afc / 1000.0, 0, 'f', 2)
                    .arg(ppmText));
            }
            if (spectrum) spectrum->setLiveLevels(sig, nf);
        });
        rmsUpdate->start(400);

        connect(autoSquelchBtn, &QPushButton::clicked, this, [this, squelchSpin]() {
            double nf = gLastNoiseFloorDb.load();
            DemodMode mode = DemodMode::NFM;
            {
                std::lock_guard<std::mutex> lk(monitorParamsMutex);
                mode = currentMonitorMode;
            }
            const double offsetDb = (mode == DemodMode::NFM) ? 6.0
                                  : (mode == DemodMode::AM || mode == DemodMode::USB || mode == DemodMode::LSB || mode == DemodMode::CW) ? 8.0
                                  : 10.0;
            // Set squelch threshold above the local RF floor, not above integrated channel RMS.
            double target = (nf > -150.0) ? (nf + offsetDb) : -105.0;
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
        colorMaxSpin->setRange(-60, 40); colorMaxSpin->setValue(-10);
        colorMaxSpin->setToolTip("Upper end of heat map. Adjust to make strong signals 'hot' red.");
        colorLay->addWidget(colorMaxSpin);
        rxLay->addLayout(colorLay);

        QHBoxLayout* trainingLay = new QHBoxLayout();
        QPushButton* captureTrainingBtn = new QPushButton("Capture Training Sample");
        QPushButton* captureIqStartBtn = new QPushButton("Start IQ Capture");
        QPushButton* captureIqStopBtn = new QPushButton("Stop IQ Capture");
        captureIqStopBtn->setEnabled(false);
        QLabel* trainingStatus = new QLabel("Capture: idle");
        trainingStatus->setMinimumWidth(320);
        captureTrainingBtn->setToolTip("Save a SigMF IQ capture plus normalized waterfall ROI tile for classifier training.");
        captureIqStartBtn->setToolTip("Start a continuous SigMF cf32_le IQ recording from the live ring with synchronized JSONL/P25/ring-health logs.");
        captureIqStopBtn->setToolTip("Stop the active IQ recording and finalize metadata, manifest, and log files.");
        trainingLay->addWidget(captureTrainingBtn);
        trainingLay->addWidget(captureIqStartBtn);
        trainingLay->addWidget(captureIqStopBtn);
        trainingLay->addWidget(trainingStatus);
        trainingLay->addStretch();
        rxLay->addLayout(trainingLay);

        QGroupBox* savedBox = new QGroupBox("Saved Frequencies");
        QVBoxLayout* savedLay = new QVBoxLayout(savedBox);
        savedLay->setContentsMargins(6, 8, 6, 6);
        savedLay->setSpacing(4);
        QHBoxLayout* savedBtnLay = new QHBoxLayout();
        QPushButton* savedAddBtn = new QPushButton("Add Current");
        QPushButton* savedTuneBtn = new QPushButton("Tune");
        QPushButton* savedDeleteBtn = new QPushButton("Delete");
        QPushButton* savedRefreshBtn = new QPushButton("Refresh");
        savedBtnLay->addWidget(savedAddBtn);
        savedBtnLay->addWidget(savedTuneBtn);
        savedBtnLay->addWidget(savedDeleteBtn);
        savedBtnLay->addWidget(savedRefreshBtn);
        savedBtnLay->addStretch();
        savedLay->addLayout(savedBtnLay);
        QTableWidget* savedTable = new QTableWidget(0, 5, this);
        savedTable->setHorizontalHeaderLabels({"Name", "Freq (MHz)", "Mode", "BW kHz", "Tags"});
        savedTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        savedTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        savedTable->verticalHeader()->setVisible(false);
        savedTable->setMaximumHeight(130);
        savedTable->horizontalHeader()->setStretchLastSection(true);
        savedLay->addWidget(savedTable);
        rxLay->addWidget(savedBox);

        auto refreshSavedTable = [savedTable]() {
            populateSavedFrequencyTable(savedTable, loadSavedFrequencies());
        };
        refreshSavedTable();

        connect(savedRefreshBtn, &QPushButton::clicked, this, [refreshSavedTable]() { refreshSavedTable(); });
        connect(savedAddBtn, &QPushButton::clicked, this, [this, savedTable]() {
            SavedFrequency sf;
            {
                std::lock_guard<std::mutex> lk(monitorParamsMutex);
                sf.freqHz = currentMonitorFreq;
                sf.mode = currentMonitorMode;
                sf.bandwidthHz = monitorChannelBwHz;
                sf.lpfHz = monitorLpfHz;
                sf.lpfEnabled = monitorAudioLpfEnabled;
                sf.squelchDb = monitorSquelchDb;
            }
            const QString defaultName = QString("%1 %2 MHz")
                .arg(modeToQString(sf.mode))
                .arg(sf.freqHz / 1e6, 0, 'f', 5);
            bool ok = false;
            const QString name = QInputDialog::getText(this, "Save Frequency", "Name:", QLineEdit::Normal, defaultName, &ok);
            if (!ok) return;
            sf.name = trimCopy(name.toStdString());
            if (sf.name.empty()) sf.name = defaultName.toStdString();
            if (const auto* plan = findBandPlanForFrequency(sf.freqHz)) sf.tags = plan->name;
            auto freqs = loadSavedFrequencies();
            freqs.push_back(sf);
            saveSavedFrequencies(freqs);
            populateSavedFrequencyTable(savedTable, freqs);
            statusBar()->showMessage(QString("Saved %1 at %2 MHz").arg(QString::fromStdString(sf.name)).arg(sf.freqHz / 1e6, 0, 'f', 5), 2500);
        });
        connect(savedTuneBtn, &QPushButton::clicked, this, [this, savedTable, monFreq, modeBox, squelchSpin, setMonBtn, spectrum]() {
            int row = savedTable->currentRow();
            auto freqs = loadSavedFrequencies();
            if (row < 0 || row >= static_cast<int>(freqs.size())) return;
            const auto sf = freqs[static_cast<size_t>(row)];

            monFreq->setValue(sf.freqHz / 1e6);
            modeBox->blockSignals(true);
            modeBox->setCurrentText(modeToQString(sf.mode));
            modeBox->blockSignals(false);
            if (bwSpin) {
                bwSpin->blockSignals(true);
                bwSpin->setValue(sf.bandwidthHz / 1000.0);
                bwSpin->blockSignals(false);
            }
            if (lpfSpin) {
                lpfSpin->blockSignals(true);
                lpfSpin->setValue(sf.lpfHz / 1000.0);
                lpfSpin->blockSignals(false);
            }
            if (lpfEnableCheck) {
                lpfEnableCheck->blockSignals(true);
                lpfEnableCheck->setChecked(sf.lpfEnabled);
                lpfEnableCheck->blockSignals(false);
                if (lpfSpin) lpfSpin->setEnabled(sf.lpfEnabled);
            }
            squelchSpin->blockSignals(true);
            squelchSpin->setValue(sf.squelchDb);
            squelchSpin->blockSignals(false);
            if (spectrum) spectrum->setSquelchThreshold(sf.squelchDb);

            {
                std::lock_guard<std::mutex> lk(monitorParamsMutex);
                currentMonitorFreq = sf.freqHz;
                currentMonitorMode = sf.mode;
                autoDetectMode = (sf.mode == DemodMode::AUTO);
                monitorChannelBwHz = sf.bandwidthHz;
                monitorLpfHz = sf.lpfHz;
                monitorAudioLpfEnabled = sf.lpfEnabled;
                monitorSquelchDb = sf.squelchDb;
            }
            syncMonitorVarsToReceiver(0);
            setMonBtn->click();
        });
        connect(savedDeleteBtn, &QPushButton::clicked, this, [this, savedTable]() {
            int row = savedTable->currentRow();
            auto freqs = loadSavedFrequencies();
            if (row < 0 || row >= static_cast<int>(freqs.size())) return;
            const auto removed = freqs[static_cast<size_t>(row)];
            freqs.erase(freqs.begin() + row);
            saveSavedFrequencies(freqs);
            populateSavedFrequencyTable(savedTable, freqs);
            statusBar()->showMessage(QString("Deleted saved frequency %1").arg(QString::fromStdString(removed.name)), 2000);
        });
        connect(savedTable, &QTableWidget::cellDoubleClicked, this, [savedTuneBtn](int, int) {
            savedTuneBtn->click();
        });

        QGroupBox* p25Box = new QGroupBox("P25 Control Channels");
        QVBoxLayout* p25Lay = new QVBoxLayout(p25Box);
        p25Lay->setContentsMargins(6, 8, 6, 6);
        p25Lay->setSpacing(4);
        QHBoxLayout* p25BtnLay = new QHBoxLayout();
        QPushButton* p25ScanBtn = new QPushButton("Scan P25 CC");
        p25ScanBtn->setCheckable(true);
        QPushButton* p25MonitorBtn = new QPushButton("Monitor CC");
        QPushButton* p25GrantTestBtn = new QPushButton("Grant Test");
        p25GrantTestBtn->setToolTip("Tune the selected control channel, mute raw control audio, enable auto-follow, and open the P25 log for grant/audio testing.");
        QPushButton* p25RefreshBtn = new QPushButton("Refresh");
        QPushButton* p25KnownBtn = new QPushButton("Add CC...");
        QPushButton* p25LogBtn = new QPushButton("P25 Log");
        QCheckBox* p25AutoFollowCheck = new QCheckBox("Auto Follow Grants");
        p25AutoFollowCheck->setToolTip("While monitoring a P25 control channel, automatically follow clear voice grants and return to the control channel when activity drops.");
        QCheckBox* p25IndependentTrafficCheck = new QCheckBox("Traffic Source");
        p25IndependentTrafficCheck->setToolTip("Use an sdrtrunk-style traffic-channel source for P25 grants. With one RTL-SDR this DDCs traffic that is already inside the sampled passband, or temporarily retunes the single tuner to traffic and returns to control after the call. Disable to use the older direct scanner-follow path.");
        p25IndependentTrafficCheck->setChecked(QSettings().value("p25/independentTrafficSource", true).toBool());
        p25IndependentTrafficEnabled = p25IndependentTrafficCheck->isChecked();
        QLabel* p25Status = new QLabel("Idle");
        p25AutoFollowCheckBox = p25AutoFollowCheck;
        p25IndependentTrafficCheckBox = p25IndependentTrafficCheck;
        p25StatusLabel = p25Status;
        p25BtnLay->addWidget(p25ScanBtn);
        p25BtnLay->addWidget(p25MonitorBtn);
        p25BtnLay->addWidget(p25GrantTestBtn);
        p25BtnLay->addWidget(p25RefreshBtn);
        p25BtnLay->addWidget(p25KnownBtn);
        p25BtnLay->addWidget(p25LogBtn);
        p25BtnLay->addWidget(p25AutoFollowCheck);
        p25BtnLay->addWidget(p25IndependentTrafficCheck);
        p25BtnLay->addWidget(p25Status);
        p25BtnLay->addStretch();
        p25Lay->addLayout(p25BtnLay);
        QTableWidget* p25Table = new QTableWidget(0, 6, this);
        p25Table->setHorizontalHeaderLabels({"Freq (MHz)", "SNR", "BW kHz", "Peak", "NAC", "Source / Notes"});
        p25Table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        p25Table->setSelectionBehavior(QAbstractItemView::SelectRows);
        p25Table->verticalHeader()->setVisible(false);
        p25Table->setMaximumHeight(130);
        p25Table->horizontalHeader()->setStretchLastSection(true);
        p25Lay->addWidget(p25Table);
        populateP25Table(p25Table, {}, loadP25KnownControlChannels());

        QHBoxLayout* p25TgBtnLay = new QHBoxLayout();
        QPushButton* p25TgManualBtn = new QPushButton("Add TG...");
        QPushButton* p25TgVerifyBtn = new QPushButton("Verify");
        QPushButton* p25TgScannerBtn = new QPushButton("Add to Scanner");
        QPushButton* p25TgPriorityBtn = new QPushButton("Set Priority...");
        p25TgPriorityBtn->setToolTip("Set userPriority for auto-follow preempt (higher wins). Persisted in p25_talkgroups.json.");
        QPushButton* p25TgFollowBtn = new QPushButton("Follow TG");
        p25TgFollowBtn->setCheckable(true);
        QPushButton* p25TgDeleteBtn = new QPushButton("Delete TG");
        QPushButton* p25TgRefreshBtn = new QPushButton("Refresh TGs");
        p25TgBtnLay->addWidget(new QLabel("Talkgroups:"));
        p25TgBtnLay->addWidget(p25TgManualBtn);
        p25TgBtnLay->addWidget(p25TgVerifyBtn);
        p25TgBtnLay->addWidget(p25TgScannerBtn);
        p25TgBtnLay->addWidget(p25TgPriorityBtn);
        p25TgBtnLay->addWidget(p25TgFollowBtn);
        p25TgBtnLay->addWidget(p25TgDeleteBtn);
        p25TgBtnLay->addWidget(p25TgRefreshBtn);
        p25TgBtnLay->addStretch();
        p25Lay->addLayout(p25TgBtnLay);

        QTableWidget* p25TgTable = new QTableWidget(0, 10, this);
        p25TgTable->setHorizontalHeaderLabels(
            {"CC MHz", "TGID", "Alpha Tag", "Voice MHz", "Src", "Hits", "Pri", "Enc", "Status", "Last Seen"});
        p25TgTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        p25TgTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        p25TgTable->verticalHeader()->setVisible(false);
        p25TgTable->setMaximumHeight(150);
        p25TgTable->horizontalHeader()->setStretchLastSection(true);
        p25Lay->addWidget(p25TgTable);
        rxLay->addWidget(p25Box);

        // Sprint 0: P25 clear TX shell (no RF). Arm + hold-PTT drives stub SM only.
        QGroupBox* p25TxBox = new QGroupBox("P25 Phase 2 Clear TX (Sprint 0 — no RF)");
        QVBoxLayout* p25TxLay = new QVBoxLayout(p25TxBox);
        p25TxLay->setContentsMargins(6, 8, 6, 6);
        p25TxLay->setSpacing(4);
        QHBoxLayout* p25TxCfgLay = new QHBoxLayout();
        QCheckBox* p25TxArmCheck = new QCheckBox("Arm TX");
        p25TxArmCheck->setToolTip("TX is off until armed. Requires RID, TG, NAC, and a TX-capable device (not RTL). No RF in Sprint 0.");
        QSpinBox* p25TxRidSpin = new QSpinBox();
        p25TxRidSpin->setRange(0, 0xFFFFFF);
        p25TxRidSpin->setPrefix("RID ");
        p25TxRidSpin->setToolTip("Subscriber unit ID (RID) for channel request.");
        QSpinBox* p25TxTgSpin = new QSpinBox();
        p25TxTgSpin->setRange(0, 0xFFFF);
        p25TxTgSpin->setPrefix("TG ");
        p25TxTgSpin->setToolTip("Talkgroup to request (clear only).");
        QSpinBox* p25TxNacSpin = new QSpinBox();
        p25TxNacSpin->setRange(0, 0xFFF);
        p25TxNacSpin->setDisplayIntegerBase(16);
        p25TxNacSpin->setPrefix("NAC 0x");
        p25TxNacSpin->setToolTip("Network Access Code (hex).");
        QSpinBox* p25TxDevSpin = new QSpinBox();
        p25TxDevSpin->setRange(-1, 31);
        p25TxDevSpin->setSpecialValueText("TX dev —");
        p25TxDevSpin->setPrefix("TX dev ");
        p25TxDevSpin->setToolTip("DeviceManager index of TX-capable SDR (HackRF/Pluto/Lime). RTL cannot TX.");
        p25TxDevSpin->setValue(-1);
        QPushButton* p25TxPttBtn = new QPushButton("PTT (hold)");
        p25TxPttBtn->setCheckable(true);
        p25TxPttBtn->setEnabled(false);
        p25TxPttBtn->setMinimumWidth(110);
        p25TxPttBtn->setStyleSheet("QPushButton:checked { background-color: #c0392b; color: white; font-weight: bold; }");
        p25TxPttBtn->setToolTip("Press and hold to request/TX (stub until Sprint 6). Release to unkey.");
        QLabel* p25TxStatus = new QLabel("Idle (disarmed)");
        p25TxStatus->setMinimumWidth(220);
        p25TxCfgLay->addWidget(p25TxArmCheck);
        p25TxCfgLay->addWidget(p25TxRidSpin);
        p25TxCfgLay->addWidget(p25TxTgSpin);
        p25TxCfgLay->addWidget(p25TxNacSpin);
        p25TxCfgLay->addWidget(p25TxDevSpin);
        p25TxCfgLay->addWidget(p25TxPttBtn);
        p25TxCfgLay->addWidget(p25TxStatus);
        p25TxCfgLay->addStretch();
        p25TxLay->addLayout(p25TxCfgLay);

        // Sprint 2: mic select + level meter (feeds future AMBE encode).
        QHBoxLayout* p25TxMicLay = new QHBoxLayout();
        QComboBox* p25TxMicCombo = new QComboBox();
        p25TxMicCombo->setMinimumWidth(220);
        p25TxMicCombo->setToolTip("Capture device for P25 TX mic path.");
        QPushButton* p25TxMicRefreshBtn = new QPushButton("Mics");
        p25TxMicRefreshBtn->setToolTip("Refresh capture device list.");
        QProgressBar* p25TxMicMeter = new QProgressBar();
        p25TxMicMeter->setRange(0, 100);
        p25TxMicMeter->setValue(0);
        p25TxMicMeter->setTextVisible(true);
        p25TxMicMeter->setFormat("mic %p%");
        p25TxMicMeter->setMaximumWidth(160);
        QCheckBox* p25TxMicMuteSpk = new QCheckBox("Mute spk on PTT");
        p25TxMicMuteSpk->setChecked(true);
        p25TxMicMuteSpk->setToolTip("Mute speaker outputs while PTT is held (anti-feedback).");
        p25TxMicLay->addWidget(new QLabel("Mic:"));
        p25TxMicLay->addWidget(p25TxMicCombo, 1);
        p25TxMicLay->addWidget(p25TxMicRefreshBtn);
        p25TxMicLay->addWidget(p25TxMicMeter);
        p25TxMicLay->addWidget(p25TxMicMuteSpk);
        p25TxLay->addLayout(p25TxMicLay);

        QLabel* p25TxHint = new QLabel("Clear unencrypted only. Sprint 2: mic capture + level meter. PTT still advances TX SM; RF/encode not wired.");
        p25TxHint->setWordWrap(true);
        p25TxHint->setStyleSheet("color: #888;");
        p25TxLay->addWidget(p25TxHint);
        rxLay->addWidget(p25TxBox);

        p25TxMicComboBox = p25TxMicCombo;
        p25TxMicMeterBar = p25TxMicMeter;
        p25TxMuteSpkCheckBox = p25TxMicMuteSpk;

        auto refreshP25TxMics = [this, p25TxMicCombo]() {
            p25TxMicCombo->clear();
            const auto mics = p25TxMic.enumerateCaptureDevices();
            if (mics.empty()) {
                p25TxMicCombo->addItem("(no capture devices)");
                return;
            }
            int def = 0;
            for (size_t i = 0; i < mics.size(); ++i) {
                QString label = QString::fromStdString(mics[i].name);
                if (mics[i].isDefault) {
                    label += " (default)";
                    def = static_cast<int>(i);
                }
                p25TxMicCombo->addItem(label, static_cast<int>(i));
            }
            p25TxMicCombo->setCurrentIndex(def);
        };
        refreshP25TxMics();
        connect(p25TxMicRefreshBtn, &QPushButton::clicked, this, [refreshP25TxMics]() { refreshP25TxMics(); });

        if (!p25TxMicMeterTimer) {
            p25TxMicMeterTimer = new QTimer(this);
            p25TxMicMeterTimer->setInterval(50);
            connect(p25TxMicMeterTimer, &QTimer::timeout, this, [this]() {
                if (!p25TxMicMeterBar) return;
                if (!p25TxMic.isCapturing()) {
                    p25TxMicMeterBar->setValue(0);
                    return;
                }
                float sink[2048];
                size_t pulled = 0;
                while (true) {
                    const size_t n = p25TxMic.pull(sink, 2048);
                    if (n == 0) break;
                    pulled += n;
                    // Sprint 3: while PTT held, resample to 8 kHz and encode placeholders.
                    if (p25TxSnapshot.pttHeld && p25TxPacketizer) {
                        float pcm8k[512];
                        // Simple box-decimate 48k→8k: every 6th sample average of 6.
                        size_t outN = 0;
                        for (size_t i = 0; i + 6 <= n && outN < 512; i += 6) {
                            float s = 0.0f;
                            for (size_t k = 0; k < 6; ++k) s += sink[i + k];
                            pcm8k[outN++] = s / 6.0f;
                        }
                        if (outN > 0) {
                            std::vector<P25AmbeEncodedFrame> produced;
                            p25TxPacketizer->pushPcm8k(pcm8k, outN, produced);
                            if (!produced.empty()) {
                                p25TxAmbeSession.insert(p25TxAmbeSession.end(), produced.begin(), produced.end());
                            }
                        }
                    }
                }
                const int pct = static_cast<int>(std::lround(std::min(1.0f, p25TxMic.levelMeter()) * 100.0f));
                p25TxMicMeterBar->setValue(pct);
                if (p25TxSnapshot.pttHeld && p25TxPacketizer) {
                    const qint64 now = QDateTime::currentMSecsSinceEpoch();
                    if (now - p25TxLastEncodeLogMs >= 1000) {
                        p25TxLastEncodeLogMs = now;
                        appendP25LogLine(QString("P25 TX encode: backend=%1 frames=%2 pending=%3 meter=%4%")
                            .arg(QString::fromUtf8(p25TxPacketizer->encoder() ? p25TxPacketizer->encoder()->name() : "?"))
                            .arg(static_cast<qulonglong>(p25TxPacketizer->framesEncoded()))
                            .arg(static_cast<int>(p25TxPacketizer->pendingSamples()))
                            .arg(pct));
                    }
                }
                (void)pulled;
            });
        }

        p25TxArmCheckBox = p25TxArmCheck;
        p25TxPttButton = p25TxPttBtn;
        p25TxStatusLabel = p25TxStatus;
        p25TxRidSpinBox = p25TxRidSpin;
        p25TxTgSpinBox = p25TxTgSpin;
        p25TxNacSpinBox = p25TxNacSpin;
        p25TxDevSpinBox = p25TxDevSpin;

        // Restore Sprint 0 TX identity from settings (still disarmed by default).
        {
            QSettings s;
            p25TxRidSpin->setValue(s.value("p25tx/rid", 0).toInt());
            p25TxTgSpin->setValue(s.value("p25tx/tg", 0).toInt());
            p25TxNacSpin->setValue(s.value("p25tx/nac", 0).toInt());
            p25TxDevSpin->setValue(s.value("p25tx/device", -1).toInt());
            p25TxSnapshot = {};
            p25TxSnapshot.state = P25TxState::Idle;
            p25TxSnapshot.config.armed = false;
        }

        auto syncP25TxConfigFromUi = [this]() {
            p25TxSnapshot.config.unitId = static_cast<uint32_t>(p25TxRidSpinBox ? p25TxRidSpinBox->value() : 0);
            p25TxSnapshot.config.talkgroupId = static_cast<uint32_t>(p25TxTgSpinBox ? p25TxTgSpinBox->value() : 0);
            p25TxSnapshot.config.nac = static_cast<uint16_t>(p25TxNacSpinBox ? p25TxNacSpinBox->value() : 0);
            p25TxSnapshot.config.txDeviceIndex = p25TxDevSpinBox ? p25TxDevSpinBox->value() : -1;
            p25TxSnapshot.config.clearOnly = true;
            p25TxSnapshot.config.armed = p25TxArmCheckBox && p25TxArmCheckBox->isChecked();
            // Sprint 0: SM arm only needs a selected device index so the PTT shell
            // is testable without TX hardware. Sprint 1+ writeStream still requires
            // DeviceInfo::canTx on a real TX-capable SDR.
            p25TxSnapshot.deviceCanTx = (p25TxSnapshot.config.txDeviceIndex >= 0);
            QSettings s;
            s.setValue("p25tx/rid", static_cast<int>(p25TxSnapshot.config.unitId));
            s.setValue("p25tx/tg", static_cast<int>(p25TxSnapshot.config.talkgroupId));
            s.setValue("p25tx/nac", static_cast<int>(p25TxSnapshot.config.nac));
            s.setValue("p25tx/device", p25TxSnapshot.config.txDeviceIndex);
        };

        auto applyP25TxEvent = [this, syncP25TxConfigFromUi](P25TxEvent ev) {
            syncP25TxConfigFromUi();
            p25TxSnapshot.nowMs = QDateTime::currentMSecsSinceEpoch();
            if (p25TxSnapshot.stateEnteredMs <= 0) {
                p25TxSnapshot.stateEnteredMs = p25TxSnapshot.nowMs;
            }
            const auto decision = evaluateP25Tx(p25TxSnapshot, ev);
            if (decision.changed) {
                p25TxSnapshot.state = decision.nextState;
                p25TxSnapshot.stateEnteredMs = p25TxSnapshot.nowMs;
            }
            if (p25TxStatusLabel) {
                p25TxStatusLabel->setText(QString::fromStdString(decision.statusLine));
            }
            if (decision.changed || ev == P25TxEvent::PttPress || ev == P25TxEvent::PttRelease ||
                ev == P25TxEvent::Arm || ev == P25TxEvent::Disarm) {
                appendP25LogLine(QString("P25 TX SM: event=%1 state=%2 reason=%3 request=%4 startVoice=%5 stopVoice=%6")
                    .arg(QString::fromUtf8(p25TxEventLabel(ev)))
                    .arg(QString::fromUtf8(p25TxStateLabel(p25TxSnapshot.state)))
                    .arg(QString::fromStdString(decision.reason))
                    .arg(decision.emitChannelRequest ? "yes" : "no")
                    .arg(decision.startVoiceTx ? "stub" : "no")
                    .arg(decision.stopVoiceTx ? "yes" : "no"));
            }
            if (p25TxPttButton &&
                (p25TxSnapshot.state == P25TxState::Idle ||
                 p25TxSnapshot.state == P25TxState::Error ||
                 p25TxSnapshot.state == P25TxState::Armed ||
                 p25TxSnapshot.state == P25TxState::Hang)) {
                // Keep visual press only while truly held.
            }
            if (p25TxArmCheckBox) {
                const bool showArmed = p25TxSnapshot.state != P25TxState::Idle &&
                    p25TxSnapshot.state != P25TxState::Error;
                if (p25TxArmCheckBox->isChecked() != (showArmed || p25TxSnapshot.config.armed)) {
                    // do not fight user checkbox mid-flow
                }
            }
            if (p25TxPttButton) {
                const bool allowPtt = p25TxSnapshot.state == P25TxState::Armed ||
                    p25TxSnapshot.state == P25TxState::Requesting ||
                    p25TxSnapshot.state == P25TxState::WaitGrant ||
                    p25TxSnapshot.state == P25TxState::TuningUplink ||
                    p25TxSnapshot.state == P25TxState::VoiceActive ||
                    p25TxSnapshot.state == P25TxState::Hang;
                p25TxPttButton->setEnabled(allowPtt || (p25TxArmCheckBox && p25TxArmCheckBox->isChecked()));
            }
        };

        connect(p25TxArmCheck, &QCheckBox::toggled, this, [this, applyP25TxEvent, syncP25TxConfigFromUi](bool on) {
            syncP25TxConfigFromUi();
            if (on) {
                applyP25TxEvent(P25TxEvent::Arm);
                if (p25TxSnapshot.state == P25TxState::Error) {
                    p25TxArmCheckBox->blockSignals(true);
                    p25TxArmCheckBox->setChecked(false);
                    p25TxArmCheckBox->blockSignals(false);
                    p25TxSnapshot.config.armed = false;
                }
            } else {
                applyP25TxEvent(P25TxEvent::Disarm);
                p25TxMic.stop();
                if (p25TxMicMeterTimer) p25TxMicMeterTimer->stop();
                if (p25TxMicMeterBar) p25TxMicMeterBar->setValue(0);
                if (AudioEngine* eng = getOrCreateAudioEngine()) {
                    eng->setOutputMuted(false);
                }
            }
            if (p25TxPttButton) {
                p25TxPttButton->setEnabled(p25TxSnapshot.state == P25TxState::Armed ||
                    p25TxSnapshot.state == P25TxState::Requesting ||
                    p25TxSnapshot.state == P25TxState::WaitGrant ||
                    p25TxSnapshot.state == P25TxState::TuningUplink ||
                    p25TxSnapshot.state == P25TxState::VoiceActive ||
                    p25TxSnapshot.state == P25TxState::Hang);
            }
        });
        // Hold-to-talk: press = PTT, release = unkey
        p25TxPttBtn->setAutoRepeat(false);
        connect(p25TxPttBtn, &QPushButton::pressed, this, [this, applyP25TxEvent]() {
            p25TxSnapshot.pttHeld = true;
            p25TxSnapshot.pttPressedMs = QDateTime::currentMSecsSinceEpoch();
            // Sprint 2: open mic + optional speaker mute while keyed.
            int micIdx = -1;
            if (p25TxMicComboBox && p25TxMicComboBox->currentData().isValid()) {
                micIdx = p25TxMicComboBox->currentData().toInt();
            }
            if (!p25TxMic.isCapturing()) {
                p25TxMic.start(micIdx, 48000.0);
            }
            if (!p25TxPacketizer) {
                // Energy placeholder until licensed AMBE encoder is linked.
                p25TxPacketizer = std::make_unique<P25TxVoicePacketizer>(p25CreateAmbeEncoder("energy"));
            }
            p25TxPacketizer->reset();
            p25TxAmbeSession.clear();
            p25TxLastEncodeLogMs = 0;
            if (p25TxMicMeterTimer) p25TxMicMeterTimer->start();
            if (p25TxMuteSpkCheckBox && p25TxMuteSpkCheckBox->isChecked()) {
                if (AudioEngine* eng = getOrCreateAudioEngine()) {
                    eng->setOutputMuted(true);
                }
            }
            applyP25TxEvent(P25TxEvent::PttPress);
            // Sprint 0: advance Requesting → WaitGrant → (no grant) stays waiting
            if (p25TxSnapshot.state == P25TxState::Requesting) {
                applyP25TxEvent(P25TxEvent::None);
            }
        });
        connect(p25TxPttBtn, &QPushButton::released, this, [this, applyP25TxEvent]() {
            p25TxSnapshot.pttHeld = false;
            if (AudioEngine* eng = getOrCreateAudioEngine()) {
                eng->setOutputMuted(false);
            }
            applyP25TxEvent(P25TxEvent::PttRelease);
            // Drain hang so UI returns to Armed without a timer for Sprint 0
            if (p25TxSnapshot.state == P25TxState::Hang) {
                p25TxSnapshot.stateEnteredMs = 0;
                p25TxSnapshot.nowMs = QDateTime::currentMSecsSinceEpoch();
                p25TxSnapshot.hangMs = 0;
                applyP25TxEvent(P25TxEvent::HangComplete);
            }
            if (p25TxPttButton) p25TxPttButton->setChecked(false);
            // Dump AMBE placeholders + skeleton superframe from this PTT hold.
            if (p25TxPacketizer && !p25TxAmbeSession.empty()) {
                const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
                QDir().mkpath(appData + "/tx_dumps");
                const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
                const std::string ambePath = (appData + "/tx_dumps/ambe_" + stamp + ".bin").toStdString();
                p25WriteAmbePackedDump(ambePath, p25TxAmbeSession);
                P25Phase2TxFramer framer;
                P25Phase2TxFramerConfig fcfg;
                fcfg.nac = p25TxSnapshot.config.nac;
                fcfg.wacn = p25TxSnapshot.config.wacn;
                fcfg.systemId = p25TxSnapshot.config.systemId;
                fcfg.talkgroupId = p25TxSnapshot.config.talkgroupId;
                fcfg.unitId = p25TxSnapshot.config.unitId;
                fcfg.slot = p25TxSnapshot.config.tdmaSlot;
                framer.setConfig(fcfg);
                const auto sf = framer.buildSuperframe(p25TxAmbeSession);
                const std::string dibitPath = (appData + "/tx_dumps/dibits_" + stamp + ".bin").toStdString();
                if (sf.valid) p25WriteTxDibitDump(dibitPath, sf);
                appendP25LogLine(QString("P25 TX PTT dump: ambeFrames=%1 ambe=%2 dibits=%3 note=%4")
                    .arg(static_cast<int>(p25TxAmbeSession.size()))
                    .arg(QString::fromStdString(ambePath))
                    .arg(QString::fromStdString(dibitPath))
                    .arg(QString::fromStdString(sf.note)));
            }
            // Keep mic open while armed so next PTT has no open delay; stop if disarmed.
            if (p25TxSnapshot.state == P25TxState::Idle || p25TxSnapshot.state == P25TxState::Error) {
                p25TxMic.stop();
                if (p25TxMicMeterTimer) p25TxMicMeterTimer->stop();
                if (p25TxMicMeterBar) p25TxMicMeterBar->setValue(0);
            }
        });

        auto refreshP25Talkgroups = [p25TgTable]() {
            populateP25TalkgroupTable(p25TgTable, loadP25Talkgroups());
        };
        refreshP25Talkgroups();

        auto selectedP25ControlHz = [this, p25Table]() -> double {
            const int row = p25Table ? p25Table->currentRow() : -1;
            if (row >= 0 && p25Table && p25Table->item(row, 0)) {
                const QVariant storedHz = p25Table->item(row, 0)->data(Qt::UserRole);
                if (storedHz.isValid() && storedHz.toDouble() > 0.0) return storedHz.toDouble();
                bool ok = false;
                const double mhz = p25Table->item(row, 0)->text().toDouble(&ok);
                if (ok && mhz > 0.0) return mhz * 1e6;
            }
            std::lock_guard<std::mutex> lk(monitorParamsMutex);
            return currentMonitorFreq;
        };

        auto tryApplyP25RetuneReceiverState = [this](double hz) -> bool {
            if (!std::isfinite(hz) || hz <= 0.0) return false;

            // NEVER block the Qt GUI thread on receivers/state locks.  The voice
            // worker can hold stateMutex for hundreds of ms during Phase-2 decode;
            // a blocking lock here freezes the whole UI (Windows: "not responding").
            // Caller retries via QTimer when we return false.
            std::unique_lock<std::mutex> listLock(receiversMutex, std::try_to_lock);
            if (!listLock.owns_lock()) return false;
            ensureReceiver();
            if (receivers.empty() || !receivers[0]) return true;

            auto& rx = *receivers[0];
            std::unique_lock<std::mutex> rxLock(rx.stateMutex, std::try_to_lock);
            if (!rxLock.owns_lock()) return false;
            double liveMonitorHz = 0.0;
            {
                std::unique_lock<std::mutex> monLock(monitorParamsMutex, std::try_to_lock);
                if (monLock.owns_lock()) liveMonitorHz = currentMonitorFreq;
            }
            if (std::isfinite(liveMonitorHz) && liveMonitorHz > 0.0 &&
                std::abs(liveMonitorHz - hz) > 50.0) {
                return true;
            }
            if (rx.p25VoiceDecodeEnabled &&
                p25FollowEnabled &&
                std::abs(p25AutoFollowVoiceFreqHz - hz) <= 50.0) {
                return true;
            }

            const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
            rx.p25ControlChannelMute = true;
            rx.p25VoiceDecodeEnabled = false;
            rx.p25VoiceSettleUntilMs = nowMs + kP25RetunePreArmMuteMs;
            rx.p25VoiceDiscardWindows = kP25RetunePreArmDiscardWindows;
            rx.freqHz = hz;
            rx.mode = DemodMode::NFM;
            rx.channelBwHz = 12500.0;
            rx.lpfHz = 3000.0;
            rx.audioLpfEnabled = false;
            rx.active = true;
            std::unique_lock<std::recursive_mutex> dspLock(rx.dspMutex, std::try_to_lock);
            if (dspLock.owns_lock()) {
                rx.resetDemodState();
                p25ClearPhase2PendingAudio(rx);
                rx.resetP25VoiceState();
                clearP25SessionScopedState(rx);
                rx.p25VoiceResetPending = false;
            } else {
                rx.lastConsumedAbsolute.store(0, std::memory_order_release);
                rx.afcLocked = false;
                rx.afcOffsetHz = 0.0;
                rx.p25AfcFrozen = false;
                rx.p25FrozenAfcOffsetHz = 0.0;
                rx.p25VoiceResetPending = true;
            }
            clearP25VoiceDiagnostics(rx);
            return true;
        };

        auto queueP25RetuneReceiverState = std::make_shared<std::function<void(double, int)>>();
        std::weak_ptr<std::function<void(double, int)>> weakP25RetuneReceiverState = queueP25RetuneReceiverState;
        *queueP25RetuneReceiverState = [this, tryApplyP25RetuneReceiverState, weakP25RetuneReceiverState](double hz, int attempt) {
            if (tryApplyP25RetuneReceiverState(hz)) return;
            if (attempt == 0) {
                appendP25LogLineKeyed("p25-retune-state-deferred",
                    "P25 follow state update deferred because the DSP worker was busy; UI remains responsive.",
                    2500);
            }
            if (attempt >= 120) {
                appendP25LogLine(QString("P25 follow state update timed out for %1MHz; DSP worker stayed busy too long.")
                    .arg(hz / 1e6, 0, 'f', 5));
                return;
            }
            if (auto retry = weakP25RetuneReceiverState.lock()) {
                QTimer::singleShot(15, this, [retry, hz, attempt]() {
                    (*retry)(hz, attempt + 1);
                });
            }
        };

        auto tuneP25Path = [this, monFreq, modeBox, spectrum, tryApplyP25RetuneReceiverState, queueP25RetuneReceiverState](double hz) {
            if (!std::isfinite(hz) || hz <= 0.0) return false;
            monFreq->setValue(hz / 1e6);
            modeBox->blockSignals(true);
            modeBox->setCurrentText("NFM");
            modeBox->blockSignals(false);
            if (bwSpin) {
                bwSpin->blockSignals(true);
                bwSpin->setValue(12.5);
                bwSpin->blockSignals(false);
            }
            if (lpfSpin) {
                lpfSpin->blockSignals(true);
                lpfSpin->setValue(3.0);
                lpfSpin->blockSignals(false);
            }
            if (lpfEnableCheck) {
                lpfEnableCheck->blockSignals(true);
                lpfEnableCheck->setChecked(false);
                lpfEnableCheck->blockSignals(false);
                if (lpfSpin) lpfSpin->setEnabled(false);
            }
            {
                std::lock_guard<std::mutex> lk(monitorParamsMutex);
                currentMonitorFreq = hz;
                currentMonitorMode = DemodMode::NFM;
                autoDetectMode = false;
                monitorChannelBwHz = 12500.0;
                monitorLpfHz = 3000.0;
                monitorAudioLpfEnabled = false;
            }
            // Hard mute immediately before any P25 retune. If the DSP worker is
            // inside a long voice decode window, defer the receiver mutation
            // instead of blocking the Qt event loop.
            if (!tryApplyP25RetuneReceiverState(hz)) {
                (*queueP25RetuneReceiverState)(hz, 0);
            }
            if (engineForAudio) {
                QTimer::singleShot(0, this, [this]() {
                    if (engineForAudio) engineForAudio->clearBuffers();
                });
            }

            auto& mgr = DeviceManager::instance();
            if (!mgr.getDevices().empty()) {
                mgr.setCenterFreq(0, hz);
                if (!mgr.isStreaming(0)) {
                    mgr.setEnabled(0, true);
                    try { mgr.startStreaming(0, true); } catch (...) {}
                }
            }
            if (spectrum) spectrum->setCenterFreq(hz);
            return true;
        };

        auto setP25ControlChannelMute = [this](bool muted) -> bool {
            {
                std::unique_lock<std::mutex> lk(receiversMutex, std::try_to_lock);
                if (!lk.owns_lock()) return false;
                ensureReceiver();
                if (receivers.empty() || !receivers[0]) return true;
                auto& rx = *receivers[0];
                std::unique_lock<std::mutex> rxLock(rx.stateMutex, std::try_to_lock);
                if (!rxLock.owns_lock()) return false;
                rx.p25ControlChannelMute = muted;
                if (muted) {
                    p25ClearPhase2PendingAudio(rx);
                    std::unique_lock<std::recursive_mutex> dspLock(rx.dspMutex, std::try_to_lock);
                    if (dspLock.owns_lock()) {
                        rx.resetDemodState();
                    }
                }
            }
            if (muted && engineForAudio) {
                engineForAudio->clearBuffers();
            }
            return true;
        };

        // Returns false if receiver locks were busy (caller should retry). Never blocks the GUI.
        auto clearP25VoiceFollowState = [this](bool controlMuteAfterClear = false) -> bool {
            std::unique_lock<std::mutex> lk(receiversMutex, std::try_to_lock);
            if (!lk.owns_lock()) return false;
            ensureReceiver();

            // sdrtrunk-style traffic-channel sources are separate receivers.
            // Tear them down by removing only those virtual/traffic receivers; do
            // not disturb the primary muted control-channel receiver.
            for (auto& oldTraffic : receivers) {
                if (!oldTraffic || !oldTraffic->p25IndependentTrafficSource) continue;
                std::unique_lock<std::mutex> oldLock(oldTraffic->stateMutex, std::try_to_lock);
                if (!oldLock.owns_lock()) return false;
                oldTraffic->active = false;
                oldTraffic->p25VoiceDecodeEnabled = false;
                oldTraffic->p25TrafficGeneration = 0;
            }
            const size_t before = receivers.size();
            receivers.erase(std::remove_if(receivers.begin(), receivers.end(),
                [](const std::shared_ptr<Receiver>& rx) {
                    return rx && rx->p25IndependentTrafficSource;
                }), receivers.end());
            if (receivers.size() != before) {
                p25IndependentTrafficActive = false;
                p25IndependentTrafficRetunedPrimary = false;
                p25PendingAudioFlushSeq.fetch_add(1, std::memory_order_release);
            }

            if (receivers.empty() || !receivers[0]) return true;
            auto& rx = *receivers[0];
            std::unique_lock<std::mutex> rxLock(rx.stateMutex, std::try_to_lock);
            if (!rxLock.owns_lock()) return false;
            clearP25VoiceFollowFieldsLocked(rx, controlMuteAfterClear);
            tryApplyP25VoiceResetLocked(rx);
            if (controlMuteAfterClear && engineForAudio) {
                engineForAudio->clearBuffers();
            }
            return true;
        };

        auto returnP25AutoFollowToControl = [this, p25Status, p25TgFollowBtn, tuneP25Path, clearP25VoiceFollowState, setP25ControlChannelMute]() {
            const auto retStart = std::chrono::steady_clock::now();
            const qint64 returnNowMs = QDateTime::currentMSecsSinceEpoch();
            const double releasedVoiceHz = p25AutoFollowVoiceFreqHz;
            const qint64 lastSpeakerBeforeReturnMs =
                guiP25AudioLastOutputMs.load(std::memory_order_relaxed);
            const bool recentSpeakerBeforeReturn =
                lastSpeakerBeforeReturnMs > 0 &&
                returnNowMs >= lastSpeakerBeforeReturnMs &&
                returnNowMs - lastSpeakerBeforeReturnMs <= kP25Phase2SpeakerFollowHoldMs;
            p25PendingAudioFlushSeq.fetch_add(1, std::memory_order_release);
            p25AutoFollowLastReturnMs = returnNowMs;
            p25AutoFollowLastReturnVoiceHz = releasedVoiceHz;
            // Cancel queued Phase-2 voice jobs without blocking the GUI if the
            // worker briefly holds the queue mutex.
            {
                std::unique_lock<std::mutex> lock(p25VoiceWorkerMutex, std::try_to_lock);
                if (lock.owns_lock()) {
                    p25VoicePendingJobs.clear();
                    p25VoiceCompletedResults.clear();
                }
            }
            p25VoiceWorkerCv.notify_all();

            const double ccHz = p25AutoFollowReturnControlFreqHz > 0.0
                ? p25AutoFollowReturnControlFreqHz
                : p25MonitoredControlFreqHz;
            p25FollowEnabled = false;
            p25FollowAutoActive = false;
            p25FollowTalkgroupId = 0;
            p25AutoFollowVoiceFreqHz = 0.0;
            p25AutoFollowTunedAtMs = 0;
            p25AutoFollowLastGrantMs = 0;
            p25AutoFollowLastActiveMs = 0;
            p25AutoFollowLastMHzHopMs = 0;
            guiP25AudioOutputEvents.store(0, std::memory_order_relaxed);
            guiP25AudioOutputSamples.store(0, std::memory_order_relaxed);
            guiP25AudioDecodedFrames.store(0, std::memory_order_relaxed);
            guiP25AudioAcceptedAmbeFrames.store(0, std::memory_order_relaxed);
            guiP25AudioLastOutputMs.store(0, std::memory_order_relaxed);
            gP25AudioLastSpeakerOutputMs.store(0, std::memory_order_relaxed);
            const bool wasIndependentTraffic = p25IndependentTrafficActive;
            const bool trafficRetunedPrimary = p25IndependentTrafficRetunedPrimary;
            if (p25TgFollowBtn) p25TgFollowBtn->setChecked(false);
            if (engineForAudio) engineForAudio->clearBuffers();
            if (!clearP25VoiceFollowState(true)) {
                appendP25LogLineKeyed("p25-return-clear-busy",
                    "P25 return-to-control deferred clear (receiver locks busy); will retry without freezing UI.",
                    1500);
                QTimer::singleShot(25, this, [this, clearP25VoiceFollowState]() {
                    if (!clearP25VoiceFollowState(true)) {
                        appendP25LogLineKeyed("p25-return-clear-busy",
                            "P25 return-to-control clear still busy; retrying.",
                            1500);
                        QTimer::singleShot(40, this, [clearP25VoiceFollowState]() {
                            (void)clearP25VoiceFollowState(true);
                        });
                    }
                });
            }
            auto ensureMute = [setP25ControlChannelMute]() {
                if (setP25ControlChannelMute(true)) return;
                QTimer::singleShot(20, [setP25ControlChannelMute]() {
                    if (!setP25ControlChannelMute(true)) {
                        QTimer::singleShot(40, [setP25ControlChannelMute]() {
                            (void)setP25ControlChannelMute(true);
                        });
                    }
                });
            };
            if (wasIndependentTraffic) {
                p25IndependentTrafficActive = false;
                p25IndependentTrafficRetunedPrimary = false;
                p25LiveDecoder.reset();
                p25ControlWorkerResetPending.store(true, std::memory_order_release);
                {
                    std::unique_lock<std::mutex> pendingLock(p25ControlPendingMutex, std::try_to_lock);
                    if (pendingLock.owns_lock()) p25ControlPendingResult.reset();
                }
                p25LastDiagSignature.clear();
                if (ccHz > 0.0) {
                    p25MonitoredControlFreqHz = ccHz;
                    if (trafficRetunedPrimary) {
                        if (releasedVoiceHz > 0.0 &&
                            std::isfinite(releasedVoiceHz) &&
                            recentSpeakerBeforeReturn) {
                            p25AutoFollowWarmStandbyVoiceHz = releasedVoiceHz;
                            p25AutoFollowWarmStandbyUntilMs =
                                returnNowMs + kP25Phase2WarmStandbyMs;
                            ensureMute();
                            appendP25LogLine(QString("P25 warm standby: tuner held on voice=%1MHz for %2ms rapid same-carrier re-grant (control=%3MHz decode muted).")
                                .arg(releasedVoiceHz / 1e6, 0, 'f', 5)
                                .arg(kP25Phase2WarmStandbyMs)
                                .arg(ccHz / 1e6, 0, 'f', 5));
                        } else if (tuneP25Path(ccHz)) {
                            if (releasedVoiceHz > 0.0 && std::isfinite(releasedVoiceHz)) {
                                appendP25LogLine(QString("P25 warm standby skipped: TG traffic had no recent selected-slot speaker audio, so RF retuned immediately from voice=%1MHz to control=%2MHz.")
                                    .arg(releasedVoiceHz / 1e6, 0, 'f', 5)
                                    .arg(ccHz / 1e6, 0, 'f', 5));
                            } else {
                                appendP25LogLine(QString("P25 one-RTL traffic source released; RF retuned back to control channel %1MHz.")
                                    .arg(ccHz / 1e6, 0, 'f', 5));
                            }
                            ensureMute();
                        } else {
                            appendP25LogLine(QString("P25 one-RTL traffic source released, but RF retune back to control channel %1MHz failed; not claiming CC monitor is active.")
                                .arg(ccHz / 1e6, 0, 'f', 5));
                            if (p25Status) p25Status->setText("Auto follow idle");
                            return;
                        }
                    } else {
                        appendP25LogLine(QString("P25 independent traffic source released; continuing muted control-channel monitor on %1MHz without RF retune.")
                            .arg(ccHz / 1e6, 0, 'f', 5));
                        ensureMute();
                    }
                    if (p25Status) {
                        if (p25AutoFollowWarmStandbyUntilMs > returnNowMs) {
                            p25Status->setText(QString("Warm standby voice %1 MHz")
                                .arg(p25AutoFollowWarmStandbyVoiceHz / 1e6, 0, 'f', 5));
                        } else {
                            p25Status->setText(QString("Monitoring CC %1 MHz").arg(ccHz / 1e6, 0, 'f', 5));
                        }
                    }
                } else if (p25Status) {
                    p25Status->setText("Auto follow idle");
                }
                return;
            }
            if (ccHz > 0.0 && tuneP25Path(ccHz)) {
                p25MonitoredControlFreqHz = ccHz;
                ensureMute();
                p25LiveDecoder.reset();
                // Do not block the Qt/UI thread behind an in-flight P25 control decode.
                // Request a reset and let the worker apply it when it next owns the decoder.
                p25ControlWorkerResetPending.store(true, std::memory_order_release);
                {
                    std::unique_lock<std::mutex> pendingLock(p25ControlPendingMutex, std::try_to_lock);
                    if (pendingLock.owns_lock()) p25ControlPendingResult.reset();
                }
                p25LastDiagSignature.clear();
                appendP25LogLine(QString("P25 follow returned to muted control channel %1MHz.").arg(ccHz / 1e6, 0, 'f', 5));
                appendP25LogLine("AFC unlock: returned to control channel; live AFC adaptation resumed.");
                if (p25Status) p25Status->setText(QString("Monitoring CC %1 MHz").arg(ccHz / 1e6, 0, 'f', 5));
                // Per-rx publish + disable paths below drain the live voice path when decoding stops.
            } else if (p25Status) {
                p25Status->setText("Auto follow idle");
            }
            const auto retDur = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - retStart).count();
            if (retDur > 250) {
                appendP25LogLine(QString("P25 return-to-control completed slowly in %1 ms; UI stayed off the DSP lock path.")
                    .arg(retDur));
            }
        };

        auto armP25VoiceFollowState = [this](const P25TalkgroupEntry& tg) -> bool {
            P25TalkgroupEntry armTg = tg;
            const qint64 armPrepareMs = QDateTime::currentMSecsSinceEpoch();
            const auto registrySnapshot = loadP25Talkgroups();
            const bool refreshedGrantMetadata =
                p25RefreshFollowGrantFromRegistry(armTg, registrySnapshot, armPrepareMs);
            p25AugmentTalkgroupFromKnownSite(armTg, registrySnapshot,
                armTg.controlFreqHz > 0.0 ? armTg.controlFreqHz : p25MonitoredControlFreqHz);
            bool phase2VoiceLog = false;
            double frozenAfcHzLog = 0.0;
            bool trafficOffsetSeededLog = false;
            double trafficOffsetHzLog = 0.0;
            bool clearAudio = false;
            {
                // A follow grant is a hard real-time transition: once the control channel
                // grants a traffic channel, publish the traffic decoder immediately.
                // Match sdrtrunk's semantics: create/commit the traffic channel synchronously,
                // and own dspMutex before decoder replacement. The heavy
                // Phase-2 voice decode path is now a bounded worker, so this handoff no
                // longer waits behind GUI-inline TDMA/AMBE processing.
                std::unique_lock<std::mutex> lk(receiversMutex, std::try_to_lock);
                if (!lk.owns_lock()) {
                    appendP25LogLineKeyed("p25-arm-receivers-busy", "P25 voice arm deferred (receivers busy); will retry shortly. GUI did not block.", 500);
                    return false;
                }
                ensureReceiver();
                if (receivers.empty() || !receivers[0]) return true;
                auto& rx = *receivers[0];
                std::unique_lock<std::mutex> rxLock(rx.stateMutex, std::try_to_lock);
                if (!rxLock.owns_lock()) {
                    appendP25LogLineKeyed("p25-arm-state-busy", "P25 voice arm deferred (rx state busy); will retry shortly. GUI did not block.", 500);
                    return false;
                }
                const bool phase2Voice = p25TalkgroupIsPhase2(armTg);
                const uint64_t trafficGeneration = phase2Voice
                    ? (p25TrafficSourceGeneration.fetch_add(1, std::memory_order_acq_rel) + 1)
                    : 0;
                double frozenAfcHz = rx.afcLocked ? rx.afcOffsetHz : gLastAfcOffsetHz.load(std::memory_order_relaxed);
                if (!std::isfinite(frozenAfcHz) || std::abs(frozenAfcHz) > 45000.0) frozenAfcHz = 0.0;
                const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
                double trustedControlOffsetHz = 0.0;
                const bool seedPhase2TrafficOffset =
                    phase2Voice &&
                    p25TrustedControlOffsetForPhase2Traffic(
                        p25MonitoredControlFreqHz > 0.0 ? p25MonitoredControlFreqHz : armTg.controlFreqHz,
                        nowMs,
                        &trustedControlOffsetHz);

                // Publish the critical voice metadata under state lock so the worker
                // can see the new grant immediately.  The arm does not report success
                // until the decoder reset is committed under dspMutex; otherwise the
                // retry loop keeps trying instead of running a stale decoder on a new
                // TG/slot and producing short, blocky bursts.
                rx.p25TrafficRetunesPrimary = phase2Voice;
                rx.p25TrafficGeneration = trafficGeneration;
                rx.p25TrafficControlFreqHz = p25MonitoredControlFreqHz;
                rx.p25TrafficSourceCenterFreqHz = armTg.lastVoiceFreqHz;
                rx.p25TrafficVoiceFreqHz = armTg.lastVoiceFreqHz;
                rx.p25TrafficSlot = armTg.tdmaSlotKnown ? static_cast<uint8_t>(armTg.tdmaSlot & 0x01u) : 0;
                rx.p25TrafficLastGrantMs = nowMs;
                rx.p25Phase2TrafficTargetOffsetKnown = seedPhase2TrafficOffset;
                rx.p25Phase2TrafficTargetOffsetHz = seedPhase2TrafficOffset ? trustedControlOffsetHz : 0.0;
                rx.p25Phase2TrafficTargetOffsetTrust = seedPhase2TrafficOffset ? 1 : 0;
                rx.p25Phase2TrafficTargetOffsetMisses = 0;
                rx.freqHz = armTg.lastVoiceFreqHz;
                rx.mode = DemodMode::NFM;
                rx.channelBwHz = 12500.0;
                rx.lpfHz = 3000.0;
                rx.audioLpfEnabled = false;
                rx.active = true;

                // resetP25VoiceState() intentionally clears... apply grant metadata.
                // Set these BEFORE optional dsp reset so the enabled flag is visible
                // immediately to guiDspWorker / follow status / stillCurrent checks.
                rx.p25VoiceDecodeEnabled = true;
                rx.p25VoiceClearKnown = p25TalkgroupGrantProvesSpeakerClear(armTg);
                rx.p25VoiceEncrypted = p25TalkgroupGrantProvesSpeakerEncrypted(armTg);
                // Phase 2: do not promote sticky TG clear into speaker-clear on arm
                // without a current-grant service option proof.  OP=0x02 updates force
                // encryptionKnown=false before arm so audio waits for traffic PTT/ESS.
                // Phase 1 may still inherit sticky clear for follow continuity.
                if (!rx.p25VoiceClearKnown && !rx.p25VoiceEncrypted &&
                    armTg.talkgroupId != 0 && armTg.encryptionKnown && !armTg.encrypted &&
                    !phase2Voice) {
                    rx.p25VoiceClearKnown = true;
                }
                rx.p25VoiceTalkgroupId = armTg.talkgroupId;
                rx.p25VoiceSourceId = armTg.lastSourceId;
                const qint64 armNowMs = QDateTime::currentMSecsSinceEpoch();
                p25Phase2BeginNewPtt(rx, armNowMs);
                rx.p25VoicePhase2 = phase2Voice;
                rx.p25VoiceTdmaSlotKnown = armTg.tdmaSlotKnown;
                rx.p25VoiceTdmaSlot = armTg.tdmaSlot;
                if (armTg.tdmaSlotKnown) {
                    p25Phase2MarkGrantedSlotImmutable(rx);
                }
                rx.p25VoiceSlotProbePending = false;
                rx.p25VoiceSlotProbeRequested = 0;
                rx.p25VoiceMaskParamsKnown = armTg.p25MaskParamsKnown;
                rx.p25VoiceNac = armTg.nac;
                rx.p25VoiceWacn = armTg.wacn;
                rx.p25VoiceSystemId = armTg.systemId;
                rx.p25VoiceSettleUntilMs = armNowMs + p25PostArmSettleMs(rx.p25VoicePhase2);
                rx.p25VoiceDiscardWindows = p25PostArmDiscardWindows(rx.p25VoicePhase2);
                rx.p25ControlChannelMute = false;
                rx.p25Phase2AllowLateEntryAudioProbe =
                    phase2Voice && guiRuntimeConfig.p25LateEntryAudioProbe;
                rx.p25AfcFrozen = !phase2Voice;
                rx.p25FrozenAfcOffsetHz = phase2Voice
                    ? (seedPhase2TrafficOffset ? trustedControlOffsetHz : 0.0)
                    : frozenAfcHz;

                std::unique_lock<std::recursive_mutex> dspLock(rx.dspMutex, std::try_to_lock);
                if (dspLock.owns_lock()) {
                    const bool armedVoiceDecodeEnabled = rx.p25VoiceDecodeEnabled;
                    const bool armedVoiceClearKnown = rx.p25VoiceClearKnown;
                    const bool armedVoiceEncrypted = rx.p25VoiceEncrypted;
                    const uint32_t armedTalkgroupId = rx.p25VoiceTalkgroupId;
                    const uint32_t armedSourceId = rx.p25VoiceSourceId;
                    const int64_t armedGrantEpochMs = rx.p25VoiceGrantEpochMs;
                    const uint64_t armedCallSessionId = rx.p25CurrentCallSessionId;
                    const uint64_t armedPttGeneration = rx.p25PttGeneration;
                    const bool armedPhase2 = rx.p25VoicePhase2;
                    const bool armedSlotKnown = rx.p25VoiceTdmaSlotKnown;
                    const uint8_t armedSlot = rx.p25VoiceTdmaSlot;
                    const bool armedGrantedSlotImmutable = rx.p25Phase2GrantedSlotImmutable;
                    const bool armedMaskKnown = rx.p25VoiceMaskParamsKnown;
                    const uint16_t armedNac = rx.p25VoiceNac;
                    const uint32_t armedWacn = rx.p25VoiceWacn;
                    const uint16_t armedSystemId = rx.p25VoiceSystemId;
                    const int64_t armedSettleUntilMs = rx.p25VoiceSettleUntilMs;
                    const int armedDiscardWindows = rx.p25VoiceDiscardWindows;
                    const bool armedLateEntryProbe = rx.p25Phase2AllowLateEntryAudioProbe;
                    const bool armedAfcFrozen = rx.p25AfcFrozen;
                    const double armedFrozenAfcOffsetHz = rx.p25FrozenAfcOffsetHz;
                    const bool armedIndependentTrafficSource = rx.p25IndependentTrafficSource;
                    const bool armedTrafficRetunesPrimary = rx.p25TrafficRetunesPrimary;
                    const uint64_t armedTrafficGeneration = rx.p25TrafficGeneration;
                    const double armedTrafficControlFreqHz = rx.p25TrafficControlFreqHz;
                    const double armedTrafficSourceCenterFreqHz = rx.p25TrafficSourceCenterFreqHz;
                    const double armedTrafficVoiceFreqHz = rx.p25TrafficVoiceFreqHz;
                    const uint8_t armedTrafficSlot = rx.p25TrafficSlot;
                    const qint64 armedTrafficLastGrantMs = rx.p25TrafficLastGrantMs;
                    const bool armedTargetOffsetKnown = rx.p25Phase2TrafficTargetOffsetKnown;
                    const double armedTargetOffsetHz = rx.p25Phase2TrafficTargetOffsetHz;
                    const int armedTargetOffsetTrust = rx.p25Phase2TrafficTargetOffsetTrust;
                    const int armedTargetOffsetMisses = rx.p25Phase2TrafficTargetOffsetMisses;
                    rx.resetDemodState();
                    p25ClearPhase2PendingAudio(rx);
                    rx.resetP25VoiceState();
                    clearP25SessionScopedState(rx);
                    rx.p25VoiceDecodeEnabled = armedVoiceDecodeEnabled;
                    rx.p25VoiceClearKnown = armedVoiceClearKnown;
                    rx.p25VoiceEncrypted = armedVoiceEncrypted;
                    rx.p25VoiceTalkgroupId = armedTalkgroupId;
                    rx.p25VoiceSourceId = armedSourceId;
                    rx.p25VoiceGrantEpochMs = armedGrantEpochMs;
                    rx.p25PttGeneration = armedPttGeneration;
                    rx.p25CurrentCallSessionId = armedCallSessionId != 0
                        ? armedCallSessionId
                        : p25MakeCurrentCallSessionId(rx.p25VoiceTalkgroupId, rx.p25PttGeneration);
                    rx.p25VoicePhase2 = armedPhase2;
                    rx.p25VoiceTdmaSlotKnown = armedSlotKnown;
                    rx.p25VoiceTdmaSlot = armedSlot;
                    rx.p25Phase2GrantedSlotImmutable = armedGrantedSlotImmutable;
                    rx.p25VoiceMaskParamsKnown = armedMaskKnown;
                    rx.p25VoiceNac = armedNac;
                    rx.p25VoiceWacn = armedWacn;
                    rx.p25VoiceSystemId = armedSystemId;
                    rx.p25VoiceSettleUntilMs = armedSettleUntilMs;
                    rx.p25VoiceDiscardWindows = armedDiscardWindows;
                    rx.p25Phase2AllowLateEntryAudioProbe = armedLateEntryProbe;
                    rx.p25AfcFrozen = armedAfcFrozen;
                    rx.p25FrozenAfcOffsetHz = armedFrozenAfcOffsetHz;
                    rx.p25IndependentTrafficSource = armedIndependentTrafficSource;
                    rx.p25TrafficRetunesPrimary = armedTrafficRetunesPrimary;
                    rx.p25TrafficGeneration = armedTrafficGeneration;
                    rx.p25TrafficControlFreqHz = armedTrafficControlFreqHz;
                    rx.p25TrafficSourceCenterFreqHz = armedTrafficSourceCenterFreqHz;
                    rx.p25TrafficVoiceFreqHz = armedTrafficVoiceFreqHz;
                    rx.p25TrafficSlot = armedTrafficSlot;
                    rx.p25TrafficLastGrantMs = armedTrafficLastGrantMs;
                    rx.p25Phase2TrafficTargetOffsetKnown = armedTargetOffsetKnown;
                    rx.p25Phase2TrafficTargetOffsetHz = armedTargetOffsetHz;
                    rx.p25Phase2TrafficTargetOffsetTrust = armedTargetOffsetTrust;
                    rx.p25Phase2TrafficTargetOffsetMisses = armedTargetOffsetMisses;
                    rx.p25VoiceResetPending = false;
                    rx.p25VoiceLiveDecoder = P25LiveDecoder(p25VoiceDecoderConfigForReceiver(rx));
                    if (rx.p25VoicePhase2) {
                        DeviceManager::instance().setReceiverCursorBeforeLiveEdge(
                            rx.deviceIndex, rx, p25Phase2TrafficPreRollSamples(0.0, true));
                    } else {
                        DeviceManager::instance().setReceiverCursorToLiveEdge(rx.deviceIndex, rx);
                    }
                    if (rx.p25VoicePhase2 && rx.p25VoiceMaskParamsKnown) {
                        rx.p25VoiceLiveDecoder.setPhase2MaskParameters(rx.p25VoiceNac, rx.p25VoiceWacn, rx.p25VoiceSystemId);
                    } else {
                        rx.p25VoiceLiveDecoder.clearPhase2MaskParameters();
                    }
                    clearAudio = true;
                } else {
                    // Defer full reset; set pending so the worker or next arm retry
                    // applies it when free.  Return false below so the GUI follow
                    // state is not declared armed until the decoder is actually fresh.
                    rx.p25VoiceResetPending = true;
                }

                phase2VoiceLog = phase2Voice;
                frozenAfcHzLog = frozenAfcHz;
                trafficOffsetSeededLog = seedPhase2TrafficOffset;
                trafficOffsetHzLog = seedPhase2TrafficOffset ? trustedControlOffsetHz : 0.0;
            }
            if (!clearAudio) {
                return false;
            }
            if (clearAudio) {
                AudioEngine* activeEngine = ensureAudioOutputActive("P25 voice follow");
                QTimer::singleShot(0, this, [this]() {
                    if (engineForAudio) engineForAudio->clearBuffers();
                });
                (void)activeEngine;
            }
            if (refreshedGrantMetadata) {
                appendP25LogLine(QString("P25 voice arm refreshed same-call grant metadata: TG=%1 voice=%2MHz clear=%3 encrypted=%4 slot=%5 source=%6 mask=%7.")
                    .arg(armTg.talkgroupId)
                    .arg(armTg.lastVoiceFreqHz / 1e6, 0, 'f', 5)
                    .arg(armTg.encryptionKnown && !armTg.encrypted ? "yes" : "no")
                    .arg(armTg.encryptionKnown && armTg.encrypted ? "yes" : "no")
                    .arg(armTg.tdmaSlotKnown ? QString::number(armTg.tdmaSlot & 0x01u) : QString("unknown"))
                    .arg(armTg.lastSourceId != 0 ? QString::number(armTg.lastSourceId) : QString("unknown"))
                    .arg(armTg.p25MaskParamsKnown ? "known" : "unknown"));
            }
            if (phase2VoiceLog) {
                if (trafficOffsetSeededLog) {
                    appendP25LogLine(QString("AFC carry: legacy Phase 2 traffic seeded from trusted control target offset=%1kHz as an unverified first-eye hint; traffic TDMA/MAC evidence must promote it.")
                        .arg(trafficOffsetHzLog / 1000.0, 0, 'f', 3));
                } else if (std::isfinite(frozenAfcHzLog) &&
                    std::abs(frozenAfcHzLog) >= 50.0 &&
                    std::abs(frozenAfcHzLog) <= 45000.0) {
                    appendP25LogLine(QString("AFC carry: legacy Phase 2 retune saw control AFC offset=%1kHz; starting on the granted voice center and requiring TDMA MAC/mask/voice evidence before any traffic offset is locked.")
                        .arg(frozenAfcHzLog / 1000.0, 0, 'f', 3));
                } else {
                    appendP25LogLine(QString("AFC carry: legacy Phase 2 retune has no reliable control AFC seed; starting on granted voice center with bounded target-offset recovery."));
                }
            }
            return true;
        };

        auto scheduleP25VoiceFollowArm = [this, p25Status, armP25VoiceFollowState, returnP25AutoFollowToControl](P25TalkgroupEntry tg, const char* reason) {
            const quint32 expectedTg = tg.talkgroupId;
            const double expectedVoiceHz = tg.lastVoiceFreqHz;
            const QString reasonText = reason ? QString::fromUtf8(reason) : QString("follow");
            // Phase 2 voice has to begin decoding immediately after the grant.
            // sdrtrunk starts a traffic-channel processing chain right away and
            // lets the audio module queue/mute until PTT/ESS says clear.  Do the
            // same in this single-receiver path: arm the decoder immediately for
            // Phase 2 and use p25VoiceSettleUntilMs only to mute speaker output,
            // not to delay feeding IQ into the TDMA/AMBE decoder.
            auto armAttempt = std::make_shared<std::function<void(int)>>();
            std::weak_ptr<std::function<void(int)>> weakArmAttempt = armAttempt;
            *armAttempt = [this, p25Status, armP25VoiceFollowState, returnP25AutoFollowToControl, tg, expectedTg, expectedVoiceHz, reasonText, weakArmAttempt](int attempt) mutable {
                try {
                    if (!p25FollowEnabled || p25FollowTalkgroupId != expectedTg) return;
                    if (std::abs(p25AutoFollowVoiceFreqHz - expectedVoiceHz) > 50.0) return;
                    if (armP25VoiceFollowState(tg)) {
                        const bool phase2 = p25TalkgroupIsPhase2(tg);
                        appendP25LogLine(QString("P25 voice decode armed: TG=%1 voice=%2MHz reason=%3 postArmSettleMute=%4ms discardWindows=%5.")
                            .arg(expectedTg)
                            .arg(expectedVoiceHz / 1e6, 0, 'f', 5)
                            .arg(reasonText)
                            .arg(p25PostArmSettleMs(phase2))
                            .arg(p25PostArmDiscardWindows(phase2)));
                        if (p25Status) p25Status->setText(QString("Follow armed TG %1").arg(expectedTg));
                        return;
                    }
                    if (attempt == 0) {
                        appendP25LogLineKeyed("p25-voice-arm-deferred",
                            "P25 voice arm retry: traffic decoder was not committed on this attempt; receiver/state/DSP locks are busy; GUI did not block and the arm will retry.",
                            2500);
                    }
                    if (attempt >= 600) {
                        appendP25LogLine(QString("P25 voice arm timed out for TG %1 after extended retry window; returning to control channel to avoid stale voice state.")
                            .arg(expectedTg));
                        if (p25Status) p25Status->setText(QString("TG %1 arm timeout").arg(expectedTg));
                        returnP25AutoFollowToControl();
                        return;
                    }
                    if (auto retry = weakArmAttempt.lock()) {
                        QTimer::singleShot(25, this, [retry, attempt]() {
                            (*retry)(attempt + 1);
                        });
                    }
                } catch (const std::exception& ex) {
                    appendP25LogLine(QString("P25 voice arm exception for TG %1: %2; returning to control channel.")
                        .arg(expectedTg)
                        .arg(ex.what()));
                    returnP25AutoFollowToControl();
                } catch (...) {
                    appendP25LogLine(QString("P25 voice arm unknown exception for TG %1; returning to control channel.").arg(expectedTg));
                    returnP25AutoFollowToControl();
                }
            };
            const int armDelayMs = p25TalkgroupIsPhase2(tg) ? kP25Phase2ArmDelayMs : kP25Phase1ArmDelayMs;
            QTimer::singleShot(armDelayMs, this, [armAttempt]() {
                (*armAttempt)(0);
            });
        };

        auto p25TrafficInCurrentSamplePassband = [](double trafficHz, double centerHz, double sampleRateHz) noexcept -> bool {
            if (!std::isfinite(trafficHz) || trafficHz <= 0.0 ||
                !std::isfinite(centerHz) || centerHz <= 0.0 ||
                !std::isfinite(sampleRateHz) || sampleRateHz <= 0.0) {
                return false;
            }
            // Keep a conservative guard band for tuner/LPF transition regions.  sdrtrunk
            // asks the source manager for a channel source and only moves/tunes when the
            // requested traffic channel cannot be sourced from the current wideband stream.
            return std::abs(trafficHz - centerHz) <= sampleRateHz * 0.42;
        };
        // DEC-0015: SDRTrunk TunerController.isTunedFor, not the 250 kHz /
        // 0.25*Nyquist clamp. Capture 005246 (750 kHz from a CC-at-DC tuner)
        // was that geometry; the calculator moves the lower channel off DC.
        auto p25Phase2TrafficInQualityPassband = [](double trafficHz, double centerHz, double sampleRateHz) noexcept -> bool {
            return p25SdrtrunkTunerIsTunedFor(centerHz, sampleRateHz, trafficHz);
        };

        auto prepareP25InBandVoiceTarget = [this, monFreq, modeBox](double voiceHz) -> bool {
            if (!std::isfinite(voiceHz) || voiceHz <= 0.0) return false;
            if (monFreq) monFreq->setValue(voiceHz / 1e6);
            if (modeBox) {
                modeBox->blockSignals(true);
                modeBox->setCurrentText("NFM");
                modeBox->blockSignals(false);
            }
            if (bwSpin) {
                bwSpin->blockSignals(true);
                bwSpin->setValue(12.5);
                bwSpin->blockSignals(false);
            }
            if (lpfSpin) {
                lpfSpin->blockSignals(true);
                lpfSpin->setValue(3.0);
                lpfSpin->blockSignals(false);
            }
            if (lpfEnableCheck) {
                lpfEnableCheck->blockSignals(true);
                lpfEnableCheck->setChecked(false);
                lpfEnableCheck->blockSignals(false);
                if (lpfSpin) lpfSpin->setEnabled(false);
            }
            // Commit the visible voice target without blocking the Qt thread.  If
            // DSP owns stateMutex mid Phase-2 decode, return false and let the
            // grant path retry rather than freezing the UI.
            std::unique_lock<std::mutex> lk(receiversMutex, std::try_to_lock);
            if (!lk.owns_lock()) return false;
            ensureReceiver();
            if (receivers.empty() || !receivers[0]) return true;
            auto& rx = *receivers[0];
            std::unique_lock<std::mutex> rxLock(rx.stateMutex, std::try_to_lock);
            if (!rxLock.owns_lock()) return false;
            rx.freqHz = voiceHz;
            rx.mode = DemodMode::NFM;
            rx.channelBwHz = 12500.0;
            rx.lpfHz = 3000.0;
            rx.audioLpfEnabled = false;
            rx.active = true;
            rx.p25ControlChannelMute = true;
            rx.p25VoiceDecodeEnabled = false;
            rx.p25VoiceSettleUntilMs = QDateTime::currentMSecsSinceEpoch() + kP25RetunePreArmMuteMs;
            rx.p25VoiceDiscardWindows = kP25RetunePreArmDiscardWindows;
            std::unique_lock<std::recursive_mutex> dspLock(rx.dspMutex, std::try_to_lock);
            if (dspLock.owns_lock()) {
                rx.resetDemodState();
                p25ClearPhase2PendingAudio(rx);
                rx.resetP25VoiceState();
                clearP25SessionScopedState(rx);
                rx.p25VoiceResetPending = false;
            } else {
                rx.lastConsumedAbsolute.store(0, std::memory_order_release);
                rx.afcLocked = false;
                rx.afcOffsetHz = 0.0;
                rx.p25AfcFrozen = false;
                rx.p25FrozenAfcOffsetHz = 0.0;
                rx.p25VoiceResetPending = true;
            }
            clearP25VoiceDiagnostics(rx);
            return true;
        };

        struct P25TrafficSourceSelection {
            bool valid = false;
            size_t deviceIndex = 0;
            double centerHz = 0.0;
            double sampleRateHz = 0.0;
            bool retunesPrimary = false;
            QString sourceKind;
        };

        auto selectP25IndependentTrafficSource = [this, p25TrafficInCurrentSamplePassband](double voiceHz, double ccHz, bool phase2Traffic) -> P25TrafficSourceSelection {
            P25TrafficSourceSelection out;
            if (!std::isfinite(voiceHz) || voiceHz <= 0.0) return out;

            auto& mgr = DeviceManager::instance();
            const auto devices = mgr.getDevices();

            // First preference: keep CC on this tuner only when the
            // voice-parked LO (DEC-0016 single-channel) still sources CC.
            // Do not use two-channel getCenterFrequency for the follow LO
            // (115315: 421.975 ended at offset=997.3 kHz, CADENCE drop=A).
            for (size_t i = 0; i < devices.size(); ++i) {
                if (!mgr.isStreaming(i)) continue;
                std::vector<float> pwr;
                double cf = 0.0;
                double sr = 0.0;
                if (!mgr.getLatestSpectrum(i, pwr, cf, sr) || sr <= 0.0) {
                    sr = devices[i].sampleRate > 0.0 ? devices[i].sampleRate : 2.048e6;
                    cf = 0.0;
                }
                if (phase2Traffic) {
                    const double desiredHz = p25Phase2LowIfTrafficCenterHz(voiceHz, sr);
                    if (i == 0) {
                        const bool ccFitsOnVoicePark =
                            !(std::isfinite(ccHz) && ccHz > 0.0) ||
                            p25SdrtrunkTunerIsTunedFor(desiredHz, sr, voiceHz, ccHz);
                        if (!ccFitsOnVoicePark) continue;
                        const bool alreadyTuned =
                            cf > 0.0 && std::abs(cf - desiredHz) <= 50.0;
                        const bool centeredOnVoice = std::abs(cf - voiceHz) <= 50.0;
                        const bool phase2LowIfFriendly =
                            !centeredOnVoice || std::abs(cf - ccHz) <= 50.0;
                        out.valid = true;
                        out.deviceIndex = i;
                        out.centerHz = alreadyTuned ? cf : desiredHz;
                        out.sampleRateHz = sr;
                        out.retunesPrimary = false;
                        if (!alreadyTuned) {
                            out.sourceKind = QStringLiteral("same-wideband-control-source-sdrtrunk-center");
                        } else if (centeredOnVoice && !phase2LowIfFriendly) {
                            out.sourceKind = QStringLiteral("reuse-centered-traffic-source");
                        } else if (std::abs(cf - ccHz) <= 50.0) {
                            out.sourceKind = QStringLiteral("same-wideband-control-source-low-if");
                        } else {
                            out.sourceKind = QStringLiteral("existing-wideband-source-low-if");
                        }
                        return out;
                    }
                    if (!(cf > 0.0 && p25SdrtrunkTunerIsTunedFor(cf, sr, voiceHz))) continue;
                    out.valid = true;
                    out.deviceIndex = i;
                    out.centerHz = cf;
                    out.sampleRateHz = sr;
                    out.retunesPrimary = false;
                    out.sourceKind = QStringLiteral("existing-wideband-source-low-if");
                    return out;
                }
                if (!(cf > 0.0 && p25TrafficInCurrentSamplePassband(voiceHz, cf, sr))) continue;
                out.valid = true;
                out.deviceIndex = i;
                out.centerHz = cf;
                out.sampleRateHz = sr;
                out.retunesPrimary = false;
                out.sourceKind = (std::abs(cf - ccHz) <= 50.0)
                    ? QStringLiteral("same-wideband-control-source-low-if")
                    : QStringLiteral("existing-wideband-source-low-if");
                return out;
            }

            // Second preference: allocate/retune a different physical SDR if one
            // exists.  This gives true simultaneous control+traffic monitoring.
            for (size_t i = 1; i < devices.size(); ++i) {
                try {
                    const double sr = devices[i].sampleRate > 0.0 ? devices[i].sampleRate : 2.048e6;
                    const double trafficCenterHz = phase2Traffic
                        ? p25Phase2LowIfTrafficCenterHz(voiceHz, sr)
                        : voiceHz;
                    mgr.setEnabled(i, true);
                    mgr.setCenterFreq(i, trafficCenterHz);
                    if (!mgr.isStreaming(i)) mgr.startStreaming(i, true);
                    out.valid = true;
                    out.deviceIndex = i;
                    out.centerHz = trafficCenterHz;
                    out.sampleRateHz = sr;
                    out.sourceKind = phase2Traffic
                        ? QStringLiteral("dedicated-sdr-traffic-source-low-if")
                        : QStringLiteral("dedicated-sdr-traffic-source");
                    return out;
                } catch (const std::exception& ex) {
                    appendP25LogLineKeyed(QString("p25-traffic-source-dev%1-start-failed").arg(i),
                        QString("P25 traffic source could not start SDR device %1 for %2MHz: %3")
                            .arg(static_cast<qulonglong>(i))
                            .arg(voiceHz / 1e6, 0, 'f', 5)
                            .arg(ex.what()),
                        5000);
                } catch (...) {
                    appendP25LogLineKeyed(QString("p25-traffic-source-dev%1-start-failed").arg(i),
                        QString("P25 traffic source could not start SDR device %1 for %2MHz: unknown error")
                            .arg(static_cast<qulonglong>(i))
                            .arg(voiceHz / 1e6, 0, 'f', 5),
                        5000);
                }
            }

            // Third preference: sdrtrunk-style one-tuner trunk tracking.  A
            // TunerChannelSource is still a traffic source even when it is backed
            // by the same physical RTL-SDR; the trade-off is that control-channel
            // monitoring is paused while the single tuner is parked on traffic.
            // This is the mode that lets sdrtrunk work well with one RTL-SDR when
            // the voice channel is outside the current sampled passband.
            if (!devices.empty()) {
                const double sr = devices[0].sampleRate > 0.0 ? devices[0].sampleRate : 2.048e6;
                out.valid = true;
                out.deviceIndex = 0;
                out.centerHz = phase2Traffic
                    ? p25Phase2LowIfTrafficCenterHz(voiceHz, sr)
                    : voiceHz;
                out.sampleRateHz = sr;
                out.retunesPrimary = true;
                out.sourceKind = phase2Traffic
                    ? QStringLiteral("single-rtl-retune-traffic-source-low-if")
                    : QStringLiteral("single-rtl-retune-traffic-source");
                return out;
            }

            return out;
        };

        auto startP25IndependentTrafficSource = [this, p25Status](const P25TrafficSourceSelection& source,
                                                                  const P25TalkgroupEntry& tg,
                                                                  double ccHz,
                                                                  qint64 nowMs) -> bool {
            if (!source.valid || tg.lastVoiceFreqHz <= 0.0) return false;

            const bool phase2Traffic = p25TalkgroupIsPhase2(tg);
            double inheritedControlAfcHz = 0.0;
            bool inheritedControlAfcKnown = false;
            double inheritedControlTargetOffsetHz = 0.0;
            bool inheritedControlTargetOffsetKnown = false;
            if (phase2Traffic && (source.retunesPrimary || source.deviceIndex == 0)) {
                if (p25TrustedControlOffsetForPhase2Traffic(ccHz, nowMs, &inheritedControlTargetOffsetHz)) {
                    inheritedControlTargetOffsetKnown = true;
                }
            }
            if (source.retunesPrimary) {
                auto acceptControlAfc = [&](double candidateHz) {
                    if (!std::isfinite(candidateHz)) return;
                    if (std::abs(candidateHz) > 45000.0) return;
                    inheritedControlAfcHz = candidateHz;
                    inheritedControlAfcKnown = true;
                };
                {
                    std::unique_lock<std::mutex> lk(receiversMutex, std::try_to_lock);
                    if (lk.owns_lock() && !receivers.empty() && receivers[0]) {
                        std::unique_lock<std::mutex> primaryLock(receivers[0]->stateMutex, std::try_to_lock);
                        if (primaryLock.owns_lock() && receivers[0]->afcLocked) {
                            acceptControlAfc(receivers[0]->afcOffsetHz);
                        }
                    }
                }
                if (!inheritedControlAfcKnown) {
                    acceptControlAfc(gLastAfcOffsetHz.load(std::memory_order_relaxed));
                }
            }

            uint64_t primaryRetuneSeq = 0;
            if (source.retunesPrimary) {
                try {
                    auto& mgr = DeviceManager::instance();
                    mgr.setEnabled(source.deviceIndex, true);
                    double currentCenterHz = source.centerHz;
                    std::vector<float> pwr;
                    double cf = 0.0;
                    double sr = 0.0;
                    if (mgr.getLatestSpectrum(source.deviceIndex, pwr, cf, sr) && cf > 0.0) {
                        currentCenterHz = cf;
                    }
                    const double desiredCenterHz = source.centerHz > 0.0
                        ? source.centerHz
                        : tg.lastVoiceFreqHz;
                    const bool alreadyCenteredOnTrafficSource =
                        std::isfinite(currentCenterHz) &&
                        std::abs(currentCenterHz - desiredCenterHz) <= 50.0;
                    if (!alreadyCenteredOnTrafficSource) {
                        primaryRetuneSeq = mgr.setCenterFreq(source.deviceIndex, desiredCenterHz);
                        appendP25LogLine(QString("P25 one-RTL traffic source retuned primary tuner low-IF: rfCenter=%1MHz voice=%2MHz offset=%3kHz control=%4MHz. Control-channel decode is paused until call teardown/return, matching sdrtrunk single-tuner trunking semantics.")
                            .arg(desiredCenterHz / 1e6, 0, 'f', 5)
                            .arg(tg.lastVoiceFreqHz / 1e6, 0, 'f', 5)
                            .arg((tg.lastVoiceFreqHz - desiredCenterHz) / 1000.0, 0, 'f', 1)
                            .arg(ccHz / 1e6, 0, 'f', 5));
                    } else {
                        appendP25LogLine(QString("P25 one-RTL traffic source already at low-IF center=%1MHz for voice=%2MHz; reusing tuner without physical retune (control=%3MHz).")
                            .arg(desiredCenterHz / 1e6, 0, 'f', 5)
                            .arg(tg.lastVoiceFreqHz / 1e6, 0, 'f', 5)
                            .arg(ccHz / 1e6, 0, 'f', 5));
                    }
                    if (!mgr.isStreaming(source.deviceIndex)) mgr.startStreaming(source.deviceIndex, true);
                } catch (const std::exception& ex) {
                    appendP25LogLine(QString("P25 one-RTL traffic source failed to retune primary tuner to %1MHz: %2")
                        .arg(tg.lastVoiceFreqHz / 1e6, 0, 'f', 5)
                        .arg(ex.what()));
                    return false;
                } catch (...) {
                    appendP25LogLine(QString("P25 one-RTL traffic source failed to retune primary tuner to %1MHz: unknown error")
                        .arg(tg.lastVoiceFreqHz / 1e6, 0, 'f', 5));
                    return false;
                }
            } else if (phase2Traffic &&
                       source.centerHz > 0.0 &&
                       std::isfinite(source.centerHz)) {
                // DEC-0015 / HeterodyneChannelSourceManager.updateTunerFrequency:
                // CC stays sourced; only move the LO if the current center is
                // not already isTunedFor {cc, voice}.
                try {
                    auto& mgr = DeviceManager::instance();
                    double currentCenterHz = 0.0;
                    std::vector<float> pwr;
                    double cf = 0.0;
                    double sr = 0.0;
                    if (mgr.getLatestSpectrum(source.deviceIndex, pwr, cf, sr) && cf > 0.0) {
                        currentCenterHz = cf;
                    }
                    if (!std::isfinite(currentCenterHz) ||
                        std::abs(currentCenterHz - source.centerHz) > 50.0) {
                        mgr.setCenterFreq(source.deviceIndex, source.centerHz);
                        appendP25LogLine(QString("P25 tuner center aligned (SDRTrunk CenterFrequencyCalculator): rfCenter=%1MHz voice=%2MHz offset=%3kHz control=%4MHz.")
                            .arg(source.centerHz / 1e6, 0, 'f', 5)
                            .arg(tg.lastVoiceFreqHz / 1e6, 0, 'f', 5)
                            .arg((tg.lastVoiceFreqHz - source.centerHz) / 1000.0, 0, 'f', 1)
                            .arg(ccHz / 1e6, 0, 'f', 5));
                    }
                } catch (const std::exception& ex) {
                    appendP25LogLine(QString("P25 tuner center align failed for voice=%1MHz: %2")
                        .arg(tg.lastVoiceFreqHz / 1e6, 0, 'f', 5)
                        .arg(ex.what()));
                } catch (...) {
                    appendP25LogLine(QString("P25 tuner center align failed for voice=%1MHz: unknown error")
                        .arg(tg.lastVoiceFreqHz / 1e6, 0, 'f', 5));
                }
            }

            std::shared_ptr<Receiver> trafficRx;
            {
                // Never block the Qt GUI thread on receivers/state locks. DSP and
                // the voice worker can hold stateMutex across IQ/decode windows;
                // a blocking lock here freezes the UI (Windows: Not Responding).
                std::unique_lock<std::mutex> lk(receiversMutex, std::try_to_lock);
                if (!lk.owns_lock()) {
                    appendP25LogLineKeyed("p25-traffic-source-receivers-busy",
                        "P25 traffic source start deferred (receivers busy); GUI did not block.",
                        500);
                    return false;
                }
                ensureReceiver();

                for (auto& existing : receivers) {
                    if (!existing || !existing->p25IndependentTrafficSource) continue;
                    if (existing->deviceIndex != source.deviceIndex) continue;
                    const double existingVoiceHz = existing->p25TrafficVoiceFreqHz > 0.0
                        ? existing->p25TrafficVoiceFreqHz
                        : existing->freqHz;
                    if (!std::isfinite(existingVoiceHz) ||
                        std::abs(existingVoiceHz - tg.lastVoiceFreqHz) > 50.0) {
                        continue;
                    }

                    trafficRx = existing;
                    {
                        std::unique_lock<std::mutex> rxLock(trafficRx->stateMutex, std::try_to_lock);
                        if (!rxLock.owns_lock()) {
                            appendP25LogLineKeyed("p25-traffic-source-state-busy",
                                "P25 traffic source reuse deferred (rx state busy); GUI did not block.",
                                500);
                            return false;
                        }
                        p25CommitPhase2TrafficMetadataFollow(*trafficRx, tg, ccHz, nowMs);
                        trafficRx->active = true;
                        trafficRx->p25TrafficRetunesPrimary = source.retunesPrimary;
                        trafficRx->p25TrafficControlFreqHz = ccHz;
                        trafficRx->p25TrafficSourceCenterFreqHz = source.centerHz > 0.0
                            ? source.centerHz
                            : tg.lastVoiceFreqHz;
                        if (phase2Traffic &&
                            source.retunesPrimary &&
                            inheritedControlTargetOffsetKnown &&
                            (!trafficRx->p25Phase2TrafficTargetOffsetKnown ||
                             trafficRx->p25Phase2TrafficTargetOffsetTrust < kP25Phase2TrafficTargetOffsetVerifiedTrust)) {
                            p25SeedPhase2TrafficOffsetFromControl(*trafficRx, inheritedControlTargetOffsetHz, 1);
                        }
                        if (tg.p25MaskParamsKnown) {
                            trafficRx->p25VoiceMaskParamsKnown = true;
                            trafficRx->p25VoiceNac = tg.nac;
                            trafficRx->p25VoiceWacn = tg.wacn;
                            trafficRx->p25VoiceSystemId = tg.systemId;
                            std::unique_lock<std::recursive_mutex> dspLock(trafficRx->dspMutex, std::try_to_lock);
                            if (dspLock.owns_lock()) {
                                trafficRx->p25VoiceLiveDecoder.setPhase2MaskParameters(
                                    trafficRx->p25VoiceNac, trafficRx->p25VoiceWacn, trafficRx->p25VoiceSystemId);
                            }
                        }
                    }

                    p25IndependentTrafficActive = true;
                    p25IndependentTrafficRetunedPrimary = source.retunesPrimary;
                    p25AutoFollowReturnControlFreqHz = ccHz;
                    p25AutoFollowVoiceFreqHz = tg.lastVoiceFreqHz;
                    p25AutoFollowTunedAtMs = nowMs;
                    p25AutoFollowLastGrantMs = nowMs;
                    p25AutoFollowLastActiveMs = nowMs;
                    p25FollowEnabled = true;
                    p25FollowAutoActive = true;
                    p25FollowTalkgroupId = tg.talkgroupId;
                    p25MonitoredControlFreqHz = ccHz;
                    if (!receivers.empty() && receivers[0]) {
                        std::unique_lock<std::mutex> primaryLock(receivers[0]->stateMutex, std::try_to_lock);
                        if (primaryLock.owns_lock()) {
                            receivers[0]->p25ControlChannelMute = true;
                            receivers[0]->p25VoiceDecodeEnabled = false;
                        }
                    }
                    appendP25LogLine(QString("P25 traffic source reused at same MHz: TG=%1 voice=%2MHz slot=%3 control=%4MHz dev=%5 generation=%6. Rolling decoder/IQ preserved for faster audio.")
                        .arg(tg.talkgroupId)
                        .arg(tg.lastVoiceFreqHz / 1e6, 0, 'f', 5)
                        .arg(tg.tdmaSlotKnown ? QString::number(tg.tdmaSlot & 0x01u) : QString("unknown"))
                        .arg(ccHz / 1e6, 0, 'f', 5)
                        .arg(static_cast<qulonglong>(source.deviceIndex))
                        .arg(static_cast<qulonglong>(trafficRx->p25TrafficGeneration)));
                    return true;
                }

                // One active P25 traffic source for this first implementation.
                // That mirrors a single selected audio path while preserving the
                // control-channel receiver.  Later, this same flag can support a
                // pool of demod traffic sources for DMR/NXDN/etc.
                for (auto& oldTraffic : receivers) {
                    if (!oldTraffic || !oldTraffic->p25IndependentTrafficSource) continue;
                    std::unique_lock<std::mutex> oldLock(oldTraffic->stateMutex, std::try_to_lock);
                    if (!oldLock.owns_lock()) {
                        appendP25LogLineKeyed("p25-traffic-source-old-busy",
                            "P25 traffic source replace deferred (old traffic state busy); GUI did not block.",
                            500);
                        return false;
                    }
                    oldTraffic->active = false;
                    oldTraffic->p25VoiceDecodeEnabled = false;
                    oldTraffic->p25TrafficGeneration = 0;
                }
                receivers.erase(std::remove_if(receivers.begin(), receivers.end(),
                    [](const std::shared_ptr<Receiver>& rx) {
                        return rx && rx->p25IndependentTrafficSource;
                    }), receivers.end());

                const uint64_t trafficGeneration = p25TrafficSourceGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
                p25PendingAudioFlushSeq.fetch_add(1, std::memory_order_release);
                trafficRx = std::make_shared<Receiver>();
                trafficRx->deviceIndex = source.deviceIndex;
                trafficRx->freqHz = tg.lastVoiceFreqHz;
                trafficRx->mode = DemodMode::NFM;
                trafficRx->channelBwHz = 12500.0;
                trafficRx->lpfHz = 3000.0;
                trafficRx->audioLpfEnabled = false;
                trafficRx->squelchDb = monitorSquelchDb;
                trafficRx->rfGainDb = monitorRfGainDb;
                trafficRx->audioGain = monitorGain;
                trafficRx->gain = monitorGain;
                trafficRx->active = true;
                trafficRx->p25IndependentTrafficSource = true;
                trafficRx->p25TrafficRetunesPrimary = source.retunesPrimary;
                trafficRx->p25TrafficGeneration = trafficGeneration;
                trafficRx->p25TrafficControlFreqHz = ccHz;
                trafficRx->p25TrafficSourceCenterFreqHz = source.centerHz > 0.0
                    ? source.centerHz
                    : tg.lastVoiceFreqHz;
                trafficRx->p25TrafficVoiceFreqHz = tg.lastVoiceFreqHz;
                trafficRx->p25TrafficSlot = tg.tdmaSlotKnown ? static_cast<uint8_t>(tg.tdmaSlot & 0x01u) : 0;
                trafficRx->p25TrafficLastGrantMs = nowMs;

                {
                    std::lock_guard<std::mutex> rxLock(trafficRx->stateMutex);
                    trafficRx->resetDemodState();
                    trafficRx->resetP25VoiceState();
                    clearP25SessionScopedState(*trafficRx);
                    trafficRx->p25VoiceDecodeEnabled = true;
                    trafficRx->p25VoiceClearKnown = p25TalkgroupGrantProvesSpeakerClear(tg);
                    trafficRx->p25VoiceEncrypted = p25TalkgroupGrantProvesSpeakerEncrypted(tg);
                    trafficRx->p25VoiceTalkgroupId = tg.talkgroupId;
                    trafficRx->p25VoiceSourceId = tg.lastSourceId;
                    p25Phase2BeginNewPtt(*trafficRx, nowMs);
                    trafficRx->p25VoicePhase2 = phase2Traffic;
                    trafficRx->p25VoiceTdmaSlotKnown = tg.tdmaSlotKnown;
                    trafficRx->p25VoiceTdmaSlot = tg.tdmaSlot;
                    trafficRx->p25VoiceSlotProbePending = false;
                    trafficRx->p25VoiceSlotProbeRequested = 0;
                    trafficRx->p25VoiceMaskParamsKnown = tg.p25MaskParamsKnown;
                    trafficRx->p25VoiceNac = tg.nac;
                    trafficRx->p25VoiceWacn = tg.wacn;
                    trafficRx->p25VoiceSystemId = tg.systemId;
                    // Decode starts immediately. Keep the tiny speaker warmup even
                    // on explicit-clear grants; the output gate may bypass it only
                    // after followed-slot traffic proof or validated queued AMBE.
                    trafficRx->p25VoiceSettleUntilMs = nowMs + p25PostArmSettleMs(true);
                    trafficRx->p25VoiceDiscardWindows = 0;
                    trafficRx->p25ControlChannelMute = false;
                    trafficRx->p25Phase2AllowLateEntryAudioProbe =
                        phase2Traffic && guiRuntimeConfig.p25LateEntryAudioProbe;
                    if (phase2Traffic && source.retunesPrimary) {
                        // Physical one-RTL Phase 2 follow can park the tuner on a
                        // low-IF source center near the granted voice MHz.
                        // Do not freeze control AFC into the traffic channelizer, but
                        // keep the Hz as a soft PPM hint for cold acquire / offset
                        // probe ordering.  Field capture 20260712_014121 showed the
                        // same RTL needing ~0.8–1.25 kHz on CC while one-RTL traffic
                        // stayed at exact grant MHz with p2bursts≈0 until watchdog.
                        // Current behavior: prefer a fresh P25 control target
                        // offset as the first traffic eye, then require
                        // traffic-side evidence before promoting it.
                        trafficRx->p25AfcFrozen = false;
                        trafficRx->p25FrozenAfcOffsetHz = inheritedControlTargetOffsetKnown
                            ? inheritedControlTargetOffsetHz
                            : (inheritedControlAfcKnown ? inheritedControlAfcHz : 0.0);
                        trafficRx->p25Phase2TrafficTargetOffsetKnown = false;
                        trafficRx->p25Phase2TrafficTargetOffsetHz = 0.0;
                        trafficRx->p25Phase2TrafficTargetOffsetTrust = 0;
                        trafficRx->p25Phase2TrafficTargetOffsetMisses = 0;
                        if (inheritedControlTargetOffsetKnown) {
                            p25SeedPhase2TrafficOffsetFromControl(*trafficRx, inheritedControlTargetOffsetHz, 1);
                        }
                        // Seed an *unverified* soft-AFC offset so the first
                        // channelizer pass and probe ordering sit on the same eye
                        // SDRTrunk would already be centred on after PPM correction.
                        if (!trafficRx->p25Phase2TrafficTargetOffsetKnown &&
                            std::isfinite(trafficRx->p25FrozenAfcOffsetHz) &&
                            std::abs(trafficRx->p25FrozenAfcOffsetHz) >= 200.0 &&
                            std::abs(trafficRx->p25FrozenAfcOffsetHz) <= 2500.0) {
                            trafficRx->p25Phase2TrafficTargetOffsetKnown = true;
                            trafficRx->p25Phase2TrafficTargetOffsetHz =
                                trafficRx->p25FrozenAfcOffsetHz;
                            trafficRx->p25Phase2TrafficTargetOffsetTrust = 1;
                            trafficRx->p25Phase2TrafficTargetOffsetMisses = 0;
                        }
                    } else {
                        trafficRx->p25AfcFrozen = source.retunesPrimary && inheritedControlAfcKnown && !phase2Traffic;
                        trafficRx->p25FrozenAfcOffsetHz = trafficRx->p25AfcFrozen ? inheritedControlAfcHz : 0.0;
                        trafficRx->p25Phase2TrafficTargetOffsetKnown = inheritedControlTargetOffsetKnown;
                        trafficRx->p25Phase2TrafficTargetOffsetHz = inheritedControlTargetOffsetKnown ? inheritedControlTargetOffsetHz : 0.0;
                        trafficRx->p25Phase2TrafficTargetOffsetTrust = inheritedControlTargetOffsetKnown ? kP25Phase2TrafficTargetOffsetVerifiedTrust : 0;
                        trafficRx->p25Phase2TrafficTargetOffsetMisses = 0;
                    }
                    trafficRx->p25VoiceLiveDecoder = P25LiveDecoder(p25VoiceDecoderConfigForReceiver(*trafficRx));
                    if (trafficRx->p25VoiceMaskParamsKnown) {
                        trafficRx->p25VoiceLiveDecoder.setPhase2MaskParameters(
                            trafficRx->p25VoiceNac, trafficRx->p25VoiceWacn, trafficRx->p25VoiceSystemId);
                    } else {
                        trafficRx->p25VoiceLiveDecoder.clearPhase2MaskParameters();
                    }
                    clearP25VoiceDiagnostics(*trafficRx);
                }

                auto& mgr = DeviceManager::instance();
                // Never sleep on the Qt GUI thread waiting for Soapy retune (was up to
                // 650 ms via waitForCenterTuneApplied). That freezes the UI until Windows
                // reports "Not Responding". Stream thread applies center async; pre-roll
                // absorbs the gap.
                if (source.retunesPrimary && primaryRetuneSeq != 0) {
                    const bool tuneApplied = mgr.waitForCenterTuneApplied(source.deviceIndex, primaryRetuneSeq, 0);
                    if (!tuneApplied) {
                        appendP25LogLine(QString("P25 one-RTL traffic source tune pending (seq=%1); arming cursor with bounded pre-roll (non-blocking).")
                            .arg(static_cast<qulonglong>(primaryRetuneSeq)));
                    }
                }

                // Start every Phase-2 traffic cursor with bounded pre-roll so the
                // first decode has enough continuous context to derive TDMA
                // superframe/mask timing.  One-RTL physical retunes use a shorter
                // pre-roll; old control-channel IQ is harmlessly ignored by the
                // traffic decoder, but missing early traffic bursts is not.
                mgr.setReceiverCursorBeforeLiveEdge(
                    source.deviceIndex, *trafficRx,
                    p25Phase2TrafficPreRollSamples(source.sampleRateHz, source.retunesPrimary));
                receivers.push_back(trafficRx);

                // Keep the primary control-channel receiver muted and alive.
                if (!receivers.empty() && receivers[0]) {
                    std::unique_lock<std::mutex> primaryLock(receivers[0]->stateMutex, std::try_to_lock);
                    if (primaryLock.owns_lock()) {
                        receivers[0]->p25ControlChannelMute = true;
                        receivers[0]->p25VoiceDecodeEnabled = false;
                    }
                }
            }

            p25IndependentTrafficActive = true;
            p25IndependentTrafficRetunedPrimary = source.retunesPrimary;
            {
                std::unique_lock<std::mutex> lock(p25VoiceWorkerMutex, std::try_to_lock);
                if (lock.owns_lock()) {
                    p25VoicePendingJobs.clear();
                    p25VoiceCompletedResults.clear();
                }
            }
            p25VoiceWorkerCv.notify_all();
            p25AutoFollowReturnControlFreqHz = ccHz;
            p25AutoFollowVoiceFreqHz = tg.lastVoiceFreqHz;
            p25AutoFollowTunedAtMs = nowMs;
            p25AutoFollowLastGrantMs = nowMs;
            p25AutoFollowLastActiveMs = nowMs;
            p25FollowEnabled = true;
            p25FollowAutoActive = true;
            p25FollowTalkgroupId = tg.talkgroupId;
            p25MonitoredControlFreqHz = ccHz;

            AudioEngine* activeEngine = ensureAudioOutputActive("P25 auto-follow");
            if (activeEngine) {
                QTimer::singleShot(0, this, [this]() {
                    if (engineForAudio) engineForAudio->clearBuffers();
                });
            }

            if (source.retunesPrimary && phase2Traffic) {
                if (inheritedControlTargetOffsetKnown) {
                    appendP25LogLine(QString("P25 one-RTL Phase 2 traffic decode seeded from trusted control target offset=%1kHz; traffic TDMA/MAC evidence must promote the hint.")
                        .arg(inheritedControlTargetOffsetHz / 1000.0, 0, 'f', 3));
                } else {
                    appendP25LogLine(QString("P25 one-RTL Phase 2 traffic decode uses low-IF source center; bounded offset probe will run during acquisition (control AFC=%1kHz kept as soft cold-acquire seed/probe hint, not locked).")
                        .arg(inheritedControlAfcKnown ? inheritedControlAfcHz / 1000.0 : 0.0, 0, 'f', 3));
                }
            }
            appendP25LogLine(QString(source.retunesPrimary
                    ? "P25 traffic source started: TG=%1 voice=%2MHz slot=%3 control=%4MHz dev=%5 kind=%6 sourceCenter=%7MHz sr=%8MHz. One RTL mode: physical tuner is on traffic, control receiver is paused until return; traffic receiver has its own IQ cursor and P25 decoder."
                    : "P25 traffic source started: TG=%1 voice=%2MHz slot=%3 control=%4MHz dev=%5 kind=%6 sourceCenter=%7MHz sr=%8MHz. Control receiver remains on CC; traffic receiver has its own IQ cursor and P25 decoder.")
                .arg(tg.talkgroupId)
                .arg(tg.lastVoiceFreqHz / 1e6, 0, 'f', 5)
                .arg(tg.tdmaSlotKnown ? QString::number(tg.tdmaSlot & 0x01u) : QString("unknown"))
                .arg(ccHz / 1e6, 0, 'f', 5)
                .arg(static_cast<qulonglong>(source.deviceIndex))
                .arg(source.sourceKind)
                .arg(source.centerHz / 1e6, 0, 'f', 5)
                .arg(source.sampleRateHz / 1e6, 0, 'f', 3));
            if (p25Status) p25Status->setText(QString("Traffic source TG %1").arg(tg.talkgroupId));
            return true;
        };

        auto expireP25WarmStandbyIfNeeded = [this, tuneP25Path, setP25ControlChannelMute, p25Status]() {
            if (p25IndependentTrafficActive || p25AutoFollowWarmStandbyUntilMs <= 0) return;
            const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
            if (nowMs < p25AutoFollowWarmStandbyUntilMs) return;
            const double ccHz = p25MonitoredControlFreqHz;
            p25AutoFollowWarmStandbyUntilMs = 0;
            p25AutoFollowWarmStandbyVoiceHz = 0.0;
            if (ccHz > 0.0 && tuneP25Path(ccHz)) {
                setP25ControlChannelMute(true);
                appendP25LogLine(QString("P25 warm standby expired; RF retuned back to control channel %1MHz.")
                    .arg(ccHz / 1e6, 0, 'f', 5));
                if (p25Status) p25Status->setText(QString("Monitoring CC %1 MHz").arg(ccHz / 1e6, 0, 'f', 5));
            }
        };

        auto autoFollowP25Grant = [this, p25Status, p25TgFollowBtn, tuneP25Path, prepareP25InBandVoiceTarget, p25TrafficInCurrentSamplePassband, p25Phase2TrafficInQualityPassband, scheduleP25VoiceFollowArm, returnP25AutoFollowToControl, selectP25IndependentTrafficSource, startP25IndependentTrafficSource, expireP25WarmStandbyIfNeeded]
            (const P25TalkgroupEntry& tg, const P25ControlEvent& event, qint64 nowMs) -> bool {
            if (!p25AutoFollowEnabled || tg.talkgroupId == 0) return false;
            if (event.talkgroupId != 0 && event.talkgroupId != tg.talkgroupId) return false;

            const double ccHz = p25MonitoredControlFreqHz > 0.0 ? p25MonitoredControlFreqHz : tg.controlFreqHz;
            if (ccHz <= 0.0) return false;

            P25TalkgroupEntry followTg = tg;
            if (p25ControlEventIsResolvedVoiceGrant(event)) {
                followTg = p25TalkgroupEntryFromCurrentGrant(ccHz, event, nowMs);
                p25PreserveTalkgroupEncryptionFromPrior(followTg, tg);
            }
            if (followTg.lastVoiceFreqHz <= 0.0) return false;
            const bool grantLooksPhase2 = p25TalkgroupIsPhase2(followTg);
            double sameCallFollowVoiceHz = followTg.lastVoiceFreqHz;
            if (grantLooksPhase2) {
                const bool maskKnownBefore = followTg.p25MaskParamsKnown;
                auto registrySnapshot = loadP25Talkgroups();
                if (p25AugmentTalkgroupFromKnownSite(followTg, registrySnapshot, ccHz) &&
                    !maskKnownBefore && followTg.p25MaskParamsKnown) {
                    appendP25LogLineKeyed(QString("p2-mask-metadata-inherited:%1:%2")
                            .arg(followTg.talkgroupId)
                            .arg(static_cast<qulonglong>(std::llround(ccHz))),
                        QString("Phase 2 follow inherited site mask metadata for TG %1 on %2MHz: NAC=%3 WACN=%4 SYS=%5.")
                            .arg(followTg.talkgroupId)
                            .arg(ccHz / 1e6, 0, 'f', 5)
                            .arg(p25HexId(followTg.nac, 3))
                            .arg(p25HexId(followTg.wacn, 5))
                            .arg(p25HexId(followTg.systemId, 3)),
                        10000);
                }
            }

            if (grantLooksPhase2) {
                p25PruneRecentExplicitEncryptedPhase2Grants(gP25RecentExplicitEncryptedPhase2Grants, nowMs);
                p25RememberExplicitEncryptedPhase2Grant(gP25RecentExplicitEncryptedPhase2Grants, event, &followTg, nowMs);
            }

            bool probingUnknownPhase2EncryptedHistory = false;
            const bool followReady = p25PrepareTalkgroupForFollowGrant(
                followTg,
                event,
                probingUnknownPhase2EncryptedHistory);

            auto findActiveP25FollowReceiverLocked = [this]() -> std::shared_ptr<Receiver> {
                if (p25IndependentTrafficActive) {
                    const uint64_t liveGen = p25TrafficSourceGeneration.load(std::memory_order_acquire);
                    for (auto& rxPtr : receivers) {
                        if (rxPtr && rxPtr->p25IndependentTrafficSource &&
                            rxPtr->p25TrafficGeneration == liveGen) {
                            return rxPtr;
                        }
                    }
                }
                if (!receivers.empty()) return receivers[0];
                return {};
            };

            if (followTg.encryptionKnown && followTg.encrypted) {
                bool retainFollowDespiteEncryptedCc = false;
                if (p25FollowAutoActive && p25FollowTalkgroupId == followTg.talkgroupId) {
                    retainFollowDespiteEncryptedCc =
                        p25RecentSpeakerOutputActive(nowMs, kP25Phase2SpeakerFollowHoldMs);
                    if (!retainFollowDespiteEncryptedCc) {
                        std::unique_lock<std::mutex> lk(receiversMutex, std::try_to_lock);
                        if (lk.owns_lock()) {
                            auto activeRx = findActiveP25FollowReceiverLocked();
                            if (activeRx) {
                                std::unique_lock<std::mutex> rxLock(activeRx->stateMutex, std::try_to_lock);
                                if (rxLock.owns_lock()) {
                                    retainFollowDespiteEncryptedCc =
                                        p25ActiveFollowTrafficDisprovesEncryption(*activeRx);
                                    if (!retainFollowDespiteEncryptedCc &&
                                        activeRx->p25VoicePhase2 &&
                                        !activeRx->p25VoiceEncrypted &&
                                        p25AutoFollowTunedAtMs > 0 &&
                                        nowMs - p25AutoFollowTunedAtMs < 45000) {
                                        const auto& diag = activeRx->p25VoiceDiagnostics;
                                        // sdrtrunk treats traffic MAC/ESS as authoritative for the
                                        // current call; a CC SVC encrypted re-grant can lag, disagree,
                                        // or arrive on a different traffic MHz for the same TG.
                                        // Only a target-slot clear ESS/session can override a new
                                        // encrypted CC update. Raw VCWs or MAC CRC without ESS are
                                        // not proof that the current voice payload is clear.
                                        retainFollowDespiteEncryptedCc = p25DiagTargetHardClear(diag);
                                    }
                                }
                            }
                        }
                    }
                }
                if (retainFollowDespiteEncryptedCc) {
                    appendP25LogLineKeyed(QString("auto-follow-ignore-cc-encrypted:%1:%2")
                            .arg(followTg.talkgroupId)
                            .arg(static_cast<qlonglong>(std::llround(followTg.lastVoiceFreqHz))),
                        QString("Ignoring encrypted CC grant for active follow TG %1 on %2MHz; traffic MAC/ESS has not confirmed encryption on the current voice channel.")
                            .arg(followTg.talkgroupId)
                            .arg(followTg.lastVoiceFreqHz / 1e6, 0, 'f', 5),
                        4000);
                    return false;
                }
                appendP25LogLineKeyed(QString("auto-skip-encrypted:%1:%2").arg(followTg.talkgroupId).arg(static_cast<int>(grantLooksPhase2)),
                    QString("Auto-follow skipped encrypted P25 TG %1 (%2); staying on/returning to control channel.")
                        .arg(followTg.talkgroupId)
                        .arg(grantLooksPhase2 ? "Phase 2 TDMA" : "Phase 1 FDMA"),
                    4000);
                if (p25FollowAutoActive && p25FollowTalkgroupId == followTg.talkgroupId &&
                    !retainFollowDespiteEncryptedCc) {
                    appendP25LogLine(QString("Encrypted re-grant/late update for followed TG %1; releasing voice follow immediately.").arg(followTg.talkgroupId));
                    returnP25AutoFollowToControl();
                }
                return false;
            }
            if (grantLooksPhase2 && !event.encryptionKnown) {
                bool bypassEncryptedHold = false;
                if (p25FollowAutoActive && p25FollowTalkgroupId == followTg.talkgroupId &&
                    p25RecentSpeakerOutputActive(nowMs, kP25Phase2SpeakerFollowHoldMs)) {
                    bypassEncryptedHold = true;
                }
                if (!bypassEncryptedHold) {
                    std::shared_ptr<Receiver> activeRx;
                    {
                        std::unique_lock<std::mutex> lk(receiversMutex, std::try_to_lock);
                        if (lk.owns_lock()) {
                            activeRx = findActiveP25FollowReceiverLocked();
                            if (activeRx) {
                                std::unique_lock<std::mutex> rxLock(activeRx->stateMutex, std::try_to_lock);
                                if (rxLock.owns_lock()) {
                                    const auto& diag = activeRx->p25VoiceDiagnostics;
                                    if (!activeRx->p25VoiceEncrypted &&
                                        p25DiagTargetHardClear(diag)) {
                                        bypassEncryptedHold = true;
                                        p25ClearExplicitEncryptedPhase2GrantHold(
                                            gP25RecentExplicitEncryptedPhase2Grants, followTg);
                                    }
                                }
                            }
                        }
                    }
                }
                if (!bypassEncryptedHold) {
                    const qint64 encryptedHoldTtlMs = 15000;
                    const qint64 encryptedHoldAgeMs = p25RecentExplicitEncryptedPhase2GrantAgeMs(
                        gP25RecentExplicitEncryptedPhase2Grants, event, followTg, nowMs, encryptedHoldTtlMs);
                    if (encryptedHoldAgeMs >= 0) {
                        appendP25LogLineKeyed(QString("auto-skip-p2-unknown-after-current-encrypted:%1:%2")
                                .arg(followTg.talkgroupId)
                                .arg(followTg.lastChannel),
                            QString("Auto-follow skipped Phase 2 TG %1 unknown grant update because an explicit encrypted grant for the same TG/channel/frequency was seen %2ms earlier; matching sdrtrunk, wait for a fresh clear/current-call MAC or ESS before opening audio.")
                                .arg(followTg.talkgroupId)
                                .arg(encryptedHoldAgeMs),
                            5000);
                        return false;
                    }
                }
            }
            if (probingUnknownPhase2EncryptedHistory) {
                appendP25LogLineKeyed(QString("auto-follow-p2-unknown-after-encrypted:%1").arg(followTg.talkgroupId),
                    QString("Auto-follow probing Phase 2 TG %1 despite stale encrypted history; matching sdrtrunk, follow the allocated timeslot and let MAC/ESS from the current call decide whether audio opens.")
                        .arg(followTg.talkgroupId),
                    8000);
            }
            // OP=0x02 GroupVoiceUpdate has no service options.  Field systems often
            // re-issue OP=0x02 for the entire call and rarely re-send OP=0x00; deferring
            // all new follows until OP=0x00 caused "far and few" retunes (capture logs:
            // repeated "deferred OP=0x02 unknown update" with no voice arm).
            // sdrtrunk retunes on Group Voice Channel Grant Updates and keeps audio
            // muted until the *current* traffic call's PTT/ESS proves clear/encrypted.
            // Do not inherit sticky clear onto a new RF allocation (080701 wrong-channel
            // chase). Same-call updates while already following still retune below.
            if (grantLooksPhase2 &&
                event.type == P25ControlEventType::GroupVoiceUpdate &&
                !event.encryptionKnown) {
                const bool alreadyFollowingThisTg =
                    p25FollowAutoActive && p25FollowTalkgroupId == followTg.talkgroupId;
                if (!alreadyFollowingThisTg) {
                    // Force unknown security for this allocation so sticky TG clear
                    // cannot open the speaker before traffic-channel MAC/ESS proof.
                    followTg.encryptionKnown = false;
                    followTg.encrypted = false;
                    appendP25LogLineKeyed(QString("auto-follow-p2-op02-unknown:%1:%2")
                            .arg(followTg.talkgroupId)
                            .arg(static_cast<qlonglong>(std::llround(followTg.lastVoiceFreqHz))),
                        QString("Auto-follow Phase 2 TG %1 OP=0x02 on %2MHz with unknown encryption; retuning, speaker waits for traffic-channel PTT/ESS (not deferred for OP=0x00).")
                            .arg(followTg.talkgroupId)
                            .arg(followTg.lastVoiceFreqHz / 1e6, 0, 'f', 5),
                        5000);
                }
            }
            if (!followReady) {
                if (grantLooksPhase2 && followTg.encryptionKnown && followTg.encrypted && !event.encryptionKnown) {
                    appendP25LogLineKeyed(QString("auto-skip-p2-sticky-encrypted:%1:%2")
                            .arg(followTg.talkgroupId)
                            .arg(static_cast<qlonglong>(std::llround(followTg.lastVoiceFreqHz))),
                        QString("Auto-follow skipped Phase 2 TG %1 OP=0x%2 on %3MHz because prior explicit encrypted state is still active; waiting for a fresh clear grant or traffic-channel MAC/ESS proof before audio.")
                            .arg(followTg.talkgroupId)
                            .arg(QString("%1").arg(static_cast<int>(event.opcode), 2, 16, QLatin1Char('0')).toUpper())
                            .arg(followTg.lastVoiceFreqHz / 1e6, 0, 'f', 5),
                        5000);
                }
                appendP25LogLineKeyed(QString("auto-skip-clear-unknown:%1").arg(followTg.talkgroupId),
                    QString("Auto-follow is waiting for a clear-state grant before following P25 TG %1.").arg(followTg.talkgroupId),
                    5000);
                return false;
            }
            if (!followTg.encryptionKnown && p25TalkgroupIsPhase2(followTg)) {
                appendP25LogLineKeyed(QString("auto-follow-p2-clear-unknown:%1").arg(followTg.talkgroupId),
                    QString("Auto-following Phase 2 TG %1 with unknown grant encryption; audio queues until target-slot PTT/ESS proves clear, with bounded late-entry recovery after strong target-slot mask/superframe/voice evidence.").arg(followTg.talkgroupId),
                    5000);
            }
            if (p25FollowEnabled && !p25FollowAutoActive) {
                appendP25LogLineKeyed(QString("auto-follow-blocked-manual:%1").arg(followTg.talkgroupId),
                    QString("Auto-follow saw P25 TG %1 voice=%2MHz but manual follow state is active for TG %3; not stealing the receiver. Stop manual follow or return to the control channel to let auto-follow take this grant.")
                        .arg(followTg.talkgroupId)
                        .arg(followTg.lastVoiceFreqHz / 1e6, 0, 'f', 5)
                        .arg(p25FollowTalkgroupId),
                    5000);
                return false;
            }

            if (p25FollowAutoActive && p25FollowTalkgroupId == followTg.talkgroupId) {
                p25AutoFollowLastGrantMs = nowMs;
                p25AutoFollowLastActiveMs = nowMs;
                const double priorFollowVoiceFreqHz = p25AutoFollowVoiceFreqHz;
                bool promotedClear = false;
                bool promotedEncrypted = false;
                bool updatedSlot = false;
                bool updatedSource = false;
                bool updatedMask = false;
                bool updatedTrafficMetadata = false;
                bool updatedTrafficCarrier = false;
                bool heldSourceMetadata = false;
                bool resetTrafficCarrierAfterMetadata = false;
                bool rejectedGrantMHzJumpForActiveFollow = false;
                bool sameCallCarrierNeedsRetune = false;
                bool sameCallCarrierOutsideSourcePassband = false;
                bool activeTrafficDecodeUnlocked = false;
                uint32_t heldPriorSourceId = 0;
                uint32_t heldIncomingSourceId = 0;
                double sameCallTrafficSourceCenterHz = 0.0;
                double sameCallTrafficSampleRateHz = 0.0;
                double liveTrafficVoiceFreqHz = priorFollowVoiceFreqHz;
                uint16_t maskNac = 0;
                uint32_t maskWacn = 0;
                uint16_t maskSystemId = 0;
                {
                    // Same-call OP=0x02 metadata updates used to block on these locks
                    // while the DSP worker held stateMutex across rolling IQ pulls —
                    // field freeze at 2026-07-16 18:52:55 ended mid OP=0x02 grant.
                    std::unique_lock<std::mutex> lk(receiversMutex, std::try_to_lock);
                    if (!lk.owns_lock()) {
                        appendP25LogLineKeyed("auto-follow-same-call-receivers-busy",
                            "P25 same-call metadata update deferred (receivers busy); GUI did not block.",
                            500);
                        return false;
                    }
                    auto activeRx = findActiveP25FollowReceiverLocked();
                    if (activeRx) {
                        std::unique_lock<std::mutex> rxLock(activeRx->stateMutex, std::try_to_lock);
                        if (!rxLock.owns_lock()) {
                            appendP25LogLineKeyed("auto-follow-same-call-state-busy",
                                "P25 same-call metadata update deferred (rx state busy); GUI did not block.",
                                500);
                            return false;
                        }
                        liveTrafficVoiceFreqHz = activeRx->p25TrafficVoiceFreqHz > 0.0
                            ? activeRx->p25TrafficVoiceFreqHz
                            : activeRx->freqHz;
                        const bool trafficDecodeUnlocked = p25FollowTrafficDecodeUnlocked(*activeRx);
                        activeTrafficDecodeUnlocked = trafficDecodeUnlocked;
                        sameCallFollowVoiceHz = p25SanitizedSameCallFollowVoiceHz(
                            event, liveTrafficVoiceFreqHz, followTg.lastVoiceFreqHz, trafficDecodeUnlocked);
                        const bool rejectedGrantMHzJump =
                            followTg.lastVoiceFreqHz > 0.0 &&
                            sameCallFollowVoiceHz > 0.0 &&
                            std::abs(followTg.lastVoiceFreqHz - sameCallFollowVoiceHz) > 50.0;
                        rejectedGrantMHzJumpForActiveFollow = rejectedGrantMHzJump;
                        if (rejectedGrantMHzJump) {
                            appendP25LogLineKeyed(QString("auto-follow-ignore-grant-mhz-jump:%1:%2:%3")
                                    .arg(followTg.talkgroupId)
                                    .arg(static_cast<qlonglong>(std::llround(liveTrafficVoiceFreqHz)))
                                    .arg(static_cast<qlonglong>(std::llround(followTg.lastVoiceFreqHz))),
                                QString("P25 auto-follow ignored same-call grant MHz jump: TG %1 staying on %2MHz instead of OP=0x%3 update to %4MHz (likely stale identifier resolution).")
                                    .arg(followTg.talkgroupId)
                                    .arg(liveTrafficVoiceFreqHz / 1e6, 0, 'f', 5)
                                    .arg(static_cast<int>(event.opcode), 2, 16, QLatin1Char('0'))
                                    .arg(followTg.lastVoiceFreqHz / 1e6, 0, 'f', 5),
                                2500);
                        }
                        const bool trafficCarrierChanged =
                            !rejectedGrantMHzJump &&
                            sameCallFollowVoiceHz > 0.0 &&
                            std::isfinite(sameCallFollowVoiceHz) &&
                            liveTrafficVoiceFreqHz > 0.0 &&
                            std::isfinite(liveTrafficVoiceFreqHz) &&
                            std::abs(liveTrafficVoiceFreqHz - sameCallFollowVoiceHz) > 50.0;
                        bool commitSameCallMetadataInPlace = !rejectedGrantMHzJump;
                        if (activeRx->p25IndependentTrafficSource && trafficCarrierChanged) {
                            sameCallTrafficSourceCenterHz =
                                activeRx->p25TrafficSourceCenterFreqHz > 0.0 &&
                                std::isfinite(activeRx->p25TrafficSourceCenterFreqHz)
                                    ? activeRx->p25TrafficSourceCenterFreqHz
                                    : liveTrafficVoiceFreqHz;
                            try {
                                const auto devices = DeviceManager::instance().getDevices();
                                if (activeRx->deviceIndex >= 0 &&
                                    static_cast<size_t>(activeRx->deviceIndex) < devices.size()) {
                                    sameCallTrafficSampleRateHz = devices[static_cast<size_t>(activeRx->deviceIndex)].sampleRate;
                                }
                            } catch (...) {
                                sameCallTrafficSampleRateHz = 0.0;
                            }
                            if (!std::isfinite(sameCallTrafficSampleRateHz) || sameCallTrafficSampleRateHz <= 0.0) {
                                sameCallTrafficSampleRateHz = 2.048e6;
                            }
                            // Phase-2 needs quality passband (not just Nyquist-legal).
                            // Phase-1 can stay on the looser sample passband.
                            sameCallCarrierOutsideSourcePassband = grantLooksPhase2
                                ? !p25Phase2TrafficInQualityPassband(sameCallFollowVoiceHz,
                                    sameCallTrafficSourceCenterHz, sameCallTrafficSampleRateHz)
                                : !p25TrafficInCurrentSamplePassband(sameCallFollowVoiceHz,
                                    sameCallTrafficSourceCenterHz, sameCallTrafficSampleRateHz);
                            sameCallCarrierNeedsRetune = sameCallCarrierOutsideSourcePassband;
                            if (sameCallCarrierNeedsRetune) {
                                commitSameCallMetadataInPlace = false;
                            }
                        }
                        activeRx->p25VoiceDecodeEnabled = true;
                        activeRx->p25VoicePhase2 = grantLooksPhase2 || activeRx->p25VoicePhase2;
                        activeRx->p25VoiceTalkgroupId = followTg.talkgroupId;
                        const bool incomingSourceKnown = followTg.lastSourceId != 0;
                        const bool incomingSourceChanged =
                            incomingSourceKnown &&
                            activeRx->p25VoiceSourceId != 0 &&
                            activeRx->p25VoiceSourceId != followTg.lastSourceId;
                        const bool incomingSameSlot =
                            followTg.tdmaSlotKnown &&
                            activeRx->p25VoiceTdmaSlotKnown &&
                            static_cast<uint8_t>(activeRx->p25VoiceTdmaSlot & 0x01u) ==
                                static_cast<uint8_t>(followTg.tdmaSlot & 0x01u);
                        const bool sourceChangeIsControlMetadataOnly =
                            grantLooksPhase2 &&
                            incomingSourceChanged &&
                            commitSameCallMetadataInPlace &&
                            !trafficCarrierChanged &&
                            incomingSameSlot &&
                            activeRx->p25CurrentCallSessionId != 0;
                        const bool incomingSourceStartsNewPtt =
                            incomingSourceChanged && !sourceChangeIsControlMetadataOnly;
                        if (incomingSourceChanged && sourceChangeIsControlMetadataOnly) {
                            heldSourceMetadata = true;
                            heldPriorSourceId = activeRx->p25VoiceSourceId;
                            heldIncomingSourceId = followTg.lastSourceId;
                        }
                        if (incomingSourceKnown && incomingSourceStartsNewPtt) {
                            p25Phase2AdoptGrantSourceIdForCurrentCall(*activeRx, followTg.lastSourceId);
                            updatedSource = true;
                        }
                        if (trafficCarrierChanged ||
                            incomingSourceStartsNewPtt ||
                            !commitSameCallMetadataInPlace ||
                            activeRx->p25CurrentCallSessionId == 0) {
                            p25Phase2BeginNewPtt(*activeRx, nowMs);
                        } else {
                            p25Phase2RefreshGrantEpoch(*activeRx, nowMs);
                        }
                        if (activeRx->p25IndependentTrafficSource) {
                            activeRx->p25TrafficLastGrantMs = nowMs;
                            if (commitSameCallMetadataInPlace) {
                                activeRx->p25TrafficVoiceFreqHz = sameCallFollowVoiceHz;
                                if (sameCallFollowVoiceHz > 0.0 && std::isfinite(sameCallFollowVoiceHz)) {
                                    activeRx->freqHz = sameCallFollowVoiceHz;
                                }
                                updatedTrafficMetadata = true;
                            }
                            activeRx->p25TrafficControlFreqHz = ccHz;
                            if (trafficCarrierChanged && commitSameCallMetadataInPlace) {
                                updatedTrafficCarrier = true;
                                resetTrafficCarrierAfterMetadata = true;
                            }
                        }
                        if (commitSameCallMetadataInPlace && p25TalkgroupGrantProvesSpeakerEncrypted(followTg)) {
                            bool trafficProvesEncrypted = true;
                            if (p25ActiveFollowTrafficDisprovesEncryption(*activeRx)) {
                                trafficProvesEncrypted = false;
                            }
                            if (trafficProvesEncrypted) {
                                promotedEncrypted = !activeRx->p25VoiceEncrypted;
                                activeRx->p25VoiceClearKnown = false;
                                activeRx->p25VoiceEncrypted = true;
                            }
                        } else if (commitSameCallMetadataInPlace && p25TalkgroupGrantProvesSpeakerClear(followTg)) {
                            promotedClear = !activeRx->p25VoiceClearKnown || activeRx->p25VoiceEncrypted;
                            activeRx->p25VoiceClearKnown = true;
                            activeRx->p25VoiceEncrypted = false;
                        } else if (!followTg.encryptionKnown &&
                                   activeRx->p25VoiceClearKnown &&
                                   !activeRx->p25VoiceEncrypted) {
                            // OP=0x02 service-option-less updates must not downgrade an
                            // explicit clear grant already established on the traffic rx.
                        }
                        if (commitSameCallMetadataInPlace &&
                            followTg.lastSourceId != 0 &&
                            activeRx->p25VoiceSourceId != followTg.lastSourceId) {
                            if (sourceChangeIsControlMetadataOnly) {
                                heldSourceMetadata = true;
                                heldPriorSourceId = activeRx->p25VoiceSourceId;
                                heldIncomingSourceId = followTg.lastSourceId;
                            } else {
                                p25Phase2AdoptGrantSourceIdForCurrentCall(*activeRx, followTg.lastSourceId);
                                updatedSource = true;
                            }
                        }
                        if (commitSameCallMetadataInPlace && followTg.tdmaSlotKnown) {
                            const uint8_t slot = static_cast<uint8_t>(followTg.tdmaSlot & 0x01u);
                            updatedSlot = !activeRx->p25VoiceTdmaSlotKnown || activeRx->p25VoiceTdmaSlot != slot;
                            activeRx->p25VoiceTdmaSlotKnown = true;
                            activeRx->p25VoiceTdmaSlot = slot;
                            activeRx->p25TrafficSlot = slot;
                            p25Phase2MarkGrantedSlotImmutable(*activeRx);
                        }
                        if (commitSameCallMetadataInPlace && followTg.p25MaskParamsKnown) {
                            updatedMask =
                                !activeRx->p25VoiceMaskParamsKnown ||
                                activeRx->p25VoiceNac != followTg.nac ||
                                activeRx->p25VoiceWacn != followTg.wacn ||
                                activeRx->p25VoiceSystemId != followTg.systemId;
                            activeRx->p25VoiceMaskParamsKnown = true;
                            activeRx->p25VoiceNac = followTg.nac;
                            activeRx->p25VoiceWacn = followTg.wacn;
                            activeRx->p25VoiceSystemId = followTg.systemId;
                            maskNac = followTg.nac;
                            maskWacn = followTg.wacn;
                            maskSystemId = followTg.systemId;
                        }
                        if (resetTrafficCarrierAfterMetadata) {
                            p25Phase2ResetTrafficTargetOffset(*activeRx);
                            p25ClearPhase2PendingAudio(*activeRx);
                            activeRx->p25VoiceSettleUntilMs = nowMs + 80;
                            activeRx->p25VoiceDiscardWindows = 0;
                            activeRx->p25VoiceResetPending = true;
                            (void)tryApplyP25VoiceResetLocked(*activeRx);
                            // tryApply may clear reset-pending on this thread before the
                            // DSP worker runs; keep an explicit speaker-playback clear so
                            // prior-carrier pending/ring PCM cannot drain after the hop.
                            activeRx->p25Phase2SpeakerPlaybackClearPending = true;
                        }
                        if (updatedMask) {
                            std::unique_lock<std::recursive_mutex> dspLock(activeRx->dspMutex, std::try_to_lock);
                            if (dspLock.owns_lock()) {
                                activeRx->p25VoiceLiveDecoder.setPhase2MaskParameters(maskNac, maskWacn, maskSystemId);
                            }
                        }
                    }
                }
                if (promotedEncrypted) {
                    appendP25LogLine(QString("P25 auto-follow same-call encrypted promotion: TG %1 on %2MHz was updated to encrypted by a later grant/MAC update; returning to control.")
                        .arg(followTg.talkgroupId)
                        .arg(followTg.lastVoiceFreqHz / 1e6, 0, 'f', 5));
                    returnP25AutoFollowToControl();
                    return false;
                }
                if (promotedClear || updatedSlot || updatedSource || updatedMask || updatedTrafficMetadata) {
                    appendP25LogLineKeyed(QString("auto-follow-same-call-promote:%1:%2")
                            .arg(followTg.talkgroupId)
                            .arg(static_cast<qlonglong>(std::llround(followTg.lastVoiceFreqHz))),
                        QString("P25 auto-follow same-call metadata promotion: TG %1 on %2MHz now clear=%3 slot=%4 source=%5 mask=%6; active GUI traffic receiver was updated in place. Grants stay queued for target-slot traffic ESS/PTT/session proof before speaker release.")
                            .arg(followTg.talkgroupId)
                            .arg(followTg.lastVoiceFreqHz / 1e6, 0, 'f', 5)
                            .arg(promotedClear || followTg.encryptionKnown
                                ? (!followTg.encrypted ? "yes" : "no")
                                : "unknown")
                            .arg(followTg.tdmaSlotKnown ? QString::number(followTg.tdmaSlot & 0x01u) : QString("unknown"))
                            .arg(followTg.lastSourceId != 0 ? QString::number(followTg.lastSourceId) : QString("unknown"))
                            .arg(followTg.p25MaskParamsKnown ? "known" : "unknown"),
                        2500);
                }
                if (heldSourceMetadata) {
                    appendP25LogLineKeyed(QString("auto-follow-same-call-source-soft:%1:%2:%3")
                            .arg(followTg.talkgroupId)
                            .arg(static_cast<qlonglong>(std::llround(followTg.lastVoiceFreqHz)))
                            .arg(heldIncomingSourceId),
                        QString("P25 same-call source metadata held soft: TG %1 on %2MHz kept active audio session RID %3 while control grant reported RID %4. Traffic-channel PTT/ESS/MAC will own the real call boundary so queued Phase-2 AMBE is not discarded before clear release.")
                            .arg(followTg.talkgroupId)
                            .arg(followTg.lastVoiceFreqHz / 1e6, 0, 'f', 5)
                            .arg(heldPriorSourceId != 0 ? p25HexId(heldPriorSourceId, 6) : QStringLiteral("unknown"))
                            .arg(heldIncomingSourceId != 0 ? p25HexId(heldIncomingSourceId, 6) : QStringLiteral("unknown")),
                        2500);
                }
                if (updatedTrafficCarrier) {
                    appendP25LogLineKeyed(QString("auto-follow-same-call-inband-carrier-hop:%1:%2:%3")
                            .arg(followTg.talkgroupId)
                            .arg(static_cast<qlonglong>(std::llround(liveTrafficVoiceFreqHz)))
                            .arg(static_cast<qlonglong>(std::llround(sameCallFollowVoiceHz))),
                        QString("P25 same-call in-source channel hop: TG %1 target %2MHz -> %3MHz; selected voice carrier, decoder state, audio de-dupe, rolling IQ session, and speaker playback queue were reset while RF/source center stayed unchanged.")
                            .arg(followTg.talkgroupId)
                            .arg(liveTrafficVoiceFreqHz / 1e6, 0, 'f', 5)
                            .arg(sameCallFollowVoiceHz / 1e6, 0, 'f', 5),
                        2500);
                    if (engineForAudio) {
                        QTimer::singleShot(0, this, [this]() {
                            if (engineForAudio) engineForAudio->clearBuffers();
                        });
                    }
                }
                if (sameCallCarrierNeedsRetune) {
                    appendP25LogLineKeyed(QString("auto-follow-same-call-out-of-source:%1:%2:%3")
                            .arg(followTg.talkgroupId)
                            .arg(static_cast<qlonglong>(std::llround(liveTrafficVoiceFreqHz)))
                            .arg(static_cast<qlonglong>(std::llround(sameCallFollowVoiceHz))),
                        QString("P25 same-call grant requires RF retune: TG %1 voice %2MHz -> %3MHz is outside Phase-2 quality passband of source center %4MHz (offset=%5kHz sr=%6MHz); not updating traffic target until retune commits.")
                            .arg(followTg.talkgroupId)
                            .arg(liveTrafficVoiceFreqHz / 1e6, 0, 'f', 5)
                            .arg(sameCallFollowVoiceHz / 1e6, 0, 'f', 5)
                            .arg(sameCallTrafficSourceCenterHz / 1e6, 0, 'f', 5)
                            .arg((sameCallFollowVoiceHz - sameCallTrafficSourceCenterHz) / 1000.0, 0, 'f', 1)
                            .arg(sameCallTrafficSampleRateHz / 1e6, 0, 'f', 3),
                        2500);
                }
                const qint64 dwellSinceTuneMs = p25AutoFollowTunedAtMs > 0 ? nowMs - p25AutoFollowTunedAtMs : 0;
                const qint64 dwellSinceLastHopMs = p25AutoFollowLastMHzHopMs > 0
                    ? nowMs - p25AutoFollowLastMHzHopMs
                    : dwellSinceTuneMs;
                const double sameCallHopDeltaHz =
                    (sameCallFollowVoiceHz > 0.0 && liveTrafficVoiceFreqHz > 0.0)
                        ? std::abs(liveTrafficVoiceFreqHz - sameCallFollowVoiceHz)
                        : 0.0;
                const bool sameCallHopAuthorized =
                    p25GrantAuthorizesSameCallVoiceMHzHop(
                        event, liveTrafficVoiceFreqHz, sameCallFollowVoiceHz,
                        dwellSinceTuneMs, dwellSinceLastHopMs, activeTrafficDecodeUnlocked);
                // Capture 20260808_005246: OP=0x02 hop to 419.875 stayed on
                // RF center 419.125 (750 kHz edge) because Nyquist-legal in-source
                // hop never retuned. When quality passband fails, force a real
                // low-IF retune even for grant updates (within correction cap).
                const bool sameCallQualityRetuneRequired =
                    sameCallCarrierNeedsRetune &&
                    grantLooksPhase2 &&
                    sameCallHopDeltaHz > 50.0 &&
                    sameCallHopDeltaHz <= kP25SameCallGrantUpdateCorrectionMaxMHzHopHz &&
                    dwellSinceLastHopMs >= kP25SameCallDecodeUnlockedHopMinDwellMs;
                const bool sameCallVoiceMHzHop =
                    sameCallCarrierNeedsRetune &&
                    grantLooksPhase2 &&
                    p25IndependentTrafficEnabled &&
                    p25IndependentTrafficActive &&
                    sameCallFollowVoiceHz > 0.0 &&
                    std::isfinite(liveTrafficVoiceFreqHz) &&
                    liveTrafficVoiceFreqHz > 0.0 &&
                    sameCallHopDeltaHz > 50.0 &&
                    (sameCallHopAuthorized || sameCallQualityRetuneRequired);
                if (sameCallVoiceMHzHop) {
                    // Same TG moved to a new voice-channel allocation (e.g. 421.225 ->
                    // 420.225).  Metadata promotion alone leaves rx.freqHz and the RTL
                    // tuner on the old MHz while the scheduler overrides cf to the new
                    // grant, producing target=oldMHz decode on newMHz IQ (one lucky
                    // 80 ms burst then silence).  Fall through to the in-place retune path.
                    appendP25LogLineKeyed(QString("auto-follow-same-call-mhz-hop:%1:%2:%3")
                            .arg(followTg.talkgroupId)
                            .arg(static_cast<qlonglong>(std::llround(liveTrafficVoiceFreqHz)))
                            .arg(static_cast<qlonglong>(std::llround(sameCallFollowVoiceHz))),
                        QString("P25 auto-follow same-call MHz hop pending: TG %1 voice %2MHz -> %3MHz; retuning traffic source%4.")
                            .arg(followTg.talkgroupId)
                            .arg(liveTrafficVoiceFreqHz / 1e6, 0, 'f', 5)
                            .arg(sameCallFollowVoiceHz / 1e6, 0, 'f', 5)
                            .arg(sameCallQualityRetuneRequired && !sameCallHopAuthorized
                                ? " (quality passband / edge-of-RF recenter)"
                                : " before continuing metadata-only follow"),
                        2500);
                } else {
                    if (sameCallFollowVoiceHz > 0.0 &&
                        !sameCallCarrierNeedsRetune &&
                        !rejectedGrantMHzJumpForActiveFollow) {
                        p25AutoFollowVoiceFreqHz = sameCallFollowVoiceHz;
                    }
                    if (sameCallCarrierNeedsRetune) {
                        appendP25LogLineKeyed(QString("auto-follow-same-call-retune-deferred:%1:%2")
                                .arg(followTg.talkgroupId)
                                .arg(static_cast<qlonglong>(std::llround(followTg.lastVoiceFreqHz))),
                            QString("P25 auto-follow deferred same-call out-of-source grant for TG %1 on %2MHz; current receiver remains on %3MHz until a trusted retune is accepted.")
                                .arg(followTg.talkgroupId)
                                .arg(followTg.lastVoiceFreqHz / 1e6, 0, 'f', 5)
                                .arg(liveTrafficVoiceFreqHz / 1e6, 0, 'f', 5),
                            2500);
                    }
                    if (rejectedGrantMHzJumpForActiveFollow) {
                        appendP25LogLineKeyed(QString("auto-follow-ignore-grant-metadata:%1:%2")
                                .arg(followTg.talkgroupId)
                                .arg(static_cast<qlonglong>(std::llround(followTg.lastVoiceFreqHz))),
                            QString("P25 auto-follow ignored stale same-call grant metadata for TG %1 on %2MHz; active receiver remains on %3MHz/slot unchanged.")
                                .arg(followTg.talkgroupId)
                                .arg(followTg.lastVoiceFreqHz / 1e6, 0, 'f', 5)
                                .arg(sameCallFollowVoiceHz / 1e6, 0, 'f', 5),
                            2500);
                    }
                    return false;
                }
            }

            if (p25FollowAutoActive) {
                const qint64 dwellMs = p25AutoFollowTunedAtMs > 0
                    ? nowMs - p25AutoFollowTunedAtMs
                    : 0;

                bool sameRfPhase2SlotHandoff = false;
                bool currentVoiceUnacquired = false;
                bool currentVoiceSilent = false;
                bool activePhase2Unacquired = false;
                bool currentFollowSecurityUnknown = false;
                bool currentFollowClearTrusted = false;
                bool sameRfPhase2Carrier = false;
                bool phase2HandoffGraceActive = false;
                bool currentFollowSpeakerActive = false;
                double liveFollowCarrierHz = p25AutoFollowVoiceFreqHz;
                if (grantLooksPhase2 && p25TalkgroupIsPhase2(followTg) &&
                    p25FollowTalkgroupId != followTg.talkgroupId) {
                    P25VoiceDiagSnapshot activeDiag;
                    bool haveActiveDiag = false;
                    {
                        std::unique_lock<std::mutex> lk(receiversMutex, std::try_to_lock);
                        auto activeRx = lk.owns_lock() ? findActiveP25FollowReceiverLocked() : std::shared_ptr<Receiver>{};
                        if (activeRx) {
                            std::unique_lock<std::mutex> rxLock(activeRx->stateMutex, std::try_to_lock);
                            if (rxLock.owns_lock()) {
                                liveFollowCarrierHz = p25Phase2VoiceSchedulerNominalHz(*activeRx);
                                activeDiag = activeRx->p25VoiceDiagnostics;
                                currentFollowSecurityUnknown =
                                    activeRx->p25VoicePhase2 &&
                                    !activeRx->p25VoiceClearKnown &&
                                    !activeRx->p25VoiceEncrypted;
                                currentFollowClearTrusted =
                                    activeRx->p25VoicePhase2 &&
                                    activeRx->p25VoiceClearKnown &&
                                    !activeRx->p25VoiceEncrypted;
                                haveActiveDiag = true;
                            }
                        }
                    }
                    if (haveActiveDiag) {
                        const bool noDecodedAudio =
                            activeDiag.decodedFrames == 0 &&
                            activeDiag.phase2AmbeAcceptedFrames == 0 &&
                            activeDiag.audioSamples == 0;
                        const bool noPhase2Lock =
                            activeDiag.phase2SuperframeBursts < 3 &&
                            activeDiag.phase2MaskedBursts < 3 &&
                            activeDiag.phase2MacCrcValid == 0 &&
                            activeDiag.phase2EssKnown == false;
                        // Capture 20260808_022809: preempted ~24s after last emit
                        // mid clear call. Hold longer for clear trusted follows.
                        const bool recentSpeakerHold =
                            p25RecentSpeakerOutputActive(nowMs,
                                currentFollowClearTrusted ? 30000 : 15000);
                        currentVoiceUnacquired =
                            noDecodedAudio && noPhase2Lock && !recentSpeakerHold;
                        const qint64 silentDwellStealGraceMs = currentFollowClearTrusted
                            ? kP25Phase2ClearTrustedSilentDwellStealGraceMs
                            : kP25Phase2SilentDwellStealGraceMs;
                        currentVoiceSilent =
                            noDecodedAudio &&
                            !recentSpeakerHold &&
                            !activeDiag.phase2EssEncrypted &&
                            dwellMs >= silentDwellStealGraceMs;
                        activePhase2Unacquired = currentVoiceUnacquired || currentVoiceSilent;
                    }
                    sameRfPhase2Carrier = std::isfinite(liveFollowCarrierHz) &&
                        liveFollowCarrierHz > 0.0 &&
                        followTg.lastVoiceFreqHz > 0.0 &&
                        std::abs(liveFollowCarrierHz - followTg.lastVoiceFreqHz) <= 50.0;
                    currentFollowSpeakerActive =
                        p25RecentSpeakerOutputActive(nowMs, kP25Phase2SpeakerFollowProtectMs);
                    phase2HandoffGraceActive = dwellMs >= 0 && dwellMs < kP25Phase2SameRfSlotHandoffGraceMs;
                    sameRfPhase2SlotHandoff = haveActiveDiag && currentVoiceUnacquired &&
                        sameRfPhase2Carrier && !phase2HandoffGraceActive &&
                        (currentVoiceSilent ||
                         dwellMs >= kP25Phase2SameRfUnacquiredSlotStealMs);
                    if (sameRfPhase2Carrier && phase2HandoffGraceActive) {
                        appendP25LogLineKeyed(QString("auto-follow-same-rf-grace:%1:%2")
                                .arg(p25FollowTalkgroupId)
                                .arg(followTg.talkgroupId),
                            QString("P25 Phase 2 same-RF slot grant TG %1 slot %2 arrived %3ms after tuning TG %4; holding the current slot through the %5ms acquisition grace instead of immediately stealing. sdrtrunk can decode both traffic slots; this scanner-follow path must give the selected slot a chance to acquire PTT/ESS/voice first.")
                                .arg(followTg.talkgroupId)
                                .arg(followTg.tdmaSlotKnown ? QString::number(followTg.tdmaSlot & 0x01u) : QString("unknown"))
                                .arg(std::max<qint64>(0, dwellMs))
                                .arg(p25FollowTalkgroupId)
                                .arg(kP25Phase2SameRfSlotHandoffGraceMs),
                            2500);
                    }
                }

                const qint64 unacquiredDwellStealGraceMs = currentFollowClearTrusted
                    ? kP25Phase2ClearTrustedUnacquiredDwellStealGraceMs
                    : kP25Phase2UnacquiredDwellStealGraceMs;
                const bool sameRfDifferentSlotGrant =
                    sameRfPhase2Carrier && p25FollowTalkgroupId != followTg.talkgroupId;
                const bool sameTgVoiceHopPending =
                    p25FollowTalkgroupId == followTg.talkgroupId &&
                    grantLooksPhase2 &&
                    p25IndependentTrafficEnabled &&
                    p25IndependentTrafficActive &&
                    sameCallFollowVoiceHz > 0.0 &&
                    std::isfinite(sameCallFollowVoiceHz) &&
                    p25AutoFollowVoiceFreqHz > 0.0 &&
                    std::isfinite(p25AutoFollowVoiceFreqHz) &&
                    std::abs(p25AutoFollowVoiceFreqHz - sameCallFollowVoiceHz) > 50.0;
                // Absolute protect: any different TG while speaker recently played
                // (field 032428: steal mid-emit after min dwell on different MHz).
                // User priority (higher wins) may preempt protect/dwell — roadmap.
                int currentFollowUserPriority = 0;
                int currentFollowActivityScore = 0;
                {
                    const auto registrySnapshot = loadP25Talkgroups();
                    const double holdCcHz = p25MonitoredControlFreqHz > 0.0
                        ? p25MonitoredControlFreqHz
                        : followTg.controlFreqHz;
                    for (const auto& row : registrySnapshot) {
                        if (row.talkgroupId == p25FollowTalkgroupId &&
                            (holdCcHz <= 0.0 || std::abs(row.controlFreqHz - holdCcHz) <= 50.0)) {
                            currentFollowUserPriority = row.userPriority;
                            currentFollowActivityScore = row.activityScore;
                            break;
                        }
                    }
                }
                const bool allowUserPriorityPreempt =
                    p25FollowTalkgroupId != followTg.talkgroupId &&
                    followTg.userPriority > 0 &&
                    followTg.userPriority > currentFollowUserPriority;
                if (allowUserPriorityPreempt) {
                    appendP25LogLineKeyed(QString("auto-follow-priority-preempt:%1:%2")
                            .arg(p25FollowTalkgroupId)
                            .arg(followTg.talkgroupId),
                        QString("P25 auto-follow user-priority preempt: TG %1 pri=%2 activity=%3 -> TG %4 pri=%5 activity=%6 voice=%7MHz.")
                            .arg(p25FollowTalkgroupId)
                            .arg(currentFollowUserPriority)
                            .arg(currentFollowActivityScore)
                            .arg(followTg.talkgroupId)
                            .arg(followTg.userPriority)
                            .arg(followTg.activityScore)
                            .arg(followTg.lastVoiceFreqHz / 1e6, 0, 'f', 5),
                        2500);
                }
                if (p25FollowTalkgroupId != followTg.talkgroupId &&
                    !sameTgVoiceHopPending &&
                    !allowUserPriorityPreempt &&
                    p25RecentSpeakerOutputActive(nowMs, kP25Phase2SpeakerFollowProtectMs)) {
                    appendP25LogLineKeyed(QString("auto-follow-speaker-protect:%1:%2")
                            .arg(p25FollowTalkgroupId)
                            .arg(followTg.talkgroupId),
                        QString("P25 auto-follow speaker protect: keeping TG %1 (recent speaker PCM within %2ms); ignoring different TG %3 voice=%4MHz.")
                            .arg(p25FollowTalkgroupId)
                            .arg(kP25Phase2SpeakerFollowProtectMs)
                            .arg(followTg.talkgroupId)
                            .arg(followTg.lastVoiceFreqHz / 1e6, 0, 'f', 5),
                        1500);
                    return false;
                }
                if (sameRfDifferentSlotGrant &&
                    currentFollowSpeakerActive &&
                    !activePhase2Unacquired &&
                    !currentVoiceSilent &&
                    !allowUserPriorityPreempt) {
                    appendP25LogLineKeyed(QString("auto-follow-same-rf-speaker-hold:%1:%2")
                            .arg(p25FollowTalkgroupId)
                            .arg(followTg.talkgroupId),
                        QString("P25 Phase 2 same-RF speaker hold: keeping active TG %1 on %2MHz while recent speaker audio is present; ignoring different same-RF TG %3 slot %4. SDRTrunk can keep independent slot audio paths, but this GUI speaker path must not steal the selected slot mid-speech.")
                            .arg(p25FollowTalkgroupId)
                            .arg(p25AutoFollowVoiceFreqHz / 1e6, 0, 'f', 5)
                            .arg(followTg.talkgroupId)
                            .arg(followTg.tdmaSlotKnown ? QString::number(followTg.tdmaSlot & 0x01u) : QString("unknown")),
                        1500);
                    return false;
                }
                const qint64 effectiveUnacquiredStealMs = sameRfDifferentSlotGrant
                    ? kP25Phase2SameRfUnacquiredSlotStealMs
                    : unacquiredDwellStealGraceMs;
                const bool allowPhase2DwellSteal = grantLooksPhase2 && activePhase2Unacquired &&
                    ((currentVoiceUnacquired && dwellMs >= effectiveUnacquiredStealMs) ||
                     currentVoiceSilent);
                const bool allowExplicitClearPreempt =
                    grantLooksPhase2 &&
                    p25TalkgroupGrantProvesSpeakerClear(followTg) &&
                    activePhase2Unacquired &&
                    currentFollowSecurityUnknown &&
                    !(sameRfPhase2Carrier && phase2HandoffGraceActive) &&
                    (!sameRfPhase2Carrier ||
                     currentVoiceSilent ||
                     dwellMs >= kP25Phase2SameRfUnacquiredSlotStealMs);
                if (dwellMs < kP25AutoFollowDifferentCallMinDwellMs &&
                    !sameTgVoiceHopPending &&
                    !sameRfPhase2SlotHandoff &&
                    !allowPhase2DwellSteal &&
                    !allowExplicitClearPreempt &&
                    !allowUserPriorityPreempt) {
                    appendP25LogLineKeyed(QString("auto-follow-dwell:%1").arg(p25FollowTalkgroupId),
                        QString("P25 auto-follow holding TG %1 voice=%2MHz for TDMA MAC/ESS acquisition; ignoring different TG %3 until minimum dwell completes (%4/%5ms).")
                            .arg(p25FollowTalkgroupId)
                            .arg(p25AutoFollowVoiceFreqHz / 1e6, 0, 'f', 5)
                            .arg(followTg.talkgroupId)
                            .arg(std::max<qint64>(0, dwellMs))
                            .arg(kP25AutoFollowDifferentCallMinDwellMs),
                        3000);
                    return false;
                }
                if (allowPhase2DwellSteal && !sameRfPhase2SlotHandoff) {
                    appendP25LogLineKeyed(QString("auto-follow-stalled-preempt:%1:%2")
                            .arg(p25FollowTalkgroupId)
                            .arg(followTg.talkgroupId),
                        QString("P25 Phase 2 auto-follow preempting stalled TG %1 after %2ms with no decoded audio; following grant TG %3 voice=%4MHz.")
                            .arg(p25FollowTalkgroupId)
                            .arg(std::max<qint64>(0, dwellMs))
                            .arg(followTg.talkgroupId)
                            .arg(followTg.lastVoiceFreqHz / 1e6, 0, 'f', 5),
                        2500);
                }
                if (allowExplicitClearPreempt) {
                    appendP25LogLineKeyed(QString("auto-follow-clear-preempt:%1:%2")
                            .arg(p25FollowTalkgroupId)
                            .arg(followTg.talkgroupId),
                        QString("P25 Phase 2 auto-follow preempting unknown/no-audio TG %1 with explicit clear grant TG %2 voice=%3MHz slot=%4.")
                            .arg(p25FollowTalkgroupId)
                            .arg(followTg.talkgroupId)
                            .arg(followTg.lastVoiceFreqHz / 1e6, 0, 'f', 5)
                            .arg(followTg.tdmaSlotKnown ? QString::number(followTg.tdmaSlot & 0x01u) : QString("unknown")),
                        2500);
                }
                if (sameRfPhase2SlotHandoff) {
                    appendP25LogLineKeyed(QString("auto-follow-same-rf-slot-handoff:%1:%2")
                            .arg(p25FollowTalkgroupId)
                            .arg(followTg.talkgroupId),
                        QString("P25 Phase 2 same-RF slot handoff: current TG %1 on %2MHz remained unacquired after the acquisition grace, so following new grant TG %3 slot %4. This approximates sdrtrunk's independent traffic-slot handling on a single scanner receiver.")
                            .arg(p25FollowTalkgroupId)
                            .arg(p25AutoFollowVoiceFreqHz / 1e6, 0, 'f', 5)
                            .arg(followTg.talkgroupId)
                            .arg(followTg.tdmaSlotKnown ? QString::number(followTg.tdmaSlot & 0x01u) : QString("unknown")),
                        2500);
                }

                const bool incomingClearGrant = p25TalkgroupGrantProvesSpeakerClear(followTg);
                if (sameRfPhase2Carrier && incomingClearGrant && p25FollowTalkgroupId != followTg.talkgroupId) {
                    p25SameRfClearGrantHoldUntilMs = nowMs + kP25Phase2SameRfClearGrantHoldMs;
                }
                if (sameRfPhase2Carrier && p25SameRfClearGrantHoldUntilMs > nowMs &&
                    currentFollowClearTrusted && !incomingClearGrant &&
                    p25FollowTalkgroupId != followTg.talkgroupId) {
                    appendP25LogLineKeyed(QString("auto-follow-same-rf-clear-hold:%1:%2")
                            .arg(p25FollowTalkgroupId)
                            .arg(followTg.talkgroupId),
                        QString("P25 Phase 2 same-RF clear-grant hold: keeping TG %1 on %2MHz through %3ms hold instead of switching to TG %4 with unknown/encrypted grant.")
                            .arg(p25FollowTalkgroupId)
                            .arg(p25AutoFollowVoiceFreqHz / 1e6, 0, 'f', 5)
                            .arg(std::max<qint64>(0, p25SameRfClearGrantHoldUntilMs - nowMs))
                            .arg(followTg.talkgroupId),
                        2500);
                    return false;
                }

                const bool sameRfMetadataFollowReady =
                    sameRfPhase2Carrier &&
                    p25FollowTalkgroupId != followTg.talkgroupId &&
                    !currentFollowSpeakerActive &&
                    (currentVoiceSilent ||
                     (currentVoiceUnacquired &&
                      dwellMs >= kP25Phase2SameRfUnacquiredSlotStealMs));
                if (sameRfPhase2Carrier &&
                    (sameRfPhase2SlotHandoff || allowPhase2DwellSteal || allowExplicitClearPreempt ||
                     sameRfMetadataFollowReady)) {
                    if (p25LastSameRfMetadataSwitchMs > 0 &&
                        nowMs - p25LastSameRfMetadataSwitchMs < kP25Phase2SameRfMetadataSwitchCooldownMs &&
                        p25FollowTalkgroupId != followTg.talkgroupId) {
                        return false;
                    }
                    // Same RF carrier, different Phase-2 timeslot/TG: sdrtrunk would
                    // keep the traffic channel running and update the TS1/TS2 tracker.
                    // In this single-receiver build, do not retune, destroy the live
                    // TDMA decoder, clear rolling IQ, or restart the 300/250 ms arm path.
                    // Just switch the selected output call/slot metadata and let the
                    // already-running decoder continue parsing both slots on the carrier.
                    bool metadataSwitchCommitted = false;
                    {
                        std::unique_lock<std::mutex> lk(receiversMutex, std::try_to_lock);
                        if (lk.owns_lock()) {
                            ensureReceiver();
                            auto activeRx = findActiveP25FollowReceiverLocked();
                            if (activeRx) {
                                auto& rx = *activeRx;
                                std::unique_lock<std::mutex> rxLock(rx.stateMutex, std::try_to_lock);
                                if (rxLock.owns_lock()) {
                                    p25CommitPhase2TrafficMetadataFollow(rx, followTg, ccHz, nowMs);
                                    metadataSwitchCommitted = true;
                                }
                            }
                        }
                    }
                    if (metadataSwitchCommitted) {
                        p25AutoFollowLastGrantMs = nowMs;
                        p25AutoFollowLastActiveMs = nowMs;
                        p25AutoFollowVoiceFreqHz = followTg.lastVoiceFreqHz;
                        p25AutoFollowTunedAtMs = nowMs;
                        p25FollowEnabled = true;
                        p25FollowAutoActive = true;
                        p25FollowTalkgroupId = followTg.talkgroupId;
                        p25MonitoredControlFreqHz = ccHz;
                        p25LastSameRfMetadataSwitchMs = nowMs;
                        if (incomingClearGrant) {
                            p25SameRfClearGrantHoldUntilMs = nowMs + kP25Phase2SameRfClearGrantHoldMs;
                        }
                        if (p25TgFollowBtn) p25TgFollowBtn->setChecked(true);
                        appendP25LogLine(QString("P25 Phase 2 same-RF slot metadata switch: now selecting TG %1 slot %2 on %3MHz without retune/rearm; rolling traffic decoder was preserved for faster audio.")
                            .arg(followTg.talkgroupId)
                            .arg(followTg.tdmaSlotKnown ? QString::number(followTg.tdmaSlot & 0x01u) : QString("unknown"))
                            .arg(followTg.lastVoiceFreqHz / 1e6, 0, 'f', 5));
                        if (p25Status) p25Status->setText(QString("Auto follow TG %1").arg(followTg.talkgroupId));
                        return true;
                    }
                    appendP25LogLineKeyed("auto-follow-same-rf-metadata-switch-busy",
                        "P25 Phase 2 same-RF slot metadata switch was skipped because receiver locks were busy; preserving current decoder instead of forcing a retune/reset.",
                        2500);
                    return false;
                }
            }

            // Same TG, new voice-channel allocation: retune RF in place and reset
            // the rolling Phase-2 decode cursor/decoder after a trusted MHz hop.
            if (p25FollowAutoActive && p25FollowTalkgroupId == followTg.talkgroupId &&
                grantLooksPhase2 && p25IndependentTrafficEnabled && p25IndependentTrafficActive &&
                sameCallFollowVoiceHz > 0.0 &&
                std::abs(p25AutoFollowVoiceFreqHz - sameCallFollowVoiceHz) > 50.0) {
                const qint64 hopDwellSinceTuneMs = p25AutoFollowTunedAtMs > 0 ? nowMs - p25AutoFollowTunedAtMs : 0;
                const qint64 hopDwellSinceLastHopMs = p25AutoFollowLastMHzHopMs > 0
                    ? nowMs - p25AutoFollowLastMHzHopMs
                    : hopDwellSinceTuneMs;
                bool trafficDecodeUnlocked = false;
                {
                    std::unique_lock<std::mutex> lk(receiversMutex, std::try_to_lock);
                    if (lk.owns_lock()) {
                        auto activeRx = findActiveP25FollowReceiverLocked();
                        if (activeRx) {
                            std::unique_lock<std::mutex> rxLock(activeRx->stateMutex, std::try_to_lock);
                            if (rxLock.owns_lock()) {
                                trafficDecodeUnlocked = p25FollowTrafficDecodeUnlocked(*activeRx);
                            }
                        }
                    }
                }
                if (!p25GrantAuthorizesSameCallVoiceMHzHop(
                        event, p25AutoFollowVoiceFreqHz, sameCallFollowVoiceHz,
                        hopDwellSinceTuneMs, hopDwellSinceLastHopMs, trafficDecodeUnlocked)) {
                    appendP25LogLineKeyed(QString("auto-follow-same-call-hop-not-trusted:%1:%2:%3")
                            .arg(followTg.talkgroupId)
                            .arg(static_cast<qlonglong>(std::llround(p25AutoFollowVoiceFreqHz)))
                            .arg(static_cast<qlonglong>(std::llround(sameCallFollowVoiceHz))),
                        QString("P25 auto-follow ignored untrusted same-call MHz hop for TG %1: active voice remains %2MHz, grant was %3MHz.")
                            .arg(followTg.talkgroupId)
                            .arg(p25AutoFollowVoiceFreqHz / 1e6, 0, 'f', 5)
                            .arg(sameCallFollowVoiceHz / 1e6, 0, 'f', 5),
                        2500);
                    return false;
                }
                std::shared_ptr<Receiver> activeRx;
                int trafficDeviceIndex = -1;
                double oldVoiceFreqHz = p25AutoFollowVoiceFreqHz;
                double inheritedControlAfcHz = 0.0;
                bool inheritedControlAfcKnown = false;
                double inheritedControlTargetOffsetHz = 0.0;
                bool inheritedControlTargetOffsetKnown = false;
                bool keepVerifiedOffset = false;
                {
                    std::unique_lock<std::mutex> lk(receiversMutex, std::try_to_lock);
                    if (lk.owns_lock()) {
                        activeRx = findActiveP25FollowReceiverLocked();
                        if (activeRx && activeRx->p25IndependentTrafficSource && activeRx->p25TrafficRetunesPrimary) {
                            trafficDeviceIndex = activeRx->deviceIndex;
                            const double channelHopHz = std::abs(oldVoiceFreqHz - sameCallFollowVoiceHz);
                            keepVerifiedOffset =
                                channelHopHz <= 50.0 &&
                                activeRx->p25Phase2TrafficTargetOffsetKnown &&
                                activeRx->p25Phase2TrafficTargetOffsetTrust >= kP25Phase2TrafficTargetOffsetVerifiedTrust &&
                                p25Phase2EffectiveTrafficTargetOffsetHz(*activeRx) != 0.0;
                            if (!keepVerifiedOffset) {
                                if (!receivers.empty() && receivers[0]) {
                                    std::unique_lock<std::mutex> primaryLock(receivers[0]->stateMutex, std::try_to_lock);
                                    if (primaryLock.owns_lock() && receivers[0]->afcLocked &&
                                        std::isfinite(receivers[0]->afcOffsetHz)) {
                                        inheritedControlAfcHz = receivers[0]->afcOffsetHz;
                                    }
                                }
                                if (!std::isfinite(inheritedControlAfcHz)) {
                                    inheritedControlAfcHz = gLastAfcOffsetHz.load(std::memory_order_relaxed);
                                }
                                inheritedControlAfcKnown =
                                    std::isfinite(inheritedControlAfcHz) &&
                                    std::abs(inheritedControlAfcHz) >= 50.0 &&
                                    std::abs(inheritedControlAfcHz) <= 45000.0;
                                inheritedControlTargetOffsetKnown =
                                    p25TrustedControlOffsetForPhase2Traffic(
                                        ccHz, nowMs, &inheritedControlTargetOffsetHz);
                            }
                        } else {
                            activeRx.reset();
                        }
                    }
                }
                if (activeRx && trafficDeviceIndex >= 0) {
                    uint64_t retuneSeq = 0;
                    bool retuned = false;
                    double hopCenterHz = sameCallFollowVoiceHz;
                    try {
                        auto& mgr = DeviceManager::instance();
                        mgr.setEnabled(trafficDeviceIndex, true);
                        double hopSampleRateHz = 0.0;
                        const auto devices = mgr.getDevices();
                        if (trafficDeviceIndex >= 0 &&
                            static_cast<size_t>(trafficDeviceIndex) < devices.size()) {
                            hopSampleRateHz = devices[static_cast<size_t>(trafficDeviceIndex)].sampleRate;
                        }
                        hopCenterHz = p25Phase2LowIfTrafficCenterHz(
                            sameCallFollowVoiceHz, hopSampleRateHz);
                        retuneSeq = mgr.setCenterFreq(trafficDeviceIndex, hopCenterHz);
                        if (!mgr.isStreaming(trafficDeviceIndex)) {
                            mgr.startStreaming(trafficDeviceIndex, true);
                        }
                        retuned = true;
                    } catch (const std::exception& ex) {
                        appendP25LogLine(QString("P25 same-call channel hop failed to retune to %1MHz: %2")
                            .arg(sameCallFollowVoiceHz / 1e6, 0, 'f', 5)
                            .arg(ex.what()));
                    } catch (...) {
                        appendP25LogLine(QString("P25 same-call channel hop failed to retune to %1MHz: unknown error")
                            .arg(sameCallFollowVoiceHz / 1e6, 0, 'f', 5));
                    }
                    if (retuned) {
                        auto& mgr = DeviceManager::instance();
                        // Non-blocking: do not park the GUI event loop on retune apply.
                        if (retuneSeq != 0 &&
                            !mgr.waitForCenterTuneApplied(trafficDeviceIndex, retuneSeq, 0)) {
                            appendP25LogLineKeyed(
                                QString("p25-same-call-tune-pending:%1").arg(followTg.talkgroupId),
                                QString("P25 same-call hop tune pending (seq=%1) for TG %2; continuing without GUI sleep.")
                                    .arg(static_cast<qulonglong>(retuneSeq))
                                    .arg(followTg.talkgroupId),
                                3000);
                        }
                        {
                            std::unique_lock<std::mutex> rxLock(activeRx->stateMutex, std::try_to_lock);
                            if (!rxLock.owns_lock()) {
                                appendP25LogLineKeyed("p25-same-call-hop-state-busy",
                                    "P25 same-call hop metadata update deferred (rx state busy); GUI did not block.",
                                    2000);
                                return false;
                            }
                            activeRx->freqHz = sameCallFollowVoiceHz;
                            activeRx->p25TrafficVoiceFreqHz = sameCallFollowVoiceHz;
                            activeRx->p25TrafficLastGrantMs = nowMs;
                            activeRx->p25TrafficControlFreqHz = ccHz;
                            activeRx->p25TrafficSourceCenterFreqHz = hopCenterHz;
                            activeRx->p25VoiceDecodeEnabled = true;
                            activeRx->p25VoicePhase2 = true;
                            activeRx->p25VoiceTalkgroupId = followTg.talkgroupId;
                            const bool incomingSlotKnown = followTg.tdmaSlotKnown;
                            const uint8_t incomingSlot = incomingSlotKnown
                                ? static_cast<uint8_t>(followTg.tdmaSlot & 0x01u)
                                : 0xffu;
                            const bool incomingSlotChanged =
                                incomingSlotKnown &&
                                (!activeRx->p25VoiceTdmaSlotKnown ||
                                 static_cast<uint8_t>(activeRx->p25VoiceTdmaSlot & 0x01u) != incomingSlot);
                            const bool incomingSourceKnown = followTg.lastSourceId != 0;
                            const bool incomingSourceChanged =
                                incomingSourceKnown &&
                                activeRx->p25VoiceSourceId != 0 &&
                                activeRx->p25VoiceSourceId != followTg.lastSourceId;
                            if (incomingSourceKnown && incomingSourceChanged) {
                                p25Phase2AdoptGrantSourceIdForCurrentCall(*activeRx, followTg.lastSourceId);
                            }
                            if (incomingSlotKnown) {
                                activeRx->p25VoiceTdmaSlotKnown = true;
                                activeRx->p25VoiceTdmaSlot = incomingSlot;
                                activeRx->p25TrafficSlot = incomingSlot;
                            }
                            const bool missingCallSession =
                                activeRx->p25CurrentCallSessionId == 0 ||
                                activeRx->p25PttGeneration == 0;
                            const bool hopCreatesAudioBoundary =
                                missingCallSession ||
                                incomingSlotChanged ||
                                incomingSourceChanged ||
                                std::abs(oldVoiceFreqHz - sameCallFollowVoiceHz) > 50.0;
                            if (hopCreatesAudioBoundary) {
                                p25Phase2BeginNewPtt(*activeRx, nowMs);
                            } else {
                                p25Phase2RefreshGrantEpoch(*activeRx, nowMs);
                            }
                            if (p25TalkgroupGrantProvesSpeakerEncrypted(followTg) &&
                                !p25ActiveFollowTrafficDisprovesEncryption(*activeRx)) {
                                activeRx->p25VoiceClearKnown = false;
                                activeRx->p25VoiceEncrypted = true;
                            } else if (p25TalkgroupGrantProvesSpeakerClear(followTg)) {
                                activeRx->p25VoiceClearKnown = true;
                                activeRx->p25VoiceEncrypted = false;
                            }
                            if (followTg.lastSourceId != 0) {
                                p25Phase2AdoptGrantSourceIdForCurrentCall(*activeRx, followTg.lastSourceId);
                            }
                            if (followTg.p25MaskParamsKnown) {
                                activeRx->p25VoiceMaskParamsKnown = true;
                                activeRx->p25VoiceNac = followTg.nac;
                                activeRx->p25VoiceWacn = followTg.wacn;
                                activeRx->p25VoiceSystemId = followTg.systemId;
                            }
                            activeRx->p25VoiceSettleUntilMs = nowMs + 80;
                            activeRx->p25VoiceDiscardWindows = 0;
                            if (!keepVerifiedOffset) {
                                p25Phase2ResetTrafficTargetOffset(*activeRx);
                                p25ClearPhase2PendingAudio(*activeRx);
                            }
                            activeRx->p25VoiceResetPending = true;
                            tryApplyP25VoiceResetLocked(*activeRx);
                            // Same-call MHz hop: flush speaker jitter/ring so old-carrier
                            // PCM cannot play after the retune (20260810_221028).
                            activeRx->p25Phase2SpeakerPlaybackClearPending = true;
                            if (incomingSlotKnown) {
                                const uint8_t slot = incomingSlot;
                                activeRx->p25VoiceTdmaSlotKnown = true;
                                activeRx->p25VoiceTdmaSlot = slot;
                                activeRx->p25TrafficSlot = slot;
                                p25Phase2MarkGrantedSlotImmutable(*activeRx);
                            }
                            if (!keepVerifiedOffset && inheritedControlTargetOffsetKnown) {
                                p25SeedPhase2TrafficOffsetFromControl(*activeRx, inheritedControlTargetOffsetHz, 1);
                            } else if (!keepVerifiedOffset && inheritedControlAfcKnown && !activeRx->p25TrafficRetunesPrimary) {
                                const double seededOffsetHz = std::clamp(inheritedControlAfcHz,
                                    -kP25Phase2TrafficTargetOffsetMaxHz, kP25Phase2TrafficTargetOffsetMaxHz);
                                activeRx->p25Phase2TrafficTargetOffsetKnown = true;
                                activeRx->p25Phase2TrafficTargetOffsetHz = seededOffsetHz;
                                activeRx->p25Phase2TrafficTargetOffsetTrust = 0;
                                activeRx->p25Phase2TrafficTargetOffsetMisses = 0;
                                activeRx->p25AfcFrozen = true;
                                activeRx->p25FrozenAfcOffsetHz = seededOffsetHz;
                            }
                        }
                        if (followTg.p25MaskParamsKnown) {
                            std::unique_lock<std::recursive_mutex> dspLock(activeRx->dspMutex, std::try_to_lock);
                            if (dspLock.owns_lock()) {
                                activeRx->p25VoiceLiveDecoder.setPhase2MaskParameters(
                                    followTg.nac, followTg.wacn, followTg.systemId);
                            }
                        }
                        const auto devices = mgr.getDevices();
                        const double trafficSr = (trafficDeviceIndex < static_cast<int>(devices.size()) &&
                                                  devices[trafficDeviceIndex].sampleRate > 0.0)
                            ? devices[trafficDeviceIndex].sampleRate
                            : 2.048e6;
                        mgr.setReceiverCursorBeforeLiveEdge(
                            trafficDeviceIndex, *activeRx,
                            p25Phase2TrafficPreRollSamples(trafficSr, true));
                        p25AutoFollowVoiceFreqHz = sameCallFollowVoiceHz;
                        p25AutoFollowTunedAtMs = nowMs;
                        p25AutoFollowLastMHzHopMs = nowMs;
                        p25AutoFollowLastGrantMs = nowMs;
                        p25AutoFollowLastActiveMs = nowMs;
                        if (p25TgFollowBtn) p25TgFollowBtn->setChecked(true);
                        appendP25LogLine(QString("P25 same-call channel hop: TG %1 retuned voice %2MHz -> %3MHz with low-IF RF center and decoder/cursor reset for TDMA re-acquisition.")
                            .arg(followTg.talkgroupId)
                            .arg(oldVoiceFreqHz / 1e6, 0, 'f', 5)
                            .arg(sameCallFollowVoiceHz / 1e6, 0, 'f', 5));
                        if (p25Status) p25Status->setText(QString("Auto follow TG %1").arg(followTg.talkgroupId));
                        return true;
                    }
                }
            }

            if (grantLooksPhase2 && p25IndependentTrafficEnabled) {
                if (!p25IndependentTrafficActive &&
                    p25AutoFollowWarmStandbyUntilMs > nowMs &&
                    std::isfinite(p25AutoFollowWarmStandbyVoiceHz) &&
                    std::abs(p25AutoFollowWarmStandbyVoiceHz - followTg.lastVoiceFreqHz) <= 50.0) {
                    auto& warmMgr = DeviceManager::instance();
                    const auto warmDevices = warmMgr.getDevices();
                    P25TrafficSourceSelection warmSource;
                    warmSource.valid = true;
                    warmSource.deviceIndex = 0;
                    warmSource.centerHz = p25AutoFollowWarmStandbyVoiceHz;
                    warmSource.sampleRateHz = (!warmDevices.empty() && warmDevices[0].sampleRate > 0.0)
                        ? warmDevices[0].sampleRate
                        : 2.048e6;
                    // Capture 20260712_024853: warm-standby used retunesPrimary=false, so
                    // call teardown took the dual-SDR "without RF retune" path while the
                    // single RTL was still parked on voice.  GUI then claimed CC monitor
                    // at 420.475 with cf still on the voice MHz (offset≈250 kHz).  Keep
                    // one-RTL return/hold semantics: primary is physically on voice, and
                    // startP25IndependentTrafficSource will reuse the center without an
                    // extra MHz hop when already parked there.
                    warmSource.retunesPrimary = true;
                    warmSource.sourceKind = QStringLiteral("warm-standby-same-mhz");
                    p25AutoFollowWarmStandbyUntilMs = 0;
                    p25AutoFollowWarmStandbyVoiceHz = 0.0;
                    if (startP25IndependentTrafficSource(warmSource, followTg, ccHz, nowMs)) {
                        p25IndependentTrafficRetunedPrimary = true;
                        if (p25TgFollowBtn) p25TgFollowBtn->setChecked(true);
                        appendP25LogLine(QString("P25 Phase 2 warm-standby re-follow: TG %1 slot %2 on %3MHz without CC retune.")
                            .arg(followTg.talkgroupId)
                            .arg(followTg.tdmaSlotKnown ? QString::number(followTg.tdmaSlot & 0x01u) : QString("unknown"))
                            .arg(followTg.lastVoiceFreqHz / 1e6, 0, 'f', 5));
                        appendP25LogLine(p25FollowDetailLogText(followTg));
                        return true;
                    }
                } else if (p25AutoFollowWarmStandbyUntilMs > nowMs &&
                           std::isfinite(p25AutoFollowWarmStandbyVoiceHz) &&
                           std::abs(p25AutoFollowWarmStandbyVoiceHz - followTg.lastVoiceFreqHz) > 50.0) {
                    p25AutoFollowWarmStandbyUntilMs = 0;
                    p25AutoFollowWarmStandbyVoiceHz = 0.0;
                    if (ccHz > 0.0) tuneP25Path(ccHz);
                }
                const P25TrafficSourceSelection source = selectP25IndependentTrafficSource(followTg.lastVoiceFreqHz, ccHz, true);
                if (source.valid && !source.retunesPrimary && p25IndependentTrafficActive) {
                    std::shared_ptr<Receiver> activeRx;
                    {
                        std::unique_lock<std::mutex> lk(receiversMutex, std::try_to_lock);
                        if (!lk.owns_lock()) {
                            appendP25LogLineKeyed("auto-follow-same-mhz-receivers-busy",
                                "P25 same-MHz follow deferred (receivers busy); GUI did not block.",
                                500);
                            return false;
                        }
                        const uint64_t liveGen = p25TrafficSourceGeneration.load(std::memory_order_acquire);
                        for (auto& rxPtr : receivers) {
                            if (rxPtr && rxPtr->p25IndependentTrafficSource &&
                                rxPtr->p25TrafficGeneration == liveGen &&
                                rxPtr->deviceIndex == source.deviceIndex) {
                                activeRx = rxPtr;
                                break;
                            }
                        }
                    }
                    if (activeRx) {
                        const double activeVoiceHz = activeRx->p25TrafficVoiceFreqHz > 0.0
                            ? activeRx->p25TrafficVoiceFreqHz
                            : activeRx->freqHz;
                        if (std::isfinite(activeVoiceHz) &&
                            std::abs(activeVoiceHz - followTg.lastVoiceFreqHz) <= 50.0) {
                            {
                                std::unique_lock<std::mutex> rxLock(activeRx->stateMutex, std::try_to_lock);
                                if (!rxLock.owns_lock()) {
                                    appendP25LogLineKeyed("auto-follow-same-mhz-state-busy",
                                        "P25 same-MHz follow deferred (rx state busy); GUI did not block.",
                                        500);
                                    return false;
                                }
                                p25CommitPhase2TrafficMetadataFollow(*activeRx, followTg, ccHz, nowMs);
                                activeRx->active = true;
                            }
                            p25AutoFollowLastGrantMs = nowMs;
                            p25AutoFollowLastActiveMs = nowMs;
                            p25AutoFollowVoiceFreqHz = followTg.lastVoiceFreqHz;
                            p25FollowEnabled = true;
                            p25FollowAutoActive = true;
                            p25FollowTalkgroupId = followTg.talkgroupId;
                            p25MonitoredControlFreqHz = ccHz;
                            if (p25TgFollowBtn) p25TgFollowBtn->setChecked(true);
                            appendP25LogLine(QString("P25 Phase 2 same-MHz follow: TG %1 slot %2 on %3MHz without traffic-source restart.")
                                .arg(followTg.talkgroupId)
                                .arg(followTg.tdmaSlotKnown ? QString::number(followTg.tdmaSlot & 0x01u) : QString("unknown"))
                                .arg(followTg.lastVoiceFreqHz / 1e6, 0, 'f', 5));
                            return true;
                        }
                    }
                }
                if (source.valid) {
                    if (startP25IndependentTrafficSource(source, followTg, ccHz, nowMs)) {
                        if (p25TgFollowBtn) p25TgFollowBtn->setChecked(true);
                        appendP25LogLine(p25FollowDetailLogText(followTg));
                        appendP25LogLine(QString("Auto-following P25 TG %1 with independent traffic source voice=%2MHz control=%3MHz protocol=Phase 2 TDMA enc=%4.")
                            .arg(followTg.talkgroupId)
                            .arg(followTg.lastVoiceFreqHz / 1e6, 0, 'f', 5)
                            .arg(ccHz / 1e6, 0, 'f', 5)
                            .arg(followTg.encryptionKnown ? (followTg.encrypted ? "encrypted" : "clear") : "unknown"));
                        appendP25LogLine(QString("TDMA ACQ armed on independent traffic source: TG=%1 voice=%2MHz control=%3MHz slot=%4 mask=%5 cfgSymbolRate=6000Hz postArmSettle=%6ms clearGate=target-ptt-ess unknownAudioProbe=%7.")
                            .arg(followTg.talkgroupId)
                            .arg(followTg.lastVoiceFreqHz / 1e6, 0, 'f', 5)
                            .arg(ccHz / 1e6, 0, 'f', 5)
                            .arg(followTg.tdmaSlotKnown ? QString::number(followTg.tdmaSlot & 0x01u) : QString("unknown"))
                            .arg(followTg.p25MaskParamsKnown ? "known" : "pending")
                            .arg(kP25Phase2PostArmSettleMs)
                            .arg(guiRuntimeConfig.p25LateEntryAudioProbe ? "on" : "off"));
                        return true;
                    }
                } else {
                    appendP25LogLineKeyed(QString("p25-traffic-source-unavailable:%1:%2")
                            .arg(followTg.talkgroupId)
                            .arg(static_cast<qlonglong>(std::llround(followTg.lastVoiceFreqHz))),
                        QString("P25 traffic source unavailable for TG %1 voice=%2MHz: no usable SDR source is available. Falling back to legacy retune-follow.")
                            .arg(followTg.talkgroupId)
                            .arg(followTg.lastVoiceFreqHz / 1e6, 0, 'f', 5),
                        5000);
                }
            } else if (grantLooksPhase2 && !p25IndependentTrafficEnabled) {
                appendP25LogLineKeyed("p25-traffic-source-disabled",
                    "P25 traffic source is disabled; Phase 2 grants will use physical RF retune instead of in-band DDC on the control receiver.",
                    10000);
            }

            static std::atomic_bool p25AutoFollowTransitionBusy{false};
            bool expectedTransition = false;
            if (!p25AutoFollowTransitionBusy.compare_exchange_strong(expectedTransition, true,
                    std::memory_order_acq_rel, std::memory_order_relaxed)) {
                appendP25LogLineKeyed("auto-follow-transition-busy",
                    "P25 auto-follow ignored a grant while a previous voice-follow retune is still settling.",
                    2500);
                return false;
            }
            struct AutoFollowTransitionGuard {
                std::atomic_bool& flag;
                ~AutoFollowTransitionGuard() { flag.store(false, std::memory_order_release); }
            } autoFollowTransitionGuard{p25AutoFollowTransitionBusy};

            // Drop any queued control-channel worker result before retuning to voice. Otherwise a stale
            // CC result can be consumed after the receiver has already moved to the voice channel and
            // can re-enter follow/release logic at the worst possible time.
            {
                std::lock_guard<std::mutex> pendingLock(p25ControlPendingMutex);
                p25ControlPendingResult.reset();
            }

            double liveCenterHz = 0.0;
            double liveSampleRateHz = 0.0;
            std::vector<float> livePower;
            const bool haveLiveSpectrum = DeviceManager::instance().getLatestSpectrum(0, livePower, liveCenterHz, liveSampleRateHz);
            const bool trafficInCurrentPassband = grantLooksPhase2 && haveLiveSpectrum &&
                p25TrafficInCurrentSamplePassband(followTg.lastVoiceFreqHz, liveCenterHz, liveSampleRateHz);

            if (trafficInCurrentPassband && grantLooksPhase2) {
                appendP25LogLine(QString("P25 Phase 2 grant in RF passband center=%1MHz target=%2MHz offset=%3kHz sr=%4MHz; forcing physical retune (in-band DDC on the shared control receiver does not acquire CQPSK on field captures).")
                    .arg(liveCenterHz / 1e6, 0, 'f', 5)
                    .arg(followTg.lastVoiceFreqHz / 1e6, 0, 'f', 5)
                    .arg((followTg.lastVoiceFreqHz - liveCenterHz) / 1000.0, 0, 'f', 1)
                    .arg(liveSampleRateHz / 1e6, 0, 'f', 3));
            }

            if (!tuneP25Path(followTg.lastVoiceFreqHz)) return false;

            p25AutoFollowReturnControlFreqHz = ccHz;
            p25AutoFollowVoiceFreqHz = followTg.lastVoiceFreqHz;
            p25AutoFollowTunedAtMs = nowMs;
            p25AutoFollowLastGrantMs = nowMs;
            p25AutoFollowLastActiveMs = nowMs;
            p25FollowEnabled = true;
            p25FollowAutoActive = true;
            p25FollowTalkgroupId = followTg.talkgroupId;
            p25MonitoredControlFreqHz = ccHz;
            if (p25TgFollowBtn) p25TgFollowBtn->setChecked(true);
            scheduleP25VoiceFollowArm(followTg, "auto-follow");

            appendP25LogLine(p25FollowDetailLogText(followTg));
            appendP25LogLine(QString("Auto-following P25 TG %1 voice=%2MHz control=%3MHz protocol=%4 enc=%5.")
                .arg(followTg.talkgroupId)
                .arg(followTg.lastVoiceFreqHz / 1e6, 0, 'f', 5)
                .arg(ccHz / 1e6, 0, 'f', 5)
                .arg(p25TalkgroupIsPhase2(followTg) ? "Phase 2 TDMA" : "Phase 1 FDMA")
                .arg(followTg.encryptionKnown ? (followTg.encrypted ? "encrypted" : "clear") : "unknown"));
            if (p25TalkgroupIsPhase2(followTg)) {
                appendP25LogLine(QString("TDMA ACQ armed: TG=%1 voice=%2MHz control=%3MHz slot=%4 mask=%5 cfgSymbolRate=6000Hz armDelay=%6ms postArmSettle=%7ms clearGate=target-ptt-ess unknownAudioProbe=%8.")
                    .arg(followTg.talkgroupId)
                    .arg(followTg.lastVoiceFreqHz / 1e6, 0, 'f', 5)
                    .arg(ccHz / 1e6, 0, 'f', 5)
                    .arg(followTg.tdmaSlotKnown ? QString::number(followTg.tdmaSlot & 0x01u) : QString("unknown"))
                    .arg(followTg.p25MaskParamsKnown ? "known" : "pending")
                    .arg(kP25Phase2ArmDelayMs)
                    .arg(kP25Phase2PostArmSettleMs)
                    .arg(guiRuntimeConfig.p25LateEntryAudioProbe ? "on" : "off"));
            }
            if (p25Status) p25Status->setText(QString("Auto follow TG %1").arg(followTg.talkgroupId));
            return true;
        };

        auto rememberPendingP25VoiceGrant = [this](const P25ControlEvent& ev, int correctedDibitErrors, qint64 nowMs) {
            if (!p25RememberPendingVoiceGrant(p25PendingVoiceGrants, ev, correctedDibitErrors, nowMs)) return;

            const uint8_t id = static_cast<uint8_t>((ev.channel >> 12) & 0x0f);
            appendP25LogLineKeyed(QString("pending-unresolved-grant:%1:%2")
                    .arg(ev.talkgroupId)
                    .arg(ev.channel),
                QString("Voice grant pending: TG=%1 CH=%2 needs identifier table ID %3 before auto-follow can tune.")
                    .arg(ev.talkgroupId)
                    .arg(p25ChannelText(ev.channel))
                    .arg(static_cast<int>(id)),
                5000);
        };

        auto tryResolvePendingP25VoiceGrants = [this, refreshP25Talkgroups, autoFollowP25Grant](qint64 nowMs, const QString& reason) {
            if (p25PendingVoiceGrants.empty()) return;
            auto talkgroups = loadP25Talkgroups();
            bool registryChanged = false;
            for (const auto& resolved : p25ResolvePendingVoiceGrants(p25PendingVoiceGrants, p25LiveControlAnalyzer, nowMs)) {
                appendP25LogLine(QString("Resolved pending P25 voice grant after %1: %2")
                    .arg(reason)
                    .arg(p25GrantDetailLogText(resolved)));
                const bool merged = mergeP25TalkgroupEvent(talkgroups, p25MonitoredControlFreqHz, resolved, nowMs);
                registryChanged = merged || registryChanged;
                if (p25AutoFollowEnabled && p25ControlEventIsResolvedVoiceGrant(resolved)) {
                    auto tgIt = std::find_if(talkgroups.begin(), talkgroups.end(), [&](const P25TalkgroupEntry& tg) {
                        return sameP25Talkgroup(tg, p25MonitoredControlFreqHz, resolved.talkgroupId);
                    });
                    if (tgIt != talkgroups.end()) autoFollowP25Grant(*tgIt, resolved, nowMs);
                }
            }
            if (registryChanged) {
                saveP25Talkgroups(talkgroups);
                refreshP25Talkgroups();
            }
        };

        auto refreshP25 = [this, p25Table, p25Status, refreshP25Talkgroups]() {
            auto& mgr = DeviceManager::instance();
            std::vector<float> pwr;
            double cf = 0.0, sr = 0.0;
            bool got = false;
            for (size_t i = 0; i < mgr.getDevices().size(); ++i) {
                if (mgr.isStreaming(i) && mgr.getLatestSpectrum(i, pwr, cf, sr) && !pwr.empty() && sr > 0.0) {
                    got = true;
                    break;
                }
            }
            if (!got) {
                const auto known = loadP25KnownControlChannels();
                populateP25Table(p25Table, {}, known);
                p25Status->setText("No live spectrum");
                refreshP25Talkgroups();
                appendP25LogLine(QString("P25 scan refresh: no live spectrum available; showing %1 known CC%2.")
                    .arg(known.size())
                    .arg(known.size() == 1 ? "" : "s"));
                return;
            }
            auto hits = detectP25ControlCandidates(pwr, sr, cf);
            const auto known = loadP25KnownControlChannels();
            populateP25Table(p25Table, hits, known);
            refreshP25Talkgroups();
            p25Status->setText(QString("%1 candidate%2")
                .arg(hits.size())
                .arg(hits.size() == 1 ? "" : "s"));
            appendP25LogLine(QString("P25 scan refresh: cf=%1MHz sr=%2MHz candidates=%3 known=%4")
                .arg(cf / 1e6, 0, 'f', 5)
                .arg(sr / 1e6, 0, 'f', 3)
                .arg(hits.size())
                .arg(known.size()));
        };

        connect(p25RefreshBtn, &QPushButton::clicked, this, refreshP25);
        connect(p25KnownBtn, &QPushButton::clicked, this, [this, p25Table, p25Status, refreshP25]() {
            double defaultMhz = 0.0;
            {
                std::lock_guard<std::mutex> lk(monitorParamsMutex);
                defaultMhz = currentMonitorFreq / 1e6;
            }
            if (p25Table && p25Table->currentRow() >= 0 && p25Table->item(p25Table->currentRow(), 0)) {
                bool okRow = false;
                const double rowMhz = p25Table->item(p25Table->currentRow(), 0)->text().toDouble(&okRow);
                if (okRow && rowMhz > 0.0) defaultMhz = rowMhz;
            }

            bool ok = false;
            const double mhz = QInputDialog::getDouble(this, "Add P25 Control Channel",
                "Control channel frequency (MHz):", defaultMhz, 20.0, 6000.0, 5, &ok);
            if (!ok || mhz <= 0.0) return;
            const QString defaultLabel = QString("CC %1 MHz").arg(mhz, 0, 'f', 5);
            const QString label = QInputDialog::getText(this, "P25 Control Channel Label",
                "Label:", QLineEdit::Normal, defaultLabel, &ok);
            if (!ok) return;

            if (upsertP25KnownControlChannel(mhz * 1e6, label.toStdString())) {
                refreshP25();
                const int rows = p25Table ? p25Table->rowCount() : 0;
                for (int row = 0; row < rows; ++row) {
                    auto* item = p25Table->item(row, 0);
                    if (!item) continue;
                    const double rowHz = item->data(Qt::UserRole).toDouble();
                    if (std::abs(rowHz - mhz * 1e6) <= 50.0) {
                        p25Table->selectRow(row);
                        break;
                    }
                }
                if (p25Status) p25Status->setText(QString("Known CC %1 MHz added").arg(mhz, 0, 'f', 5));
                appendP25LogLine(QString("Known P25 control channel saved: %1MHz label=\"%2\"")
                    .arg(mhz, 0, 'f', 5)
                    .arg(label));
            }
        });
        connect(p25LogBtn, &QPushButton::clicked, this, [this]() { showP25LogWindow(); });
        connect(p25TgRefreshBtn, &QPushButton::clicked, this, refreshP25Talkgroups);
        connect(p25TgDeleteBtn, &QPushButton::clicked, this,
            [this, p25TgTable, p25TgFollowBtn, refreshP25Talkgroups, clearP25VoiceFollowState]() {
                const int row = p25TgTable ? p25TgTable->currentRow() : -1;
                auto talkgroups = loadP25Talkgroups();
                if (row < 0 || row >= static_cast<int>(talkgroups.size())) return;
                const auto removed = talkgroups[static_cast<size_t>(row)];
                const auto answer = QMessageBox::question(this,
                    "Delete P25 Talkgroup",
                    QString("Delete TG %1 from the local registry?").arg(removed.talkgroupId));
                if (answer != QMessageBox::Yes) return;
                talkgroups.erase(talkgroups.begin() + row);
                saveP25Talkgroups(talkgroups);
                if (p25FollowTalkgroupId == removed.talkgroupId) {
                    p25FollowEnabled = false;
                    p25FollowAutoActive = false;
                    p25FollowTalkgroupId = 0;
                    if (p25TgFollowBtn) p25TgFollowBtn->setChecked(false);
                    clearP25VoiceFollowState();
                    appendP25LogLine(QString("Stopped follow because TG %1 was deleted.").arg(removed.talkgroupId));
                }
                refreshP25Talkgroups();
                if (p25TgTable && row < p25TgTable->rowCount()) p25TgTable->selectRow(row);
                statusBar()->showMessage(QString("Deleted P25 TG %1").arg(removed.talkgroupId), 2000);
            });
        connect(p25ScanBtn, &QPushButton::toggled, this, [this, p25Status](bool on) {
            p25Status->setText(on ? "Scanning" : "Idle");
            appendP25LogLine(on ? "P25 candidate scan enabled." : "P25 candidate scan disabled.");
        });
        connect(p25AutoFollowCheck, &QCheckBox::toggled, this,
            [this, p25Status, returnP25AutoFollowToControl](bool on) {
                p25AutoFollowEnabled = on;
                appendP25LogLine(on
                    ? "P25 auto-follow enabled; followable grants will tune to voice and return to the control channel after activity drops."
                    : "P25 auto-follow disabled.");
                if (!on && p25FollowAutoActive) {
                    returnP25AutoFollowToControl();
                } else if (p25Status) {
                    p25Status->setText(on ? "Auto follow armed" : "Auto follow off");
                }
            });
        connect(p25IndependentTrafficCheck, &QCheckBox::toggled, this,
            [this, returnP25AutoFollowToControl](bool on) {
                p25IndependentTrafficEnabled = on;
                QSettings().setValue("p25/independentTrafficSource", on);
                appendP25LogLine(on
                    ? "P25 traffic source enabled: grants will create/reuse a traffic-channel receiver. One RTL-SDR is supported by DDC when in-passband or fast retune when out-of-passband."
                    : "P25 traffic source disabled: auto-follow will use the older direct scanner-follow path.");
                if (!on && p25IndependentTrafficActive) {
                    returnP25AutoFollowToControl();
                }
            });
        connect(p25MonitorBtn, &QPushButton::clicked, this, [this, p25Status, selectedP25ControlHz, tuneP25Path, clearP25VoiceFollowState, setP25ControlChannelMute]() {
            const double ccHz = selectedP25ControlHz();
            if (!tuneP25Path(ccHz)) return;
            p25MonitoredControlFreqHz = ccHz;
            p25FollowEnabled = false;
            p25FollowAutoActive = false;
            p25FollowTalkgroupId = 0;
            p25AutoFollowReturnControlFreqHz = ccHz;
            p25AutoFollowVoiceFreqHz = 0.0;
            p25AutoFollowTunedAtMs = 0;
            p25AutoFollowLastGrantMs = 0;
            p25AutoFollowLastActiveMs = 0;
            p25AutoFollowLastMHzHopMs = 0;
            p25LiveDecoder.reset();
            // Do not block the Qt/UI thread behind an in-flight P25 control decode.
            // Request a reset and let the worker apply it when it next owns the decoder.
            p25ControlWorkerResetPending.store(true, std::memory_order_release);
            { std::lock_guard<std::mutex> pendingLock(p25ControlPendingMutex); p25ControlPendingResult.reset(); }
            p25LiveControlAnalyzer.reset();
            p25PendingVoiceGrants.clear();
            p25RepeatedVoiceGrants.clear();
            const size_t seededIdentifiers = seedP25AnalyzerFromCachedChannelIdentifiers(p25LiveControlAnalyzer, ccHz);
            p25LastDiagSignature.clear();
            clearP25VoiceFollowState();
            p25IndependentTrafficActive = false;
            p25IndependentTrafficRetunedPrimary = false;
            setP25ControlChannelMute(true);
            appendP25LogLine(QString("Monitoring muted P25 control channel target=%1MHz.").arg(ccHz / 1e6, 0, 'f', 5));
            if (seededIdentifiers > 0) {
                appendP25LogLine(QString("Seeded %1 cached P25 channel identifier table(s) for %2MHz.")
                    .arg(static_cast<qulonglong>(seededIdentifiers))
                    .arg(ccHz / 1e6, 0, 'f', 5));
            }
            p25Status->setText(QString("Monitoring CC %1 MHz").arg(ccHz / 1e6, 0, 'f', 5));
            statusBar()->showMessage(QString("Monitoring muted P25 control channel %1 MHz").arg(ccHz / 1e6, 0, 'f', 5), 2500);
        });
        connect(p25GrantTestBtn, &QPushButton::clicked, this, [this, p25Status, p25AutoFollowCheck, selectedP25ControlHz, tuneP25Path, clearP25VoiceFollowState, setP25ControlChannelMute]() {
            const double ccHz = selectedP25ControlHz();
            if (!tuneP25Path(ccHz)) return;
            p25MonitoredControlFreqHz = ccHz;
            p25AutoFollowReturnControlFreqHz = ccHz;
            p25AutoFollowVoiceFreqHz = 0.0;
            p25AutoFollowTunedAtMs = 0;
            p25AutoFollowLastGrantMs = 0;
            p25AutoFollowLastActiveMs = 0;
            p25AutoFollowLastMHzHopMs = 0;
            p25FollowEnabled = false;
            p25FollowAutoActive = false;
            p25FollowTalkgroupId = 0;
            p25LiveDecoder.reset();
            // Do not block the Qt/UI thread behind an in-flight P25 control decode.
            // Request a reset and let the worker apply it when it next owns the decoder.
            p25ControlWorkerResetPending.store(true, std::memory_order_release);
            { std::lock_guard<std::mutex> pendingLock(p25ControlPendingMutex); p25ControlPendingResult.reset(); }
            p25LiveControlAnalyzer.reset();
            p25PendingVoiceGrants.clear();
            p25RepeatedVoiceGrants.clear();
            const size_t seededIdentifiers = seedP25AnalyzerFromCachedChannelIdentifiers(p25LiveControlAnalyzer, ccHz);
            p25LastDiagSignature.clear();
            clearP25VoiceFollowState();
            p25IndependentTrafficActive = false;
            p25IndependentTrafficRetunedPrimary = false;
            setP25ControlChannelMute(true);
            p25AutoFollowEnabled = true;
            if (p25AutoFollowCheck && !p25AutoFollowCheck->isChecked()) {
                p25AutoFollowCheck->blockSignals(true);
                p25AutoFollowCheck->setChecked(true);
                p25AutoFollowCheck->blockSignals(false);
            }
            showP25LogWindow();
            appendP25LogLine(QString("Grant test armed: monitoring muted P25 control channel %1MHz; auto-follow will tune followable grants and log voice/audio gates.")
                .arg(ccHz / 1e6, 0, 'f', 5));
            if (seededIdentifiers > 0) {
                appendP25LogLine(QString("Seeded %1 cached P25 channel identifier table(s) for grant test.")
                    .arg(static_cast<qulonglong>(seededIdentifiers)));
            }
            if (p25Status) p25Status->setText(QString("Grant test %1 MHz").arg(ccHz / 1e6, 0, 'f', 5));
            statusBar()->showMessage(QString("P25 grant test armed on %1 MHz").arg(ccHz / 1e6, 0, 'f', 5), 2500);
        });
        connect(p25Table, &QTableWidget::cellDoubleClicked, this, [monFreq, setMonBtn, p25Table](int row, int) {
            auto* item = p25Table->item(row, 0);
            if (!item) return;
            bool ok = false;
            double mhz = item->text().toDouble(&ok);
            if (!ok) return;
            monFreq->setValue(mhz);
            setMonBtn->click();
        });
        connect(p25TgManualBtn, &QPushButton::clicked, this, [this, p25Table, p25TgTable, refreshP25Talkgroups]() {
            double controlHz = 0.0;
            const int ccRow = p25Table ? p25Table->currentRow() : -1;
            if (ccRow >= 0 && p25Table && p25Table->item(ccRow, 0)) {
                bool okFreq = false;
                const double mhz = p25Table->item(ccRow, 0)->text().toDouble(&okFreq);
                if (okFreq) controlHz = mhz * 1e6;
            }
            if (controlHz <= 0.0) {
                std::lock_guard<std::mutex> lk(monitorParamsMutex);
                controlHz = currentMonitorFreq;
            }

            bool ok = false;
            const int tgid = QInputDialog::getInt(this, "Add P25 Talkgroup", "Talkgroup ID:", 1, 1, 16777215, 1, &ok);
            if (!ok) return;
            const QString defaultTag = QString("TG %1").arg(tgid);
            const QString tag = QInputDialog::getText(this, "Talkgroup Alpha Tag", "Alpha tag:", QLineEdit::Normal, defaultTag, &ok);
            if (!ok) return;

            auto talkgroups = loadP25Talkgroups();
            const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
            auto it = std::find_if(talkgroups.begin(), talkgroups.end(), [&](const P25TalkgroupEntry& tg) {
                return sameP25Talkgroup(tg, controlHz, static_cast<uint32_t>(tgid));
            });
            if (it == talkgroups.end()) {
                P25TalkgroupEntry entry;
                entry.controlFreqHz = controlHz;
                entry.talkgroupId = static_cast<uint32_t>(tgid);
                entry.alphaTag = trimCopy(tag.toStdString());
                entry.verified = true;
                entry.firstSeenMs = nowMs;
                entry.lastSeenMs = nowMs;
                talkgroups.push_back(entry);
            } else {
                it->alphaTag = trimCopy(tag.toStdString());
                it->verified = true;
                it->lastSeenMs = nowMs;
            }
            saveP25Talkgroups(talkgroups);
            refreshP25Talkgroups();
            if (p25TgTable) p25TgTable->selectRow(static_cast<int>(talkgroups.size()) - 1);
            statusBar()->showMessage(QString("P25 TG %1 saved for CC %2 MHz").arg(tgid).arg(controlHz / 1e6, 0, 'f', 5), 2500);
        });
        connect(p25TgVerifyBtn, &QPushButton::clicked, this, [this, p25TgTable, refreshP25Talkgroups]() {
            const int row = p25TgTable ? p25TgTable->currentRow() : -1;
            auto talkgroups = loadP25Talkgroups();
            if (row < 0 || row >= static_cast<int>(talkgroups.size())) return;
            talkgroups[static_cast<size_t>(row)].verified = true;
            talkgroups[static_cast<size_t>(row)].lastSeenMs = QDateTime::currentMSecsSinceEpoch();
            saveP25Talkgroups(talkgroups);
            refreshP25Talkgroups();
            if (p25TgTable) p25TgTable->selectRow(row);
            statusBar()->showMessage(QString("Verified P25 TG %1").arg(talkgroups[static_cast<size_t>(row)].talkgroupId), 2000);
        });
        connect(p25TgScannerBtn, &QPushButton::clicked, this, [this, p25TgTable, savedTable, refreshP25Talkgroups]() {
            const int row = p25TgTable ? p25TgTable->currentRow() : -1;
            auto talkgroups = loadP25Talkgroups();
            if (row < 0 || row >= static_cast<int>(talkgroups.size())) return;
            auto& tg = talkgroups[static_cast<size_t>(row)];
            if (tg.encryptionKnown && tg.encrypted) {
                statusBar()->showMessage(QString("P25 TG %1 is encrypted; not adding to scanner").arg(tg.talkgroupId), 3500);
                return;
            }
            tg.verified = true;
            tg.scannerEnabled = true;
            tg.lastSeenMs = QDateTime::currentMSecsSinceEpoch();
            saveP25Talkgroups(talkgroups);

            // If a voice frequency was decoded, also expose it in the existing saved-frequency list.
            // The real trunking scanner uses TGID+CC; this saved row is a useful quick-tune fallback.
            if (tg.lastVoiceFreqHz > 0.0) {
                auto freqs = loadSavedFrequencies();
                const bool exists = std::any_of(freqs.begin(), freqs.end(), [&](const SavedFrequency& sf) {
                    return std::abs(sf.freqHz - tg.lastVoiceFreqHz) <= 50.0 && sf.tags.find("p25") != std::string::npos;
                });
                if (!exists) {
                    SavedFrequency sf;
                    sf.name = tg.alphaTag.empty()
                        ? ("P25 TG " + std::to_string(tg.talkgroupId))
                        : tg.alphaTag;
                    sf.freqHz = tg.lastVoiceFreqHz;
                    sf.mode = DemodMode::NFM;
                    sf.bandwidthHz = 12500.0;
                    sf.lpfHz = 3000.0;
                    sf.lpfEnabled = false;
                    sf.squelchDb = -105.0;
                    sf.tags = p25TalkgroupIsPhase2(tg)
                        ? "p25,phase2,tdma,talkgroup,scanner"
                        : "p25,phase1,talkgroup,scanner";
                    freqs.push_back(sf);
                    saveSavedFrequencies(freqs);
                    populateSavedFrequencyTable(savedTable, freqs);
                }
            }

            refreshP25Talkgroups();
            if (p25TgTable) p25TgTable->selectRow(row);
            statusBar()->showMessage(QString("Added P25 TG %1 to scanner list").arg(tg.talkgroupId), 2500);
        });
        connect(p25TgPriorityBtn, &QPushButton::clicked, this, [this, p25TgTable, refreshP25Talkgroups]() {
            const int row = p25TgTable ? p25TgTable->currentRow() : -1;
            auto talkgroups = loadP25Talkgroups();
            if (row < 0 || row >= static_cast<int>(talkgroups.size())) return;
            auto& tg = talkgroups[static_cast<size_t>(row)];
            bool ok = false;
            const int pri = QInputDialog::getInt(
                this,
                "Talkgroup Priority",
                QString("User priority for TG %1 (higher preempts lower; 0=default):").arg(tg.talkgroupId),
                tg.userPriority,
                0,
                1000,
                1,
                &ok);
            if (!ok) return;
            tg.userPriority = pri;
            tg.lastSeenMs = QDateTime::currentMSecsSinceEpoch();
            saveP25Talkgroups(talkgroups);
            refreshP25Talkgroups();
            if (p25TgTable) p25TgTable->selectRow(row);
            statusBar()->showMessage(QString("P25 TG %1 userPriority=%2").arg(tg.talkgroupId).arg(pri), 2500);
        });
        connect(p25TgFollowBtn, &QPushButton::clicked, this, [this, p25TgFollowBtn, p25TgTable, p25Status, tuneP25Path, clearP25VoiceFollowState, scheduleP25VoiceFollowArm]() {
            if (!p25TgFollowBtn->isChecked()) {
                p25FollowEnabled = false;
                p25FollowAutoActive = false;
                p25FollowTalkgroupId = 0;
                clearP25VoiceFollowState();
                appendP25LogLine("P25 talkgroup follow disabled.");
                p25Status->setText("Follow off");
                return;
            }

            const int row = p25TgTable ? p25TgTable->currentRow() : -1;
            auto talkgroups = loadP25Talkgroups();
            if (row < 0 || row >= static_cast<int>(talkgroups.size())) {
                p25TgFollowBtn->setChecked(false);
                p25Status->setText("Select a TG first");
                return;
            }

            auto tg = talkgroups[static_cast<size_t>(row)];
            p25AugmentTalkgroupFromKnownSite(tg, talkgroups, tg.controlFreqHz);
            if (tg.encryptionKnown && tg.encrypted && !p25TalkgroupIsPhase2(tg)) {
                p25TgFollowBtn->setChecked(false);
                p25Status->setText(QString("TG %1 encrypted").arg(tg.talkgroupId));
                statusBar()->showMessage(QString("Skipping encrypted P25 TG %1").arg(tg.talkgroupId), 3500);
                return;
            }
            const bool manualEncryptedPhase2Follow =
                tg.encryptionKnown && tg.encrypted && p25TalkgroupIsPhase2(tg);
            if (manualEncryptedPhase2Follow) {
                // Tune for MAC/ESS confirmation, but keep encryptionKnown so the
                // speaker gate stays fail-closed (no speculative clear audio).
                appendP25LogLine(QString("Manual follow of encrypted Phase 2 TG %1; audio remains muted until a clear grant or clear ESS/PTT proves otherwise.")
                    .arg(tg.talkgroupId));
            }
            if (!manualEncryptedPhase2Follow && !p25TalkgroupCanTuneForFollow(tg)) {
                p25TgFollowBtn->setChecked(false);
                p25Status->setText(QString("TG %1 clear state unknown").arg(tg.talkgroupId));
                statusBar()->showMessage("Waiting for a clear P25 voice grant before decoding audio for this talkgroup.", 4500);
                return;
            }
            if (tg.lastVoiceFreqHz <= 0.0) {
                p25TgFollowBtn->setChecked(false);
                p25Status->setText(QString("TG %1 waiting for voice grant").arg(tg.talkgroupId));
                statusBar()->showMessage("No active voice frequency for this talkgroup yet. Keep decoding the control channel until a grant appears.", 4500);
                return;
            }

            {
                std::lock_guard<std::mutex> pendingLock(p25ControlPendingMutex);
                p25ControlPendingResult.reset();
            }
            if (!tuneP25Path(tg.lastVoiceFreqHz)) {
                p25TgFollowBtn->setChecked(false);
                return;
            }

            p25FollowEnabled = true;
            p25FollowAutoActive = false;
            p25FollowTalkgroupId = tg.talkgroupId;
            p25MonitoredControlFreqHz = tg.controlFreqHz;
            p25AutoFollowReturnControlFreqHz = tg.controlFreqHz;
            p25AutoFollowVoiceFreqHz = tg.lastVoiceFreqHz;
            scheduleP25VoiceFollowArm(tg, "manual-follow");
            appendP25LogLine(p25FollowDetailLogText(tg));
            appendP25LogLine(QString("Following P25 TG %1 voice=%2MHz control=%3MHz protocol=%4 enc=%5.")
                .arg(tg.talkgroupId)
                .arg(tg.lastVoiceFreqHz / 1e6, 0, 'f', 5)
                .arg(tg.controlFreqHz / 1e6, 0, 'f', 5)
                .arg(p25TalkgroupIsPhase2(tg) ? "Phase 2 TDMA" : "Phase 1 FDMA")
                .arg(tg.encryptionKnown ? (tg.encrypted ? "encrypted" : "clear") : "unknown"));
            p25Status->setText(QString("Following TG %1").arg(tg.talkgroupId));
            statusBar()->showMessage(QString("Following P25 TG %1 at %2 MHz with %3 voice decode%4.")
                .arg(tg.talkgroupId)
                .arg(tg.lastVoiceFreqHz / 1e6, 0, 'f', 5)
                .arg(p25TalkgroupIsPhase2(tg) ? "AMBE" : "IMBE")
                .arg(tg.encryptionKnown ? "" : " gated until clear state is proven"), 6500);
        });

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

        connect(captureTrainingBtn, &QPushButton::clicked, this, [this, trainingStatus]() {
            double freqHz = 100e6;
            DemodMode mode = DemodMode::AUTO;
            {
                std::lock_guard<std::mutex> lk(monitorParamsMutex);
                freqHz = currentMonitorFreq;
                mode = currentMonitorMode;
            }
            const QString defaultLabel = QString("%1_%2MHz")
                .arg(modeToQString(mode))
                .arg(freqHz / 1e6, 0, 'f', 5);
            bool ok = false;
            const QString label = QInputDialog::getText(this, "Capture Training Sample", "Label:", QLineEdit::Normal, defaultLabel, &ok);
            if (!ok) return;
            trainingStatus->setText("Training: saving...");
            const auto result = captureTrainingSample(label.toStdString());
            trainingStatus->setText(result.ok ? QString("Training: saved") : QString("Training: failed"));
            if (result.ok) {
                statusBar()->showMessage(result.message + "  " + result.directory, 6000);
                QMessageBox::information(this, "Training Capture Saved", result.message + "\n\n" + result.directory);
            } else {
                statusBar()->showMessage(result.message, 6000);
                QMessageBox::warning(this, "Training Capture Failed", result.message);
            }
        });

        connect(captureIqStartBtn, &QPushButton::clicked, this, [this, captureIqStartBtn, captureIqStopBtn, trainingStatus]() {
            double freqHz = 100e6;
            DemodMode mode = DemodMode::AUTO;
            {
                std::lock_guard<std::mutex> lk(monitorParamsMutex);
                freqHz = currentMonitorFreq;
                mode = currentMonitorMode;
            }
            const QString defaultLabel = QString("iq_%1_%2MHz")
                .arg(modeToQString(mode))
                .arg(freqHz / 1e6, 0, 'f', 5);
            bool ok = false;
            const QString label = QInputDialog::getText(this, "Start IQ Capture", "Capture label:", QLineEdit::Normal, defaultLabel, &ok);
            if (!ok) return;

            const auto result = startLiveIqCapture(label.toStdString());
            if (result.ok) {
                captureIqStartBtn->setEnabled(false);
                captureIqStopBtn->setEnabled(true);
                trainingStatus->setText("IQ: recording...");
                statusBar()->showMessage(result.message + "  " + result.directory, 8000);
                appendP25LogLine(QString("Start/Stop IQ capture active: %1").arg(result.directory));
            } else {
                trainingStatus->setText("IQ: start failed");
                statusBar()->showMessage(result.message, 8000);
                QMessageBox::warning(this, "IQ Capture Start Failed", result.message);
            }
        });

        connect(captureIqStopBtn, &QPushButton::clicked, this, [this, captureIqStartBtn, captureIqStopBtn, trainingStatus]() {
            trainingStatus->setText("IQ: finalizing...");
            const auto result = stopLiveIqCapture();
            captureIqStartBtn->setEnabled(true);
            captureIqStopBtn->setEnabled(false);
            trainingStatus->setText(result.ok ? QString("IQ: saved") : QString("IQ: stop failed"));
            if (result.ok) {
                appendP25LogLine(QString("Start/Stop IQ capture finalized: %1").arg(result.directory));
                statusBar()->showMessage(result.message + "  " + result.directory, 10000);
                QMessageBox::information(this, "IQ Capture Saved", result.message + "\n\n" + result.directory);
            } else {
                appendP25LogLine(QString("Start/Stop IQ capture finalize failed: %1").arg(result.message));
                statusBar()->showMessage(result.message, 10000);
                QMessageBox::warning(this, "IQ Capture Stop Failed", result.message);
            }
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

        connect(autoBwBtn, &QPushButton::clicked, this, [this]() {
            auto& mgr = DeviceManager::instance();
            std::vector<float> pwr;
            double cf = 0.0, sr = 0.0;
            bool got = false;
            for (size_t i = 0; i < mgr.getDevices().size(); ++i) {
                if (mgr.isStreaming(i) && mgr.getLatestSpectrum(i, pwr, cf, sr) && !pwr.empty() && sr > 0.0) {
                    got = true;
                    break;
                }
            }
            if (!got) return;

            DemodMode mode;
            double freq;
            {
                std::lock_guard<std::mutex> lk(monitorParamsMutex);
                freq = currentMonitorFreq;
                mode = currentMonitorMode;
            }
            auto smart = chooseSmartModeAndBandwidth(pwr, sr, cf, freq, mode);
            double snapped = smart.bandwidthHz;
            double lpf = std::clamp(smart.lpfHz, 100.0, 200000.0);

            {
                std::lock_guard<std::mutex> lk(monitorParamsMutex);
                if (mode == DemodMode::AUTO) currentMonitorMode = smart.mode;
                monitorChannelBwHz = snapped;
                monitorLpfHz = lpf;
            }
            syncMonitorVarsToReceiver(0);
            if (bwSpin) {
                bwSpin->blockSignals(true);
                bwSpin->setValue(snapped / 1000.0);
                bwSpin->blockSignals(false);
            }
            if (lpfSpin) {
                lpfSpin->blockSignals(true);
                lpfSpin->setValue(lpf / 1000.0);
                lpfSpin->blockSignals(false);
            }
            statusBar()->showMessage(QString("Auto BW: %1 kHz (%2, %3%, %4)")
                .arg(snapped / 1000.0, 0, 'f', 1)
                .arg(QString::fromStdString(smart.source))
                .arg(smart.classifier.confidence * 100.0, 0, 'f', 0)
                .arg(QString::fromStdString(classifierFilterKindToString(smart.classifier.filterKind))), 3000);
        });

        connect(lpfEnableCheck, &QCheckBox::toggled, this, [this](bool enabled) {
            {
                std::lock_guard<std::mutex> lk(monitorParamsMutex);
                monitorAudioLpfEnabled = enabled;
            }
            if (lpfSpin) lpfSpin->setEnabled(enabled);
            syncMonitorVarsToReceiver(0);
        });

        connect(lpfSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v) {
            std::lock_guard<std::mutex> lk(monitorParamsMutex);
            monitorLpfHz = std::clamp(v * 1000.0, 100.0, 200000.0);
            syncMonitorVarsToReceiver(0);
        });

        connect(modeBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, modeBox](int) {
            // P1 audit: do NOT call bwSpin->setValue() while holding monitorParamsMutex.
            // setValue synchronously emits valueChanged whose handler also locks the same mutex -> deadlock/stall risk on live mode transitions.
            // Solution: compute desired state, lock only for the shared vars, then blockSignals + set spin (no emit) after unlock.
            QString m = modeBox->currentText();
            bool newAuto = false;
            DemodMode newMode = DemodMode::NFM;
            if (m == "AUTO") { newAuto = true; newMode = DemodMode::AUTO; }
            else if (m == "NFM") { newAuto = false; newMode = DemodMode::NFM; }
            else if (m == "WFM") { newAuto = false; newMode = DemodMode::WFM; }
            else if (m == "AM") { newAuto = false; newMode = DemodMode::AM; }
            else if (m == "USB" || m == "LSB" || m == "CW") {
                newAuto = false;
                newMode = (m == "USB") ? DemodMode::USB : (m == "LSB" ? DemodMode::LSB : DemodMode::CW);
            }
            double tunedHz = currentMonitorFreq;
            {
                std::lock_guard<std::mutex> lk(monitorParamsMutex);
                tunedHz = currentMonitorFreq;
            }
            const auto* plan = findBandPlanForFrequency(tunedHz);
            const double newBwHz = (plan && (newMode == DemodMode::AUTO || newMode == plan->mode))
                ? plan->bandwidthHz
                : defaultBandwidthForMode(newMode);
            const double newBwK = newBwHz / 1000.0;
            const double newLpfHz = (plan && (newMode == DemodMode::AUTO || newMode == plan->mode))
                ? plan->lpfHz
                : lpfForModeAndBandwidth(newMode, newBwHz);
            {
                std::lock_guard<std::mutex> lk(monitorParamsMutex);
                autoDetectMode = newAuto;
                currentMonitorMode = newMode;
                monitorChannelBwHz = newBwHz;
                monitorLpfHz = newLpfHz;
            }
            syncMonitorVarsToReceiver(0);
            if (bwSpin) {
                bwSpin->blockSignals(true);
                bwSpin->setValue(newBwK);
                bwSpin->blockSignals(false);
            }
            if (lpfSpin) {
                lpfSpin->blockSignals(true);
                lpfSpin->setValue(newLpfHz / 1000.0);
                lpfSpin->blockSignals(false);
            }
        });

        connect(setMonBtn, &QPushButton::clicked, this, [this, monFreq]() {
            classifierRoiBuilder.clear();
            const double tunedHz = monFreq->value() * 1e6;
            const BandPlanEntry* plan = autoDetectMode ? findBandPlanForFrequency(tunedHz) : nullptr;
            {
                std::lock_guard<std::mutex> lk(monitorParamsMutex);
                currentMonitorFreq = tunedHz;
                if (plan) {
                    currentMonitorMode = plan->mode;
                    monitorChannelBwHz = plan->bandwidthHz;
                    monitorLpfHz = plan->lpfHz;
                }
            }
            if (plan && bwSpin) {
                bwSpin->blockSignals(true);
                bwSpin->setValue(plan->bandwidthHz / 1000.0);
                bwSpin->blockSignals(false);
            }
            if (plan && lpfSpin) {
                lpfSpin->blockSignals(true);
                lpfSpin->setValue(plan->lpfHz / 1000.0);
                lpfSpin->blockSignals(false);
            }
            syncMonitorVarsToReceiver(0);
            setReceiverActive(0, true);
            auto& mgr = DeviceManager::instance();
            bool any = false;
            for (size_t i=0; i<mgr.getDevices().size(); ++i) {
                if (mgr.isStreaming(i)) {
                    mgr.setCenterFreq(i, tunedHz);
                    statusBar()->showMessage(QString("Monitor tuned to %1 MHz").arg(tunedHz/1e6,0,'f',3), 2000);
                    any = true;
                    break;
                }
            }
            if (!any && !mgr.getDevices().empty()) {
                // Auto-start on explicit tune request so user gets audio without separate "Add" click.
                mgr.setEnabled(0, true);
                mgr.startStreaming(0, true /* real from SDR */);
                mgr.setCenterFreq(0, tunedHz);
                statusBar()->showMessage(QString("Started monitor + tuned to %1 MHz").arg(tunedHz/1e6,0,'f',3), 2500);
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

        // Decode Log / voice-to-text hub (P25 STT first; AM/ADS-B/ACARS/POCSAG plug in later).
        m_transcriptHub = new TranscriptHub(this);
        m_sttEngine = new SttEngine(this);
        m_p25TranscriptSource = new P25TranscriptSource(m_transcriptHub, m_sttEngine, this);
        m_p25TranscriptSource->installAsGlobalTap();
        m_transcriptHub->registerSource({QStringLiteral("am"), QStringLiteral("AM Voice"),
                                         TranscriptCategory::VoiceStt, false});
        m_transcriptHub->registerSource({QStringLiteral("adsb"), QStringLiteral("ADS-B"),
                                         TranscriptCategory::Adsb, false});
        m_transcriptHub->registerSource({QStringLiteral("acars"), QStringLiteral("ACARS"),
                                         TranscriptCategory::Acars, false});
        m_transcriptHub->registerSource({QStringLiteral("pocsag"), QStringLiteral("POCSAG"),
                                         TranscriptCategory::Pocsag, false});
        m_transcriptHub->registerSource({QStringLiteral("system"), QStringLiteral("System"),
                                         TranscriptCategory::System, true});
        m_sttEngine->start();
        m_transcriptHub->appendSystem(
            QStringLiteral("Decode Log ready. P25 clear speaker PCM is tapped for STT. "
                           "Other sources (AM / ADS-B / ACARS / POCSAG) can register later."));

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
        connect(updateTimer, &QTimer::timeout, this, [this, spectrum, p25ScanBtn, p25Table, p25Status, classifierStatus,
                                                      refreshP25Talkgroups, autoFollowP25Grant, returnP25AutoFollowToControl,
                                                      expireP25WarmStandbyIfNeeded,
                                                      rememberPendingP25VoiceGrant, tryResolvePendingP25VoiceGrants]() {
            try {
                expireP25WarmStandbyIfNeeded();
                // Live traffic audio owns the tuner ring. Waterfall/classifier
                // must not run at 20 Hz on the UI thread during a follow.
                const bool liveVoicePriority =
                    p25IndependentTrafficActive || p25Phase2SpeakerSustainDecodeActive();
                static auto lastSpectrumUiTick = std::chrono::steady_clock::now() - std::chrono::seconds(1);
                const auto uiTickNow = std::chrono::steady_clock::now();
                const bool doSpectrumUi =
                    uiTickNow - lastSpectrumUiTick >= std::chrono::milliseconds(50);
                if (doSpectrumUi) {
                    lastSpectrumUiTick = uiTickNow;
                }

                auto& mgr = DeviceManager::instance();
                for (size_t i = 0; i < mgr.getDevices().size(); ++i) {
                    if (mgr.isStreaming(i)) {
                        std::vector<float> pwr;
                        double cf = 100e6, sr = 2.048e6;
                        if (mgr.getLatestSpectrum(i, pwr, cf, sr) && !pwr.empty()) {
                            if (doSpectrumUi) {
                                spectrum->updateSpectrum(pwr, cf, sr);
                            }

                            // Stage 2 hardening: keep the 10 ms UI timer light. The classifier and
                            // AUTO bandwidth resolver are useful, but running ROI construction +
                            // deterministic/model classification on every spectrum paint tick makes
                            // the GUI compete with P25 acquisition and IQ capture. Rate-limit this
                            // work and leave the timer as a spectrum/UI pump.
                            static auto lastClassifierUi = std::chrono::steady_clock::now() - std::chrono::seconds(1);
                            const auto classifierNow = std::chrono::steady_clock::now();
                            if (!liveVoicePriority &&
                                classifierNow - lastClassifierUi > std::chrono::milliseconds(500)) {
                                double monFreqForClassifier = currentMonitorFreq;
                                double monBwForClassifier = monitorChannelBwHz;
                                {
                                    std::lock_guard<std::mutex> lk(monitorParamsMutex);
                                    monFreqForClassifier = currentMonitorFreq;
                                    monBwForClassifier = monitorChannelBwHz;
                                }
                                classifierRoiBuilder.pushSpectrum(pwr);
                                const double roiHz = std::clamp(
                                    std::max(monBwForClassifier * 4.0, monBwForClassifier >= 100000.0 ? 350000.0 : 50000.0),
                                    20000.0,
                                    sr);
                                auto tile = classifierRoiBuilder.buildTile(sr, cf, monFreqForClassifier, roiHz, 256, 256);
                                auto modelRec = tile.valid()
                                    ? ClassifierModelBackend::instance().classifyTile(tile, sr, cf, monFreqForClassifier, roiHz)
                                    : std::optional<SignalRecommendation>{};
                                auto liveRec = modelRec.has_value()
                                    ? *modelRec
                                    : (tile.valid()
                                        ? AdvancedSignalClassifier::instance().classifyWaterfallTile(tile, sr, cf, monFreqForClassifier, roiHz)
                                        : AdvancedSignalClassifier::instance().classifySpectrum(pwr, sr, cf, monFreqForClassifier));
                                if (classifierStatus) {
                                    classifierStatus->setText(QString("Classifier: deterministic %1 %2%  BW %3 kHz  %4")
                                        .arg(QString::fromStdString(liveRec.label))
                                        .arg(liveRec.confidence * 100.0, 0, 'f', 0)
                                        .arg(liveRec.standardBandwidthHz / 1000.0, 0, 'f', 1)
                                        .arg(QString::fromStdString(classifierFilterKindToString(liveRec.filterKind))));
                                }
                                if (autoDetectMode) {
                                    double monFreq = monFreqForClassifier;
                                    auto smart = chooseSmartModeAndBandwidth(pwr, sr, cf, monFreq, DemodMode::AUTO, &liveRec);
                                    DemodMode newM = smart.mode;
                                    double useBwHz = smart.bandwidthHz;
                                    double useBwK = useBwHz / 1000.0;
                                    double useLpfHz = smart.lpfHz;
                                    {
                                        std::lock_guard<std::mutex> lk(monitorParamsMutex);
                                        currentMonitorMode = newM;
                                        monitorChannelBwHz = useBwHz;
                                        monitorLpfHz = useLpfHz;
                                    }
                                    syncMonitorVarsToReceiver(0);
                                    if (bwSpin) {
                                        bwSpin->blockSignals(true);
                                        bwSpin->setValue(useBwK);
                                        bwSpin->blockSignals(false);
                                    }
                                    if (lpfSpin) {
                                        lpfSpin->blockSignals(true);
                                        lpfSpin->setValue(useLpfHz / 1000.0);
                                        lpfSpin->blockSignals(false);
                                    }
                                }
                                lastClassifierUi = classifierNow;
                            }
                            if (!liveVoicePriority && p25ScanBtn && p25ScanBtn->isChecked()) {
                                static auto lastP25Ui = std::chrono::steady_clock::now() - std::chrono::seconds(1);
                                auto now = std::chrono::steady_clock::now();
                                if (now - lastP25Ui > std::chrono::milliseconds(700)) {
                                    auto hits = detectP25ControlCandidates(pwr, sr, cf);
                                    const auto known = loadP25KnownControlChannels();
                                    populateP25Table(p25Table, hits, known);
                                    refreshP25Talkgroups();
                                    if (p25Status) {
                                        p25Status->setText(QString("%1 candidate%2, %3 known")
                                            .arg(hits.size())
                                            .arg(hits.size() == 1 ? "" : "s")
                                            .arg(known.size()));
                                    }
                                    lastP25Ui = now;
                                }
                            }
                            const bool p25CcInPassband = p25MonitoredControlFreqHz > 0.0 &&
                                sr > 0.0 && std::abs(p25MonitoredControlFreqHz - cf) <= sr * 0.48;
                            // One-RTL traffic follow retunes the physical tuner to voice MHz.  Decoding the
                            // control channel from that wideband IQ only sees CC bleed (+250 kHz here) and
                            // re-triggers same-RF slot grants while starving the traffic follow/ACQ path.
                            const double ccOffsetFromDeviceHz = std::abs(p25MonitoredControlFreqHz - cf);
                            const bool oneRtlTrafficTunerAwayFromCc =
                                p25IndependentTrafficActive &&
                                (p25IndependentTrafficRetunedPrimary || ccOffsetFromDeviceHz > 75e3);
                            const bool decodeControlFromThisDevice =
                                p25MonitoredControlFreqHz > 0.0 &&
                                p25CcInPassband &&
                                !oneRtlTrafficTunerAwayFromCc &&
                                (!p25FollowEnabled ||
                                 (p25IndependentTrafficActive && !p25IndependentTrafficRetunedPrimary));
                            if (decodeControlFromThisDevice) {
                                static auto lastP25LiveDecode = std::chrono::steady_clock::now() - std::chrono::seconds(1);
                                const auto now = std::chrono::steady_clock::now();
                                if (now - lastP25LiveDecode > std::chrono::milliseconds(kP25ControlDecodeCadenceMs)) {
                                    P25LiveDecodeResult live;
                                    bool haveP25LiveResult = false;
                                    {
                                        std::lock_guard<std::mutex> pendingLock(p25ControlPendingMutex);
                                        if (p25ControlPendingResult.has_value()) {
                                            live = std::move(*p25ControlPendingResult);
                                            p25ControlPendingResult.reset();
                                            haveP25LiveResult = true;
                                        }
                                    }

                                    if (!haveP25LiveResult) {
                                        if (!p25ControlWorkerBusy.exchange(true, std::memory_order_acq_rel)) {
                                            const size_t requestedSamples = static_cast<size_t>(
                                                std::clamp(sr * kP25ControlDecodeWindowSeconds, 24000.0, 4194304.0));
                                            auto iq = mgr.getRecentIQWindow(i, requestedSamples);
                                            const double workerSr = sr;
                                            const double workerCf = cf;
                                            const double workerTarget = p25MonitoredControlFreqHz;
                                            if (p25ControlWorkerThread.joinable()) {
                                                // Previous worker already cleared busy before exit. Never join()
                                                // on the Qt timer thread — a slow teardown races with the next
                                                // control window and freezes the UI. Detach is safe: decoder
                                                // access is guarded by p25ControlWorkerDecoderMutex + busy flag.
                                                p25ControlWorkerThread.detach();
                                            }
                                            p25ControlWorkerThread = std::thread([this, iq = std::move(iq), workerSr, workerCf, workerTarget]() mutable {
                                                P25LiveDecodeResult result;
                                                try {
                                                    std::lock_guard<std::mutex> decoderLock(p25ControlWorkerDecoderMutex);
                                                    if (p25ControlWorkerResetPending.exchange(false, std::memory_order_acq_rel)) {
                                                        p25ControlWorkerDecoder = P25LiveDecoder(p25RealtimeControlDecoderConfig());
                                                    }
                                                    double effectiveTargetHz = workerTarget;
                                                    result = decodeP25ControlWithOffsetProbe(
                                                        p25ControlWorkerDecoder, iq, workerSr, workerCf, workerTarget, &effectiveTargetHz);
                                                    if ((p25ControlDecodeHasTrustedPayload(result) || p25ControlDecodeHasValidatedNid(result)) &&
                                                        std::isfinite(effectiveTargetHz) &&
                                                        std::abs(effectiveTargetHz - workerTarget) <= 25000.0) {
                                                        gP25LastTrustedControlFreqHz.store(workerTarget, std::memory_order_release);
                                                        gP25LastTrustedControlOffsetHz.store(effectiveTargetHz - workerTarget, std::memory_order_release);
                                                        gP25LastTrustedControlOffsetMs.store(
                                                            static_cast<long long>(QDateTime::currentMSecsSinceEpoch()),
                                                            std::memory_order_release);
                                                    }
                                                } catch (const std::exception& ex) {
                                                    result.warnings.push_back(std::string("P25 control worker exception: ") + ex.what());
                                                } catch (...) {
                                                    result.warnings.push_back("P25 control worker unknown exception");
                                                }
                                                {
                                                    std::lock_guard<std::mutex> pendingLock(p25ControlPendingMutex);
                                                    if (p25ControlPendingResult.has_value()) {
                                                        ++p25ControlDroppedResults;
                                                    }
                                                    p25ControlPendingResult = std::move(result);
                                                }
                                                p25ControlWorkerBusy.store(false, std::memory_order_release);
                                            });
                                        }
                                        lastP25LiveDecode = now;
                                    }

                                    if (haveP25LiveResult) {
                                    // If a voice-follow transition happened while the control worker was running,
                                    // discard this stale CC result rather than applying grants/events after retune.
                                    if ((p25FollowEnabled || p25FollowAutoActive) && !p25IndependentTrafficActive) {
                                        appendP25LogLineKeyed("p25-control-worker-stale-after-follow",
                                            "Dropped stale P25 control worker result after single-receiver voice-follow retune.",
                                            5000);
                                    } else {
                                    for (const auto& nid : live.nids) {
                                        if (nid.fecValidated) {
                                            p25LiveControlAnalyzer.setNac(nid.nac);
                                            break;
                                        }
                                    }
                                    bool registryChanged = false;
                                    size_t trustedTsbk = 0;
                                    size_t trustedPhase1Pdu = 0;
                                    size_t trustedPhase2Mac = 0;
                                    size_t controlVoiceGrantEvents = 0;
                                    size_t controlResolvedVoiceGrantEvents = 0;
                                    size_t controlUnresolvedVoiceGrantEvents = 0;
                                    std::map<std::string, size_t> trustedControlOps;
                                    auto talkgroups = loadP25Talkgroups();
                                    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
                                    if (!live.rawTsbkBlocks.empty()) {
                                        for (const auto& block : live.rawTsbkBlocks) {
                                            if (!block.fecDecoded || !block.crcValid) continue;
                                            ++trustedTsbk;
                                            ++trustedControlOps[p25ControlAuditTsbkKey(block.bytes)];
                                            const QString rawHex = p25BytesToHex(block.bytes);
                                            appendP25LogLineKeyed(QString("tsbk:%1").arg(rawHex),
                                                QString("Trusted TSBK raw=%1 corrected_dibits=%2").arg(rawHex).arg(block.correctedDibitErrors),
                                                5000);
                                            const bool registryEligible = block.correctedDibitErrors <= kP25RegistryMaxCorrectedDibits;
                                            bool acceptedHighCorrectionGrant = false;
                                            bool preservedSessionIdentifier = false;
                                            P25ControlChannelAnalyzer analyzerBefore = p25LiveControlAnalyzer;
                                            const auto events = p25LiveControlAnalyzer.ingestTsbk(block.bytes);
                                            for (const auto& ev : events) {
                                                const QString evText = p25EventLogText(ev);
                                                const QString key = QString("event:%1:%2:%3:%4:%5")
                                                    .arg(ev.opcode)
                                                    .arg(ev.mfid)
                                                    .arg(ev.talkgroupId)
                                                    .arg(ev.channel)
                                                    .arg(ev.channelB);
                                                appendP25LogLineKeyed(key, "Instruction: " + evText, 2500);
                                                if (ev.type == P25ControlEventType::Unknown) {
                                                    appendP25LogLineKeyed(QString("phase2-unknown-op:%1").arg(key),
                                                        QString("Unsupported TSBK opcode seen; preserved raw block for Phase 2/MBT parser work: %1").arg(rawHex),
                                                        6000);
                                                } else if (p25ControlEventIsVoiceGrant(ev)) {
                                                    ++controlVoiceGrantEvents;
                                                    if (p25ControlEventIsResolvedVoiceGrant(ev)) ++controlResolvedVoiceGrantEvents;
                                                    else ++controlUnresolvedVoiceGrantEvents;
                                                    const QString grantText = p25GrantDetailLogText(ev);
                                                    appendP25LogLineKeyed(QString("grant-detail:%1").arg(key),
                                                        grantText,
                                                        2500);
                                                    if (ev.phase2Candidate) {
                                                        appendP25LogLineKeyed(QString("phase2-grant:%1").arg(key),
                                                            "Phase 2 TDMA voice grant: " + grantText,
                                                            2500);
                                                    }
                                                    // Always remember explicit encrypted Phase-2 grants for the
                                                    // current-call hold, even when high-correction keeps the
                                                    // event out of the registry/auto-follow path.  Capture
                                                    // 20260712_072438 had ENC=encrypted @11 corrections then
                                                    // OP=0x02 unknown that would otherwise open audio.
                                                    if (ev.encryptionKnown && ev.encrypted &&
                                                        (ev.phase2Candidate ||
                                                         ev.voiceProtocol == P25VoiceProtocol::Phase2TDMA ||
                                                         ev.tdmaSlotKnown)) {
                                                        p25PruneRecentExplicitEncryptedPhase2Grants(
                                                            gP25RecentExplicitEncryptedPhase2Grants, nowMs);
                                                        p25RememberExplicitEncryptedPhase2Grant(
                                                            gP25RecentExplicitEncryptedPhase2Grants, ev, nullptr, nowMs);
                                                    }
                                                    if (p25TsbkPendingVoiceGrantEligible(block.correctedDibitErrors, ev)) {
                                                        rememberPendingP25VoiceGrant(ev, block.correctedDibitErrors, nowMs);
                                                    }
                                                } else if (ev.type == P25ControlEventType::IdentifierUpdate && ev.phase2Candidate) {
                                                    appendP25LogLineKeyed(QString("tdma-identifier:%1").arg(key),
                                                        "TDMA identifier table update: " + evText,
                                                        5000);
                                                }
                                                const P25RepeatedVoiceGrantDecision repeatDecision =
                                                    p25RememberRepeatedHighCorrectionResolvedVoiceGrant(
                                                        p25RepeatedVoiceGrants,
                                                        p25MonitoredControlFreqHz,
                                                        ev,
                                                        block.correctedDibitErrors,
                                                        nowMs);
                                                const bool eventRegistryEligible =
                                                    p25TsbkEventRegistryEligible(block.correctedDibitErrors, ev) ||
                                                    repeatDecision.promoted;
                                                if (eventRegistryEligible) {
                                                    if (registryEligible && ev.type == P25ControlEventType::IdentifierUpdate) {
                                                        const bool usableIdentifier = p25ChannelIdentifierUsable(p25IdentifierFromEvent(ev));
                                                        if (upsertP25ChannelIdentifier(p25MonitoredControlFreqHz, ev, nowMs)) {
                                                            appendP25LogLineKeyed(QString("iden-cache:%1:%2")
                                                                    .arg(static_cast<int>(ev.identifier))
                                                                    .arg(static_cast<qlonglong>(std::llround(p25MonitoredControlFreqHz))),
                                                                QString("Cached P25 identifier ID %1 for %2MHz: base=%3MHz step=%4kHz slots=%5.")
                                                                    .arg(static_cast<int>(ev.identifier))
                                                                    .arg(p25MonitoredControlFreqHz / 1e6, 0, 'f', 5)
                                                                    .arg(ev.baseFrequencyHz / 1e6, 0, 'f', 5)
                                                                    .arg(ev.channelSpacingHz / 1000.0, 0, 'f', 3)
                                                                    .arg(ev.slotsPerCarrier),
                                                                10000);
                                                        }
                                                        if (usableIdentifier) {
                                                            tryResolvePendingP25VoiceGrants(nowMs, QString("identifier ID %1").arg(static_cast<int>(ev.identifier)));
                                                        }
                                                    }
                                                    if (!registryEligible && p25ControlEventIsResolvedVoiceGrant(ev)) {
                                                        acceptedHighCorrectionGrant = true;
                                                        if (repeatDecision.promoted) {
                                                            appendP25LogLineKeyed(QString("tsbk-repeat-voice-grant:%1").arg(key),
                                                                QString("Accepted repeat-confirmed high-correction Phase 2 grant: corrected_dibits=%1 best=%2 hits=%3 repeat_threshold=%4 raw=%5")
                                                                    .arg(block.correctedDibitErrors)
                                                                    .arg(repeatDecision.bestCorrectedDibitErrors)
                                                                    .arg(repeatDecision.hitCount)
                                                                    .arg(kP25RepeatedVoiceGrantMaxCorrectedDibits)
                                                                    .arg(rawHex),
                                                                2500);
                                                        } else {
                                                            appendP25LogLineKeyed(QString("tsbk-weak-voice-grant:%1").arg(key),
                                                                QString("Accepted resolved voice grant from high-correction TSBK: corrected_dibits=%1 threshold=%2 raw=%3")
                                                                    .arg(block.correctedDibitErrors)
                                                                    .arg(kP25VoiceGrantMaxCorrectedDibits)
                                                                    .arg(rawHex),
                                                                2500);
                                                        }
                                                    }
                                                    const bool merged = mergeP25TalkgroupEvent(talkgroups, p25MonitoredControlFreqHz, ev, nowMs);
                                                    registryChanged = merged || registryChanged;
                                                    if (p25AutoFollowEnabled && p25ControlEventIsResolvedVoiceGrant(ev)) {
                                                        auto tgIt = std::find_if(talkgroups.begin(), talkgroups.end(), [&](const P25TalkgroupEntry& tg) {
                                                            return sameP25Talkgroup(tg, p25MonitoredControlFreqHz, ev.talkgroupId);
                                                        });
                                                        if (tgIt != talkgroups.end()) autoFollowP25Grant(*tgIt, ev, nowMs);
                                                    }
                                                }
                                                if (!eventRegistryEligible &&
                                                    p25TsbkSessionIdentifierEligible(block.correctedDibitErrors, ev)) {
                                                    preservedSessionIdentifier = true;
                                                    appendP25LogLineKeyed(QString("session-identifier:%1:%2:%3")
                                                            .arg(static_cast<int>(ev.identifier))
                                                            .arg(block.correctedDibitErrors)
                                                            .arg(rawHex),
                                                        QString("Kept high-correction identifier ID %1 in the current decode session for pending grant resolution; corrected_dibits=%2 session_threshold=%3. Persistent cache remains strict.")
                                                            .arg(static_cast<int>(ev.identifier))
                                                            .arg(block.correctedDibitErrors)
                                                            .arg(kP25SessionIdentifierMaxCorrectedDibits),
                                                        6000);
                                                    tryResolvePendingP25VoiceGrants(nowMs,
                                                        QString("session identifier ID %1").arg(static_cast<int>(ev.identifier)));
                                                }
                                                if (!eventRegistryEligible &&
                                                    p25TsbkPendingVoiceGrantEligible(block.correctedDibitErrors, ev)) {
                                                    rememberPendingP25VoiceGrant(ev, block.correctedDibitErrors, nowMs);
                                                    appendP25LogLineKeyed(QString("tsbk-weak-pending-grant:%1").arg(key),
                                                        QString("Queued near-threshold unresolved voice grant pending identifier resolution: corrected_dibits=%1 pending_threshold=%2 raw=%3")
                                                            .arg(block.correctedDibitErrors)
                                                            .arg(kP25PendingVoiceGrantMaxCorrectedDibits)
                                                            .arg(rawHex),
                                                        2500);
                                                }
                                                if (!eventRegistryEligible && repeatDecision.considered) {
                                                    appendP25LogLineKeyed(QString("tsbk-repeat-wait:%1").arg(key),
                                                        QString("Waiting for repeat-confirmed Phase 2 grant before auto-follow: corrected_dibits=%1 hits=%2/%3 repeat_threshold=%4 raw=%5")
                                                            .arg(block.correctedDibitErrors)
                                                            .arg(repeatDecision.hitCount)
                                                            .arg(kP25RepeatedVoiceGrantMinHits)
                                                            .arg(kP25RepeatedVoiceGrantMaxCorrectedDibits)
                                                            .arg(rawHex),
                                                        2500);
                                                }
                                            }
                                            if (!registryEligible && !acceptedHighCorrectionGrant && !preservedSessionIdentifier) {
                                                p25LiveControlAnalyzer = analyzerBefore;
                                                appendP25LogLineKeyed(QString("tsbk-weak:%1").arg(rawHex),
                                                    QString("TSBK raw=%1 passed CRC but needed %2 dibit corrections; non-grant state stays read-only. Resolved voice grants are allowed up to %3 corrections; unresolved voice grants are only queued up to %4 corrections.")
                                                        .arg(rawHex)
                                                        .arg(block.correctedDibitErrors)
                                                        .arg(kP25VoiceGrantMaxCorrectedDibits)
                                                        .arg(kP25PendingVoiceGrantMaxCorrectedDibits),
                                                    6000);
                                            }
                                        }
                                    }
                                    for (const auto& pdu : live.phase1Pdus) {
                                        if (!pdu.headerFecDecoded || !pdu.headerCrcValid) continue;
                                        ++trustedPhase1Pdu;
                                        ++trustedControlOps[p25ControlAuditPhase1PduKey(pdu)];
                                        const QString rawHex = p25BytesToHex(pdu.headerBytes);
                                        appendP25LogLineKeyed(QString("p1pdu:%1:%2:%3:%4")
                                                .arg(static_cast<int>(pdu.format))
                                                .arg(static_cast<int>(pdu.vendor))
                                                .arg(static_cast<int>(pdu.opcode))
                                                .arg(rawHex),
                                            QString("Trusted Phase 1 PDU format=%1 vendor=0x%2 opcode=0x%3 btf=%4 blocks=%5 hdr=%6 corrected_dibits=%7")
                                                .arg(static_cast<int>(pdu.format))
                                                .arg(static_cast<int>(pdu.vendor), 2, 16, QLatin1Char('0'))
                                                .arg(static_cast<int>(pdu.opcode), 2, 16, QLatin1Char('0'))
                                                .arg(static_cast<int>(pdu.blocksToFollow))
                                                .arg(static_cast<qulonglong>(pdu.dataBlocks.size()))
                                                .arg(rawHex)
                                                .arg(pdu.headerCorrectedDibitErrors),
                                            3000);
                                        std::vector<std::vector<uint8_t>> dataBlocks;
                                        dataBlocks.reserve(pdu.dataBlocks.size());
                                        for (const auto& block : pdu.dataBlocks) dataBlocks.push_back(block.bytes);
                                        const auto events = p25LiveControlAnalyzer.ingestPhase1Pdu(
                                            pdu.format, pdu.vendor, pdu.opcode, pdu.headerBytes, dataBlocks, pdu.headerCrcValid);
                                        for (const auto& ev : events) {
                                            const QString evText = p25EventLogText(ev);
                                            const QString key = QString("p1pdu-event:%1:%2:%3:%4:%5")
                                                .arg(ev.opcode)
                                                .arg(ev.mfid)
                                                .arg(ev.talkgroupId)
                                                .arg(ev.channel)
                                                .arg(ev.channelB);
                                            appendP25LogLineKeyed(key, "Instruction: " + evText, 2500);
                                            if (ev.type == P25ControlEventType::Unknown || ev.type == P25ControlEventType::VendorCommand) {
                                                appendP25LogLineKeyed(QString("phase1-pdu-unknown:%1").arg(key),
                                                    QString("Unsupported Phase 1 PDU/AMBTC message preserved for parser work: %1").arg(rawHex),
                                                    6000);
                                            } else if (p25ControlEventIsVoiceGrant(ev)) {
                                                ++controlVoiceGrantEvents;
                                                if (p25ControlEventIsResolvedVoiceGrant(ev)) ++controlResolvedVoiceGrantEvents;
                                                else ++controlUnresolvedVoiceGrantEvents;
                                                const QString grantText = p25GrantDetailLogText(ev);
                                                appendP25LogLineKeyed(QString("p1pdu-grant-detail:%1").arg(key),
                                                    grantText,
                                                    2500);
                                                if (!p25ControlEventIsResolvedVoiceGrant(ev)) {
                                                    rememberPendingP25VoiceGrant(ev, 0, nowMs);
                                                }
                                            }
                                            const bool merged = mergeP25TalkgroupEvent(talkgroups, p25MonitoredControlFreqHz, ev, nowMs);
                                            registryChanged = merged || registryChanged;
                                            if (p25AutoFollowEnabled && p25ControlEventIsResolvedVoiceGrant(ev)) {
                                                auto tgIt = std::find_if(talkgroups.begin(), talkgroups.end(), [&](const P25TalkgroupEntry& tg) {
                                                    return sameP25Talkgroup(tg, p25MonitoredControlFreqHz, ev.talkgroupId);
                                                });
                                                if (tgIt != talkgroups.end()) autoFollowP25Grant(*tgIt, ev, nowMs);
                                            }
                                        }
                                    }
                                    for (const auto& pdu : live.phase2MacPdus) {
                                        if (!pdu.fecDecoded || !pdu.crcValid) continue;
                                        ++trustedPhase2Mac;
                                        ++trustedControlOps[p25ControlAuditPhase2MacKey(pdu)];
                                        const QString rawHex = p25BytesToHex(pdu.bytes);
                                        appendP25LogLineKeyed(QString("p2mac:%1:%2:%3")
                                                .arg(static_cast<int>(pdu.source))
                                                .arg(pdu.dibitOffset)
                                                .arg(rawHex),
                                            QString("Trusted Phase 2 MAC PDU type=%1 offset=%2 source=%3 raw=%4 corrected_symbols=%5")
                                                .arg(QString::fromStdString(p25Phase2MacPduTypeToString(pdu.opcode)))
                                                .arg(static_cast<int>(pdu.offset))
                                                .arg(QString::fromStdString(P25LiveDecoder::phase2BurstKindToString(pdu.source)))
                                                .arg(rawHex)
                                                .arg(pdu.correctedSymbols),
                                            3000);
                                        const auto events = p25LiveControlAnalyzer.ingestPhase2MacPdu(
                                            pdu.opcode, pdu.offset, pdu.bytes, pdu.crcValid, pdu.macStructureMaxBits);
                                        for (const auto& ev : events) {
                                            const QString evText = p25EventLogText(ev);
                                            const QString key = QString("p2mac-event:%1:%2:%3:%4:%5:%6")
                                                .arg(ev.macPduType)
                                                .arg(ev.macMessageOpcode)
                                                .arg(static_cast<qulonglong>(ev.macMessageOffset))
                                                .arg(ev.talkgroupId)
                                                .arg(ev.channel)
                                                .arg(ev.channelB);
                                            appendP25LogLineKeyed(key, "Instruction: " + evText, 2500);
                                            if (ev.type == P25ControlEventType::Unknown || ev.type == P25ControlEventType::VendorCommand) {
                                                appendP25LogLineKeyed(QString("phase2-mac-unknown:%1").arg(key),
                                                    QString("Unsupported Phase 2 MAC message preserved for parser work: %1").arg(rawHex),
                                                    6000);
                                            } else if (p25ControlEventIsVoiceGrant(ev)) {
                                                ++controlVoiceGrantEvents;
                                                if (p25ControlEventIsResolvedVoiceGrant(ev)) ++controlResolvedVoiceGrantEvents;
                                                else ++controlUnresolvedVoiceGrantEvents;
                                                const QString grantText = p25GrantDetailLogText(ev);
                                                appendP25LogLineKeyed(QString("p2mac-grant-detail:%1").arg(key),
                                                    grantText,
                                                    2500);
                                                appendP25LogLineKeyed(QString("phase2-mac-grant:%1").arg(key),
                                                    "Phase 2 MAC voice grant: " + grantText,
                                                    2500);
                                                if (!p25ControlEventIsResolvedVoiceGrant(ev)) {
                                                    rememberPendingP25VoiceGrant(ev, 0, nowMs);
                                                }
                                            }
                                            if (ev.type == P25ControlEventType::IdentifierUpdate) {
                                                const bool usableIdentifier = p25ChannelIdentifierUsable(p25IdentifierFromEvent(ev));
                                                if (upsertP25ChannelIdentifier(p25MonitoredControlFreqHz, ev, nowMs)) {
                                                    appendP25LogLineKeyed(QString("p2mac-iden-cache:%1:%2")
                                                            .arg(static_cast<int>(ev.identifier))
                                                            .arg(static_cast<qlonglong>(std::llround(p25MonitoredControlFreqHz))),
                                                        QString("Cached Phase 2 MAC identifier ID %1 for %2MHz: base=%3MHz step=%4kHz slots=%5.")
                                                            .arg(static_cast<int>(ev.identifier))
                                                            .arg(p25MonitoredControlFreqHz / 1e6, 0, 'f', 5)
                                                            .arg(ev.baseFrequencyHz / 1e6, 0, 'f', 5)
                                                            .arg(ev.channelSpacingHz / 1000.0, 0, 'f', 3)
                                                            .arg(ev.slotsPerCarrier),
                                                        10000);
                                                }
                                                if (usableIdentifier) {
                                                    tryResolvePendingP25VoiceGrants(nowMs, QString("Phase 2 MAC identifier ID %1").arg(static_cast<int>(ev.identifier)));
                                                }
                                            }
                                            const bool merged = mergeP25TalkgroupEvent(talkgroups, p25MonitoredControlFreqHz, ev, nowMs);
                                            registryChanged = merged || registryChanged;
                                            if (p25AutoFollowEnabled && p25ControlEventIsResolvedVoiceGrant(ev)) {
                                                auto tgIt = std::find_if(talkgroups.begin(), talkgroups.end(), [&](const P25TalkgroupEntry& tg) {
                                                    return sameP25Talkgroup(tg, p25MonitoredControlFreqHz, ev.talkgroupId);
                                                });
                                                if (tgIt != talkgroups.end()) autoFollowP25Grant(*tgIt, ev, nowMs);
                                            }
                                        }
                                    }
                                    const QString lockStage = p25LiveLockStageText(live, trustedTsbk);
                                    const bool hasTrustedControl = trustedTsbk > 0 || trustedPhase1Pdu > 0 || trustedPhase2Mac > 0;
                                    if (hasTrustedControl || live.stats.phase2Bursts > 0) {
                                        const QString opsText = p25ControlAuditOpsText(trustedControlOps);
                                        const QString auditText = QString("P25 grant audit stage=%1 trustedTsbk=%2 trustedP1Pdu=%3 trustedP2Mac=%4 voiceGrants=%5 resolved=%6 unresolved=%7 ops=%8")
                                            .arg(lockStage)
                                            .arg(static_cast<qulonglong>(trustedTsbk))
                                            .arg(static_cast<qulonglong>(trustedPhase1Pdu))
                                            .arg(static_cast<qulonglong>(trustedPhase2Mac))
                                            .arg(static_cast<qulonglong>(controlVoiceGrantEvents))
                                            .arg(static_cast<qulonglong>(controlResolvedVoiceGrantEvents))
                                            .arg(static_cast<qulonglong>(controlUnresolvedVoiceGrantEvents))
                                            .arg(opsText);
                                        appendP25LogLineThrottled(
                                            QString("p25-grant-audit:%1:%2:%3:%4:%5:%6")
                                                .arg(lockStage)
                                                .arg(static_cast<qulonglong>(trustedTsbk))
                                                .arg(static_cast<qulonglong>(trustedPhase1Pdu))
                                                .arg(static_cast<qulonglong>(trustedPhase2Mac))
                                                .arg(static_cast<qulonglong>(controlVoiceGrantEvents))
                                                .arg(opsText),
                                            auditText,
                                            controlVoiceGrantEvents == 0 ? 2500 : 1000);
                                        if (hasTrustedControl && controlVoiceGrantEvents == 0) {
                                            appendP25LogLineThrottled(
                                                QString("p25-no-grant:%1:%2").arg(lockStage).arg(opsText),
                                                "P25 grant audit: trusted control decode contained no voice-grant opcode in this window; nothing was eligible for follow. If SDRTrunk shows a grant at the same instant, this receiver missed that grant in the RF/symbol/framer layer rather than ignoring it in follow.",
                                                5000);
                                        }
                                        if (live.stats.phase2Bursts > 0 && trustedPhase2Mac == 0) {
                                            appendP25LogLineThrottled(
                                                QString("p25-p2burst-no-mac:%1:%2")
                                                    .arg(live.stats.phase2Bursts)
                                                    .arg(lockStage),
                                                "P25 grant audit: Phase 2 burst telemetry is present but no CRC-valid MAC PDU was decoded, so the burst evidence is not yet a followable Phase 2 grant.",
                                                5000);
                                        }
                                    }
                                    if (registryChanged) {
                                        saveP25Talkgroups(talkgroups);
                                        refreshP25Talkgroups();
                                    }
                                    QString nidState = "none";
                                    if (!live.nids.empty()) {
                                        const auto& nid = live.nids.front();
                                        nidState = nid.fecValidated
                                            ? QString("NAC=0x%1 %2 corr=%3")
                                                .arg(nid.nac, 3, 16, QLatin1Char('0')).toUpper()
                                                .arg(QString::fromStdString(P25LiveDecoder::dataUnitIdToString(nid.duid)))
                                                .arg(nid.correctedBitErrors)
                                            : "BCH-fail";
                                    }
                                    const double offsetKHz = (p25MonitoredControlFreqHz - cf) / 1000.0;
                                    const QString bestNidDist = live.stats.bestNidBchDistance >= 0
                                        ? QString::number(live.stats.bestNidBchDistance)
                                        : QString("-");
                                    const QString diag = QString("P25 stage lock=%1 dev=%2 path=%3 cqpskLock=%4/%5/%6 sticky=%7 cqpskCand=%8 c4fmSkipCqpsk=%9 trust=%10 miss=%11 phase=%12 fine=%13 resid=%14Hz err=%15 cf=%16MHz target=%17MHz offset=%18kHz sr=%19MHz chanSr=%20kHz discMean=%21Hz iq=%22 sym=%23 conf=%24 softQ=%25 softLlr=%26 softLow=%27/%28 sync=%29 bestErr=%30 aligned=%31 bestNidDist=%32 nid=%33 tsbk=%34 trusted=%35")
                                        .arg(lockStage)
                                        .arg(i)
                                        .arg(QString::fromStdString(live.stats.demodPath.empty() ? std::string("unknown") : live.stats.demodPath))
                                        .arg(live.stats.cqpskLockActive ? "active" : "new")
                                        .arg(live.stats.cqpskLockUsed ? "used" : "search")
                                        .arg(live.stats.cqpskLockUpdated ? "updated" : "held")
                                        .arg(live.stats.cqpskStickyOverride ? "yes" : "no")
                                        .arg(static_cast<qulonglong>(live.stats.cqpskCandidatesEvaluated))
                                        .arg(live.stats.c4fmHardLockSkippedCqpsk ? "yes" : "no")
                                        .arg(live.stats.cqpskLockTrustScore)
                                        .arg(live.stats.cqpskLockMisses)
                                        .arg(live.stats.cqpskSymbolPhaseFraction, 0, 'f', 3)
                                        .arg(live.stats.cqpskFineCorrectionApplied ? live.stats.cqpskFineRotationRad : 0.0, 0, 'f', 4)
                                        .arg(live.stats.cqpskResidualCarrierHz, 0, 'f', 1)
                                        .arg(live.stats.cqpskPhaseErrorRmsRad, 0, 'f', 4)
                                        .arg(cf / 1e6, 0, 'f', 5)
                                        .arg(p25MonitoredControlFreqHz / 1e6, 0, 'f', 5)
                                        .arg(offsetKHz, 0, 'f', 1)
                                        .arg(sr / 1e6, 0, 'f', 3)
                                        .arg(live.stats.channelSampleRate / 1000.0, 0, 'f', 2)
                                        .arg(live.stats.discriminatorMeanHz, 0, 'f', 1)
                                        .arg(static_cast<qulonglong>(live.stats.inputSamples))
                                        .arg(live.stats.symbols)
                                        .arg(live.stats.symbolConfidence, 0, 'f', 2)
                                        .arg(live.stats.softDecisionQuality, 0, 'f', 3)
                                        .arg(live.stats.softBitLlrMean, 0, 'f', 2)
                                        .arg(static_cast<qulonglong>(live.stats.softLowConfidenceSymbols))
                                        .arg(static_cast<qulonglong>(live.stats.softDecisionSymbols))
                                        .arg(live.syncs.size())
                                        .arg(live.stats.bestFrameSyncBitErrors)
                                        .arg(live.stats.bestFrameSyncBitAligned ? "yes" : "no")
                                        .arg(bestNidDist)
                                        .arg(nidState)
                                        .arg(live.rawTsbkBlocks.size())
                                        .arg(trustedTsbk);
                                    const QString phase2Diag = QString(" p2bursts=%1 p2vcw=%2 p2sf=%3 p2mask=%4 p2phase=%5/%6 score=%7 p2mac=%8/%9 %10 p2ess=%11 p2isch=%12/%13 p2syncAdj=%14/%15 p2best=%16")
                                        .arg(live.stats.phase2Bursts)
                                        .arg(live.stats.phase2VoiceCodewords)
                                        .arg(live.stats.phase2SuperframeBursts)
                                        .arg(live.stats.phase2MaskedBursts)
                                        .arg(live.stats.phase2MaskPhaseKnown ? QString::number(static_cast<int>(live.stats.phase2MaskPhase)) : QString("-"))
                                        .arg(live.stats.phase2MaskPhaseMacCrcValid)
                                        .arg(live.stats.phase2MaskPhaseScore)
                                        .arg(live.stats.phase2MacCrcValid)
                                        .arg(live.stats.phase2MacPdus)
                                        .arg(p25Phase2AcchStatsText(live.stats))
                                         .arg(live.stats.phase2EssKnown ? (live.stats.phase2EssEncrypted ? "enc" : "clear") : "unknown")
                                         .arg(live.stats.phase2IschDecoded)
                                         .arg(live.stats.phase2IschSync)
                                         .arg(static_cast<qulonglong>(live.stats.phase2SyncOffsetCorrections))
                                         .arg(live.stats.phase2SyncOffsetCorrectionDibits)
                                         .arg(live.stats.bestPhase2SyncErrors >= 0 ? QString::number(live.stats.bestPhase2SyncErrors) : QString("-"));
                                    const QString diagSig = QString("dev%1:sync%2:best%3:nid%4:dist%5:trusted%6")
                                        .arg(i)
                                        .arg(live.syncs.empty() ? 0 : 1)
                                        .arg(live.stats.bestFrameSyncBitErrors)
                                        .arg(live.nids.empty() ? "none" : (live.nids.front().fecValidated ? "ok" : "fail"))
                                        .arg(bestNidDist)
                                        .arg(trustedTsbk > 0 ? 1 : 0);
                                    appendP25LogLineThrottled(diagSig, diag + phase2Diag, live.syncs.empty() ? 1500 : 900);
                                    if (std::abs(p25MonitoredControlFreqHz - cf) > sr * 0.48) {
                                        appendP25LogLineKeyed("p25-target-outside-passband",
                                            "P25 target is near/outside the sampled passband; retune center or widen sample-rate before sync can lock.",
                                            5000);
                                    }
                                    for (const auto& sync : live.syncs) {
                                        appendP25LogLineKeyed(QString("sync:%1:%2:%3").arg(sync.bitOffset).arg(sync.inverted).arg(sync.bitErrors),
                                            QString("Frame sync bit=%1 inverted=%2 errors=%3 confidence=%4")
                                                .arg(sync.bitOffset)
                                                .arg(sync.inverted ? "yes" : "no")
                                                .arg(sync.bitErrors)
                                                .arg(sync.confidence, 0, 'f', 2),
                                            3500);
                                    }
                                    for (const auto& burst : live.phase2Bursts) {
                                        const QString ischText = !burst.isch.valid
                                            ? QString("-")
                                            : (burst.isch.sync
                                                ? QString("sync err=%1").arg(burst.isch.errors)
                                                : QString("ch=%1 loc=%2 fa=%3 cnt=%4 err=%5")
                                                    .arg(static_cast<int>(burst.isch.channel))
                                                    .arg(static_cast<int>(burst.isch.location))
                                                    .arg(burst.isch.freeAccess ? "yes" : "no")
                                                    .arg(static_cast<int>(burst.isch.ultraframeCounter))
                                                    .arg(burst.isch.errors));
                                        appendP25LogLineKeyed(QString("p2burst:%1:%2:%3")
                                                .arg(burst.dibitOffset)
                                                .arg(static_cast<int>(burst.rawDuidCodeword))
                                                .arg(burst.syncErrors),
                                            QString("Phase 2 burst dibit=%1 kind=%2 duid=0x%3 duidErr=%4 syncErr=%5 syncAdj=%6 vcw=%7 tdmaSync=%8 sf=%9 score=%10 legacyAudioLock=%11 sessionRelease=%12 sfBurst=%13 grantSlot=%14 xorMask=%15 phase=%16 phaseScore=%17 mac=%18 ess=%19 isch=%20")
                                                 .arg(burst.dibitOffset)
                                                 .arg(QString::fromStdString(P25LiveDecoder::phase2BurstKindToString(burst.kind)))
                                                 .arg(burst.duid, 1, 16, QLatin1Char('0')).toUpper()
                                                 .arg(burst.duidErrors)
                                                 .arg(burst.syncErrors)
                                                 .arg(burst.syncOffsetAdjusted ? QString::number(burst.syncOffsetDibits) : QString("0"))
                                                 .arg(burst.voiceCodewords.size())
                                                 .arg(burst.tdmaSyncLock ? "yes" : "no")
                                                .arg(burst.superframeLocked ? "locked" : "no")
                                                .arg(burst.superframeSyncScore)
                                                .arg(burst.phase2AudioLock ? "yes" : "no")
                                                .arg(burst.sessionAudioRelease ? "yes" : "no")
                                                .arg(burst.superframeBurstIndexKnown ? QString::number(static_cast<int>(burst.superframeBurstIndex)) : QString("-"))
                                                .arg(burst.grantSlotKnown ? QString::number(static_cast<int>(burst.grantSlot)) : QString("-"))
                                                .arg(burst.xorMaskApplied ? "yes" : "not-yet")
                                                .arg(burst.xorMaskPhaseKnown ? QString::number(static_cast<int>(burst.xorMaskPhase)) : QString("-"))
                                                .arg(burst.xorMaskPhaseScore)
                                                .arg(burst.macCrcValid ? "crc-ok" : (burst.macFecDecoded ? "fec-only" : "-"))
                                                .arg(burst.essKnown ? (burst.encrypted ? "encrypted" : "clear") : "unknown")
                                                .arg(ischText),
                                            5000);
                                    }
                                    for (const auto& warning : live.warnings) {
                                        const QString warningText = QString::fromStdString(warning);
                                        const bool offsetProbeNote =
                                            warningText.startsWith("P25 control target-offset probe selected");
                                        const bool decodeProfileNote =
                                            warningText.startsWith("decodeProfile ");
                                        if (offsetProbeNote || decodeProfileNote) {
                                            const QString key = offsetProbeNote
                                                ? QStringLiteral("diag:p25-control-target-offset-probe")
                                                : QStringLiteral("diag:p25-decode-profile");
                                            appendP25LogLineKeyed(key,
                                                "Decoder diagnostic: " + warningText,
                                                offsetProbeNote ? 12000 : 10000);
                                            continue;
                                        }
                                        appendP25LogLineKeyed(QString("warn:%1").arg(warningText),
                                            "Decoder warning: " + warningText,
                                            5000);
                                    }
                                    if (p25Status) {
                                        if (!live.nids.empty()) {
                                            const auto& nid = live.nids.front();
                                            if (nid.fecValidated) {
                                                p25Status->setText(QString("CC %1 MHz NAC %2 %3 sync")
                                                    .arg(p25MonitoredControlFreqHz / 1e6, 0, 'f', 5)
                                                    .arg(nid.nac, 3, 16, QChar('0')).toUpper()
                                                    .arg(QString::fromStdString(P25LiveDecoder::dataUnitIdToString(nid.duid))));
                                            } else {
                                                p25Status->setText(QString("CC %1 MHz sync, NID BCH fail")
                                                    .arg(p25MonitoredControlFreqHz / 1e6, 0, 'f', 5));
                                            }
                                        } else if (!live.syncs.empty()) {
                                            p25Status->setText(QString("CC %1 MHz sync, waiting NID")
                                                .arg(p25MonitoredControlFreqHz / 1e6, 0, 'f', 5));
                                        } else {
                                            p25Status->setText(QString("CC %1 MHz no P25 sync, best err %2")
                                                .arg(p25MonitoredControlFreqHz / 1e6, 0, 'f', 5)
                                                .arg(live.stats.bestFrameSyncBitErrors));
                                        }
                                    }
                                    if (const long long dropped = p25ControlDroppedResults.exchange(0); dropped > 0) {
                                        appendP25LogLineKeyed("p25-control-worker-dropped",
                                            QString("P25 control worker dropped %1 stale result(s) while GUI was busy; latest result kept.").arg(dropped),
                                            5000);
                                    }
                                    } // end stale-control-result guard else
                                    }
                                }
                            } else if (p25FollowEnabled && p25Status) {
                                P25VoiceDiagSnapshot voiceDiag;
                                P25TrafficProcessorStatusSnapshot trafficStatus;
                                bool haveVoiceDiag = false;
                                bool voiceStateDecodeEnabled = false;
                                bool voiceStatePhase2 = false;
                                uint64_t voiceStateCurrentCallSessionId = 0;
                                bool voiceStateClearKnown = false;
                                bool voiceStateEncrypted = false;
                                P25CallSecurityLatch voiceStateCallSecurityLatch = P25CallSecurityLatch::Unknown;
                                bool voiceStateSlotKnown = false;
                                uint8_t voiceStateSlot = 0;
                                bool voiceStateMaskKnown = false;
                                uint16_t voiceStateNac = 0;
                                uint32_t voiceStateWacn = 0;
                                uint16_t voiceStateSystemId = 0;
                                bool voiceStateResetPending = false;
                                bool voiceStateSlotProbePending = false;
                                uint8_t voiceStateSlotProbeRequested = 0;
                                qint64 voiceStateSettleUntilMs = 0;
                                int voiceStateDiscardWindows = 0;
                                long long voiceDiagUpdateMs = 0;
                                bool voiceStateSessionHadVoiceLock = false;
                                {
                                    std::unique_lock<std::mutex> lk(receiversMutex, std::try_to_lock);
                                    if (lk.owns_lock()) {
                                        std::shared_ptr<Receiver> statusRx;
                                        if (p25IndependentTrafficActive) {
                                            const uint64_t liveGen = p25TrafficSourceGeneration.load(std::memory_order_acquire);
                                            for (auto& candidate : receivers) {
                                                if (candidate && candidate->p25IndependentTrafficSource &&
                                                    candidate->p25TrafficGeneration == liveGen) {
                                                    statusRx = candidate;
                                                    break;
                                                }
                                            }
                                        }
                                        if (!statusRx && !p25IndependentTrafficActive && !receivers.empty()) statusRx = receivers[0];
                                        if (statusRx) {
                                            std::unique_lock<std::mutex> rxLock(statusRx->stateMutex, std::try_to_lock);
                                            if (!rxLock.owns_lock()) {
                                                uint8_t mirrorSlot = 0;
                                                bool mirrorSlotKnown = false;
                                                if (loadP25VoiceDiagMirror(voiceDiag, mirrorSlot, mirrorSlotKnown)) {
                                                    voiceStateSlotKnown = mirrorSlotKnown;
                                                    voiceStateSlot = mirrorSlot;
                                                    voiceDiagUpdateMs = voiceDiag.updatedMs;
                                                    haveVoiceDiag = true;
                                                }
                                                P25FollowGuiStatusCache cached;
                                                const uint64_t activeTrafficGeneration =
                                                    p25IndependentTrafficActive
                                                        ? p25TrafficSourceGeneration.load(std::memory_order_acquire)
                                                        : 0;
                                                if (loadP25FollowGuiStatusCache(cached) &&
                                                    p25FollowGuiStatusCacheMatchesActiveFollow(
                                                        cached,
                                                        p25IndependentTrafficActive,
                                                        p25FollowTalkgroupId,
                                                        p25AutoFollowVoiceFreqHz,
                                                        activeTrafficGeneration)) {
                                                    if (!haveVoiceDiag) {
                                                        voiceDiag = cached.voiceDiag;
                                                        voiceDiagUpdateMs = cached.updatedMs;
                                                        haveVoiceDiag = cached.updatedMs > 0;
                                                    }
                                                    trafficStatus = cached.trafficStatus;
                                                    voiceStateDecodeEnabled = cached.voiceStateDecodeEnabled;
                                                    voiceStatePhase2 = cached.voiceStatePhase2;
                                                    voiceStateCurrentCallSessionId = cached.voiceStateCurrentCallSessionId;
                                                    voiceStateClearKnown = cached.voiceStateClearKnown;
                                                    voiceStateEncrypted = cached.voiceStateEncrypted;
                                                    voiceStateCallSecurityLatch = cached.voiceStateCallSecurityLatch;
                                                    voiceStateSlotKnown = cached.voiceStateSlotKnown || voiceStateSlotKnown;
                                                    if (!voiceStateSlotKnown && cached.voiceStateSlotKnown) {
                                                        voiceStateSlot = cached.voiceStateSlot;
                                                    }
                                                    voiceStateMaskKnown = cached.voiceStateMaskKnown;
                                                    voiceStateNac = cached.voiceStateNac;
                                                    voiceStateWacn = cached.voiceStateWacn;
                                                    voiceStateSystemId = cached.voiceStateSystemId;
                                                    voiceStateResetPending = cached.voiceStateResetPending;
                                                    voiceStateSlotProbePending = cached.voiceStateSlotProbePending;
                                                    voiceStateSlotProbeRequested = cached.voiceStateSlotProbeRequested;
                                                    voiceStateSettleUntilMs = cached.voiceStateSettleUntilMs;
                                                    voiceStateDiscardWindows = cached.voiceStateDiscardWindows;
                                                 }
                                             } else {
                                                 const auto& rxState = *statusRx;
                                                 voiceDiag = rxState.p25VoiceDiagnostics;
                                                 trafficStatus = snapshotP25TrafficProcessorStatus(rxState);
                                                voiceStateDecodeEnabled = rxState.p25VoiceDecodeEnabled;
                                                voiceStatePhase2 = rxState.p25VoicePhase2;
                                                voiceStateCurrentCallSessionId = rxState.p25CurrentCallSessionId;
                                                voiceStateClearKnown = rxState.p25VoiceClearKnown;
                                                voiceStateEncrypted = rxState.p25VoiceEncrypted;
                                                voiceStateCallSecurityLatch = rxState.p25SessionState.callSecurityLatch;
                                                voiceStateSlotKnown = rxState.p25VoiceTdmaSlotKnown;
                                                voiceStateSlot = static_cast<uint8_t>(rxState.p25VoiceTdmaSlot & 0x01u);
                                                voiceStateMaskKnown = rxState.p25VoiceMaskParamsKnown;
                                                voiceStateNac = rxState.p25VoiceNac;
                                                voiceStateWacn = rxState.p25VoiceWacn;
                                                voiceStateSystemId = rxState.p25VoiceSystemId;
                                                voiceStateResetPending = rxState.p25VoiceResetPending;
                                                voiceStateSlotProbePending = rxState.p25VoiceSlotProbePending;
                                                voiceStateSlotProbeRequested = static_cast<uint8_t>(rxState.p25VoiceSlotProbeRequested & 0x01u);
                                                voiceStateSettleUntilMs = rxState.p25VoiceSettleUntilMs;
                                                voiceStateDiscardWindows = rxState.p25VoiceDiscardWindows;
                                                voiceDiagUpdateMs = rxState.p25VoiceDiagnostics.updatedMs;
                                                voiceStateSessionHadVoiceLock = p25Phase2SessionHadVoiceLock(rxState);
                                                 haveVoiceDiag = true;
                                                 P25FollowGuiStatusCache cacheSnapshot;
                                                 cacheSnapshot.updatedMs = QDateTime::currentMSecsSinceEpoch();
                                                 cacheSnapshot.receiverKey = reinterpret_cast<quintptr>(statusRx.get());
                                                 cacheSnapshot.trafficGeneration = rxState.p25TrafficGeneration;
                                                 cacheSnapshot.independentTrafficSource = rxState.p25IndependentTrafficSource;
                                                 cacheSnapshot.trafficVoiceFreqHz = rxState.p25TrafficVoiceFreqHz > 0.0
                                                     ? rxState.p25TrafficVoiceFreqHz
                                                     : rxState.freqHz;
                                                 cacheSnapshot.voiceDiag = voiceDiag;
                                                 cacheSnapshot.trafficStatus = trafficStatus;
                                                cacheSnapshot.voiceStateDecodeEnabled = voiceStateDecodeEnabled;
                                                cacheSnapshot.voiceStatePhase2 = voiceStatePhase2;
                                                cacheSnapshot.voiceStateCurrentCallSessionId = voiceStateCurrentCallSessionId;
                                                cacheSnapshot.voiceStateClearKnown = voiceStateClearKnown;
                                                cacheSnapshot.voiceStateEncrypted = voiceStateEncrypted;
                                                cacheSnapshot.voiceStateCallSecurityLatch = voiceStateCallSecurityLatch;
                                                cacheSnapshot.voiceStateSlotKnown = voiceStateSlotKnown;
                                                cacheSnapshot.voiceStateSlot = voiceStateSlot;
                                                cacheSnapshot.voiceStateMaskKnown = voiceStateMaskKnown;
                                                cacheSnapshot.voiceStateNac = voiceStateNac;
                                                cacheSnapshot.voiceStateWacn = voiceStateWacn;
                                                cacheSnapshot.voiceStateSystemId = voiceStateSystemId;
                                                cacheSnapshot.voiceStateResetPending = voiceStateResetPending;
                                                cacheSnapshot.voiceStateSlotProbePending = voiceStateSlotProbePending;
                                                cacheSnapshot.voiceStateSlotProbeRequested = voiceStateSlotProbeRequested;
                                                cacheSnapshot.voiceStateSettleUntilMs = voiceStateSettleUntilMs;
                                                cacheSnapshot.voiceStateDiscardWindows = voiceStateDiscardWindows;
                                                updateP25FollowGuiStatusCache(cacheSnapshot);
                                            }
                                        }
                                     } else {
                                         P25FollowGuiStatusCache cached;
                                         const uint64_t activeTrafficGeneration =
                                             p25IndependentTrafficActive
                                                 ? p25TrafficSourceGeneration.load(std::memory_order_acquire)
                                                 : 0;
                                         if (loadP25FollowGuiStatusCache(cached) &&
                                             p25FollowGuiStatusCacheMatchesActiveFollow(
                                                 cached,
                                                 p25IndependentTrafficActive,
                                                 p25FollowTalkgroupId,
                                                 p25AutoFollowVoiceFreqHz,
                                                 activeTrafficGeneration)) {
                                             voiceDiag = cached.voiceDiag;
                                             trafficStatus = cached.trafficStatus;
                                            voiceStateDecodeEnabled = cached.voiceStateDecodeEnabled;
                                            voiceStatePhase2 = cached.voiceStatePhase2;
                                            voiceStateCurrentCallSessionId = cached.voiceStateCurrentCallSessionId;
                                            voiceStateClearKnown = cached.voiceStateClearKnown;
                                            voiceStateEncrypted = cached.voiceStateEncrypted;
                                            voiceStateCallSecurityLatch = cached.voiceStateCallSecurityLatch;
                                            voiceStateSlotKnown = cached.voiceStateSlotKnown;
                                            voiceStateSlot = cached.voiceStateSlot;
                                            voiceStateMaskKnown = cached.voiceStateMaskKnown;
                                            voiceStateNac = cached.voiceStateNac;
                                            voiceStateWacn = cached.voiceStateWacn;
                                            voiceStateSystemId = cached.voiceStateSystemId;
                                            voiceStateResetPending = cached.voiceStateResetPending;
                                            voiceStateSlotProbePending = cached.voiceStateSlotProbePending;
                                            voiceStateSlotProbeRequested = cached.voiceStateSlotProbeRequested;
                                            voiceStateSettleUntilMs = cached.voiceStateSettleUntilMs;
                                            voiceStateDiscardWindows = cached.voiceStateDiscardWindows;
                                            voiceDiagUpdateMs = cached.updatedMs;
                                            haveVoiceDiag = cached.updatedMs > 0;
                                        }
                                    }
                                }
                                if (p25IndependentTrafficActive &&
                                    haveVoiceDiag &&
                                    !voiceStateDecodeEnabled &&
                                    (p25AutoFollowVoiceFreqHz > 0.0 || p25FollowTalkgroupId != 0) &&
                                    (voiceDiag.phase2Bursts > 0 ||
                                     voiceDiag.phase2VoiceCodewords > 0 ||
                                     voiceDiag.phase2SuperframeBursts > 0 ||
                                     voiceDiag.phase2MaskedBursts > 0 ||
                                     trafficStatus.present)) {
                                    // The voice worker can hold the traffic receiver state mutex for
                                    // most of a Phase-2 acquisition window.  Do not let an old cache
                                    // from the muted control receiver make the GUI/follow watchdog
                                    // believe this active traffic source has no decoder attached.
                                    voiceStateDecodeEnabled = true;
                                    voiceStatePhase2 = true;
                                    voiceStateMaskKnown = voiceStateMaskKnown ||
                                        voiceDiag.phase2MaskedBursts > 0 ||
                                        voiceDiag.phase2SuperframeBursts > 0;
                                    if (voiceDiag.phase2EssKnown) {
                                        // DEC-0025 / 103955: do not promote grantEncrypted from
                                        // ESS-diag alone while the call is latched clear. That
                                        // made follow ReturnEncrypted via grantProvesEncrypted
                                        // without the macCrc bar trustedEncryptedEss already has.
                                        if (voiceDiag.phase2EssEncrypted) {
                                            const bool strongEssEnc =
                                                voiceStateCallSecurityLatch !=
                                                    P25CallSecurityLatch::Clear ||
                                                voiceDiag.phase2MacCrcValid > 0 ||
                                                voiceDiag.phase2TargetMacCrcValid;
                                            if (strongEssEnc) {
                                                voiceStateEncrypted = true;
                                                voiceStateClearKnown = false;
                                            }
                                        } else {
                                            voiceStateEncrypted = false;
                                            voiceStateClearKnown = true;
                                        }
                                    }
                                }
                                if (!haveVoiceDiag) {
                                    if (p25FollowAutoActive &&
                                        (p25AutoFollowVoiceFreqHz > 0.0 || p25FollowTalkgroupId != 0)) {
                                        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
                                        voiceDiag = P25VoiceDiagSnapshot{};
                                        voiceDiag.diag = static_cast<int>(P25VoiceDiagCode::NoSync);
                                        voiceDiag.talkgroupId = static_cast<uint32_t>(std::max<long long>(0, p25FollowTalkgroupId));
                                        voiceDiag.updatedMs = nowMs;
                                        voiceDiagUpdateMs = nowMs;
                                        voiceStateDecodeEnabled = p25IndependentTrafficActive || p25FollowAutoActive;
                                        voiceStatePhase2 = true;
                                        haveVoiceDiag = true;
                                        appendP25LogLineKeyed("p25-follow-no-diag-watchdog",
                                            QString("P25 follow watchdog: no live voice diagnostics for TG %1 on %2MHz; treating as no-sync so return-to-control timeout can fire.")
                                                .arg(static_cast<long long>(p25FollowTalkgroupId))
                                                .arg(p25AutoFollowVoiceFreqHz / 1e6, 0, 'f', 5),
                                            3000);
                                    } else {
                                        break;
                                    }
                                }
                                {
                                    P25VoiceDiagSnapshot mirrorDiag;
                                    uint8_t mirrorSlot = 0;
                                    bool mirrorSlotKnown = false;
                                    if (loadP25VoiceDiagMirror(mirrorDiag, mirrorSlot, mirrorSlotKnown) &&
                                        mirrorDiag.updatedMs > voiceDiagUpdateMs) {
                                        voiceDiagUpdateMs = mirrorDiag.updatedMs;
                                        if (mirrorDiag.talkgroupId != 0) {
                                            voiceDiag.talkgroupId = mirrorDiag.talkgroupId;
                                        }
                                        voiceDiag.diag = mirrorDiag.diag;
                                        voiceDiag.phase2Bursts = std::max(voiceDiag.phase2Bursts, mirrorDiag.phase2Bursts);
                                        voiceDiag.phase2VoiceCodewords = std::max(voiceDiag.phase2VoiceCodewords,
                                            mirrorDiag.phase2VoiceCodewords);
                                        voiceDiag.phase2SuperframeBursts = std::max(voiceDiag.phase2SuperframeBursts,
                                            mirrorDiag.phase2SuperframeBursts);
                                        voiceDiag.phase2MaskedBursts = std::max(voiceDiag.phase2MaskedBursts,
                                            mirrorDiag.phase2MaskedBursts);
                                        voiceDiag.phase2MacCrcValid = std::max(voiceDiag.phase2MacCrcValid,
                                            mirrorDiag.phase2MacCrcValid);
                                        voiceDiag.phase2MacPdus = std::max(voiceDiag.phase2MacPdus,
                                            mirrorDiag.phase2MacPdus);
                                        voiceDiag.decodedFrames = std::max(voiceDiag.decodedFrames,
                                            mirrorDiag.decodedFrames);
                                        if (mirrorSlotKnown) {
                                            voiceStateSlotKnown = mirrorSlotKnown;
                                            voiceStateSlot = mirrorSlot;
                                        }
                                    }
                                }
                                const auto code = static_cast<P25VoiceDiagCode>(voiceDiag.diag);
                                const long long tg = static_cast<long long>(voiceDiag.talkgroupId);
                                const long long syncs = voiceDiag.syncs;
                                const long long nids = voiceDiag.nids;
                                const long long imbe = voiceDiag.imbeFrames;
                                const long long decoded = voiceDiag.decodedFrames;
                                const long long p2bursts = trafficStatus.present
                                    ? std::max<long long>(voiceDiag.phase2Bursts, trafficStatus.diag.p2bursts)
                                    : voiceDiag.phase2Bursts;
                                // Prefer traffic's p2vcw which is now filtered to only "meaningful" (mask+lock) VCW.
                                // This prevents noise on voice freq from keeping p2vcw high and follow "active".
                                const long long p2vcw = trafficStatus.present
                                    ? trafficStatus.diag.p2vcw
                                    : voiceDiag.phase2VoiceCodewords;
                                const long long p2sf = trafficStatus.present
                                    ? std::max<long long>(voiceDiag.phase2SuperframeBursts, trafficStatus.diag.p2sf)
                                    : voiceDiag.phase2SuperframeBursts;
                                const long long p2mask = trafficStatus.present
                                    ? std::max<long long>(voiceDiag.phase2MaskedBursts, trafficStatus.diag.p2mask)
                                    : voiceDiag.phase2MaskedBursts;
                                const long long p2mac = trafficStatus.present
                                    ? std::max<long long>(voiceDiag.phase2MacPdus, trafficStatus.diag.p2macPdus)
                                    : voiceDiag.phase2MacPdus;
                                const long long p2crc = trafficStatus.present
                                    ? std::max<long long>(voiceDiag.phase2MacCrcValid, trafficStatus.diag.p2macCrcValid)
                                    : voiceDiag.phase2MacCrcValid;
                                const long long p2AmbeAttempts = voiceDiag.phase2AmbeDecodeAttempts;
                                const long long p2AmbeAccepted = voiceDiag.phase2AmbeAcceptedFrames;
                                const bool p2EssKnown = voiceDiag.phase2EssKnown ||
                                    (trafficStatus.present && trafficStatus.diag.essTrusted);
                                const bool p2EssEncrypted = voiceDiag.phase2EssEncrypted ||
                                    (trafficStatus.present && trafficStatus.diag.encrypted);
                                const QString p2ess = p2EssKnown
                                    ? (p2EssEncrypted ? "enc" : "clear")
                                    : "unknown";
                                const long long statusTg = tg > 0 ? tg : static_cast<long long>(p25FollowTalkgroupId);
                                if (p25FollowAutoActive) {
                                    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
                                    const qint64 lastSpeakerMs =
                                        guiP25AudioLastOutputMs.load(std::memory_order_relaxed);
                                    P25FollowSnapshot followSnapshot;
                                    followSnapshot.nowMs = nowMs;
                                    followSnapshot.tunedAtMs = p25AutoFollowTunedAtMs;
                                    followSnapshot.lastActiveMs = p25AutoFollowLastActiveMs;
                                    followSnapshot.recentSpeakerOutputMs = lastSpeakerMs;
                                    followSnapshot.diagUpdatedMs = voiceDiagUpdateMs;
                                    followSnapshot.currentCallSessionId = voiceStateCurrentCallSessionId;
                                    if (p2EssKnown && trafficStatus.present) {
                                        followSnapshot.essCallSessionId = trafficStatus.diag.sessionId;
                                    }
                                    followSnapshot.autoActive = p25FollowAutoActive;
                                    followSnapshot.phase2Voice = voiceStatePhase2;
                                    followSnapshot.talkgroupId = voiceDiag.talkgroupId;
                                    followSnapshot.fallbackTalkgroupId = p25FollowTalkgroupId;
                                    followSnapshot.diag = voiceDiag.diag;
                                    followSnapshot.syncs = syncs;
                                    followSnapshot.nids = nids;
                                    followSnapshot.imbeFrames = imbe;
                                    followSnapshot.decodedFrames = decoded;
                                    followSnapshot.phase2Bursts = p2bursts;
                                    followSnapshot.phase2VoiceCodewords = p2vcw;
                                    followSnapshot.phase2SuperframeBursts = p2sf;
                                    followSnapshot.phase2MaskedBursts = p2mask;
                                    followSnapshot.phase2MacPdus = p2mac;
                                    followSnapshot.phase2MacCrcValid = p2crc;
                                    followSnapshot.phase2EssKnown = p2EssKnown;
                                    followSnapshot.phase2EssEncrypted = p2EssEncrypted;
                                    followSnapshot.phase2TrafficProcessorActive = trafficStatus.present;
                                    followSnapshot.phase2TrafficCallActive = trafficStatus.callActive;
                                    followSnapshot.phase2TrafficAudioOpen =
                                        voiceStateCallSecurityLatch == P25CallSecurityLatch::Clear &&
                                        trafficStatus.callActive;
                                    followSnapshot.phase2TrafficEncrypted =
                                        voiceStateCallSecurityLatch == P25CallSecurityLatch::Encrypted ||
                                        (trafficStatus.present && trafficStatus.diag.encrypted);
                                    followSnapshot.grantEncryptionKnown =
                                        voiceStateClearKnown || voiceStateEncrypted;
                                    followSnapshot.grantEncrypted = voiceStateEncrypted;
                                    followSnapshot.phase2OppositeVoiceCodewords =
                                        voiceDiag.phase2OppositeVoiceCodewords;

                                    // Use live RF metrics (peak in voice BW vs noise floor) to detect real carrier drop.
                                    // This is independent of P25 decoder state (which can be fooled by noise after TX stops).
                                    followSnapshot.recentSignalLevelDb = gLastRmsDb.load(std::memory_order_relaxed);
                                    followSnapshot.recentNoiseFloorDb = gLastNoiseFloorDb.load(std::memory_order_relaxed);
                                    followSnapshot.recentSnrDb = gLastSnrDb.load(std::memory_order_relaxed);
                                    followSnapshot.rfMetricsPopulated = true;

                                    const auto followDecision = evaluateP25Follow(followSnapshot);
                                    // Keep Phase-2 acquisition alive before PCM opens. A clear
                                    // traffic channel can need several windows for mask/MAC/ESS
                                    // and AMBE to line up; gating lastActive only on speaker PCM
                                    // repeatedly restarted real calls as fragmented audio.
                                    const bool freshFollowAcquireEvidence =
                                        voiceStatePhase2 &&
                                        !voiceStateEncrypted &&
                                        (p2vcw > 0 ||
                                         decoded > 0 ||
                                         p2crc > 0 ||
                                         p2EssKnown ||
                                         (p2bursts > 0 && p2sf > 0 && p2mask > 0) ||
                                         (trafficStatus.callActive && p2bursts > 0));
                                    // Only refresh the follow "last active" on actual voice/acquire evidence,
                                    // to avoid keeping an inactive TG alive forever from loose telemetry.
                                    const bool freshFollowVoiceEvidence =
                                        p2vcw > 0 ||
                                        decoded > 0 ||
                                        (voiceStateCallSecurityLatch == P25CallSecurityLatch::Clear &&
                                         trafficStatus.callActive &&
                                         trafficStatus.diag.p2vcw > 0) ||
                                        freshFollowAcquireEvidence;
                                    if (followDecision.voiceStillLooksActive &&
                                        freshFollowVoiceEvidence) {
                                        p25AutoFollowLastActiveMs = nowMs;
                                        if (voiceStatePhase2 && decoded == 0 &&
                                            guiP25AudioLastOutputMs.load(std::memory_order_relaxed) <= 0) {
                                            appendP25LogLineKeyed(QString("p2-preaudio-acq-hold:%1")
                                                    .arg(static_cast<long long>(followDecision.effectiveTalkgroupId)),
                                                QString("Phase 2 acquisition hold: TG %1 has pre-audio evidence p2=%2/%3 sf=%4 mask=%5 mac=%6/%7 ess=%8; keeping call alive while the decoder/ESS gate catches up.")
                                                    .arg(static_cast<long long>(followDecision.effectiveTalkgroupId))
                                                    .arg(p2bursts)
                                                    .arg(p2vcw)
                                                    .arg(p2sf)
                                                    .arg(p2mask)
                                                    .arg(p2crc)
                                                    .arg(p2mac)
                                                    .arg(p2ess),
                                                2500);
                                        }
                                    }
                                    if (followDecision.action == P25FollowAction::ReturnEncrypted) {
                                        P25TalkgroupEntry encryptedHoldTg;
                                        {
                                            auto registry = loadP25Talkgroups();
                                            bool changed = false;
                                            const uint32_t encryptedTg =
                                                static_cast<uint32_t>(std::max<long long>(0, followDecision.effectiveTalkgroupId));
                                            for (auto& row : registry) {
                                                if (row.talkgroupId != encryptedTg) continue;
                                                const bool voiceMatches =
                                                    p25AutoFollowVoiceFreqHz <= 0.0 ||
                                                    row.lastVoiceFreqHz <= 0.0 ||
                                                    std::abs(row.lastVoiceFreqHz - p25AutoFollowVoiceFreqHz) <= 50.0;
                                                if (!voiceMatches) continue;
                                                row.encryptionKnown = true;
                                                row.encrypted = true;
                                                row.lastSeenMs = nowMs;
                                                if (encryptedHoldTg.talkgroupId == 0) encryptedHoldTg = row;
                                                changed = true;
                                            }
                                            if (changed) saveP25Talkgroups(registry);
                                        }
                                        if (encryptedHoldTg.talkgroupId != 0) {
                                            p25RememberVoiceProvedEncryptedPhase2Grant(
                                                gP25RecentExplicitEncryptedPhase2Grants,
                                                encryptedHoldTg,
                                                nowMs);
                                        }
                                        // DEC-0025: name which encryptedOnVoice clause fired
                                        // (trusted ESS vs grant latch vs traffic).
                                        const bool retEncTrustedEss =
                                            p2EssKnown && p2EssEncrypted && p2crc > 0;
                                        const bool retEncGrant =
                                            (voiceStateClearKnown || voiceStateEncrypted) &&
                                            voiceStateEncrypted;
                                        const bool retEncTraffic =
                                            followSnapshot.phase2TrafficEncrypted &&
                                            p2EssKnown && p2EssEncrypted && p2crc > 0;
                                        appendP25LogLine(QString("P25 auto-follow TG %1 proved encrypted on voice channel; returning to control channel immediately. reason={trustedEss=%2 grantLatch=%3 traffic=%4} ess=%5 macCrc=%6 latchEnc=%7.")
                                            .arg(static_cast<long long>(followDecision.effectiveTalkgroupId))
                                            .arg(retEncTrustedEss ? "yes" : "no")
                                            .arg(retEncGrant ? "yes" : "no")
                                            .arg(retEncTraffic ? "yes" : "no")
                                            .arg(p2ess)
                                            .arg(p2crc)
                                            .arg(voiceStateEncrypted ? "yes" : "no"));
                                        returnP25AutoFollowToControl();
                                        return;
                                    }
                                    if (followDecision.action != P25FollowAction::None) {
                                        if (followDecision.action == P25FollowAction::ReturnNoMacEss) {
                                            if (followDecision.tdmaVcwNoSuperframeTimeout) {
                                                appendP25LogLine(QString("TDMA ACQ watchdog: Phase 2 VCWs are present but no superframe/mask/ESS lock formed for TG %1; returning to control channel to avoid hanging on a stale or mis-acquired voice channel. sf=%2 mask=%3 mac=%4/%5 ess=%6 p2vcw=%7.")
                                                    .arg(static_cast<long long>(followDecision.effectiveTalkgroupId))
                                                    .arg(p2sf)
                                                    .arg(p2mask)
                                                    .arg(p2crc)
                                                    .arg(p2mac)
                                                    .arg(p2ess)
                                                    .arg(p2vcw));
                                            } else {
                                                appendP25LogLine(QString("TDMA ACQ watchdog: untrusted sf/mask hypothesis present but MAC/ESS did not progress for TG %1; returning to control channel to avoid hanging on stale voice frequency. sf=%2 mask=%3 mac=%4/%5 ess=%6 p2vcw=%7.")
                                                    .arg(static_cast<long long>(followDecision.effectiveTalkgroupId))
                                                    .arg(p2sf)
                                                    .arg(p2mask)
                                                    .arg(p2crc)
                                                    .arg(p2mac)
                                                    .arg(p2ess)
                                                    .arg(p2vcw));
                                            }
                                        } else if (followDecision.action == P25FollowAction::ReturnNoVoiceCodewords) {
                                            appendP25LogLine(QString("TDMA ACQ watchdog: no Phase 2 VCWs after retune for TG %1; returning to control channel.")
                                                .arg(static_cast<long long>(followDecision.effectiveTalkgroupId)));
                                        } else {
                                            appendP25LogLine(QString("P25 auto-follow TG %1 ended or went quiet; returning to control channel.")
                                                .arg(static_cast<long long>(followDecision.effectiveTalkgroupId)));
                                        }
                                        returnP25AutoFollowToControl();
                                        return;
                                    }
                                }
                                const QString followStatusText = QString("TG %1 %2 sync=%3 nid=%4 imbe=%5 dec=%6 p2=%7/%8 sf=%9 mask=%10 mac=%11/%12 ess=%13")
                                    .arg(tg > 0 ? tg : static_cast<long long>(p25FollowTalkgroupId))
                                    .arg(p25VoiceDiagLabel(code))
                                    .arg(syncs)
                                    .arg(nids)
                                    .arg(imbe)
                                    .arg(decoded)
                                    .arg(p2bursts)
                                    .arg(p2vcw)
                                    .arg(p2sf)
                                    .arg(p2mask)
                                    .arg(p2crc)
                                    .arg(p2mac)
                                    .arg(p2ess);
                                {
                                    static qint64 lastP25FollowStatusPaintMs = 0;
                                    static QString lastP25FollowStatusText;
                                    const qint64 paintNowMs = QDateTime::currentMSecsSinceEpoch();
                                    if (lastP25FollowStatusText.isEmpty() ||
                                        (followStatusText != lastP25FollowStatusText &&
                                         paintNowMs - lastP25FollowStatusPaintMs >= 250)) {
                                        p25Status->setText(followStatusText);
                                        lastP25FollowStatusText = followStatusText;
                                        lastP25FollowStatusPaintMs = paintNowMs;
                                    }
                                }

                                if (p25AutoFollowVoiceFreqHz > 0.0 || p25FollowTalkgroupId != 0) {
                                    static qint64 lastTdmaAcqStatusMs = 0;
                                    const qint64 acqNowMs = QDateTime::currentMSecsSinceEpoch();
                                    if (acqNowMs - lastTdmaAcqStatusMs > 1000) {
                                        const double voiceHz = p25AutoFollowVoiceFreqHz > 0.0 ? p25AutoFollowVoiceFreqHz : currentMonitorFreq;
                                        const double effectiveVoiceHz = voiceDiag.phase2EffectiveTargetFreqHz > 0.0
                                            ? voiceDiag.phase2EffectiveTargetFreqHz
                                            : voiceHz;
                                        const bool inPassband = sr > 0.0 && std::abs(effectiveVoiceHz - cf) <= sr * 0.48;
                                        appendP25LogLine(QString("TDMA ACQ check: TG=%1 voice=%2MHz cf=%3MHz offset=%4kHz inPassband=%5 diag=%6 p2bursts=%7 p2vcw=%8 sf=%9 mask=%10 mac=%11/%12 ambe=%13/%14 %15 ess=%16 demod=cqpsk-traffic audioGate=ptt-ess-or-validated-clear-grant")
                                            .arg(tg > 0 ? tg : static_cast<long long>(p25FollowTalkgroupId))
                                            .arg(voiceHz / 1e6, 0, 'f', 5)
                                            .arg(cf / 1e6, 0, 'f', 5)
                                            .arg((effectiveVoiceHz - cf) / 1000.0, 0, 'f', 1)
                                            .arg(inPassband ? "yes" : "NO")
                                            .arg(p25VoiceDiagLabel(code))
                                            .arg(p2bursts)
                                            .arg(p2vcw)
                                            .arg(p2sf)
                                            .arg(p2mask)
                                            .arg(p2crc)
                                            .arg(p2mac)
                                            .arg(p2AmbeAttempts)
                                            .arg(p2AmbeAccepted)
                                            .arg(p25Phase2AcchStatsText(voiceDiag))
                                            .arg(p2ess));
                                        const bool recentSpeakerDiag =
                                            p25RecentSpeakerOutputActive(acqNowMs, kP25Phase2SpeakerFollowHoldMs);
                                        const qint64 lastSpeakerMs =
                                            gP25AudioLastSpeakerOutputMs.load(std::memory_order_relaxed);
                                        const bool veryRecentSpeakerGap =
                                            lastSpeakerMs > 0 &&
                                            acqNowMs - lastSpeakerMs <= kP25Phase2SpeakerDecodeGapBlockMs;
                                        // Do not treat speaker-output timestamps alone as VCW evidence.
                                        // A stale pending-audio push can set recentSpeaker without any
                                        // real target-slot codewords and falsely open clear-trust gates.
                                        const bool vcwPresentDiag = p2vcw > 0 ||
                                            (recentSpeakerDiag && (p2bursts > 0 || p2sf > 0 || p2mask > 0));
                                        const bool superframeLockedDiag = p2sf >= 6;
                                        const bool maskLockedDiag = p2mask >= 6;
                                        const bool macTrustedDiag = p2crc > 0;
                                        const bool essTrustedDiag = p2EssKnown;
                                        const bool callClearTrustedDiag =
                                            !voiceStateEncrypted &&
                                            (voiceStateClearKnown ||
                                             (p2EssKnown && !p2EssEncrypted) ||
                                             macTrustedDiag ||
                                             (trafficStatus.present && trafficStatus.diag.audioOpen));
                                        const bool grantUnknownProbeDiag =
                                            voiceStatePhase2 &&
                                            voiceStateMaskKnown &&
                                            !voiceStateEncrypted &&
                                            !callClearTrustedDiag;
                                        const qint64 followAgeMs = p25AutoFollowTunedAtMs > 0 ? (acqNowMs - p25AutoFollowTunedAtMs) : -1;
                                        const qint64 activeAnchorMs = std::max<qint64>(
                                            p25AutoFollowLastActiveMs,
                                            lastSpeakerMs);
                                        const qint64 activeAgeMs =
                                            activeAnchorMs > 0 ? (acqNowMs - activeAnchorMs) : -1;
                                        const qint64 settleRemainingMs = std::max<qint64>(0, voiceStateSettleUntilMs - acqNowMs);
                                        QString blockReason;
                                        if (!voiceStateDecodeEnabled) blockReason = "voice-decode-disabled";
                                        else if (!voiceDiag.backendAvailable && vcwPresentDiag) blockReason = "backend-missing";
                                        else if (voiceStateResetPending) blockReason = "reset-pending";
                                        else if (settleRemainingMs > 0) blockReason = "post-arm-settle";
                                        else if (voiceStateDiscardWindows > 0) blockReason = "discard-window";
                                        else if (trafficStatus.present && trafficStatus.diag.audioOpen) blockReason = "traffic-processor-audio-open";
                                        else if (!vcwPresentDiag && trafficStatus.callActive && !recentSpeakerDiag &&
                                                 (trafficStatus.diag.p2bursts > 0 || trafficStatus.diag.p2mac > 0 ||
                                                  trafficStatus.diag.p2vcw > 0)) blockReason = "traffic-processor-call-active";
                                        else if (!vcwPresentDiag && veryRecentSpeakerGap &&
                                                 !(p2bursts > 0 || p2sf > 0 || p2mask > 0) &&
                                                 !voiceStateSessionHadVoiceLock)
                                            blockReason = "decode-cadence-gap-after-speaker";
                                        else if (!vcwPresentDiag) blockReason = "no-vcw-from-live-window";
                                        else if ((!superframeLockedDiag || !maskLockedDiag) && grantUnknownProbeDiag) blockReason = "late-entry-vocoder-probe-active";
                                        else if (!superframeLockedDiag || !maskLockedDiag) blockReason = "vcw-present-but-no-sf-mask-yet";
                                        else if (!macTrustedDiag && !essTrustedDiag && !callClearTrustedDiag) blockReason = "metadata-gate-waiting-traffic-mac-ess";
                                        else if (callClearTrustedDiag && p2AmbeAttempts == 0 && voiceDiag.phase2ExpectedVoiceCodewords > 0) blockReason = "clear-grant-vcw-not-fed";
                                        else if (callClearTrustedDiag && p2AmbeAttempts == 0 && p2vcw > 0) blockReason = "vcw-soft-or-nonvoice-filtered";
                                        else if (p2AmbeAttempts > 0 && p2AmbeAccepted == 0) blockReason = "ambe-rejected-zero-accepted";
                                        else if (code == P25VoiceDiagCode::Phase2AmbeRejected) blockReason = "ambe-fec-rejected";
                                        else if (decoded == 0) blockReason = "vocoder-produced-no-frames";
                                        else blockReason = "audio-path-open-or-near-open";
                                        appendP25LogLine(QString("TDMA DEEP DIAG: TG=%1 state{decode=%2 phase2=%3 clearKnown=%4 encrypted=%5 callClearTrusted=%6 unknownProbe=%7 slotKnown=%8 slot=%9 maskParamsKnown=%10 nac=0x%11 wacn=0x%12 sys=0x%13 resetPending=%14 slotProbePending=%15 slotProbeReq=%16 settleRemainMs=%17 discardWindows=%18} rf{voice=%19MHz cf=%20MHz offsetHz=%21 inPassband=%22 sr=%23MHz chanSr=48.00kHz followAgeMs=%24 lastActiveAgeMs=%25} live{diag=%26 sync=%27 nid=%28 nidLock=%29 imbe=%30 decoded=%31 audioSamples=%32 backend=%33 p2bursts=%34 p2vcw=%35 sf=%36 mask=%37 mac=%38/%39 %40 ess=%41 expVcw=%42 fed=%43 emit=%44 gaps=%45} gates{vcwPresent=%46 sfLocked=%47 maskLocked=%48 macTrusted=%49 essTrusted=%50 block=%51}")
                                            .arg(statusTg)
                                            .arg(voiceStateDecodeEnabled ? "yes" : "no")
                                            .arg(voiceStatePhase2 ? "yes" : "no")
                                            .arg(voiceStateClearKnown ? "yes" : "no")
                                            .arg(voiceStateEncrypted ? "yes" : "no")
                                            .arg(callClearTrustedDiag ? "yes" : "no")
                                            .arg(grantUnknownProbeDiag ? "yes" : "no")
                                            .arg(voiceStateSlotKnown ? "yes" : "no")
                                            .arg(static_cast<int>(voiceStateSlot))
                                            .arg(voiceStateMaskKnown ? "yes" : "no")
                                            .arg(voiceStateNac, 0, 16)
                                            .arg(voiceStateWacn, 0, 16)
                                            .arg(voiceStateSystemId, 0, 16)
                                            .arg(voiceStateResetPending ? "yes" : "no")
                                            .arg(voiceStateSlotProbePending ? "yes" : "no")
                                            .arg(static_cast<int>(voiceStateSlotProbeRequested))
                                            .arg(settleRemainingMs)
                                            .arg(voiceStateDiscardWindows)
                                            .arg(voiceHz / 1e6, 0, 'f', 5)
                                            .arg(cf / 1e6, 0, 'f', 5)
                                            .arg(effectiveVoiceHz - cf, 0, 'f', 1)
                                            .arg(inPassband ? "yes" : "NO")
                                            .arg(sr / 1e6, 0, 'f', 3)
                                            .arg(followAgeMs)
                                            .arg(activeAgeMs)
                                            .arg(p25VoiceDiagLabel(code))
                                            .arg(syncs)
                                            .arg(nids)
                                            .arg(voiceDiag.nidLock ? "yes" : "no")
                                            .arg(imbe)
                                            .arg(decoded)
                                            .arg(voiceDiag.audioSamples)
                                            .arg(voiceDiag.backendAvailable ? "yes" : "no")
                                            .arg(p2bursts)
                                            .arg(p2vcw)
                                            .arg(p2sf)
                                            .arg(p2mask)
                                            .arg(p2crc)
                                            .arg(p2mac)
                                            .arg(p25Phase2AcchStatsText(voiceDiag))
                                            .arg(p2ess)
                                            .arg(static_cast<qulonglong>(voiceDiag.phase2ExpectedVoiceCodewords))
                                            .arg(static_cast<qulonglong>(voiceDiag.phase2FedToMbelib))
                                            .arg(static_cast<qulonglong>(voiceDiag.phase2EmittedPcmFrames))
                                            .arg(static_cast<qulonglong>(voiceDiag.phase2FeedGaps))
                                            .arg(vcwPresentDiag ? "yes" : "no")
                                            .arg(superframeLockedDiag ? "yes" : "no")
                                            .arg(maskLockedDiag ? "yes" : "no")
                                            .arg(macTrustedDiag ? "yes" : "no")
                                            .arg(essTrustedDiag ? "yes" : "no")
                                            .arg(blockReason));
                                        // ~1 Hz cadence rollup (relaxed atomics; no DSP cost).
                                        {
                                            const long long lastCadMs =
                                                gP25Phase2Cadence.lastLogMs.load(std::memory_order_relaxed);
                                            if (lastCadMs == 0) {
                                                // Capture 20260907_095450: first CADENCE printed
                                                // windows=593 dutySec=2.320 as "1s" because lastLogMs
                                                // stayed 0 until this path ran ~53 s after arm.
                                                // SDRTrunk starts a new AudioModule stream at PTT;
                                                // start the 1 s bucket here and drop the backlog.
                                                gP25Phase2Cadence.lastLogMs.store(acqNowMs, std::memory_order_relaxed);
                                                gP25Phase2Cadence.windows.exchange(0, std::memory_order_relaxed);
                                                gP25Phase2Cadence.vcw.exchange(0, std::memory_order_relaxed);
                                                gP25Phase2Cadence.targetVcw.exchange(0, std::memory_order_relaxed);
                                                gP25Phase2Cadence.fed.exchange(0, std::memory_order_relaxed);
                                                gP25Phase2Cadence.emitted.exchange(0, std::memory_order_relaxed);
                                                gP25Phase2Cadence.dups.exchange(0, std::memory_order_relaxed);
                                                gP25Phase2Cadence.gaps.exchange(0, std::memory_order_relaxed);
                                                gP25Phase2Cadence.reject.exchange(0, std::memory_order_relaxed);
                                            } else if (acqNowMs - lastCadMs >= 1000) {
                                                gP25Phase2Cadence.lastLogMs.store(acqNowMs, std::memory_order_relaxed);
                                                const long long w = gP25Phase2Cadence.windows.exchange(0, std::memory_order_relaxed);
                                                const long long v = gP25Phase2Cadence.vcw.exchange(0, std::memory_order_relaxed);
                                                const long long tv = gP25Phase2Cadence.targetVcw.exchange(0, std::memory_order_relaxed);
                                                const long long f = gP25Phase2Cadence.fed.exchange(0, std::memory_order_relaxed);
                                                const long long e = gP25Phase2Cadence.emitted.exchange(0, std::memory_order_relaxed);
                                                const long long d = gP25Phase2Cadence.dups.exchange(0, std::memory_order_relaxed);
                                                const long long g = gP25Phase2Cadence.gaps.exchange(0, std::memory_order_relaxed);
                                                const long long r = gP25Phase2Cadence.reject.exchange(0, std::memory_order_relaxed);
                                                const double windowSec = std::max(1.0,
                                                    static_cast<double>(acqNowMs - lastCadMs) / 1000.0);
                                                const double duty = e > 0
                                                    ? (static_cast<double>(e) * 0.020) / windowSec
                                                    : 0.0;
                                                const double feedRatio = (tv > 0) ? (static_cast<double>(f) / static_cast<double>(tv)) : 0.0;
                                                P25AudioDropSample cadenceDrop;
                                                cadenceDrop.targetVcw = tv;
                                                cadenceDrop.fed = f;
                                                cadenceDrop.emittedPcm = e;
                                                cadenceDrop.dups = d;
                                                cadenceDrop.windowSeconds = windowSec;
                                                const char* dropLabel =
                                                    p25AudioDropBucketLabel(classifyP25AudioDrop(cadenceDrop));
                                                appendP25LogLineKeyed(
                                                    QString("p25-cadence:%1").arg(statusTg),
                                                    QString("P25 CADENCE 1s: TG=%1 windows=%2 vcw=%3 targetVcw=%4 fed=%5 emit=%6 dups=%7 gaps=%8 reject=%9 feedRatio=%10 dutySec=%11 drop=%12 block=%13")
                                                        .arg(statusTg)
                                                        .arg(w).arg(v).arg(tv).arg(f).arg(e).arg(d).arg(g).arg(r)
                                                        .arg(feedRatio, 0, 'f', 3)
                                                        .arg(duty, 0, 'f', 3)
                                                        .arg(QLatin1String(dropLabel))
                                                        .arg(blockReason),
                                                    900);
                                            }
                                        }
                                        if (p25Status && p25FollowAutoActive && voiceStateDecodeEnabled &&
                                            decoded == 0 && !recentSpeakerDiag) {
                                            p25Status->setText(QString("TG %1 acquiring (%2)")
                                                .arg(statusTg)
                                                .arg(blockReason));
                                        }
                                        // If the voice channel is definitely in passband and we repeatedly
                                        // see Phase 2 voice codewords but the selected grant slot never forms a
                                        // superframe, try the opposite TDMA slot once for diagnostics. Some
                                        // control-channel sources report the physical channel slot differently
                                        // from the local burst-slot convention; this prevents permanent lockout
                                        // while keeping audio gated until MAC/ESS proves clear.
                                        static uint32_t slotProbeTg = 0;
                                        static double slotProbeVoiceHz = 0.0;
                                        static qint64 slotProbeArmMs = 0;
                                        static int wrongSlotChecks = 0;
                                        static int slotProbeFlipCount = 0;
                                        static qint64 lastSlotProbeFlipMs = 0;
                                        static qint64 lastSelectedSlotAudioMs = 0;
                                        const uint32_t acqTg = static_cast<uint32_t>(statusTg > 0 ? statusTg : 0);
                                        P25SlotProbeSnapshot slotProbeSnapshot;
                                        slotProbeSnapshot.nowMs = acqNowMs;
                                        slotProbeSnapshot.tunedAtMs = p25AutoFollowTunedAtMs;
                                        slotProbeSnapshot.trackedArmMs = slotProbeArmMs;
                                        slotProbeSnapshot.lastFlipMs = lastSlotProbeFlipMs;
                                        slotProbeSnapshot.talkgroupId = acqTg;
                                        slotProbeSnapshot.trackedTalkgroupId = slotProbeTg;
                                        slotProbeSnapshot.voiceHz = voiceHz;
                                        slotProbeSnapshot.trackedVoiceHz = slotProbeVoiceHz;
                                        slotProbeSnapshot.wrongSlotChecks = wrongSlotChecks;
                                        slotProbeSnapshot.flipCount = slotProbeFlipCount;
                                        slotProbeSnapshot.maxFlips = 4;
                                        slotProbeSnapshot.wrongSlotThreshold = 3;
                                        slotProbeSnapshot.minFlipIntervalMs = 8000;
                                        slotProbeSnapshot.earlyNoSyncFlipMs =
                                            voiceStateClearKnown ? 6000 : 15000;
                                        slotProbeSnapshot.inPassband = inPassband;
                                        slotProbeSnapshot.grantClearStateUnknown =
                                            voiceStatePhase2 &&
                                            !voiceStateClearKnown &&
                                            !voiceStateEncrypted;
                                        slotProbeSnapshot.grantClearKnown = voiceStateClearKnown;
                                        slotProbeSnapshot.grantMaskParamsKnown = voiceStateMaskKnown;
                                        // Keep the allocated slot only when the current slot is actually
                                        // producing a meaningful PCM block.  Do not use
                                        // p25AutoFollowLastActiveMs here: evaluateP25Follow() updates that
                                        // timestamp for VCW/superframe activity, not just audio.  The field
                                        // regression showed p2vcw=18..36 with decoded=0 and repeated
                                        // "Phase 2 wrong TDMA slot", but the stale "recent active" hold
                                        // downgraded wrong-slot to Decoding forever and prevented the slot
                                        // probe from ever trying the opposite traffic slot.  sdrtrunk's
                                        // timeslot path stays bound to the allocated channel, but it does not
                                        // treat mere VCW presence as proof that the current slot is decoding
                                        // voice.
                                        const bool currentSlotHasUsefulAudio =
                                            decoded > 0 && voiceDiag.audioSamples > 0;
                                        if (currentSlotHasUsefulAudio) {
                                            lastSelectedSlotAudioMs = acqNowMs;
                                        }
                                        slotProbeSnapshot.diag = currentSlotHasUsefulAudio
                                            ? static_cast<int>(P25VoiceDiagCode::Decoding)
                                            : voiceDiag.diag;
                                        slotProbeSnapshot.phase2VoiceCodewords = p2vcw;
                                        slotProbeSnapshot.phase2TargetVoiceCodewords =
                                            voiceDiag.phase2TargetVoiceCodewords;
                                        slotProbeSnapshot.phase2Bursts = p2bursts;
                                        slotProbeSnapshot.phase2OppositeVoiceCodewords =
                                            voiceDiag.phase2OppositeVoiceCodewords;
                                        slotProbeSnapshot.phase2SuperframeBursts = p2sf;
                                        slotProbeSnapshot.phase2MaskedBursts = p2mask;
                                        slotProbeSnapshot.phase2MacPdus = p2mac;
                                        slotProbeSnapshot.phase2MacCrcValid = p2crc;
                                        slotProbeSnapshot.phase2EssKnown = p2EssKnown;
                                        slotProbeSnapshot.recentSelectedSlotAudio =
                                            currentSlotHasUsefulAudio || veryRecentSpeakerGap;
                                        slotProbeSnapshot.lastSelectedSlotAudioMs = lastSelectedSlotAudioMs;
                                        const auto slotProbeDecision = evaluateP25SlotProbe(slotProbeSnapshot);
                                        if (slotProbeDecision.resetTracking) {
                                            slotProbeTg = acqTg;
                                            slotProbeVoiceHz = voiceHz;
                                            slotProbeArmMs = p25AutoFollowTunedAtMs;
                                            lastSlotProbeFlipMs = 0;
                                            lastSelectedSlotAudioMs = currentSlotHasUsefulAudio ? acqNowMs : 0;
                                        }
                                        wrongSlotChecks = slotProbeDecision.wrongSlotChecksAfterObservation;
                                        slotProbeFlipCount = slotProbeDecision.flipCountAfterObservation;

                                        // Treat repeated "wrong slot" with real Phase 2 VCWs as a slot-convention
                                        // hypothesis to validate. Superframe/mask lock alone is not enough to freeze
                                        // the selected grant slot: if MAC/ESS is still absent, the opposite slot may
                                        // be the only path to a standards-valid clear/enc decision.
                                        const bool tdmaEpochLocked = slotProbeDecision.tdmaEpochLocked;
                                        const bool noMacEssYet = slotProbeDecision.noMacEssYet;

                                        static qint64 lastTdmLockedWrongSlotNoteMs = 0;
                                        if (tdmaEpochLocked && noMacEssYet && code == P25VoiceDiagCode::Phase2WrongSlot &&
                                            acqNowMs - lastTdmLockedWrongSlotNoteMs > 8000) {
                                            lastTdmLockedWrongSlotNoteMs = acqNowMs;
                                            appendP25LogLine(QString("TDMA ACQ note: superframe/mask hypothesis is present but selected TG/slot has no valid MAC/ESS yet; not treating this as audio lock. TG=%1 voice=%2MHz sf=%3 mask=%4 mac=%5/%6 ess=%7 p2vcw=%8. This usually means late-entry wait or MAC/ESS extraction still incomplete; the watchdog will return to the control channel if it does not progress.")
                                                .arg(statusTg)
                                                .arg(voiceHz / 1e6, 0, 'f', 5)
                                                .arg(p2sf)
                                                .arg(p2mask)
                                                .arg(p2crc)
                                                .arg(p2mac)
                                                .arg(p2ess)
                                                .arg(p2vcw));
                                        }

                                        // Allow more than one probe during long calls, but rate-limit it so we do
                                        // not thrash the decoder. This also fixes re-grants for the same TG/freq:
                                        // a new p25AutoFollowTunedAtMs resets the probe state.
                                        if (slotProbeDecision.shouldFlip || slotProbeDecision.earlyNoSyncFlip ||
                                            slotProbeDecision.maskedOppositeDominantFlip) {
                                            bool flipped = false;
                                            bool queued = false;
                                            uint8_t oldSlot = 0;
                                            uint8_t newSlot = 0;
                                            {
                                                std::unique_lock<std::mutex> lk(receiversMutex, std::try_to_lock);
                                                if (!lk.owns_lock()) {
                                                    appendP25LogLineKeyed("tdma-slot-probe-list-busy",
                                                        "TDMA slot auto-probe deferred because receiver list was busy; GUI timer did not block.",
                                                        3000);
                                                } else {
                                                    std::shared_ptr<Receiver> probeRx;
                                                    if (p25IndependentTrafficActive) {
                                                        const uint64_t liveGen = p25TrafficSourceGeneration.load(std::memory_order_acquire);
                                                        for (auto& candidate : receivers) {
                                                            if (candidate && candidate->p25IndependentTrafficSource &&
                                                                candidate->p25TrafficGeneration == liveGen) {
                                                                probeRx = candidate;
                                                                break;
                                                            }
                                                        }
                                                    }
                                                    if (!probeRx && !receivers.empty()) probeRx = receivers[0];
                                                    if (!probeRx) {
                                                        appendP25LogLineKeyed("tdma-slot-probe-no-rx",
                                                            "TDMA slot auto-probe deferred because no active receiver was available.",
                                                            3000);
                                                    } else {
                                                    std::unique_lock<std::mutex> rxLock(probeRx->stateMutex, std::try_to_lock);
                                                    if (!rxLock.owns_lock()) {
                                                        appendP25LogLineKeyed("tdma-slot-probe-rx-busy",
                                                            "TDMA slot auto-probe deferred because DSP owned receiver state; GUI timer did not block.",
                                                            3000);
                                                    } else {
                                                    auto& rx = *probeRx;
                                                    if (rx.p25VoiceDecodeEnabled &&
                                                        (rx.p25VoicePhase2 || p25AutoFollowVoiceFreqHz > 0.0)) {
                                                        if (p25Phase2GrantedSlotIsImmutable(rx)) {
                                                            ++rx.p25DiagSlotProbeBlocked;
                                                            rx.p25VoiceSlotProbePending = false;
                                                            rx.p25VoiceSlotProbeRequested = 0;
                                                        } else {
                                                        const bool oldKnown = rx.p25VoiceTdmaSlotKnown;
                                                        oldSlot = oldKnown ? static_cast<uint8_t>(rx.p25VoiceTdmaSlot & 0x01u) : 0u;
                                                        newSlot = oldKnown
                                                            ? static_cast<uint8_t>((oldSlot ^ 0x01u) & 0x01u)
                                                            : static_cast<uint8_t>(slotProbeFlipCount & 0x01u);

                                                        std::unique_lock<std::recursive_mutex> dspLock(rx.dspMutex, std::try_to_lock);
                                                        if (!dspLock.owns_lock()) {
                                                            rx.p25VoiceSlotProbePending = true;
                                                            rx.p25VoiceSlotProbeRequested = newSlot;
                                                            queued = true;
                                                        } else {
                                                            applyP25Phase2SlotProbeLocked(rx, newSlot, acqNowMs);
                                                            flipped = true;
                                                        }
                                                        }
                                                    }
                                                    }
                                                    }
                                                }
                                            }
                                            if (flipped || queued) {
                                                ++slotProbeFlipCount;
                                                wrongSlotChecks = 0;
                                                lastSlotProbeFlipMs = acqNowMs;
                                                const QString probeReason = slotProbeDecision.maskedOppositeDominantFlip
                                                    ? "masked superframe with opposite-slot VCWs and no target-slot voice"
                                                    : (slotProbeDecision.earlyNoSyncFlip
                                                        ? "unknown-grant no-sync/no-target-vcw acquisition timeout"
                                                        : "repeated wrong-slot diagnostics with VCWs present");
                                                appendP25LogLine(QString("TDMA ACQ slot auto-probe: %1; %2 slot %3 -> %4 for TG=%5 voice=%6MHz sf=%7 mask=%8 mac=%9/%10 ess=%11. Audio remains gated until lock.")
                                                    .arg(probeReason)
                                                    .arg(flipped ? "switching" : "queued switch")
                                                    .arg(static_cast<int>(oldSlot))
                                                    .arg(static_cast<int>(newSlot))
                                                    .arg(statusTg)
                                                    .arg(voiceHz / 1e6, 0, 'f', 5)
                                                    .arg(p2sf)
                                                    .arg(p2mask)
                                                    .arg(p2crc)
                                                    .arg(p2mac)
                                                    .arg(p2ess));
                                            }
                                        }
                                        lastTdmaAcqStatusMs = acqNowMs;
                                    }
                                }

                                static int lastVoiceDiag = -1;
                                static long long lastVoiceTalkgroup = -1;
                                static long long lastVoiceP2Bursts = -1;
                                static long long lastVoiceP2Vcw = -1;
                                static long long lastVoiceDecoded = -1;
                                static long long lastVoiceP2Sf = -1;
                                static long long lastVoiceP2Mask = -1;
                                static long long lastVoiceP2Crc = -1;
                                static int lastVoiceSlot = -1;
                                const int diagInt = static_cast<int>(code);
                                const bool metricsChanged =
                                    p2bursts != lastVoiceP2Bursts ||
                                    p2vcw != lastVoiceP2Vcw ||
                                    decoded != lastVoiceDecoded ||
                                    p2sf != lastVoiceP2Sf ||
                                    p2mask != lastVoiceP2Mask ||
                                    p2crc != lastVoiceP2Crc ||
                                    static_cast<int>(voiceStateSlot) != lastVoiceSlot;
                                if (diagInt != lastVoiceDiag || statusTg != lastVoiceTalkgroup || metricsChanged) {
                                    appendP25LogLine(QString("P25 voice follow: TG %1 %2 sync=%3 nid=%4 imbe=%5 decoded=%6 audio=%7 p2bursts=%8 p2vcw=%9 p2sf=%10 p2mask=%11 p2mac=%12/%13 %14 p2ess=%15 backend=%16 nidLock=%17 stateDecode=%18 slot=%19/%20 maskParams=%21 settleMs=%22 discard=%23.")
                                        .arg(statusTg)
                                        .arg(p25VoiceDiagLabel(code))
                                        .arg(syncs)
                                        .arg(nids)
                                        .arg(imbe)
                                        .arg(decoded)
                                        .arg(voiceDiag.audioSamples)
                                        .arg(p2bursts)
                                        .arg(p2vcw)
                                        .arg(p2sf)
                                        .arg(p2mask)
                                        .arg(p2crc)
                                        .arg(p2mac)
                                        .arg(p25Phase2AcchStatsText(voiceDiag))
                                        .arg(p2ess)
                                        .arg(voiceDiag.backendAvailable ? "yes" : "no")
                                        .arg(voiceDiag.nidLock ? "yes" : "no")
                                        .arg(voiceStateDecodeEnabled ? "yes" : "no")
                                        .arg(voiceStateSlotKnown ? "known" : "unknown")
                                        .arg(static_cast<int>(voiceStateSlot))
                                        .arg(voiceStateMaskKnown ? "yes" : "no")
                                        .arg(std::max<qint64>(0, voiceStateSettleUntilMs - QDateTime::currentMSecsSinceEpoch()))
                                        .arg(voiceStateDiscardWindows));
                                    if (code == P25VoiceDiagCode::Phase2LateEntryWaiting && p2vcw > 0 && p2mask > 0) {
                                        appendP25LogLine(QString("Phase 2 late entry: voice bursts present, mask applied, waiting for MAC CRC/ESS before audio release. TG=%1 p2vcw=%2 p2mask=%3 p2mac=%4/%5 p2ess=%6.")
                                            .arg(statusTg)
                                            .arg(p2vcw)
                                            .arg(p2mask)
                                            .arg(p2crc)
                                            .arg(p2mac)
                                            .arg(p2ess));
                                        if (p2crc > 0 && p2ess == "unknown") {
                                            appendP25LogLine(QString("TDMA voice present, mask OK, MAC CRC OK, waiting ESS before audio release. TG=%1.")
                                                .arg(statusTg));
                                        }
                                    }
                                    lastVoiceDiag = diagInt;
                                    lastVoiceTalkgroup = statusTg;
                                    lastVoiceP2Bursts = p2bursts;
                                    lastVoiceP2Vcw = p2vcw;
                                    lastVoiceDecoded = decoded;
                                    lastVoiceP2Sf = p2sf;
                                    lastVoiceP2Mask = p2mask;
                                    lastVoiceP2Crc = p2crc;
                                    lastVoiceSlot = static_cast<int>(voiceStateSlot);
                                }
                                if (p25StatusLabel && p25FollowAutoActive && statusTg > 0) {
                                    p25StatusLabel->setText(QString("TG %1 %2 b=%3 vcw=%4 sf=%5 mask=%6 mac=%7/%8")
                                        .arg(statusTg)
                                        .arg(p25VoiceDiagLabel(code))
                                        .arg(p2bursts)
                                        .arg(p2vcw)
                                        .arg(p2sf)
                                        .arg(p2mask)
                                        .arg(p2crc)
                                        .arg(p2mac));
                                }
                            }
                        }
                        break;
                    }
                }
            } catch (const std::exception& ex) {
                static std::atomic<qint64> lastLoggedMs{0};
                const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
                qint64 prev = lastLoggedMs.load(std::memory_order_relaxed);
                if (nowMs - prev > 5000 && lastLoggedMs.compare_exchange_strong(
                        prev, nowMs, std::memory_order_relaxed)) {
                    spdlog::warn("P25 GUI monitor exception at {:.5f} MHz: {}",
                        p25MonitoredControlFreqHz / 1e6, ex.what());
                }
            } catch (...) {
                static std::atomic<qint64> lastLoggedMs{0};
                const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
                qint64 prev = lastLoggedMs.load(std::memory_order_relaxed);
                if (nowMs - prev > 5000 && lastLoggedMs.compare_exchange_strong(
                        prev, nowMs, std::memory_order_relaxed)) {
                    spdlog::warn("P25 GUI monitor unknown exception at {:.5f} MHz",
                        p25MonitoredControlFreqHz / 1e6);
                }
            }
        });
        QTimer::singleShot(200, this, [this]() {
            if (updateTimer && !updateTimer->isActive()) updateTimer->start(50); // 20 Hz UI pump; spectrum path also self-throttles
        });

        // Dedicated background DSP worker thread for the GUI monitor path.
        // Owned (not detached) so we can join on shutdown. Uses stop flag.
        startP25VoiceWorker();
        stopDspWorker.store(false, std::memory_order_release);
        guiDspWorker = std::thread([this]() {
            auto& mgr = DeviceManager::instance();
            std::map<ReceiverSessionKey, std::chrono::steady_clock::time_point> lastPhase2DecodeByRx;
            P25SpeakerPendingMap pendingAudioByRx;
            std::map<ReceiverSessionKey, RollingIqWindow> phase2IqByRx;
            uint64_t lastTrafficGenerationSeen = 0;
            uint64_t lastPendingAudioFlushSeq = 0;
            std::deque<P25VoiceDecodeResult> pendingVoicePublishResults;
            auto drainP25VoiceResults = [&]() {
                for (auto& result : takeP25VoiceDecodeResults()) {
                    pendingVoicePublishResults.push_back(std::move(result));
                    p25VoicePendingPublishDepth.store(pendingVoicePublishResults.size(),
                        std::memory_order_release);
                }
                auto purgeLocalPublishResultsForSession =
                    [&](const ReceiverSessionKey& sessionKey, uint64_t afterSequence) -> size_t {
                    size_t purged = 0;
                    for (auto it = std::next(pendingVoicePublishResults.begin());
                         it != pendingVoicePublishResults.end();) {
                        if (it->receiverSessionKey == sessionKey &&
                            (afterSequence == 0 || it->sequence > afterSequence)) {
                            it = pendingVoicePublishResults.erase(it);
                            ++purged;
                        } else {
                            ++it;
                        }
                    }
                    if (purged > 0) {
                        p25VoiceDroppedResults.fetch_add(purged, std::memory_order_relaxed);
                        p25VoicePendingPublishDepth.store(pendingVoicePublishResults.size(),
                            std::memory_order_release);
                    }
                    return purged;
                };
                bool drained = false;
                while (!pendingVoicePublishResults.empty()) {
                    auto& result = pendingVoicePublishResults.front();
                    if (result.rx) {
                        std::unique_lock<std::mutex> lk(receiversMutex, std::try_to_lock);
                        if (!lk.owns_lock()) {
                            p25VoicePublicationLockMisses.fetch_add(1, std::memory_order_relaxed);
                            break;
                        }
                        bool rxStillValid = false;
                        for (auto& r : receivers) {
                            if (r && r.get() == result.rx.get()) {
                                rxStillValid = true;
                                break;
                            }
                        }
                        if (!rxStillValid) {
                            if (result.rx && result.rollingDecode && result.iqDecodeEndAbsoluteKnown) {
                                auto rollingIt = phase2IqByRx.find(result.receiverSessionKey);
                                if (rollingIt != phase2IqByRx.end()) {
                                    rollingIt->second.rollbackSubmittedDecode();
                                }
                            }
                            pendingVoicePublishResults.pop_front();
                            p25VoicePendingPublishDepth.store(pendingVoicePublishResults.size(),
                                std::memory_order_release);
                            drained = true;
                            continue;
                        }
                    }
                    if (result.rx && result.rollingDecode && result.iqDecodeEndAbsoluteKnown) {
                        auto rollingIt = phase2IqByRx.find(result.receiverSessionKey);
                        if (rollingIt != phase2IqByRx.end() &&
                            !rollingIt->second.resultCoversHeldDecodeRange(
                                result.iqStartAbsolute,
                                result.iqStartAbsoluteKnown,
                                result.iqDecodeEndAbsolute,
                                result.iqDecodeEndAbsoluteKnown)) {
                            rollingIt->second.rollbackSubmittedDecode();
                            const size_t localPurged = purgeLocalPublishResultsForSession(
                                result.receiverSessionKey, result.sequence);
                            const P25VoiceDecodeWorkPurge workerPurged =
                                purgeP25VoiceDecodeWorkForSession(result.receiverSessionKey,
                                    result.sequence);
                            const uintptr_t rxKey = reinterpret_cast<uintptr_t>(result.rx.get());
                            const uint64_t seqLog = result.sequence;
                            const qulonglong localPurgedLog =
                                static_cast<qulonglong>(localPurged);
                            const qulonglong workerJobsPurgedLog =
                                static_cast<qulonglong>(workerPurged.pendingJobs);
                            const qulonglong workerResultsPurgedLog =
                                static_cast<qulonglong>(workerPurged.completedResults);
                            QTimer::singleShot(0, this, [this, rxKey, seqLog, localPurgedLog,
                                                          workerJobsPurgedLog,
                                                          workerResultsPurgedLog]() {
                                appendP25LogLineKeyed(
                                    QString("p25-rolling-held-result-drop:%1").arg(static_cast<qulonglong>(rxKey)),
                                    QString("P25 rolling held-result drop: seq=%1 did not cover held selected-slot RF; purged publish=%2 jobs=%3 completed=%4.")
                                        .arg(static_cast<qulonglong>(seqLog))
                                        .arg(localPurgedLog)
                                        .arg(workerJobsPurgedLog)
                                        .arg(workerResultsPurgedLog),
                                    750);
                            });
                            pendingVoicePublishResults.pop_front();
                            p25VoicePendingPublishDepth.store(pendingVoicePublishResults.size(),
                                std::memory_order_release);
                            drained = true;
                            continue;
                        }
                    }
                    const P25VoicePublishOutcome outcome =
                        publishP25VoiceDecodeResult(result, pendingAudioByRx);
                    if (outcome == P25VoicePublishOutcome::Deferred) {
                        break;
                    }
                    if (result.rx && result.rollingDecode && result.iqDecodeEndAbsoluteKnown) {
                        auto rollingIt = phase2IqByRx.find(result.receiverSessionKey);
                        if (rollingIt != phase2IqByRx.end()) {
                            const bool consumedRollingWindow =
                                outcome == P25VoicePublishOutcome::Published &&
                                result.hasAudioBlock &&
                                p25Phase2RollingDecodeWindowConsumed(result.audio);
                            if (outcome == P25VoicePublishOutcome::Published &&
                                result.hasAudioBlock &&
                                consumedRollingWindow) {
                                rollingIt->second.commitDecodeAbsolute(result.iqDecodeEndAbsolute);
                            } else if (outcome == P25VoicePublishOutcome::DiscardedStale ||
                                       outcome == P25VoicePublishOutcome::ReceiverGone ||
                                       (outcome == P25VoicePublishOutcome::Published &&
                                         !result.hasAudioBlock)) {
                                rollingIt->second.rollbackSubmittedDecode();
                            } else if (outcome == P25VoicePublishOutcome::Published &&
                                       result.hasAudioBlock &&
                                       !consumedRollingWindow) {
                                // DEC-0037: hold the rolling range for a MAC/ESS
                                // retry, but do NOT purge newer publish/jobs.
                                // Capture 095846 TG 30302: hold+purge turned one
                                // unqueued eye into a silent follow. Keep later
                                // near-live work; overlap will still re-see this RF.
                                rollingIt->second.holdDecodeAbsolute(result.iqDecodeEndAbsolute);
                                const uintptr_t rxKey = reinterpret_cast<uintptr_t>(result.rx.get());
                                const uint64_t seqLog = result.sequence;
                                const qulonglong targetVcwLog = static_cast<qulonglong>(result.audio.phase2TargetVoiceCodewords);
                                const qulonglong expVcwLog = static_cast<qulonglong>(result.audio.phase2ExpectedVoiceCodewords);
                                const qulonglong fedLog = static_cast<qulonglong>(result.audio.phase2FedToMbelib);
                                const qulonglong queuedLog = static_cast<qulonglong>(result.audio.phase2PendingAmbeFramesQueued);
                                const qulonglong ctxDropLog = static_cast<qulonglong>(result.audio.phase2ContextSuppressedVoiceCodewords);
                                const qulonglong dupLog = static_cast<qulonglong>(result.audio.phase2DuplicateSuppressedVoiceCodewords);
                                const qulonglong absDupLog = static_cast<qulonglong>(result.audio.phase2AbsoluteDuplicateSuppressedVoiceCodewords);
                                const qulonglong seqDropLog = static_cast<qulonglong>(result.audio.phase2SequencerSuppressedVoiceCodewords);
                                const qulonglong rejectLog = static_cast<qulonglong>(result.audio.phase2RejectedVoiceCodewords);
                                const qulonglong wrongSlotLog = static_cast<qulonglong>(result.audio.phase2WrongSlotVoiceCodewords);
                                QTimer::singleShot(0, this, [this, rxKey, seqLog, targetVcwLog,
                                                             expVcwLog, fedLog, queuedLog,
                                                             ctxDropLog, dupLog, absDupLog,
                                                             seqDropLog, rejectLog, wrongSlotLog]() {
                                    appendP25LogLineKeyed(QString("p25-rolling-cursor-hold:%1").arg(static_cast<qulonglong>(rxKey)),
                                        QString("P25 rolling cursor hold: seq=%1 targetVcw=%2 expVcw=%3 fed=%4 pendingQueued=%5 ctxDrop=%6 dup=%7 absDup=%8 seqDrop=%9 reject=%10 wrongSlot=%11; selected voice not consumed, retrying with later MAC/ESS/context (no purge).")
                                            .arg(static_cast<qulonglong>(seqLog))
                                            .arg(targetVcwLog)
                                            .arg(expVcwLog)
                                            .arg(fedLog)
                                            .arg(queuedLog)
                                            .arg(ctxDropLog)
                                            .arg(dupLog)
                                            .arg(absDupLog)
                                            .arg(seqDropLog)
                                            .arg(rejectLog)
                                            .arg(wrongSlotLog),
                                        750);
                                });
                            }
                        }
                    }
                    if (outcome == P25VoicePublishOutcome::Published &&
                        result.rx && result.hasAudioBlock &&
                        (p25Phase2ShouldFlushStaleVoicePipeline(result.audio) ||
                         p25Phase2ShouldFlushAudioTail(result.audio))) {
                        Receiver& rx = *result.rx;
                        phase2IqByRx.erase(p25ReceiverSessionKey(rx));
                        p25Phase2ClearSpeakerPendingQueue(rx,
                                                          p25SpeakerPendingFor(pendingAudioByRx, rx),
                                                          P25PendingClearReason::RetuneOrGeneration);
                        lastPhase2DecodeByRx.erase(p25ReceiverSessionKey(rx));
                        size_t devIndex = 0;
                        bool haveDev = false;
                        {
                            std::unique_lock<std::mutex> rxLock(rx.stateMutex, std::try_to_lock);
                            if (rxLock.owns_lock() && rx.active) {
                                devIndex = rx.deviceIndex;
                                haveDev = true;
                            }
                        }
                        if (haveDev) {
                            DeviceManager::instance().setReceiverCursorToLiveEdge(devIndex, rx);
                        }
                    }
                    pendingVoicePublishResults.pop_front();
                    p25VoicePendingPublishDepth.store(pendingVoicePublishResults.size(),
                        std::memory_order_release);
                    drained = true;
                }
                return drained;
            };
            while (!stopDspWorker.load(std::memory_order_acquire)) {
                bool didWork = false;
                for (int drainPass = 0; drainPass < 6; ++drainPass) {
                    if (!drainP25VoiceResults()) break;
                    didWork = true;
                }
                const uint64_t flushSeq = p25PendingAudioFlushSeq.load(std::memory_order_acquire);
                if (flushSeq != lastPendingAudioFlushSeq) {
                    pendingAudioByRx.clear();
                    phase2IqByRx.clear();
                    lastPhase2DecodeByRx.clear();
                    lastPendingAudioFlushSeq = flushSeq;
                }
                const uint64_t liveTrafficGeneration =
                    p25TrafficSourceGeneration.load(std::memory_order_acquire);
                if (liveTrafficGeneration != 0 && liveTrafficGeneration != lastTrafficGenerationSeen) {
                    pendingAudioByRx.clear();
                    phase2IqByRx.clear();
                    lastPhase2DecodeByRx.clear();
                    lastTrafficGenerationSeen = liveTrafficGeneration;
                }
                // S0-2 (P0 audit): short lock to snapshot the current receivers (shared_ptrs — cheap, stable).
                // Then process without holding lock. shared_ptr keeps the Demodulator alive even if vector reallocates.
                std::vector<std::shared_ptr<Receiver>> rxSnapshot;
                {
                    // try_to_lock: never stall the DSP worker behind a GUI grant path
                    // that (historically) held receiversMutex while waiting on stateMutex.
                    std::unique_lock<std::mutex> lk(receiversMutex, std::try_to_lock);
                    if (lk.owns_lock()) {
                        ensureReceiver();
                        rxSnapshot.reserve(receivers.size());
                        for (auto& r : receivers) if (r && r->active) rxSnapshot.push_back(r);
                    }
                }
                if (rxSnapshot.empty()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                    continue;
                }
                auto receiverSessionStillActive = [&rxSnapshot](const ReceiverSessionKey& key) {
                    return std::any_of(rxSnapshot.begin(), rxSnapshot.end(),
                        [&key](const std::shared_ptr<Receiver>& r) {
                            return r.get() == key.receiver &&
                                r->p25TrafficSessionGeneration.load(std::memory_order_acquire) == key.generation;
                        });
                };
                for (auto it = lastPhase2DecodeByRx.begin(); it != lastPhase2DecodeByRx.end();) {
                    it = receiverSessionStillActive(it->first) ? std::next(it) : lastPhase2DecodeByRx.erase(it);
                }
                for (auto it = pendingAudioByRx.begin(); it != pendingAudioByRx.end();) {
                    it = receiverSessionStillActive(it->first) ? std::next(it) : pendingAudioByRx.erase(it);
                }
                for (auto it = phase2IqByRx.begin(); it != phase2IqByRx.end();) {
                    it = receiverSessionStillActive(it->first) ? std::next(it) : phase2IqByRx.erase(it);
                }
                for (size_t r = 0; r < rxSnapshot.size() && !stopDspWorker.load(std::memory_order_acquire); ++r) {
                    auto& rxPtr = rxSnapshot[r];
                    if (!rxPtr) continue;
                    Receiver& rx = *rxPtr;  // reference to the stable object
                    size_t i = 0;
                    bool rxP25VoiceDecodeSnapshot = false;
                    bool rxP25VoicePhase2Snapshot = false;
                    bool skipPausedOneRtlControlReceiver = false;

                    {
                        std::lock_guard<std::mutex> rxLock(rx.stateMutex);
                        if (!rx.active) continue;
                        i = rx.deviceIndex;
                        rxP25VoiceDecodeSnapshot = rx.p25VoiceDecodeEnabled;
                        rxP25VoicePhase2Snapshot = rx.p25VoicePhase2;
                        skipPausedOneRtlControlReceiver =
                            p25IndependentTrafficActive &&
                            p25IndependentTrafficRetunedPrimary &&
                            rx.p25ControlChannelMute &&
                            !rx.p25VoiceDecodeEnabled &&
                            !rx.p25IndependentTrafficSource;
                    }
                    auto logP25VoiceSchedulerEarly = [&](const QString& reason,
                                                         const QString& detail,
                                                         int throttleMs = 1000) {
                        const uintptr_t rxKey = reinterpret_cast<uintptr_t>(&rx);
                        QTimer::singleShot(0, this, [this, rxKey, reason, detail, throttleMs]() {
                            appendP25LogLineKeyed(QString("p25-voice-scheduler:%1:%2")
                                    .arg(static_cast<qulonglong>(rxKey))
                                    .arg(reason),
                                QString("P25 voice scheduler: %1 %2").arg(reason, detail),
                                throttleMs);
                        });
                    };
                    if (skipPausedOneRtlControlReceiver) {
                        mgr.setReceiverCursorToLiveEdge(i, rx);
                        didWork = true;
                        continue;
                    }
                    if (i >= mgr.getDevices().size() || !mgr.isStreaming(i)) {
                        if (rxP25VoiceDecodeSnapshot && rxP25VoicePhase2Snapshot) {
                            logP25VoiceSchedulerEarly("stream-not-ready",
                                QString("device=%1 valid=%2 streaming=%3; voice follow is armed but no IQ can be pulled.")
                                    .arg(i)
                                    .arg(i < mgr.getDevices().size() ? "yes" : "no")
                                    .arg((i < mgr.getDevices().size() && mgr.isStreaming(i)) ? "yes" : "no"),
                                1500);
                        }
                        continue;
                    }

                    std::vector<float> pwr; double cf = 0.0, sr = 0.0;
                    if (!mgr.getLatestSpectrum(i, pwr, cf, sr) || sr <= 0.0) {
                        bool canUseVoiceFallback = false;
                        double fallbackCenterHz = 0.0;
                        {
                            std::lock_guard<std::mutex> rxLock(rx.stateMutex);
                            canUseVoiceFallback = rx.active && rx.deviceIndex == i && rx.p25VoiceDecodeEnabled;
                            fallbackCenterHz = p25Phase2TrafficSourceCenterHz(rx);
                        }
                        if (!canUseVoiceFallback) {
                            if (rxP25VoiceDecodeSnapshot && rxP25VoicePhase2Snapshot) {
                                logP25VoiceSchedulerEarly("no-spectrum-or-fallback",
                                    QString("device=%1; latest spectrum unavailable and voice fallback is not armed.")
                                        .arg(i),
                                    1500);
                            }
                            continue;
                        }
                        const auto devices = mgr.getDevices();
                        if (i < devices.size()) sr = devices[i].sampleRate;
                        if (!std::isfinite(sr) || sr <= 0.0) sr = 2.048e6;
                        cf = (std::isfinite(fallbackCenterHz) && fallbackCenterHz > 0.0)
                            ? fallbackCenterHz
                            : 100e6;
                        pwr.clear();
                    }

                    std::vector<float> ch;
                    double rms = -100;
                    long long dspMicros = 0;
                    double afcOffsetHz = 0.0;
                    RfSquelchMetrics rfMetrics;
                    std::vector<size_t> rxAudioOutputs;
                    double monFreq = 100e6, monLpf = 15000.0, monSquelch = -80.0;
                    double monGain = 1.0, monWfmDe = 75.0, monWfmNotch = 0.96, monBw = 180000.0;
                    double demodFreq = 100e6;
                    double rfSquelchLevel = std::numeric_limits<double>::quiet_NaN();
                    DemodMode monMode = DemodMode::AUTO;
                    bool monAudioLpfEnabled = true;
                    bool monP25ControlMute = false;
                    bool monP25VoiceDecode = false;
                    bool monP25VoicePhase2 = false;
                    bool monP25IndependentTrafficSource = false;
                    uint64_t monP25TrafficGeneration = 0;
                    uint32_t monP25VoiceTalkgroupId = 0;
                    uint32_t monP25VoiceSourceId = 0;
                    bool monP25VoiceTdmaSlotKnown = false;
                    uint8_t monP25VoiceTdmaSlot = 0;
                    double monP25TrafficVoiceFreqHz = 0.0;
                    bool skipP25VoiceWindow = false;
                    bool p25VoiceOutputMutedForSettle = false;
                    bool appliedQueuedSlotProbe = false;
                    uint8_t appliedQueuedSlot = 0;
                    bool appliedQueuedVoiceReset = false;
                    bool phase2BufferedDecode = false;
                    bool phase2SustainDecodeWindow = false;
                    bool phase2SessionHadVoiceLock = false;
                    bool phase2SessionHadBurstEye = false;
                    bool phase2SessionSpeakerSustain = false;
                    bool phase2StableSuperframeLock = false;
                    bool phase2WideReacquireWindow = false;
                    bool phase2MaskEpochRepairWindow = false;
                    bool phase2EstablishedClearStreaming = false;
                    bool phase2HardTargetAcquire = false;
                    bool phase2UseRecentTrafficWindow = false;
                    bool phase2VoiceQueueSustainHint = false;
                    uint64_t iqStartAbsolute = 0;
                    bool iqStartAbsoluteKnown = false;
                    uint64_t iqDecodeEndAbsolute = 0;
                    bool iqDecodeEndAbsoluteKnown = false;
                    size_t phase2FreshIqSamples = 0;
                    size_t phase2ContextIqSamples = 0;
                    std::vector<std::complex<float>> iq;
                    AudioEngine* audioOutputEngine = nullptr;
                    size_t audioActiveOutputCount = 0;
                    size_t audioQueuedSamples = 0;
                    double audioRingFillPercent = 0.0;
                    int audioUnderrunCount = 0;

                    auto logP25VoiceScheduler = [&](const QString& reason,
                                                     const QString& detail,
                                                     int throttleMs = 1000) {
                        const uintptr_t rxKey = reinterpret_cast<uintptr_t>(&rx);
                        QTimer::singleShot(0, this, [this, rxKey, reason, detail, throttleMs]() {
                            appendP25LogLineKeyed(QString("p25-voice-scheduler:%1:%2")
                                    .arg(static_cast<qulonglong>(rxKey))
                                    .arg(reason),
                                QString("P25 voice scheduler: %1 %2").arg(reason, detail),
                                throttleMs);
                        });
                    };

                    {
                        std::lock_guard<std::mutex> rxLock(rx.stateMutex);
                        if (!rx.active || rx.deviceIndex != i) continue;
                        if (rx.p25IndependentTrafficSource) {
                            const uint64_t liveGen = p25TrafficSourceGeneration.load(std::memory_order_acquire);
                            if (rx.p25TrafficGeneration == 0 || rx.p25TrafficGeneration != liveGen) {
                                rx.active = false;
                                rx.p25VoiceDecodeEnabled = false;
                                continue;
                            }
                        }
                        monFreq = rx.freqHz;
                        monMode = rx.mode;
                        monBw = rx.channelBwHz;
                        monLpf = rx.lpfHz;
                        monAudioLpfEnabled = rx.audioLpfEnabled;
                        monSquelch = rx.squelchDb;
                        monGain = rx.audioGain;
                        monWfmDe = rx.wfmDeTauUs;
                        monWfmNotch = rx.wfmPilotNotchR;
                        monP25ControlMute = rx.p25ControlChannelMute;
                        monP25VoiceDecode = rx.p25VoiceDecodeEnabled;
                        monP25VoicePhase2 = rx.p25VoicePhase2;
                        monP25IndependentTrafficSource = rx.p25IndependentTrafficSource;
                        monP25TrafficGeneration = rx.p25TrafficGeneration;
                        monP25VoiceTalkgroupId = rx.p25VoiceTalkgroupId;
                        monP25VoiceSourceId = rx.p25VoiceSourceId;
                        monP25VoiceTdmaSlotKnown = rx.p25VoiceTdmaSlotKnown;
                        monP25VoiceTdmaSlot = static_cast<uint8_t>(rx.p25VoiceTdmaSlot & 0x01u);
                        monP25TrafficVoiceFreqHz = rx.p25TrafficVoiceFreqHz > 0.0 ? rx.p25TrafficVoiceFreqHz : rx.freqHz;
                        phase2SustainDecodeWindow = p25Phase2UseSustainDecodeWindowLocked(rx);
                        phase2SessionHadVoiceLock = p25Phase2SessionHadVoiceLock(rx);
                        phase2SessionHadBurstEye = p25Phase2SessionHadBurstEye(rx);
                        phase2SessionSpeakerSustain = p25Phase2SessionSpeakerSustainActive(rx);
                        phase2StableSuperframeLock = p25Phase2HasStableSuperframeLockLocked(rx);
                        phase2WideReacquireWindow = p25Phase2NeedsWideReacquireWindowLocked(rx);
                        phase2MaskEpochRepairWindow = rx.p25Phase2MaskEpochRepairHoldWindows > 0;
                        phase2EstablishedClearStreaming = p25Phase2EstablishedClearVoiceStreamingLocked(rx);
                        phase2HardTargetAcquire = p25Phase2SessionHasHardTargetAcquire(rx);
                        if (monP25VoiceDecode && rx.p25IndependentTrafficSource && rx.p25TrafficRetunesPrimary) {
                            const double trafficCenterHz = p25Phase2TrafficSourceCenterHz(rx);
                            if (std::isfinite(trafficCenterHz) && trafficCenterHz > 0.0) {
                                // Spectrum metadata can lag one-RTL retunes by a UI/DSP tick.
                                // The IQ ring after the retune is centered on the traffic source
                                // RF, which can be a low-IF center distinct from the granted voice.
                                cf = trafficCenterHz;
                            }
                        }
                        if (rx.p25VoiceResetPending) {
                            if (tryApplyP25VoiceResetLocked(rx)) {
                                monP25VoiceDecode = rx.p25VoiceDecodeEnabled;
                                monP25VoicePhase2 = rx.p25VoicePhase2;
                                monP25VoiceSourceId = rx.p25VoiceSourceId;
                                phase2IqByRx.erase(p25ReceiverSessionKey(rx));
                                p25Phase2ClearSpeakerPendingQueue(rx,
                                                          p25SpeakerPendingFor(pendingAudioByRx, rx),
                                                          P25PendingClearReason::RetuneOrGeneration);
                                if (AudioEngine* eng = peekAudioEngineIfReady()) {
                                    eng->clearBuffers();
                                }
                                appliedQueuedVoiceReset = true;
                            }
                        }
                        if (rx.p25Phase2SpeakerPlaybackClearPending) {
                            p25Phase2ClearSpeakerPendingQueue(rx,
                                                      p25SpeakerPendingFor(pendingAudioByRx, rx),
                                                      P25PendingClearReason::RetuneOrGeneration);
                            rx.p25Phase2SpeakerPlaybackClearPending = false;
                            if (AudioEngine* eng = peekAudioEngineIfReady()) {
                                eng->clearBuffers();
                            }
                            const uint32_t tgLog = rx.p25VoiceTalkgroupId;
                            QTimer::singleShot(0, this, [this, tgLog]() {
                                appendP25LogLineKeyed(
                                    QString("p25-speaker-playback-cleared:%1").arg(tgLog),
                                    QString("P25 speaker playback cleared after same-call hop: TG=%1 pending+ring flushed.")
                                        .arg(tgLog),
                                    1500);
                            });
                        }
                        if (rx.p25VoiceSlotProbePending && rx.p25VoiceDecodeEnabled && rx.p25VoicePhase2) {
                            if (p25Phase2GrantedSlotIsImmutable(rx)) {
                                ++rx.p25DiagSlotProbeBlocked;
                                rx.p25VoiceSlotProbePending = false;
                                rx.p25VoiceSlotProbeRequested = 0;
                            } else {
                                std::unique_lock<std::recursive_mutex> dspLock(rx.dspMutex, std::try_to_lock);
                                if (dspLock.owns_lock()) {
                                    appliedQueuedSlot =
                                        static_cast<uint8_t>(rx.p25VoiceSlotProbeRequested & 0x01u);
                                    const bool probeApplied = applyP25Phase2SlotProbeLocked(
                                        rx, appliedQueuedSlot, QDateTime::currentMSecsSinceEpoch());
                                    monP25VoiceDecode = rx.p25VoiceDecodeEnabled;
                                    monP25VoicePhase2 = rx.p25VoicePhase2;
                                    monP25VoiceSourceId = rx.p25VoiceSourceId;
                                    if (probeApplied) {
                                        skipP25VoiceWindow = true;
                                        mgr.setReceiverCursorToLiveEdge(i, rx);
                                        phase2IqByRx.erase(p25ReceiverSessionKey(rx));
                                        p25Phase2ClearSpeakerPendingQueue(rx,
                                                          p25SpeakerPendingFor(pendingAudioByRx, rx),
                                                          P25PendingClearReason::RetuneOrGeneration);
                                        appliedQueuedSlotProbe = true;
                                    }
                                }
                            }
                        }
                        rxAudioOutputs = rx.audioOutputIndices;
                        if (monP25VoiceDecode) {
                            const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
                            p25VoiceOutputMutedForSettle = rx.p25VoiceSettleUntilMs > nowMs;
                            // sdrtrunk starts the traffic-channel decoder immediately and
                            // gates speaker audio until clear/encrypted state is known. Do
                            // not skip Phase-2 decode during retune/settle; the first
                            // hundreds of milliseconds often contain the PTT/MAC/ESS that
                            // late entry needs.  Only explicit slot-probe resets set
                            // skipP25VoiceWindow above.
                            if (!p25VoiceOutputMutedForSettle && rx.p25VoiceDiscardWindows > 0) {
                                rx.p25VoiceDiscardWindows = 0;
                            }
                        }
                        if (!monP25VoiceDecode && monMode == DemodMode::AUTO && !pwr.empty()) {
                            auto smart = chooseSmartModeAndBandwidth(pwr, sr, cf, monFreq, DemodMode::AUTO, nullptr);
                            monMode = smart.mode;
                            monBw = smart.bandwidthHz;
                            monLpf = smart.lpfHz;
                        }

                        demodFreq = monP25VoiceDecode
                            ? p25VoiceAfcTargetHz(rx, p25Phase2VoiceSchedulerNominalHz(rx), monBw)
                            : applyNfmAfcFromSpectrum(rx, pwr, sr, cf, monFreq, monBw, monMode);
                        // P25 TDMA acquisition must stay centered on the granted channel.
                        const double voiceNominalHz = monP25VoiceDecode
                            ? p25Phase2VoiceSchedulerNominalHz(rx)
                            : monFreq;
                        afcOffsetHz = demodFreq - voiceNominalHz;

                        rfMetrics = computeRfSquelchMetrics(pwr, sr, cf, demodFreq, monBw, monMode);
                        rfSquelchLevel = rfMetrics.valid
                            ? rfMetrics.signalLevelDb
                            : std::numeric_limits<double>::quiet_NaN();

                        // audit-followup-2: use per-rx cursor consumption. Each rx pulls only its own *new* chronological samples.
                        // No more "demod the latest 25ms overlapping window" for every rx.
                        phase2BufferedDecode = monP25VoiceDecode && monP25VoicePhase2;
                        // Traffic receivers have private absolute-sample cursors.  Use
                        // those cursors for Phase 2 too; polling the newest recent ring
                        // window can replay pre-retune control-channel IQ and make the
                        // GUI follow path look alive while the TDMA decoder is starved.
                        phase2UseRecentTrafficWindow = false;
                        if (appliedQueuedVoiceReset) {
                            logP25VoiceScheduler("reset-applied",
                                QString("cleared rolling IQ and skipped this DSP tick for tg=%1 voice=%2MHz.")
                                    .arg(monP25VoiceTalkgroupId)
                                    .arg(monP25TrafficVoiceFreqHz / 1e6, 0, 'f', 5),
                                1000);
                            didWork = true;
                            continue;
                        }
                        if (skipP25VoiceWindow) {
                            if (appliedQueuedSlotProbe) {
                                const int slotForLog = static_cast<int>(appliedQueuedSlot);
                                QTimer::singleShot(0, this, [this, slotForLog]() {
                                    appendP25LogLineKeyed(QString("tdma-slot-probe-applied:%1").arg(slotForLog),
                                        QString("TDMA slot auto-probe applied by DSP worker: selected slot %1; decoder history reset and audio remains gated until MAC/ESS lock.")
                                            .arg(slotForLog),
                                        2500);
                                });
                            }
                            logP25VoiceScheduler("slot-probe-applied",
                                QString("slot=%1; cleared rolling IQ and skipped this DSP tick.")
                                    .arg(static_cast<int>(appliedQueuedSlot)),
                                1000);
                            phase2IqByRx.erase(p25ReceiverSessionKey(rx));
                            didWork = true;
                            continue;
                        }
                    } // release stateMutex before ring/IQ work — holding it across
                      // getNewIQWindow/takeUndecoded freezes GUI auto-follow (lock_guard).

                    if (!phase2BufferedDecode) {
                        phase2IqByRx.erase(p25ReceiverSessionKey(rx));
                    }
                    {
                        size_t tgt = (sr > 0)
                            ? static_cast<size_t>(sr * (phase2BufferedDecode ? kP25Phase2VoiceDecodeWindowSeconds : 0.025))
                            : 8192;
                        if (phase2BufferedDecode) {
                            // Phase 2 traffic must be decoded from a continuous chronological
                            // stream.  The previous implementation sampled the newest recent
                            // 512 ms snapshot and then reset the receiver cursor to live edge on
                            // every decode cadence.  Field logs showed the consequence: the
                            // voice receiver was on the granted frequency with decode=yes, but
                            // every TDMA DEEP DIAG line stayed at block=no-vcw-from-live-window
                            // because most of the active call was skipped between snapshots.
                            // Keep a rolling per-receiver IQ window instead, append only new
                            // samples, and decode the rolling buffer at cadence.  This is much
                            // closer to sdrtrunk's continuously-running traffic-channel source.
                            const double rollingWindowSeconds =
                                (phase2SessionSpeakerSustain ||
                                 phase2EstablishedClearStreaming ||
                                 phase2SessionHadVoiceLock)
                                    ? kP25Phase2VoiceDecodeActiveRollingSeconds
                                    : kP25Phase2VoiceDecodeWindowSeconds;
                            const size_t rollingWindow = (sr > 0.0)
                                ? p25Phase2VoiceRollingMaxSamples(sr, rollingWindowSeconds)
                                : tgt;
                            const ReceiverSessionKey sessionKey = p25ReceiverSessionKey(rx);
                            auto& rolling = phase2IqByRx[sessionKey];
                            size_t pullWindow = (sr > 0.0)
                                ? static_cast<size_t>(std::clamp(sr * kP25Phase2VoicePullWindowSeconds, 4096.0, static_cast<double>(rollingWindow)))
                                : std::min<size_t>(tgt, rollingWindow);
                            p25Phase2PrepareRollingIqPull(mgr, i, rx, rolling, rollingWindow, pullWindow, sr);
                            auto newWin = phase2UseRecentTrafficWindow
                                ? mgr.getRecentIQWindowWithCursor(i, rollingWindow)
                                : mgr.getNewIQWindowForReceiver(i, rx, pullWindow);
                            if (newWin.streamEpoch != 0 && rolling.streamEpochKnown &&
                                rolling.streamEpoch != newWin.streamEpoch) {
                                if (newWin.samples.empty()) {
                                    newWin.cursorDiscontinuity = true;
                                } else {
                                    const uint64_t oldStreamEpoch = rolling.streamEpoch;
                                    const uint64_t newStreamEpoch = newWin.streamEpoch;
                                    logP25VoiceScheduler("stream-retune-handoff",
                                        QString("epoch=%1->%2 new=%3; clearing rolling IQ and restarting traffic decoder.")
                                            .arg(static_cast<qulonglong>(oldStreamEpoch))
                                            .arg(static_cast<qulonglong>(newStreamEpoch))
                                            .arg(static_cast<qulonglong>(newWin.samples.size())),
                                        1000);
                                    rolling.clear();
                                    p25Phase2ClearSpeakerPendingQueue(rx,
                                        pendingAudioByRx[sessionKey],
                                        P25PendingClearReason::RetuneOrGeneration);
                                    lastPhase2DecodeByRx.erase(sessionKey);
                                    bool resetQueuedOrApplied = false;
                                    {
                                        std::unique_lock<std::mutex> resetLock(rx.stateMutex, std::try_to_lock);
                                        if (resetLock.owns_lock()) {
                                            resetQueuedOrApplied =
                                                tryResetP25TrafficSessionNonBlocking(rx, "iq-stream-retune-handoff", true);
                                        } else {
                                            logP25VoiceScheduler("stream-retune-reset-deferred",
                                                QString("epoch=%1->%2; rx state busy, scheduler will retry without blocking.")
                                                    .arg(static_cast<qulonglong>(oldStreamEpoch))
                                                    .arg(static_cast<qulonglong>(newStreamEpoch)),
                                                1000);
                                        }
                                    }
                                    if (!resetQueuedOrApplied) {
                                        phase2IqByRx.erase(sessionKey);
                                    }
                                    didWork = true;
                                    continue;
                                }
                            }
                            if (newWin.cursorDiscontinuity) {
                                logP25VoiceScheduler("cursor-discontinuity",
                                    QString("new=%1 rolling=%2; clearing traffic IQ history.")
                                        .arg(static_cast<qulonglong>(newWin.samples.size()))
                                        .arg(static_cast<qulonglong>(rolling.samples.size())),
                                    1000);
                                rolling.clear();
                                p25Phase2ClearSpeakerPendingQueue(rx,
                                    pendingAudioByRx[sessionKey],
                                    P25PendingClearReason::RetuneOrGeneration);
                                lastPhase2DecodeByRx.erase(sessionKey);
                                {
                                    std::unique_lock<std::mutex> resetLock(rx.stateMutex, std::try_to_lock);
                                    if (resetLock.owns_lock()) {
                                        (void)tryResetP25TrafficSessionNonBlocking(rx, "iq-cursor-discontinuity", true);
                                    }
                                }
                                didWork = true;
                                continue;
                            }
                            const bool appended = rolling.append(newWin, rollingWindow);
                            const auto now = std::chrono::steady_clock::now();
                            auto& last = lastPhase2DecodeByRx[p25ReceiverSessionKey(rx)];
                            if (rolling.samples.empty()) {
                                logP25VoiceScheduler(appended ? QStringLiteral("rolling-empty") : QStringLiteral("no-new-iq"),
                                    QString("new=%1 rolling=%2 sr=%3MHz cf=%4MHz target=%5MHz tg=%6.")
                                        .arg(static_cast<qulonglong>(newWin.samples.size()))
                                        .arg(static_cast<qulonglong>(rolling.samples.size()))
                                        .arg(sr / 1e6, 0, 'f', 3)
                                        .arg(cf / 1e6, 0, 'f', 5)
                                        .arg(demodFreq / 1e6, 0, 'f', 5)
                                        .arg(monP25VoiceTalkgroupId),
                                    1500);
                                didWork = true;
                                continue;
                            }
                            // After a one-RTL MHz hop, retuneValidFromAbsolute clamps the cursor
                            // to the live edge so pre-roll of pre-retune IQ is impossible.  Capture
                            // 20260808_115158 then followed clear TG 30003 but the first live worker
                            // only saw 120 ms and never found a Phase-2 burst; replay of the same RF
                            // succeeded once it had the full two-superframe traffic eye.
                            //
                            // RollingIqWindow::append() sets decodeAbsoluteKnown=true on the first
                            // fill with lastDecodeAbsolute=startAbsolute.  Requiring
                            // !decodeAbsoluteKnown therefore never armed the wait after the first
                            // append — capture 20260712_035432 shows every post-retune WORKER START
                            // at iq=32768 and zero waiting-post-retune-iq scheduler lines.
                            {
                                const bool coldAcquireNeverDecoded =
                                    !rolling.decodeAbsoluteKnown ||
                                    rolling.lastDecodeAbsolute <= rolling.startAbsolute;
                                const bool coldAcquireEye =
                                    monP25VoicePhase2 &&
                                    rx.p25IndependentTrafficSource &&
                                    coldAcquireNeverDecoded &&
                                    !phase2SessionHadBurstEye;
                                const size_t minColdAcquireIq = (sr > 0.0)
                                    ? std::min(rollingWindow, static_cast<size_t>(
                                          std::max(32768.0, sr * kP25Phase2VoiceDecodeFirstColdEyeSeconds)))
                                    : std::min<size_t>(rollingWindow, 1474560u);
                                if (coldAcquireEye && rolling.samples.size() < minColdAcquireIq) {
                                    logP25VoiceScheduler("waiting-post-retune-iq",
                                        QString("rolling=%1 need=%2 sr=%3MHz tg=%4 target=%5MHz.")
                                            .arg(static_cast<qulonglong>(rolling.samples.size()))
                                            .arg(static_cast<qulonglong>(minColdAcquireIq))
                                            .arg(sr / 1e6, 0, 'f', 3)
                                            .arg(monP25VoiceTalkgroupId)
                                            .arg(demodFreq / 1e6, 0, 'f', 5),
                                        500);
                                    didWork = true;
                                    continue;
                                }
                            }
                            if (!appended) {
                                logP25VoiceScheduler(QStringLiteral("no-new-iq"),
                                    QString("new=%1 rolling=%2 sr=%3MHz cf=%4MHz target=%5MHz tg=%6.")
                                        .arg(static_cast<qulonglong>(newWin.samples.size()))
                                        .arg(static_cast<qulonglong>(rolling.samples.size()))
                                        .arg(sr / 1e6, 0, 'f', 3)
                                        .arg(cf / 1e6, 0, 'f', 5)
                                        .arg(demodFreq / 1e6, 0, 'f', 5)
                                        .arg(monP25VoiceTalkgroupId),
                                    3000);
                            }
                            if (last.time_since_epoch().count() != 0 &&
                                now - last < std::chrono::milliseconds(p25Phase2AdaptiveVoiceDecodeCadenceMs(rx))) {
                                didWork = true;
                                continue;
                            }
                            phase2VoiceQueueSustainHint =
                                phase2SessionSpeakerSustain ||
                                phase2EstablishedClearStreaming ||
                                p25Phase2SpeakerSustainDecodeActive() ||
                                phase2SessionHadBurstEye;
                            if (!p25VoiceWorkerCanAcceptJobForDepth(phase2VoiceQueueSustainHint)) {
                                const auto workerState = p25VoiceWorkerQueueSnapshot();
                                logP25VoiceScheduler("worker-busy",
                                    QString("pending=%1 pendingJobs=%2 busy=%3 stopping=%4 thread=%5 qDrop=%6 rDrop=%7 publish=%8 completed=%9 rolling=%10 tg=%11 target=%12MHz.")
                                        .arg(workerState.pending ? "yes" : "no")
                                        .arg(static_cast<qulonglong>(workerState.pendingJobs))
                                        .arg(workerState.busy ? "yes" : "no")
                                        .arg(workerState.stopping ? "yes" : "no")
                                        .arg(workerState.threadRunning ? "yes" : "no")
                                        .arg(workerState.droppedJobs)
                                        .arg(workerState.droppedResults)
                                        .arg(static_cast<qulonglong>(workerState.pendingPublishResults))
                                        .arg(static_cast<qulonglong>(workerState.completedResults))
                                        .arg(static_cast<qulonglong>(rolling.samples.size()))
                                        .arg(monP25VoiceTalkgroupId)
                                        .arg(demodFreq / 1e6, 0, 'f', 5),
                                    1000);
                                didWork = true;
                                continue;
                            }
                            const bool wideReacquireWindow = phase2WideReacquireWindow;
                            const bool maskEpochRepairWindow = phase2MaskEpochRepairWindow;
                            const size_t undecodedBacklog = p25Phase2UndecodedBacklogSamples(rolling);
                            // Catch up as soon as lag exceeds one sustain hop so we
                            // never leave hundreds of ms of voice RF unprocessed
                            // (20260807_232020: 60 ms decode / 350 ms wall → chop).
                            const bool activeSpeakerClearPath = phase2VoiceQueueSustainHint;
                            // Streaming DDC path: contiguous fresh-only hops (no
                            // overlap re-feed). Once sticky CQPSK is live, short
                            // catch-up thresholds keep the worker on the RF edge.
                            const bool phase2StreamingDdc =
                                monP25VoicePhase2 &&
                                rx.p25VoiceLiveDecoder.config().enableStreamingChannelDdc;
                            const double backlogCatchUpSeconds = activeSpeakerClearPath
                                ? (phase2StreamingDdc ? 0.040 : 0.050)
                                : 0.120;
                            const size_t backlogCatchUpThreshold = (sr > 0.0)
                                ? static_cast<size_t>(std::clamp(sr * backlogCatchUpSeconds, 32768.0, 1048576.0))
                                : (activeSpeakerClearPath ? 102400u : 245760u);
                            const bool preAcquiredPhase2Traffic =
                                !wideReacquireWindow &&
                                !phase2SessionHadBurstEye &&
                                !phase2SessionSpeakerSustain &&
                                !phase2EstablishedClearStreaming &&
                                monP25VoiceDecode &&
                                monP25VoicePhase2 &&
                                monP25IndependentTrafficSource &&
                                (!phase2HardTargetAcquire || maskEpochRepairWindow);
                            const bool backlogCatchUp =
                                !preAcquiredPhase2Traffic &&
                                undecodedBacklog > backlogCatchUpThreshold;
                            const bool speakerSustainEligible =
                                activeSpeakerClearPath &&
                                ((phase2StableSuperframeLock && phase2SessionHadVoiceLock) ||
                                 phase2SessionSpeakerSustain ||
                                 phase2EstablishedClearStreaming);
                            const bool speakerSustainDecode =
                                !wideReacquireWindow &&
                                !maskEpochRepairWindow &&
                                speakerSustainEligible &&
                                (phase2SustainDecodeWindow ||
                                 phase2SessionSpeakerSustain ||
                                 phase2EstablishedClearStreaming ||
                                 p25Phase2SpeakerSustainDecodeActive());
                            // Never stay on cold/unacquired after the first eye/emit —
                            // that path used minFresh=120ms and blocky 80ms islands.
                            const bool unacquiredAcquireWindow =
                                preAcquiredPhase2Traffic &&
                                !speakerSustainDecode;
                            const bool decodeCursorAdvancedPastStart =
                                rolling.submittedDecodeEndKnown ||
                                (rolling.decodeAbsoluteKnown && rolling.absoluteKnown &&
                                 rolling.effectiveDecodeAbsolute() > rolling.startAbsolute);
                            const bool firstColdEyeChunk =
                                !phase2SustainDecodeWindow &&
                                !wideReacquireWindow &&
                                !maskEpochRepairWindow &&
                                !speakerSustainDecode &&
                                !phase2HardTargetAcquire &&
                                !phase2SessionHadBurstEye &&
                                !decodeCursorAdvancedPastStart &&
                                (!rolling.decodeAbsoluteKnown ||
                                 rolling.lastDecodeAbsolute <= rolling.startAbsolute);
                            const P25Phase2VoiceChunkPlan chunkPlan = p25Phase2PlanVoiceDecodeChunk(
                                phase2StreamingDdc,
                                backlogCatchUp,
                                activeSpeakerClearPath,
                                wideReacquireWindow,
                                maskEpochRepairWindow,
                                speakerSustainDecode,
                                phase2SustainDecodeWindow,
                                firstColdEyeChunk,
                                unacquiredAcquireWindow,
                                decodeCursorAdvancedPastStart);
                            const double maxDecodeChunkSeconds = chunkPlan.maxChunkSeconds;
                            const double decodeOverlapSeconds = chunkPlan.overlapSeconds;
                            const double minDecodeFreshSeconds = chunkPlan.minFreshSeconds;
                            const double minDecodeFreshFloor = chunkPlan.minFreshFloorSamples;
                            const size_t maxDecodeChunk = (sr > 0.0)
                                ? static_cast<size_t>(std::clamp(sr * maxDecodeChunkSeconds, 32768.0, static_cast<double>(rollingWindow)))
                                : std::min<size_t>(rolling.samples.size(), rollingWindow);
                            const size_t maxOverlapSamples = (rollingWindow > maxDecodeChunk)
                                ? (rollingWindow - maxDecodeChunk)
                                : 0;
                            const size_t decodeOverlap = (sr > 0.0)
                                ? static_cast<size_t>(std::clamp(sr * decodeOverlapSeconds, 0.0, static_cast<double>(maxOverlapSamples)))
                                : std::min<size_t>(maxOverlapSamples, rolling.samples.size());
                            const size_t minDecodeFreshNominal = (sr > 0.0)
                                ? static_cast<size_t>(std::clamp(sr * minDecodeFreshSeconds, 1.0, static_cast<double>(maxDecodeChunk)))
                                : 4096u;
                            const size_t minDecodeFresh = p25Phase2EffectiveMinFreshSamples(
                                rolling.samples.size(), decodeOverlap, minDecodeFreshNominal,
                                static_cast<size_t>(minDecodeFreshFloor), maxDecodeChunk);
                            // Phase 2 TDMA must reach the decoder in complete
                            // AMBE cadence units.  Letting live-edge pressure
                            // soften a 40 ms speaker slice down to 16-32 ms
                            // burns worker passes and leaves the output ring
                            // playing isolated words.
                            iq = rolling.takeUndecoded(maxDecodeChunk, decodeOverlap, iqStartAbsolute, iqStartAbsoluteKnown,
                                &phase2FreshIqSamples, &phase2ContextIqSamples, minDecodeFresh,
                                &iqDecodeEndAbsolute, &iqDecodeEndAbsoluteKnown, false);
                            if (iq.empty()) {
                                logP25VoiceScheduler("waiting-fresh-iq",
                                    QString("rolling=%1 maxChunk=%2 overlap=%3 minFresh=%4 effMinFresh=%5 absKnown=%6 tg=%7 target=%8MHz.")
                                        .arg(static_cast<qulonglong>(rolling.samples.size()))
                                        .arg(static_cast<qulonglong>(maxDecodeChunk))
                                        .arg(static_cast<qulonglong>(decodeOverlap))
                                        .arg(static_cast<qulonglong>(minDecodeFreshNominal))
                                        .arg(static_cast<qulonglong>(minDecodeFresh))
                                        .arg(iqStartAbsoluteKnown ? "yes" : "no")
                                        .arg(monP25VoiceTalkgroupId)
                                        .arg(demodFreq / 1e6, 0, 'f', 5),
                                    1500);
                                didWork = true;
                                continue;
                            }
                            if (chunkPlan.treatAsContextFreeFresh) {
                                phase2FreshIqSamples = iq.size();
                                phase2ContextIqSamples = 0;
                            }
                            last = now;
                        } else {
                            iq = mgr.getNewSamplesForReceiver(i, rx, tgt);  // updates the live rx's lastConsumedAbsolute
                        }
                    }

                    const size_t got = iq.size();
                    if (got == 0) {
                        if (monP25VoiceDecode && monP25VoicePhase2) {
                            logP25VoiceScheduler("empty-iq-after-pull",
                                QString("sr=%1MHz cf=%2MHz target=%3MHz tg=%4; receiver cursor returned no IQ.")
                                    .arg(sr / 1e6, 0, 'f', 3)
                                    .arg(cf / 1e6, 0, 'f', 5)
                                    .arg(demodFreq / 1e6, 0, 'f', 5)
                                    .arg(monP25VoiceTalkgroupId),
                                1500);
                        }
                        continue;
                    }

                    const double t = got / sr;
                    const double orate = (engineForAudio ? engineForAudio->getSampleRate() : 48000.0);
                    const size_t need = (size_t)std::round(t * orate);
                    if (monP25VoiceDecode && monP25VoicePhase2) {
                        P25VoiceDecodeJob job;
                        job.rx = rxPtr;
                        job.iq = std::move(iq);
                        job.audioOutputIndices = rxAudioOutputs;
                        job.sampleRateHz = sr;
                        job.centerFreqHz = cf;
                        job.targetFreqHz = demodFreq;
                        job.outputRateHz = orate;
                        job.iqStartAbsolute = iqStartAbsolute;
                        job.iqStartAbsoluteKnown = iqStartAbsoluteKnown;
                        job.iqDecodeEndAbsolute = iqDecodeEndAbsolute;
                        job.iqDecodeEndAbsoluteKnown = iqDecodeEndAbsoluteKnown;
                        job.outputMutedForSettle = p25VoiceOutputMutedForSettle;
                        job.rollingDecode = phase2BufferedDecode;
                        job.speakerSustainDecode = phase2VoiceQueueSustainHint;
                        job.freshIqSamples = phase2FreshIqSamples;
                        job.contextIqSamples = phase2ContextIqSamples;
                        job.trafficGeneration = monP25TrafficGeneration;
                        job.independentTrafficSource = monP25IndependentTrafficSource;
                        job.talkgroupId = monP25VoiceTalkgroupId;
                        job.sourceId = monP25VoiceSourceId;
                        job.tdmaSlotKnown = monP25VoiceTdmaSlotKnown;
                        job.tdmaSlot = monP25VoiceTdmaSlot;
                        job.voiceFreqHz = monP25TrafficVoiceFreqHz;
                        job.flushSeq = p25PendingAudioFlushSeq.load(std::memory_order_acquire);
                        if (rxPtr) {
                            job.receiverSessionKey = p25ReceiverSessionKey(*rxPtr);
                            job.callSessionId = rxPtr->p25CurrentCallSessionId;
                        }
                        if (!submitP25VoiceDecodeJob(std::move(job))) {
                            const auto workerState = p25VoiceWorkerQueueSnapshot();
                            const QString submitDetail = QString("pending=%1 pendingJobs=%2 busy=%3 stopping=%4 thread=%5 nextSeq=%6 qDrop=%7 rDrop=%8 publish=%9 completed=%10.")
                                .arg(workerState.pending ? "yes" : "no")
                                .arg(static_cast<qulonglong>(workerState.pendingJobs))
                                .arg(workerState.busy ? "yes" : "no")
                                .arg(workerState.stopping ? "yes" : "no")
                                .arg(workerState.threadRunning ? "yes" : "no")
                                .arg(static_cast<qulonglong>(workerState.nextSequence))
                                .arg(workerState.droppedJobs)
                                .arg(workerState.droppedResults)
                                .arg(static_cast<qulonglong>(workerState.pendingPublishResults))
                                .arg(static_cast<qulonglong>(workerState.completedResults));
                            logP25VoiceScheduler("submit-failed", submitDetail, 1000);
                            QTimer::singleShot(0, this, [this]() {
                                appendP25LogLineKeyed("p25-voice-worker-submit-failed",
                                    "P25 voice worker submit failed; Phase 2 IQ window was dropped because the worker is stopping or unavailable.",
                                    2500);
                            });
                        } else {
                            if (phase2BufferedDecode && iqDecodeEndAbsoluteKnown) {
                                phase2IqByRx[p25ReceiverSessionKey(rx)].markDecodeSubmitted(iqDecodeEndAbsolute);
                            }
                            logP25VoiceScheduler("submitted",
                                QString("iq=%1 fresh=%2 context=%3 absKnown=%4 sr=%5MHz cf=%6MHz target=%7MHz tg=%8 slot=%9 generation=%10.")
                                    .arg(static_cast<qulonglong>(got))
                                    .arg(static_cast<qulonglong>(phase2FreshIqSamples))
                                    .arg(static_cast<qulonglong>(phase2ContextIqSamples))
                                    .arg(iqStartAbsoluteKnown ? "yes" : "no")
                                    .arg(sr / 1e6, 0, 'f', 3)
                                    .arg(cf / 1e6, 0, 'f', 5)
                                    .arg(demodFreq / 1e6, 0, 'f', 5)
                                    .arg(monP25VoiceTalkgroupId)
                                    .arg(monP25VoiceTdmaSlotKnown ? QString::number(monP25VoiceTdmaSlot & 0x01u) : QStringLiteral("unknown"))
                                    .arg(static_cast<qulonglong>(monP25TrafficGeneration)),
                                    150);
                        }
                        gLastAfcOffsetHz.store(afcOffsetHz);
                        if (rfMetrics.valid) {
                            gLastRmsDb.store(rfMetrics.signalLevelDb);
                            gLastNoiseFloorDb.store(rfMetrics.noiseFloorDb);
                            gLastSnrDb.store(rfMetrics.snrDb);
                        }
                        didWork = true;
                        continue;
                    }
                    auto t0 = std::chrono::steady_clock::now();
                    bool haveP25Audio = false;
                    bool publishVoiceDiag = false;
                    bool effectiveSpeakerMayEmit = false;
                    P25VoiceAudioBlock p25Audio;
                    {
                        std::unique_lock<std::recursive_mutex> dspLock(rx.dspMutex, std::try_to_lock);
                        if (!dspLock.owns_lock()) {
                            didWork = true;
                            continue;
                        }
                        if (p25ShouldSuppressAnalogDemod(monP25VoiceDecode,
                                                         monP25ControlMute,
                                                         monP25IndependentTrafficSource,
                                                         monP25VoicePhase2) &&
                            !monP25VoiceDecode) {
                            rms = -120.0;
                            (void)need;
                        } else if (monP25VoiceDecode) {
                            p25Audio = decodeP25VoiceAudioBlock(rx, iq, sr, cf, demodFreq, orate,
                                iqStartAbsolute, iqStartAbsoluteKnown, phase2ContextIqSamples);
                            (void)phase2FreshIqSamples;
                            // Do not sample-trim Phase-2 PCM here.  Audio de-duplication is now
                            // done at AMBE codeword granularity using absolute dibit positions in
                            // decodeP25Phase2VoiceBlock().  The previous fresh-tail sample limit
                            // could collapse a valid multi-frame superframe decode to ~100 ms,
                            // producing the fast/jittery doubled-frame sound reported in field logs.
                            bool dropStaleTrafficAudio = false;
                            if (rx.p25IndependentTrafficSource) {
                                const uint64_t liveGen = p25TrafficSourceGeneration.load(std::memory_order_acquire);
                                std::unique_lock<std::mutex> genLock(rx.stateMutex, std::try_to_lock);
                                if (!genLock.owns_lock() || !rx.active ||
                                    rx.p25TrafficGeneration == 0 || rx.p25TrafficGeneration != liveGen) {
                                    dropStaleTrafficAudio = true;
                                }
                            }
                            if (dropStaleTrafficAudio) {
                                didWork = true;
                                continue;
                            }
                            publishVoiceDiag = true;
                            haveP25Audio = true;
                            const std::string rawSpeakerGateReason = p25VoiceBlockSpeakerGateReason(p25Audio);
                            const bool settleMuteBypassedForValidatedVoice =
                                p25VoiceOutputMutedForSettle &&
                                p25VoiceBlockMayBypassPostArmSettle(p25Audio, rawSpeakerGateReason);
                            const bool effectiveSettleMute =
                                p25VoiceOutputMutedForSettle && !settleMuteBypassedForValidatedVoice;
                            const std::string speakerGateReason = effectiveSettleMute
                                ? std::string("post-arm-settle-muted")
                                : rawSpeakerGateReason;
                            const bool speakerMayEmit =
                                speakerGateReason == "emit" &&
                                p25VoiceBlockHasSpeakerTimelineAudio(p25Audio);
                            p25Audio.phase2SpeakerGateReason = speakerGateReason;
                            const double curSnrForAudio = gLastSnrDb.load(std::memory_order_relaxed);
                            const bool hasCarrierForAudio = (curSnrForAudio > 2.0) ||
                                (gLastRmsDb.load(std::memory_order_relaxed) > gLastNoiseFloorDb.load(std::memory_order_relaxed) + 4.0);
                            const bool effectiveSpeakerMayEmitLocal = speakerMayEmit &&
                                (hasCarrierForAudio ||
                                 (p25Audio.phase2EmittedPcmFrames > 0 &&
                                  p25VoiceBlockHasSpeakerTimelineAudio(p25Audio)) ||
                                 (p25Audio.phase2FedToMbelib > 0 && p25Audio.phase2TargetVoiceCodewords > 0));
                            effectiveSpeakerMayEmit = effectiveSpeakerMayEmitLocal;
                            ch = effectiveSpeakerMayEmitLocal ? p25Audio.audio : std::vector<float>{};
                            audioOutputEngine = ch.empty()
                                ? engineForAudio.get()
                                : ensureAudioOutputActive("P25 voice");
                            if (audioOutputEngine) {
                                audioActiveOutputCount = audioOutputEngine->activeOutputCount();
                                audioQueuedSamples = audioOutputEngine->getRingQueuedSamples();
                                audioRingFillPercent = audioOutputEngine->getRingFillPercent();
                                audioUnderrunCount = audioOutputEngine->getUnderrunCount();
                            }
                            writeP25Phase2AudioOutputTrace(rx, p25Audio, "gui-dsp-worker",
                                p25VoiceOutputMutedForSettle, speakerMayEmit, audioOutputEngine != nullptr,
                                audioActiveOutputCount, audioQueuedSamples, audioRingFillPercent,
                                audioUnderrunCount, ch.size(), orate);
                            if (!ch.empty()) {
                                double sum = 0.0;
                                for (float sample : ch) sum += static_cast<double>(sample) * sample;
                                rms = 20.0 * std::log10(std::sqrt(sum / static_cast<double>(ch.size())) + 1e-12);
                            }
                            (void)need;
                        } else {
                            ch = rx.demod.demodulateToAudio(iq, sr, cf, demodFreq, monMode,
                                rms, monLpf, monSquelch, monGain, monWfmDe,
                                monWfmNotch, monBw, need, orate, rfSquelchLevel, monAudioLpfEnabled);
                        }
                    }
                    if (haveP25Audio) {
                        publishP25VoiceDiagnostics(rx, p25Audio, publishVoiceDiag);
                        if (monP25VoiceDecode && monP25VoicePhase2) {
                            const uintptr_t rxKey = reinterpret_cast<uintptr_t>(&rx);
                            const long long absStartLog = iqStartAbsoluteKnown ? static_cast<long long>(iqStartAbsolute) : -1LL;
                            const size_t gotLog = got;
                            const bool rollingLog = phase2BufferedDecode;
                            const double cfLog = cf;
                            const double targetLog = p25Audio.effectiveTargetFreqHz > 0.0
                                ? p25Audio.effectiveTargetFreqHz
                                : demodFreq;
                            const double srLog = sr;
                            const QString diagLog = p25VoiceDiagLabel(p25Audio.diag);
                            const QString backendLog = p25Audio.backendAvailable ? QStringLiteral("yes") : QStringLiteral("no");
                            const QString essLog = p25Audio.phase2EssKnown
                                ? (p25Audio.phase2EssEncrypted ? QStringLiteral("enc") : QStringLiteral("clear"))
                                : QStringLiteral("unknown");
                            const QString acchLog = QString("p2acch=nom:%1 altKind:%2 swap:%3 slip:%4 inv:%5")
                                .arg(static_cast<qulonglong>(p25Audio.phase2MacNominalCrcValid))
                                .arg(static_cast<qulonglong>(p25Audio.phase2MacAltKindCrcValid))
                                .arg(static_cast<qulonglong>(p25Audio.phase2MacBitSwapCrcValid))
                                .arg(static_cast<qulonglong>(p25Audio.phase2MacSlipCrcValid))
                                .arg(static_cast<qulonglong>(p25Audio.phase2MacInvertCrcValid));
                            const qulonglong syncsLog = static_cast<qulonglong>(p25Audio.syncs);
                            const qulonglong nidsLog = static_cast<qulonglong>(p25Audio.nids);
                            const qulonglong imbeLog = static_cast<qulonglong>(p25Audio.imbeFrames);
                            const qulonglong decodedLog = static_cast<qulonglong>(p25Audio.decodedFrames);
                            const qulonglong audioSamplesLog = static_cast<qulonglong>(p25Audio.audio.size());
                            const qulonglong burstsLog = static_cast<qulonglong>(p25Audio.phase2Bursts);
                            const qulonglong vcwLog = static_cast<qulonglong>(p25Audio.phase2VoiceCodewords);
                            const qulonglong targetVcwLog = static_cast<qulonglong>(p25Audio.phase2TargetVoiceCodewords);
                            const qulonglong oppVcwLog = static_cast<qulonglong>(p25Audio.phase2OppositeVoiceCodewords);
                            const qulonglong ambeAttemptsLog = static_cast<qulonglong>(p25Audio.phase2AmbeDecodeAttempts);
                            const qulonglong ambeAcceptedLog = static_cast<qulonglong>(p25Audio.phase2AmbeAcceptedFrames);
                            const qulonglong rejectedVcwLog = static_cast<qulonglong>(p25Audio.phase2RejectedVoiceCodewords);
                            const qulonglong wrongSlotVcwLog = static_cast<qulonglong>(p25Audio.phase2WrongSlotVoiceCodewords);
                            const qulonglong duplicateVcwLog = static_cast<qulonglong>(p25Audio.phase2DuplicateSuppressedVoiceCodewords);
                            const qulonglong absDuplicateVcwLog = static_cast<qulonglong>(p25Audio.phase2AbsoluteDuplicateSuppressedVoiceCodewords);
                            const qulonglong seqSuppressVcwLog = static_cast<qulonglong>(p25Audio.phase2SequencerSuppressedVoiceCodewords);
                            const qulonglong sfLog = static_cast<qulonglong>(p25Audio.phase2SuperframeBursts);
                            const qulonglong maskLog = static_cast<qulonglong>(p25Audio.phase2MaskedBursts);
                            const qulonglong macValidLog = static_cast<qulonglong>(p25Audio.phase2MacCrcValid);
                            const qulonglong macTotalLog = static_cast<qulonglong>(p25Audio.phase2MacPdus);
                            const qulonglong expVcwLog = static_cast<qulonglong>(p25Audio.phase2ExpectedVoiceCodewords);
                            const qulonglong fedLog = static_cast<qulonglong>(p25Audio.phase2FedToMbelib);
                            const qulonglong emitLog = static_cast<qulonglong>(p25Audio.phase2EmittedPcmFrames);
                            const qulonglong gapsLog = static_cast<qulonglong>(p25Audio.phase2FeedGaps);
                            const qulonglong contextVcwLog = static_cast<qulonglong>(p25Audio.phase2ContextVoiceCodewords);
                            const qulonglong contextDropLog = static_cast<qulonglong>(p25Audio.phase2ContextSuppressedVoiceCodewords);
                            const QString sourceLog = rx.p25VoiceSourceId != 0
                                ? p25HexId(rx.p25VoiceSourceId, 6)
                                : QStringLiteral("unknown");
                            const qulonglong callSessionLog = static_cast<qulonglong>(rx.p25CurrentCallSessionId);
                            const qlonglong grantEpochLog = static_cast<qlonglong>(rx.p25VoiceGrantEpochMs);
                            const qulonglong pttGenerationLog = static_cast<qulonglong>(rx.p25PttGeneration);
                            QTimer::singleShot(0, this, [this, rxKey, absStartLog, gotLog, rollingLog, cfLog, targetLog, srLog,
                                                          diagLog, backendLog, syncsLog, nidsLog, imbeLog, decodedLog,
                                                          audioSamplesLog, burstsLog, vcwLog, targetVcwLog, oppVcwLog,
                                                           ambeAttemptsLog, ambeAcceptedLog,
                                                           rejectedVcwLog, wrongSlotVcwLog, duplicateVcwLog,
                                                           absDuplicateVcwLog, seqSuppressVcwLog,
                                                           sfLog, maskLog,
                                                          macValidLog, macTotalLog, acchLog, essLog,
                                                          expVcwLog, fedLog, emitLog, gapsLog,
                                                          contextVcwLog, contextDropLog,
                                                          sourceLog, callSessionLog, grantEpochLog, pttGenerationLog]() {
                                const QString key = QString("p25-dsp-voice-loop:%1").arg(static_cast<qulonglong>(rxKey));
                                const QString line = QString("P25 DSP VOICE LOOP: rolling=%1 iq=%2 absStart=%3 sr=%4MHz cf=%5MHz target=%6MHz diag=%7 backend=%8 sync=%9 nid=%10 imbe=%11 decoded=%12 audio=%13 p2bursts=%14 p2vcw=%15 targetVcw=%16 oppVcw=%17 exp=%18 fed=%19 emit=%20 gaps=%21 ctxVcw=%22 ctxDrop=%23 reject=%24 wrongSlot=%25 dup=%26 absDup=%27 seqDrop=%28 p2sf=%29 p2mask=%30 p2mac=%31/%32 %33 ess=%34 src=%35 call=%36 grantEpoch=%37 pttGen=%38")
                                    .arg(rollingLog ? "yes" : "no")
                                    .arg(static_cast<qulonglong>(gotLog))
                                    .arg(absStartLog)
                                    .arg(srLog / 1e6, 0, 'f', 3)
                                    .arg(cfLog / 1e6, 0, 'f', 5)
                                    .arg(targetLog / 1e6, 0, 'f', 5)
                                    .arg(diagLog)
                                    .arg(backendLog)
                                    .arg(syncsLog)
                                    .arg(nidsLog)
                                    .arg(imbeLog)
                                    .arg(decodedLog)
                                    .arg(audioSamplesLog)
                                    .arg(burstsLog)
                                    .arg(vcwLog)
                                    .arg(targetVcwLog)
                                    .arg(oppVcwLog)
                                    .arg(expVcwLog)
                                    .arg(fedLog)
                                    .arg(emitLog)
                                    .arg(gapsLog)
                                    .arg(contextVcwLog)
                                    .arg(contextDropLog)
                                    .arg(rejectedVcwLog)
                                    .arg(wrongSlotVcwLog)
                                    .arg(duplicateVcwLog)
                                    .arg(absDuplicateVcwLog)
                                    .arg(seqSuppressVcwLog)
                                    .arg(sfLog)
                                    .arg(maskLog)
                                    .arg(macValidLog)
                                    .arg(macTotalLog)
                                    .arg(acchLog)
                                    .arg(essLog)
                                    .arg(sourceLog)
                                    .arg(callSessionLog)
                                    .arg(grantEpochLog)
                                    .arg(pttGenerationLog);
                                appendP25LogLineKeyed(key, line, 750);
                            });
                        }
                    }
                    auto t1 = std::chrono::steady_clock::now();
                    dspMicros = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();

                    gLastDspMicros.store(dspMicros);
                    gLastAfcOffsetHz.store(afcOffsetHz);
                    if (rfMetrics.valid) {
                        gLastRmsDb.store(rfMetrics.signalLevelDb);
                        gLastNoiseFloorDb.store(rfMetrics.noiseFloorDb);
                        gLastSnrDb.store(rfMetrics.snrDb);
                    } else {
                        gLastRmsDb.store(rms);
                    }
                    if (!ch.empty()) {
                        if (!audioOutputEngine) audioOutputEngine = ensureAudioOutputActive("decoded audio");
                        if (audioOutputEngine && audioOutputEngine->activeOutputCount() > 0) {
                            if (haveP25Audio && monP25VoiceDecode) {
                                // Suppress pure tail/conceal when no fresh target voice in block (prevents post-signal bursts)
                                // But for established Phase-2 clear calls, do not clear pending here: windows can legitimately
                                // contribute 0 new in a given slice while prior audio is still playing. Clearing causes blocky/not-joined.
                                size_t pushedSamples = 0;
                                const qint64 pendingNowMs = QDateTime::currentMSecsSinceEpoch();
                                const bool gateEmit =
                                    effectiveSpeakerMayEmit &&
                                    p25Audio.phase2SpeakerGateReason == "emit" &&
                                    p25VoiceBlockMayEmitAudio(p25Audio) &&
                                    !p25Audio.phase2StaleAudioTail;
                                auto& pendingSpeaker = p25SpeakerPendingFor(pendingAudioByRx, rx);
                                p25Phase2BindSpeakerPendingToCall(pendingSpeaker, rx);
                                const bool hasNewPcm =
                                    p25VoiceBlockHasSpeakerTimelineAudio(p25Audio) &&
                                    p25Audio.phase2EmittedPcmFrames > 0;
                                std::vector<float> pushedRealAudio;
                                std::vector<float> speakerAudioForQueue;
                                const std::vector<float>* speakerAudioToQueue = &p25Audio.audio;
                                if (monP25VoicePhase2 && hasNewPcm) {
                                    const double outRate = std::max(8000.0,
                                        static_cast<double>(audioOutputEngine->getSampleRate()));
                                    const size_t phase2FrameSamples = std::max<size_t>(160,
                                        static_cast<size_t>(outRate * 0.020 + 0.5));
                                    speakerAudioForQueue = p25Phase2SpeakerAudioForQueue(
                                        pendingSpeaker, p25Audio, p25Audio.audio, phase2FrameSamples);
                                    speakerAudioToQueue = &speakerAudioForQueue;
                                }
                                const bool hasPlayableNewPcm = !speakerAudioToQueue->empty();
                                if (gateEmit && (hasPlayableNewPcm || !pendingSpeaker.samples.empty())) {
                                    pushedSamples = pushP25SpeakerAudio(audioOutputEngine,
                                        pendingSpeaker.samples,
                                        *speakerAudioToQueue,
                                        rxAudioOutputs,
                                        audioOutputEngine->getRingFillPercent(),
                                        !hasPlayableNewPcm,
                                        &pushedRealAudio);
                                }
                                // Never clear accepted speaker PCM on transient gate failure.
                                // Playback queue is cleared only on explicit call boundaries.
                                if (pushedSamples > 0) {
                                    p25Phase2ResetPlayoutBridge(rx);
                                    const P25P2CallAudioKey speakerKey =
                                        p25CurrentPhase2AudioKey(rx, p25Audio.effectiveTargetFreqHz);
                                    const bool bridgeAnchor =
                                        p25Phase2CleanPlayoutBridgeAnchorWindow(p25Audio);
                                    p25Phase2RememberLastEmittedSample(
                                        rx, speakerKey, pushedRealAudio.empty() ? ch : pushedRealAudio,
                                        bridgeAnchor);
                                // Tap clear speaker PCM for Decode Log STT (async; never blocks DSP).
                                // Encrypted / gated-silent paths never reach here.
                                {
                                    const int sttRate = static_cast<int>(
                                        std::lround(std::max(8000.0,
                                            static_cast<double>(audioOutputEngine->getSampleRate()))));
                                    const double sttFreq = p25TranscriptVoiceLabelHz(
                                        rx, p25Audio.effectiveTargetFreqHz, demodFreq);
                                    const int sttSlot = rx.p25VoiceTdmaSlotKnown
                                        ? static_cast<int>(rx.p25VoiceTdmaSlot & 0x01u)
                                        : -1;
                                    const std::vector<float>& transcriptAudio =
                                        pushedRealAudio.empty() ? ch : pushedRealAudio;
                                    p25TranscriptTapSpeakerPcm(transcriptAudio.data(), transcriptAudio.size(),
                                        sttRate, p25Audio.talkgroupId, sttFreq, sttSlot,
                                        p25Audio.effectiveTargetFreqHz);
                                }
                                const size_t activeCount = audioOutputEngine->activeOutputCount();
                                const size_t queuedAfter = audioOutputEngine->getRingQueuedSamples();
                                const double fillAfter = audioOutputEngine->getRingFillPercent();
                                const int underrunsAfter = audioOutputEngine->getUnderrunCount();
                                if (haveP25Audio && monP25VoiceDecode) {
                                    guiP25AudioOutputEvents.fetch_add(1, std::memory_order_relaxed);
                                    guiP25AudioOutputSamples.fetch_add(static_cast<long long>(pushedSamples), std::memory_order_relaxed);
                                    guiP25AudioDecodedFrames.fetch_add(static_cast<long long>(p25Audio.decodedFrames), std::memory_order_relaxed);
                                    guiP25AudioAcceptedAmbeFrames.fetch_add(static_cast<long long>(p25Audio.phase2AmbeAcceptedFrames), std::memory_order_relaxed);
                                    const qint64 speakerNowMs = QDateTime::currentMSecsSinceEpoch();
                                    guiP25AudioLastOutputMs.store(speakerNowMs, std::memory_order_relaxed);
                                    // Always refresh speaker/sustain on a real ring push (see
                                    // p25Phase2ShouldPreserveLivePlaybackBuffers / 20260720_080118).
                                    gP25AudioLastSpeakerOutputMs.store(speakerNowMs, std::memory_order_relaxed);
                                    p25Phase2UpdateSessionSustainState(rx, p25Audio, speakerNowMs, true);
                                    if (p25Phase2SpeakerOutputCanRefreshFollowActivity(p25Audio)) {
                                        p25AutoFollowLastActiveMs = std::max(p25AutoFollowLastActiveMs, speakerNowMs);
                                    }
                                    const quint32 tgLog = p25Audio.talkgroupId;
                                    const double targetFreqLog = p25Audio.effectiveTargetFreqHz > 0.0
                                        ? p25Audio.effectiveTargetFreqHz
                                        : demodFreq;
                                    const QString slotLog = rx.p25VoiceTdmaSlotKnown
                                        ? QString::number(rx.p25VoiceTdmaSlot & 0x01u)
                                        : QStringLiteral("unknown");
                                    const qulonglong generationLog =
                                        static_cast<qulonglong>(rx.p25TrafficGeneration);
                                    const QString gateLog = QString::fromStdString(p25Audio.phase2SpeakerGateReason);
                                    const QString essLog = p25Audio.phase2EssKnown
                                        ? (p25Audio.phase2EssEncrypted ? QStringLiteral("enc") : QStringLiteral("clear"))
                                        : QStringLiteral("unknown");
                                    const QString actionLog = QString::fromStdString(p25Audio.phase2SecurityGateAction);
                                    const QString targetEssLog = p25Audio.phase2TargetEssKnown
                                        ? (p25Audio.phase2TargetEssEncrypted ? QStringLiteral("enc") : QStringLiteral("clear"))
                                        : QStringLiteral("unknown");
                                    const QString targetSessionLog = p25Audio.phase2TargetSessionAudioRelease
                                        ? QStringLiteral("yes")
                                        : QStringLiteral("no");
                                    const QString targetPttLog = p25Audio.phase2TargetSecurityStateFromPtt
                                        ? QStringLiteral("yes")
                                        : QStringLiteral("no");
                                    const QString sourceLog = rx.p25VoiceSourceId != 0
                                        ? p25HexId(rx.p25VoiceSourceId, 6)
                                        : QStringLiteral("unknown");
                                    const qulonglong callSessionLog = static_cast<qulonglong>(rx.p25CurrentCallSessionId);
                                    const qlonglong grantEpochLog = static_cast<qlonglong>(rx.p25VoiceGrantEpochMs);
                                    const qulonglong pttGenerationLog = static_cast<qulonglong>(rx.p25PttGeneration);
                                    const qulonglong decodedLog = static_cast<qulonglong>(p25Audio.decodedFrames);
                                    const qulonglong targetVcwLog = static_cast<qulonglong>(p25Audio.phase2TargetVoiceCodewords);
                                    const qulonglong oppVcwLog = static_cast<qulonglong>(p25Audio.phase2OppositeVoiceCodewords);
                                    const qulonglong contextVcwLog = static_cast<qulonglong>(p25Audio.phase2ContextVoiceCodewords);
                                    const qulonglong contextDropLog = static_cast<qulonglong>(p25Audio.phase2ContextSuppressedVoiceCodewords);
                                    const qulonglong macValidLog = static_cast<qulonglong>(p25Audio.phase2MacCrcValid);
                                    const qulonglong macTotalLog = static_cast<qulonglong>(p25Audio.phase2MacPdus);
                                    const qulonglong probeAcceptedLog = static_cast<qulonglong>(p25Audio.phase2DiagnosticAmbeProbeAccepted);
                                    const qulonglong probeAttemptsLog = static_cast<qulonglong>(p25Audio.phase2DiagnosticAmbeProbeAttempts);
                                    QTimer::singleShot(0, this, [this, tgLog, targetFreqLog, slotLog, generationLog,
                                                                 pushedSamples, activeCount, queuedAfter, fillAfter, underrunsAfter,
                                                                 gateLog, essLog, actionLog, targetEssLog, targetSessionLog, targetPttLog,
                                                                 sourceLog, callSessionLog, grantEpochLog, pttGenerationLog,
                                                                 decodedLog, targetVcwLog, oppVcwLog,
                                                                 contextVcwLog, contextDropLog,
                                                                 macValidLog, macTotalLog, probeAcceptedLog, probeAttemptsLog]() {
                                        appendP25LogLineKeyed(QString("p25-audio-output:%1").arg(tgLog),
                                            QString("P25 audio output: TG=%1 target=%2MHz slot=%3 gen=%4 pushed=%5 samples gate=%6 decoded=%7 targetVcw=%8 oppVcw=%9 ctxVcw=%10 ctxDrop=%11 p2mac=%12/%13 probe=%14/%15 ess=%16 targetEss=%17 targetSession=%18 targetPtt=%19 action=%20 activeOutputs=%21 ringQueued=%22 ringFill=%23% underruns=%24 src=%25 call=%26 grantEpoch=%27 pttGen=%28.")
                                                .arg(tgLog)
                                                .arg(targetFreqLog / 1e6, 0, 'f', 5)
                                                .arg(slotLog)
                                                .arg(generationLog)
                                                .arg(static_cast<qulonglong>(pushedSamples))
                                                .arg(gateLog)
                                                .arg(decodedLog)
                                                .arg(targetVcwLog)
                                                .arg(oppVcwLog)
                                                .arg(contextVcwLog)
                                                .arg(contextDropLog)
                                                .arg(macValidLog)
                                                .arg(macTotalLog)
                                                .arg(probeAcceptedLog)
                                                .arg(probeAttemptsLog)
                                                .arg(essLog)
                                                .arg(targetEssLog)
                                                .arg(targetSessionLog)
                                                .arg(targetPttLog)
                                                .arg(actionLog)
                                                .arg(static_cast<qulonglong>(activeCount))
                                                .arg(static_cast<qulonglong>(queuedAfter))
                                                .arg(fillAfter, 0, 'f', 2)
                                                .arg(underrunsAfter)
                                                .arg(sourceLog)
                                                .arg(callSessionLog)
                                                .arg(grantEpochLog)
                                                .arg(pttGenerationLog),
                                            150);
                                    });
                                }
                                }
                            } else if (!monP25VoiceDecode) {
                                // Analog demod (WFM/NFM/AM/etc): use the simple frame pusher, not the
                                // P25 jitter-buffer path.  GUI regression: demod ran but audio never
                                // reached the speakers because only the P25 branch pushed PCM.
                                pushAudioFrames(audioOutputEngine,
                                    p25SpeakerPendingFor(pendingAudioByRx, rx).samples,
                                    ch,
                                    rxAudioOutputs);
                            }
                        } else if (haveP25Audio && monP25VoiceDecode) {
                            const quint32 tgLog = p25Audio.talkgroupId;
                            const size_t samplesLog = ch.size();
                            QTimer::singleShot(0, this, [this, tgLog, samplesLog]() {
                                appendP25LogLineKeyed(QString("p25-audio-output-missing:%1").arg(tgLog),
                                    QString("P25 audio output blocked: TG=%1 decoded=%2 samples but no active playback output.")
                                        .arg(tgLog)
                                        .arg(static_cast<qulonglong>(samplesLog)),
                                    1000);
                            });
                        }
                    }
                    didWork = true;
                }
                for (int drainPass = 0; drainPass < 6; ++drainPass) {
                    if (!drainP25VoiceResults()) break;
                    didWork = true;
                }
                // Idle top-up must never open miniaudio / create AudioEngine.  The DSP
                // worker starts mid-MainWindow construction; calling ensureAudioOutputActive
                // here raced show()/exec() and produced intermittent open crashes
                // (heap corruption / AV) plus statusBar invokeMethod on a half-built UI.
                if (AudioEngine* speakerEngine = peekAudioEngineIfReady()) {
                    size_t realTopUpPushed = 0;
                    size_t bridgeTopUpPushed = 0;
                    const size_t topUpPushed = p25TopUpSpeakerPlaybackRing(
                        speakerEngine,
                        pendingAudioByRx,
                        receiverSessionStillActive,
                        &realTopUpPushed,
                        &bridgeTopUpPushed);
                    if (realTopUpPushed > 0) {
                        const qint64 topUpNowMs = QDateTime::currentMSecsSinceEpoch();
                        guiP25AudioLastOutputMs.store(topUpNowMs, std::memory_order_relaxed);
                        guiP25AudioOutputEvents.fetch_add(1, std::memory_order_relaxed);
                        guiP25AudioOutputSamples.fetch_add(static_cast<long long>(realTopUpPushed), std::memory_order_relaxed);
                        p25AutoFollowLastActiveMs = std::max(p25AutoFollowLastActiveMs, topUpNowMs);
                    }
                    if (topUpPushed > 0) {
                        const size_t queuedAfter = speakerEngine->getRingQueuedSamples();
                        const double fillAfter = speakerEngine->getRingFillPercent();
                        const int underrunsAfter = speakerEngine->getUnderrunCount();
                        QTimer::singleShot(0, this, [this, realTopUpPushed, bridgeTopUpPushed,
                                                      queuedAfter, fillAfter, underrunsAfter]() {
                            appendP25LogLineKeyed(QStringLiteral("p25-audio-top-up:idle"),
                                QString("P25 audio top-up: real=%1 bridge=%2 ringQueued=%3 ringFill=%4% underruns=%5.")
                                    .arg(static_cast<qulonglong>(realTopUpPushed))
                                    .arg(static_cast<qulonglong>(bridgeTopUpPushed))
                                    .arg(static_cast<qulonglong>(queuedAfter))
                                    .arg(fillAfter, 0, 'f', 2)
                                    .arg(underrunsAfter),
                                150);
                        });
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(
                    didWork ? (p25Phase2SpeakerSustainDecodeActive() ? 2 : 3) : 12));
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
        QTimer::singleShot(5500, this, [this]() {
            checkRemoteIssueFixStatus();
        });
        QTimer::singleShot(1200, this, [this]() {
            maybeShowAlphaDiagnosticsDisclosure();
        });
        QTimer::singleShot(2500, this, [this]() {
            submitDiagnosticsStartupSnapshot();
            startDiagnosticsHealthMonitors();
        });

        // Allow deferred audio open only after show()+event-loop entry.  The DSP
        // worker is already running; keeping guiStartupSettled false through ctor
        // prevents miniaudio/WASAPI init racing MainWindow construction.
        // Once settled, prewarm the default playback device so the first clear
        // P25 voice window does not race create/open on the DSP/voice worker.
        QTimer::singleShot(0, this, [this]() {
            guiStartupSettled.store(true, std::memory_order_release);
            writeEarlyCrashLog("gui-startup-settled");
            if (AudioEngine* eng = ensureAudioOutputActive("GUI settle prewarm")) {
                writeEarlyCrashLog(eng->activeOutputCount() > 0
                    ? "gui-audio-prewarm-ok"
                    : "gui-audio-prewarm-no-output");
            } else {
                writeEarlyCrashLog("gui-audio-prewarm-failed");
            }
        });

        // Apply scriptable GUI startup options after Qt has entered the event loop, so
        // device/audio startup and self-test timers run on the same path as normal UI use.
        QTimer::singleShot(250, this, [this]() {
            applyGuiRuntimeStartupConfig();
        });
    }

private slots:
    void checkRemoteIssueFixStatus()
    {
        if (!remoteDiagnosticsEnabled()) return;
        remoteDiagnosticsCheckClientStatus(this, [this](const QJsonObject& status) {
            if (!status.value("ok").toBool(false) ||
                !status.value("bugFixUpdateAvailable").toBool(false)) {
                return;
            }

            const QJsonArray fixedIssues = status.value("fixedIssues").toArray();
            if (fixedIssues.isEmpty()) return;
            const QString recommendedVersion = status.value("recommendedVersion").toString().trimmed();
            if (recommendedVersion.isEmpty()) return;

            QStringList issueIds;
            QStringList titles;
            for (const QJsonValue& v : fixedIssues) {
                const QJsonObject issue = v.toObject();
                const QString id = issue.value("issue_id").toString().trimmed();
                if (!id.isEmpty()) issueIds << id;
                const QString title = issue.value("title").toString().trimmed();
                if (!title.isEmpty() && titles.size() < 3) titles << title.left(120);
            }
            const QString noticeKey = recommendedVersion + ":" + issueIds.join(",");
            QSettings settings;
            if (settings.value("remoteDiagnostics/lastFixNoticeKey").toString() == noticeKey) {
                return;
            }
            settings.setValue("remoteDiagnostics/lastFixNoticeKey", noticeKey);

            QJsonObject payload;
            payload["recommendedVersion"] = recommendedVersion;
            payload["fixedIssueCount"] = fixedIssues.size();
            payload["issueIds"] = issueIds.join(",");
            remoteDiagnosticsSubmit("diagnostics.fix_notice", "info", payload);

            QString detail;
            if (!titles.isEmpty()) {
                detail = "\n\nFixed report(s):\n- " + titles.join("\n- ");
            }
            QMessageBox box(this);
            box.setWindowTitle("Update Addresses Your Report");
            box.setText(QString("SDR Town %1 includes fixes for issue reports from this installation.%2\n\nInstall the update to pick up those fixes.")
                            .arg(recommendedVersion, detail));
            QPushButton* checkNow = box.addButton("Check for Updates", QMessageBox::AcceptRole);
            box.addButton("Later", QMessageBox::RejectRole);
            box.exec();
            if (box.clickedButton() == checkNow && m_updateManager) {
                m_updateManager->checkForUpdates(true);
            }
        });
    }

    QJsonObject diagnosticsRuntimeSnapshot(const QString& reason)
    {
        QJsonObject payload;
        payload["reason"] = reason.left(80);
        payload["clientId"] = remoteDiagnosticsClientId();
        payload["timeUtc"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        payload["system"] = diagnosticsSystemHealthPayload();
        payload["devices"] = diagnosticsDeviceInventory(true);
        payload["signalRmsDb"] = gLastRmsDb.load(std::memory_order_relaxed);
        payload["noiseFloorDb"] = gLastNoiseFloorDb.load(std::memory_order_relaxed);
        payload["snrDb"] = gLastSnrDb.load(std::memory_order_relaxed);
        payload["afcOffsetHz"] = gLastAfcOffsetHz.load(std::memory_order_relaxed);
        payload["afcPpmDelta"] = gLastAfcPpmDelta.load(std::memory_order_relaxed);

        double monitorHz = 0.0;
        double monitorBw = 0.0;
        double lpfHz = 0.0;
        double squelch = 0.0;
        double rfGain = 0.0;
        bool lpfEnabled = false;
        DemodMode mode = DemodMode::AUTO;
        {
            std::lock_guard<std::mutex> lk(monitorParamsMutex);
            monitorHz = currentMonitorFreq;
            monitorBw = monitorChannelBwHz;
            lpfHz = monitorLpfHz;
            squelch = monitorSquelchDb;
            rfGain = monitorRfGainDb;
            lpfEnabled = monitorAudioLpfEnabled;
            mode = currentMonitorMode;
        }
        QJsonObject monitor;
        monitor["frequencyHz"] = monitorHz;
        monitor["mode"] = modeToQString(mode);
        monitor["channelBwHz"] = monitorBw;
        monitor["audioLpfHz"] = lpfHz;
        monitor["audioLpfEnabled"] = lpfEnabled;
        monitor["squelchDb"] = squelch;
        monitor["rfGainDb"] = rfGain;
        payload["monitor"] = monitor;

        QJsonObject p25;
        p25["controlHz"] = p25MonitoredControlFreqHz;
        p25["autoFollowEnabled"] = p25AutoFollowEnabled;
        p25["independentTrafficEnabled"] = p25IndependentTrafficEnabled;
        p25["independentTrafficActive"] = p25IndependentTrafficActive;
        p25["followEnabled"] = p25FollowEnabled;
        p25["followAutoActive"] = p25FollowAutoActive;
        p25["followTalkgroupId"] = static_cast<int>(p25FollowTalkgroupId);
        p25["voiceHz"] = p25AutoFollowVoiceFreqHz;
        p25["returnControlHz"] = p25AutoFollowReturnControlFreqHz;
        p25["lastGrantAgeMs"] = p25AutoFollowLastGrantMs > 0
            ? QDateTime::currentMSecsSinceEpoch() - p25AutoFollowLastGrantMs
            : -1;
        p25["lastAudioAgeMs"] = guiP25AudioLastOutputMs.load(std::memory_order_relaxed) > 0
            ? QDateTime::currentMSecsSinceEpoch() - guiP25AudioLastOutputMs.load(std::memory_order_relaxed)
            : -1;
        p25["audioOutputEvents"] = QString::number(guiP25AudioOutputEvents.load(std::memory_order_relaxed));
        p25["audioOutputSamples"] = QString::number(guiP25AudioOutputSamples.load(std::memory_order_relaxed));
        p25["voiceDroppedJobs"] = QString::number(p25VoiceDroppedJobs.load(std::memory_order_relaxed));
        p25["voiceDroppedResults"] = QString::number(p25VoiceDroppedResults.load(std::memory_order_relaxed));
        p25["controlDroppedResults"] = QString::number(p25ControlDroppedResults.load(std::memory_order_relaxed));
        payload["p25"] = p25;

        QJsonObject audio;
        AudioEngine* eng = peekAudioEngineIfReady();
        audio["engineCreated"] = engineForAudio != nullptr;
        audio["activeOutputCount"] = eng ? static_cast<int>(eng->activeOutputCount()) : 0;
        audio["activeOutputNames"] = eng ? QString::fromStdString(eng->getActiveDeviceNames()).left(240) : QString();
        payload["audio"] = audio;
        return payload;
    }

    void maybeShowAlphaDiagnosticsDisclosure()
    {
        if (!remoteDiagnosticsEnabled()) return;
        QSettings settings;
        const QString key = QStringLiteral("alpha-auto-diagnostics-v1");
        if (settings.value("remoteDiagnostics/alphaDisclosureKey").toString() == key) return;

        QMessageBox box(this);
        box.setIcon(QMessageBox::Warning);
        box.setWindowTitle("Alpha Diagnostics Notice");
        box.setText(
            "This alpha tester build sends compact diagnostics automatically.\n\n"
            "Reports may include crash markers, exception summaries, device/open state, "
            "performance stalls, hardware class, hashed install/hardware identifiers, "
            "and any issue reports or capped file snippets you manually attach.\n\n"
            "Full IQ/audio files are not uploaded automatically. Manual attachments are capped.\n\n"
            "If you do not agree during alpha testing, block SDR Town from internet access "
            "or discontinue use until a later build has full diagnostics controls.");
        QPushButton* continueBtn = box.addButton("Continue Alpha Testing", QMessageBox::AcceptRole);
        QPushButton* exitBtn = box.addButton("Exit SDR Town", QMessageBox::RejectRole);
        box.setDefaultButton(continueBtn);
        box.exec();
        if (box.clickedButton() == exitBtn) {
            qApp->quit();
            return;
        }
        settings.setValue("remoteDiagnostics/alphaDisclosureKey", key);
        QJsonObject payload;
        payload["stage"] = "alpha-disclosure";
        payload["acceptedNoticeKey"] = key;
        remoteDiagnosticsSubmit("diagnostics.disclosure", "info", payload);
    }

    void submitDiagnosticsStartupSnapshot()
    {
        if (!remoteDiagnosticsEnabled()) return;
        QJsonObject payload = diagnosticsRuntimeSnapshot("startup");
        remoteDiagnosticsSubmit("diagnostics.snapshot", "info", payload);

        const QJsonArray devices = payload.value("devices").toArray();
        bool anyOpening = false;
        for (const QJsonValue& value : devices) {
            const QString state = value.toObject().value("runtimeState").toString();
            if (state.contains("opening", Qt::CaseInsensitive)) {
                anyOpening = true;
                break;
            }
        }
        if (anyOpening) {
            payload["stage"] = "startup-hardware-opening";
            remoteDiagnosticsSubmit("hardware.open", "warn", payload);
        }
    }

    void startDiagnosticsHealthMonitors()
    {
        if (!remoteDiagnosticsEnabled()) return;
        if (!diagnosticsHeartbeatTimer) {
            diagnosticsLastHeartbeatMs = QDateTime::currentMSecsSinceEpoch();
            diagnosticsHeartbeatTimer = new QTimer(this);
            diagnosticsHeartbeatTimer->setInterval(1000);
            connect(diagnosticsHeartbeatTimer, &QTimer::timeout, this, [this]() {
                const qint64 now = QDateTime::currentMSecsSinceEpoch();
                const qint64 drift = diagnosticsLastHeartbeatMs > 0 ? now - diagnosticsLastHeartbeatMs : 1000;
                diagnosticsLastHeartbeatMs = now;
                if (drift < 8000) return;
                if (now - diagnosticsLastUiStallReportMs < 60000) return;
                diagnosticsLastUiStallReportMs = now;
                QJsonObject payload = diagnosticsRuntimeSnapshot("ui-stall");
                payload["stage"] = "gui-heartbeat";
                payload["stallMs"] = static_cast<int>(std::min<qint64>(drift, 600000));
                remoteDiagnosticsSubmit("app.performance.ui_stall", "warn", payload);
            });
            diagnosticsHeartbeatTimer->start();
        }
        if (!diagnosticsResourceTimer) {
            diagnosticsResourceTimer = new QTimer(this);
            diagnosticsResourceTimer->setInterval(60000);
            connect(diagnosticsResourceTimer, &QTimer::timeout, this, [this]() {
                const qint64 now = QDateTime::currentMSecsSinceEpoch();
                if (now - diagnosticsLastResourceReportMs < 300000) return;
                QJsonObject system = diagnosticsSystemHealthPayload();
                const QJsonArray pressureReasons = diagnosticsSystemHealthPressureReasons(system);
                if (pressureReasons.isEmpty()) return;
                const QString pressureSummary = diagnosticsSystemHealthPressureSummary(pressureReasons);
                system["pressureReasons"] = pressureReasons;
                system["pressureSummary"] = pressureSummary;
                diagnosticsLastResourceReportMs = now;
                QJsonObject payload = diagnosticsRuntimeSnapshot("resource-pressure");
                payload["stage"] = "resource-monitor";
                payload["pressureReasons"] = pressureReasons;
                payload["pressureSummary"] = pressureSummary;
                payload["system"] = system;
                remoteDiagnosticsSubmit("app.performance.resource_pressure", "warn", payload);
            });
            diagnosticsResourceTimer->start();
        }
    }

    void showDiagnosticsReportDialog()
    {
        if (!remoteDiagnosticsEnabled()) {
            QMessageBox::warning(this, "Remote Diagnostics",
                "Remote diagnostics are not configured, so reports cannot be uploaded from this build.");
            return;
        }

        QDialog dlg(this);
        dlg.setWindowTitle("Report Issue");
        dlg.resize(720, 620);
        QVBoxLayout* lay = new QVBoxLayout(&dlg);
        lay->setContentsMargins(10, 10, 10, 10);
        lay->setSpacing(8);

        QLabel* idLabel = new QLabel(QString("Diagnostics ID: %1").arg(remoteDiagnosticsClientId()), &dlg);
        idLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        lay->addWidget(idLabel);

        QFormLayout* form = new QFormLayout();
        QLineEdit* titleEdit = new QLineEdit(&dlg);
        titleEdit->setMaxLength(140);
        titleEdit->setPlaceholderText("Short summary");
        QComboBox* areaCombo = new QComboBox(&dlg);
        areaCombo->addItems({"P25 audio", "P25 follow/grants", "Hardware/device open", "WFM/NFM/AM demod", "Waterfall/spectrum", "Crash/freeze", "Performance", "Updater", "Other"});
        QComboBox* severityCombo = new QComboBox(&dlg);
        severityCombo->addItem("Problem / bug", "warn");
        severityCombo->addItem("Crash / data loss", "error");
        severityCombo->addItem("Performance / freeze", "warn");
        severityCombo->addItem("Question / feedback", "warn");
        form->addRow("Title", titleEdit);
        form->addRow("Area", areaCombo);
        form->addRow("Severity", severityCombo);
        lay->addLayout(form);

        QTextEdit* detailsEdit = new QTextEdit(&dlg);
        detailsEdit->setPlaceholderText("What happened, what you expected, frequency/mode, and whether it repeats.");
        detailsEdit->setMinimumHeight(150);
        lay->addWidget(detailsEdit);

        QCheckBox* includeAppLog = new QCheckBox("Include latest app log tail (4 KB)", &dlg);
        includeAppLog->setChecked(true);
        QCheckBox* includeP25Log = new QCheckBox("Include visible P25 log tail (2 KB)", &dlg);
        includeP25Log->setChecked(true);
        lay->addWidget(includeAppLog);
        lay->addWidget(includeP25Log);

        QHBoxLayout* fileLay = new QHBoxLayout();
        QLineEdit* fileEdit = new QLineEdit(&dlg);
        fileEdit->setPlaceholderText("Optional log or IQ/capture file. Only a tiny capped snippet is uploaded.");
        QPushButton* browseBtn = new QPushButton("Browse...", &dlg);
        fileLay->addWidget(fileEdit);
        fileLay->addWidget(browseBtn);
        lay->addLayout(fileLay);
        QLabel* capLabel = new QLabel("Attachment cap: 8 KB raw sample plus file size/hash metadata. Large IQ captures are never uploaded whole.", &dlg);
        capLabel->setWordWrap(true);
        lay->addWidget(capLabel);

        QHBoxLayout* btns = new QHBoxLayout();
        QPushButton* submitBtn = new QPushButton("Submit Report", &dlg);
        QPushButton* cancelBtn = new QPushButton("Cancel", &dlg);
        btns->addStretch();
        btns->addWidget(submitBtn);
        btns->addWidget(cancelBtn);
        lay->addLayout(btns);

        connect(browseBtn, &QPushButton::clicked, &dlg, [fileEdit, &dlg]() {
            const QString path = QFileDialog::getOpenFileName(&dlg, "Attach Capped Diagnostic Snippet");
            if (!path.isEmpty()) fileEdit->setText(path);
        });
        connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
        connect(submitBtn, &QPushButton::clicked, &dlg, [this, &dlg, titleEdit, areaCombo, severityCombo, detailsEdit, includeAppLog, includeP25Log, fileEdit]() {
            const QString title = titleEdit->text().trimmed();
            const QString details = detailsEdit->toPlainText().trimmed();
            if (title.isEmpty() || details.isEmpty()) {
                QMessageBox::warning(&dlg, "Report Issue", "Add a short title and details before submitting.");
                return;
            }

            QJsonObject payload = diagnosticsRuntimeSnapshot("manual-user-report");
            payload["userReportId"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
            payload["title"] = title.left(140);
            payload["area"] = areaCombo->currentText().left(80);
            payload["details"] = details.left(4000);
            payload["submittedAtUtc"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);

            if (includeAppLog->isChecked()) {
                const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
                const QString logPath = appData + "/logs/sdr_town.log";
                payload["appLogTail"] = QString::fromUtf8(readFileTailCapped(logPath, 4096)).left(4096);
                payload["appLogTailHash"] = diagnosticsBytesHash(readFileTailCapped(logPath, 4096), 24);
            }
            if (includeP25Log->isChecked()) {
                payload["p25VisibleLogTail"] = p25LogLines.join('\n').right(2048);
            }
            const QString attachmentPath = fileEdit->text().trimmed();
            if (!attachmentPath.isEmpty()) {
                payload["attachment"] = diagnosticsAttachmentSummary(attachmentPath, 8192);
            }

            QString severity = severityCombo->currentData().toString().trimmed();
            if (severity.isEmpty()) severity = "warn";
            remoteDiagnosticsSubmit("user.report", severity, payload);
            QMessageBox::information(&dlg, "Report Queued",
                "Your report has been queued for upload. It will appear under Help > My Submitted Issues after the server receives it.");
            dlg.accept();
        });

        dlg.exec();
    }

    void showMyDiagnosticsReports()
    {
        if (!remoteDiagnosticsEnabled()) {
            QMessageBox::warning(this, "My Submitted Issues",
                "Remote diagnostics are not configured, so there is no server issue list to show.");
            return;
        }

        QDialog dlg(this);
        dlg.setWindowTitle("My Submitted Issues");
        dlg.resize(920, 520);
        QVBoxLayout* lay = new QVBoxLayout(&dlg);
        QLabel* idLabel = new QLabel(QString("Diagnostics ID: %1").arg(remoteDiagnosticsClientId()), &dlg);
        idLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        lay->addWidget(idLabel);
        QTableWidget* table = new QTableWidget(0, 7, &dlg);
        table->setHorizontalHeaderLabels({"Issue ID", "Title", "Status", "Fixed In", "Last Seen", "Reports", "Note"});
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->horizontalHeader()->setStretchLastSection(true);
        lay->addWidget(table);

        QHBoxLayout* btns = new QHBoxLayout();
        QPushButton* refreshBtn = new QPushButton("Refresh", &dlg);
        QPushButton* closeBtn = new QPushButton("Close", &dlg);
        btns->addStretch();
        btns->addWidget(refreshBtn);
        btns->addWidget(closeBtn);
        lay->addLayout(btns);

        auto refresh = [this, table, refreshBtn]() {
            refreshBtn->setEnabled(false);
            table->setRowCount(0);
            remoteDiagnosticsCheckClientStatus(table, [table, refreshBtn](const QJsonObject& status) {
                refreshBtn->setEnabled(true);
                const QJsonArray issues = status.value("issues").toArray();
                table->setRowCount(issues.size());
                int row = 0;
                for (const QJsonValue& value : issues) {
                    const QJsonObject issue = value.toObject();
                    const QStringList cells = {
                        issue.value("issue_id").toString(),
                        issue.value("title").toString(),
                        issue.value("status").toString(),
                        issue.value("fixed_version").toString(),
                        issue.value("client_last_seen").toString(issue.value("last_seen").toString()),
                        QString::number(issue.value("client_report_count").toInt(issue.value("report_count").toInt())),
                        issue.value("fix_note").toString()
                    };
                    for (int col = 0; col < cells.size(); ++col) {
                        QTableWidgetItem* item = new QTableWidgetItem(cells[col]);
                        if (col == 2) {
                            const QString statusText = cells[col].toLower();
                            if (statusText == "fixed") item->setForeground(QColor(120, 220, 150));
                            else if (statusText == "unrequired") item->setForeground(QColor(180, 180, 180));
                            else item->setForeground(QColor(255, 210, 120));
                        }
                        table->setItem(row, col, item);
                    }
                    ++row;
                }
                if (!status.value("ok").toBool(false)) {
                    table->setRowCount(1);
                    table->setItem(0, 0, new QTableWidgetItem("status-error"));
                    table->setItem(0, 1, new QTableWidgetItem(status.value("error").toString("Could not load issue status.")));
                } else if (issues.isEmpty()) {
                    table->setRowCount(1);
                    table->setItem(0, 0, new QTableWidgetItem("none"));
                    table->setItem(0, 1, new QTableWidgetItem("No issues recorded for this installation yet."));
                }
                table->resizeColumnsToContents();
            });
        };
        connect(refreshBtn, &QPushButton::clicked, &dlg, refresh);
        connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
        refresh();
        dlg.exec();
    }

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
            "<b>Alpha diagnostics:</b> Tester builds may send compact automatic diagnostics "
            "and user-submitted capped snippets to the configured SDR Town diagnostics service. "
            "Full IQ/audio files are not uploaded automatically.<br><br>"
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
        masterSlider->setValue(static_cast<int>(std::lround(engine->getMasterVolume() * 100.0f)));
        QLabel* masterVal = new QLabel(QString("%1%").arg(masterSlider->value()));
        masterLay->addWidget(masterSlider);
        masterLay->addWidget(masterVal);
        lay->addLayout(masterLay);

        connect(masterSlider, &QSlider::valueChanged, [&](int v) {
            masterVal->setText(QString("%1%").arg(v));
            monitorMasterVolume = std::clamp(v / 100.0, 0.0, 1.0);
            engine->setMasterVolume(v / 100.0f);
        });

        // Device list
        QTableWidget* table = new QTableWidget(devs.size(), 5, &dlg);
        table->setHorizontalHeaderLabels({"Use", "Device Name", "Default", "Volume", "Test"});
        table->horizontalHeader()->setStretchLastSection(true);

        std::vector<QCheckBox*> useChecks;
        std::vector<QSlider*> volSliders;
        auto applyAudioSelection = [&]() -> std::vector<size_t> {
            std::vector<size_t> active;
            for (size_t i = 0; i < useChecks.size(); ++i) {
                if (useChecks[i]->isChecked()) active.push_back(i);
            }
            if (active.empty()) {
                return active;
            }
            engine->setActiveOutputs(active);
            preferredAudioOutputNames.clear();
            for (size_t idx : active) {
                if (idx < devs.size()) {
                    preferredAudioOutputNames.push_back(devs[idx].name);
                }
                // Volume index is position in the active list, not enum index.
            }
            for (size_t i = 0; i < active.size(); ++i) {
                if (active[i] < volSliders.size()) {
                    engine->setOutputVolume(i, volSliders[active[i]]->value() / 100.0f);
                }
            }
            monitorMasterVolume = std::clamp(masterSlider->value() / 100.0, 0.0, 1.0);
            engine->setMasterVolume(static_cast<float>(monitorMasterVolume));
            return active;
        };

        const auto currentlyActiveNames = engine->getActiveDeviceNameList();
        for (size_t i = 0; i < devs.size(); ++i) {
            int row = static_cast<int>(i);
            const auto& d = devs[i];

            QCheckBox* use = new QCheckBox();
            // Pre-check by live device name (stable across re-enumerate). Fall back
            // to default only when nothing is active yet.
            const bool nameActive = std::find(currentlyActiveNames.begin(),
                                              currentlyActiveNames.end(),
                                              d.name) != currentlyActiveNames.end();
            const bool preferred = std::find(preferredAudioOutputNames.begin(),
                                             preferredAudioOutputNames.end(),
                                             d.name) != preferredAudioOutputNames.end();
            const bool coldDefault = currentlyActiveNames.empty() &&
                                     preferredAudioOutputNames.empty() &&
                                     d.isDefault;
            use->setChecked(nameActive || preferred || coldDefault);
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
            connect(test, &QPushButton::clicked, [engine, i, this, &useChecks, &applyAudioSelection]() {
                if (i < useChecks.size()) useChecks[i]->setChecked(true);
                applyAudioSelection();
                engine->playTestToneForDevice(i, 1000.0f, 0.7f);
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
            const auto active = applyAudioSelection();
            if (active.empty()) {
                QMessageBox::warning(&dlg, "Audio outputs",
                    "Select at least one playback device before Apply.\n\n"
                    "Clearing all outputs was resetting to None and then fighting the "
                    "auto-activate path (freeze / intermittent device changes).");
                return;
            }

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

        QLabel* hint = new QLabel("Rescan to refresh. Enable devices, adjust gain/sample rate/antenna/PPM correction. Settings persist across runs. Real SoapySDR + HackRF recommended (stubs shown if no Soapy).");
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

        QTableWidget* table = new QTableWidget(devs.size(), 9, &dlg);
        QStringList headers = {"Enabled", "Runtime", "Label / Driver", "Serial", "Antenna", "Sample Rate (MS/s)", "Gain (dB)", "PPM", "Freq Range (MHz)"};
        table->setHorizontalHeaderLabels(headers);
        table->horizontalHeader()->setStretchLastSection(true);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);

        std::vector<QCheckBox*> enableChecks;
        std::vector<QComboBox*> antCombos;
        std::vector<QDoubleSpinBox*> rateSpins;
        std::vector<QDoubleSpinBox*> gainSpins;
        std::vector<QDoubleSpinBox*> ppmSpins;

        for (size_t i = 0; i < devs.size(); ++i) {
            const auto& d = devs[i];
            int row = static_cast<int>(i);

            // Enabled
            QCheckBox* cb = new QCheckBox();
            cb->setChecked(d.enabled);
            table->setCellWidget(row, 0, cb);
            enableChecks.push_back(cb);

            table->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(mgr.getRuntimeStateLabel(i))));

            // Label
            table->setItem(row, 2, new QTableWidgetItem(QString("%1 (%2)").arg(QString::fromStdString(d.label)).arg(QString::fromStdString(d.driver))));

            // Serial
            table->setItem(row, 3, new QTableWidgetItem(QString::fromStdString(d.serial)));

            // Antenna combo
            QComboBox* ant = new QComboBox();
            for (const auto& a : d.antennas) ant->addItem(QString::fromStdString(a));
            if (!d.antenna.empty()) ant->setCurrentText(QString::fromStdString(d.antenna));
            table->setCellWidget(row, 4, ant);
            antCombos.push_back(ant);

            // Sample rate
            QDoubleSpinBox* rate = new QDoubleSpinBox();
            rate->setRange(0.1, 60.0);
            rate->setDecimals(3);
            rate->setSingleStep(0.1);
            rate->setSuffix(" MS/s");
            rate->setValue(d.sampleRate / 1e6);
            // add common rates from list if present
            table->setCellWidget(row, 5, rate);
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
            table->setCellWidget(row, 6, gain);
            gainSpins.push_back(gain);

            QDoubleSpinBox* ppm = new QDoubleSpinBox();
            ppm->setRange(-200.0, 200.0);
            ppm->setDecimals(2);
            ppm->setSingleStep(0.5);
            ppm->setSuffix(" ppm");
            ppm->setValue(d.frequencyCorrectionPpm);
            ppm->setToolTip("Oscillator correction. Positive values compensate receivers that read high; applied live when supported.");
            table->setCellWidget(row, 7, ppm);
            ppmSpins.push_back(ppm);
            connect(ppm, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [i](double ppmVal) {
                DeviceManager::instance().setFrequencyCorrection(i, ppmVal);
            });

            // Make per-device gain changes in the dialog live while the device is running.
            // Previously only took effect on "Apply" + restart for many users.
            connect(gain, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [i](double gval) {
                auto& mgr = DeviceManager::instance();
                mgr.setLiveGain(i, gval);
            });

            // Freq range
            QString fr = QString("%1 – %2").arg(d.minFreq/1e6, 0, 'f', 0).arg(d.maxFreq/1e6, 0, 'f', 0);
            table->setItem(row, 8, new QTableWidgetItem(fr));
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
                    double ppm = ppmSpins[i]->value();
                    std::string ant = antCombos[i]->currentText().toStdString();

                    mgr.updateDeviceParams(i, rateHz, g, ant, ppm);

                    // Start/stop real streaming on enable. startStreaming itself is hardened (try/catch + stub fallback + thread guards)
                    // so this should not propagate, but outer try is defense-in-depth for any future native/USB fault on Apply.
                    if (en) {
                        mgr.startStreaming(i, true /* real SDR, not stub */);
                        mgr.setLiveGain(i, g);
                        mgr.setFrequencyCorrection(i, ppm);
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
                if (remoteDiagnosticsEnabled()) {
                    QJsonObject payload = diagnosticsRuntimeSnapshot("device-manager-apply-exception");
                    payload["stage"] = "device-manager-apply";
                    payload["message"] = QString::fromLocal8Bit(ex.what()).left(500);
                    remoteDiagnosticsSubmit("hardware.open", "error", payload);
                }
                statusBar()->showMessage("Apply error (see log). Device left in safe/stub state if possible.", 6000);
                QMessageBox::warning(&dlg, "Device Start Error",
                    QString("An error occurred while starting the device (RTL-SDR or other).\n\n%1\n\nCheck the log for details (sdr_town.log). "
                            "The device will use safe internal simulation if real hardware could not be initialized. "
                            "Verify Zadig WinUSB driver is installed for the RTL device and that no other SDR app has it open.").arg(ex.what()));
            } catch (...) {
                spdlog::error("Unknown exception during Device Manager Apply (possible driver/USB SEH).");
                if (remoteDiagnosticsEnabled()) {
                    QJsonObject payload = diagnosticsRuntimeSnapshot("device-manager-apply-unknown-exception");
                    payload["stage"] = "device-manager-apply";
                    payload["message"] = "Unknown exception during device apply";
                    remoteDiagnosticsSubmit("hardware.open", "error", payload);
                }
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
    struct P25VoiceDecodeJob {
        std::shared_ptr<Receiver> rx;
        std::vector<std::complex<float>> iq;
        std::vector<size_t> audioOutputIndices;
        double sampleRateHz = 0.0;
        double centerFreqHz = 0.0;
        double targetFreqHz = 0.0;
        double outputRateHz = 48000.0;
        uint64_t iqStartAbsolute = 0;
        bool iqStartAbsoluteKnown = false;
        uint64_t iqDecodeEndAbsolute = 0;
        bool iqDecodeEndAbsoluteKnown = false;
        bool outputMutedForSettle = false;
        bool rollingDecode = false;
        size_t freshIqSamples = 0;
        size_t contextIqSamples = 0;
        uint64_t trafficGeneration = 0;
        bool independentTrafficSource = false;
        bool speakerSustainDecode = false;
        uint32_t talkgroupId = 0;
        uint32_t sourceId = 0;
        bool tdmaSlotKnown = false;
        uint8_t tdmaSlot = 0;
        double voiceFreqHz = 0.0;
        uint64_t sequence = 0;
        uint64_t flushSeq = 0;
        ReceiverSessionKey receiverSessionKey{};
        uint64_t callSessionId = 0;
    };

    struct P25VoiceDecodeResult {
        std::shared_ptr<Receiver> rx;
        P25VoiceAudioBlock audio;
        std::vector<float> speakerAudio;
        std::vector<size_t> audioOutputIndices;
        double sampleRateHz = 0.0;
        double centerFreqHz = 0.0;
        double targetFreqHz = 0.0;
        double outputRateHz = 48000.0;
        double rmsDb = -120.0;
        long long dspMicros = 0;
        uint64_t iqStartAbsolute = 0;
        bool iqStartAbsoluteKnown = false;
        uint64_t iqDecodeEndAbsolute = 0;
        bool iqDecodeEndAbsoluteKnown = false;
        bool outputMutedForSettle = false;
        bool rollingDecode = false;
        bool speakerMayEmit = false;
        bool hasAudioBlock = false;
        bool publishVoiceDiag = false;
        bool stale = false;
        size_t iqSamples = 0;
        size_t freshIqSamples = 0;
        size_t contextIqSamples = 0;
        uint64_t trafficGeneration = 0;
        uint32_t talkgroupId = 0;
        uint32_t sourceId = 0;
        bool tdmaSlotKnown = false;
        uint8_t tdmaSlot = 0;
        double voiceFreqHz = 0.0;
        uint64_t sequence = 0;
        uint64_t flushSeq = 0;
        ReceiverSessionKey receiverSessionKey{};
        uint64_t callSessionId = 0;
        std::string speakerGateReason;
        std::string staleReason;
        std::string error;
    };

    struct P25VoiceWorkerQueueSnapshot {
    bool stopping = false;
    bool threadRunning = false;
    bool pending = false;
    bool busy = false;
    size_t pendingJobs = 0;
    uint64_t nextSequence = 0;
    long long droppedJobs = 0;
        long long droppedResults = 0;
        size_t completedResults = 0;
        long long publicationLockMisses = 0;
        size_t pendingPublishResults = 0;
    };

    QTimer* updateTimer = nullptr;
    std::unique_ptr<AudioEngine> engineForAudio;
    std::thread guiDspWorker;
    std::thread p25ControlWorkerThread;
    std::thread p25VoiceWorkerThread;
    GuiRuntimeConfig guiRuntimeConfig;
    QStringList guiRuntimeStartupErrors;
    qint64 guiRuntimeStartupAppliedMs = 0;
    std::atomic<long long> guiP25AudioOutputEvents{0};
    std::atomic<long long> guiP25AudioOutputSamples{0};
    std::atomic<long long> guiP25AudioDecodedFrames{0};
    std::atomic<long long> guiP25AudioAcceptedAmbeFrames{0};
    std::atomic<long long> guiP25AudioLastOutputMs{0};
    std::atomic<long long> guiIqReplayWindows{0};
    std::atomic<long long> guiIqReplayAudioOutputEvents{0};
    std::atomic<long long> guiIqReplayAudioOutputSamples{0};
    std::atomic<long long> guiIqReplayDecodedFrames{0};
    std::atomic<long long> guiIqReplayLastOutputMs{0};
    std::mutex guiIqReplayStatusMutex;
    QString guiIqReplayLastStatus;
    QStringList guiIqReplayRecentStatus;

    UpdateManager* m_updateManager = nullptr;   // professional GitHub release + in-app updater (state-of-the-art, safe)
    TranscriptHub* m_transcriptHub = nullptr;
    SttEngine* m_sttEngine = nullptr;
    P25TranscriptSource* m_p25TranscriptSource = nullptr;
    TranscriptWindow* m_transcriptWindow = nullptr;
    std::atomic<bool> stopDspWorker{false};
    std::atomic<bool> p25VoiceWorkerStop{false};
    std::atomic<bool> p25VoiceWorkerBusy{false};
    std::atomic<uint64_t> p25VoiceJobSequence{0};
    std::atomic<long long> p25VoiceDroppedJobs{0};
    std::atomic<long long> p25VoiceDroppedResults{0};
    std::atomic<long long> p25VoicePublicationLockMisses{0};
    std::atomic<size_t> p25VoicePendingPublishDepth{0};
    std::mutex p25VoiceWorkerMutex;
    std::condition_variable p25VoiceWorkerCv;
    std::deque<P25VoiceDecodeJob> p25VoicePendingJobs;
    std::deque<P25VoiceDecodeResult> p25VoiceCompletedResults;
    std::mutex monitorParamsMutex;  // protects currentMonitor* / monitor* vars between GUI DSP worker and UI thread (P2) -- transitional during per-receiver refactor

    // S0-2 (P0 audit): mutex + snapshot for receivers vector. GUI thread mutates (Add, Remove, tune etc).
    // DSP worker takes short snapshot then processes without holding lock or & across yields.
    // Same pattern applied to CLI cliReceivers.
    std::mutex receiversMutex;

    // S0-5 (P1): centralize AudioEngine creation. DSP worker and hot paths call this;
    // creation happens at most once (call_once). UI config also uses it.
    // guiStartupSettled stays false until after show()/event-loop entry so the DSP
    // worker cannot open WASAPI/miniaudio (or queue UI updates) during ctor.
    std::atomic<bool> guiStartupSettled{false};
    std::atomic<bool> shutdownStarted{false};
    std::atomic<bool> stopAllStreamingStarted{false};
    std::once_flag audioEngineInitFlag;
    AudioEngine* getOrCreateAudioEngine() {
        if (shutdownStarted.load(std::memory_order_acquire) ||
            !guiStartupSettled.load(std::memory_order_acquire)) {
            return engineForAudio.get();
        }
        std::call_once(audioEngineInitFlag, [this]() {
            if (!engineForAudio) {
                engineForAudio = std::make_unique<AudioEngine>();
                engineForAudio->setMasterVolume(static_cast<float>(monitorMasterVolume));
            }
        });
        return engineForAudio.get();
    }

    // Non-creating peek for idle DSP paths. Never opens devices.
    AudioEngine* peekAudioEngineIfReady() const noexcept
    {
        if (!guiStartupSettled.load(std::memory_order_acquire)) return nullptr;
        if (shutdownStarted.load(std::memory_order_acquire)) return nullptr;
        AudioEngine* eng = engineForAudio.get();
        if (!eng || eng->activeOutputCount() == 0) return nullptr;
        return eng;
    }

    AudioEngine* ensureAudioOutputActive(const char* reason = "audio")
    {
        if (!guiStartupSettled.load(std::memory_order_acquire) ||
            shutdownStarted.load(std::memory_order_acquire)) {
            return peekAudioEngineIfReady();
        }

        AudioEngine* eng = getOrCreateAudioEngine();
        if (!eng) return nullptr;
        if (eng->activeOutputCount() > 0) return eng;

        try {
            const auto outputs = eng->enumeratePlaybackDevices();
            if (outputs.empty()) {
                spdlog::warn("Audio auto-activate failed for {}: no playback devices were enumerated.",
                             reason ? reason : "audio");
                return eng;
            }

            // Prefer the user's last Apply selection (full names) before default.
            if (!preferredAudioOutputNames.empty()) {
                eng->setActiveOutputsByName(preferredAudioOutputNames);
                if (eng->activeOutputCount() > 0) {
                    const std::string names = eng->getActiveDeviceNames();
                    spdlog::info("Audio auto-activated preferred output for {}: {}",
                                 reason ? reason : "audio", names);
                    return eng;
                }
            }

            size_t selected = 0;
            for (size_t i = 0; i < outputs.size(); ++i) {
                if (outputs[i].isDefault) {
                    selected = i;
                    break;
                }
            }

            eng->setActiveOutputs({selected});
            if (preferredAudioOutputNames.empty() && selected < outputs.size()) {
                preferredAudioOutputNames.push_back(outputs[selected].name);
            }
            const std::string names = eng->getActiveDeviceNames();
            spdlog::info("Audio auto-activated default output for {}: {}", reason ? reason : "audio", names);
            const QString qNames = QString::fromStdString(names);
            const QString qReason = QString::fromUtf8(reason ? reason : "audio");
            QMetaObject::invokeMethod(this, [this, qNames, qReason]() {
                if (statusBar()) {
                    statusBar()->showMessage(QString("Audio output active for %1: %2").arg(qReason, qNames), 3000);
                }
            }, Qt::QueuedConnection);
        } catch (const std::exception& ex) {
            spdlog::warn("Audio auto-activate failed for {}: {}", reason ? reason : "audio", ex.what());
        } catch (...) {
            spdlog::warn("Audio auto-activate failed for {}: unknown error.", reason ? reason : "audio");
        }

        return eng;
    }

    void startP25VoiceWorker()
    {
        if (p25VoiceWorkerThread.joinable()) return;
        p25VoiceWorkerStop.store(false, std::memory_order_release);
        p25VoiceWorkerThread = std::thread([this]() {
            for (;;) {
                P25VoiceDecodeJob job;
                {
                    std::unique_lock<std::mutex> lock(p25VoiceWorkerMutex);
                    p25VoiceWorkerCv.wait(lock, [this]() {
                        return p25VoiceWorkerStop.load(std::memory_order_acquire) ||
                               !p25VoicePendingJobs.empty();
                    });
                    if (p25VoiceWorkerStop.load(std::memory_order_acquire) && p25VoicePendingJobs.empty()) {
                        break;
                    }
                    job = std::move(p25VoicePendingJobs.front());
                    p25VoicePendingJobs.pop_front();
                    p25VoiceWorkerBusy.store(true, std::memory_order_release);
                }

                P25VoiceDecodeResult result;
                result.rx = job.rx;
                result.audioOutputIndices = job.audioOutputIndices;
                result.sampleRateHz = job.sampleRateHz;
                result.centerFreqHz = job.centerFreqHz;
                result.targetFreqHz = job.targetFreqHz;
                result.outputRateHz = job.outputRateHz;
                result.iqStartAbsolute = job.iqStartAbsolute;
                result.iqStartAbsoluteKnown = job.iqStartAbsoluteKnown;
                result.iqDecodeEndAbsolute = job.iqDecodeEndAbsolute;
                result.iqDecodeEndAbsoluteKnown = job.iqDecodeEndAbsoluteKnown;
                result.outputMutedForSettle = job.outputMutedForSettle;
                result.rollingDecode = job.rollingDecode;
                result.iqSamples = job.iq.size();
                result.freshIqSamples = job.freshIqSamples;
                result.contextIqSamples = job.contextIqSamples;
                result.trafficGeneration = job.trafficGeneration;
                result.talkgroupId = job.talkgroupId;
                result.sourceId = job.sourceId;
                result.tdmaSlotKnown = job.tdmaSlotKnown;
                result.tdmaSlot = job.tdmaSlot;
                result.voiceFreqHz = job.voiceFreqHz;
                result.sequence = job.sequence;
                result.flushSeq = job.flushSeq;
                result.receiverSessionKey = job.receiverSessionKey;
                result.callSessionId = job.callSessionId;

                const uintptr_t workerRxKey = reinterpret_cast<uintptr_t>(job.rx.get());
                const uint64_t workerSeq = job.sequence;
                const size_t workerIq = job.iq.size();
                const size_t workerFresh = job.freshIqSamples;
                const size_t workerContext = job.contextIqSamples;
                const bool workerRolling = job.rollingDecode;
                const double workerSr = job.sampleRateHz;
                const double workerCf = job.centerFreqHz;
                const double workerTarget = job.targetFreqHz;
                const uint32_t workerTg = job.talkgroupId;
                const uint32_t workerSource = job.sourceId;
                const bool workerSlotKnown = job.tdmaSlotKnown;
                const int workerSlot = static_cast<int>(job.tdmaSlot & 0x01u);
                const uint64_t workerGeneration = job.trafficGeneration;
                QTimer::singleShot(0, this, [this, workerRxKey, workerSeq, workerIq, workerFresh,
                                              workerContext, workerRolling, workerSr, workerCf,
                                              workerTarget, workerTg, workerSlotKnown, workerSlot,
                                              workerGeneration, workerSource]() {
                    const QString key = QString("p25-voice-worker-start:%1").arg(static_cast<qulonglong>(workerRxKey));
                    appendP25LogLineKeyed(key,
                        QString("P25 DSP VOICE WORKER START: seq=%1 rolling=%2 iq=%3 fresh=%4 context=%5 sr=%6MHz cf=%7MHz target=%8MHz tg=%9 src=%10 slot=%11 generation=%12.")
                            .arg(static_cast<qulonglong>(workerSeq))
                            .arg(workerRolling ? "yes" : "no")
                            .arg(static_cast<qulonglong>(workerIq))
                            .arg(static_cast<qulonglong>(workerFresh))
                            .arg(static_cast<qulonglong>(workerContext))
                            .arg(workerSr / 1e6, 0, 'f', 3)
                            .arg(workerCf / 1e6, 0, 'f', 5)
                            .arg(workerTarget / 1e6, 0, 'f', 5)
                            .arg(workerTg)
                            .arg(workerSource != 0 ? p25HexId(workerSource, 6) : QStringLiteral("unknown"))
                            .arg(workerSlotKnown ? QString::number(workerSlot) : QStringLiteral("unknown"))
                            .arg(static_cast<qulonglong>(workerGeneration)),
                        750);
                });

                auto applyQueuedSlotProbeBeforeDecode = [&](std::string* reason) -> bool {
                    if (!job.rx) return false;
                    Receiver& rx = *job.rx;
                    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
                    std::lock_guard<std::mutex> rxLock(rx.stateMutex);
                    if (!rx.p25VoiceSlotProbePending || !rx.p25VoiceDecodeEnabled || !rx.p25VoicePhase2) {
                        return false;
                    }

                    // Immutable grant slot: cancel queued probe and keep decoding.
                    if (p25Phase2GrantedSlotIsImmutable(rx)) {
                        ++rx.p25DiagSlotProbeBlocked;
                        rx.p25VoiceSlotProbePending = false;
                        rx.p25VoiceSlotProbeRequested = 0;
                        return false;
                    }

                    // If the current granted slot has produced recent target-slot PCM,
                    // the queued probe is stale.  Cancel it instead of flipping away
                    // from working audio.  This addresses field logs where
                    // slotProbePending=yes stayed set while decoded/audio continued.
                    const auto& diag = rx.p25VoiceDiagnostics;
                    const bool recentUsefulAudio =
                        diag.updatedMs > 0 &&
                        nowMs - diag.updatedMs <= 1800 &&
                        diag.decodedFrames > 0 &&
                        diag.audioSamples > 0 &&
                        diag.phase2TargetVoiceCodewords > 0 &&
                        diag.phase2OppositeVoiceCodewords == 0;
                    if (recentUsefulAudio) {
                        rx.p25VoiceSlotProbePending = false;
                        rx.p25VoiceSlotProbeRequested = 0;
                        return false;
                    }

                    const uint8_t requestedSlot = static_cast<uint8_t>(rx.p25VoiceSlotProbeRequested & 0x01u);
                    {
                        std::unique_lock<std::recursive_mutex> dspLock(rx.dspMutex, std::try_to_lock);
                        if (!dspLock.owns_lock()) {
                            if (reason) {
                                *reason = "slot-probe-pending-dsp-busy";
                            }
                            // Do not decode the old slot while a probe is pending; that
                            // is exactly how stale slot hypotheses leaked random/choppy audio.
                            return true;
                        }
                        if (!applyP25Phase2SlotProbeLocked(rx, requestedSlot, nowMs)) {
                            return false;
                        }
                    }
                    if (reason) {
                        *reason = "slot-probe-applied-before-decode";
                    }
                    return false;
                };

                auto stillCurrent = [&](std::string* reason) -> bool {
                    auto fail = [&](const char* why) {
                        if (reason && reason->empty()) *reason = why ? why : "stale";
                        return false;
                    };
                    if (!job.rx) return fail("no-receiver");
                    if (job.flushSeq != p25PendingAudioFlushSeq.load(std::memory_order_acquire)) {
                        return fail("audio-flush-sequence-stale");
                    }
                    Receiver& rx = *job.rx;
                    std::lock_guard<std::mutex> rxLock(rx.stateMutex);
                    if (!rx.active) return fail("receiver-inactive");
                    if (!rx.p25VoiceDecodeEnabled) return fail("voice-decode-disabled");
                    if (!rx.p25VoicePhase2) return fail("not-phase2");
                    if (rx.p25VoiceResetPending) return fail("voice-reset-pending");
                    if (job.callSessionId != 0 &&
                        rx.p25CurrentCallSessionId != 0 &&
                        rx.p25CurrentCallSessionId != job.callSessionId) {
                        return fail("call-session-changed");
                    }
                    const double liveVoiceHz = rx.p25TrafficVoiceFreqHz > 0.0 ? rx.p25TrafficVoiceFreqHz : rx.freqHz;
                    if (job.voiceFreqHz > 0.0 && liveVoiceHz > 0.0 &&
                        std::abs(liveVoiceHz - job.voiceFreqHz) > 50.0) {
                        return fail("voice-frequency-changed");
                    }
                    // The decoder may observe both TDMA slots on one RF carrier,
                    // but the GUI speaker path has one selected call.  Do not
                    // publish a queued result for an old TG/slot just because the
                    // physical voice carrier/generation still matches.
                    if (job.talkgroupId != 0 && rx.p25VoiceTalkgroupId != job.talkgroupId) return fail("talkgroup-changed");
                    // Source/RID is traffic metadata, not a live decode-session
                    // boundary.  The selected Phase-2 audio path is keyed by
                    // TG, call session, grant epoch, slot, and carrier; dropping
                    // worker results solely on RID churn discards queued target
                    // AMBE before ESS/PTT can release it.
                    if (job.tdmaSlotKnown &&
                        (!rx.p25VoiceTdmaSlotKnown ||
                         static_cast<uint8_t>(rx.p25VoiceTdmaSlot & 0x01u) != static_cast<uint8_t>(job.tdmaSlot & 0x01u))) {
                        return fail("slot-changed");
                    }
                    if (job.trafficGeneration != 0 || job.independentTrafficSource) {
                        const uint64_t liveGen = p25TrafficSourceGeneration.load(std::memory_order_acquire);
                        if (job.trafficGeneration == 0 ||
                            rx.p25TrafficGeneration != job.trafficGeneration ||
                            liveGen != job.trafficGeneration) {
                            return fail("traffic-generation-stale");
                        }
                    }
                    return true;
                };

                bool publishResult = false;
                try {
                    std::string staleReason;
                    if (!job.rx) {
                        result.stale = true;
                        result.staleReason = "no-receiver";
                        publishResult = true;
                    } else if (job.iq.empty()) {
                        result.stale = true;
                        result.staleReason = "empty-iq";
                        publishResult = true;
                    } else if (applyQueuedSlotProbeBeforeDecode(&staleReason)) {
                        result.stale = true;
                        result.staleReason = staleReason.empty() ? "slot-probe-applied-before-decode" : staleReason;
                        publishResult = true;
                    } else if (!stillCurrent(&staleReason)) {
                        result.stale = true;
                        result.staleReason = staleReason.empty() ? "stale-before-decode" : staleReason;
                        publishResult = true;
                    } else {
                        Receiver& rx = *job.rx;
                        const auto t0 = std::chrono::steady_clock::now();
                        // Cold only until hard acquire, target VCW, or solid
                        // mask/superframe structure. Ending cold on any p2burst
                        // alone (20260807_235726) drove lock-only/hot budgets
                        // while the eye was still wrong → emit=7 empty=805.
                        // Capture 20260909_053448: structureNoVcw eyes still ran
                        // cold 240/64 (med ~484 ms) because peakTargetVcw stayed
                        // 0 while sf/mask were already present — worker-busy
                        // starved the next live windows (drop D). Exit cold once
                        // sustain has epoch-grade structure (sf+mask >=4), same
                        // bar as tdmaEpochLockedNoMacEss, without inventing TTL.
                        const auto& coldSustain = rx.p25SessionState.sustain;
                        const bool sustainStructureAcquired =
                            coldSustain.peakPhase2MaskedBursts >= 4 &&
                            coldSustain.peakPhase2SuperframeBursts >= 4;
                        const bool coldAcquireJob =
                            rx.p25IndependentTrafficSource &&
                            rx.p25VoicePhase2 &&
                            !p25Phase2SessionHasHardTargetAcquire(rx) &&
                            !sustainStructureAcquired &&
                            coldSustain.peakPhase2TargetVoiceCodewords == 0 &&
                            rx.p25VoiceDiagnostics.phase2TargetVoiceCodewords == 0;
                        const bool establishedClearStreaming =
                            p25Phase2EstablishedClearVoiceStreamingLocked(rx);
                        const P25VoiceDiagSnapshot& workerDiag = rx.p25VoiceDiagnostics;
                        const bool selectedClearStreamingEye =
                            rx.p25VoiceClearKnown &&
                            !rx.p25VoiceEncrypted &&
                            rx.p25VoiceMaskParamsKnown &&
                            rx.p25VoiceTdmaSlotKnown &&
                            (rx.p25SessionState.sustain.peakPhase2TargetVoiceCodewords >= 1 ||
                             workerDiag.phase2TargetVoiceCodewords > 0 ||
                             rx.p25SessionState.sustain.peakPhase2MaskedBursts >= 1 ||
                             workerDiag.phase2MaskedBursts > 0 ||
                             p25DiagTargetHardClear(workerDiag)) &&
                            p25DiagTargetHardClear(workerDiag) &&
                            (p25Phase2SessionHadBurstEye(rx) ||
                             rx.p25VoiceLiveDecoder.cqpskLockValid() ||
                             rx.p25SessionState.sustain.hadBootstrapMaskLock);
                        // Lock-only only when streaming DDC is on AND CQPSK is
                        // actually valid with clear streaming evidence. Never
                        // force candidates=1 from a single false p2burst.
                        // DEC-0021/0022: streaming eye still dies after first
                        // emit on 105622 even without lock-only / with 160 ms
                        // slices — leave this gate in place for future env=1
                        // work; default live stays block (DEC-0014).
                        const bool streamingCqpskJob =
                            rx.p25IndependentTrafficSource &&
                            rx.p25VoicePhase2 &&
                            rx.p25VoiceLiveDecoder.config().enableStreamingChannelDdc &&
                            rx.p25VoiceLiveDecoder.cqpskLockValid() &&
                            !coldAcquireJob &&
                            (establishedClearStreaming ||
                             selectedClearStreamingEye ||
                             rx.p25SessionState.sustain.hadSuccessfulEmit);
                        const bool hotPhase2TrafficJob =
                            rx.p25IndependentTrafficSource &&
                            rx.p25VoicePhase2 &&
                            !coldAcquireJob &&
                            (streamingCqpskJob ||
                             p25Phase2SessionHasHardTargetAcquire(rx) ||
                             p25Phase2SessionHadBurstEye(rx) ||
                             rx.p25SessionState.sustain.peakPhase2MaskedBursts >= 1 ||
                             rx.p25SessionState.sustain.peakPhase2SuperframeBursts >= 2);
                        const int decodeWallMs = coldAcquireJob
                            ? kP25VoiceWorkerColdDecodeWallMs
                            : kP25VoiceWorkerMaxDecodeWallMs;
                        const auto dspWaitDeadline = t0 + std::chrono::milliseconds(kP25VoiceWorkerDspMutexWaitMs);
                        std::unique_lock<std::recursive_mutex> dspLock(rx.dspMutex, std::defer_lock);
                        while (!dspLock.try_lock()) {
                            if (std::chrono::steady_clock::now() >= dspWaitDeadline) break;
                            std::this_thread::sleep_for(std::chrono::milliseconds(2));
                        }
                        if (!dspLock.owns_lock()) {
                            result.stale = true;
                            result.staleReason = "dsp-mutex-timeout";
                            publishResult = true;
                        } else {
                            const auto decodeDeadline =
                                std::chrono::steady_clock::now() + std::chrono::milliseconds(decodeWallMs);
                            const int priorDecodeBudgetMs = rx.p25VoiceLiveDecoder.config().realtimeDecodeBudgetMs;
                            const size_t priorCqpskCandidates = rx.p25VoiceLiveDecoder.config().maxCqpskSearchCandidates;
                            const size_t priorPhase2SyncHits = rx.p25VoiceLiveDecoder.config().maxPhase2SyncHits;
                            const size_t priorPhase2Locks = rx.p25VoiceLiveDecoder.config().maxPhase2SuperframeLocks;
                            auto boundedConfigValue = [](size_t current, size_t cap) {
                                return current == 0 ? cap : std::min(current, cap);
                            };
                            // Never boost CQPSK budget to the wall timeout — that
                            // turned cold acquire into multi-second monoliths
                            // (20260712_021852 seq=2 decode-wall-timeout ~2.7s).
                            // Cap cold jobs instead so soft-AFC / ±1250 probes fit
                            // several attempts inside one short PTT.
                            // Once CQPSK is soft-locked, force lock-only streaming
                            // (SDRTrunk continuous demod) instead of re-searching.
                            if (coldAcquireJob) {
                                rx.p25VoiceLiveDecoder.setRealtimeDecodeBudgetMs(
                                    std::min(priorDecodeBudgetMs, kP25VoiceWorkerColdRealtimeBudgetMs));
                                rx.p25VoiceLiveDecoder.setMaxCqpskSearchCandidates(
                                    std::min(priorCqpskCandidates, kP25VoiceWorkerColdMaxCqpskCandidates));
                            } else if (streamingCqpskJob) {
                                // Live locked stream: one Costas eye, walk every
                                // timeslot in the hop. Candidates=1 is required
                                // (0 = unlimited grid). Do not cap SF locks at 1
                                // — that was the 80 ms Voice4 chip.
                                rx.p25VoiceLiveDecoder.setRealtimeDecodeBudgetMs(
                                    std::min(priorDecodeBudgetMs, kP25LiveLockedStreamBudgetMs));
                                rx.p25VoiceLiveDecoder.setMaxCqpskSearchCandidates(
                                    kP25LiveLockedStreamCqpskCandidates);
                                rx.p25VoiceLiveDecoder.setMaxPhase2SyncHits(
                                    kP25LiveLockedStreamSyncHits);
                                rx.p25VoiceLiveDecoder.setMaxPhase2SuperframeLocks(
                                    kP25LiveLockedStreamSuperframeLocks);
                            } else if (hotPhase2TrafficJob &&
                                       rx.p25VoiceLiveDecoder.config().enableStreamingChannelDdc) {
                                rx.p25VoiceLiveDecoder.setRealtimeDecodeBudgetMs(
                                    std::min(priorDecodeBudgetMs, kP25VoiceWorkerHotRealtimeBudgetMs));
                                rx.p25VoiceLiveDecoder.setMaxCqpskSearchCandidates(
                                    boundedConfigValue(priorCqpskCandidates, kP25VoiceWorkerHotMaxCqpskCandidates));
                                rx.p25VoiceLiveDecoder.setMaxPhase2SyncHits(
                                    boundedConfigValue(priorPhase2SyncHits, kP25VoiceWorkerHotMaxPhase2SyncHits));
                                rx.p25VoiceLiveDecoder.setMaxPhase2SuperframeLocks(
                                    boundedConfigValue(priorPhase2Locks, kP25VoiceWorkerHotMaxPhase2SuperframeLocks));
                            } else if (hotPhase2TrafficJob) {
                                // Block-channelize clears CQPSK/Gardner every hop.
                                // Capture 20260808_012422: after emit islands,
                                // empty eyes needed re-lock — that is coldAcquireJob
                                // / soft mask rehunt, not post-emit 240/64.
                                const bool speakerLiveHot =
                                    rx.p25SessionState.sustain.hadSuccessfulEmit ||
                                    p25Phase2SessionSpeakerSustainActive(rx) ||
                                    establishedClearStreaming;
                                // DEC-0026/0027 tried to narrow post-emit cold
                                // escalate (opp-only on capture 20260908_110146,
                                // then emptyEye-only on 20260908_112922). Capture
                                // 20260908_115603 after DEC-0027: first emit on TG
                                // 30302 was perfect (duty 0.553 emit=28), then
                                // emptyEye streak>=2 armed cold 240/64 and the
                                // *next* structure/opp/target hops burned
                                // dsp=470–605 ms (worker-busy). emptyEye itself is
                                // already ~102 ms on the hot grid. Soft mask-epoch
                                // rehunt (StructureNoTargetVoiceWindows) and
                                // coldAcquireJob cover true lost-eye / new follow.
                                // DEC-0028: never cold-escalate after the call has
                                // spoken — stay on hot search (not cold 240/64).
                                int hotBudgetMs = 80;
                                size_t hotCands = size_t{12};
                                size_t hotSyncHits = kP25VoiceWorkerHotMaxPhase2SyncHits;
                                size_t hotSfLocks = kP25VoiceWorkerHotMaxPhase2SuperframeLocks;
                                if (speakerLiveHot) {
                                    // DEC-0035 / capture 20260909_094846: live
                                    // post-emit used hot cand=8/120 while GUI
                                    // replay + CLI voicetest use cand=16/240 on
                                    // the *same* IQ and recover targetVcw (file
                                    // duty 0.43 vs live max duty 0.338 then all-A).
                                    // When the previous hop already has a target
                                    // eye, keep DEC-0019 cand=8 (060221 worker-busy).
                                    // When the target eye is gone, match replay
                                    // caps so block-channelize can re-lock Costas.
                                    //
                                    // DEC-0039 / capture 20260909_110941: after
                                    // speak, hops with companion/structure bursts
                                    // but targetVcw=0 kept cand=8 and never
                                    // matched the 095846 re-lock. Treat post-emit
                                    // no-target (and no decode) as eye-lost too.
                                    const auto& liveDiag = rx.p25VoiceDiagnostics;
                                    const bool noTargetEye =
                                        liveDiag.phase2TargetVoiceCodewords == 0 &&
                                        liveDiag.decodedFrames == 0;
                                    const bool noStructureEye =
                                        liveDiag.phase2Bursts == 0 &&
                                        liveDiag.phase2MaskedBursts == 0;
                                    const bool eyeLost =
                                        noTargetEye &&
                                        (noStructureEye ||
                                         rx.p25SessionState.sustain.hadSuccessfulEmit);
                                    if (eyeLost) {
                                        hotBudgetMs = kP25ReplayHotBudgetMs;
                                        hotCands = kP25ReplayHotCqpskCandidates;
                                        hotSyncHits = kP25ReplayHotSyncHits;
                                        hotSfLocks = kP25ReplayHotSuperframeLocks;
                                    } else {
                                        hotBudgetMs = kP25VoiceWorkerHotRealtimeBudgetMs;
                                        hotCands = kP25VoiceWorkerHotMaxCqpskCandidates;
                                        hotSyncHits = kP25VoiceWorkerHotMaxPhase2SyncHits;
                                        hotSfLocks = kP25VoiceWorkerHotMaxPhase2SuperframeLocks;
                                    }
                                }
                                rx.p25VoiceLiveDecoder.setRealtimeDecodeBudgetMs(
                                    std::min(priorDecodeBudgetMs, hotBudgetMs));
                                rx.p25VoiceLiveDecoder.setMaxCqpskSearchCandidates(
                                    boundedConfigValue(priorCqpskCandidates, hotCands));
                                rx.p25VoiceLiveDecoder.setMaxPhase2SyncHits(
                                    boundedConfigValue(priorPhase2SyncHits, hotSyncHits));
                                rx.p25VoiceLiveDecoder.setMaxPhase2SuperframeLocks(
                                    boundedConfigValue(priorPhase2Locks, hotSfLocks));
                            }
                            rx.p25VoiceLiveDecoder.setCqpskDiscreteFrozen(
                                p25Phase2ShouldFreezeCqpskDiscrete(rx));
                            const bool phase2HardReacquireJob =
                                rx.p25VoicePhase2 &&
                                rx.p25Phase2WideReacquireHoldWindows > 0 &&
                                !p25DiagTargetHardClear(rx.p25VoiceDiagnostics) &&
                                rx.p25VoiceDiagnostics.decodedFrames == 0 &&
                                // Hard CQPSK reset only when the Phase-2 eye is
                                // actually gone. MAC/ESS starve with live SF/mask/
                                // VCW uses invalidatePhase2StickyMaskEpoch instead.
                                rx.p25VoiceDiagnostics.phase2Bursts == 0 &&
                                rx.p25VoiceDiagnostics.phase2MaskedBursts == 0 &&
                                rx.p25VoiceDiagnostics.phase2TargetVoiceCodewords == 0;
                            if (rx.p25Phase2ForceMaskEpochRehunt) {
                                rx.p25VoiceLiveDecoder.invalidatePhase2StickyMaskEpoch();
                                rx.p25Phase2ForceMaskEpochRehunt = false;
                            }
                            if (rx.p25Phase2MaskEpochRepairHoldWindows > 0) {
                                rx.p25Phase2MaskEpochRepairHoldWindows =
                                    std::max(0, rx.p25Phase2MaskEpochRepairHoldWindows - 1);
                            }
                            if (phase2HardReacquireJob) {
                                // DEC-0014: env=1 keeps the HDQPSK stream through
                                // Costas reset. Default live stays block-channelize
                                // (105622 duty 0.685→0.095 with default-on DDC).
                                if (!p25Phase2StreamingDdcExperimentEnabled()) {
                                    rx.p25VoiceLiveDecoder.setEnableStreamingChannelDdc(false);
                                }
                                rx.p25VoiceLiveDecoder.reset();
                                if (rx.p25VoiceMaskParamsKnown) {
                                    rx.p25VoiceLiveDecoder.setPhase2MaskParameters(
                                        rx.p25VoiceNac, rx.p25VoiceWacn, rx.p25VoiceSystemId);
                                }
                                rx.p25VoiceLiveDecoder.setPhase2PreferredTdmaSlot(
                                    rx.p25VoiceTdmaSlotKnown,
                                    static_cast<uint8_t>(rx.p25VoiceTdmaSlot & 0x01u));
                                rx.p25Phase2WideReacquireHoldWindows =
                                    std::max(0, rx.p25Phase2WideReacquireHoldWindows - 1);
                            } else if (rx.p25Phase2WideReacquireHoldWindows > 0) {
                                rx.p25Phase2WideReacquireHoldWindows =
                                    std::max(0, rx.p25Phase2WideReacquireHoldWindows - 1);
                            }
                            result.audio = decodeP25VoiceAudioBlock(rx, job.iq, job.sampleRateHz,
                                job.centerFreqHz, job.targetFreqHz, job.outputRateHz,
                                job.iqStartAbsolute, job.iqStartAbsoluteKnown,
                                job.contextIqSamples);
                            rx.p25DiagCqpskHypothesisChanges = std::max(
                                rx.p25DiagCqpskHypothesisChanges,
                                rx.p25VoiceLiveDecoder.cqpskDiscreteChangesBlocked());
                            if (coldAcquireJob || streamingCqpskJob || hotPhase2TrafficJob) {
                                rx.p25VoiceLiveDecoder.setRealtimeDecodeBudgetMs(priorDecodeBudgetMs);
                                rx.p25VoiceLiveDecoder.setMaxCqpskSearchCandidates(priorCqpskCandidates);
                                rx.p25VoiceLiveDecoder.setMaxPhase2SyncHits(priorPhase2SyncHits);
                                rx.p25VoiceLiveDecoder.setMaxPhase2SuperframeLocks(priorPhase2Locks);
                            }
                            if (rx.p25IndependentTrafficSource && rx.p25VoicePhase2 &&
                                !p25Phase2StreamingDdcExperimentEnabled()) {
                                // DEC-0014: default-on streaming DDC dropped 105622
                                // duty 0.685→0.095. Keep live GUI on block-channelize
                                // unless SDR_TOWN_P25_STREAMING_DDC=1.
                                rx.p25VoiceLiveDecoder.setEnableStreamingChannelDdc(false);
                            }
                            result.hasAudioBlock = true;
                            if (std::chrono::steady_clock::now() > decodeDeadline) {
                                // Capture 20260712_024853: the only live p2bursts=1 hit was on a
                                // job that then stamped decode-wall-timeout and was hard-dropped
                                // before publishP25VoiceDiagnostics, so acquisition never advanced.
                                // Keep useful Phase-2 evidence from over-budget jobs, but do not
                                // let an opposite-slot-only eye masquerade as selected-call progress.
                                const bool keepEvidence =
                                    result.audio.phase2TargetVoiceCodewords > 0 ||
                                    result.audio.phase2TargetMaskedBursts > 0 ||
                                    p25Phase2TargetHardClearEvidence(result.audio) ||
                                    result.audio.phase2TargetMacCrcValid ||
                                    result.audio.decodedFrames > 0 ||
                                    !result.audio.audio.empty();
                                if (keepEvidence) {
                                    result.stale = false;
                                    result.staleReason = "decode-wall-overbudget-kept";
                                } else {
                                    result.stale = true;
                                    result.staleReason = "decode-wall-timeout";
                                }
                            }
                        }
                        const auto t1 = std::chrono::steady_clock::now();
                        result.dspMicros = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();

                        staleReason.clear();
                        if (result.hasAudioBlock && !result.stale && stillCurrent(&staleReason)) {
                            result.publishVoiceDiag = true;
                            const std::string rawSpeakerGateReason = p25VoiceBlockSpeakerGateReason(result.audio);
                            const bool settleMuteBypassedForValidatedVoice =
                                job.outputMutedForSettle &&
                                p25VoiceBlockMayBypassPostArmSettle(result.audio, rawSpeakerGateReason);
                            const bool effectiveSettleMute =
                                job.outputMutedForSettle && !settleMuteBypassedForValidatedVoice;
                            result.speakerGateReason = effectiveSettleMute
                                ? std::string("post-arm-settle-muted")
                                : rawSpeakerGateReason;
                            result.speakerMayEmit =
                                result.speakerGateReason == "emit" &&
                                p25VoiceBlockHasSpeakerTimelineAudio(result.audio);
                            result.audio.phase2SpeakerGateReason = result.speakerGateReason;
                            result.speakerAudio = result.speakerMayEmit ? result.audio.audio : std::vector<float>{};
                            if (!result.speakerAudio.empty()) {
                                double sum = 0.0;
                                for (float sample : result.speakerAudio) {
                                    sum += static_cast<double>(sample) * sample;
                                }
                                result.rmsDb = 20.0 * std::log10(
                                    std::sqrt(sum / static_cast<double>(result.speakerAudio.size())) + 1e-12);
                            }
                            publishResult = true;
                        } else if (result.hasAudioBlock) {
                            if (!result.stale) {
                                result.stale = true;
                                result.staleReason = staleReason.empty() ? "stale-after-decode" : staleReason;
                            }
                            publishResult = true;
                        }
                    }
                } catch (const std::exception& ex) {
                    result.error = ex.what();
                    publishResult = true;
                } catch (...) {
                    result.error = "unknown exception";
                    publishResult = true;
                }

                if (publishResult) {
                    std::unique_lock<std::mutex> lock(p25VoiceWorkerMutex);
                    // Block until the GUI DSP worker drains completed voice
                    // results.  Never evict decoded PCM after state mutation.
                    p25VoiceWorkerCv.wait(lock, [this]() {
                        return p25VoiceWorkerStop.load(std::memory_order_acquire) ||
                               (p25VoiceCompletedResults.size() +
                                p25VoicePendingPublishDepth.load(std::memory_order_acquire)) <
                                   kP25VoiceDecodeMaxCompletedResults;
                    });
                    if (p25VoiceWorkerStop.load(std::memory_order_acquire)) {
                        continue;
                    }
                    p25VoiceCompletedResults.push_back(std::move(result));
                }
                p25VoiceWorkerBusy.store(false, std::memory_order_release);
            }
            p25VoiceWorkerBusy.store(false, std::memory_order_release);
        });
    }

    void stopP25VoiceWorker()
    {
        p25VoiceWorkerStop.store(true, std::memory_order_release);
        {
            std::lock_guard<std::mutex> lock(p25VoiceWorkerMutex);
            p25VoicePendingJobs.clear();
        }
        p25VoiceWorkerCv.notify_all();
        if (p25VoiceWorkerThread.joinable()) {
            p25VoiceWorkerThread.join();
        }
        {
            std::lock_guard<std::mutex> lock(p25VoiceWorkerMutex);
            p25VoicePendingJobs.clear();
            p25VoiceCompletedResults.clear();
        }
        p25VoiceWorkerBusy.store(false, std::memory_order_release);
    }

    bool submitP25VoiceDecodeJob(P25VoiceDecodeJob job)
    {
        if (!job.rx || job.iq.empty()) return false;
        {
            std::lock_guard<std::mutex> lock(p25VoiceWorkerMutex);
            if (p25VoiceWorkerStop.load(std::memory_order_acquire) || !p25VoiceWorkerThread.joinable()) {
                return false;
            }
            const size_t publishBacklog =
                p25VoiceCompletedResults.size() +
                p25VoicePendingPublishDepth.load(std::memory_order_acquire);
            const size_t runningJobs = p25VoiceWorkerBusy.load(std::memory_order_acquire) ? 1u : 0u;
            const size_t inFlightJobs = p25VoicePendingJobs.size() + publishBacklog + runningJobs;
            if (inFlightJobs >= p25VoiceDecodeMaxPendingJobsNow(job.speakerSustainDecode)) {
                // Single-flight live Phase-2: do not advance the rolling IQ cursor
                // while a prior decode is still running or waiting to publish.  A
                // queued stale window can replay/delay a different security/slot
                // state and sounds exactly like blocky doubled speech.
                p25VoiceDroppedJobs.fetch_add(1, std::memory_order_relaxed);
                return false;
            }
            job.sequence = p25VoiceJobSequence.fetch_add(1, std::memory_order_relaxed) + 1;
            p25VoicePendingJobs.push_back(std::move(job));
        }
        p25VoiceWorkerCv.notify_one();
        return true;
    }

    bool p25VoiceWorkerCanAcceptJob()
    {
        return p25VoiceWorkerCanAcceptJobForDepth(false);
    }

    bool p25VoiceWorkerCanAcceptJobForDepth(bool speakerSustainHint)
    {
        std::lock_guard<std::mutex> lock(p25VoiceWorkerMutex);
        // Backpressure must happen before RollingIqWindow::takeUndecoded().
        // If any decode is running, queued, or waiting to publish, leave the
        // rolling decode cursor parked so the next scheduler pass still sees the
        // same unprocessed IQ instead of creating a stale out-of-order backlog.
        const size_t publishBacklog =
            p25VoiceCompletedResults.size() +
            p25VoicePendingPublishDepth.load(std::memory_order_acquire);
        const size_t runningJobs = p25VoiceWorkerBusy.load(std::memory_order_acquire) ? 1u : 0u;
        const size_t inFlightJobs = p25VoicePendingJobs.size() + publishBacklog + runningJobs;
        return !p25VoiceWorkerStop.load(std::memory_order_acquire) &&
               p25VoiceWorkerThread.joinable() &&
               inFlightJobs < p25VoiceDecodeMaxPendingJobsNow(speakerSustainHint) &&
               publishBacklog < kP25VoiceDecodeMaxCompletedResults;
    }

    P25VoiceWorkerQueueSnapshot p25VoiceWorkerQueueSnapshot()
    {
        P25VoiceWorkerQueueSnapshot snap;
        {
            std::lock_guard<std::mutex> lock(p25VoiceWorkerMutex);
            snap.pendingJobs = p25VoicePendingJobs.size();
            snap.pending = snap.pendingJobs > 0;
            snap.threadRunning = p25VoiceWorkerThread.joinable();
            snap.completedResults = p25VoiceCompletedResults.size();
        }
        snap.stopping = p25VoiceWorkerStop.load(std::memory_order_acquire);
        snap.busy = p25VoiceWorkerBusy.load(std::memory_order_acquire);
        snap.nextSequence = p25VoiceJobSequence.load(std::memory_order_relaxed) + 1;
        snap.droppedJobs = p25VoiceDroppedJobs.load(std::memory_order_relaxed);
        snap.droppedResults = p25VoiceDroppedResults.load(std::memory_order_relaxed);
        snap.publicationLockMisses = p25VoicePublicationLockMisses.load(std::memory_order_relaxed);
        snap.pendingPublishResults = p25VoicePendingPublishDepth.load(std::memory_order_acquire);
        return snap;
    }

    std::vector<P25VoiceDecodeResult> takeP25VoiceDecodeResults()
    {
        std::deque<P25VoiceDecodeResult> local;
        {
            std::lock_guard<std::mutex> lock(p25VoiceWorkerMutex);
            local.swap(p25VoiceCompletedResults);
            if (!local.empty()) {
                p25VoiceWorkerCv.notify_one();
            }
        }
        std::vector<P25VoiceDecodeResult> out;
        out.reserve(local.size());
        while (!local.empty()) {
            out.push_back(std::move(local.front()));
            local.pop_front();
        }
        return out;
    }

    struct P25VoiceDecodeWorkPurge {
        size_t pendingJobs = 0;
        size_t completedResults = 0;

        size_t total() const noexcept { return pendingJobs + completedResults; }
    };

    P25VoiceDecodeWorkPurge purgeP25VoiceDecodeWorkForSession(
        const ReceiverSessionKey& sessionKey,
        uint64_t afterSequence)
    {
        P25VoiceDecodeWorkPurge purged;
        {
            std::lock_guard<std::mutex> lock(p25VoiceWorkerMutex);
            for (auto it = p25VoicePendingJobs.begin(); it != p25VoicePendingJobs.end();) {
                if (it->receiverSessionKey == sessionKey &&
                    (afterSequence == 0 || it->sequence > afterSequence)) {
                    it = p25VoicePendingJobs.erase(it);
                    ++purged.pendingJobs;
                } else {
                    ++it;
                }
            }
            for (auto it = p25VoiceCompletedResults.begin(); it != p25VoiceCompletedResults.end();) {
                if (it->receiverSessionKey == sessionKey &&
                    (afterSequence == 0 || it->sequence > afterSequence)) {
                    it = p25VoiceCompletedResults.erase(it);
                    ++purged.completedResults;
                } else {
                    ++it;
                }
            }
        }
        if (purged.pendingJobs > 0) {
            p25VoiceDroppedJobs.fetch_add(purged.pendingJobs, std::memory_order_relaxed);
        }
        if (purged.completedResults > 0) {
            p25VoiceDroppedResults.fetch_add(purged.completedResults, std::memory_order_relaxed);
        }
        if (purged.total() > 0) {
            p25VoiceWorkerCv.notify_all();
        }
        return purged;
    }

    P25VoicePublishOutcome publishP25VoiceDecodeResult(const P25VoiceDecodeResult& result,
                                                       P25SpeakerPendingMap& pendingAudioByRx)
    {
        if (!result.rx) return P25VoicePublishOutcome::ReceiverGone;
        // After return-to-control, the traffic Receiver may have been removed from the receivers list
        // (and its storage released). Guard against publishing stale results that would dereference
        // a now-invalid Receiver* (e.g. rx.stateMutex). This eliminates a source of freezes after
        // repeated follow/return cycles.
        {
            std::unique_lock<std::mutex> lk(receiversMutex, std::try_to_lock);
            if (!lk.owns_lock()) {
                p25VoicePublicationLockMisses.fetch_add(1, std::memory_order_relaxed);
                return P25VoicePublishOutcome::Deferred;
            }
            bool stillActive = false;
            for (auto& r : receivers) {
                if (r && r.get() == result.rx.get()) { stillActive = true; break; }
            }
            if (!stillActive) {
                const uintptr_t rxKey = reinterpret_cast<uintptr_t>(result.rx.get());
                const uint64_t seqLog = result.sequence;
                const QString reason = QString::fromStdString(
                    result.staleReason.empty() ? std::string("receiver-object-gone") : result.staleReason);
                QTimer::singleShot(0, this, [this, rxKey, seqLog, reason]() {
                    appendP25LogLineKeyed(QString("p25-voice-worker-stale-gone:%1").arg(static_cast<qulonglong>(rxKey)),
                        QString("P25 voice worker stale/drop: seq=%1 reason=%2; receiver object is no longer active.")
                            .arg(static_cast<qulonglong>(seqLog))
                            .arg(reason),
                        1000);
                });
                return P25VoicePublishOutcome::ReceiverGone;
            }
        }
        Receiver& rx = *result.rx;
        if (!result.error.empty()) {
            const QString err = QString::fromStdString(result.error);
            QTimer::singleShot(0, this, [this, err]() {
                appendP25LogLineKeyed("p25-voice-worker-error",
                    QString("P25 voice worker decode error: %1").arg(err),
                    2500);
            });
            return P25VoicePublishOutcome::DiscardedStale;
        }
        bool stale = result.stale;
        bool publishVoiceDiag = result.publishVoiceDiag;
        std::string staleReason = result.staleReason;
        if (!result.hasAudioBlock && !stale) return P25VoicePublishOutcome::Published;
        if (!stale &&
            result.flushSeq != p25PendingAudioFlushSeq.load(std::memory_order_acquire)) {
            stale = true;
            staleReason = "audio-flush-sequence-stale";
        }
        {
            std::unique_lock<std::mutex> rxLock(rx.stateMutex, std::try_to_lock);
            if (!rxLock.owns_lock()) {
                p25VoicePublicationLockMisses.fetch_add(1, std::memory_order_relaxed);
                return P25VoicePublishOutcome::Deferred;
            }
            if (!rx.active || !rx.p25VoiceDecodeEnabled || !rx.p25VoicePhase2) {
                stale = true;
                if (staleReason.empty()) staleReason = "receiver-not-active-for-voice";
                p25Phase2ClearStaleResultSpeakerPending(pendingAudioByRx,
                    result.receiverSessionKey,
                    result.callSessionId,
                    rx,
                    P25PendingClearReason::RetuneOrGeneration);
            } else if (rx.p25VoiceResetPending) {
                stale = true;
                if (staleReason.empty()) staleReason = "voice-reset-pending";
                p25Phase2ClearStaleResultSpeakerPending(pendingAudioByRx,
                    result.receiverSessionKey,
                    result.callSessionId,
                    rx,
                    P25PendingClearReason::RetuneOrGeneration);
            } else if (result.callSessionId != 0 &&
                       rx.p25CurrentCallSessionId != 0 &&
                       rx.p25CurrentCallSessionId != result.callSessionId) {
                stale = true;
                if (staleReason.empty()) staleReason = "call-session-changed";
                p25Phase2ClearStaleResultSpeakerPending(pendingAudioByRx,
                    result.receiverSessionKey,
                    result.callSessionId,
                    rx,
                    P25PendingClearReason::RetuneOrGeneration);
            } else if (result.talkgroupId != 0 && rx.p25VoiceTalkgroupId != result.talkgroupId) {
                stale = true;
                if (staleReason.empty()) staleReason = "talkgroup-changed";
            } else if (result.tdmaSlotKnown &&
                       (!rx.p25VoiceTdmaSlotKnown ||
                        static_cast<uint8_t>(rx.p25VoiceTdmaSlot & 0x01u) != static_cast<uint8_t>(result.tdmaSlot & 0x01u))) {
                stale = true;
                if (staleReason.empty()) staleReason = "slot-changed";
            } else if (result.voiceFreqHz > 0.0) {
                const double liveVoiceHz = rx.p25TrafficVoiceFreqHz > 0.0 ? rx.p25TrafficVoiceFreqHz : rx.freqHz;
                if (liveVoiceHz > 0.0 && std::abs(liveVoiceHz - result.voiceFreqHz) > 50.0) {
                    stale = true;
                    if (staleReason.empty()) staleReason = "voice-frequency-changed";
                }
            }
            if (!stale && (result.trafficGeneration != 0 || rx.p25IndependentTrafficSource)) {
                const uint64_t liveGen = p25TrafficSourceGeneration.load(std::memory_order_acquire);
                if (result.trafficGeneration == 0 ||
                    rx.p25TrafficGeneration != result.trafficGeneration ||
                    liveGen != result.trafficGeneration) {
                    stale = true;
                    if (staleReason.empty()) staleReason = "traffic-generation-stale";
                }
            }
        }
        if (stale) {
            const bool keepWallTimeoutEvidence =
                result.hasAudioBlock &&
                (result.staleReason == "decode-wall-timeout" ||
                 result.staleReason == "decode-wall-overbudget-kept") &&
                (result.audio.phase2TargetVoiceCodewords > 0 ||
                 result.audio.phase2TargetMaskedBursts > 0 ||
                 p25Phase2TargetHardClearEvidence(result.audio) ||
                 result.audio.phase2TargetMacCrcValid ||
                 result.audio.decodedFrames > 0 ||
                 !result.audio.audio.empty());
            if (!keepWallTimeoutEvidence) {
                p25Phase2ClearStaleResultSpeakerPending(pendingAudioByRx,
                    result.receiverSessionKey,
                    result.callSessionId,
                    rx,
                    P25PendingClearReason::RetuneOrGeneration);
                const uintptr_t rxKey = reinterpret_cast<uintptr_t>(&rx);
                const uint64_t seqLog = result.sequence;
                const QString reason = QString::fromStdString(
                    staleReason.empty() ? std::string("stale") : staleReason);
                const uint32_t tgLog = result.talkgroupId;
                const uint32_t sourceLogValue = result.sourceId;
                const bool slotKnownLog = result.tdmaSlotKnown;
                const int slotLog = static_cast<int>(result.tdmaSlot & 0x01u);
                const qulonglong genLog = static_cast<qulonglong>(result.trafficGeneration);
                QTimer::singleShot(0, this, [this, rxKey, seqLog, reason, tgLog, sourceLogValue, slotKnownLog, slotLog, genLog]() {
                    appendP25LogLineKeyed(QString("p25-voice-worker-stale:%1").arg(static_cast<qulonglong>(rxKey)),
                        QString("P25 voice worker stale/drop: seq=%1 reason=%2 tg=%3 src=%4 slot=%5 generation=%6.")
                            .arg(static_cast<qulonglong>(seqLog))
                            .arg(reason)
                            .arg(tgLog)
                            .arg(sourceLogValue != 0 ? p25HexId(sourceLogValue, 6) : QStringLiteral("unknown"))
                            .arg(slotKnownLog ? QString::number(slotLog) : QStringLiteral("unknown"))
                            .arg(genLog),
                        1000);
                });
                return P25VoicePublishOutcome::DiscardedStale;
            }
            stale = false;
            publishVoiceDiag = true;
            QTimer::singleShot(0, this, [this, seq = result.sequence, tg = result.talkgroupId]() {
                appendP25LogLineKeyed(QString("p25-voice-worker-wall-kept:%1").arg(static_cast<qulonglong>(seq)),
                    QString("P25 voice worker kept over-budget decode evidence: seq=%1 tg=%2 (not hard-dropping Phase-2 bursts/VCW).")
                        .arg(static_cast<qulonglong>(seq))
                        .arg(tg),
                    1000);
            });
        }

        publishP25VoiceDiagnostics(rx, result.audio, publishVoiceDiag);

        AudioEngine* audioOutputEngine = result.speakerAudio.empty()
            ? engineForAudio.get()
            : ensureAudioOutputActive("P25 voice");
        size_t audioActiveOutputCount = 0;
        size_t audioQueuedSamples = 0;
        double audioRingFillPercent = 0.0;
        int audioUnderrunCount = 0;
        if (audioOutputEngine) {
            audioActiveOutputCount = audioOutputEngine->activeOutputCount();
            audioQueuedSamples = audioOutputEngine->getRingQueuedSamples();
            audioRingFillPercent = audioOutputEngine->getRingFillPercent();
            audioUnderrunCount = audioOutputEngine->getUnderrunCount();
        }
        writeP25Phase2AudioOutputTrace(rx, result.audio, "gui-p25-voice-worker",
            result.outputMutedForSettle, result.speakerMayEmit, audioOutputEngine != nullptr,
            audioActiveOutputCount, audioQueuedSamples, audioRingFillPercent, audioUnderrunCount,
            result.speakerAudio.size(), result.outputRateHz);

        gLastDspMicros.store(result.dspMicros, std::memory_order_relaxed);
        if (!result.speakerAudio.empty()) {
            gLastRmsDb.store(result.rmsDb, std::memory_order_relaxed);
        }
        if (result.speakerAudio.empty() && audioOutputEngine &&
            (result.audio.phase2VoiceCodewords == 0 ||
             result.audio.phase2TargetVoiceCodewords == 0 ||
             result.speakerGateReason != "emit")) {
            const qint64 emptyWindowNowMs = QDateTime::currentMSecsSinceEpoch();
            const bool preserveAudioBuffers =
                p25RecentSpeakerOutputActive(emptyWindowNowMs,
                    kP25Phase2SpeakerFollowHoldMs);
            const bool hasTrustedPhase2ClearContext =
                rx.p25VoicePhase2 &&
                !rx.p25VoiceEncrypted &&
                (p25Phase2BlockHasTrustedClearContext(result.audio) ||
                 (preserveAudioBuffers && p25Phase2AudioTailGraceActive(rx)));
            const bool hasActiveP25VoiceCall = rx.p25VoicePhase2
                ? hasTrustedPhase2ClearContext
                : (!rx.p25VoiceEncrypted && (rx.p25VoiceClearKnown || preserveAudioBuffers));
            const bool preserveLivePlayback =
                hasActiveP25VoiceCall ||
                p25Phase2ShouldPreserveLivePlaybackBuffers(rx, emptyWindowNowMs) ||
                (audioOutputEngine->getRingQueuedSamples() > 0) ||
                !p25SpeakerPendingFor(pendingAudioByRx, rx).samples.empty();
            if (!preserveLivePlayback) {
                p25Phase2ClearSpeakerPendingQueue(rx,
                    p25SpeakerPendingFor(pendingAudioByRx, rx),
                    P25PendingClearReason::UserStop);
                // Only flush when playback is truly idle. Clearing a non-empty
                // ring here races just-pushed clear PCM and the silence bridge
                // (20260720_080118 total live silence / offline WAV PASS).
                audioOutputEngine->clearBuffers();
            } else {
                const auto currentWorkerSessionActive = [&rx](const ReceiverSessionKey& key) {
                    return key.receiver == &rx &&
                        rx.p25TrafficSessionGeneration.load(std::memory_order_acquire) == key.generation;
                };
                size_t realTopUpPushed = 0;
                size_t bridgeTopUpPushed = 0;
                const size_t topUpPushed =
                    p25TopUpSpeakerPlaybackRing(audioOutputEngine,
                                                pendingAudioByRx,
                                                currentWorkerSessionActive,
                                                &realTopUpPushed,
                                                &bridgeTopUpPushed);
                if (realTopUpPushed > 0) {
                    gP25AudioLastSpeakerOutputMs.store(emptyWindowNowMs, std::memory_order_relaxed);
                    guiP25AudioLastOutputMs.store(emptyWindowNowMs, std::memory_order_relaxed);
                    guiP25AudioOutputEvents.fetch_add(1, std::memory_order_relaxed);
                    guiP25AudioOutputSamples.fetch_add(static_cast<long long>(realTopUpPushed), std::memory_order_relaxed);
                    p25AutoFollowLastActiveMs = std::max(p25AutoFollowLastActiveMs, emptyWindowNowMs);
                }
                if (topUpPushed > 0) {
                    const uint32_t tgLog = result.audio.talkgroupId != 0
                        ? result.audio.talkgroupId
                        : rx.p25VoiceTalkgroupId;
                    const size_t queuedAfter = audioOutputEngine->getRingQueuedSamples();
                    const double fillAfter = audioOutputEngine->getRingFillPercent();
                    const int underrunsAfter = audioOutputEngine->getUnderrunCount();
                    QTimer::singleShot(0, this, [this, tgLog, realTopUpPushed, bridgeTopUpPushed,
                                                  queuedAfter, fillAfter, underrunsAfter]() {
                        appendP25LogLineKeyed(QString("p25-audio-top-up:%1").arg(tgLog),
                            QString("P25 audio top-up: TG=%1 real=%2 bridge=%3 ringQueued=%4 ringFill=%5% underruns=%6.")
                                .arg(tgLog)
                                .arg(static_cast<qulonglong>(realTopUpPushed))
                                .arg(static_cast<qulonglong>(bridgeTopUpPushed))
                                .arg(static_cast<qulonglong>(queuedAfter))
                                .arg(fillAfter, 0, 'f', 2)
                                .arg(underrunsAfter),
                            150);
                    });
                }
            }
            // During established clear Phase-2 call, preserve pending/ring to keep audio joined across
            // decode windows that happen to contribute 0 new VCW in this slice (overlap/cadence timing).
        }

        const uintptr_t rxKey = reinterpret_cast<uintptr_t>(&rx);
        const long long absStartLog = result.iqStartAbsoluteKnown ? static_cast<long long>(result.iqStartAbsolute) : -1LL;
        const size_t iqLog = result.iqSamples;
        const size_t freshLog = result.freshIqSamples;
        const size_t contextLog = result.contextIqSamples;
        const bool rollingLog = result.rollingDecode;
        const double cfLog = result.centerFreqHz;
        const double srLog = result.sampleRateHz;
        const double targetLog = result.audio.effectiveTargetFreqHz > 0.0
            ? result.audio.effectiveTargetFreqHz
            : result.targetFreqHz;
        const QString diagLog = p25VoiceDiagLabel(result.audio.diag);
        const QString backendLog = result.audio.backendAvailable ? QStringLiteral("yes") : QStringLiteral("no");
        const QString essLog = result.audio.phase2EssKnown
            ? (result.audio.phase2EssEncrypted ? QStringLiteral("enc") : QStringLiteral("clear"))
            : QStringLiteral("unknown");
        const QString gateLog = QString::fromStdString(result.speakerGateReason);
        const QString acchLog = QString("p2acch=nom:%1 altKind:%2 swap:%3 slip:%4 inv:%5")
            .arg(static_cast<qulonglong>(result.audio.phase2MacNominalCrcValid))
            .arg(static_cast<qulonglong>(result.audio.phase2MacAltKindCrcValid))
            .arg(static_cast<qulonglong>(result.audio.phase2MacBitSwapCrcValid))
            .arg(static_cast<qulonglong>(result.audio.phase2MacSlipCrcValid))
            .arg(static_cast<qulonglong>(result.audio.phase2MacInvertCrcValid));
        const qulonglong syncsLog = static_cast<qulonglong>(result.audio.syncs);
        const qulonglong nidsLog = static_cast<qulonglong>(result.audio.nids);
        const qulonglong decodedLog = static_cast<qulonglong>(result.audio.decodedFrames);
        const qulonglong audioSamplesLog = static_cast<qulonglong>(result.audio.audio.size());
        const qulonglong speakerSamplesLog = static_cast<qulonglong>(result.speakerAudio.size());
        const qulonglong burstsLog = static_cast<qulonglong>(result.audio.phase2Bursts);
        const qulonglong vcwLog = static_cast<qulonglong>(result.audio.phase2VoiceCodewords);
        const qulonglong targetVcwLog = static_cast<qulonglong>(result.audio.phase2TargetVoiceCodewords);
        const qulonglong oppVcwLog = static_cast<qulonglong>(result.audio.phase2OppositeVoiceCodewords);
        const qulonglong ambeAttemptsLog = static_cast<qulonglong>(result.audio.phase2AmbeDecodeAttempts);
        const qulonglong ambeAcceptedLog = static_cast<qulonglong>(result.audio.phase2AmbeAcceptedFrames);
        const qulonglong rejectedVcwLog = static_cast<qulonglong>(result.audio.phase2RejectedVoiceCodewords);
        const qulonglong wrongSlotVcwLog = static_cast<qulonglong>(result.audio.phase2WrongSlotVoiceCodewords);
        const qulonglong duplicateVcwLog = static_cast<qulonglong>(result.audio.phase2DuplicateSuppressedVoiceCodewords);
        const qulonglong absDuplicateVcwLog = static_cast<qulonglong>(result.audio.phase2AbsoluteDuplicateSuppressedVoiceCodewords);
        const qulonglong seqSuppressVcwLog = static_cast<qulonglong>(result.audio.phase2SequencerSuppressedVoiceCodewords);
        const qulonglong sfLog = static_cast<qulonglong>(result.audio.phase2SuperframeBursts);
        const qulonglong maskLog = static_cast<qulonglong>(result.audio.phase2MaskedBursts);
        const qulonglong macValidLog = static_cast<qulonglong>(result.audio.phase2MacCrcValid);
        const qulonglong macTotalLog = static_cast<qulonglong>(result.audio.phase2MacPdus);
        const qulonglong droppedJobsLog = static_cast<qulonglong>(p25VoiceDroppedJobs.load(std::memory_order_relaxed));
        const qulonglong droppedResultsLog = static_cast<qulonglong>(p25VoiceDroppedResults.load(std::memory_order_relaxed));
        const long long dspMicrosLog = result.dspMicros;
        const qulonglong expVcwLogW = static_cast<qulonglong>(result.audio.phase2ExpectedVoiceCodewords);
        const qulonglong fedLogW = static_cast<qulonglong>(result.audio.phase2FedToMbelib);
        const qulonglong emitLogW = static_cast<qulonglong>(result.audio.phase2EmittedPcmFrames);
        const qulonglong gapsLogW = static_cast<qulonglong>(result.audio.phase2FeedGaps);
        const qulonglong lastAbsLogW = static_cast<qulonglong>(result.audio.phase2LastFedAbsDibit);
        const qulonglong contextVcwLogW = static_cast<qulonglong>(result.audio.phase2ContextVoiceCodewords);
        const qulonglong contextDropLogW = static_cast<qulonglong>(result.audio.phase2ContextSuppressedVoiceCodewords);
        const qulonglong pendingQueuedLogW = static_cast<qulonglong>(result.audio.phase2PendingAmbeFramesQueued);
        QTimer::singleShot(0, this, [this, rxKey, absStartLog, iqLog, freshLog, contextLog, rollingLog,
                                      cfLog, targetLog, srLog, diagLog, backendLog, gateLog, syncsLog,
                                      nidsLog, decodedLog, audioSamplesLog, speakerSamplesLog, burstsLog,
                                      vcwLog, targetVcwLog, oppVcwLog, ambeAttemptsLog, ambeAcceptedLog,
                                      rejectedVcwLog, wrongSlotVcwLog, duplicateVcwLog,
                                      absDuplicateVcwLog, seqSuppressVcwLog,
                                      sfLog, maskLog, macValidLog, macTotalLog, acchLog, essLog,
                                      droppedJobsLog, droppedResultsLog, dspMicrosLog,
                                      expVcwLogW, fedLogW, emitLogW, gapsLogW, lastAbsLogW,
                                      contextVcwLogW, contextDropLogW, pendingQueuedLogW]() {
            const QString key = QString("p25-dsp-voice-worker:%1").arg(static_cast<qulonglong>(rxKey));
            const QString line = QString("P25 DSP VOICE WORKER: rolling=%1 iq=%2 fresh=%3 context=%4 absStart=%5 sr=%6MHz cf=%7MHz target=%8MHz diag=%9 gate=%10 backend=%11 sync=%12 nid=%13 decoded=%14 audio=%15 speaker=%16 p2bursts=%17 p2vcw=%18 targetVcw=%19 oppVcw=%20 expVcw=%21 fed=%22 emitPcm=%23 gaps=%24 ctxVcw=%25 ctxDrop=%26 pendingQueued=%27 reject=%28 wrongSlot=%29 dup=%30 absDup=%31 seqDrop=%32 lastAbs=%33 p2sf=%34 p2mask=%35 p2mac=%36/%37 %38 ess=%39 dsp=%40us qDrop=%41 rDrop=%42")
                .arg(rollingLog ? "yes" : "no")
                .arg(static_cast<qulonglong>(iqLog))
                .arg(static_cast<qulonglong>(freshLog))
                .arg(static_cast<qulonglong>(contextLog))
                .arg(absStartLog)
                .arg(srLog / 1e6, 0, 'f', 3)
                .arg(cfLog / 1e6, 0, 'f', 5)
                .arg(targetLog / 1e6, 0, 'f', 5)
                .arg(diagLog)
                .arg(gateLog)
                .arg(backendLog)
                .arg(syncsLog)
                .arg(nidsLog)
                .arg(decodedLog)
                .arg(audioSamplesLog)
                .arg(speakerSamplesLog)
                .arg(burstsLog)
                .arg(vcwLog)
                .arg(targetVcwLog)
                .arg(oppVcwLog)
                .arg(expVcwLogW)
                .arg(fedLogW)
                .arg(emitLogW)
                .arg(gapsLogW)
                .arg(contextVcwLogW)
                .arg(contextDropLogW)
                .arg(pendingQueuedLogW)
                .arg(rejectedVcwLog)
                .arg(wrongSlotVcwLog)
                .arg(duplicateVcwLog)
                .arg(absDuplicateVcwLog)
                .arg(seqSuppressVcwLog)
                .arg(lastAbsLogW)
                .arg(sfLog)
                .arg(maskLog)
                .arg(macValidLog)
                .arg(macTotalLog)
                .arg(acchLog)
                .arg(essLog)
                .arg(dspMicrosLog)
                .arg(droppedJobsLog)
                .arg(droppedResultsLog);
            appendP25LogLineKeyed(key, line, 750);
        });

        if (result.speakerMayEmit && !result.speakerAudio.empty() &&
            result.audio.phase2EmittedPcmFrames > 0) {
            if (!audioOutputEngine) audioOutputEngine = ensureAudioOutputActive("decoded audio");
            if (audioOutputEngine && audioOutputEngine->activeOutputCount() > 0) {
                const double curSnrForAudio = gLastSnrDb.load(std::memory_order_relaxed);
                const bool hasCarrierForAudio = (curSnrForAudio > 2.0) ||
                    (gLastRmsDb.load(std::memory_order_relaxed) > gLastNoiseFloorDb.load(std::memory_order_relaxed) + 4.0);
                const bool carrierOk = hasCarrierForAudio ||
                    (result.audio.phase2FedToMbelib > 0 && result.audio.phase2TargetVoiceCodewords > 0) ||
                    (result.audio.phase2EmittedPcmFrames > 0 &&
                     p25VoiceBlockHasSpeakerTimelineAudio(result.audio));
                size_t pushedSamples = 0;
                const bool gateEmit =
                    result.speakerGateReason == "emit" &&
                    p25VoiceBlockMayEmitAudio(result.audio) &&
                    !result.audio.phase2StaleAudioTail;
                auto& pendingSpeaker = p25SpeakerPendingFor(pendingAudioByRx, rx);
                p25Phase2BindSpeakerPendingToCall(pendingSpeaker, rx);
                const bool hasNewPcm =
                    p25VoiceBlockHasSpeakerTimelineAudio(result.audio) &&
                    result.audio.phase2EmittedPcmFrames > 0;
                const bool phase2SpeakerSessionReady =
                    !rx.p25VoicePhase2 ||
                    (result.callSessionId != 0 &&
                     rx.p25CurrentCallSessionId != 0 &&
                     result.callSessionId == rx.p25CurrentCallSessionId &&
                     rx.p25PttGeneration != 0 &&
                     rx.p25VoiceGrantEpochMs > 0);
                if (!phase2SpeakerSessionReady) {
                    const quint32 tgLog = result.audio.talkgroupId != 0
                        ? result.audio.talkgroupId
                        : rx.p25VoiceTalkgroupId;
                    const qulonglong resultCallLog = static_cast<qulonglong>(result.callSessionId);
                    const qulonglong rxCallLog = static_cast<qulonglong>(rx.p25CurrentCallSessionId);
                    const qulonglong pttLog = static_cast<qulonglong>(rx.p25PttGeneration);
                    const qlonglong grantEpochLog = static_cast<qlonglong>(rx.p25VoiceGrantEpochMs);
                    QTimer::singleShot(0, this, [this, tgLog, resultCallLog, rxCallLog, pttLog, grantEpochLog]() {
                        appendP25LogLineKeyed(QString("p25-audio-session-block:%1").arg(tgLog),
                            QString("P25 audio blocked: TG=%1 missing or stale Phase 2 call session resultCall=%2 rxCall=%3 pttGen=%4 grantEpoch=%5.")
                                .arg(tgLog)
                                .arg(resultCallLog)
                                .arg(rxCallLog)
                                .arg(pttLog)
                                .arg(grantEpochLog),
                            500);
                    });
                }
                std::vector<float> pushedRealAudio;
                std::vector<float> speakerAudioForQueue;
                const std::vector<float>* speakerAudioToQueue = &result.speakerAudio;
                if (rx.p25VoicePhase2 && hasNewPcm) {
                    const double outRate = audioOutputEngine
                        ? std::max(8000.0, static_cast<double>(audioOutputEngine->getSampleRate()))
                        : std::max(8000.0, result.outputRateHz);
                    const size_t phase2FrameSamples = std::max<size_t>(160,
                        static_cast<size_t>(outRate * 0.020 + 0.5));
                    speakerAudioForQueue = p25Phase2SpeakerAudioForQueue(
                        pendingSpeaker, result.audio, result.speakerAudio, phase2FrameSamples);
                    speakerAudioToQueue = &speakerAudioForQueue;
                }
                const bool hasPlayableNewPcm = !speakerAudioToQueue->empty();
                if (phase2SpeakerSessionReady && carrierOk && gateEmit &&
                    (hasPlayableNewPcm || !pendingSpeaker.samples.empty())) {
                    pushedSamples = pushP25SpeakerAudio(audioOutputEngine,
                        pendingSpeaker.samples,
                        *speakerAudioToQueue,
                        result.audioOutputIndices,
                        audioRingFillPercent,
                        !hasPlayableNewPcm,
                        &pushedRealAudio);
                }
                // Never clear accepted speaker PCM on transient gate failure.
                if (pushedSamples > 0) {
                p25Phase2ResetPlayoutBridge(rx);
                const P25P2CallAudioKey speakerKey =
                    p25CurrentPhase2AudioKey(rx, result.audio.effectiveTargetFreqHz);
                const bool bridgeAnchor =
                    p25Phase2CleanPlayoutBridgeAnchorWindow(result.audio);
                p25Phase2RememberLastEmittedSample(
                    rx, speakerKey, pushedRealAudio.empty() ? result.speakerAudio : pushedRealAudio,
                    bridgeAnchor);
                // Tap clear speaker PCM for Decode Log STT (async; never blocks DSP).
                {
                    const int sttRate = static_cast<int>(
                        std::lround(std::max(8000.0,
                            static_cast<double>(audioOutputEngine->getSampleRate()))));
                    const double sttFreq = p25TranscriptVoiceLabelHz(
                        rx, result.audio.effectiveTargetFreqHz, result.targetFreqHz);
                    const int sttSlot = result.tdmaSlotKnown
                        ? static_cast<int>(result.tdmaSlot & 0x01u)
                        : (rx.p25VoiceTdmaSlotKnown
                               ? static_cast<int>(rx.p25VoiceTdmaSlot & 0x01u)
                               : -1);
                    const std::vector<float>& transcriptAudio =
                        pushedRealAudio.empty() ? result.speakerAudio : pushedRealAudio;
                    p25TranscriptTapSpeakerPcm(transcriptAudio.data(), transcriptAudio.size(),
                        sttRate, result.audio.talkgroupId, sttFreq, sttSlot,
                        result.audio.effectiveTargetFreqHz);
                }
                const quint32 tgLog = result.audio.talkgroupId;
                const double targetFreqLog = result.audio.effectiveTargetFreqHz > 0.0
                    ? result.audio.effectiveTargetFreqHz
                    : (result.targetFreqHz > 0.0 ? result.targetFreqHz : rx.freqHz);
                const QString slotLog = result.tdmaSlotKnown
                    ? QString::number(result.tdmaSlot & 0x01u)
                    : (rx.p25VoiceTdmaSlotKnown
                           ? QString::number(rx.p25VoiceTdmaSlot & 0x01u)
                           : QStringLiteral("unknown"));
                const qulonglong generationLog = static_cast<qulonglong>(result.trafficGeneration);
                const qulonglong seqLog = static_cast<qulonglong>(result.sequence);
                const size_t activeCount = audioOutputEngine->activeOutputCount();
                const size_t queuedAfter = audioOutputEngine->getRingQueuedSamples();
                const double fillAfter = audioOutputEngine->getRingFillPercent();
                const int underrunsAfter = audioOutputEngine->getUnderrunCount();
                const QString gateLog = QString::fromStdString(result.speakerGateReason);
                const QString essLog = result.audio.phase2EssKnown
                    ? (result.audio.phase2EssEncrypted ? QStringLiteral("enc") : QStringLiteral("clear"))
                    : QStringLiteral("unknown");
                const QString actionLog = QString::fromStdString(result.audio.phase2SecurityGateAction);
                const QString targetEssLog = result.audio.phase2TargetEssKnown
                    ? (result.audio.phase2TargetEssEncrypted ? QStringLiteral("enc") : QStringLiteral("clear"))
                    : QStringLiteral("unknown");
                const QString targetSessionLog = result.audio.phase2TargetSessionAudioRelease
                    ? QStringLiteral("yes")
                    : QStringLiteral("no");
                const QString targetPttLog = result.audio.phase2TargetSecurityStateFromPtt
                    ? QStringLiteral("yes")
                    : QStringLiteral("no");
                const QString sourceLog = rx.p25VoiceSourceId != 0
                    ? p25HexId(rx.p25VoiceSourceId, 6)
                    : QStringLiteral("unknown");
                const qulonglong callSessionLog = static_cast<qulonglong>(
                    result.callSessionId != 0 ? result.callSessionId : rx.p25CurrentCallSessionId);
                const qlonglong grantEpochLog = static_cast<qlonglong>(rx.p25VoiceGrantEpochMs);
                const qulonglong pttGenerationLog = static_cast<qulonglong>(rx.p25PttGeneration);
                const qulonglong decodedLog = static_cast<qulonglong>(result.audio.decodedFrames);
                const qulonglong targetVcwLog = static_cast<qulonglong>(result.audio.phase2TargetVoiceCodewords);
                const qulonglong oppVcwLog = static_cast<qulonglong>(result.audio.phase2OppositeVoiceCodewords);
                const qulonglong fedLog = static_cast<qulonglong>(result.audio.phase2FedToMbelib);
                const qulonglong emitLog = static_cast<qulonglong>(result.audio.phase2EmittedPcmFrames);
                const qulonglong gapLog = static_cast<qulonglong>(result.audio.phase2FeedGaps);
                const qulonglong rejectLog = static_cast<qulonglong>(result.audio.phase2RejectedVoiceCodewords);
                const qulonglong wrongSlotLog = static_cast<qulonglong>(result.audio.phase2WrongSlotVoiceCodewords);
                const qulonglong duplicateLog = static_cast<qulonglong>(result.audio.phase2DuplicateSuppressedVoiceCodewords);
                const qulonglong absDuplicateLog = static_cast<qulonglong>(result.audio.phase2AbsoluteDuplicateSuppressedVoiceCodewords);
                const qulonglong seqSuppressLog = static_cast<qulonglong>(result.audio.phase2SequencerSuppressedVoiceCodewords);
                const qulonglong contextVcwLog = static_cast<qulonglong>(result.audio.phase2ContextVoiceCodewords);
                const qulonglong contextDropLog = static_cast<qulonglong>(result.audio.phase2ContextSuppressedVoiceCodewords);
                const qulonglong pendingQueuedLog = static_cast<qulonglong>(result.audio.phase2PendingAmbeFramesQueued);
                const qulonglong pendingReleaseLog = static_cast<qulonglong>(result.audio.phase2PendingAmbeFramesReleased);
                const qulonglong macValidLog = static_cast<qulonglong>(result.audio.phase2MacCrcValid);
                const qulonglong macTotalLog = static_cast<qulonglong>(result.audio.phase2MacPdus);
                const qulonglong probeAcceptedLog = static_cast<qulonglong>(result.audio.phase2DiagnosticAmbeProbeAccepted);
                const qulonglong probeAttemptsLog = static_cast<qulonglong>(result.audio.phase2DiagnosticAmbeProbeAttempts);
                guiP25AudioOutputEvents.fetch_add(1, std::memory_order_relaxed);
                guiP25AudioOutputSamples.fetch_add(static_cast<long long>(pushedSamples), std::memory_order_relaxed);
                guiP25AudioDecodedFrames.fetch_add(static_cast<long long>(result.audio.decodedFrames), std::memory_order_relaxed);
                guiP25AudioAcceptedAmbeFrames.fetch_add(static_cast<long long>(result.audio.phase2AmbeAcceptedFrames), std::memory_order_relaxed);
                const qint64 speakerNowMs = QDateTime::currentMSecsSinceEpoch();
                guiP25AudioLastOutputMs.store(speakerNowMs, std::memory_order_relaxed);
                // Always refresh speaker/sustain on a real ring push so empty-window
                // preserve + silence bridge see continuity. Follow-hold activity alone
                // stays gated (stale encrypted/wrong-slot must not extend tune hold).
                gP25AudioLastSpeakerOutputMs.store(speakerNowMs, std::memory_order_relaxed);
                p25Phase2UpdateSessionSustainState(rx, result.audio, speakerNowMs, true);
                if (p25Phase2SpeakerOutputCanRefreshFollowActivity(result.audio)) {
                    p25AutoFollowLastActiveMs = std::max(p25AutoFollowLastActiveMs, speakerNowMs);
                }
                QTimer::singleShot(0, this, [this, tgLog, targetFreqLog, slotLog, generationLog, seqLog,
                                             pushedSamples, activeCount, queuedAfter, fillAfter, underrunsAfter,
                                             gateLog, essLog, actionLog, targetEssLog, targetSessionLog, targetPttLog,
                                             sourceLog, callSessionLog, grantEpochLog, pttGenerationLog,
                                              decodedLog, targetVcwLog, oppVcwLog,
                                              fedLog, emitLog, gapLog, rejectLog, wrongSlotLog,
                                               duplicateLog, absDuplicateLog, seqSuppressLog,
                                               contextVcwLog, contextDropLog, pendingQueuedLog, pendingReleaseLog,
                                               macValidLog, macTotalLog, probeAcceptedLog, probeAttemptsLog]() {
                    appendP25LogLineKeyed(QString("p25-audio-output:%1").arg(tgLog),
                        QString("P25 audio output: TG=%1 target=%2MHz slot=%3 gen=%4 seq=%5 pushed=%6 samples gate=%7 decoded=%8 targetVcw=%9 oppVcw=%10 fed=%11 emitPcm=%12 gaps=%13 ctxVcw=%14 ctxDrop=%15 reject=%16 wrongSlot=%17 dup=%18 absDup=%19 seqDrop=%20 pendingQueued=%21 pendingRel=%22 p2mac=%23/%24 probe=%25/%26 ess=%27 targetEss=%28 targetSession=%29 targetPtt=%30 action=%31 activeOutputs=%32 ringQueued=%33 ringFill=%34% underruns=%35 src=%36 call=%37 grantEpoch=%38 pttGen=%39.")
                            .arg(tgLog)
                            .arg(targetFreqLog / 1e6, 0, 'f', 5)
                            .arg(slotLog)
                            .arg(generationLog)
                            .arg(seqLog)
                            .arg(static_cast<qulonglong>(pushedSamples))
                            .arg(gateLog)
                            .arg(decodedLog)
                            .arg(targetVcwLog)
                            .arg(oppVcwLog)
                            .arg(fedLog)
                            .arg(emitLog)
                            .arg(gapLog)
                            .arg(contextVcwLog)
                            .arg(contextDropLog)
                            .arg(rejectLog)
                            .arg(wrongSlotLog)
                            .arg(duplicateLog)
                            .arg(absDuplicateLog)
                            .arg(seqSuppressLog)
                            .arg(pendingQueuedLog)
                            .arg(pendingReleaseLog)
                            .arg(macValidLog)
                            .arg(macTotalLog)
                            .arg(probeAcceptedLog)
                            .arg(probeAttemptsLog)
                            .arg(essLog)
                            .arg(targetEssLog)
                            .arg(targetSessionLog)
                            .arg(targetPttLog)
                            .arg(actionLog)
                            .arg(static_cast<qulonglong>(activeCount))
                            .arg(static_cast<qulonglong>(queuedAfter))
                            .arg(fillAfter, 0, 'f', 2)
                            .arg(underrunsAfter)
                            .arg(sourceLog)
                            .arg(callSessionLog)
                            .arg(grantEpochLog)
                            .arg(pttGenerationLog),
                        150);
                });
                }
            } else {
                const quint32 tgLog = result.audio.talkgroupId;
                const size_t samplesLog = result.speakerAudio.size();
                QTimer::singleShot(0, this, [this, tgLog, samplesLog]() {
                    appendP25LogLineKeyed(QString("p25-audio-output-missing:%1").arg(tgLog),
                        QString("P25 audio output blocked: TG=%1 decoded=%2 samples but no active playback output.")
                            .arg(tgLog)
                            .arg(static_cast<qulonglong>(samplesLog)),
                        1000);
                });
            }
        } else if (result.hasAudioBlock &&
                   result.speakerGateReason != "emit" &&
                   rx.p25SessionState.callSecurityLatch == P25CallSecurityLatch::Encrypted &&
                   !p25Phase2SessionSpeakerSustainActive(rx)) {
            p25Phase2ClearSpeakerPlaybackQueue(rx,
                p25SpeakerPendingFor(pendingAudioByRx, rx),
                P25PendingClearReason::EncryptedState);
        }
        return P25VoicePublishOutcome::Published;
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
    bool monitorAudioLpfEnabled = true;
    double monitorSquelchDb = -105;
    double monitorRfGainDb = 20.0;
    double monitorMasterVolume = 0.85;
    // Last Apply selection from Configure Output Devices (full device names).
    // ensureAudioOutputActive restores these instead of blindly opening "default",
    // which fought the dialog after re-enumerate (log 20260811 10:36 outputs→None).
    std::vector<std::string> preferredAudioOutputNames;
    double monitorGain = 1.0; // audio gain, not RF gain
    double monitorWfmDeTauUs = 75.0;
    double monitorWfmPilotNotchR = 0.96;
    double monitorChannelBwHz = 180000.0; // AUTO/WFM default; NFM snaps to 12.5 kHz for modern CB/PMR
    double p25MonitoredControlFreqHz = 0.0;
    bool p25AutoFollowEnabled = false;
    bool p25IndependentTrafficEnabled = true;
    bool p25IndependentTrafficActive = false;
    // True only for the one-RTL traffic-source mode where the physical tuner
    // has been retuned away from the control channel.  In this mode return to
    // control must retune RF before the UI/log can claim CC monitoring.
    bool p25IndependentTrafficRetunedPrimary = false;
    // Increments every time a P25 traffic-channel source is created.  The DSP
    // worker snapshots shared_ptrs, so an old traffic receiver can still exist
    // briefly after a newer grant replaces it.  The generation check prevents
    // stale sources from decoding/logging/audio after replacement.
    std::atomic<uint64_t> p25TrafficSourceGeneration{0};
    std::atomic<uint64_t> p25PendingAudioFlushSeq{0};
    bool p25FollowEnabled = false;
    bool p25FollowAutoActive = false;
    uint32_t p25FollowTalkgroupId = 0;
    double p25AutoFollowReturnControlFreqHz = 0.0;
    double p25AutoFollowVoiceFreqHz = 0.0;
    qint64 p25AutoFollowTunedAtMs = 0;
    qint64 p25AutoFollowLastGrantMs = 0;
    qint64 p25AutoFollowLastActiveMs = 0;
    qint64 p25AutoFollowLastMHzHopMs = 0;
    qint64 p25AutoFollowLastReturnMs = 0;
    double p25AutoFollowLastReturnVoiceHz = 0.0;
    qint64 p25AutoFollowWarmStandbyUntilMs = 0;
    double p25AutoFollowWarmStandbyVoiceHz = 0.0;
    qint64 p25LastSameRfMetadataSwitchMs = 0;
    qint64 p25SameRfClearGrantHoldUntilMs = 0;
    P25LiveDecoder p25LiveDecoder;
    // Stage 4: keep heavy P25 control-channel decode off the Qt GUI timer.
    // The worker owns this decoder instance; GUI thread only consumes completed results.
    P25LiveDecoder p25ControlWorkerDecoder{p25RealtimeControlDecoderConfig()};
    std::mutex p25ControlWorkerDecoderMutex;
    std::mutex p25ControlPendingMutex;
    std::optional<P25LiveDecodeResult> p25ControlPendingResult;
    std::atomic<bool> p25ControlWorkerBusy{false};
    std::atomic<long long> p25ControlDroppedResults{0};
    std::atomic<bool> p25ControlWorkerResetPending{false};
    P25ControlChannelAnalyzer p25LiveControlAnalyzer;
    std::vector<P25PendingVoiceGrant> p25PendingVoiceGrants;
    std::vector<P25RepeatedVoiceGrant> p25RepeatedVoiceGrants;
    QDialog* p25LogDialog = nullptr;
    QTextEdit* p25LogText = nullptr;
    QDialog* iqReplayDialog = nullptr;
    QCheckBox* p25AutoFollowCheckBox = nullptr;
    QCheckBox* p25IndependentTrafficCheckBox = nullptr;
    QLabel* p25StatusLabel = nullptr;
    // Sprint 0–2 P25 clear TX shell (mic capture; no RF encode yet).
    P25TxSnapshot p25TxSnapshot{};
    AudioCapture p25TxMic;
    QCheckBox* p25TxArmCheckBox = nullptr;
    QPushButton* p25TxPttButton = nullptr;
    QLabel* p25TxStatusLabel = nullptr;
    QSpinBox* p25TxRidSpinBox = nullptr;
    QSpinBox* p25TxTgSpinBox = nullptr;
    QSpinBox* p25TxNacSpinBox = nullptr;
    QSpinBox* p25TxDevSpinBox = nullptr;
    QComboBox* p25TxMicComboBox = nullptr;
    QProgressBar* p25TxMicMeterBar = nullptr;
    QCheckBox* p25TxMuteSpkCheckBox = nullptr;
    QTimer* p25TxMicMeterTimer = nullptr;
    // Sprint 3: mic → 8 kHz → AMBE placeholder packetizer while PTT held.
    std::unique_ptr<P25TxVoicePacketizer> p25TxPacketizer;
    std::vector<P25AmbeEncodedFrame> p25TxAmbeSession;
    qint64 p25TxLastEncodeLogMs = 0;
    QTimer* diagnosticsHeartbeatTimer = nullptr;
    QTimer* diagnosticsResourceTimer = nullptr;
    qint64 diagnosticsLastHeartbeatMs = 0;
    qint64 diagnosticsLastUiStallReportMs = 0;
    qint64 diagnosticsLastResourceReportMs = 0;
    QStringList p25LogLines;
    QStringList p25VisibleLogPending;
    bool p25VisibleLogFlushQueued = false;
    LiveIqCaptureSession liveIqCapture;
    std::atomic<bool> liveIqCaptureLogActive{false};
    QStringList liveIqCapturePendingP25Lines;
    QTimer* liveIqCaptureTimer = nullptr;
    std::thread liveIqCaptureWriterThread;
    std::atomic<bool> stopLiveIqCaptureWriter{false};
    QString p25LastDiagSignature;
    qint64 p25LastDiagLogMs = 0;
    std::map<std::string, qint64> p25LogThrottleByKey;
    QDoubleSpinBox* bwSpin = nullptr;
    QDoubleSpinBox* lpfSpin = nullptr;
    QCheckBox* lpfEnableCheck = nullptr;
    WaterfallRoiBuilder classifierRoiBuilder{128};
    // spectrumWidget kept for future if needed

    void appendP25LogLine(const QString& text) {
        const QDateTime nowLocal = QDateTime::currentDateTime();
        const QDateTime nowUtc = nowLocal.toUTC();
        const QString line = QString("[%1 | %2 UTC] %3")
            .arg(nowLocal.toString("HH:mm:ss.zzz"))
            .arg(nowUtc.toString(Qt::ISODateWithMs))
            .arg(text);
        spdlog::debug("[P25] {}", text.toStdString());
        p25LogLines << line;
        while (p25LogLines.size() > 1500) p25LogLines.removeFirst();
        static qint64 lastRemoteP25LogMs = 0;
        const qint64 nowMs = nowLocal.toMSecsSinceEpoch();
        const bool importantRemoteP25Line =
            text.contains(QStringLiteral("error"), Qt::CaseInsensitive) ||
            text.contains(QStringLiteral("exception"), Qt::CaseInsensitive) ||
            text.contains(QStringLiteral("watchdog"), Qt::CaseInsensitive) ||
            text.contains(QStringLiteral("audio output"), Qt::CaseInsensitive) ||
            text.contains(QStringLiteral("voice-decode-disabled"), Qt::CaseInsensitive);
        const qint64 remoteP25MinIntervalMs = importantRemoteP25Line ? 2500 : 10000;
        if (remoteDiagnosticsEnabled() &&
            (lastRemoteP25LogMs <= 0 || nowMs - lastRemoteP25LogMs >= remoteP25MinIntervalMs)) {
            lastRemoteP25LogMs = nowMs;
            QJsonObject payload;
            payload["line"] = text.left(1200);
            payload["receiverCount"] = boundedJsonInt(receivers.size());
            payload["liveIqCaptureActive"] = liveIqCaptureLogActive.load(std::memory_order_acquire);
            remoteDiagnosticsSubmit("p25.log", "debug", payload);
        }
        if (liveIqCaptureLogActive.load(std::memory_order_acquire)) {
            // Queue for capture without touching LiveIqCaptureSession from the
            // GUI/logging path. The writer owns the session/file streams and
            // drains this side queue under the same mutex.
            std::lock_guard<std::mutex> lk(gCaptureP25PendingMutex);
            liveIqCapturePendingP25Lines << line;
            while (liveIqCapturePendingP25Lines.size() > 20000) {
                liveIqCapturePendingP25Lines.removeFirst();
            }
        }
        if (p25LogText && p25LogDialog && p25LogDialog->isVisible()) {
            // Batch visible QTextEdit updates. Appending every decoder/log line
            // directly from hot P25 state changes can dominate the Qt thread.
            p25VisibleLogPending << line;
            while (p25VisibleLogPending.size() > 600) p25VisibleLogPending.removeFirst();
            if (!p25VisibleLogFlushQueued) {
                p25VisibleLogFlushQueued = true;
                QTimer::singleShot(75, this, [this]() { flushVisibleP25LogLines(); });
            }
        }
    }

    void flushVisibleP25LogLines() {
        p25VisibleLogFlushQueued = false;
        if (!p25LogText || !p25LogDialog || !p25LogDialog->isVisible()) {
            p25VisibleLogPending.clear();
            return;
        }
        if (p25VisibleLogPending.isEmpty()) return;

        p25LogText->moveCursor(QTextCursor::End);
        for (const auto& pendingLine : p25VisibleLogPending) {
            p25LogText->append(pendingLine.toHtmlEscaped());
        }
        p25VisibleLogPending.clear();
        auto cursor = p25LogText->textCursor();
        cursor.movePosition(QTextCursor::End);
        p25LogText->setTextCursor(cursor);
    }

    void appendP25LogLineThrottled(const QString& signature, const QString& text, qint64 minIntervalMs = 1200) {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (signature == p25LastDiagSignature && now - p25LastDiagLogMs < minIntervalMs) return;
        p25LastDiagSignature = signature;
        p25LastDiagLogMs = now;
        appendP25LogLine(text);
    }

    void appendP25LogLineKeyed(const QString& key, const QString& text, qint64 minIntervalMs = 3000) {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const std::string mapKey = key.toStdString();
        auto it = p25LogThrottleByKey.find(mapKey);
        if (it != p25LogThrottleByKey.end() && now - it->second < minIntervalMs) return;
        p25LogThrottleByKey[mapKey] = now;
        while (p25LogThrottleByKey.size() > 800) {
            auto oldest = std::min_element(p25LogThrottleByKey.begin(), p25LogThrottleByKey.end(),
                [](const auto& a, const auto& b) { return a.second < b.second; });
            if (oldest == p25LogThrottleByKey.end()) break;
            p25LogThrottleByKey.erase(oldest);
        }
        appendP25LogLine(text);
    }

    void showTranscriptWindow() {
        if (!m_transcriptHub || !m_sttEngine) return;
        if (!m_transcriptWindow) {
            m_transcriptWindow = new TranscriptWindow(m_transcriptHub, m_sttEngine, this);
            connect(m_transcriptWindow, &QObject::destroyed, this, [this]() {
                m_transcriptWindow = nullptr;
            });
        }
        m_transcriptWindow->refreshFromHub();
        m_transcriptWindow->show();
        m_transcriptWindow->raise();
        m_transcriptWindow->activateWindow();
    }

    void showP25LogWindow() {
        if (p25LogDialog) {
            p25LogDialog->show();
            p25LogDialog->raise();
            p25LogDialog->activateWindow();
            return;
        }

        p25LogDialog = new QDialog(this);
        p25LogDialog->setWindowTitle("P25 Decoder Log");
        p25LogDialog->setAttribute(Qt::WA_DeleteOnClose);
        p25LogDialog->resize(900, 520);

        QVBoxLayout* lay = new QVBoxLayout(p25LogDialog);
        lay->setContentsMargins(8, 8, 8, 8);
        lay->setSpacing(6);

        p25LogText = new QTextEdit(p25LogDialog);
        p25LogText->setReadOnly(true);
        p25LogText->setLineWrapMode(QTextEdit::NoWrap);
        p25LogText->setFontFamily("Consolas");
        p25LogText->document()->setMaximumBlockCount(300);
        p25LogText->setPlainText(p25LogLines.join('\n'));
        lay->addWidget(p25LogText);

        QHBoxLayout* btns = new QHBoxLayout();
        QPushButton* clearBtn = new QPushButton("Clear", p25LogDialog);
        QPushButton* closeBtn = new QPushButton("Close", p25LogDialog);
        btns->addStretch();
        btns->addWidget(clearBtn);
        btns->addWidget(closeBtn);
        lay->addLayout(btns);

        connect(clearBtn, &QPushButton::clicked, this, [this]() {
            p25LogLines.clear();
            p25LogThrottleByKey.clear();
            if (p25LogText) p25LogText->clear();
        });
        connect(closeBtn, &QPushButton::clicked, p25LogDialog, &QDialog::close);
        connect(p25LogDialog, &QObject::destroyed, this, [this]() {
            p25LogDialog = nullptr;
            p25LogText = nullptr;
        });
        p25LogDialog->show();
    }

    void showIqReplayWindow() {
        if (iqReplayDialog) {
            iqReplayDialog->show();
            iqReplayDialog->raise();
            iqReplayDialog->activateWindow();
            return;
        }

        struct ReplayState {
            SigmfCaptureInfo info;
            std::atomic<bool> stopping{false};
            std::atomic<bool> busy{false};
            std::atomic<bool> drainingTail{false};
            std::atomic<long long> drainDeadlineMs{0};
            bool playing = false;
            std::atomic<bool> resetRequested{true};
            QString activeKey;
            QString lastWavPath;
            std::unique_ptr<Receiver> rx{std::make_unique<Receiver>()};
            SigmfIqCapture streamCapture;
            bool streamCaptureLoaded = false;
            QString streamCaptureKey;
            int streamCaptureStartMs = 0;
            int streamCaptureDurationMs = 0;
            std::vector<float> pendingSpeaker;
            P25Phase2SpeakerPendingQueue speakerQueue;
            Pcm16WavCapture wav;
            long long windows = 0;
            long long emitWindows = 0;
            long long gatedRawWindows = 0;
            long long emptyWindows = 0;
            long long decodedFrames = 0;
            long long speakerSamples = 0;
            long long speakerDrainEvents = 0;
            long long speakerDrainSamples = 0;
            long long speakerDroppedTailSamples = 0;
            long long fedFrames = 0;
            long long emittedPcmFrames = 0;
            long long feedGaps = 0;
            long long targetVoiceCodewords = 0;
            long long oppositeVoiceCodewords = 0;
            long long rejectedVoiceCodewords = 0;
            long long inputQualityRejectedVoiceCodewords = 0;
            bool hardTargetAcquire = false;
            int macEssStarveWindows = 0;
            int wideReacquireHoldWindows = 0;
            bool forceMaskEpochRehunt = false;
            int maskEpochRepairHoldWindows = 0;
            int emptyEyeWindows = 0;
            double streamEndMs = -1.0;
            std::mutex mutex;
        };

        auto state = std::make_shared<ReplayState>();
        iqReplayDialog = new QDialog(this);
        iqReplayDialog->setWindowTitle("IQ Replay");
        iqReplayDialog->setAttribute(Qt::WA_DeleteOnClose);
        iqReplayDialog->resize(980, 640);

        QVBoxLayout* lay = new QVBoxLayout(iqReplayDialog);
        lay->setContentsMargins(8, 8, 8, 8);
        lay->setSpacing(6);

        QHBoxLayout* pathRow = new QHBoxLayout();
        QLineEdit* pathEdit = new QLineEdit(iqReplayDialog);
        pathEdit->setText(QString::fromStdString(guiRuntimeConfig.iqReplayPath));
        QPushButton* browseBtn = new QPushButton("Browse", iqReplayDialog);
        QPushButton* loadBtn = new QPushButton("Load", iqReplayDialog);
        pathRow->addWidget(pathEdit, 1);
        pathRow->addWidget(browseBtn);
        pathRow->addWidget(loadBtn);
        lay->addLayout(pathRow);

        QLabel* metaLabel = new QLabel("No capture loaded", iqReplayDialog);
        lay->addWidget(metaLabel);

        QFormLayout* form = new QFormLayout();
        QDoubleSpinBox* centerSpin = new QDoubleSpinBox(iqReplayDialog);
        centerSpin->setDecimals(6);
        centerSpin->setRange(0.0, 6000.0);
        centerSpin->setSuffix(" MHz");
        centerSpin->setValue(guiRuntimeConfig.iqReplayCenterHz > 0.0 ? guiRuntimeConfig.iqReplayCenterHz / 1e6 : 0.0);
        QDoubleSpinBox* voiceCenterSpin = new QDoubleSpinBox(iqReplayDialog);
        voiceCenterSpin->setDecimals(6);
        voiceCenterSpin->setRange(0.0, 6000.0);
        voiceCenterSpin->setSpecialValueText("auto");
        voiceCenterSpin->setSuffix(" MHz");
        voiceCenterSpin->setValue(guiRuntimeConfig.iqReplayVoiceCenterHz > 0.0 ? guiRuntimeConfig.iqReplayVoiceCenterHz / 1e6 : 0.0);
        QDoubleSpinBox* targetSpin = new QDoubleSpinBox(iqReplayDialog);
        targetSpin->setDecimals(6);
        targetSpin->setRange(0.0, 6000.0);
        targetSpin->setSuffix(" MHz");
        targetSpin->setValue(guiRuntimeConfig.iqReplayTargetHz > 0.0 ? guiRuntimeConfig.iqReplayTargetHz / 1e6 : 0.0);
        QSpinBox* tgSpin = new QSpinBox(iqReplayDialog);
        tgSpin->setRange(0, 99999999);
        tgSpin->setValue(std::max(0, guiRuntimeConfig.iqReplayTalkgroup));
        QSpinBox* slotSpin = new QSpinBox(iqReplayDialog);
        slotSpin->setRange(-1, 1);
        slotSpin->setSpecialValueText("auto");
        slotSpin->setValue(guiRuntimeConfig.iqReplaySlot);
        QSpinBox* nacSpin = new QSpinBox(iqReplayDialog);
        nacSpin->setRange(-1, 0x0fff);
        nacSpin->setSpecialValueText("auto");
        nacSpin->setValue(guiRuntimeConfig.iqReplayNac);
        QLineEdit* wacnEdit = new QLineEdit(iqReplayDialog);
        if (guiRuntimeConfig.iqReplayWacn >= 0) wacnEdit->setText(QString::number(guiRuntimeConfig.iqReplayWacn));
        QSpinBox* systemSpin = new QSpinBox(iqReplayDialog);
        systemSpin->setRange(-1, 0x0fff);
        systemSpin->setSpecialValueText("auto");
        systemSpin->setValue(guiRuntimeConfig.iqReplaySystemId);
        QCheckBox* clearCheck = new QCheckBox("Clear grant", iqReplayDialog);
        clearCheck->setChecked(guiRuntimeConfig.iqReplayClearGrant);
        QCheckBox* encCheck = new QCheckBox("Encrypted grant", iqReplayDialog);
        encCheck->setChecked(guiRuntimeConfig.iqReplayEncryptedGrant);
        QCheckBox* sttCheck = new QCheckBox("STT", iqReplayDialog);
        sttCheck->setChecked(guiRuntimeConfig.iqReplayStt);
        form->addRow("Center", centerSpin);
        form->addRow("Voice center", voiceCenterSpin);
        form->addRow("Target", targetSpin);
        form->addRow("Talkgroup", tgSpin);
        form->addRow("TDMA slot", slotSpin);
        form->addRow("NAC", nacSpin);
        form->addRow("WACN", wacnEdit);
        form->addRow("System", systemSpin);
        form->addRow(clearCheck, encCheck);
        form->addRow("Speech text", sttCheck);
        lay->addLayout(form);

        QHBoxLayout* timingRow = new QHBoxLayout();
        QSpinBox* startSpin = new QSpinBox(iqReplayDialog);
        startSpin->setRange(0, 36000000);
        startSpin->setSuffix(" ms");
        startSpin->setValue(std::max(0, guiRuntimeConfig.iqReplayStartMs));
        QSpinBox* durationSpin = new QSpinBox(iqReplayDialog);
        durationSpin->setRange(100, 600000);
        durationSpin->setSuffix(" ms");
        durationSpin->setValue(std::clamp(guiRuntimeConfig.iqReplayDurationMs, 100, 600000));
        QSpinBox* windowSpin = new QSpinBox(iqReplayDialog);
        windowSpin->setRange(80, 5000);
        windowSpin->setSuffix(" ms");
        windowSpin->setValue(std::clamp(guiRuntimeConfig.iqReplayWindowMs, 80, 5000));
        QSpinBox* hopSpin = new QSpinBox(iqReplayDialog);
        hopSpin->setRange(0, 1000);
        hopSpin->setSpecialValueText("auto");
        hopSpin->setSuffix(" ms");
        hopSpin->setValue(guiRuntimeConfig.iqReplayHopMs <= 0 ? 0 : std::clamp(guiRuntimeConfig.iqReplayHopMs, 10, 1000));
        timingRow->addWidget(new QLabel("Start", iqReplayDialog));
        timingRow->addWidget(startSpin);
        timingRow->addWidget(new QLabel("Duration", iqReplayDialog));
        timingRow->addWidget(durationSpin);
        timingRow->addWidget(new QLabel("Window", iqReplayDialog));
        timingRow->addWidget(windowSpin);
        timingRow->addWidget(new QLabel("Hop", iqReplayDialog));
        timingRow->addWidget(hopSpin);
        lay->addLayout(timingRow);

        QSlider* posSlider = new QSlider(Qt::Horizontal, iqReplayDialog);
        posSlider->setRange(0, 0);
        posSlider->setValue(std::max(0, guiRuntimeConfig.iqReplayStartMs));
        QLabel* timeLabel = new QLabel("00:00.000 / 00:00.000", iqReplayDialog);
        QHBoxLayout* sliderRow = new QHBoxLayout();
        sliderRow->addWidget(posSlider, 1);
        sliderRow->addWidget(timeLabel);
        lay->addLayout(sliderRow);

        QTextEdit* logText = new QTextEdit(iqReplayDialog);
        logText->setReadOnly(true);
        logText->setLineWrapMode(QTextEdit::NoWrap);
        logText->setFontFamily("Consolas");
        logText->document()->setMaximumBlockCount(400);
        lay->addWidget(logText, 1);

        QHBoxLayout* buttons = new QHBoxLayout();
        QPushButton* playBtn = new QPushButton("Play", iqReplayDialog);
        QPushButton* stepBtn = new QPushButton("Step", iqReplayDialog);
        QPushButton* resetBtn = new QPushButton("Reset Decoder", iqReplayDialog);
        QPushButton* transcriptBtn = new QPushButton("Transcript", iqReplayDialog);
        QPushButton* closeBtn = new QPushButton("Close", iqReplayDialog);
        buttons->addWidget(playBtn);
        buttons->addWidget(stepBtn);
        buttons->addWidget(resetBtn);
        buttons->addStretch();
        buttons->addWidget(transcriptBtn);
        buttons->addWidget(closeBtn);
        lay->addLayout(buttons);

        auto formatMs = [](int ms) {
            const int clamped = std::max(0, ms);
            const int minutes = clamped / 60000;
            const int seconds = (clamped / 1000) % 60;
            const int millis = clamped % 1000;
            return QString("%1:%2.%3")
                .arg(minutes, 2, 10, QChar('0'))
                .arg(seconds, 2, 10, QChar('0'))
                .arg(millis, 3, 10, QChar('0'));
        };
        auto updateTimeLabel = [=]() {
            timeLabel->setText(QString("%1 / %2")
                .arg(formatMs(posSlider->value()))
                .arg(formatMs(posSlider->maximum())));
        };
        auto appendReplayLog = [=](const QString& line) {
            const QString stamped = QString("[%1] %2")
                .arg(QDateTime::currentDateTime().toString("HH:mm:ss.zzz"))
                .arg(line);
            logText->append(stamped);
            appendP25LogLine("IQ replay: " + line);
            {
                std::lock_guard<std::mutex> lk(guiIqReplayStatusMutex);
                guiIqReplayLastStatus = line.left(500);
                guiIqReplayRecentStatus.push_back(line.left(1000));
                while (guiIqReplayRecentStatus.size() > 200) {
                    guiIqReplayRecentStatus.removeFirst();
                }
            }
        };
        auto parseWacn = [=]() -> int64_t {
            const QString text = wacnEdit->text().trimmed();
            if (text.isEmpty() || text.compare("auto", Qt::CaseInsensitive) == 0) return -1;
            bool ok = false;
            const int base = text.startsWith("0x", Qt::CaseInsensitive) ? 16 : 10;
            const qlonglong value = text.toLongLong(&ok, base);
            return ok ? static_cast<int64_t>(value) : -1;
        };
        auto metadataSummary = [](const SigmfCaptureInfo& info) {
            if (!info.ok) return QString("Load failed: %1").arg(QString::fromStdString(info.error));
            return QString("%1 | %2 Hz | center %3 MHz | %4 samples | %5")
                .arg(info.datatype.empty() ? QStringLiteral("unknown") : QString::fromStdString(info.datatype))
                .arg(info.sampleRateHz, 0, 'f', 0)
                .arg(info.centerFreqHz / 1e6, 0, 'f', 6)
                .arg(static_cast<qulonglong>(info.totalSamples))
                .arg(humanBytes(static_cast<qint64>(std::min<uint64_t>(
                    info.totalBytes, static_cast<uint64_t>(std::numeric_limits<qint64>::max())))));
        };
        auto resetDecoder = [state]() {
            std::lock_guard<std::mutex> lk(state->mutex);
            state->rx = std::make_unique<Receiver>();
            state->pendingSpeaker.clear();
            state->speakerQueue = P25Phase2SpeakerPendingQueue{};
            state->wav.close();
            state->lastWavPath.clear();
            state->activeKey.clear();
            state->windows = 0;
            state->emitWindows = 0;
            state->gatedRawWindows = 0;
            state->emptyWindows = 0;
            state->decodedFrames = 0;
            state->speakerSamples = 0;
            state->fedFrames = 0;
            state->emittedPcmFrames = 0;
            state->feedGaps = 0;
            state->targetVoiceCodewords = 0;
            state->oppositeVoiceCodewords = 0;
            state->rejectedVoiceCodewords = 0;
            state->inputQualityRejectedVoiceCodewords = 0;
            state->resetRequested.store(true, std::memory_order_release);
        };
        auto refreshMetadata = [=]() {
            const QString path = QDir::fromNativeSeparators(pathEdit->text()).trimmed();
            if (path.isEmpty()) {
                metaLabel->setText("No capture loaded");
                return false;
            }
            const SigmfCaptureInfo info = inspectSigmfCf32Capture(path);
            state->info = info;
            metaLabel->setText(metadataSummary(info));
            if (!info.ok) {
                appendReplayLog(metaLabel->text());
                return false;
            }
            if (centerSpin->value() <= 0.0) centerSpin->setValue(info.centerFreqHz / 1e6);
            if (voiceCenterSpin->value() <= 0.0 && guiRuntimeConfig.iqReplayVoiceCenterHz > 0.0) {
                voiceCenterSpin->setValue(guiRuntimeConfig.iqReplayVoiceCenterHz / 1e6);
            }
            if (targetSpin->value() <= 0.0) {
                const double targetHz = info.targetFreqHz > 0.0 ? info.targetFreqHz : info.centerFreqHz;
                targetSpin->setValue(targetHz / 1e6);
            }
            const int maxMs = static_cast<int>(std::clamp(info.totalDurationMs, 0.0, 36000000.0));
            posSlider->setRange(0, maxMs);
            startSpin->setRange(0, std::max(0, maxMs));
            if (posSlider->value() == 0 && guiRuntimeConfig.iqReplayStartMs > 0) {
                posSlider->setValue(std::min(guiRuntimeConfig.iqReplayStartMs, maxMs));
            }
            updateTimeLabel();
            appendReplayLog(QString("Loaded %1").arg(metadataSummary(info)));
            resetDecoder();
            return true;
        };

        struct ReplayJob {
            QString path;
            QString wavPath;
            int startMs = 0;
            int durationMs = 5000;
            int windowMs = 720;
            int hopMs = 0;
            double centerHz = 0.0;
            double voiceCenterHz = 0.0;
            double targetHz = 0.0;
            uint32_t talkgroup = 0;
            int slot = -1;
            int nac = -1;
            int64_t wacn = -1;
            int systemId = -1;
            bool clearGrant = false;
            bool encryptedGrant = false;
            bool stt = true;
            bool reset = false;
        };
        auto makeReplayKey = [](const ReplayJob& job) {
            return QString("%1|%2|%3|%4|%5|%6|%7|%8|%9|%10|%11|%12")
                .arg(job.path)
                .arg(job.centerHz, 0, 'f', 0)
                .arg(job.voiceCenterHz, 0, 'f', 0)
                .arg(job.targetHz, 0, 'f', 0)
                .arg(job.durationMs)
                .arg(job.talkgroup)
                .arg(job.slot)
                .arg(job.nac)
                .arg(static_cast<qlonglong>(job.wacn))
                .arg(job.systemId)
                .arg(job.clearGrant ? 1 : 0)
                .arg(job.encryptedGrant ? 1 : 0);
        };
        auto configureReceiver = [](Receiver& rx, P25ReplayCliArgs& args, const ReplayJob& job, const SigmfIqCapture& capture) {
            const double targetHz = args.targetMhz > 0.0 && std::isfinite(args.targetMhz)
                ? args.targetMhz * 1e6
                : (job.targetHz > 0.0 ? job.targetHz : capture.targetFreqHz);
            const double trafficCenterHz = args.voiceCenterMhz > 0.0 && std::isfinite(args.voiceCenterMhz)
                ? args.voiceCenterMhz * 1e6
                : (job.voiceCenterHz > 0.0 ? job.voiceCenterHz : job.centerHz);
            rx.freqHz = targetHz;
            rx.mode = DemodMode::NFM;
            rx.channelBwHz = 12500.0;
            rx.lpfHz = 3000.0;
            rx.audioLpfEnabled = false;
            rx.squelchDb = -105.0;
            p25ClearPhase2PendingAudio(rx);
            rx.resetP25VoiceState();
            clearP25SessionScopedState(rx);
            rx.p25VoiceDecodeEnabled = true;
            rx.p25VoiceClearKnown = job.clearGrant && !job.encryptedGrant;
            rx.p25VoiceEncrypted = job.encryptedGrant;
            rx.p25VoiceTalkgroupId = job.talkgroup != 0 ? job.talkgroup : 1u;
            rx.p25VoiceSourceId = 0;
            rx.p25VoiceGrantEpochMs = QDateTime::currentMSecsSinceEpoch() - 1000;
            p25Phase2BeginNewPtt(rx, rx.p25VoiceGrantEpochMs);
            rx.p25VoicePhase2 = true;
            rx.p25TrafficRetunesPrimary = true;
            rx.p25IndependentTrafficSource = true;
            rx.p25TrafficVoiceFreqHz = targetHz;
            rx.p25TrafficSourceCenterFreqHz = trafficCenterHz;
            rx.p25TrafficControlFreqHz = capture.centerFreqHz > 0.0 ? capture.centerFreqHz : job.centerHz;
            (void)trySeedP25ReplayMaskFromCaptureLog(args);
            rx.p25VoiceTdmaSlotKnown = job.slot >= 0;
            rx.p25VoiceTdmaSlot = job.slot >= 0 ? static_cast<uint8_t>(job.slot & 0x01) : 0u;
            rx.p25VoiceMaskParamsKnown = p25ReplayHasMaskParameters(args);
            rx.p25VoiceNac = args.nac >= 0 ? static_cast<uint16_t>(args.nac) : 0;
            rx.p25VoiceWacn = args.wacn >= 0 ? static_cast<uint32_t>(args.wacn) : 0;
            rx.p25VoiceSystemId = args.systemId >= 0 ? static_cast<uint16_t>(args.systemId) : 0;
            rx.p25Phase2GrantedSlotImmutable = false;
            if (rx.p25VoiceTdmaSlotKnown &&
                (rx.p25VoiceClearKnown || rx.p25VoiceEncrypted || rx.p25VoiceMaskParamsKnown)) {
                p25Phase2MarkGrantedSlotImmutable(rx);
            }
            rx.p25Phase2AllowLateEntryAudioProbe = false;
            rx.p25VoiceLiveDecoder = P25LiveDecoder(p25VoiceDecoderConfigForReceiver(rx, P25VoiceDecodeProfile::Realtime));
            if (rx.p25VoiceMaskParamsKnown) {
                rx.p25VoiceLiveDecoder.setPhase2MaskParameters(
                    rx.p25VoiceNac, rx.p25VoiceWacn, rx.p25VoiceSystemId);
            } else {
                rx.p25VoiceLiveDecoder.clearPhase2MaskParameters();
            }
        };

        auto writeReplayResult = [=](const QString& phase) {
            if (guiRuntimeConfig.iqReplayResultPath.empty() &&
                (!guiRuntimeConfig.selfTest || guiRuntimeConfig.selfTestPath.empty())) {
                return;
            }
            const QString path = !guiRuntimeConfig.iqReplayResultPath.empty()
                ? QString::fromStdString(guiRuntimeConfig.iqReplayResultPath)
                : guiRuntimeSelfTestPath();
            try {
                json record;
                record["schema"] = "sdr-town-gui-iq-replay-v1";
                record["phase"] = phase.toStdString();
                record["timeUtc"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toStdString();
                record["path"] = pathEdit->text().toStdString();
                record["positionMs"] = posSlider->value();
                record["durationMs"] = durationSpin->value();
                record["windowMs"] = windowSpin->value();
                record["hopMs"] = hopSpin->value();
                record["targetHz"] = targetSpin->value() * 1e6;
                record["centerHz"] = centerSpin->value() * 1e6;
                record["voiceCenterHz"] = voiceCenterSpin->value() * 1e6;
                record["talkgroup"] = tgSpin->value();
                record["slot"] = slotSpin->value();
                record["clearGrant"] = clearCheck->isChecked();
                record["encryptedGrant"] = encCheck->isChecked();
                record["stt"] = sttCheck->isChecked();
                record["windows"] = guiIqReplayWindows.load(std::memory_order_relaxed);
                record["audioEvents"] = guiIqReplayAudioOutputEvents.load(std::memory_order_relaxed);
                record["audioSamples"] = guiIqReplayAudioOutputSamples.load(std::memory_order_relaxed);
                record["decodedFrames"] = guiIqReplayDecodedFrames.load(std::memory_order_relaxed);
                record["lastOutputMs"] = guiIqReplayLastOutputMs.load(std::memory_order_relaxed);
                {
                    std::lock_guard<std::mutex> lk(state->mutex);
                    record["iqReplay"]["speakerPushedSamples"] = state->speakerSamples;
                    record["iqReplay"]["speakerDrainEvents"] = state->speakerDrainEvents;
                    record["iqReplay"]["speakerDrainSamples"] = state->speakerDrainSamples;
                    record["iqReplay"]["speakerDroppedTailSamples"] = state->speakerDroppedTailSamples;
                    record["iqReplay"]["fedFrames"] = state->fedFrames;
                    record["iqReplay"]["emittedPcmFrames"] = state->emittedPcmFrames;
                    record["iqReplay"]["feedGaps"] = state->feedGaps;
                    record["iqReplay"]["targetVoiceCodewords"] = state->targetVoiceCodewords;
                    record["iqReplay"]["oppositeVoiceCodewords"] = state->oppositeVoiceCodewords;
                    record["iqReplay"]["rejectedVoiceCodewords"] = state->rejectedVoiceCodewords;
                    record["iqReplay"]["inputQualityRejectedVoiceCodewords"] =
                        state->inputQualityRejectedVoiceCodewords;
                    record["iqReplay"]["pendingSpeakerSamples"] = state->pendingSpeaker.size();
                    record["iqReplay"]["emitWindows"] = state->emitWindows;
                    record["iqReplay"]["gatedRawWindows"] = state->gatedRawWindows;
                    record["iqReplay"]["emptyWindows"] = state->emptyWindows;
                }
                {
                    std::lock_guard<std::mutex> lk(guiIqReplayStatusMutex);
                    record["lastStatus"] = guiIqReplayLastStatus.toStdString();
                    json recent = json::array();
                    for (const QString& item : guiIqReplayRecentStatus) {
                        recent.push_back(item.toStdString());
                    }
                    record["recentStatus"] = std::move(recent);
                }
                record["ok"] = !guiRuntimeConfig.requireClearAudio ||
                    guiIqReplayAudioOutputEvents.load(std::memory_order_relaxed) > 0;
                const QFileInfo info(path);
                if (!info.absolutePath().isEmpty()) QDir().mkpath(info.absolutePath());
                std::ofstream out(path.toStdString(), std::ios::trunc);
                out << record.dump(2) << "\n";
            } catch (...) {
            }
        };

        QTimer* playTimer = new QTimer(iqReplayDialog);
        playTimer->setInterval(20);
        QPointer<QDialog> dialogPtr(iqReplayDialog);
        QPointer<MainWindow> self(this);
        auto finishReplay = [=](const QString& phase) {
            state->playing = false;
            state->drainingTail.store(false, std::memory_order_release);
            state->drainDeadlineMs.store(0, std::memory_order_release);
            playTimer->stop();
            playBtn->setText("Play");
            {
                std::lock_guard<std::mutex> lk(state->mutex);
                state->wav.close();
            }
            writeReplayResult(phase);
            const QByteArray phaseUtf8 = phase.toUtf8();
            writeGuiRuntimeSelfTestResult(phaseUtf8.constData());
            if (guiRuntimeConfig.selfTest || guiRuntimeConfig.exitAfterMs > 0) {
                QCoreApplication::quit();
            }
        };
        std::shared_ptr<std::function<void()>> drainReplaySpeakerTail =
            std::make_shared<std::function<void()>>();
        *drainReplaySpeakerTail = [=]() {
            if (!dialogPtr || state->stopping.load(std::memory_order_acquire)) return;

            AudioEngine* audioEngine = peekAudioEngineIfReady();
            const double outRate = audioEngine
                ? std::max(8000.0, static_cast<double>(audioEngine->getSampleRate()))
                : 48000.0;
            const size_t phase2FrameSamples = std::max<size_t>(160,
                static_cast<size_t>(outRate * 0.020 + 0.5));

            size_t pendingBefore = 0;
            {
                std::lock_guard<std::mutex> lk(state->mutex);
                pendingBefore = state->pendingSpeaker.size();
                if (pendingBefore > 0 && pendingBefore < phase2FrameSamples) {
                    state->speakerDroppedTailSamples += static_cast<long long>(pendingBefore);
                    state->pendingSpeaker.clear();
                    pendingBefore = 0;
                }
            }
            if (pendingBefore == 0) {
                appendReplayLog("speaker pending drain complete");
                finishReplay("replay-finished");
                return;
            }

            const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
            const long long deadlineMs = state->drainDeadlineMs.load(std::memory_order_acquire);
            if (deadlineMs > 0 && nowMs > deadlineMs) {
                appendReplayLog(QString("speaker pending drain timeout: pending=%1 samples")
                    .arg(static_cast<qulonglong>(pendingBefore)));
                finishReplay("replay-finished-drain-timeout");
                return;
            }

            if (!audioEngine || audioEngine->activeOutputCount() == 0) {
                std::vector<float> flushed;
                {
                    std::lock_guard<std::mutex> lk(state->mutex);
                    flushed.swap(state->pendingSpeaker);
                    if (!flushed.empty()) {
                        state->speakerSamples += static_cast<long long>(flushed.size());
                        ++state->speakerDrainEvents;
                        state->speakerDrainSamples += static_cast<long long>(flushed.size());
                        if (state->wav.active()) state->wav.append(flushed);
                    }
                }
                if (!flushed.empty()) {
                    guiIqReplayAudioOutputEvents.fetch_add(1, std::memory_order_relaxed);
                    guiIqReplayAudioOutputSamples.fetch_add(static_cast<long long>(flushed.size()),
                                                            std::memory_order_relaxed);
                    guiIqReplayLastOutputMs.store(nowMs, std::memory_order_relaxed);
                    if (sttCheck->isChecked()) {
                        p25TranscriptTapSpeakerPcm(flushed.data(), flushed.size(), 48000,
                                                  static_cast<uint32_t>(std::max(0, tgSpin->value())),
                                                  targetSpin->value() * 1e6,
                                                  slotSpin->value());
                    }
                }
                appendReplayLog(QString("speaker pending drain wrote offline tail=%1 samples")
                    .arg(static_cast<qulonglong>(flushed.size())));
                finishReplay("replay-finished");
                return;
            }

            std::vector<float> pushedRealAudio;
            size_t pushed = 0;
            size_t pendingAfter = pendingBefore;
            {
                std::lock_guard<std::mutex> lk(state->mutex);
                pushed = pushP25LiveStreamingAudio(audioEngine,
                                                   state->pendingSpeaker,
                                                   {},
                                                   {},
                                                   phase2FrameSamples,
                                                   -1.0,
                                                   true,
                                                   &pushedRealAudio);
                pendingAfter = state->pendingSpeaker.size();
                if (!pushedRealAudio.empty()) {
                    state->speakerSamples += static_cast<long long>(pushedRealAudio.size());
                    ++state->speakerDrainEvents;
                    state->speakerDrainSamples += static_cast<long long>(pushedRealAudio.size());
                    if (state->wav.active()) state->wav.append(pushedRealAudio);
                }
            }
            if (!pushedRealAudio.empty()) {
                guiIqReplayAudioOutputEvents.fetch_add(1, std::memory_order_relaxed);
                guiIqReplayAudioOutputSamples.fetch_add(static_cast<long long>(pushedRealAudio.size()),
                                                        std::memory_order_relaxed);
                guiIqReplayLastOutputMs.store(nowMs, std::memory_order_relaxed);
                if (sttCheck->isChecked()) {
                    p25TranscriptTapSpeakerPcm(pushedRealAudio.data(), pushedRealAudio.size(),
                                              static_cast<int>(std::lround(outRate)),
                                              static_cast<uint32_t>(std::max(0, tgSpin->value())),
                                              targetSpin->value() * 1e6,
                                              slotSpin->value());
                }
            }

            if (pushed > 0) {
                appendReplayLog(QString("speaker pending drain: pushed=%1 pending=%2")
                    .arg(static_cast<qulonglong>(pushed))
                    .arg(static_cast<qulonglong>(pendingAfter)));
            }
            QTimer::singleShot(20, dialogPtr, [=]() {
                if (!dialogPtr || !state->drainingTail.load(std::memory_order_acquire)) return;
                (*drainReplaySpeakerTail)();
            });
        };

        std::shared_ptr<std::function<void(bool)>> decodeOne = std::make_shared<std::function<void(bool)>>();
        *decodeOne = [=](bool resetBefore) {
            if (!dialogPtr || state->stopping.load(std::memory_order_acquire)) return;
            if (state->drainingTail.load(std::memory_order_acquire)) return;
            if (state->busy.exchange(true, std::memory_order_acq_rel)) return;
            ReplayJob job;
            job.path = QDir::fromNativeSeparators(pathEdit->text()).trimmed();
            job.wavPath = QString::fromStdString(guiRuntimeConfig.iqReplayWavPath).trimmed();
            job.startMs = posSlider->value();
            job.durationMs = durationSpin->value();
            job.windowMs = windowSpin->value();
            job.hopMs = hopSpin->value();
            job.centerHz = centerSpin->value() * 1e6;
            job.voiceCenterHz = voiceCenterSpin->value() * 1e6;
            job.targetHz = targetSpin->value() * 1e6;
            job.talkgroup = static_cast<uint32_t>(std::max(0, tgSpin->value()));
            job.slot = slotSpin->value();
            job.nac = nacSpin->value();
            job.wacn = parseWacn();
            job.systemId = systemSpin->value();
            job.clearGrant = clearCheck->isChecked() && !encCheck->isChecked();
            job.encryptedGrant = encCheck->isChecked();
            job.stt = sttCheck->isChecked();
            job.reset = resetBefore;
            const int sliderMax = posSlider->maximum();
            AudioEngine* audioEngine = ensureAudioOutputActive("IQ replay");
            std::thread([=]() {
                QString line;
                int nextPos = job.startMs;
                bool emitted = false;
                try {
                    const QString replayKey = makeReplayKey(job);
                    int loadStartMs = job.startMs;
                    int loadDurationMs = std::max(1, job.windowMs);
                    double plannedContextMs = 0.0;
                    double decodeStreamEndMs = static_cast<double>(job.startMs + std::max(1, job.windowMs));
                    double steadyFreshMs = static_cast<double>(std::max(1, job.hopMs));
                    bool needsStreamCaptureLoad = false;
                    int streamCaptureStartMs = job.startMs;
                    int streamCaptureDurationMs = std::max(job.durationMs, job.windowMs);
                    {
                        std::lock_guard<std::mutex> lk(state->mutex);
                        const bool resettingReplay =
                            job.reset ||
                            state->resetRequested.load(std::memory_order_acquire) ||
                            state->activeKey != replayKey || !state->rx;
                        streamCaptureStartMs = resettingReplay ? job.startMs : state->streamCaptureStartMs;
                        streamCaptureDurationMs = std::max(job.durationMs, job.windowMs);
                        needsStreamCaptureLoad =
                            resettingReplay ||
                            !state->streamCaptureLoaded ||
                            state->streamCaptureKey != replayKey ||
                            state->streamCaptureStartMs != streamCaptureStartMs ||
                            state->streamCaptureDurationMs < streamCaptureDurationMs;
                        const bool streamingDdc =
                            !resettingReplay &&
                            state->rx &&
                            state->rx->p25VoiceLiveDecoder.config().enableStreamingChannelDdc;
                        steadyFreshMs = job.hopMs > 0
                            ? static_cast<double>(std::max(1, job.hopMs))
                            : (streamingDdc
                                ? kP25Phase2VoiceDecodeSustainChunkSeconds * 1000.0
                                : kP25Phase2VoiceDecodeSpeakerSustainChunkSeconds * 1000.0);
                        if (resettingReplay || !std::isfinite(state->streamEndMs) || state->streamEndMs <= 0.0) {
                            state->streamEndMs = static_cast<double>(job.startMs + std::max(1, job.windowMs));
                        }
                        const bool forceWideReacquire = !resettingReplay && state->wideReacquireHoldWindows > 0;
                        const bool forceMaskRepair = !resettingReplay && state->maskEpochRepairHoldWindows > 0;
                        const bool unacquiredWindow =
                            resettingReplay || !state->hardTargetAcquire || forceMaskRepair;
                        const bool speakerSustainWindow =
                            !forceWideReacquire &&
                            !forceMaskRepair &&
                            !unacquiredWindow &&
                            state->windows > 1 &&
                            (state->speakerSamples > 0 ||
                             (state->rx && p25Phase2SessionSpeakerSustainActive(*state->rx)));
                        const double coldMs = static_cast<double>(std::max(1, job.windowMs));
                        const double acquireContextMs =
                            std::min(coldMs, kP25Phase2VoiceDecodeAcquireOverlapSeconds * 1000.0);
                        const double sustainContextMs =
                            std::min(coldMs, kP25Phase2VoiceDecodeSustainOverlapSeconds * 1000.0);
                        const double speakerContextMs =
                            std::min(coldMs, kP25Phase2VoiceDecodeSpeakerSustainOverlapSeconds * 1000.0);
                        const double contextMs = streamingDdc
                            ? 0.0
                            : (speakerSustainWindow
                                ? speakerContextMs
                                : (unacquiredWindow ? coldMs : (state->windows > 1 ? sustainContextMs : acquireContextMs)));
                        const double desiredLookbackMs = streamingDdc
                            ? ((forceWideReacquire || forceMaskRepair || state->windows == 0 || unacquiredWindow)
                                ? coldMs
                                : steadyFreshMs)
                            : ((forceWideReacquire || forceMaskRepair || state->windows == 0 || unacquiredWindow)
                                ? coldMs
                                : std::min(coldMs, contextMs + steadyFreshMs));
                        decodeStreamEndMs = state->streamEndMs;
                        loadDurationMs = std::max(1, static_cast<int>(std::ceil(desiredLookbackMs)));
                        loadStartMs = std::max(0, static_cast<int>(std::llround(decodeStreamEndMs)) - loadDurationMs);
                        // DEC-0033: never prepend overlap IQ into sticky DDC/NCO
                        // (FIR desync). CLI voicetest already zeros context.
                        plannedContextMs = streamingDdc
                            ? 0.0
                            : ((state->windows == 0 || unacquiredWindow)
                                ? 0.0
                                : std::max(0.0, desiredLookbackMs - steadyFreshMs));
                    }

                    SigmfIqCapture loadedStreamCapture;
                    if (needsStreamCaptureLoad) {
                        loadedStreamCapture = loadSigmfCf32Capture(
                            job.path, streamCaptureDurationMs, streamCaptureStartMs);
                    }
                    SigmfIqCapture capture;
                    {
                        std::lock_guard<std::mutex> lk(state->mutex);
                        if (needsStreamCaptureLoad) {
                            state->streamCapture = std::move(loadedStreamCapture);
                            state->streamCaptureLoaded = state->streamCapture.ok;
                            state->streamCaptureKey = replayKey;
                            state->streamCaptureStartMs = streamCaptureStartMs;
                            state->streamCaptureDurationMs = streamCaptureDurationMs;
                        }
                        if (!state->streamCaptureLoaded || !state->streamCapture.ok) {
                            capture.ok = false;
                            capture.error = state->streamCapture.error.empty()
                                ? std::string("could not load replay stream capture")
                                : state->streamCapture.error;
                        } else {
                            const SigmfIqCapture& base = state->streamCapture;
                            const double relStartMs = static_cast<double>(loadStartMs) - base.startOffsetMs;
                            const uint64_t relStartSample = static_cast<uint64_t>(std::clamp(
                                base.sampleRateHz * (std::max(0.0, relStartMs) / 1000.0),
                                0.0,
                                base.iq.empty() ? 0.0 : static_cast<double>(base.iq.size() - 1u)));
                            const uint64_t requestedSamples = static_cast<uint64_t>(std::clamp(
                                base.sampleRateHz * (static_cast<double>(loadDurationMs) / 1000.0),
                                1.0,
                                relStartSample < base.iq.size()
                                    ? static_cast<double>(base.iq.size() - relStartSample)
                                    : 1.0));
                            const uint64_t relEndSample = std::min<uint64_t>(
                                base.iq.size(), relStartSample + requestedSamples);
                            capture.ok = relEndSample > relStartSample;
                            capture.metaPath = base.metaPath;
                            capture.dataPath = base.dataPath;
                            capture.datatype = base.datatype;
                            capture.sampleRateHz = base.sampleRateHz;
                            capture.centerFreqHz = base.centerFreqHz;
                            capture.targetFreqHz = base.targetFreqHz;
                            capture.totalSamples = base.totalSamples;
                            capture.totalBytes = base.totalBytes;
                            capture.totalDurationMs = base.totalDurationMs;
                            capture.firstSampleOffset = base.firstSampleOffset + relStartSample;
                            capture.startOffsetMs = base.sampleRateHz > 0.0
                                ? static_cast<double>(capture.firstSampleOffset) * 1000.0 / base.sampleRateHz
                                : static_cast<double>(loadStartMs);
                            if (capture.ok) {
                                capture.iq.assign(
                                    base.iq.begin() + static_cast<std::ptrdiff_t>(relStartSample),
                                    base.iq.begin() + static_cast<std::ptrdiff_t>(relEndSample));
                            } else {
                                capture.error = "replay stream window is outside the loaded capture segment";
                            }
                        }
                    }
                    if (!capture.ok) {
                        line = QString("decode load failed: %1").arg(QString::fromStdString(capture.error));
                    } else {
                        const double centerHz = job.centerHz > 0.0 ? job.centerHz : capture.centerFreqHz;
                        const double voiceCenterHz = job.voiceCenterHz > 0.0 ? job.voiceCenterHz : centerHz;
                        const double targetHz = job.targetHz > 0.0
                            ? job.targetHz
                            : (capture.targetFreqHz > 0.0 ? capture.targetFreqHz : voiceCenterHz);
                        capture.centerFreqHz = centerHz;
                        P25ReplayCliArgs seedArgs;
                        seedArgs.path = job.path.toStdString();
                        seedArgs.targetMhz = targetHz / 1e6;
                        seedArgs.centerMhz = centerHz / 1e6;
                        seedArgs.voiceCenterMhz = voiceCenterHz / 1e6;
                        seedArgs.followTalkgroupId = job.talkgroup;
                        seedArgs.tdmaSlot = job.slot;
                        seedArgs.phase2Voice = true;
                        seedArgs.nac = job.nac;
                        seedArgs.wacn = job.wacn;
                        seedArgs.systemId = job.systemId;
                        seedArgs.clearGrant = job.clearGrant;
                        seedArgs.encryptedGrant = job.encryptedGrant;
                        seedArgs.fieldAudioProbe = false;
                        {
                            std::lock_guard<std::mutex> lk(state->mutex);
                            if (job.reset ||
                                state->resetRequested.load(std::memory_order_acquire) ||
                                state->activeKey != replayKey || !state->rx) {
                                state->rx = std::make_unique<Receiver>();
                                state->pendingSpeaker.clear();
                                state->speakerQueue = P25Phase2SpeakerPendingQueue{};
                                state->activeKey = replayKey;
                                state->windows = 0;
                                state->emitWindows = 0;
                                state->gatedRawWindows = 0;
                                state->emptyWindows = 0;
                                state->decodedFrames = 0;
                                state->speakerSamples = 0;
                                state->speakerDrainEvents = 0;
                                state->speakerDrainSamples = 0;
                                state->speakerDroppedTailSamples = 0;
                                state->fedFrames = 0;
                                state->emittedPcmFrames = 0;
                                state->feedGaps = 0;
                                state->targetVoiceCodewords = 0;
                                state->oppositeVoiceCodewords = 0;
                                state->rejectedVoiceCodewords = 0;
                                state->inputQualityRejectedVoiceCodewords = 0;
                                state->hardTargetAcquire = false;
                                state->macEssStarveWindows = 0;
                                state->wideReacquireHoldWindows = 0;
                                state->forceMaskEpochRehunt = false;
                                state->maskEpochRepairHoldWindows = 0;
                                state->emptyEyeWindows = 0;
                                state->streamEndMs = decodeStreamEndMs;
                                {
                                    std::lock_guard<std::mutex> statusLock(guiIqReplayStatusMutex);
                                    guiIqReplayLastStatus.clear();
                                    guiIqReplayRecentStatus.clear();
                                }
                                state->wav.close();
                                state->lastWavPath.clear();
                                state->resetRequested.store(false, std::memory_order_release);
                                configureReceiver(*state->rx, seedArgs, job, capture);
                            }
                            if (!job.wavPath.isEmpty() && state->lastWavPath != job.wavPath) {
                                state->wav.close();
                                QString wavError;
                                if (state->wav.open(job.wavPath, 48000u, &wavError)) {
                                    state->lastWavPath = job.wavPath;
                                }
                            }

                            const size_t contextSamples = (capture.sampleRateHz > 0.0 && plannedContextMs > 0.0)
                                ? std::min(capture.iq.size(), static_cast<size_t>(
                                      std::max(0.0, capture.sampleRateHz * plannedContextMs / 1000.0 + 0.5)))
                                : 0u;
                            const uint64_t absStart = capture.firstSampleOffset;
                            const bool streamRealtime = true;
                            const bool streamColdWindow =
                                streamRealtime &&
                                (state->windows == 0 ||
                                 !state->hardTargetAcquire ||
                                 state->wideReacquireHoldWindows > 0 ||
                                 state->maskEpochRepairHoldWindows > 0);
                            const bool streamHotWindow =
                                streamRealtime && !streamColdWindow && state->windows > 0;
                            const bool streamLockOnlyWindow =
                                streamHotWindow &&
                                state->hardTargetAcquire &&
                                state->rx->p25VoiceLiveDecoder.config().enableStreamingChannelDdc &&
                                (state->rx->p25VoiceLiveDecoder.cqpskLockValid() ||
                                 state->speakerSamples > 0);
                            const int priorDecodeBudgetMs =
                                state->rx->p25VoiceLiveDecoder.config().realtimeDecodeBudgetMs;
                            const size_t priorCqpskCandidates =
                                state->rx->p25VoiceLiveDecoder.config().maxCqpskSearchCandidates;
                            const size_t priorPhase2SyncHits =
                                state->rx->p25VoiceLiveDecoder.config().maxPhase2SyncHits;
                            const size_t priorPhase2Locks =
                                state->rx->p25VoiceLiveDecoder.config().maxPhase2SuperframeLocks;
                            auto boundedConfigValue = [](size_t current, size_t cap) {
                                return current == 0 ? cap : std::min(current, cap);
                            };
                            if (streamColdWindow) {
                                state->rx->p25VoiceLiveDecoder.setRealtimeDecodeBudgetMs(
                                    std::min(priorDecodeBudgetMs, kP25VoiceWorkerColdRealtimeBudgetMs));
                                state->rx->p25VoiceLiveDecoder.setMaxCqpskSearchCandidates(
                                    boundedConfigValue(priorCqpskCandidates, kP25VoiceWorkerColdMaxCqpskCandidates));
                            } else if (streamHotWindow) {
                                // GUI IQ replay dialog — file source, replay caps.
                                state->rx->p25VoiceLiveDecoder.setRealtimeDecodeBudgetMs(
                                    std::min(priorDecodeBudgetMs, kP25ReplayHotBudgetMs));
                                state->rx->p25VoiceLiveDecoder.setMaxCqpskSearchCandidates(
                                    streamLockOnlyWindow ? kP25LiveLockedStreamCqpskCandidates
                                                         : boundedConfigValue(priorCqpskCandidates,
                                                                              kP25ReplayHotCqpskCandidates));
                                state->rx->p25VoiceLiveDecoder.setMaxPhase2SyncHits(
                                    boundedConfigValue(priorPhase2SyncHits, kP25ReplayHotSyncHits));
                                state->rx->p25VoiceLiveDecoder.setMaxPhase2SuperframeLocks(
                                    boundedConfigValue(priorPhase2Locks, kP25ReplayHotSuperframeLocks));
                            }
                            const bool forceWideReacquireDecode = state->wideReacquireHoldWindows > 0;
                            if (state->forceMaskEpochRehunt) {
                                state->rx->p25VoiceLiveDecoder.invalidatePhase2StickyMaskEpoch();
                                state->forceMaskEpochRehunt = false;
                            }
                            if (forceWideReacquireDecode) {
                                if (state->rx->p25VoiceDiagnostics.phase2Bursts == 0 &&
                                    state->rx->p25VoiceDiagnostics.phase2MaskedBursts == 0 &&
                                    state->rx->p25VoiceDiagnostics.phase2TargetVoiceCodewords == 0) {
                                    state->rx->p25VoiceLiveDecoder.reset();
                                    if (state->rx->p25VoiceMaskParamsKnown) {
                                        state->rx->p25VoiceLiveDecoder.setPhase2MaskParameters(
                                            state->rx->p25VoiceNac,
                                            state->rx->p25VoiceWacn,
                                            state->rx->p25VoiceSystemId);
                                    }
                                    state->rx->p25VoiceLiveDecoder.setPhase2PreferredTdmaSlot(
                                        state->rx->p25VoiceTdmaSlotKnown,
                                        static_cast<uint8_t>(state->rx->p25VoiceTdmaSlot & 0x01u));
                                }
                                --state->wideReacquireHoldWindows;
                            }
                            if (state->maskEpochRepairHoldWindows > 0) {
                                --state->maskEpochRepairHoldWindows;
                            }
                            auto audio = decodeP25VoiceAudioBlock(*state->rx, capture.iq, capture.sampleRateHz,
                                                                  voiceCenterHz, targetHz, 48000.0,
                                                                  absStart, true, contextSamples);
                            if (streamColdWindow || streamHotWindow) {
                                state->rx->p25VoiceLiveDecoder.setRealtimeDecodeBudgetMs(priorDecodeBudgetMs);
                                state->rx->p25VoiceLiveDecoder.setMaxCqpskSearchCandidates(priorCqpskCandidates);
                                state->rx->p25VoiceLiveDecoder.setMaxPhase2SyncHits(priorPhase2SyncHits);
                                state->rx->p25VoiceLiveDecoder.setMaxPhase2SuperframeLocks(priorPhase2Locks);
                            }
                            if (p25Phase2TargetHardClearEvidence(audio) ||
                                audio.phase2TargetMacCrcValid ||
                                audio.decodedFrames > 0 ||
                                !audio.audio.empty()) {
                                state->hardTargetAcquire = true;
                                state->macEssStarveWindows = 0;
                                state->wideReacquireHoldWindows = 0;
                                state->forceMaskEpochRehunt = false;
                                state->maskEpochRepairHoldWindows = 0;
                                state->emptyEyeWindows = 0;
                            } else if (p25Phase2MacEssStarvedVoiceWindow(audio)) {
                                state->macEssStarveWindows = std::min(state->macEssStarveWindows + 1, 1000);
                                if (state->macEssStarveWindows >= 2) {
                                    state->forceMaskEpochRehunt = true;
                                    state->maskEpochRepairHoldWindows =
                                        std::max(state->maskEpochRepairHoldWindows, 3);
                                }
                            } else if (state->hardTargetAcquire &&
                                       audio.phase2Bursts == 0 &&
                                       audio.phase2MaskedBursts == 0 &&
                                       audio.phase2TargetVoiceCodewords == 0 &&
                                       audio.decodedFrames == 0 &&
                                       audio.audio.empty()) {
                                state->emptyEyeWindows = std::min(state->emptyEyeWindows + 1, 1000);
                                if (state->emptyEyeWindows >= 2) {
                                    state->maskEpochRepairHoldWindows =
                                        std::max(state->maskEpochRepairHoldWindows, 3);
                                }
                            } else if (audio.phase2TargetVoiceCodewords == 0 &&
                                       audio.phase2ExpectedVoiceCodewords == 0 &&
                                       audio.phase2DiagnosticAmbeProbeAttempts == 0) {
                                state->macEssStarveWindows = 0;
                                if (audio.phase2Bursts > 0) {
                                    state->emptyEyeWindows = 0;
                                }
                            } else if (audio.phase2Bursts > 0) {
                                state->emptyEyeWindows = 0;
                            }
                            const std::string speakerGate = audio.phase2SpeakerGateReason.empty()
                                ? p25VoiceBlockSpeakerGateReason(audio)
                                : audio.phase2SpeakerGateReason;
                            const bool speakerMayEmit =
                                speakerGate == "emit" &&
                                p25VoiceBlockHasSpeakerTimelineAudio(audio) &&
                                audio.phase2EmittedPcmFrames > 0 &&
                                p25VoiceBlockMayEmitAudio(audio);
                            size_t pushed = 0;
                            size_t speakerPcmSamples = 0;
                            if (speakerMayEmit) {
                                ++state->emitWindows;
                                std::vector<float> pushedRealAudio;
                                std::vector<float> speakerAudioForQueue;
                                const bool speakerOutputActive =
                                    audioEngine && audioEngine->activeOutputCount() > 0;
                                const double outRate = speakerOutputActive
                                    ? std::max(8000.0, static_cast<double>(audioEngine->getSampleRate()))
                                    : 48000.0;
                                const size_t phase2FrameSamples = std::max<size_t>(160,
                                    static_cast<size_t>(outRate * 0.020 + 0.5));
                                speakerAudioForQueue = p25Phase2SpeakerAudioForQueue(
                                    state->speakerQueue, audio, audio.audio, phase2FrameSamples);
                                const bool hasPlayableNewPcm = !speakerAudioForQueue.empty();
                                if (speakerOutputActive) {
                                    pushed = pushP25SpeakerAudio(audioEngine, state->pendingSpeaker,
                                                                 speakerAudioForQueue, {},
                                                                 audioEngine->getRingFillPercent(),
                                                                 !hasPlayableNewPcm,
                                                                 &pushedRealAudio);
                                }
                                const std::vector<float>* speakerPcm =
                                    !pushedRealAudio.empty()
                                        ? &pushedRealAudio
                                        : (!speakerOutputActive && !speakerAudioForQueue.empty() ? &speakerAudioForQueue : nullptr);
                                if (speakerPcm && !speakerPcm->empty()) {
                                    speakerPcmSamples = speakerPcm->size();
                                    state->speakerSamples += static_cast<long long>(speakerPcm->size());
                                    if (state->wav.active()) state->wav.append(*speakerPcm);
                                    if (job.stt) {
                                        p25TranscriptTapSpeakerPcm(speakerPcm->data(), speakerPcm->size(),
                                                                  48000, audio.talkgroupId, targetHz, job.slot);
                                    }
                                    emitted = true;
                                }
                                p25Phase2UpdateSessionSustainState(
                                    *state->rx, audio, QDateTime::currentMSecsSinceEpoch(),
                                    // Match CLI voicetest: sustain lattice tracks speaker-gate
                                    // emit eligibility, not AudioEngine push success.
                                    speakerMayEmit);
                            } else if (!audio.audio.empty() && audio.decodedFrames > 0) {
                                ++state->gatedRawWindows;
                            } else {
                                ++state->emptyWindows;
                            }
                            ++state->windows;
                            state->decodedFrames += static_cast<long long>(audio.decodedFrames);
                            state->fedFrames += static_cast<long long>(audio.phase2FedToMbelib);
                            state->emittedPcmFrames += static_cast<long long>(audio.phase2EmittedPcmFrames);
                            state->feedGaps += static_cast<long long>(audio.phase2FeedGaps);
                            state->targetVoiceCodewords +=
                                static_cast<long long>(audio.phase2TargetVoiceCodewords);
                            state->oppositeVoiceCodewords +=
                                static_cast<long long>(audio.phase2OppositeVoiceCodewords);
                            state->rejectedVoiceCodewords +=
                                static_cast<long long>(audio.phase2RejectedVoiceCodewords);
                            state->inputQualityRejectedVoiceCodewords +=
                                static_cast<long long>(audio.phase2InputQualityRejectedVoiceCodewords);
                            const double autoUnacquiredAdvanceMs =
                                kP25Phase2VoiceDecodeUnacquiredAcquireFreshSeconds * 1000.0;
                            const double advanceMs =
                                (job.hopMs <= 0 && !state->hardTargetAcquire)
                                    ? autoUnacquiredAdvanceMs
                                    : steadyFreshMs;
                            state->streamEndMs = decodeStreamEndMs + std::max(1.0, advanceMs);
                            const double coldMs = static_cast<double>(std::max(1, job.windowMs));
                            const double nextSpeakerContextMs =
                                std::min(coldMs, kP25Phase2VoiceDecodeSpeakerSustainOverlapSeconds * 1000.0);
                            const double nextSustainContextMs =
                                std::min(coldMs, kP25Phase2VoiceDecodeSustainOverlapSeconds * 1000.0);
                            const bool nextUnacquired =
                                !state->hardTargetAcquire ||
                                state->wideReacquireHoldWindows > 0 ||
                                state->maskEpochRepairHoldWindows > 0;
                            const double nextLookbackMs = nextUnacquired
                                ? coldMs
                                : std::min(coldMs,
                                    (state->speakerSamples > 0 ? nextSpeakerContextMs : nextSustainContextMs) +
                                    steadyFreshMs);
                            nextPos = std::min(sliderMax, std::max(0,
                                static_cast<int>(std::llround(state->streamEndMs - nextLookbackMs))));
                            guiIqReplayWindows.store(state->windows, std::memory_order_relaxed);
                            guiIqReplayDecodedFrames.store(state->decodedFrames, std::memory_order_relaxed);
                            if (emitted) {
                                guiIqReplayAudioOutputEvents.fetch_add(1, std::memory_order_relaxed);
                                guiIqReplayAudioOutputSamples.fetch_add(static_cast<long long>(speakerPcmSamples), std::memory_order_relaxed);
                                guiIqReplayLastOutputMs.store(QDateTime::currentMSecsSinceEpoch(), std::memory_order_relaxed);
                            }
                            const QString ess = audio.phase2TargetEssKnown
                                ? (audio.phase2TargetEssEncrypted ? QStringLiteral("enc") : QStringLiteral("clear"))
                                : QStringLiteral("unknown");
                            line = QString("pos=%1ms load=%2+%3ms end=%4ms ctx=%5ms next=%6ms center=%7MHz voiceCenter=%8MHz target=%9MHz tg=%10 slot=%11 diag=%12 decoded=%13 fed=%14 emitPcm=%15 speaker=%16 gate=%17 ess=%18 p2vcw=%19 targetVcw=%20 mac=%21/%22 pushed=%23 pending=%24 hard=%25 starve=%26 repair=%27")
                                .arg(job.startMs)
                                .arg(loadStartMs)
                                .arg(loadDurationMs)
                                .arg(decodeStreamEndMs, 0, 'f', 1)
                                .arg(plannedContextMs, 0, 'f', 1)
                                .arg(nextPos)
                                .arg(centerHz / 1e6, 0, 'f', 6)
                                .arg(voiceCenterHz / 1e6, 0, 'f', 6)
                                .arg(targetHz / 1e6, 0, 'f', 6)
                                .arg(audio.talkgroupId)
                                .arg(job.slot >= 0 ? QString::number(job.slot) : QStringLiteral("auto"))
                                .arg(QString::fromUtf8(p25VoiceDiagLabel(audio.diag)))
                                .arg(static_cast<qulonglong>(audio.decodedFrames))
                                .arg(static_cast<qulonglong>(audio.phase2FedToMbelib))
                                .arg(static_cast<qulonglong>(audio.phase2EmittedPcmFrames))
                                .arg(QString::fromStdString(speakerGate))
                                .arg(QString::fromStdString(audio.phase2SecurityGateAction))
                                .arg(ess)
                                .arg(static_cast<qulonglong>(audio.phase2VoiceCodewords))
                                .arg(static_cast<qulonglong>(audio.phase2TargetVoiceCodewords))
                                .arg(static_cast<qulonglong>(audio.phase2MacCrcValid))
                                .arg(static_cast<qulonglong>(audio.phase2MacPdus))
                                .arg(static_cast<qulonglong>(pushed))
                                .arg(static_cast<qulonglong>(state->pendingSpeaker.size()))
                                .arg(state->hardTargetAcquire ? QStringLiteral("yes") : QStringLiteral("no"))
                                .arg(state->macEssStarveWindows)
                                .arg(state->maskEpochRepairHoldWindows);
                        }
                    }
                } catch (const std::exception& ex) {
                    line = QString("decode exception: %1").arg(ex.what());
                } catch (...) {
                    line = "decode exception: unknown";
                }
                state->busy.store(false, std::memory_order_release);
                if (self) {
                    QMetaObject::invokeMethod(self, [=]() {
                        if (!dialogPtr) return;
                        appendReplayLog(line);
                        if (nextPos > posSlider->value()) {
                            posSlider->blockSignals(true);
                            posSlider->setValue(nextPos);
                            posSlider->blockSignals(false);
                            startSpin->setValue(nextPos);
                            updateTimeLabel();
                        }
                        if (state->playing &&
                            (nextPos >= posSlider->maximum() ||
                             (guiRuntimeConfig.iqReplayAutoPlay &&
                              posSlider->value() >= guiRuntimeConfig.iqReplayStartMs + guiRuntimeConfig.iqReplayDurationMs))) {
                            size_t pendingSpeakerSamples = 0;
                            {
                                std::lock_guard<std::mutex> lk(state->mutex);
                                pendingSpeakerSamples = state->pendingSpeaker.size();
                            }
                            if (pendingSpeakerSamples > 0) {
                                state->playing = false;
                                state->drainingTail.store(true, std::memory_order_release);
                                state->drainDeadlineMs.store(
                                    QDateTime::currentMSecsSinceEpoch() + 8000,
                                    std::memory_order_release);
                                playTimer->stop();
                                playBtn->setText("Draining");
                                appendReplayLog(QString("replay decode finished; draining pending speaker PCM=%1 samples")
                                    .arg(static_cast<qulonglong>(pendingSpeakerSamples)));
                                QTimer::singleShot(0, dialogPtr, [=]() {
                                    if (!dialogPtr) return;
                                    (*drainReplaySpeakerTail)();
                                });
                            } else {
                                finishReplay("replay-finished");
                            }
                        } else if (state->playing) {
                            // CLI voicetest is a tight loop; chain the next hop
                            // immediately instead of waiting for the 20ms timer.
                            QTimer::singleShot(0, dialogPtr, [=]() {
                                if (!dialogPtr || !state->playing) return;
                                if (state->busy.load(std::memory_order_acquire)) return;
                                (*decodeOne)(false);
                            });
                        }
                    }, Qt::QueuedConnection);
                }
            }).detach();
        };
        connect(playTimer, &QTimer::timeout, iqReplayDialog, [=]() {
            if (!state->playing || state->busy.load(std::memory_order_acquire)) return;
            (*decodeOne)(false);
        });

        connect(browseBtn, &QPushButton::clicked, iqReplayDialog, [=]() {
            QString selected = QFileDialog::getExistingDirectory(iqReplayDialog, "Open SigMF Capture");
            if (selected.isEmpty()) {
                selected = QFileDialog::getOpenFileName(iqReplayDialog, "Open SigMF Capture",
                    QString(), "SigMF (*.sigmf-meta *.sigmf-data);;All Files (*.*)");
            }
            if (!selected.isEmpty()) {
                pathEdit->setText(selected);
                refreshMetadata();
            }
        });
        connect(loadBtn, &QPushButton::clicked, iqReplayDialog, [=]() { refreshMetadata(); });
        connect(stepBtn, &QPushButton::clicked, iqReplayDialog, [=]() { (*decodeOne)(false); });
        connect(resetBtn, &QPushButton::clicked, iqReplayDialog, [=]() {
            resetDecoder();
            appendReplayLog("decoder reset");
        });
        connect(transcriptBtn, &QPushButton::clicked, this, [this]() { showTranscriptWindow(); });
        connect(closeBtn, &QPushButton::clicked, iqReplayDialog, &QDialog::close);
        connect(playBtn, &QPushButton::clicked, iqReplayDialog, [=]() {
            if (state->drainingTail.load(std::memory_order_acquire)) return;
            if (state->playing) {
                state->playing = false;
                playTimer->stop();
                playBtn->setText("Play");
                writeReplayResult("replay-paused");
                return;
            }
            if (!state->info.ok && !refreshMetadata()) return;
            state->playing = true;
            playBtn->setText("Pause");
            playTimer->start();
            (*decodeOne)(state->resetRequested.load(std::memory_order_acquire));
        });
        connect(posSlider, &QSlider::valueChanged, iqReplayDialog, [=](int v) {
            startSpin->blockSignals(true);
            startSpin->setValue(v);
            startSpin->blockSignals(false);
            updateTimeLabel();
            state->resetRequested.store(true, std::memory_order_release);
        });
        connect(startSpin, QOverload<int>::of(&QSpinBox::valueChanged), iqReplayDialog, [=](int v) {
            posSlider->setValue(v);
        });
        auto markReset = [state]() { state->resetRequested.store(true, std::memory_order_release); };
        connect(centerSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), iqReplayDialog, markReset);
        connect(targetSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), iqReplayDialog, markReset);
        connect(tgSpin, QOverload<int>::of(&QSpinBox::valueChanged), iqReplayDialog, markReset);
        connect(slotSpin, QOverload<int>::of(&QSpinBox::valueChanged), iqReplayDialog, markReset);
        connect(nacSpin, QOverload<int>::of(&QSpinBox::valueChanged), iqReplayDialog, markReset);
        connect(wacnEdit, &QLineEdit::textChanged, iqReplayDialog, markReset);
        connect(systemSpin, QOverload<int>::of(&QSpinBox::valueChanged), iqReplayDialog, markReset);
        connect(clearCheck, &QCheckBox::toggled, iqReplayDialog, [=](bool checked) {
            if (checked) encCheck->setChecked(false);
            state->resetRequested.store(true, std::memory_order_release);
        });
        connect(encCheck, &QCheckBox::toggled, iqReplayDialog, [=](bool checked) {
            if (checked) clearCheck->setChecked(false);
            state->resetRequested.store(true, std::memory_order_release);
        });
        connect(iqReplayDialog, &QObject::destroyed, this, [this, state]() {
            state->stopping.store(true, std::memory_order_release);
            {
                std::lock_guard<std::mutex> lk(state->mutex);
                state->wav.close();
            }
            iqReplayDialog = nullptr;
        });

        if (!pathEdit->text().trimmed().isEmpty()) {
            refreshMetadata();
        } else {
            updateTimeLabel();
        }
        iqReplayDialog->show();
        if (guiRuntimeConfig.iqReplayAutoPlay && !pathEdit->text().trimmed().isEmpty()) {
            QTimer::singleShot(350, iqReplayDialog, [=]() {
                if (!iqReplayDialog) return;
                if (!state->info.ok) refreshMetadata();
                state->playing = true;
                playBtn->setText("Pause");
                playTimer->start();
                (*decodeOne)(true);
            });
        }
    }

    size_t guiRuntimeDeviceIndex() const noexcept
    {
        return guiRuntimeConfig.deviceIndexSet ? guiRuntimeConfig.deviceIndex : 0u;
    }

    void recordGuiRuntimeError(const QString& message)
    {
        guiRuntimeStartupErrors << message;
        spdlog::warn("GUI runtime startup: {}", message.toStdString());
        appendP25LogLine("GUI startup: " + message);
        if (remoteDiagnosticsEnabled()) {
            QJsonObject payload = diagnosticsRuntimeSnapshot("gui-runtime-startup-error");
            payload["stage"] = "gui-runtime-startup";
            payload["message"] = message.left(500);
            remoteDiagnosticsSubmit("app.problem", "warn", payload);
        }
    }

    bool selectDefaultAudioOutputForGuiStartup(const char* reason)
    {
        if (guiRuntimeConfig.dryRun) {
            appendP25LogLine("GUI startup dry-run: default audio output selection requested but not opened.");
            return true;
        }

        AudioEngine* eng = getOrCreateAudioEngine();
        if (!eng) {
            recordGuiRuntimeError("Default audio output selection failed: audio engine unavailable.");
            return false;
        }

        try {
            const auto outputs = eng->enumeratePlaybackDevices();
            if (outputs.empty()) {
                recordGuiRuntimeError("Default audio output selection failed: no playback devices enumerated.");
                return false;
            }

            size_t selected = 0;
            for (size_t i = 0; i < outputs.size(); ++i) {
                if (outputs[i].isDefault) {
                    selected = i;
                    break;
                }
            }
            eng->setActiveOutputs({selected});
            eng->setMasterVolume(static_cast<float>(monitorMasterVolume));
            const QString names = QString::fromStdString(eng->getActiveDeviceNames());
            appendP25LogLine(QString("GUI startup selected default audio output for %1: %2")
                .arg(QString::fromUtf8(reason ? reason : "runtime startup"), names));
            if (statusBar()) statusBar()->showMessage(QString("Default audio output active: %1").arg(names), 3500);
            return true;
        } catch (const std::exception& ex) {
            recordGuiRuntimeError(QString("Default audio output selection exception: %1").arg(ex.what()));
        } catch (...) {
            recordGuiRuntimeError("Default audio output selection unknown exception.");
        }
        return false;
    }

    bool startGuiRuntimeDeviceAt(double freqHz, bool p25Defaults)
    {
        if (!std::isfinite(freqHz) || freqHz <= 0.0) {
            recordGuiRuntimeError("Device start skipped: invalid frequency.");
            return false;
        }

        const size_t devIndex = guiRuntimeDeviceIndex();
        {
            std::lock_guard<std::mutex> lk(monitorParamsMutex);
            currentMonitorFreq = freqHz;
            if (p25Defaults) {
                currentMonitorMode = DemodMode::NFM;
                autoDetectMode = false;
                monitorChannelBwHz = 12500.0;
                monitorLpfHz = 3000.0;
                monitorAudioLpfEnabled = false;
            }
        }
        syncMonitorVarsToReceiver(0);

        {
            std::lock_guard<std::mutex> lk(receiversMutex);
            ensureReceiver();
            if (!receivers.empty() && receivers[0]) {
                auto& rx = *receivers[0];
                std::lock_guard<std::mutex> rxLock(rx.stateMutex);
                rx.deviceIndex = devIndex;
                rx.freqHz = freqHz;
                if (p25Defaults) {
                    rx.mode = DemodMode::NFM;
                    rx.channelBwHz = 12500.0;
                    rx.lpfHz = 3000.0;
                    rx.audioLpfEnabled = false;
                    rx.squelchDb = -105.0;
                    rx.p25ControlChannelMute = true;
                }
                rx.p25Phase2AllowLateEntryAudioProbe =
                    p25Defaults && guiRuntimeConfig.p25LateEntryAudioProbe;
                rx.active = !guiRuntimeConfig.dryRun;
            }
        }

        if (guiRuntimeConfig.dryRun) {
            appendP25LogLine(QString("GUI startup dry-run: would start device %1 at %2MHz.")
                .arg(static_cast<qulonglong>(devIndex))
                .arg(freqHz / 1e6, 0, 'f', 5));
            return true;
        }

        auto& mgr = DeviceManager::instance();
        const auto devices = mgr.getDevices();
        if (devIndex >= devices.size()) {
            recordGuiRuntimeError(QString("Device start failed: device index %1 is not available.")
                .arg(static_cast<qulonglong>(devIndex)));
            return false;
        }

        try {
            mgr.setEnabled(devIndex, true);
            const bool started = mgr.isStreaming(devIndex) || mgr.startStreaming(devIndex, true);
            mgr.setCenterFreq(devIndex, freqHz);
            appendP25LogLine(QString("GUI startup tuned device %1 to %2MHz start=%3.")
                .arg(static_cast<qulonglong>(devIndex))
                .arg(freqHz / 1e6, 0, 'f', 5)
                .arg(started ? "yes" : "pending"));
            if (statusBar()) {
                statusBar()->showMessage(QString("GUI startup tuned device %1 to %2 MHz")
                    .arg(static_cast<qulonglong>(devIndex))
                    .arg(freqHz / 1e6, 0, 'f', 5), 3500);
            }
            return true;
        } catch (const std::exception& ex) {
            recordGuiRuntimeError(QString("Device start exception: %1").arg(ex.what()));
        } catch (...) {
            recordGuiRuntimeError("Device start unknown exception.");
        }
        return false;
    }

    bool armGuiRuntimeP25Control(double ccHz, bool grantTest)
    {
        if (!std::isfinite(ccHz) || ccHz <= 0.0) {
            recordGuiRuntimeError("P25 monitor skipped: invalid control-channel frequency.");
            return false;
        }

        if (!startGuiRuntimeDeviceAt(ccHz, true)) return false;

        p25MonitoredControlFreqHz = ccHz;
        p25AutoFollowReturnControlFreqHz = ccHz;
        p25AutoFollowVoiceFreqHz = 0.0;
        p25AutoFollowTunedAtMs = 0;
        p25AutoFollowLastGrantMs = 0;
        p25AutoFollowLastActiveMs = 0;
        p25FollowEnabled = false;
        p25FollowAutoActive = false;
        p25FollowTalkgroupId = 0;
        p25AutoFollowEnabled = guiRuntimeConfig.autoFollow || grantTest;
        if (grantTest) {
            p25IndependentTrafficEnabled = true;
        }
        p25IndependentTrafficActive = false;
        p25IndependentTrafficRetunedPrimary = false;
        p25LiveDecoder.reset();
        p25ControlWorkerResetPending.store(true, std::memory_order_release);
        {
            std::lock_guard<std::mutex> pendingLock(p25ControlPendingMutex);
            p25ControlPendingResult.reset();
        }
        p25LiveControlAnalyzer.reset();
        p25PendingVoiceGrants.clear();
        p25RepeatedVoiceGrants.clear();
        p25LastDiagSignature.clear();

        {
            std::lock_guard<std::mutex> lk(receiversMutex);
            ensureReceiver();
            if (!receivers.empty() && receivers[0]) {
                auto& rx = *receivers[0];
                std::lock_guard<std::mutex> rxLock(rx.stateMutex);
                clearP25VoiceFollowFieldsLocked(rx, true);
                rx.deviceIndex = guiRuntimeDeviceIndex();
                rx.freqHz = ccHz;
                rx.mode = DemodMode::NFM;
                rx.channelBwHz = 12500.0;
                rx.lpfHz = 3000.0;
                rx.audioLpfEnabled = false;
                rx.squelchDb = -105.0;
                rx.p25Phase2AllowLateEntryAudioProbe = guiRuntimeConfig.p25LateEntryAudioProbe;
                rx.active = !guiRuntimeConfig.dryRun;
                if (!guiRuntimeConfig.dryRun) {
                    (void)tryApplyP25VoiceResetLocked(rx);
                }
            }
        }

        if (p25AutoFollowCheckBox) {
            p25AutoFollowCheckBox->blockSignals(true);
            p25AutoFollowCheckBox->setChecked(p25AutoFollowEnabled);
            p25AutoFollowCheckBox->blockSignals(false);
        }
        if (p25IndependentTrafficCheckBox && grantTest) {
            p25IndependentTrafficCheckBox->blockSignals(true);
            p25IndependentTrafficCheckBox->setChecked(true);
            p25IndependentTrafficCheckBox->blockSignals(false);
        }
        if (p25StatusLabel) {
            p25StatusLabel->setText(grantTest
                ? QString("Grant test %1 MHz").arg(ccHz / 1e6, 0, 'f', 5)
                : QString("Monitoring CC %1 MHz").arg(ccHz / 1e6, 0, 'f', 5));
        }

        upsertP25KnownControlChannel(ccHz, "GUI runtime startup");
        const size_t seededIdentifiers = seedP25AnalyzerFromCachedChannelIdentifiers(p25LiveControlAnalyzer, ccHz);
        appendP25LogLine(QString("%1 muted P25 control channel %2MHz autoFollow=%3 dryRun=%4.")
            .arg(grantTest ? "GUI startup grant-test armed on" : "GUI startup monitoring")
            .arg(ccHz / 1e6, 0, 'f', 5)
            .arg(p25AutoFollowEnabled ? "on" : "off")
            .arg(guiRuntimeConfig.dryRun ? "yes" : "no"));
        if (grantTest) {
            appendP25LogLine(QString("GUI grant-test traffic source forced on; late-entry audio probe=%1.")
                .arg(guiRuntimeConfig.p25LateEntryAudioProbe ? "on" : "off"));
        }
        if (seededIdentifiers > 0) {
            appendP25LogLine(QString("Seeded %1 cached P25 channel identifier table(s) for GUI startup.")
                .arg(static_cast<qulonglong>(seededIdentifiers)));
        }
        if (grantTest || guiRuntimeConfig.openP25Log) showP25LogWindow();
        return true;
    }

    QString guiRuntimeSelfTestPath() const
    {
        if (!guiRuntimeConfig.selfTestPath.empty()) return QString::fromStdString(guiRuntimeConfig.selfTestPath);
        const QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        return root + "/logs/gui_startup_selftest.json";
    }

    bool guiRuntimeClearAudioDetected() const noexcept
    {
        const long long p25Frames =
            std::max(guiP25AudioDecodedFrames.load(std::memory_order_relaxed),
                     guiP25AudioAcceptedAmbeFrames.load(std::memory_order_relaxed));
        const long long p25Samples = guiP25AudioOutputSamples.load(std::memory_order_relaxed);
        const long long replayFrames = guiIqReplayDecodedFrames.load(std::memory_order_relaxed);
        const long long replaySamples = guiIqReplayAudioOutputSamples.load(std::memory_order_relaxed);
        return (p25Frames >= kGuiP25ClearAudioMinAcceptedFrames &&
                p25Samples >= kGuiP25ClearAudioMinSamples) ||
            (replayFrames >= kGuiP25ClearAudioMinAcceptedFrames &&
             replaySamples >= kGuiP25ClearAudioMinSamples);
    }

    void writeGuiRuntimeSelfTestResult(const char* phase)
    {
        if (!guiRuntimeConfig.selfTest) return;
        try {
            json record;
            record["schema"] = "sdr-town-gui-runtime-startup-v1";
            record["phase"] = phase ? phase : "unknown";
            const qint64 snapshotMs = QDateTime::currentMSecsSinceEpoch();
            record["timeUtc"] = QDateTime::fromMSecsSinceEpoch(snapshotMs, Qt::UTC).toString(Qt::ISODateWithMs).toStdString();
            record["arguments"] = {
                {"requested", guiRuntimeConfig.requested},
                {"dryRun", guiRuntimeConfig.dryRun},
                {"startDevice", guiRuntimeConfig.startDevice},
                {"deviceIndex", guiRuntimeDeviceIndex()},
                {"frequencyHz", guiRuntimeConfig.frequencyHz},
                {"p25ControlHz", guiRuntimeConfig.p25ControlHz},
                {"p25Monitor", guiRuntimeConfig.p25Monitor},
                {"p25GrantTest", guiRuntimeConfig.p25GrantTest},
                {"autoFollow", guiRuntimeConfig.autoFollow},
                {"defaultAudio", guiRuntimeConfig.defaultAudio},
                {"iqCapture", guiRuntimeConfig.iqCapture},
                {"iqCaptureDurationMs", guiRuntimeConfig.iqCaptureDurationMs},
                {"iqCaptureLabel", guiRuntimeConfig.iqCaptureLabel},
                {"iqCaptureRoot", guiRuntimeConfig.iqCaptureRoot},
                {"iqReplay", guiRuntimeConfig.iqReplay},
                {"iqReplayPath", guiRuntimeConfig.iqReplayPath},
                {"iqReplayAutoPlay", guiRuntimeConfig.iqReplayAutoPlay},
                {"iqReplayTargetHz", guiRuntimeConfig.iqReplayTargetHz},
                {"iqReplayCenterHz", guiRuntimeConfig.iqReplayCenterHz},
                {"iqReplayVoiceCenterHz", guiRuntimeConfig.iqReplayVoiceCenterHz},
                {"iqReplayStartMs", guiRuntimeConfig.iqReplayStartMs},
                {"iqReplayDurationMs", guiRuntimeConfig.iqReplayDurationMs},
                {"iqReplayWindowMs", guiRuntimeConfig.iqReplayWindowMs},
                {"iqReplayHopMs", guiRuntimeConfig.iqReplayHopMs},
                {"iqReplayTalkgroup", guiRuntimeConfig.iqReplayTalkgroup},
                {"iqReplaySlot", guiRuntimeConfig.iqReplaySlot},
                {"iqReplayClearGrant", guiRuntimeConfig.iqReplayClearGrant},
                {"iqReplayEncryptedGrant", guiRuntimeConfig.iqReplayEncryptedGrant},
                {"p25LateEntryAudioProbe", guiRuntimeConfig.p25LateEntryAudioProbe},
                {"requireClearAudio", guiRuntimeConfig.requireClearAudio},
                {"clearAudioTimeoutMs", guiRuntimeConfig.clearAudioTimeoutMs},
            };
            record["warnings"] = guiRuntimeConfig.warnings;
            record["errors"] = json::array();
            for (const auto& err : guiRuntimeStartupErrors) record["errors"].push_back(err.toStdString());

            double monitorHz = 0.0;
            double monitorBw = 0.0;
            bool monitorLpfEnabled = false;
            {
                std::lock_guard<std::mutex> lk(monitorParamsMutex);
                monitorHz = currentMonitorFreq;
                monitorBw = monitorChannelBwHz;
                monitorLpfEnabled = monitorAudioLpfEnabled;
            }

            auto& mgr = DeviceManager::instance();
            const auto devices = mgr.getDevices();
            const size_t devIndex = guiRuntimeDeviceIndex();
            bool streaming = devIndex < devices.size() ? mgr.isStreaming(devIndex) : false;
            record["device"] = {
                {"index", devIndex},
                {"availableCount", devices.size()},
                {"streaming", streaming},
                {"runtimeState", devIndex < devices.size() ? mgr.getRuntimeStateLabel(devIndex) : std::string("missing")},
            };
            record["monitor"] = {
                {"frequencyHz", monitorHz},
                {"mode", modeToString(currentMonitorMode)},
                {"channelBwHz", monitorBw},
                {"audioLpfEnabled", monitorLpfEnabled},
            };
            record["p25"] = {
                {"controlHz", p25MonitoredControlFreqHz},
                {"autoFollowEnabled", p25AutoFollowEnabled},
                {"followEnabled", p25FollowEnabled},
                {"followAutoActive", p25FollowAutoActive},
                {"followTalkgroupId", p25FollowTalkgroupId},
                {"voiceHz", p25AutoFollowVoiceFreqHz},
                {"returnControlHz", p25AutoFollowReturnControlFreqHz},
                {"lastGrantMs", p25AutoFollowLastGrantMs},
                {"lastGrantAgeMs", p25AutoFollowLastGrantMs > 0 ? snapshotMs - p25AutoFollowLastGrantMs : -1},
                {"lastActiveMs", p25AutoFollowLastActiveMs},
                {"lastActiveAgeMs", p25AutoFollowLastActiveMs > 0 ? snapshotMs - p25AutoFollowLastActiveMs : -1},
                {"independentTrafficEnabled", p25IndependentTrafficEnabled},
                {"independentTrafficActive", p25IndependentTrafficActive},
                {"independentTrafficRetunedPrimary", p25IndependentTrafficRetunedPrimary},
                {"lateEntryAudioProbe", guiRuntimeConfig.p25LateEntryAudioProbe},
            };

            json rxRows = json::array();
            {
                std::lock_guard<std::mutex> lk(receiversMutex);
                for (const auto& ptr : receivers) {
                    if (!ptr) continue;
                    const auto& rx = *ptr;
                    std::lock_guard<std::mutex> rxLock(rx.stateMutex);
                    rxRows.push_back({
                        {"deviceIndex", rx.deviceIndex},
                        {"freqHz", rx.freqHz},
                        {"active", rx.active},
                        {"mode", modeToString(rx.mode)},
                        {"p25IndependentTrafficSource", rx.p25IndependentTrafficSource},
                        {"p25TrafficControlHz", rx.p25TrafficControlFreqHz},
                        {"p25TrafficVoiceHz", rx.p25TrafficVoiceFreqHz},
                        {"p25TrafficSlot", rx.p25TrafficSlot},
                        {"p25ControlMute", rx.p25ControlChannelMute},
                        {"p25VoiceDecodeEnabled", rx.p25VoiceDecodeEnabled},
                        {"p25VoiceTalkgroupId", rx.p25VoiceTalkgroupId},
                        {"p25VoiceSourceId", rx.p25VoiceSourceId},
                        {"p25VoiceGrantEpochMs", QString::number(rx.p25VoiceGrantEpochMs).toStdString()},
                        {"p25CurrentCallSessionId", QString::number(rx.p25CurrentCallSessionId).toStdString()},
                        {"p25PttGeneration", QString::number(rx.p25PttGeneration).toStdString()},
                        {"p25VoicePhase2", rx.p25VoicePhase2},
                        {"p25VoiceClearKnown", rx.p25VoiceClearKnown},
                        {"p25VoiceEncrypted", rx.p25VoiceEncrypted},
                        {"p25VoiceSlotKnown", rx.p25VoiceTdmaSlotKnown},
                        {"p25VoiceSlot", rx.p25VoiceTdmaSlot},
                        {"p25VoiceMaskParamsKnown", rx.p25VoiceMaskParamsKnown},
                        {"p25LateEntryAudioProbe", rx.p25Phase2AllowLateEntryAudioProbe},
                    });
                }
            }
            record["receivers"] = std::move(rxRows);

            AudioEngine* eng = engineForAudio.get();
            record["audio"] = {
                {"engineCreated", eng != nullptr},
                {"activeOutputCount", eng ? eng->activeOutputCount() : 0u},
                {"activeOutputNames", eng ? eng->getActiveDeviceNames() : std::string()},
            };
            const long long p25AudioEvents = guiP25AudioOutputEvents.load(std::memory_order_relaxed);
            const long long p25AudioSamples = guiP25AudioOutputSamples.load(std::memory_order_relaxed);
            const long long p25DecodedFrames = guiP25AudioDecodedFrames.load(std::memory_order_relaxed);
            const long long p25AcceptedAmbeFrames = guiP25AudioAcceptedAmbeFrames.load(std::memory_order_relaxed);
            const long long replayAudioEvents = guiIqReplayAudioOutputEvents.load(std::memory_order_relaxed);
            const long long replayAudioSamples = guiIqReplayAudioOutputSamples.load(std::memory_order_relaxed);
            const long long replayDecodedFrames = guiIqReplayDecodedFrames.load(std::memory_order_relaxed);
            const bool clearAudioDetected = guiRuntimeClearAudioDetected();
            record["clearAudio"] = {
                {"detected", clearAudioDetected},
                {"events", p25AudioEvents + replayAudioEvents},
                {"samples", p25AudioSamples + replayAudioSamples},
                {"p25Events", p25AudioEvents},
                {"p25Samples", p25AudioSamples},
                {"p25DecodedFrames", p25DecodedFrames},
                {"p25AcceptedAmbeFrames", p25AcceptedAmbeFrames},
                {"replayEvents", replayAudioEvents},
                {"replaySamples", replayAudioSamples},
                {"replayDecodedFrames", replayDecodedFrames},
                {"requiredAcceptedFrames", kGuiP25ClearAudioMinAcceptedFrames},
                {"requiredSamples", kGuiP25ClearAudioMinSamples},
                {"lastOutputMs", std::max(guiP25AudioLastOutputMs.load(std::memory_order_relaxed),
                                           guiIqReplayLastOutputMs.load(std::memory_order_relaxed))},
            };
            record["iqReplay"] = {
                {"active", guiRuntimeConfig.iqReplay},
                {"windows", guiIqReplayWindows.load(std::memory_order_relaxed)},
                {"audioEvents", guiIqReplayAudioOutputEvents.load(std::memory_order_relaxed)},
                {"audioSamples", guiIqReplayAudioOutputSamples.load(std::memory_order_relaxed)},
                {"decodedFrames", guiIqReplayDecodedFrames.load(std::memory_order_relaxed)},
                {"lastOutputMs", guiIqReplayLastOutputMs.load(std::memory_order_relaxed)},
            };
            {
                std::lock_guard<std::mutex> replayStatusLock(guiIqReplayStatusMutex);
                record["iqReplay"]["lastStatus"] = guiIqReplayLastStatus.toStdString();
                json recent = json::array();
                for (const QString& item : guiIqReplayRecentStatus) {
                    recent.push_back(item.toStdString());
                }
                record["iqReplay"]["recentStatus"] = std::move(recent);
            }
            record["iqCapture"] = {
                {"active", liveIqCapture.active},
                {"directory", liveIqCapture.directory.toStdString()},
                {"sampleCount", liveIqCapture.samplesWritten},
                {"bytesWritten", liveIqCapture.bytesWritten},
                {"ringOverrunSamples", liveIqCapture.ringOverrunSamples},
                {"fileWriteErrorPolls", liveIqCapture.fileWriteErrorPolls},
            };
            record["ok"] = guiRuntimeStartupErrors.isEmpty() &&
                (!guiRuntimeConfig.requireClearAudio || clearAudioDetected);

            const QString path = guiRuntimeSelfTestPath();
            const QFileInfo info(path);
            if (!info.absolutePath().isEmpty()) QDir().mkpath(info.absolutePath());
            std::ofstream out(path.toStdString(), std::ios::trunc);
            out << record.dump(2) << "\n";
            out.close();
            appendP25LogLine(QString("GUI startup self-test result written: %1").arg(path));
            if (!guiRuntimeConfig.iqReplayResultPath.empty() && guiRuntimeConfig.iqReplay) {
                const QString replayPath = QString::fromStdString(guiRuntimeConfig.iqReplayResultPath);
                appendP25LogLine(QString("GUI IQ replay result preserved: %1").arg(replayPath));
            } else if (!guiRuntimeConfig.iqReplayResultPath.empty()) {
                const QString replayPath = QString::fromStdString(guiRuntimeConfig.iqReplayResultPath);
                const QFileInfo replayInfo(replayPath);
                if (!replayInfo.absolutePath().isEmpty()) QDir().mkpath(replayInfo.absolutePath());
                std::ofstream replayOut(replayPath.toStdString(), std::ios::trunc);
                record["schema"] = "sdr-town-gui-iq-replay-v1";
                replayOut << record.dump(2) << "\n";
                appendP25LogLine(QString("GUI IQ replay result written: %1").arg(replayPath));
            }
        } catch (const std::exception& ex) {
            spdlog::warn("GUI startup self-test write failed: {}", ex.what());
        } catch (...) {
            spdlog::warn("GUI startup self-test write failed: unknown error");
        }
    }

    QString guiRuntimeCaptureLabel()
    {
        if (!guiRuntimeConfig.iqCaptureLabel.empty()) {
            return QString::fromStdString(guiRuntimeConfig.iqCaptureLabel);
        }
        double freqHz = guiRuntimeConfig.p25ControlHz > 0.0
            ? guiRuntimeConfig.p25ControlHz
            : guiRuntimeConfig.frequencyHz;
        DemodMode mode = currentMonitorMode;
        {
            std::lock_guard<std::mutex> lk(monitorParamsMutex);
            if (freqHz <= 0.0) freqHz = currentMonitorFreq;
            mode = currentMonitorMode;
        }
        return QString("iq_%1_%2MHz").arg(modeToQString(mode)).arg(freqHz / 1e6, 0, 'f', 5);
    }

    void scheduleGuiRuntimeIqCapture()
    {
        if (!guiRuntimeConfig.iqCapture) return;

        const int durationMs = std::clamp(
            guiRuntimeConfig.iqCaptureDurationMs > 0 ? guiRuntimeConfig.iqCaptureDurationMs : 300000,
            1000,
            3600000);
        const QString label = guiRuntimeCaptureLabel();

        if (guiRuntimeConfig.dryRun) {
            appendP25LogLine(QString("GUI startup dry-run: would run IQ capture label=\"%1\" duration=%2ms.")
                .arg(label)
                .arg(durationMs));
            return;
        }

        auto starter = std::make_shared<std::function<void(int)>>();
        std::weak_ptr<std::function<void(int)>> weakStarter = starter;
        *starter = [this, label, durationMs, weakStarter](int attempt) {
            const auto result = startLiveIqCapture(label.toStdString(), durationMs);
            if (result.ok) {
                appendP25LogLine(QString("GUI runtime IQ capture started: label=\"%1\" duration=%2ms dir=%3")
                    .arg(label)
                    .arg(durationMs)
                    .arg(result.directory));
                QTimer::singleShot(durationMs, this, [this]() {
                    const auto stopped = stopLiveIqCapture();
                    if (stopped.ok) {
                        appendP25LogLine(QString("GUI runtime IQ capture stopped: %1").arg(stopped.directory));
                    } else {
                        recordGuiRuntimeError(QString("GUI runtime IQ capture stop failed: %1").arg(stopped.message));
                    }
                    if (guiRuntimeConfig.selfTest) {
                        writeGuiRuntimeSelfTestResult(stopped.ok ? "iq-capture-stopped" : "iq-capture-stop-failed");
                        if (guiRuntimeConfig.exitAfterMs <= 0 && !guiRuntimeConfig.requireClearAudio) {
                            QCoreApplication::quit();
                        }
                    }
                });
                return;
            }

            const bool likelyWaitingForDevice = result.message.contains("No live spectrum", Qt::CaseInsensitive);
            if (likelyWaitingForDevice && attempt < 60) {
                if (auto retry = weakStarter.lock()) {
                    QTimer::singleShot(500, this, [retry, attempt]() { (*retry)(attempt + 1); });
                }
                return;
            }

            recordGuiRuntimeError(QString("GUI runtime IQ capture start failed: %1").arg(result.message));
            if (guiRuntimeConfig.selfTest) {
                writeGuiRuntimeSelfTestResult("iq-capture-start-failed");
                if (guiRuntimeConfig.exitAfterMs <= 0 && !guiRuntimeConfig.requireClearAudio) {
                    QCoreApplication::quit();
                }
            }
        };
        QTimer::singleShot(1500, this, [starter]() { (*starter)(0); });
    }

    void scheduleGuiRuntimeIqReplay()
    {
        if (!guiRuntimeConfig.iqReplay) return;
        if (guiRuntimeConfig.iqReplayPath.empty()) {
            QTimer::singleShot(700, this, [this]() {
                showIqReplayWindow();
                appendP25LogLine("GUI runtime IQ replay window opened without a capture path.");
            });
            return;
        }

        QTimer::singleShot(900, this, [this]() {
            showIqReplayWindow();
            appendP25LogLine(QString("GUI runtime IQ replay opened: path=\"%1\" target=%2MHz start=%3ms duration=%4ms autoplay=%5.")
                .arg(QString::fromStdString(guiRuntimeConfig.iqReplayPath))
                .arg(guiRuntimeConfig.iqReplayTargetHz > 0.0 ? guiRuntimeConfig.iqReplayTargetHz / 1e6 : 0.0, 0, 'f', 5)
                .arg(guiRuntimeConfig.iqReplayStartMs)
                .arg(guiRuntimeConfig.iqReplayDurationMs)
                .arg(guiRuntimeConfig.iqReplayAutoPlay ? "yes" : "no"));
        });
    }

    void scheduleGuiRuntimeSelfTest()
    {
        const bool shouldExit = guiRuntimeConfig.selfTest || guiRuntimeConfig.exitAfterMs > 0;
        if (guiRuntimeConfig.requireClearAudio) {
            const qint64 deadline = QDateTime::currentMSecsSinceEpoch() +
                std::max(1000, guiRuntimeConfig.clearAudioTimeoutMs);
            auto poll = std::make_shared<std::function<void()>>();
            std::weak_ptr<std::function<void()>> weakPoll = poll;
            *poll = [this, deadline, shouldExit, weakPoll]() {
                const bool detected = guiRuntimeClearAudioDetected();
                const bool timedOut = QDateTime::currentMSecsSinceEpoch() >= deadline;
                if (detected || timedOut) {
                    if (timedOut && !detected) {
                        recordGuiRuntimeError("Clear P25 audio was not detected before the GUI startup timeout.");
                    }
                    writeGuiRuntimeSelfTestResult(detected ? "clear-audio-detected" : "clear-audio-timeout");
                    if (shouldExit) QCoreApplication::quit();
                    return;
                }
                if (auto retry = weakPoll.lock()) {
                    QTimer::singleShot(250, this, [retry]() { (*retry)(); });
                }
            };
            QTimer::singleShot(250, this, [poll]() { (*poll)(); });
            return;
        }

        const int delayMs = guiRuntimeConfig.exitAfterMs > 0
            ? guiRuntimeConfig.exitAfterMs
            : (guiRuntimeConfig.selfTest ? 1800 : 0);
        if (delayMs > 0) {
            QTimer::singleShot(delayMs, this, [this, shouldExit]() {
                writeGuiRuntimeSelfTestResult("startup-applied");
                if (shouldExit) QCoreApplication::quit();
            });
        } else if (guiRuntimeConfig.selfTest) {
            writeGuiRuntimeSelfTestResult("startup-applied");
        }
    }

    void applyGuiRuntimeStartupConfig()
    {
        if (!guiRuntimeConfig.hasStartupWork()) return;
        guiRuntimeStartupAppliedMs = QDateTime::currentMSecsSinceEpoch();
        for (const auto& warning : guiRuntimeConfig.warnings) {
            recordGuiRuntimeError(QString::fromStdString(warning));
        }

        if (!guiRuntimeConfig.iqCaptureRoot.empty()) {
            const QString root = QDir::fromNativeSeparators(QString::fromStdString(guiRuntimeConfig.iqCaptureRoot)).trimmed();
            if (root.isEmpty() || !QDir().mkpath(root)) {
                recordGuiRuntimeError(QString("GUI capture root is not writable/creatable: %1").arg(root));
            } else {
                gIqTestCapturesRootOverride = root;
                appendP25LogLine(QString("GUI runtime IQ capture root override: %1").arg(root));
            }
        }

        appendP25LogLine(QString("GUI runtime startup applying: freq=%1MHz p25cc=%2MHz start=%3 autoFollow=%4 defaultAudio=%5 iqCapture=%6 lateEntryProbe=%7 dryRun=%8.")
            .arg(guiRuntimeConfig.frequencyHz > 0.0 ? guiRuntimeConfig.frequencyHz / 1e6 : 0.0, 0, 'f', 5)
            .arg(guiRuntimeConfig.p25ControlHz > 0.0 ? guiRuntimeConfig.p25ControlHz / 1e6 : 0.0, 0, 'f', 5)
            .arg(guiRuntimeConfig.startDevice ? "yes" : "no")
            .arg(guiRuntimeConfig.autoFollow ? "yes" : "no")
            .arg(guiRuntimeConfig.defaultAudio ? "yes" : "no")
            .arg(guiRuntimeConfig.iqCapture ? "yes" : "no")
            .arg(guiRuntimeConfig.p25LateEntryAudioProbe ? "yes" : "no")
            .arg(guiRuntimeConfig.dryRun ? "yes" : "no"));

        if (guiRuntimeConfig.defaultAudio) {
            selectDefaultAudioOutputForGuiStartup("GUI startup");
        }

        if (guiRuntimeConfig.p25Monitor || guiRuntimeConfig.p25GrantTest) {
            const double ccHz = guiRuntimeConfig.p25ControlHz > 0.0
                ? guiRuntimeConfig.p25ControlHz
                : guiRuntimeConfig.frequencyHz;
            armGuiRuntimeP25Control(ccHz, guiRuntimeConfig.p25GrantTest);
        } else if (guiRuntimeConfig.startDevice || guiRuntimeConfig.frequencyHz > 0.0) {
            if (guiRuntimeConfig.frequencyHz > 0.0) {
                startGuiRuntimeDeviceAt(guiRuntimeConfig.frequencyHz, false);
            } else {
                recordGuiRuntimeError("GUI device start requested without a frequency.");
            }
        }

        if (guiRuntimeConfig.autoFollow && !guiRuntimeConfig.p25Monitor && !guiRuntimeConfig.p25GrantTest) {
            p25AutoFollowEnabled = true;
            if (p25AutoFollowCheckBox) {
                p25AutoFollowCheckBox->blockSignals(true);
                p25AutoFollowCheckBox->setChecked(true);
                p25AutoFollowCheckBox->blockSignals(false);
            }
            appendP25LogLine("GUI startup enabled P25 auto-follow flag without changing the current control-channel monitor.");
        }

        if (guiRuntimeConfig.openP25Log && !guiRuntimeConfig.p25GrantTest) {
            showP25LogWindow();
        }

        scheduleGuiRuntimeIqCapture();
        scheduleGuiRuntimeIqReplay();
        scheduleGuiRuntimeSelfTest();
    }

    // Helper to ensure at least one receiver exists (transitional Phase 0)
    void ensureReceiver() {
        if (receivers.empty()) {
            auto r = std::make_shared<Receiver>();
            r->deviceIndex = 0;
            r->freqHz = currentMonitorFreq;
            r->mode = currentMonitorMode;
            r->channelBwHz = monitorChannelBwHz;
            r->lpfHz = monitorLpfHz;
            r->audioLpfEnabled = monitorAudioLpfEnabled;
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
            rx.p25VoiceDecodeEnabled = false;
            rx.p25VoiceClearKnown = false;
            rx.p25VoiceEncrypted = false;
            rx.p25VoiceTalkgroupId = 0;
            rx.p25VoicePhase2 = false;
            rx.p25VoiceTdmaSlotKnown = false;
            rx.p25VoiceTdmaSlot = 0;
            rx.p25VoiceSlotProbePending = false;
            rx.p25VoiceSlotProbeRequested = 0;
            rx.p25VoiceResetPending = false;
            rx.p25VoiceMaskParamsKnown = false;
            rx.p25VoiceNac = 0;
            rx.p25VoiceWacn = 0;
            rx.p25VoiceSystemId = 0;
            rx.p25VoiceSettleUntilMs = 0;
            rx.p25VoiceDiscardWindows = 0;
            rx.p25ControlChannelMute = false;
            p25ClearPhase2PendingAudio(rx);
            p25PendingAudioFlushSeq.fetch_add(1, std::memory_order_release);
            if (engineForAudio) engineForAudio->clearBuffers();
            rx.resetP25VoiceState();
            clearP25SessionScopedState(rx);
            p25FollowEnabled = false;
            p25FollowAutoActive = false;
            p25FollowTalkgroupId = 0;
            p25AutoFollowVoiceFreqHz = 0.0;
            p25AutoFollowTunedAtMs = 0;
            p25AutoFollowLastGrantMs = 0;
            p25AutoFollowLastActiveMs = 0;
            p25AutoFollowLastMHzHopMs = 0;
            guiP25AudioOutputEvents.store(0, std::memory_order_relaxed);
            guiP25AudioOutputSamples.store(0, std::memory_order_relaxed);
            guiP25AudioDecodedFrames.store(0, std::memory_order_relaxed);
            guiP25AudioAcceptedAmbeFrames.store(0, std::memory_order_relaxed);
            guiP25AudioLastOutputMs.store(0, std::memory_order_relaxed);
            gP25AudioLastSpeakerOutputMs.store(0, std::memory_order_relaxed);
            clearP25VoiceDiagnostics(rx);
        }

        rx.freqHz = currentMonitorFreq;
        rx.mode = currentMonitorMode;
        rx.channelBwHz = monitorChannelBwHz;
        rx.lpfHz = monitorLpfHz;
        rx.audioLpfEnabled = monitorAudioLpfEnabled;
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

    TrainingCaptureResult captureTrainingSample(const std::string& label) {
        TrainingCaptureRequest req;
        req.label = trimCopy(label);
        if (req.label.empty()) req.label = "unknown";

        {
            std::lock_guard<std::mutex> lk(monitorParamsMutex);
            req.tunedFreqHz = currentMonitorFreq;
            req.mode = currentMonitorMode;
            req.channelBwHz = monitorChannelBwHz;
            req.lpfHz = monitorLpfHz;
            req.audioLpfEnabled = monitorAudioLpfEnabled;
            req.squelchDb = monitorSquelchDb;
        }
        auto& mgr = DeviceManager::instance();
        std::vector<float> pwr;
        bool gotSpectrum = false;
        for (size_t i = 0; i < mgr.getDevices().size(); ++i) {
            if (mgr.isStreaming(i) && mgr.getLatestSpectrum(i, pwr, req.centerFreqHz, req.sampleRateHz) && !pwr.empty() && req.sampleRateHz > 0.0) {
                req.deviceIndex = i;
                req.spectrumDb = pwr;
                gotSpectrum = true;
                break;
            }
        }
        if (!gotSpectrum) {
            return {false, {}, "No live spectrum yet. Enable/tune a device and wait for the waterfall first."};
        }

        const auto devices = mgr.getDevices();
        if (req.deviceIndex < devices.size()) req.device = devices[req.deviceIndex];

        const size_t requestedSamples = static_cast<size_t>(std::clamp(req.sampleRateHz * 1.0, 16384.0, 2400000.0));
        req.iq = mgr.getRecentIQWindow(req.deviceIndex, requestedSamples);

        const double roiHz = std::clamp(
            std::max(req.channelBwHz * 4.0, req.channelBwHz >= 100000.0 ? 350000.0 : 50000.0),
            20000.0,
            req.sampleRateHz);
        req.tile = classifierRoiBuilder.buildTile(req.sampleRateHz, req.centerFreqHz, req.tunedFreqHz, roiHz, 256, 256);
        if (!req.tile.valid()) {
            WaterfallRoiBuilder oneFrame(1);
            oneFrame.pushSpectrum(req.spectrumDb);
            req.tile = oneFrame.buildTile(req.sampleRateHz, req.centerFreqHz, req.tunedFreqHz, roiHz, 256, 256);
        }
        auto modelRec = req.tile.valid()
            ? ClassifierModelBackend::instance().classifyTile(req.tile, req.sampleRateHz, req.centerFreqHz, req.tunedFreqHz, roiHz)
            : std::optional<SignalRecommendation>{};
        req.recommendation = modelRec.has_value()
            ? *modelRec
            : (req.tile.valid()
                ? AdvancedSignalClassifier::instance().classifyWaterfallTile(req.tile, req.sampleRateHz, req.centerFreqHz, req.tunedFreqHz, roiHz)
                : AdvancedSignalClassifier::instance().classifySpectrum(req.spectrumDb, req.sampleRateHz, req.centerFreqHz, req.tunedFreqHz));

        return saveTrainingCapture(req);
    }


    void writeLiveIqCaptureEvent(const json& row, bool flushNow = true) {
        if (!liveIqCapture.active || !liveIqCapture.events.is_open()) return;
        liveIqCapture.events << row.dump() << "\n";
        if (flushNow) liveIqCapture.events.flush();
    }

    LiveIqCaptureResult startLiveIqCapture(const std::string& label, int plannedDurationMs = 0) {
        LiveIqCaptureResult out;
        if (liveIqCapture.active) {
            out.message = "An IQ capture is already running. Stop it before starting a new one.";
            return out;
        }

        LiveIqCaptureSession session;
        session.label = trimCopy(label);
        if (session.label.empty()) session.label = "iq_capture";
        session.startedUtc = QDateTime::currentDateTimeUtc();
        session.lastPollUtc = session.startedUtc;
        session.lastSignalLevelDb = gLastRmsDb.load(std::memory_order_relaxed);
        session.lastNoiseFloorDb = gLastNoiseFloorDb.load(std::memory_order_relaxed);
        session.lastSnrDb = gLastSnrDb.load(std::memory_order_relaxed);
        session.lastAfcOffsetHz = gLastAfcOffsetHz.load(std::memory_order_relaxed);

        {
            std::lock_guard<std::mutex> lk(monitorParamsMutex);
            session.tunedFreqHz = currentMonitorFreq;
            session.mode = currentMonitorMode;
            session.channelBwHz = monitorChannelBwHz;
            session.lpfHz = monitorLpfHz;
            session.audioLpfEnabled = monitorAudioLpfEnabled;
            session.squelchDb = monitorSquelchDb;
        }
        session.sessionId = makeCaptureSessionId(session.startedUtc, session.label, session.tunedFreqHz);

        auto& mgr = DeviceManager::instance();
        std::vector<float> spectrum;
        bool gotSpectrum = false;
        for (size_t i = 0; i < mgr.getDevices().size(); ++i) {
            if (mgr.isStreaming(i) && mgr.getLatestSpectrum(i, spectrum, session.centerFreqHz, session.sampleRateHz) &&
                session.sampleRateHz > 0.0 && std::isfinite(session.sampleRateHz)) {
                session.deviceIndex = i;
                gotSpectrum = true;
                break;
            }
        }
        if (!gotSpectrum) {
            out.message = "No live spectrum/device state yet. Start/tune a device before starting IQ capture.";
            return out;
        }
        const auto devices = mgr.getDevices();
        if (session.deviceIndex < devices.size()) session.device = devices[session.deviceIndex];

        const auto cursorWindow = mgr.getRecentIQWindowWithCursor(session.deviceIndex, 1);
        session.startAbsolute = cursorWindow.endAbsolute;
        session.cursorAbsolute = cursorWindow.endAbsolute;
        session.endAbsolute = cursorWindow.endAbsolute;

        const QString root = iqTestCapturesRoot();
        QString storageMessage;
        if (!captureStorageReadyForStart(root, session.sampleRateHz, plannedDurationMs, &storageMessage)) {
            out.message = storageMessage;
            return out;
        }
        const QString stamp = session.startedUtc.toString("yyyyMMdd_HHmmss_zzz");
        const std::string labelToken = sanitizeFileToken(session.label);
        session.baseName = QString("%1_%2_%3MHz_startstop")
            .arg(stamp)
            .arg(QString::fromStdString(labelToken))
            .arg(session.tunedFreqHz / 1e6, 0, 'f', 5);
        session.directory = root + "/" + session.baseName;
        if (!QDir().mkpath(session.directory)) {
            out.message = "Could not create IQ capture directory.";
            return out;
        }
        const QString base = session.directory + "/" + session.baseName;
        session.dataPath = base + ".sigmf-data";
        session.metaPath = base + ".sigmf-meta";
        session.eventsPath = base + "_events.jsonl";
        session.p25TextPath = base + "_p25_log.txt";
        session.ringCsvPath = base + "_ring_health.csv";
        session.statusPath = base + "_live_status.json";
        session.summaryPath = base + "_summary.json";
        session.replayPath = base + "_replay.txt";

        session.data.open(session.dataPath.toStdString(), std::ios::binary);
        session.events.open(session.eventsPath.toStdString(), std::ios::app);
        session.ringCsv.open(session.ringCsvPath.toStdString(), std::ios::app);
        session.p25LogStream.open(session.p25TextPath.toStdString(), std::ios::out | std::ios::trunc);
        if (!session.data.is_open() || !session.events.is_open() || !session.ringCsv.is_open() || !session.p25LogStream.is_open()) {
            out.message = "Could not open one or more IQ capture output files.";
            return out;
        }
        session.ringCsv << "utc,poll,window_start_abs,window_end_abs,cursor_before_abs,append_start_abs,append_end_abs,samples_appended,gap_samples,total_written,bytes_written,zero_append_polls,max_single_gap_samples,file_write_error_polls,signal_level_db,noise_floor_db,snr_db,afc_offset_hz,ring_epoch_resets,ring_epoch_reset_skipped_samples\n";
        session.startP25LogSnapshot.clear();
        session.p25LogDuringCapture.clear();
        session.p25CaptureDroppedLines = 0;
        session.p25CaptureLinesWritten = 0;
        session.p25CaptureWriteErrors = 0;
        session.startP25LogIndex = static_cast<size_t>(p25LogLines.size());
        session.p25LogStream << "# SDR Town live P25/UI log for start/stop IQ capture\n";
        session.p25LogStream << "# version=" << SDR_TOWN_VERSION
                             << " p25_baseline=" << SDR_TOWN_P25_AUDIO_BASELINE << "\n";
        session.p25LogStream << "# capture_start_utc=" << session.startedUtc.toString(Qt::ISODateWithMs).toStdString() << "\n";
        session.p25LogStream << "# session_id=" << session.sessionId << "\n";
        session.p25LogStream << "# absolute_sample_start=" << session.startAbsolute << "\n";
        session.p25LogStream << "# NOTE: capture-time log lines are written on the fly; no RAM line buffer is used.\n";
        session.p25LogStream << "\n# Recent startup context retained by GUI at capture start\n";
        for (const QString& line : p25LogLines) {
            session.p25LogStream << line.toStdString() << "\n";
        }
        session.p25LogStream << "\n# Live log lines during capture\n";
        session.p25LogStream.flush();
        session.active = true;
        liveIqCapture = std::move(session);
        {
            std::lock_guard<std::mutex> lk(gCaptureP25PendingMutex);
            liveIqCapturePendingP25Lines.clear();
        }
        liveIqCaptureLogActive.store(true, std::memory_order_release);

        json startRow = {
            {"event", "capture_start"},
            {"utc", liveIqCapture.startedUtc.toString(Qt::ISODateWithMs).toStdString()},
            {"label", liveIqCapture.label},
            {"session_id", liveIqCapture.sessionId},
            {"freq_hz", liveIqCapture.tunedFreqHz},
            {"center_freq_hz", liveIqCapture.centerFreqHz},
            {"sample_rate_hz", liveIqCapture.sampleRateHz},
            {"device_index", liveIqCapture.deviceIndex},
            {"device_driver", liveIqCapture.device.driver},
            {"device_label", liveIqCapture.device.label},
            {"absolute_sample_start", liveIqCapture.startAbsolute},
            {"mode", modeToString(liveIqCapture.mode)}
        };
        writeLiveIqCaptureEvent(startRow);
        appendP25LogLine(QString("IQ capture START label=\"%1\" dir=%2 abs_start=%3 rate=%4Hz")
            .arg(QString::fromStdString(liveIqCapture.label))
            .arg(liveIqCapture.directory)
            .arg(liveIqCapture.startAbsolute)
            .arg(liveIqCapture.sampleRateHz, 0, 'f', 0));

        // Offload the capture poll + file I/O to a background thread so heavy ring pulls,
        // data writes, csv/events/p25 log flushes during P25 activity (returns, grants, logs)
        // do not block the GUI thread. Previous timer-driven poll on main thread + per-log
        // appends during returns caused freezes after a few cycles while capturing.
        stopLiveIqCaptureWriter.store(false, std::memory_order_release);
        if (liveIqCaptureWriterThread.joinable()) {
            stopLiveIqCaptureWriter.store(true, std::memory_order_release);
            liveIqCaptureWriterThread.join();
            stopLiveIqCaptureWriter.store(false, std::memory_order_release);
        }
        liveIqCaptureWriterThread = std::thread([this]() {
            spdlog::info("DEBUG: capture writer thread started, id=%llu", (unsigned long long)QThread::currentThreadId());
            while (!stopLiveIqCaptureWriter.load(std::memory_order_acquire)) {
                if (liveIqCapture.active) {
                    try {
                        pollLiveIqCapture(false);
                    } catch (...) {}
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(125));
            }
            spdlog::info("DEBUG: capture writer thread exiting");
        });

        // Keep a slow timer only if needed for other UI (none heavy now).
        if (liveIqCaptureTimer) liveIqCaptureTimer->stop();

        out.ok = true;
        out.directory = liveIqCapture.directory;
        out.message = "Started continuous IQ capture.";
        return out;
    }

    void pollLiveIqCapture(bool finalPoll) {
        if (!liveIqCapture.active) return;
        auto& mgr = DeviceManager::instance();
        const QDateTime nowUtc = QDateTime::currentDateTimeUtc();

        if (!finalPoll && !liveIqCapture.storageStopRequested) {
            qint64 availableBytes = -1;
            QString storageReason;
            const QString storagePath = !liveIqCapture.directory.trimmed().isEmpty()
                ? liveIqCapture.directory
                : iqTestCapturesRoot();
            if (captureStorageBelowStopReserve(storagePath, &availableBytes, &storageReason)) {
                liveIqCapture.storageStopRequested = true;
                liveIqCapture.storageStopReason = storageReason;
                liveIqCapture.storageStopAvailableBytes = availableBytes;
                liveIqCapture.storageStopReserveBytes = kCaptureStorageReserveBytes;
                json lowStorageRow = {
                    {"event", "capture_auto_stop_low_storage"},
                    {"utc", nowUtc.toString(Qt::ISODateWithMs).toStdString()},
                    {"available_bytes", availableBytes},
                    {"reserve_bytes", kCaptureStorageReserveBytes},
                    {"message", storageReason.toStdString()},
                    {"bytes_written", liveIqCapture.bytesWritten},
                    {"sample_count", liveIqCapture.samplesWritten}
                };
                writeLiveIqCaptureEvent(lowStorageRow, true);
                QMetaObject::invokeMethod(this, [this, storageReason]() {
                    appendP25LogLine(QString("IQ capture auto-stop requested: %1").arg(storageReason));
                }, Qt::QueuedConnection);
                QMetaObject::invokeMethod(this, [this]() {
                    if (!liveIqCapture.active || !liveIqCapture.storageStopRequested) return;
                    const QString reason = liveIqCapture.storageStopReason;
                    const auto stopped = stopLiveIqCapture();
                    appendP25LogLine(QString("IQ capture auto-stop %1: %2")
                        .arg(stopped.ok ? "completed" : "failed")
                        .arg(stopped.ok ? stopped.directory : stopped.message));
                    statusBar()->showMessage(QString("IQ capture stopped: %1").arg(reason), 15000);
                    if (remoteDiagnosticsEnabled()) {
                        QJsonObject payload = diagnosticsRuntimeSnapshot("capture-auto-stop-low-storage");
                        payload["stage"] = "iq-capture";
                        payload["pressureSummary"] = "low_capture_storage_free_space";
                        QJsonArray reasons;
                        reasons.append("low_capture_storage_free_space");
                        payload["pressureReasons"] = reasons;
                        payload["message"] = reason.left(240);
                        remoteDiagnosticsSubmit("app.performance.resource_pressure", "warn", payload);
                    }
                }, Qt::QueuedConnection);
                return;
            }
        }

        // Dynamic pull sizing: the previous fixed 0.40 s window kept the GUI
        // lighter, but a delayed timer tick could instantly create ring gaps.
        // Size the requested ring window from the real elapsed wall time since
        // the last poll, with headroom for brief GUI/disk stalls. Normal polls
        // stay modest; delayed polls automatically pull enough history to keep
        // the cursor gapless if the device ring still has it.
        const qint64 elapsedMsRaw = liveIqCapture.lastPollUtc.isValid()
            ? liveIqCapture.lastPollUtc.msecsTo(nowUtc)
            : 250;
        const double elapsedSeconds = std::clamp(static_cast<double>(std::max<qint64>(elapsedMsRaw, 50)) / 1000.0, 0.05, 2.0);
        // Normal polls still pull about 0.5s, but startup, retune, disk, or heavy
        // P25 decode can occasionally delay the writer close to a second. A 0.5s
        // hard cap made those recoverable delays look like ring overruns because
        // getRecentIQWindowWithCursor() returns the newest window. Allow a bounded
        // catch-up pull so long captures stay gapless without turning every poll
        // into a multi-megabyte ring copy.
        const double requestSeconds = std::clamp(elapsedSeconds * 2.0 + 0.25, 0.25, 2.0);
        const size_t maxPull = static_cast<size_t>(std::clamp(
            liveIqCapture.sampleRateHz * requestSeconds,
            4096.0,
            std::max(4096.0, liveIqCapture.sampleRateHz * 2.1)));
        const auto window = mgr.getRecentIQWindowWithCursor(liveIqCapture.deviceIndex, maxPull);
        const uint64_t cursorBefore = liveIqCapture.cursorAbsolute;
        uint64_t gapSamples = 0;
        uint64_t appendStartAbs = cursorBefore;
        uint64_t appendEndAbs = cursorBefore;
        size_t appended = 0;
        bool ringEpochReset = false;
        uint64_t ringEpochResetSkipped = 0;

        liveIqCapture.lastSignalLevelDb = gLastRmsDb.load(std::memory_order_relaxed);
        liveIqCapture.lastNoiseFloorDb = gLastNoiseFloorDb.load(std::memory_order_relaxed);
        liveIqCapture.lastSnrDb = gLastSnrDb.load(std::memory_order_relaxed);
        liveIqCapture.lastAfcOffsetHz = gLastAfcOffsetHz.load(std::memory_order_relaxed);

        if (!window.samples.empty() &&
            window.endAbsolute > window.startAbsolute &&
            liveIqCapture.cursorAbsolute > window.endAbsolute) {
            const uint64_t rewindSamples = liveIqCapture.cursorAbsolute - window.endAbsolute;
            const uint64_t resetThreshold = static_cast<uint64_t>(
                std::max(4096.0, liveIqCapture.sampleRateHz * 0.25));
            if (rewindSamples >= resetThreshold) {
                ringEpochReset = true;
                ringEpochResetSkipped = window.startAbsolute;
                ++liveIqCapture.ringEpochResets;
                liveIqCapture.ringEpochResetSkippedSamples += ringEpochResetSkipped;
                liveIqCapture.cursorAbsolute = window.startAbsolute;
                json resetRow = {
                    {"event", "ring_epoch_reset"},
                    {"utc", nowUtc.toString(Qt::ISODateWithMs).toStdString()},
                    {"poll", liveIqCapture.pollCount + 1},
                    {"cursor_before_abs", cursorBefore},
                    {"ring_window_start_abs", window.startAbsolute},
                    {"ring_window_end_abs", window.endAbsolute},
                    {"rewind_samples", rewindSamples},
                    {"skipped_samples_in_new_epoch", ringEpochResetSkipped},
                    {"ring_epoch_resets", liveIqCapture.ringEpochResets}
                };
                writeLiveIqCaptureEvent(resetRow, true);
                spdlog::info("IQ capture ring epoch reset after retune/restart: old_cursor=%llu new_window=%llu-%llu skipped=%llu",
                    (unsigned long long)cursorBefore, (unsigned long long)window.startAbsolute, (unsigned long long)window.endAbsolute, (unsigned long long)ringEpochResetSkipped);
            }
        }

        if (!window.samples.empty() && window.endAbsolute > liveIqCapture.cursorAbsolute) {
            uint64_t readStart = liveIqCapture.cursorAbsolute;
            if (readStart < window.startAbsolute) {
                gapSamples = window.startAbsolute - readStart;
                liveIqCapture.ringOverrunSamples += gapSamples;
                liveIqCapture.maxSingleGapSamples = std::max<uint64_t>(liveIqCapture.maxSingleGapSamples, gapSamples);
                readStart = window.startAbsolute;
            }
            if (readStart < window.endAbsolute) {
                const uint64_t offset64 = readStart - window.startAbsolute;
                const size_t offset = static_cast<size_t>(std::min<uint64_t>(offset64, static_cast<uint64_t>(window.samples.size())));
                appendStartAbs = readStart;
                appendEndAbs = window.endAbsolute;
                appended = window.samples.size() - offset;
                if (appended > 0) {
                    // std::complex<float> is the in-memory cf32 pair this recorder
                    // writes. Bulk I/O avoids millions of tiny stream writes on
                    // the GUI timer thread.
                    liveIqCapture.data.write(
                        reinterpret_cast<const char*>(window.samples.data() + static_cast<std::ptrdiff_t>(offset)),
                        static_cast<std::streamsize>(appended * sizeof(std::complex<float>)));
                }
                liveIqCapture.samplesWritten += appended;
                liveIqCapture.bytesWritten += static_cast<uint64_t>(appended) * sizeof(float) * 2u;
                if (!liveIqCapture.data.good()) ++liveIqCapture.fileWriteErrorPolls;
                liveIqCapture.cursorAbsolute = window.endAbsolute;
                liveIqCapture.endAbsolute = window.endAbsolute;
            }
        }

        if (appended == 0 && !finalPoll) ++liveIqCapture.zeroAppendPolls;
        ++liveIqCapture.pollCount;
        liveIqCapture.lastPollUtc = nowUtc;
        const bool periodicFlush = finalPoll || (liveIqCapture.pollCount % 4u == 0u);
        if (periodicFlush && liveIqCapture.data.is_open()) liveIqCapture.data.flush();

        // Drain queued P25 logs for this capture (batched, protected).
        // Copy under lock (fast), release, then write outside lock to avoid holding mutex during slow file I/O.
        // This prevents GUI thread (appendP25LogLine from update/return paths) from blocking on the mutex while writer drains.
        QStringList p25ToWrite;
        {
            std::lock_guard<std::mutex> lk(gCaptureP25PendingMutex);
            if (liveIqCapture.active && liveIqCapture.p25LogStream.is_open() && !liveIqCapturePendingP25Lines.isEmpty()) {
                p25ToWrite.swap(liveIqCapturePendingP25Lines);
                liveIqCapture.p25CaptureLinesWritten += static_cast<uint64_t>(p25ToWrite.size());
            }
        }
        if (!p25ToWrite.isEmpty()) {
            for (const QString& pl : p25ToWrite) {
                liveIqCapture.p25LogStream << pl.toStdString() << "\n";
            }
        }
        if (periodicFlush && liveIqCapture.p25LogStream.is_open()) liveIqCapture.p25LogStream.flush();
        if (periodicFlush && liveIqCapture.p25LogStream.is_open()) liveIqCapture.p25LogStream.flush();

        if (liveIqCapture.ringCsv.is_open()) {
            liveIqCapture.ringCsv
                << nowUtc.toString(Qt::ISODateWithMs).toStdString() << ','
                << liveIqCapture.pollCount << ','
                << window.startAbsolute << ','
                << window.endAbsolute << ','
                << cursorBefore << ','
                << appendStartAbs << ','
                << appendEndAbs << ','
                << appended << ','
                << gapSamples << ','
                << liveIqCapture.samplesWritten << ','
                << liveIqCapture.bytesWritten << ','
                << liveIqCapture.zeroAppendPolls << ','
                << liveIqCapture.maxSingleGapSamples << ','
                << liveIqCapture.fileWriteErrorPolls << ','
                << liveIqCapture.lastSignalLevelDb << ','
                << liveIqCapture.lastNoiseFloorDb << ','
                << liveIqCapture.lastSnrDb << ','
                << liveIqCapture.lastAfcOffsetHz << ','
                << liveIqCapture.ringEpochResets << ','
                << liveIqCapture.ringEpochResetSkippedSamples << "\n";
            if (periodicFlush) liveIqCapture.ringCsv.flush();
        }

        json pollRow = {
            {"event", finalPoll ? "ring_poll_final" : "ring_poll"},
            {"utc", nowUtc.toString(Qt::ISODateWithMs).toStdString()},
            {"poll", liveIqCapture.pollCount},
            {"ring_window_start_abs", window.startAbsolute},
            {"ring_window_end_abs", window.endAbsolute},
            {"cursor_before_abs", cursorBefore},
            {"append_start_abs", appendStartAbs},
            {"append_end_abs", appendEndAbs},
            {"samples_appended", appended},
            {"gap_samples", gapSamples},
            {"ring_epoch_reset", ringEpochReset},
            {"ring_epoch_reset_skipped_samples", ringEpochResetSkipped},
            {"ring_epoch_resets", liveIqCapture.ringEpochResets},
            {"ring_epoch_reset_skipped_samples_total", liveIqCapture.ringEpochResetSkippedSamples},
            {"total_samples_written", liveIqCapture.samplesWritten},
            {"bytes_written", liveIqCapture.bytesWritten},
            {"zero_append_polls", liveIqCapture.zeroAppendPolls},
            {"max_single_gap_samples", liveIqCapture.maxSingleGapSamples},
            {"file_write_error_polls", liveIqCapture.fileWriteErrorPolls},
            {"storage_stop_requested", liveIqCapture.storageStopRequested},
            {"storage_stop_reason", liveIqCapture.storageStopReason.toStdString()},
            {"storage_stop_available_bytes", liveIqCapture.storageStopAvailableBytes},
            {"storage_stop_reserve_bytes", liveIqCapture.storageStopReserveBytes},
            {"signal_level_db", liveIqCapture.lastSignalLevelDb},
            {"noise_floor_db", liveIqCapture.lastNoiseFloorDb},
            {"snr_db", liveIqCapture.lastSnrDb},
            {"afc_offset_hz", liveIqCapture.lastAfcOffsetHz}
        };
        // Full JSONL poll logging is useful, but writing/flushing every GUI tick
        // can make the whole application feel laggy. Keep CSV per-poll detail and
        // emit compact JSON snapshots periodically plus the final poll.
        if (periodicFlush || gapSamples > 0 || liveIqCapture.fileWriteErrorPolls > 0) {
            writeLiveIqCaptureEvent(pollRow, periodicFlush);
        }

        const double captureSeconds = liveIqCapture.sampleRateHz > 0.0
            ? static_cast<double>(liveIqCapture.samplesWritten) / liveIqCapture.sampleRateHz
            : 0.0;
        const json status = {
            {"active", true},
            {"session_id", liveIqCapture.sessionId},
            {"label", liveIqCapture.label},
            {"updated_utc", nowUtc.toString(Qt::ISODateWithMs).toStdString()},
            {"poll_count", liveIqCapture.pollCount},
            {"sample_count", liveIqCapture.samplesWritten},
            {"bytes_written", liveIqCapture.bytesWritten},
            {"estimated_seconds", captureSeconds},
            {"absolute_sample_start", liveIqCapture.startAbsolute},
            {"absolute_sample_end", liveIqCapture.endAbsolute},
            {"ring_overrun_samples", liveIqCapture.ringOverrunSamples},
            {"max_single_gap_samples", liveIqCapture.maxSingleGapSamples},
            {"ring_epoch_resets", liveIqCapture.ringEpochResets},
            {"ring_epoch_reset_skipped_samples", liveIqCapture.ringEpochResetSkippedSamples},
            {"zero_append_polls", liveIqCapture.zeroAppendPolls},
            {"file_write_error_polls", liveIqCapture.fileWriteErrorPolls},
            {"storage_stop_requested", liveIqCapture.storageStopRequested},
            {"storage_stop_reason", liveIqCapture.storageStopReason.toStdString()},
            {"storage_stop_available_bytes", liveIqCapture.storageStopAvailableBytes},
            {"storage_stop_reserve_bytes", liveIqCapture.storageStopReserveBytes},
            {"signal_level_db", liveIqCapture.lastSignalLevelDb},
            {"noise_floor_db", liveIqCapture.lastNoiseFloorDb},
            {"snr_db", liveIqCapture.lastSnrDb},
            {"afc_offset_hz", liveIqCapture.lastAfcOffsetHz},
            {"health", captureHealthVerdict(liveIqCapture.samplesWritten, liveIqCapture.ringOverrunSamples, liveIqCapture.fileWriteErrorPolls, captureSeconds, liveIqCapture.ringEpochResetSkippedSamples)}
        };
        if (periodicFlush) writeJsonDocumentFile(liveIqCapture.statusPath, status);
    }

    LiveIqCaptureResult stopLiveIqCapture() {
        LiveIqCaptureResult out;
        if (!liveIqCapture.active) {
            out.message = "No IQ capture is currently running.";
            return out;
        }
        // Stop the background writer thread cleanly, then force a final poll for end-of-capture data.
        stopLiveIqCaptureWriter.store(true, std::memory_order_release);
        if (liveIqCaptureWriterThread.joinable()) {
            liveIqCaptureWriterThread.join();
        }
        if (liveIqCaptureTimer) liveIqCaptureTimer->stop();
        pollLiveIqCapture(true);
        liveIqCaptureLogActive.store(false, std::memory_order_release);
        liveIqCapture.stoppedUtc = QDateTime::currentDateTimeUtc();
        const double actualSeconds = liveIqCapture.sampleRateHz > 0.0
            ? static_cast<double>(liveIqCapture.samplesWritten) / liveIqCapture.sampleRateHz
            : 0.0;

        json endRow = {
            {"event", "capture_stop"},
            {"utc", liveIqCapture.stoppedUtc.toString(Qt::ISODateWithMs).toStdString()},
            {"sample_count", liveIqCapture.samplesWritten},
            {"actual_seconds", actualSeconds},
            {"absolute_sample_start", liveIqCapture.startAbsolute},
            {"absolute_sample_end", liveIqCapture.endAbsolute},
            {"ring_overrun_samples", liveIqCapture.ringOverrunSamples},
            {"max_single_gap_samples", liveIqCapture.maxSingleGapSamples},
            {"ring_epoch_resets", liveIqCapture.ringEpochResets},
            {"ring_epoch_reset_skipped_samples", liveIqCapture.ringEpochResetSkippedSamples},
            {"zero_append_polls", liveIqCapture.zeroAppendPolls},
            {"file_write_error_polls", liveIqCapture.fileWriteErrorPolls},
            {"bytes_written", liveIqCapture.bytesWritten},
            {"poll_count", liveIqCapture.pollCount},
            {"storage_stop_requested", liveIqCapture.storageStopRequested},
            {"storage_stop_reason", liveIqCapture.storageStopReason.toStdString()},
            {"storage_stop_available_bytes", liveIqCapture.storageStopAvailableBytes},
            {"storage_stop_reserve_bytes", liveIqCapture.storageStopReserveBytes},
            {"signal_level_db", liveIqCapture.lastSignalLevelDb},
            {"noise_floor_db", liveIqCapture.lastNoiseFloorDb},
            {"snr_db", liveIqCapture.lastSnrDb},
            {"afc_offset_hz", liveIqCapture.lastAfcOffsetHz}
        };
        writeLiveIqCaptureEvent(endRow);

        if (liveIqCapture.data.is_open()) liveIqCapture.data.close();
        if (liveIqCapture.events.is_open()) { liveIqCapture.events.flush(); liveIqCapture.events.close(); }
        if (liveIqCapture.ringCsv.is_open()) { liveIqCapture.ringCsv.flush(); liveIqCapture.ringCsv.close(); }

        json meta;
        meta["global"] = {
            {"core:datatype", "cf32_le"},
            {"core:sample_rate", liveIqCapture.sampleRateHz},
            {"core:version", "1.2.0"},
            {"core:description", "SDR Town start/stop IQ capture with synchronized ring-health and P25 logs"},
            {"core:recorder", "SDR Town"},
            {"sdrtown:label", liveIqCapture.label},
            {"sdrtown:session_id", liveIqCapture.sessionId},
            {"sdrtown:capture_type", "start_stop_iq_ring_capture"},
            {"sdrtown:actual_seconds", actualSeconds},
            {"sdrtown:mode", modeToString(liveIqCapture.mode)},
            {"sdrtown:channel_bandwidth_hz", liveIqCapture.channelBwHz},
            {"sdrtown:audio_lpf_hz", liveIqCapture.lpfHz},
            {"sdrtown:audio_lpf_enabled", liveIqCapture.audioLpfEnabled},
            {"sdrtown:squelch_db", liveIqCapture.squelchDb},
            {"sdrtown:device_index", liveIqCapture.deviceIndex},
            {"sdrtown:device_driver", liveIqCapture.device.driver},
            {"sdrtown:device_label", liveIqCapture.device.label},
            {"sdrtown:device_serial", liveIqCapture.device.serial},
            {"sdrtown:rf_gain_db", liveIqCapture.device.gain},
            {"sdrtown:frequency_correction_ppm", liveIqCapture.device.frequencyCorrectionPpm},
            {"sdrtown:absolute_sample_start", liveIqCapture.startAbsolute},
            {"sdrtown:absolute_sample_end", liveIqCapture.endAbsolute},
            {"sdrtown:ring_overrun_samples", liveIqCapture.ringOverrunSamples},
            {"sdrtown:max_single_gap_samples", liveIqCapture.maxSingleGapSamples},
            {"sdrtown:ring_epoch_resets", liveIqCapture.ringEpochResets},
            {"sdrtown:ring_epoch_reset_skipped_samples", liveIqCapture.ringEpochResetSkippedSamples},
            {"sdrtown:zero_append_polls", liveIqCapture.zeroAppendPolls},
            {"sdrtown:file_write_error_polls", liveIqCapture.fileWriteErrorPolls},
            {"sdrtown:bytes_written", liveIqCapture.bytesWritten},
            {"sdrtown:storage_stop_requested", liveIqCapture.storageStopRequested},
            {"sdrtown:storage_stop_reason", liveIqCapture.storageStopReason.toStdString()},
            {"sdrtown:storage_stop_available_bytes", liveIqCapture.storageStopAvailableBytes},
            {"sdrtown:storage_stop_reserve_bytes", liveIqCapture.storageStopReserveBytes},
            {"sdrtown:health", captureHealthVerdict(liveIqCapture.samplesWritten, liveIqCapture.ringOverrunSamples, liveIqCapture.fileWriteErrorPolls, actualSeconds, liveIqCapture.ringEpochResetSkippedSamples)},
            {"sdrtown:poll_count", liveIqCapture.pollCount},
            {"sdrtown:capture_started_utc", liveIqCapture.startedUtc.toString(Qt::ISODateWithMs).toStdString()},
            {"sdrtown:capture_stopped_utc", liveIqCapture.stoppedUtc.toString(Qt::ISODateWithMs).toStdString()},
            {"sdrtown:signal_level_db", liveIqCapture.lastSignalLevelDb},
            {"sdrtown:noise_floor_db", liveIqCapture.lastNoiseFloorDb},
            {"sdrtown:snr_db", liveIqCapture.lastSnrDb},
            {"sdrtown:afc_offset_hz", liveIqCapture.lastAfcOffsetHz}
        };
        meta["captures"] = json::array({
            {
                {"core:sample_start", 0},
                {"core:frequency", liveIqCapture.centerFreqHz},
                {"core:datetime", liveIqCapture.startedUtc.toString(Qt::ISODateWithMs).toStdString()},
                {"sdrtown:capture_end_utc", liveIqCapture.stoppedUtc.toString(Qt::ISODateWithMs).toStdString()},
                {"sdrtown:absolute_sample_start", liveIqCapture.startAbsolute},
                {"sdrtown:absolute_sample_end", liveIqCapture.endAbsolute},
                {"sdrtown:ring_epoch_resets", liveIqCapture.ringEpochResets}
            }
        });
        meta["annotations"] = json::array({
            {
                {"core:sample_start", 0},
                {"core:sample_count", liveIqCapture.samplesWritten},
                {"core:freq_lower_edge", liveIqCapture.tunedFreqHz - liveIqCapture.channelBwHz * 0.5},
                {"core:freq_upper_edge", liveIqCapture.tunedFreqHz + liveIqCapture.channelBwHz * 0.5},
                {"core:label", liveIqCapture.label},
                {"sdrtown:mode", modeToString(liveIqCapture.mode)},
                {"sdrtown:snr_db", liveIqCapture.lastSnrDb}
            }
        });
        meta["sdrtown:artifacts"] = {
            {"event_log_jsonl", QFileInfo(liveIqCapture.eventsPath).fileName().toStdString()},
            {"ring_health_csv", QFileInfo(liveIqCapture.ringCsvPath).fileName().toStdString()},
            {"p25_log_text", QFileInfo(liveIqCapture.p25TextPath).fileName().toStdString()},
            {"live_status_json", QFileInfo(liveIqCapture.statusPath).fileName().toStdString()},
            {"summary_json", QFileInfo(liveIqCapture.summaryPath).fileName().toStdString()},
            {"replay_notes", QFileInfo(liveIqCapture.replayPath).fileName().toStdString()}
        };

        try {
            std::ofstream metaOut(liveIqCapture.metaPath.toStdString());
            if (!metaOut.is_open()) {
                out.message = "Could not write IQ SigMF metadata file.";
                return out;
            }
            metaOut << meta.dump(2);

            const json summary = {
                {"session_id", liveIqCapture.sessionId},
                {"label", liveIqCapture.label},
                {"health", captureHealthVerdict(liveIqCapture.samplesWritten, liveIqCapture.ringOverrunSamples, liveIqCapture.fileWriteErrorPolls, actualSeconds, liveIqCapture.ringEpochResetSkippedSamples)},
                {"started_utc", liveIqCapture.startedUtc.toString(Qt::ISODateWithMs).toStdString()},
                {"stopped_utc", liveIqCapture.stoppedUtc.toString(Qt::ISODateWithMs).toStdString()},
                {"actual_seconds", actualSeconds},
                {"sample_count", liveIqCapture.samplesWritten},
                {"sample_rate_hz", liveIqCapture.sampleRateHz},
                {"bytes_written", liveIqCapture.bytesWritten},
                {"ring_overrun_samples", liveIqCapture.ringOverrunSamples},
                {"max_single_gap_samples", liveIqCapture.maxSingleGapSamples},
                {"ring_epoch_resets", liveIqCapture.ringEpochResets},
                {"ring_epoch_reset_skipped_samples", liveIqCapture.ringEpochResetSkippedSamples},
                {"zero_append_polls", liveIqCapture.zeroAppendPolls},
                {"file_write_error_polls", liveIqCapture.fileWriteErrorPolls},
                {"storage_stop_requested", liveIqCapture.storageStopRequested},
                {"storage_stop_reason", liveIqCapture.storageStopReason.toStdString()},
                {"storage_stop_available_bytes", liveIqCapture.storageStopAvailableBytes},
                {"storage_stop_reserve_bytes", liveIqCapture.storageStopReserveBytes},
                {"p25_log_lines_written", liveIqCapture.p25CaptureLinesWritten},
                {"p25_log_write_errors", liveIqCapture.p25CaptureWriteErrors},
                {"freq_hz", liveIqCapture.tunedFreqHz},
                {"center_freq_hz", liveIqCapture.centerFreqHz},
                {"mode", modeToString(liveIqCapture.mode)},
                {"snr_db", liveIqCapture.lastSnrDb},
                {"afc_offset_hz", liveIqCapture.lastAfcOffsetHz},
                {"files", {
                    {"sigmf_meta", QFileInfo(liveIqCapture.metaPath).fileName().toStdString()},
                    {"sigmf_data", QFileInfo(liveIqCapture.dataPath).fileName().toStdString()},
                    {"events_jsonl", QFileInfo(liveIqCapture.eventsPath).fileName().toStdString()},
                    {"ring_health_csv", QFileInfo(liveIqCapture.ringCsvPath).fileName().toStdString()},
                    {"p25_log", QFileInfo(liveIqCapture.p25TextPath).fileName().toStdString()}
                }}
            };
            writeJsonDocumentFile(liveIqCapture.summaryPath, summary);

            const json finalStatus = {
                {"active", false},
                {"session_id", liveIqCapture.sessionId},
                {"label", liveIqCapture.label},
                {"updated_utc", liveIqCapture.stoppedUtc.toString(Qt::ISODateWithMs).toStdString()},
                {"poll_count", liveIqCapture.pollCount},
                {"sample_count", liveIqCapture.samplesWritten},
                {"bytes_written", liveIqCapture.bytesWritten},
                {"estimated_seconds", actualSeconds},
                {"absolute_sample_start", liveIqCapture.startAbsolute},
                {"absolute_sample_end", liveIqCapture.endAbsolute},
                {"ring_overrun_samples", liveIqCapture.ringOverrunSamples},
                {"max_single_gap_samples", liveIqCapture.maxSingleGapSamples},
                {"ring_epoch_resets", liveIqCapture.ringEpochResets},
                {"ring_epoch_reset_skipped_samples", liveIqCapture.ringEpochResetSkippedSamples},
                {"zero_append_polls", liveIqCapture.zeroAppendPolls},
                {"file_write_error_polls", liveIqCapture.fileWriteErrorPolls},
                {"storage_stop_requested", liveIqCapture.storageStopRequested},
                {"storage_stop_reason", liveIqCapture.storageStopReason.toStdString()},
                {"storage_stop_available_bytes", liveIqCapture.storageStopAvailableBytes},
                {"storage_stop_reserve_bytes", liveIqCapture.storageStopReserveBytes},
                {"p25_log_lines_written", liveIqCapture.p25CaptureLinesWritten},
                {"p25_log_write_errors", liveIqCapture.p25CaptureWriteErrors},
                {"signal_level_db", liveIqCapture.lastSignalLevelDb},
                {"noise_floor_db", liveIqCapture.lastNoiseFloorDb},
                {"snr_db", liveIqCapture.lastSnrDb},
                {"afc_offset_hz", liveIqCapture.lastAfcOffsetHz},
                {"health", captureHealthVerdict(liveIqCapture.samplesWritten, liveIqCapture.ringOverrunSamples, liveIqCapture.fileWriteErrorPolls, actualSeconds, liveIqCapture.ringEpochResetSkippedSamples)}
            };
            writeJsonDocumentFile(liveIqCapture.statusPath, finalStatus);

            std::ofstream replay(liveIqCapture.replayPath.toStdString());
            if (replay.is_open()) {
                replay << "# SDR Town replay helper\n";
                replay << "# Use from the app binary working directory. Adjust target MHz/time limit if needed.\n";
                replay << "p25 replay \"" << liveIqCapture.metaPath.toStdString() << "\" "
                       << (liveIqCapture.tunedFreqHz / 1e6) << "\n";
            }

            if (liveIqCapture.p25LogStream.is_open()) {
                liveIqCapture.p25LogStream << "\n# capture_stop_utc=" << liveIqCapture.stoppedUtc.toString(Qt::ISODateWithMs).toStdString() << "\n";
                liveIqCapture.p25LogStream << "# absolute_sample_end=" << liveIqCapture.endAbsolute << "\n";
                liveIqCapture.p25LogStream << "# ring_overrun_samples=" << liveIqCapture.ringOverrunSamples << "\n";
                liveIqCapture.p25LogStream << "# ring_epoch_resets=" << liveIqCapture.ringEpochResets << "\n";
                liveIqCapture.p25LogStream << "# ring_epoch_reset_skipped_samples=" << liveIqCapture.ringEpochResetSkippedSamples << "\n";
                liveIqCapture.p25LogStream << "# p25_log_lines_written=" << liveIqCapture.p25CaptureLinesWritten << "\n";
                liveIqCapture.p25LogStream << "# p25_log_write_errors=" << liveIqCapture.p25CaptureWriteErrors << "\n";
                liveIqCapture.p25LogStream << "# storage_stop_requested=" << (liveIqCapture.storageStopRequested ? "true" : "false") << "\n";
                if (liveIqCapture.storageStopRequested) {
                    liveIqCapture.p25LogStream << "# storage_stop_reason=" << liveIqCapture.storageStopReason.toStdString() << "\n";
                    liveIqCapture.p25LogStream << "# storage_stop_available_bytes=" << liveIqCapture.storageStopAvailableBytes << "\n";
                    liveIqCapture.p25LogStream << "# storage_stop_reserve_bytes=" << liveIqCapture.storageStopReserveBytes << "\n";
                }
                liveIqCapture.p25LogStream.flush();
                liveIqCapture.p25LogStream.close();
            }

            std::ofstream manifest((iqTestCapturesRoot() + "/manifest.jsonl").toStdString(), std::ios::app);
            if (manifest.is_open()) {
                json row = {
                    {"created_utc", liveIqCapture.stoppedUtc.toString(Qt::ISODateWithMs).toStdString()},
                    {"label", liveIqCapture.label},
                    {"session_id", liveIqCapture.sessionId},
                    {"capture_type", "start_stop_iq_ring_capture"},
                    {"health", captureHealthVerdict(liveIqCapture.samplesWritten, liveIqCapture.ringOverrunSamples, liveIqCapture.fileWriteErrorPolls, actualSeconds, liveIqCapture.ringEpochResetSkippedSamples)},
                    {"freq_hz", liveIqCapture.tunedFreqHz},
                    {"center_freq_hz", liveIqCapture.centerFreqHz},
                    {"sample_rate_hz", liveIqCapture.sampleRateHz},
                    {"sample_count", liveIqCapture.samplesWritten},
                    {"actual_seconds", actualSeconds},
                    {"absolute_sample_start", liveIqCapture.startAbsolute},
                    {"absolute_sample_end", liveIqCapture.endAbsolute},
                    {"ring_overrun_samples", liveIqCapture.ringOverrunSamples},
                    {"max_single_gap_samples", liveIqCapture.maxSingleGapSamples},
                    {"ring_epoch_resets", liveIqCapture.ringEpochResets},
                    {"ring_epoch_reset_skipped_samples", liveIqCapture.ringEpochResetSkippedSamples},
                    {"zero_append_polls", liveIqCapture.zeroAppendPolls},
                    {"file_write_error_polls", liveIqCapture.fileWriteErrorPolls},
                    {"storage_stop_requested", liveIqCapture.storageStopRequested},
                    {"storage_stop_reason", liveIqCapture.storageStopReason.toStdString()},
                    {"storage_stop_available_bytes", liveIqCapture.storageStopAvailableBytes},
                    {"storage_stop_reserve_bytes", liveIqCapture.storageStopReserveBytes},
                    {"bytes_written", liveIqCapture.bytesWritten},
                    {"meta", QFileInfo(liveIqCapture.metaPath).fileName().toStdString()},
                    {"data", QFileInfo(liveIqCapture.dataPath).fileName().toStdString()},
                    {"event_log", QFileInfo(liveIqCapture.eventsPath).fileName().toStdString()},
                    {"ring_health", QFileInfo(liveIqCapture.ringCsvPath).fileName().toStdString()},
                    {"p25_log", QFileInfo(liveIqCapture.p25TextPath).fileName().toStdString()},
                    {"summary", QFileInfo(liveIqCapture.summaryPath).fileName().toStdString()},
                    {"replay", QFileInfo(liveIqCapture.replayPath).fileName().toStdString()},
                    {"directory", liveIqCapture.directory.toStdString()}
                };
                manifest << row.dump() << "\n";
            }
        } catch (const std::exception& ex) {
            out.message = QString("IQ capture finalize failed: %1").arg(ex.what());
            return out;
        }

        if (liveIqCapture.events.is_open()) liveIqCapture.events.close();
        out.ok = true;
        out.directory = liveIqCapture.directory;
        out.message = QString("Saved IQ capture: %1 samples, %2 s, ring gaps %3 samples, health %4")
            .arg(liveIqCapture.samplesWritten)
            .arg(actualSeconds, 0, 'f', 3)
            .arg(liveIqCapture.ringOverrunSamples)
            .arg(captureHealthVerdict(liveIqCapture.samplesWritten, liveIqCapture.ringOverrunSamples, liveIqCapture.fileWriteErrorPolls, actualSeconds, liveIqCapture.ringEpochResetSkippedSamples));
        {
            std::lock_guard<std::mutex> lk(gCaptureP25PendingMutex);
            liveIqCapturePendingP25Lines.clear();
        }
        liveIqCapture = LiveIqCaptureSession{};
        return out;
    }

    IqTestCaptureResult captureIqTestWindow(const std::string& label, double seconds, const QDateTime& startedUtc) {
        IqTestCaptureRequest req;
        req.label = trimCopy(label);
        if (req.label.empty()) req.label = "iq_test";
        req.requestedSeconds = std::clamp(seconds, 0.25, 120.0);
        req.captureStartedUtc = startedUtc;
        req.captureEndedUtc = QDateTime::currentDateTimeUtc();
        req.signalLevelDb = gLastRmsDb.load(std::memory_order_relaxed);
        req.noiseFloorDb = gLastNoiseFloorDb.load(std::memory_order_relaxed);
        req.snrDb = gLastSnrDb.load(std::memory_order_relaxed);
        req.afcOffsetHz = gLastAfcOffsetHz.load(std::memory_order_relaxed);

        {
            std::lock_guard<std::mutex> lk(monitorParamsMutex);
            req.tunedFreqHz = currentMonitorFreq;
            req.mode = currentMonitorMode;
            req.channelBwHz = monitorChannelBwHz;
            req.lpfHz = monitorLpfHz;
            req.audioLpfEnabled = monitorAudioLpfEnabled;
            req.squelchDb = monitorSquelchDb;
        }

        auto& mgr = DeviceManager::instance();
        bool gotSpectrum = false;
        for (size_t i = 0; i < mgr.getDevices().size(); ++i) {
            if (mgr.isStreaming(i) && mgr.getLatestSpectrum(i, req.spectrumDb, req.centerFreqHz, req.sampleRateHz) && req.sampleRateHz > 0.0) {
                req.deviceIndex = i;
                gotSpectrum = true;
                break;
            }
        }
        if (!gotSpectrum) {
            return {false, {}, "No live spectrum/device state yet. Start/tune a device before using timed IQ capture."};
        }

        const auto devices = mgr.getDevices();
        if (req.deviceIndex < devices.size()) req.device = devices[req.deviceIndex];

        const size_t requestedSamples = static_cast<size_t>(std::clamp(
            req.sampleRateHz * req.requestedSeconds,
            1024.0,
            std::max(1024.0, req.sampleRateHz * req.requestedSeconds)));
        const auto window = mgr.getRecentIQWindowWithCursor(req.deviceIndex, requestedSamples);
        req.iq = window.samples;
        req.startAbsolute = window.startAbsolute;
        req.endAbsolute = window.endAbsolute;

        // Snapshot the UI/P25 log at the same time the IQ window is cut. This gives capture reviewers
        // one directory containing the signal, absolute sample range, UTC wall-clock range, and decoder notes.
        req.p25LogSnapshot = p25LogLines;
        return saveIqTestCapture(req);
    }


    void stopAllStreaming() {
        if (stopAllStreamingStarted.exchange(true, std::memory_order_acq_rel)) {
            // closeEvent() and aboutToQuit can both fire during a normal close.  Teardown of
            // Soapy/miniaudio is not guaranteed to be re-entrant, so keep shutdown one-shot.
            shutdownStarted.store(true, std::memory_order_release);
            guiStartupSettled.store(false, std::memory_order_release);
            if (updateTimer) updateTimer->stop();
            if (liveIqCaptureTimer) liveIqCaptureTimer->stop();
            return;
        }
        // Block any late ensureAudioOutputActive / engine create while workers join.
        shutdownStarted.store(true, std::memory_order_release);
        guiStartupSettled.store(false, std::memory_order_release);

        if (liveIqCapture.active) {
            try {
                const auto result = stopLiveIqCapture();
                spdlog::info("Finalized live IQ capture during shutdown: {}", result.message.toStdString());
            } catch (const std::exception& ex) {
                spdlog::warn("Exception finalizing live IQ capture during shutdown: {}", ex.what());
            } catch (...) {
                spdlog::warn("Unknown exception finalizing live IQ capture during shutdown");
            }
        } else if (liveIqCaptureTimer) {
            liveIqCaptureTimer->stop();
        }

        if (updateTimer) updateTimer->stop();
        if (m_p25TranscriptSource) {
            m_p25TranscriptSource->uninstallGlobalTap();
        }
        if (m_sttEngine) {
            m_sttEngine->flushCurrentSegment();
            m_sttEngine->stop();
        }
        stopDspWorker.store(true, std::memory_order_release);
        if (guiDspWorker.joinable()) {
            guiDspWorker.join();
        }
        stopP25VoiceWorker();
        if (p25ControlWorkerThread.joinable()) {
            p25ControlWorkerThread.join();
        }
        p25ControlWorkerBusy.store(false, std::memory_order_release);

        auto& mgr = DeviceManager::instance();
        for (size_t i = 0; i < mgr.getDevices().size(); ++i) {
            if (mgr.isStreaming(i)) {
                try { mgr.stopStreaming(i); } catch (...) { spdlog::warn("stopAllStreaming: exception stopping {}", i); }
            }
        }
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

        // View / Tools / Settings
        menuBar()->addMenu("&View");
        QMenu* toolsMenu = menuBar()->addMenu("&Tools");
        toolsMenu->addAction("&Decode Log / Transcript...", this, [this]() {
            showTranscriptWindow();
        });
        toolsMenu->addSeparator();
        toolsMenu->addAction("&IQ Replay...", this, [this]() {
            showIqReplayWindow();
        });
        toolsMenu->addAction("P25 Decoder &Log...", this, [this]() {
            showP25LogWindow();
        });
        QMenu* settingsMenu = menuBar()->addMenu("&Settings");
        settingsMenu->addAction("Preferences...", [](){});

        // Help
        QMenu* helpMenu = menuBar()->addMenu("&Help");
        helpMenu->addAction("&About SDR Town", this, &MainWindow::showAbout);
        helpMenu->addSeparator();
        helpMenu->addAction("&Report Issue...", this, &MainWindow::showDiagnosticsReportDialog);
        helpMenu->addAction("&My Submitted Issues...", this, &MainWindow::showMyDiagnosticsReports);
        helpMenu->addSeparator();
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

