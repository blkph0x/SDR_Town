#include <QApplication>
#include <QCoreApplication>
#include <QThread>
#include <QMainWindow>
#include <QMenuBar>
#include <QStatusBar>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>
#include <QPushButton>
#include <QTextEdit>
#include <QTextCursor>
#include <QTextDocument>
#include <QPalette>
#include <QStyleFactory>
#include <QMessageBox>
#include <QAction>
#include <QCryptographicHash>
#include <QDir>
#include <QFileDialog>
#include <QStandardPaths>
#include <QDebug>
#include <QSettings>   // for updater skipped version + last check persistence (best practice)
#include <QDialog>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QProgressBar>
#include <QSlider>
#include <QHeaderView>
#include <QGroupBox>
#include <QFormLayout>
#include <QTimer>
#include <QDesktopServices>
#include <QUrl>
#include <QInputDialog>
#include <QLineEdit>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStorageInfo>
#include <QStringList>
#include <QSysInfo>
#include <QUuid>
#include <QPointer>
#include <complex>
#include <array>
#include <cstddef>
#include <cstdint>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <nlohmann/json.hpp>

#include "DeviceManager.h"
#include "SpectrumWidget.h"
#include "AudioEngine.h"
#include "AudioCapture.h"
#include "IP25AmbeEncoder.h"
#include "P25Phase2TxFramer.h"
#include "Demod.h"
#include "P25Control.h"
#include "P25LiveDecoder.h"
#include "P25FollowStateMachine.h"
#include "P25AudioDropClass.h"
#include "P25SdrtrunkTune.h"
#include "P25VoiceTiming.h"
#include "P25TalkgroupRegistry.h"
#include "P25AppGlobals.h"
#include "P25RollingIq.h"
#include "P25VoiceDecode.h"
#include "P25VoiceTest.h"
#include "CliApp.h"
#include "AppBootstrap.h"
#include "MainWindow.h"
#include "P25TxSession.h"
#include "P25TxConfig.h"
#include "SignalClassifier.h"
#include "ClassifierModelBackend.h"
#include "Receiver.h"  // Phase 0: per-receiver foundation
#include "RemoteDiagnostics.h"
#include "UpdateManager.h"
#include "P25DebugStage.h"
#include "TranscriptHub.h"
#include "TranscriptWindow.h"
#include "SttEngine.h"
#include "P25TranscriptSource.h"

#include <iostream>
#include <string>
#include <vector>
#include <deque>
#include <sstream>
#include <iomanip>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <cctype>
#include <limits>
#include <map>
#include <unordered_map>
#include <mutex>
#include <optional>
#include <iterator>
#include <functional>
#include <memory>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#endif

using json = nlohmann::json;

#ifndef SDR_TOWN_VERSION
#define SDR_TOWN_VERSION "0.0.0"
#endif

// Recoverable “best clear continuous Phase-2 audio” marker. Binary fallback:
// build/baselines/SDR_Town_p25_clear_continuous_20260810.exe
// Evidence: docs/P25_BASELINE_CLEAR_CONTINUOUS_20260810.md (live log 20260810_134531).
#ifndef SDR_TOWN_P25_AUDIO_BASELINE
#define SDR_TOWN_P25_AUDIO_BASELINE "p25-clear-continuous-20260810"
#endif

// Phase-2 late entry on real systems often reaches the traffic channel after
// PTT/ESS has already passed.  Keep the sdrtrunk-style queue as the primary
// path. Target MAC/ESS proves clear audio; an explicit clear control-channel
// grant may also release after target-slot hard voice, XOR mask, and diagnostic
// AMBE validation prove the audio path is sane. Explicit encrypted grants/ESS
// remain fail-closed.
// Capture 20260712_021852: unknown grants spent 700 ms queued while short PTTs
// ended; clear-known releases still need a small grace, but keep it tight.
static constexpr qint64 kP25Phase2AudioTailGraceMs = 100;
static constexpr qint64 kP25Phase2SpeakerAudioTailGraceMs = 2500;
// sdrtrunk P25P2AudioModule keeps encrypted/clear state for the whole call
// until squelch/reset — not a 12 s sliding window. Capture 20260808_022809
// lost continuous feed mid-call when TTL expired between Voice2/4 islands.
// Cap the unknown-security raw-AMBE queue. A deeper stash turns late-entry
// release into a ~1s PCM burst that floods the speaker ring then starves
// (classic blocky start). Keep enough for PTT/ESS settle, not a full second.
// Drain at most this many queued AMBE frames per decode tick once clear.
// Remaining frames stay armed for the next tick so cadence stays stream-like.
// kGuiP25ClearAudioMin*: see MainWindow.h (ISS-0004 Phase 8)

// GuiRuntimeConfig: see CliApp.h (ISS-0004 Phase 7)

static bool guiRuntimeIsFlag(const std::string& text) noexcept
{
    return text.rfind("--", 0) == 0 || text.rfind("-", 0) == 0;
}

static std::optional<std::string> guiRuntimeArgValue(int& i, int argc, char* argv[], const std::string& arg)
{
    const size_t eq = arg.find('=');
    if (eq != std::string::npos) return arg.substr(eq + 1);
    if (i + 1 < argc && argv[i + 1] && !guiRuntimeIsFlag(argv[i + 1])) {
        ++i;
        return std::string(argv[i]);
    }
    return std::nullopt;
}

static bool guiRuntimeParseDouble(const std::string& text, double& out) noexcept
{
    char* end = nullptr;
    out = std::strtod(text.c_str(), &end);
    return end && *end == '\0' && std::isfinite(out);
}

static bool guiRuntimeParseInt(const std::string& text, int& out) noexcept
{
    char* end = nullptr;
    const long v = std::strtol(text.c_str(), &end, 10);
    if (!end || *end != '\0') return false;
    out = static_cast<int>(std::clamp<long>(v, 0, std::numeric_limits<int>::max()));
    return true;
}

static bool guiRuntimeParseSigned64(const std::string& text, int64_t& out) noexcept
{
    try {
        size_t consumed = 0;
        const long long value = std::stoll(text, &consumed, 0);
        if (consumed != text.size()) return false;
        out = static_cast<int64_t>(value);
        return true;
    } catch (...) {
        return false;
    }
}

static bool guiRuntimeParseUnsigned32(const std::string& text, uint32_t& out) noexcept
{
    try {
        size_t consumed = 0;
        const unsigned long value = std::stoul(text, &consumed, 0);
        if (consumed != text.size() || value > std::numeric_limits<uint32_t>::max()) return false;
        out = static_cast<uint32_t>(value);
        return true;
    } catch (...) {
        return false;
    }
}

static std::optional<double> guiRuntimeParseFrequencyHz(const std::string& text)
{
    double value = 0.0;
    if (!guiRuntimeParseDouble(text, value) || value <= 0.0) return std::nullopt;
    // Human command lines usually specify MHz; raw Hz remains available for
    // automation by passing a value above 1 MHz.
    return value >= 1000000.0 ? value : value * 1e6;
}

GuiRuntimeConfig parseGuiRuntimeConfig(int argc, char* argv[])
{
    GuiRuntimeConfig cfg;
    for (int i = 1; i < argc; ++i) {
        if (!argv[i]) continue;
        std::string arg = argv[i];
        std::string key = arg;
        const size_t eq = key.find('=');
        if (eq != std::string::npos) key = key.substr(0, eq);
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });

        auto requireValue = [&](const char* name) -> std::optional<std::string> {
            auto value = guiRuntimeArgValue(i, argc, argv, arg);
            if (!value.has_value()) {
                cfg.warnings.push_back(std::string("missing value for ") + name);
            }
            return value;
        };

        if (key == "--gui-frequency" || key == "--gui-freq" ||
            key == "--frequency" || key == "--freq") {
            cfg.requested = true;
            if (auto value = requireValue(key.c_str())) {
                if (auto hz = guiRuntimeParseFrequencyHz(*value)) {
                    cfg.frequencyHz = *hz;
                    cfg.startDevice = true;
                } else {
                    cfg.warnings.push_back("invalid GUI frequency: " + *value);
                }
            }
        } else if (key == "--gui-p25-control" || key == "--p25-control" ||
                   key == "--p25-cc" || key == "--gui-p25-cc") {
            cfg.requested = true;
            if (auto value = requireValue(key.c_str())) {
                if (auto hz = guiRuntimeParseFrequencyHz(*value)) {
                    cfg.p25ControlHz = *hz;
                    cfg.frequencyHz = cfg.frequencyHz > 0.0 ? cfg.frequencyHz : *hz;
                    cfg.p25Monitor = true;
                    cfg.startDevice = true;
                } else {
                    cfg.warnings.push_back("invalid P25 control frequency: " + *value);
                }
            }
        } else if (key == "--gui-device" || key == "--device") {
            cfg.requested = true;
            if (auto value = requireValue(key.c_str())) {
                std::string lower = *value;
                std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
                    return static_cast<char>(std::tolower(c));
                });
                if (lower == "default" || lower == "first") {
                    cfg.deviceIndex = 0;
                    cfg.deviceIndexSet = true;
                } else {
                    int index = 0;
                    if (guiRuntimeParseInt(*value, index)) {
                        cfg.deviceIndex = static_cast<size_t>(std::max(0, index));
                        cfg.deviceIndexSet = true;
                    } else {
                        cfg.warnings.push_back("invalid GUI device index: " + *value);
                    }
                }
            }
        } else if (key == "--gui-start-device" || key == "--start-device") {
            cfg.requested = true;
            cfg.startDevice = true;
        } else if (key == "--gui-default-audio" || key == "--default-audio" ||
                   key == "--gui-audio-default") {
            cfg.requested = true;
            cfg.defaultAudio = true;
        } else if (key == "--gui-auto-follow" || key == "--auto-follow" ||
                   key == "--p25-auto-follow") {
            cfg.requested = true;
            cfg.autoFollow = true;
        } else if (key == "--gui-p25-monitor" || key == "--p25-monitor") {
            cfg.requested = true;
            cfg.p25Monitor = true;
        } else if (key == "--gui-grant-test" || key == "--p25-grant-test" ||
                   key == "--grant-test") {
            cfg.requested = true;
            cfg.p25GrantTest = true;
            cfg.p25Monitor = true;
            cfg.autoFollow = true;
            cfg.defaultAudio = true;
            cfg.openP25Log = true;
        } else if (key == "--gui-open-p25-log" || key == "--p25-log") {
            cfg.requested = true;
            cfg.openP25Log = true;
        } else if (key == "--gui-start-iq-capture" || key == "--gui-iq-capture" ||
                   key == "--start-iq-capture") {
            cfg.requested = true;
            cfg.iqCapture = true;
        } else if (key == "--gui-iq-replay" || key == "--iq-replay" ||
                   key == "--gui-open-iq-replay") {
            cfg.requested = true;
            cfg.iqReplay = true;
            if (auto value = guiRuntimeArgValue(i, argc, argv, arg)) {
                cfg.iqReplayPath = *value;
            }
        } else if (key == "--gui-iq-replay-autoplay" || key == "--iq-replay-autoplay") {
            cfg.requested = true;
            cfg.iqReplay = true;
            cfg.iqReplayAutoPlay = true;
        } else if (key == "--gui-iq-replay-target" || key == "--iq-replay-target") {
            cfg.requested = true;
            cfg.iqReplay = true;
            if (auto value = requireValue(key.c_str())) {
                if (auto hz = guiRuntimeParseFrequencyHz(*value)) cfg.iqReplayTargetHz = *hz;
                else cfg.warnings.push_back("invalid IQ replay target frequency: " + *value);
            }
        } else if (key == "--gui-iq-replay-center" || key == "--iq-replay-center") {
            cfg.requested = true;
            cfg.iqReplay = true;
            if (auto value = requireValue(key.c_str())) {
                if (auto hz = guiRuntimeParseFrequencyHz(*value)) cfg.iqReplayCenterHz = *hz;
                else cfg.warnings.push_back("invalid IQ replay center frequency: " + *value);
            }
        } else if (key == "--gui-iq-replay-voice-center" || key == "--gui-iq-replay-voicecenter" ||
                   key == "--gui-iq-replay-traffic-center" || key == "--gui-iq-replay-trafficcenter" ||
                   key == "--iq-replay-voice-center" || key == "--iq-replay-voicecenter" ||
                   key == "--iq-replay-traffic-center" || key == "--iq-replay-trafficcenter") {
            cfg.requested = true;
            cfg.iqReplay = true;
            if (auto value = requireValue(key.c_str())) {
                if (auto hz = guiRuntimeParseFrequencyHz(*value)) cfg.iqReplayVoiceCenterHz = *hz;
                else cfg.warnings.push_back("invalid IQ replay voice center frequency: " + *value);
            }
        } else if (key == "--gui-iq-replay-start-ms" || key == "--gui-iq-replay-skip-ms" ||
                   key == "--iq-replay-start-ms" || key == "--iq-replay-skip-ms") {
            cfg.requested = true;
            cfg.iqReplay = true;
            if (auto value = requireValue(key.c_str())) {
                int ms = 0;
                if (guiRuntimeParseInt(*value, ms)) cfg.iqReplayStartMs = std::clamp(ms, 0, 36000000);
                else cfg.warnings.push_back("invalid IQ replay start milliseconds: " + *value);
            }
        } else if (key == "--gui-iq-replay-ms" || key == "--gui-iq-replay-duration-ms" ||
                   key == "--iq-replay-ms" || key == "--iq-replay-duration-ms") {
            cfg.requested = true;
            cfg.iqReplay = true;
            if (auto value = requireValue(key.c_str())) {
                int ms = 0;
                if (guiRuntimeParseInt(*value, ms) && ms > 0) cfg.iqReplayDurationMs = std::clamp(ms, 100, 600000);
                else cfg.warnings.push_back("invalid IQ replay duration milliseconds: " + *value);
            }
        } else if (key == "--gui-iq-replay-window-ms" || key == "--iq-replay-window-ms") {
            cfg.requested = true;
            cfg.iqReplay = true;
            if (auto value = requireValue(key.c_str())) {
                int ms = 0;
                if (guiRuntimeParseInt(*value, ms) && ms > 0) cfg.iqReplayWindowMs = std::clamp(ms, 80, 5000);
                else cfg.warnings.push_back("invalid IQ replay window milliseconds: " + *value);
            }
        } else if (key == "--gui-iq-replay-hop-ms" || key == "--iq-replay-hop-ms") {
            cfg.requested = true;
            cfg.iqReplay = true;
            if (auto value = requireValue(key.c_str())) {
                int ms = 0;
                if (guiRuntimeParseInt(*value, ms) && ms >= 0) {
                    cfg.iqReplayHopMs = ms == 0 ? 0 : std::clamp(ms, 10, 1000);
                }
                else cfg.warnings.push_back("invalid IQ replay hop milliseconds: " + *value);
            }
        } else if (key == "--gui-iq-replay-tg" || key == "--iq-replay-tg") {
            cfg.requested = true;
            cfg.iqReplay = true;
            if (auto value = requireValue(key.c_str())) {
                uint32_t tg = 0;
                if (guiRuntimeParseUnsigned32(*value, tg) && tg <= static_cast<uint32_t>(std::numeric_limits<int>::max())) {
                    cfg.iqReplayTalkgroup = static_cast<int>(tg);
                } else {
                    cfg.warnings.push_back("invalid IQ replay talkgroup: " + *value);
                }
            }
        } else if (key == "--gui-iq-replay-slot" || key == "--iq-replay-slot") {
            cfg.requested = true;
            cfg.iqReplay = true;
            if (auto value = requireValue(key.c_str())) {
                int slot = -1;
                if (guiRuntimeParseInt(*value, slot) && slot >= 0 && slot <= 1) cfg.iqReplaySlot = slot;
                else cfg.warnings.push_back("invalid IQ replay TDMA slot: " + *value);
            }
        } else if (key == "--gui-iq-replay-nac" || key == "--iq-replay-nac") {
            cfg.requested = true;
            cfg.iqReplay = true;
            if (auto value = requireValue(key.c_str())) {
                int64_t nac = -1;
                if (guiRuntimeParseSigned64(*value, nac) && nac >= 0 && nac <= 0x0fff) cfg.iqReplayNac = static_cast<int>(nac);
                else cfg.warnings.push_back("invalid IQ replay NAC: " + *value);
            }
        } else if (key == "--gui-iq-replay-wacn" || key == "--iq-replay-wacn") {
            cfg.requested = true;
            cfg.iqReplay = true;
            if (auto value = requireValue(key.c_str())) {
                int64_t wacn = -1;
                if (guiRuntimeParseSigned64(*value, wacn) && wacn >= 0 && wacn <= 0x0fffff) cfg.iqReplayWacn = wacn;
                else cfg.warnings.push_back("invalid IQ replay WACN: " + *value);
            }
        } else if (key == "--gui-iq-replay-system" || key == "--iq-replay-system") {
            cfg.requested = true;
            cfg.iqReplay = true;
            if (auto value = requireValue(key.c_str())) {
                int64_t systemId = -1;
                if (guiRuntimeParseSigned64(*value, systemId) && systemId >= 0 && systemId <= 0x0fff) cfg.iqReplaySystemId = static_cast<int>(systemId);
                else cfg.warnings.push_back("invalid IQ replay system id: " + *value);
            }
        } else if (key == "--gui-iq-replay-clear" || key == "--iq-replay-clear") {
            cfg.requested = true;
            cfg.iqReplay = true;
            cfg.iqReplayClearGrant = true;
            cfg.iqReplayEncryptedGrant = false;
        } else if (key == "--gui-iq-replay-enc" || key == "--iq-replay-enc" ||
                   key == "--gui-iq-replay-encrypted" || key == "--iq-replay-encrypted") {
            cfg.requested = true;
            cfg.iqReplay = true;
            cfg.iqReplayEncryptedGrant = true;
            cfg.iqReplayClearGrant = false;
        } else if (key == "--gui-iq-replay-no-stt" || key == "--iq-replay-no-stt") {
            cfg.requested = true;
            cfg.iqReplay = true;
            cfg.iqReplayStt = false;
        } else if (key == "--gui-iq-replay-wav" || key == "--iq-replay-wav") {
            cfg.requested = true;
            cfg.iqReplay = true;
            if (auto value = requireValue(key.c_str())) cfg.iqReplayWavPath = *value;
        } else if (key == "--gui-iq-replay-result" || key == "--iq-replay-result") {
            cfg.requested = true;
            cfg.iqReplay = true;
            if (auto value = requireValue(key.c_str())) cfg.iqReplayResultPath = *value;
        } else if (key == "--gui-capture-label" || key == "--capture-label") {
            cfg.requested = true;
            if (auto value = requireValue(key.c_str())) {
                cfg.iqCaptureLabel = *value;
                cfg.iqCapture = true;
            }
        } else if (key == "--gui-capture-root" || key == "--capture-root") {
            cfg.requested = true;
            if (auto value = requireValue(key.c_str())) {
                cfg.iqCaptureRoot = *value;
                cfg.iqCapture = true;
            }
        } else if (key == "--gui-capture-seconds" || key == "--capture-seconds") {
            cfg.requested = true;
            if (auto value = requireValue(key.c_str())) {
                double seconds = 0.0;
                if (guiRuntimeParseDouble(*value, seconds) && seconds > 0.0) {
                    const double clamped = std::clamp(seconds, 1.0, 3600.0);
                    cfg.iqCaptureDurationMs = static_cast<int>(std::lround(clamped * 1000.0));
                    cfg.iqCapture = true;
                } else {
                    cfg.warnings.push_back("invalid GUI capture seconds: " + *value);
                }
            }
        } else if (key == "--gui-capture-ms" || key == "--capture-ms") {
            cfg.requested = true;
            if (auto value = requireValue(key.c_str())) {
                int ms = 0;
                if (guiRuntimeParseInt(*value, ms) && ms > 0) {
                    cfg.iqCaptureDurationMs = std::clamp(ms, 1000, 3600000);
                    cfg.iqCapture = true;
                } else {
                    cfg.warnings.push_back("invalid GUI capture milliseconds: " + *value);
                }
            }
        } else if (key == "--gui-p25-late-entry-audio-probe" ||
                   key == "--p25-late-entry-audio-probe" ||
                   key == "--gui-p25-field-audio-probe" ||
                   key == "--p25-field-audio-probe") {
            cfg.requested = true;
            cfg.p25LateEntryAudioProbe = true;
        } else if (key == "--debug-stage" || key == "--p25-debug-stage") {
            cfg.requested = true;
            if (auto value = requireValue(key.c_str())) {
                cfg.debugStage = *value;
            }
        } else if (key == "--gui-startup-dry-run" || key == "--gui-dry-run") {
            cfg.requested = true;
            cfg.dryRun = true;
        } else if (key == "--gui-startup-self-test" || key == "--gui-self-test") {
            cfg.requested = true;
            cfg.selfTest = true;
            if (auto value = guiRuntimeArgValue(i, argc, argv, arg)) {
                cfg.selfTestPath = *value;
            }
        } else if (key == "--gui-exit-after-ms") {
            cfg.requested = true;
            if (auto value = requireValue(key.c_str())) {
                int ms = 0;
                if (guiRuntimeParseInt(*value, ms)) cfg.exitAfterMs = ms;
                else cfg.warnings.push_back("invalid GUI exit timeout: " + *value);
            }
        } else if (key == "--gui-require-clear-audio") {
            cfg.requested = true;
            cfg.requireClearAudio = true;
        } else if (key == "--gui-clear-audio-timeout-ms" ||
                   key == "--gui-self-test-timeout-ms") {
            cfg.requested = true;
            if (auto value = requireValue(key.c_str())) {
                int ms = 0;
                if (guiRuntimeParseInt(*value, ms)) cfg.clearAudioTimeoutMs = ms;
                else cfg.warnings.push_back("invalid GUI clear-audio timeout: " + *value);
            }
        }
    }

    if (cfg.p25GrantTest && cfg.p25ControlHz <= 0.0 && cfg.frequencyHz > 0.0) {
        cfg.p25ControlHz = cfg.frequencyHz;
    }
    if (cfg.p25Monitor && cfg.p25ControlHz <= 0.0 && cfg.frequencyHz > 0.0) {
        cfg.p25ControlHz = cfg.frequencyHz;
    }
    if (cfg.selfTest && cfg.exitAfterMs <= 0 && !cfg.requireClearAudio) {
        cfg.exitAfterMs = (cfg.iqReplay && cfg.iqReplayAutoPlay)
            ? std::clamp(cfg.iqReplayDurationMs + 4000, 2500, 900000)
            : 1800;
    }
    if (cfg.requireClearAudio && cfg.clearAudioTimeoutMs <= 0) {
        cfg.clearAudioTimeoutMs = 300000;
    }
    if (cfg.defaultAudio || cfg.p25GrantTest || cfg.requireClearAudio) {
        cfg.requested = true;
    }
    return cfg;
}

// Shared atomics / diag mirror / cadence: see P25AppGlobals.h (ISS-0004 Phase 3)

// P25VoiceDiagCode: see P25VoiceDecode.h (ISS-0004 Phase 5)

static_assert(static_cast<int>(P25VoiceDiagCode::Phase2WrongSlot) == static_cast<int>(P25FollowDiagCode::Phase2WrongSlot),
    "P25 follow state-machine diag codes must match receiver diagnostics.");
static_assert(static_cast<int>(P25VoiceDiagCode::Decoding) == static_cast<int>(P25FollowDiagCode::Decoding),
    "P25 follow state-machine diag codes must match receiver diagnostics.");

const char* p25VoiceDiagLabel(P25VoiceDiagCode code)
{
    switch (code) {
        case P25VoiceDiagCode::SkippedEncrypted: return "encrypted, skipped";
        case P25VoiceDiagCode::WaitingForClearGrant: return "waiting clear grant";
        case P25VoiceDiagCode::NoSync: return "no voice sync";
        case P25VoiceDiagCode::NidUnlocked: return "NID not validated";
        case P25VoiceDiagCode::BackendMissing: return "voice backend missing";
        case P25VoiceDiagCode::Phase2Unsupported: return "Phase 2 protocol mismatch";
        case P25VoiceDiagCode::Phase2AudioLockMissing: return "Phase 2 audio lock missing";
        case P25VoiceDiagCode::Phase2MetadataMissing: return "Phase 2 metadata missing";
        case P25VoiceDiagCode::Phase2MaskMissing: return "Phase 2 TDMA framing/mask incomplete";
        case P25VoiceDiagCode::Phase2MaskAppliedNoMacCrc: return "Phase 2 mask applied, no MAC CRC";
        case P25VoiceDiagCode::Phase2EssMissing: return "Phase 2 ESS missing";
        case P25VoiceDiagCode::Phase2WrongSlot: return "Phase 2 wrong TDMA slot";
        case P25VoiceDiagCode::Phase2AmbeRejected: return "Phase 2 AMBE rejected";
        case P25VoiceDiagCode::Phase2LateEntryWaiting: return "Phase 2 late entry, waiting ESS";
        case P25VoiceDiagCode::NoLduVoice: return "waiting voice frames";
        case P25VoiceDiagCode::NoDecodedAudio: return "no decoded audio";
        case P25VoiceDiagCode::Decoding: return "decoding clear voice";
        case P25VoiceDiagCode::Idle:
        default: return "idle";
    }
}

// RfSquelchMetrics: see P25VoiceDecode.h (ISS-0004 Phase 5)

std::string trimCopy(const std::string& s)
{
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

std::string modeToString(DemodMode mode)
{
    switch (mode) {
        case DemodMode::WFM: return "WFM";
        case DemodMode::AM: return "AM";
        case DemodMode::USB: return "USB";
        case DemodMode::LSB: return "LSB";
        case DemodMode::CW: return "CW";
        case DemodMode::AUTO: return "AUTO";
        case DemodMode::NFM:
        default: return "NFM";
    }
}

QString modeToQString(DemodMode mode)
{
    return QString::fromStdString(modeToString(mode));
}

DemodMode modeFromString(std::string text)
{
    for (auto& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (text == "wfm") return DemodMode::WFM;
    if (text == "am") return DemodMode::AM;
    if (text == "usb") return DemodMode::USB;
    if (text == "lsb") return DemodMode::LSB;
    if (text == "cw") return DemodMode::CW;
    if (text == "auto") return DemodMode::AUTO;
    return DemodMode::NFM;
}

const std::vector<BandPlanEntry>& builtInBandPlans()
{
    static const std::vector<BandPlanEntry> plans = {
        {"LF Amateur CW", 135.7e3, 137.8e3, DemodMode::CW, 500.0, 500.0, 10.0},
        {"MF Amateur CW", 472.0e3, 479.0e3, DemodMode::CW, 500.0, 500.0, 10.0},
        {"MW Broadcast AM", 531.0e3, 1701.0e3, DemodMode::AM, 10000.0, 4500.0, 9000.0},
        {"160m CW", 1.800e6, 1.840e6, DemodMode::CW, 1000.0, 900.0, 100.0},
        {"160m LSB", 1.840e6, 2.000e6, DemodMode::LSB, 6000.0, 3000.0, 100.0},
        {"120m Broadcast AM", 2.300e6, 2.495e6, DemodMode::AM, 10000.0, 4500.0, 5000.0},
        {"90m Broadcast AM", 3.200e6, 3.400e6, DemodMode::AM, 10000.0, 4500.0, 5000.0},
        {"80m CW", 3.500e6, 3.570e6, DemodMode::CW, 1000.0, 900.0, 100.0},
        {"80m LSB", 3.570e6, 4.000e6, DemodMode::LSB, 6000.0, 3000.0, 100.0},
        {"75m Broadcast AM", 3.900e6, 4.000e6, DemodMode::AM, 10000.0, 4500.0, 5000.0},
        {"60m Broadcast AM", 4.750e6, 5.060e6, DemodMode::AM, 10000.0, 4500.0, 5000.0},
        {"49m Broadcast AM", 5.900e6, 6.200e6, DemodMode::AM, 10000.0, 4500.0, 5000.0},
        {"40m CW", 7.000e6, 7.050e6, DemodMode::CW, 1000.0, 900.0, 100.0},
        {"40m LSB", 7.050e6, 7.300e6, DemodMode::LSB, 6000.0, 3000.0, 100.0},
        {"41m Broadcast AM", 7.200e6, 7.450e6, DemodMode::AM, 10000.0, 4500.0, 5000.0},
        {"31m Broadcast AM", 9.400e6, 9.900e6, DemodMode::AM, 10000.0, 4500.0, 5000.0},
        {"30m CW/Data", 10.100e6, 10.150e6, DemodMode::CW, 1000.0, 900.0, 100.0},
        {"25m Broadcast AM", 11.600e6, 12.100e6, DemodMode::AM, 10000.0, 4500.0, 5000.0},
        {"22m Broadcast AM", 13.570e6, 13.870e6, DemodMode::AM, 10000.0, 4500.0, 5000.0},
        {"20m CW", 14.000e6, 14.070e6, DemodMode::CW, 1000.0, 900.0, 100.0},
        {"20m USB", 14.070e6, 14.350e6, DemodMode::USB, 6000.0, 3000.0, 100.0},
        {"19m Broadcast AM", 15.100e6, 15.800e6, DemodMode::AM, 10000.0, 4500.0, 5000.0},
        {"17m CW", 18.068e6, 18.095e6, DemodMode::CW, 1000.0, 900.0, 100.0},
        {"17m USB", 18.095e6, 18.168e6, DemodMode::USB, 6000.0, 3000.0, 100.0},
        {"16m Broadcast AM", 17.480e6, 17.900e6, DemodMode::AM, 10000.0, 4500.0, 5000.0},
        {"15m CW", 21.000e6, 21.070e6, DemodMode::CW, 1000.0, 900.0, 100.0},
        {"15m USB", 21.070e6, 21.450e6, DemodMode::USB, 6000.0, 3000.0, 100.0},
        {"13m Broadcast AM", 21.450e6, 21.850e6, DemodMode::AM, 10000.0, 4500.0, 5000.0},
        {"12m CW", 24.890e6, 24.915e6, DemodMode::CW, 1000.0, 900.0, 100.0},
        {"12m USB", 24.915e6, 24.990e6, DemodMode::USB, 6000.0, 3000.0, 100.0},
        {"11m Broadcast AM", 25.670e6, 26.100e6, DemodMode::AM, 10000.0, 4500.0, 5000.0},
        {"10m CW", 28.000e6, 28.070e6, DemodMode::CW, 1000.0, 900.0, 100.0},
        {"10m USB", 28.070e6, 29.700e6, DemodMode::USB, 6000.0, 3000.0, 100.0},
        {"27 MHz CB AM/SSB", 26.965e6, 27.405e6, DemodMode::AM, 10000.0, 4500.0, 10000.0},
        {"FM Broadcast", 87.5e6, 108.0e6, DemodMode::WFM, 180000.0, 15000.0, 100000.0},
        {"Airband AM", 108.0e6, 137.0e6, DemodMode::AM, 20000.0, 9000.0, 8333.333},
        {"NOAA / Weather Sat", 137.0e6, 138.0e6, DemodMode::WFM, 34000.0, 15000.0, 5000.0},
        {"2m Amateur", 144.0e6, 148.0e6, DemodMode::NFM, 12500.0, 3000.0, 12500.0},
        {"Marine VHF", 156.0e6, 162.025e6, DemodMode::NFM, 25000.0, 4500.0, 25000.0},
        {"70cm Amateur", 430.0e6, 450.0e6, DemodMode::NFM, 12500.0, 3000.0, 12500.0},
        {"AU UHF CB", 476.4125e6, 477.4125e6, DemodMode::NFM, 12500.0, 3000.0, 12500.0},
    };
    return plans;
}

const BandPlanEntry* findBandPlanForFrequency(double freqHz)
{
    if (!std::isfinite(freqHz)) return nullptr;
    for (const auto& p : builtInBandPlans()) {
        if (freqHz >= p.startHz && freqHz <= p.endHz) return &p;
    }
    return nullptr;
}

// SavedFrequency: see CliApp.h (ISS-0004 Phase 7)

static QString savedFrequenciesPath()
{
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(appData);
    return appData + "/saved_frequencies.json";
}

std::vector<SavedFrequency> loadSavedFrequencies()
{
    std::vector<SavedFrequency> out;
    std::ifstream f(savedFrequenciesPath().toStdString());
    if (!f.is_open()) return out;
    try {
        json arr;
        f >> arr;
        if (!arr.is_array()) return out;
        for (const auto& item : arr) {
            SavedFrequency sf;
            sf.name = item.value("name", std::string("Saved Frequency"));
            sf.freqHz = item.value("freqHz", 100e6);
            sf.mode = modeFromString(item.value("mode", std::string("AUTO")));
            sf.bandwidthHz = item.value("bandwidthHz", 180000.0);
            sf.lpfHz = item.value("lpfHz", 15000.0);
            sf.lpfEnabled = item.value("lpfEnabled", true);
            sf.squelchDb = item.value("squelchDb", -105.0);
            sf.tags = item.value("tags", std::string());
            if (std::isfinite(sf.freqHz) && sf.freqHz > 0.0) out.push_back(sf);
        }
    } catch (const std::exception& ex) {
        spdlog::warn("Failed to load saved_frequencies.json: {}", ex.what());
    }
    return out;
}

void saveSavedFrequencies(const std::vector<SavedFrequency>& freqs)
{
    json arr = json::array();
    for (const auto& sf : freqs) {
        arr.push_back({
            {"name", sf.name},
            {"freqHz", sf.freqHz},
            {"mode", modeToString(sf.mode)},
            {"bandwidthHz", sf.bandwidthHz},
            {"lpfHz", sf.lpfHz},
            {"lpfEnabled", sf.lpfEnabled},
            {"squelchDb", sf.squelchDb},
            {"tags", sf.tags},
        });
    }
    try {
        std::ofstream f(savedFrequenciesPath().toStdString());
        if (f.is_open()) f << arr.dump(2);
    } catch (const std::exception& ex) {
        spdlog::warn("Failed to save saved_frequencies.json: {}", ex.what());
    }
}

void populateSavedFrequencyTable(QTableWidget* table, const std::vector<SavedFrequency>& freqs)
{
    if (!table) return;
    table->setRowCount(static_cast<int>(freqs.size()));
    for (int row = 0; row < static_cast<int>(freqs.size()); ++row) {
        const auto& sf = freqs[static_cast<size_t>(row)];
        table->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(sf.name)));
        table->setItem(row, 1, new QTableWidgetItem(QString::number(sf.freqHz / 1e6, 'f', 5)));
        table->setItem(row, 2, new QTableWidgetItem(modeToQString(sf.mode)));
        table->setItem(row, 3, new QTableWidgetItem(QString::number(sf.bandwidthHz / 1000.0, 'f', 1)));
        table->setItem(row, 4, new QTableWidgetItem(QString::fromStdString(sf.tags)));
    }
}

// P25 talkgroup/CC/channel-ID registry: see P25TalkgroupRegistry.h (ISS-0004 Phase 2)



qint64 p25Phase2EffectiveAudioTailGraceMs() noexcept
{
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (p25RecentSpeakerOutputActive(nowMs, kP25Phase2SpeakerFollowHoldMs)) {
        return kP25Phase2SpeakerAudioTailGraceMs;
    }
    return kP25Phase2AudioTailGraceMs;
}


bool p25Phase2SessionHasHardTargetAcquire(const Receiver& rx) noexcept
{
    const auto& sustain = rx.p25SessionState.sustain;
    const bool acquired =
        sustain.hadSuccessfulEmit ||
        sustain.peakDecodedFrames > 0 ||
        sustain.cumulativeAudioSamples > 0 ||
        p25DiagTargetHardClear(rx.p25VoiceDiagnostics);
    if (rx.p25Phase2WideReacquireHoldWindows > 0 && !acquired) return false;
    return acquired;
}

bool p25Phase2SessionHadVoiceLock(const Receiver& rx) noexcept
{
    return p25Phase2SessionHasHardTargetAcquire(rx);
}

// Soft telemetry only proves that a Phase-2 eye may be present. It must not move
// a windowed decoder into sustain mode before target-slot MAC/ESS/audio proves
// the call; SDRTrunk can stream dibits continuously, while this path still needs
// full acquisition context until hard call state is known.
bool p25Phase2SessionHadBurstEye(const Receiver& rx) noexcept
{
    const auto& sustain = rx.p25SessionState.sustain;
    const bool voiceLock = p25Phase2SessionHadVoiceLock(rx);
    if (rx.p25Phase2WideReacquireHoldWindows > 0 &&
        !voiceLock &&
        !sustain.hadSuccessfulEmit) {
        return false;
    }
    if (voiceLock) return true;
    if (sustain.peakPhase2Bursts >= 1) return true;
    if (rx.p25VoiceDiagnostics.phase2Bursts > 0) return true;
    return rx.p25VoiceLiveDecoder.cqpskLockValid();
}

bool p25Phase2SessionSpeakerSustainActive(const Receiver& rx) noexcept
{
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const auto& sustain = rx.p25SessionState.sustain;
    if (!sustain.hadSuccessfulEmit) return false;
    if (p25RecentSpeakerOutputActive(nowMs, kP25Phase2SpeakerFollowHoldMs)) return true;
    // Keep sustain streaming after the last PCM push so inter-slot silence and
    // CQPSK re-lock do not drop us back to cold/unacquired hops (032428 droughts).
    return sustain.lastEmitMs > 0 && (nowMs - sustain.lastEmitMs) <= 20000;
}

bool p25Phase2EstablishedClearVoiceStreamingLocked(const Receiver& rx) noexcept
{
    if (!rx.p25VoiceDecodeEnabled || !rx.p25VoicePhase2 || rx.p25VoiceEncrypted) {
        return false;
    }
    // After successful emit, ignore wide-reacquire hold flags so short sustain
    // hops stay selected (see NeedsWideReacquireWindowLocked).
    if (rx.p25Phase2WideReacquireHoldWindows > 0 &&
        !rx.p25SessionState.sustain.hadSuccessfulEmit) {
        return false;
    }

    const auto& sustain = rx.p25SessionState.sustain;
    const P25VoiceDiagSnapshot& diag = rx.p25VoiceDiagnostics;
    const bool decodedAudio =
        sustain.hadSuccessfulEmit ||
        sustain.cumulativeAudioSamples > 0 ||
        sustain.peakDecodedFrames > 0 ||
        diag.decodedFrames > 0 ||
        diag.phase2AmbeAcceptedFrames > 0 ||
        diag.audioSamples > 0;
    const bool selectedSlotEvidence =
        sustain.peakPhase2TargetVoiceCodewords > 0 ||
        diag.phase2TargetVoiceCodewords > 0 ||
        p25DiagTargetHardClear(diag);
    const bool targetClearEvidence =
        p25DiagTargetHardClear(diag);
    const bool maskOrStickyLock =
        sustain.hadBootstrapMaskLock ||
        sustain.peakPhase2MaskedBursts >= 1 ||
        diag.phase2MaskedBursts > 0 ||
        rx.p25VoiceLiveDecoder.cqpskLockValid();
    const bool selectedClearTrafficStreaming =
        rx.p25VoiceClearKnown &&
        rx.p25VoiceMaskParamsKnown &&
        rx.p25VoiceTdmaSlotKnown &&
        selectedSlotEvidence &&
        (maskOrStickyLock || targetClearEvidence) &&
        (targetClearEvidence ||
         sustain.hadSuccessfulEmit);

    return (decodedAudio && selectedSlotEvidence && maskOrStickyLock &&
            (targetClearEvidence || sustain.hadSuccessfulEmit)) ||
        selectedClearTrafficStreaming;
}

double p25Phase2EffectiveRollingWindowSeconds(const Receiver& rx) noexcept
{
    if (p25Phase2SessionSpeakerSustainActive(rx) ||
        p25Phase2EstablishedClearVoiceStreamingLocked(rx) ||
        p25Phase2SessionHadVoiceLock(rx)) {
        return kP25Phase2VoiceDecodeActiveRollingSeconds;
    }
    return kP25Phase2VoiceDecodeWindowSeconds;
}

bool p25TrustedControlOffsetForPhase2Traffic(double controlFreqHz,
                                                    qint64 nowMs,
                                                    double* outOffsetHz) noexcept
{
    if (!outOffsetHz) return false;
    *outOffsetHz = 0.0;
    if (!std::isfinite(controlFreqHz) || controlFreqHz <= 0.0) return false;
    const double trustedControlFreqHz = gP25LastTrustedControlFreqHz.load(std::memory_order_acquire);
    const double trustedControlOffsetHz = gP25LastTrustedControlOffsetHz.load(std::memory_order_acquire);
    const long long trustedControlOffsetMs = gP25LastTrustedControlOffsetMs.load(std::memory_order_acquire);
    if (trustedControlOffsetMs <= 0) return false;
    if (nowMs <= 0) nowMs = QDateTime::currentMSecsSinceEpoch();
    if (nowMs - static_cast<qint64>(trustedControlOffsetMs) > kP25Phase2ControlCarryFreshMs) return false;
    if (!std::isfinite(trustedControlFreqHz) || std::abs(trustedControlFreqHz - controlFreqHz) > 50.0) return false;
    if (!std::isfinite(trustedControlOffsetHz)) return false;
    const double absOffsetHz = std::abs(trustedControlOffsetHz);
    if (absOffsetHz < kP25Phase2ControlCarryOffsetMinHz ||
        absOffsetHz > kP25Phase2ControlCarryOffsetMaxHz) {
        return false;
    }
    *outOffsetHz = std::clamp(trustedControlOffsetHz,
                              -kP25Phase2TrafficTargetOffsetMaxHz,
                              kP25Phase2TrafficTargetOffsetMaxHz);
    return true;
}

void p25SeedPhase2TrafficOffsetFromControl(Receiver& rx,
                                                  double offsetHz,
                                                  int trust) noexcept
{
    if (!std::isfinite(offsetHz)) return;
    const double absOffsetHz = std::abs(offsetHz);
    if (absOffsetHz < kP25Phase2ControlCarryOffsetMinHz ||
        absOffsetHz > kP25Phase2TrafficTargetOffsetMaxHz) {
        return;
    }
    const double clampedOffsetHz = std::clamp(offsetHz,
                                             -kP25Phase2TrafficTargetOffsetMaxHz,
                                             kP25Phase2TrafficTargetOffsetMaxHz);
    rx.p25Phase2TrafficTargetOffsetKnown = true;
    rx.p25Phase2TrafficTargetOffsetHz = clampedOffsetHz;
    rx.p25Phase2TrafficTargetOffsetTrust = std::clamp(trust, 0, kP25Phase2TrafficTargetOffsetVerifiedTrust);
    rx.p25Phase2TrafficTargetOffsetMisses = 0;
    if (rx.p25TrafficRetunesPrimary) {
        rx.p25AfcFrozen = false;
        rx.p25FrozenAfcOffsetHz = clampedOffsetHz;
    }
}

int p25Phase2AdaptiveVoiceDecodeCadenceMs() noexcept
{
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    // During active speaker sustain, keep a fixed near-live cadence.  Scaling
    // scheduler sleep to the last DSP pass duration created positive feedback:
    // slow 1–4 s acquire windows stretched the gap between sustain chunks and
    // produced blocky one-burst-then-silence audio.
    if (p25RecentSpeakerOutputActive(nowMs, kP25Phase2SpeakerFollowHoldMs)) {
        return kP25Phase2VoiceDecodeSpeakerCadenceMs;
    }
    return kP25Phase2VoiceDecodeCadenceMs;
}

int p25Phase2AdaptiveVoiceDecodeCadenceMs(const Receiver& rx) noexcept
{
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    // Keep near-live hop rate for the whole call after first emit — not only
    // while the 2.5 s speaker-hold flag is warm (capture 20260807_231232).
    if (p25RecentSpeakerOutputActive(nowMs, kP25Phase2SpeakerFollowHoldMs) ||
        (rx.p25VoicePhase2 && rx.p25SessionState.sustain.hadSuccessfulEmit)) {
        return kP25Phase2VoiceDecodeSpeakerCadenceMs;
    }
    const bool coldAcquire =
        rx.p25IndependentTrafficSource &&
        rx.p25VoicePhase2 &&
        rx.p25VoiceDecodeEnabled &&
        !p25Phase2SessionHadBurstEye(rx) &&
        !rx.p25SessionState.sustain.hadSuccessfulEmit;
    if (coldAcquire) return kP25Phase2VoiceDecodeColdCadenceMs;
    return kP25Phase2VoiceDecodeCadenceMs;
}

bool p25Phase2SpeakerSustainDecodeActive() noexcept
{
    // Global hint used for pending-job depth; prefer speaker-hold, but do not
    // starve the pipeline the moment hold expires mid-call.
    return p25RecentSpeakerOutputActive(QDateTime::currentMSecsSinceEpoch(),
                                        kP25Phase2SpeakerFollowHoldMs);
}

size_t p25VoiceDecodeMaxPendingJobsNow(bool speakerSustainHint) noexcept
{
    // Capture 20260807_231232: worker-busy starved unique VCW feed (97 busy
    // logs, feedRatio≈0.25). Keep a short pipeline so 30–60 ms hops are not
    // dropped while a 25–30 ms sticky decode is finishing.
    return (speakerSustainHint || p25Phase2SpeakerSustainDecodeActive())
        ? kP25VoiceDecodeMaxPendingJobsSpeaker
        : kP25VoiceDecodeMaxPendingJobs;
}

size_t p25VoiceDecodeMaxPendingJobsNow() noexcept
{
    return p25VoiceDecodeMaxPendingJobsNow(false);
}

bool p25Phase2HasStableSuperframeLockLocked(const Receiver& rx) noexcept
{
    const P25VoiceDiagSnapshot& diag = rx.p25VoiceDiagnostics;
    const auto& sustain = rx.p25SessionState.sustain;
    const long long sfBursts = std::max(diag.phase2SuperframeBursts, sustain.peakPhase2SuperframeBursts);
    const long long maskBursts = std::max(diag.phase2MaskedBursts, sustain.peakPhase2MaskedBursts);
    return (sfBursts >= 6 && maskBursts >= 3) ||
           (rx.p25Phase2RecentSuperframeMaskLock && sfBursts >= 3) ||
           (sustain.hadBootstrapMaskLock && sustain.hadSuccessfulEmit);
}

bool p25Phase2NeedsWideReacquireWindowLocked(const Receiver& rx) noexcept
{
    if (!rx.p25VoiceDecodeEnabled || !rx.p25VoicePhase2) return false;
    const P25VoiceDiagSnapshot& diag = rx.p25VoiceDiagnostics;
    const auto& sustain = rx.p25SessionState.sustain;
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const bool recentSpeaker =
        p25RecentSpeakerOutputActive(nowMs, kP25Phase2SpeakerFollowHoldMs);
    const bool recentTraffic =
        rx.p25Phase2RecentTrafficEvidenceMs > 0 &&
        (nowMs - rx.p25Phase2RecentTrafficEvidenceMs) <= 15000;
    const bool currentStreamingEye =
        diag.phase2Bursts > 0 ||
        diag.phase2VoiceCodewords > 0 ||
        diag.phase2MaskedBursts > 0;

    // Capture 20260807_230551: after gate=emit, many windows have p2bursts=0
    // (opposite TDMA slot / brief silence). The old logic treated that as
    // "lost eye" and forced wide-reacquire with minFresh=120-160ms, producing
    // 80ms PCM islands every 300ms+ (blocky speech) while slot isolation stayed
    // correct. Once the speaker has opened, stay on short sustain hops.
    if (sustain.hadSuccessfulEmit) {
        if (recentSpeaker) return false;
        if (currentStreamingEye) return false;
        if (recentTraffic) return false;
        const qint64 silenceMs = (sustain.lastEmitMs > 0) ? (nowMs - sustain.lastEmitMs) : 0;
        // Only cold wide-reacquire after a real hang (not inter-slot silence).
        if (silenceMs > 0 && silenceMs < 8000) return false;
    }

    if (rx.p25Phase2WideReacquireHoldWindows > 0 &&
        !p25DiagTargetHardClear(diag) &&
        diag.decodedFrames == 0 &&
        !sustain.hadSuccessfulEmit) {
        return true;
    }
    // Mask-epoch repair must not steal the speaker-live path after emit.
    if (rx.p25Phase2MaskEpochRepairHoldWindows > 0 && !sustain.hadSuccessfulEmit) {
        return true;
    }
    if (currentStreamingEye) {
        return false;
    }
    const bool hadAnySync =
        sustain.peakPhase2Bursts > 0 ||
        sustain.peakDecodedFrames > 0 ||
        sustain.hadSuccessfulEmit ||
        diag.decodedFrames > 0 ||
        diag.audioSamples > 0 ||
        recentTraffic;
    // Do not reacquire solely because we once emitted — that is the chop path.
    return hadAnySync &&
        !sustain.hadSuccessfulEmit &&
        (diag.decodedFrames > 0 ||
         diag.audioSamples > 0 ||
         sustain.peakDecodedFrames > 0);
}

bool p25Phase2UseSustainDecodeWindowLocked(const Receiver& rx) noexcept
{
    if (!rx.p25VoiceDecodeEnabled || !rx.p25VoicePhase2) return false;
    if (rx.p25Phase2WideReacquireHoldWindows > 0 &&
        !rx.p25SessionState.sustain.hadSuccessfulEmit) {
        return false;
    }
    if (rx.p25Phase2MaskEpochRepairHoldWindows > 0 &&
        !rx.p25SessionState.sustain.hadSuccessfulEmit) {
        return false;
    }
    const P25VoiceDiagSnapshot& diag = rx.p25VoiceDiagnostics;
    const auto& sustain = rx.p25SessionState.sustain;
    const bool hasDecodedAudio =
        diag.decodedFrames > 0 ||
        diag.phase2AmbeAcceptedFrames > 0 ||
        diag.audioSamples > 0 ||
        sustain.peakDecodedFrames > 0 ||
        sustain.cumulativeAudioSamples > 0;
    const bool hasStableSuperframeMask =
        std::max(diag.phase2SuperframeBursts, sustain.peakPhase2SuperframeBursts) >= 8 &&
        std::max(diag.phase2MaskedBursts, sustain.peakPhase2MaskedBursts) >= 8 &&
        (diag.phase2TargetVoiceCodewords > 0 ||
         diag.phase2VoiceCodewords > 0 ||
         sustain.peakPhase2TargetVoiceCodewords > 0);
    const bool hasBootstrappedMaskLock =
        std::max(diag.phase2MaskedBursts, sustain.peakPhase2MaskedBursts) >= 1 &&
        std::max(diag.phase2SuperframeBursts, sustain.peakPhase2SuperframeBursts) >= 1 &&
        (diag.phase2TargetVoiceCodewords > 0 ||
         diag.phase2VoiceCodewords > 0 ||
         diag.decodedFrames > 0 ||
         sustain.peakDecodedFrames > 0 ||
         sustain.hadBootstrapMaskLock);
    const bool hasTrustedCallState =
        !rx.p25VoiceEncrypted &&
        p25DiagTargetHardClear(diag);
    const bool trustedAndFramed =
        hasTrustedCallState && hasStableSuperframeMask;
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const bool recentTrafficEvidence =
        rx.p25Phase2RecentTrafficEvidenceMs > 0 &&
        (nowMs - rx.p25Phase2RecentTrafficEvidenceMs) <= 8000;
    const bool recentSuperframeMaskLock =
        rx.p25Phase2RecentSuperframeMaskLock &&
        recentTrafficEvidence;
    const bool recentSpeaker =
        gP25AudioLastSpeakerOutputMs.load(std::memory_order_relaxed) > 0 &&
        nowMs - gP25AudioLastSpeakerOutputMs.load(std::memory_order_relaxed) <= 12000;
    return hasDecodedAudio ||
        sustain.hadSuccessfulEmit ||
        p25Phase2SessionSpeakerSustainActive(rx) ||
        trustedAndFramed ||
        (hasTrustedCallState && (hasStableSuperframeMask ||
                                 hasBootstrappedMaskLock ||
                                 recentTrafficEvidence ||
                                 recentSuperframeMaskLock)) ||
        (recentSpeaker && rx.p25VoiceMaskParamsKnown && hasTrustedCallState);
}


// P25VoiceDecodeProfile: see P25VoiceDecode.h

int p25Phase2StreamingDdcEnvOverride() noexcept
{
    // 1 = force on, -1 = force off, 0 = default (traffic-source only).
    static const int override = [] {
        const QByteArray value = qgetenv("SDR_TOWN_P25_STREAMING_DDC").trimmed().toLower();
        if (value == "1" || value == "true" || value == "yes" || value == "on") return 1;
        if (value == "0" || value == "false" || value == "no" || value == "off") return -1;
        return 0;
    }();
    return override;
}

bool p25Phase2StreamingDdcExperimentEnabled()
{
    return p25Phase2StreamingDdcEnvOverride() > 0;
}

P25LiveDecoderConfig p25DiagnosticDecoderConfig()
{
    P25LiveDecoderConfig cfg;
    // GUI/CLI diagnostics feed processIq() with independent or overlapping IQ
    // windows. A stateful DDC is only valid for strictly contiguous RF chunks.
    cfg.enableStreamingChannelDdc = false;
    cfg.enableC4fmFixedPhaseSearch = true;
    cfg.maxC4fmFixedPhaseCandidates = 10;
    cfg.maxFrameSyncs = 12;
    cfg.maxRawTsbkBlocksPerFrame = 8;
    cfg.enablePhase2Decode = true;
    return cfg;
}

P25LiveDecoderConfig p25RealtimeControlDecoderConfig()
{
    P25LiveDecoderConfig cfg = p25DiagnosticDecoderConfig();
    cfg.maxRawTsbkBlocksPerFrame = 8;
    cfg.realtimeVoiceSearch = false;
    cfg.stopC4fmSearchOnHardLock = true;
    cfg.stopCqpskSearchOnHardLock = false;
    cfg.maxCqpskSearchCandidates = 32;
    // A P25 trunking control channel is decoded for TSBK/MBT grant data here.
    // Phase 2 traffic burst/ESS search belongs on the granted voice receiver;
    // running it on the CC produces noisy "ESS unknown" telemetry and can make
    // GUI follow diagnostics diverge from the CLI waitgrant path.
    cfg.enablePhase2Decode = false;
    return cfg;
}

P25LiveDecoderConfig p25CliControlGrantDecoderConfig()
{
    P25LiveDecoderConfig cfg = p25RealtimeControlDecoderConfig();
    // P25 trunking control channels carry followable grants as Phase 1 TSBK/MBT
    // control messages even when the granted traffic channel is Phase 2 TDMA.
    // The control carrier can be C4FM or CQPSK/LSM, so keep the same full
    // C4FM/CQPSK acquisition family used by the GUI control worker.  Only the
    // traffic-channel Phase 2 burst/ESS search remains disabled here; the full
    // Phase 2 chain is enabled again by p25VoiceDecoderConfig(true) after a
    // grant is selected and the receiver is retuned to the traffic channel.
    cfg.enablePhase2Decode = false;
    cfg.realtimeVoiceSearch = false;
    cfg.stopC4fmSearchOnHardLock = true;
    cfg.enableCqpskSearch = true;
    cfg.stopCqpskSearchOnHardLock = false;
    cfg.maxFrameSyncs = 5;
    cfg.maxRawTsbkBlocksPerFrame = 8;
    cfg.maxC4fmFixedPhaseCandidates = 10;
    cfg.maxCqpskSearchCandidates = 32;
    return cfg;
}

P25LiveDecoderConfig p25VoiceDecoderConfig(bool phase2,
                                                  P25VoiceDecodeProfile profile)
{
    P25LiveDecoderConfig cfg = profile == P25VoiceDecodeProfile::Forensic
        ? p25DiagnosticDecoderConfig()
        : p25RealtimeVoiceDecoderConfig();
    // Default off here. Dedicated traffic sources enable streaming DDC in
    // p25VoiceDecoderConfigForReceiver() (DEC-0014). Forensic/CC/overlapping
    // diagnostic windows stay stateless block-channelize.
    cfg.enableStreamingChannelDdc = false;
    if (phase2 && profile == P25VoiceDecodeProfile::Realtime &&
        p25Phase2StreamingDdcExperimentEnabled()) {
        cfg.enableStreamingChannelDdc = true;
    }
    // Phase 1 C4FM/control-channel symbols are 4800 sps.
    // Phase 2 H-DQPSK air rate is 6000 sps (TIA-102 / SDRTrunk P25P2DecoderHDQPSK
    // `super(6000.0)`).  The old "same air symbol rate" 4800 override was a
    // regression: Gardner strobes at 4800 never line up with 180-symbol /
    // 30 ms TDMA slots, so clear RF windows report p2bursts=0 offline+live.
    // CQPSK vs C4FM acquisition is selected via phase2CqpskTrafficDemod.
    cfg.symbolRate = phase2 ? 6000.0 : 4800.0;
    cfg.channelBandwidthHz = 12500.0;
    // Phase-2 DDC/channelizer uses SDRTrunk HDQPSK pass 6500 / stop 7200
    // (p25ChannelizerLowpass). Phase-1 keeps 0.58*BW. Independent traffic
    // enables streaming DDC in p25VoiceDecoderConfigForReceiver (DEC-0014).
    // SDRTrunk HDQPSK defaults ~25 kHz (~4.17 SPS).  Keep >=8 SPS locally so
    // Gardner/TED has headroom after channelize clamps to symbolRate*8..10.
    cfg.workSampleRate = phase2 ? 48000.0 : 48000.0;
    cfg.phase2CqpskTrafficDemod = phase2;
    cfg.maxFrameSyncBitErrors = phase2 ? std::max(3, static_cast<int>(cfg.maxFrameSyncBitErrors))
                                       : cfg.maxFrameSyncBitErrors;
    if (phase2) {
        cfg.stopCqpskSearchOnHardLock = true;
        cfg.realtimeVoiceSearch = profile == P25VoiceDecodeProfile::Realtime;
        // Match SDRTrunk CostasLoop BW_300: bandwidth = 2π/300 ≈ 0.02094.
        cfg.cqpskCarrierLoopBandwidth = (2.0 * 3.14159265358979323846) / 300.0;
        cfg.cqpskCarrierLoopMaxCorrectionHz = 3000.0; // symbolRate/2, Costas max
        if (profile == P25VoiceDecodeProfile::Realtime) {
            // Live Phase-2 traffic must stay bounded.  Keep a cheap C4FM
            // Gardner fallback for odd captures, but do not run the offline
            // fixed-phase C4FM grid on every rolling voice window.  The real
            // Phase-2 traffic path is CQPSK/H-DQPSK and the decoder maintains a
            // sticky CQPSK lock after hard MAC/ESS evidence appears.
            //
            // 2026-07-10 field capture (TG30304 clear @ 420.725): forensic
            // CQPSK search found Phase-2 bursts/MAC while the previous realtime
            // budget (85ms / 20 candidates / 48 sync hits) stayed at p2bursts=0
            // for the entire follow.  Raise the acquisition ceiling enough to
            // reach the same eye without restoring the unbounded forensic grid.
            cfg.enableC4fmFixedPhaseSearch = false;
            cfg.maxC4fmFixedPhaseCandidates = 0;
            cfg.maxFrameSyncs = std::min<size_t>(cfg.maxFrameSyncs, 6);
            cfg.maxRawTsbkBlocksPerFrame = std::min<size_t>(cfg.maxRawTsbkBlocksPerFrame, 4);
            cfg.enablePhase1Decode = false;
            // Keep the first CQPSK pass cheap so cold one-RTL can try several
            // fresh IQ windows inside a short PTT instead of one 160–450 ms pass.
            // Once CQPSK locks, keep budget modest so the worker can submit
            // many short continuous windows (SDRTrunk stream rate) instead of
            // one heavy 300 ms job that leaves 1–2 s of RF undecodeed.
            cfg.realtimeDecodeBudgetMs = 220;
            cfg.maxPhase2SyncHits = 72;
            cfg.maxPhase2SuperframeLocks = 4;
        } else {
            // Phase-2 traffic is H-DQPSK/CQPSK-family.  The fixed C4FM grid is
            // useful for Phase-1 diagnostics but can create strong-looking
            // Phase-2 false positives at +/- tone offsets, which validates
            // clipped/scrambled audio in replay while the live path correctly
            // refuses it.  Forensic Phase-2 replay should spend its budget on
            // the CQPSK candidate family we use for real traffic follow.
            cfg.enableC4fmFixedPhaseSearch = false;
            cfg.maxC4fmFixedPhaseCandidates = 0;
            cfg.stopCqpskSearchOnHardLock = false;
            cfg.maxCqpskSearchCandidates = 0;
        }
        // Phase 2 traffic follow must remain real-time.  Leaving CQPSK search
        // unbounded can evaluate over a thousand candidates per rolling voice
        // window, which field testing showed as GUI stalls and missed follow
        // audio.  Keep acquisition broad enough for LSM/H-DQPSK, then rely on
        // the persistent CQPSK lock once hard MAC/ESS evidence appears.
        // Keep cold acquire cheap enough that annotate/commit still runs inside
        // the worker wall.  28–32 candidates @ ~560ms total was still cutting
        // off mask/audio candidates on slower diagnostic runs, so keep a small
        // margin below the 700 ms cold wall.
        // extraction on live one-RTL follows (080701).
        if (profile == P25VoiceDecodeProfile::Realtime) cfg.maxCqpskSearchCandidates = 96;
        cfg.cqpskLockMissTolerance = 32;
        // SDRTrunk P25P2DecoderHDQPSK: LPF (~6500/7200) + AGC only — no RRC.
        // RRC α=0.35 was a LSM/P1-style matched filter that mistimed the P2 eye.
        cfg.cqpskUseMatchedRrcFilter = false;
        cfg.cqpskRrcAlpha = 0.20;
    }
    return cfg;
}

P25LiveDecoderConfig p25VoiceDecoderConfigForReceiver(const Receiver& rx,
                                                      P25VoiceDecodeProfile profile)
{
    P25LiveDecoderConfig cfg = p25VoiceDecoderConfig(rx.p25VoicePhase2, profile);
    if (!rx.p25VoicePhase2) return cfg;
    // SDRTrunk HDQPSK.receive is this streaming DDC. DEC-0014 measured
    // default-on for every independent traffic source: 105622 TG 30003
    // slot 0 skip=97334 duty 0.685→0.095 (emptyWindows=80/89). Keep it
    // opt-in. Do not turn it on for CC or forensic overlapping windows.
    // SDR_TOWN_P25_STREAMING_DDC=1 enables it; locked hops are 80 ms.
    if (profile == P25VoiceDecodeProfile::Realtime &&
        rx.p25IndependentTrafficSource &&
        p25Phase2StreamingDdcExperimentEnabled()) {
        cfg.enableStreamingChannelDdc = true;
    }
    // SDRTrunk queues Phase-2 voice until PTT/ESS establishes encryption state;
    // it does not let AMBE plausibility choose a sticky XOR mask phase. Field
    // replay at skip=346000 showed zero-score AMBE-only phases poisoning MAC/ESS
    // acquisition before the real phase=0 CRC-valid window arrived.
    cfg.allowPhase2SoftAmbeMaskPhaseLock = false;
    cfg.phase2PreferredTdmaSlotKnown = rx.p25VoiceTdmaSlotKnown;
    cfg.phase2PreferredTdmaSlot = static_cast<uint8_t>(rx.p25VoiceTdmaSlot & 0x01u);

    const double trafficHz = rx.p25TrafficVoiceFreqHz > 0.0 ? rx.p25TrafficVoiceFreqHz : rx.freqHz;
    const double controlHz = rx.p25TrafficControlFreqHz > 0.0 ? rx.p25TrafficControlFreqHz : 0.0;
    if (rx.p25IndependentTrafficSource && profile == P25VoiceDecodeProfile::Realtime) {
        // Capture 20260712_021852: broader 56/170ms cold windows still left
        // p2bursts≈0 while worker jobs occasionally ran multi-second and hit
        // decode-wall-timeout. Prefer more frequent ~95–110ms eyes with hard-lock
        // stop (avoids ACCH hang mid-candidate) and spread offset recovery across
        // jobs instead of one heavy in-job probe.
        cfg.maxCqpskSearchCandidates = std::max(cfg.maxCqpskSearchCandidates, size_t{96});
        cfg.realtimeDecodeBudgetMs = std::max(cfg.realtimeDecodeBudgetMs, 220);
        cfg.maxPhase2SyncHits = std::max(cfg.maxPhase2SyncHits, size_t{72});
        cfg.maxPhase2SuperframeLocks = std::max(cfg.maxPhase2SuperframeLocks, size_t{4});
        cfg.stopCqpskSearchOnHardLock = true;
    }
    if (rx.p25TrafficRetunesPrimary && controlHz > 0.0 && trafficHz > 0.0) {
        const double ccBleedHz = std::abs(controlHz - trafficHz);
        // Wideband IQ that is still centred on the control channel can leak a
        // strong CC into a soft traffic channelizer.  One-RTL physical retune
        // (DEC-0015) parks the granted 12.5 kHz channel just right of DC
        // (~11 kHz), so a CC that is hundreds of kHz away is rejected by the
        // 12.5 kHz channelizer.  The old 6.5 kHz clamp on that path starved
        // Phase-2 CQPSK/RRC (2026-07-10 capture: live/realtime p2bursts=0
        // while forensic 12.5 kHz found MAC).
        const bool physicallyOnVoice =
            rx.p25IndependentTrafficSource &&
            std::isfinite(trafficHz) && trafficHz > 0.0;
        if (!physicallyOnVoice && ccBleedHz >= 150e3 && ccBleedHz <= 400e3) {
            cfg.channelBandwidthHz = 10000.0;
            if (profile == P25VoiceDecodeProfile::Realtime) {
                cfg.maxCqpskSearchCandidates = std::max(cfg.maxCqpskSearchCandidates, size_t{56});
            }
        } else if (physicallyOnVoice) {
            cfg.channelBandwidthHz = 12500.0;
        }
    }
    return cfg;
}

int p25CliDecodeScore(const P25LiveDecodeResult& result)
{
    int score = 0;
    for (const auto& block : result.rawTsbkBlocks) {
        if (block.fecDecoded && block.crcValid) score += 400;
        else if (block.fecDecoded) score += 4;
    }
    for (const auto& pdu : result.phase1Pdus) {
        if (pdu.headerFecDecoded && pdu.headerCrcValid) score += pdu.format == 23 ? 420 : 300;
        else if (pdu.headerFecDecoded) score += 4;
    }
    for (const auto& nid : result.nids) {
        if (nid.fecValidated) score += 80;
    }
    score += static_cast<int>(std::min<size_t>(result.syncs.size(), 8)) * 4;
    score += static_cast<int>(result.stats.phase2MacCrcValid) * 400;
    if (result.stats.phase2EssKnown) score += 300;
    if (result.stats.phase2MaskPhaseKnown) score += 200;
    const bool phase2MetadataTrusted = result.stats.phase2MacCrcValid > 0 || result.stats.phase2EssKnown;
    if (phase2MetadataTrusted) {
        score += static_cast<int>(std::min<size_t>(result.stats.phase2SuperframeBursts, 12)) * 10;
        score += static_cast<int>(std::min<size_t>(result.stats.phase2VoiceCodewords, 24));
    } else {
        score += static_cast<int>(std::min<size_t>(result.stats.phase2VoiceCodewords, 6));
    }
    if (result.stats.bestFrameSyncBitErrors >= 0) score += std::max(0, 12 - result.stats.bestFrameSyncBitErrors);
    if (result.stats.bestNidBchDistance >= 0) score += std::max(0, 16 - result.stats.bestNidBchDistance);
    return score;
}

bool p25ControlDecodeHasTrustedPayload(const P25LiveDecodeResult& result)
{
    const bool trustedTsbk = std::any_of(result.rawTsbkBlocks.begin(), result.rawTsbkBlocks.end(),
        [](const P25TsbkBlock& block) {
            return block.fecDecoded && block.crcValid;
        });
    if (trustedTsbk) return true;

    const bool trustedPdu = std::any_of(result.phase1Pdus.begin(), result.phase1Pdus.end(),
        [](const P25Phase1PduMessage& pdu) {
            return pdu.headerFecDecoded && pdu.headerCrcValid;
        });
    return trustedPdu || result.stats.phase2MacCrcValid > 0;
}

bool p25ControlDecodeHasValidatedNid(const P25LiveDecodeResult& result)
{
    if (result.stats.bestNidValid) return true;
    return std::any_of(result.nids.begin(), result.nids.end(), [](const P25Nid& nid) {
        return nid.fecValidated;
    });
}

bool p25ControlDecodeShouldProbeOffsets(const P25LiveDecodeResult& result)
{
    if (p25ControlDecodeHasTrustedPayload(result)) return false;
    if (p25ControlDecodeHasValidatedNid(result) && result.stats.bestNidBchDistance >= 0 &&
        result.stats.bestNidBchDistance <= 8) {
        return false;
    }
    return result.stats.bestFrameSyncBitErrors < 0 ||
        result.stats.bestFrameSyncBitErrors <= 5 ||
        result.stats.bestNidBchDistance >= 0 ||
        !result.syncs.empty();
}

P25LiveDecodeResult decodeP25ControlWithOffsetProbe(P25LiveDecoder& decoder,
                                                           const std::vector<std::complex<float>>& iq,
                                                           double sampleRateHz,
                                                           double centerFreqHz,
                                                           double nominalTargetHz,
                                                           double* effectiveTargetHz)
{
    static std::mutex offsetMutex;
    static double lockedNominalTargetHz = 0.0;
    static double lockedOffsetHz = 0.0;
    static int lockedTrust = 0;
    static qint64 lastProbeMs = 0;

    auto resetLockIfNeeded = [&]() {
        if (!std::isfinite(nominalTargetHz) || nominalTargetHz <= 0.0 ||
            lockedNominalTargetHz <= 0.0 ||
            std::abs(lockedNominalTargetHz - nominalTargetHz) > 50.0) {
            lockedNominalTargetHz = nominalTargetHz;
            lockedOffsetHz = 0.0;
            lockedTrust = 0;
            lastProbeMs = 0;
        }
    };

    double preferredOffsetHz = 0.0;
    int preferredTrust = 0;
    {
        std::lock_guard<std::mutex> lock(offsetMutex);
        resetLockIfNeeded();
        preferredOffsetHz = lockedOffsetHz;
        preferredTrust = lockedTrust;
    }

    P25LiveDecoder baseline = decoder;
    const double firstTargetHz = nominalTargetHz + ((preferredTrust > 0 && std::abs(preferredOffsetHz) <= 18000.0)
        ? preferredOffsetHz
        : 0.0);
    auto best = decoder.processIq(iq, sampleRateHz, centerFreqHz, firstTargetHz);
    P25LiveDecoder bestDecoder = decoder;
    double bestTargetHz = firstTargetHz;
    int bestScore = p25CliDecodeScore(best);
    const bool firstHardLock = p25ControlDecodeHasTrustedPayload(best);

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    bool shouldProbe = !firstHardLock && p25ControlDecodeShouldProbeOffsets(best);
    {
        std::lock_guard<std::mutex> lock(offsetMutex);
        if (preferredTrust <= 0 && lastProbeMs > 0 && nowMs - lastProbeMs < 8000) {
            shouldProbe = false;
        } else if (preferredTrust > 0 && lastProbeMs > 0 && nowMs - lastProbeMs < 10000 &&
                   p25ControlDecodeHasValidatedNid(best)) {
            shouldProbe = false;
        }
        if (shouldProbe) lastProbeMs = nowMs;
    }

    if (shouldProbe) {
        std::vector<double> offsets;
        auto addOffset = [&](double hz) {
            if (!std::isfinite(hz) || std::abs(hz) > 6500.0) return;
            if (std::any_of(offsets.begin(), offsets.end(), [&](double existing) {
                    return std::abs(existing - hz) < 50.0;
                })) {
                return;
            }
            offsets.push_back(hz);
        };
        addOffset(0.0);
        if (preferredTrust > 0) addOffset(preferredOffsetHz);
        for (double hz : {1250.0, -1250.0}) {
            addOffset(hz);
        }
        const bool baselineHasNoSync =
            best.stats.bestFrameSyncBitErrors < 0 &&
            best.stats.bestNidBchDistance < 0 &&
            best.syncs.empty();
        if (baselineHasNoSync) {
            addOffset(2500.0);
            addOffset(-2500.0);
        }

        for (double offsetHz : offsets) {
            const double candidateTargetHz = nominalTargetHz + offsetHz;
            if (std::abs(candidateTargetHz - firstTargetHz) < 50.0) continue;
            if (!std::isfinite(sampleRateHz) || sampleRateHz <= 0.0 ||
                std::abs(candidateTargetHz - centerFreqHz) > sampleRateHz * 0.48) {
                continue;
            }

            P25LiveDecoder candidateDecoder = baseline;
            auto candidate = candidateDecoder.processIq(iq, sampleRateHz, centerFreqHz, candidateTargetHz);
            const int candidateScore = p25CliDecodeScore(candidate);
            const bool candidateHard = p25ControlDecodeHasTrustedPayload(candidate);
            const bool bestHard = p25ControlDecodeHasTrustedPayload(best);
            const bool candidateNid = p25ControlDecodeHasValidatedNid(candidate);
            const bool bestNid = p25ControlDecodeHasValidatedNid(best);
            const bool betterNidDistance =
                candidate.stats.bestNidBchDistance >= 0 &&
                (best.stats.bestNidBchDistance < 0 ||
                 candidate.stats.bestNidBchDistance + 3 < best.stats.bestNidBchDistance);

            const bool candidateLargeOffset = std::abs(offsetHz) > 5000.0;
            const bool candidateStrongEnough =
                !candidateLargeOffset ||
                (candidateHard && candidateScore > bestScore + 300);

            if (candidateStrongEnough &&
                ((candidateHard && !bestHard) ||
                (candidateHard == bestHard && candidateNid && !bestNid) ||
                (candidateHard == bestHard && candidateNid == bestNid && betterNidDistance) ||
                candidateScore > bestScore + 24)) {
                bestScore = candidateScore;
                best = std::move(candidate);
                bestDecoder = std::move(candidateDecoder);
                bestTargetHz = candidateTargetHz;
                if (candidateHard) break;
            }
        }
    }

    const double selectedOffsetHz = bestTargetHz - nominalTargetHz;
    {
        std::lock_guard<std::mutex> lock(offsetMutex);
        resetLockIfNeeded();
        if (p25ControlDecodeHasTrustedPayload(best) || p25ControlDecodeHasValidatedNid(best)) {
            if (std::abs(selectedOffsetHz) >= 50.0) {
                lockedOffsetHz = selectedOffsetHz;
                lockedTrust = std::min(lockedTrust + (p25ControlDecodeHasTrustedPayload(best) ? 3 : 1), 20);
            } else {
                lockedOffsetHz *= 0.65;
                if (std::abs(lockedOffsetHz) < 50.0) lockedOffsetHz = 0.0;
                lockedTrust = std::max(0, lockedTrust - 1);
            }
        } else if (lockedTrust > 0) {
            --lockedTrust;
            if (lockedTrust == 0) lockedOffsetHz = 0.0;
        }
    }

    if (std::abs(selectedOffsetHz) >= 50.0) {
        std::ostringstream msg;
        msg << "P25 control target-offset probe selected "
            << std::fixed << std::setprecision(0) << selectedOffsetHz
            << " Hz (effective target " << std::setprecision(5) << (bestTargetHz / 1e6)
            << " MHz, score " << bestScore << ")";
        best.warnings.push_back(msg.str());
    }

    decoder = std::move(bestDecoder);
    if (effectiveTargetHz) *effectiveTargetHz = bestTargetHz;
    return best;
}

void p25SeedAnalyzerNacFromDecode(P25ControlChannelAnalyzer& analyzer,
                                         const P25LiveDecodeResult& result)
{
    for (const auto& nid : result.nids) {
        if (nid.fecValidated) {
            analyzer.setNac(nid.nac);
            return;
        }
    }
}

void printP25CliDecodeReport(const std::string& label,
                                    int devIndex,
                                    double centerFreqHz,
                                    double sampleRateHz,
                                    double targetHz,
                                    const P25LiveDecodeResult& result,
                                    P25ControlChannelAnalyzer& analyzer)
{
    p25SeedAnalyzerNacFromDecode(analyzer, result);
    const bool nidLock = p25DecodeResultHasNidLock(result);
    size_t trustedTsbk = 0;
    size_t trustedPhase1Pdu = 0;
    size_t trustedPhase2Mac = 0;
    size_t controlVoiceGrantEvents = 0;
    size_t controlResolvedVoiceGrantEvents = 0;
    size_t controlUnresolvedVoiceGrantEvents = 0;
    std::map<std::string, size_t> trustedControlOps;
    std::vector<P25PendingVoiceGrant> pendingVoiceGrants;
    for (const auto& block : result.rawTsbkBlocks) {
        if (block.fecDecoded && block.crcValid) {
            ++trustedTsbk;
            ++trustedControlOps[p25ControlAuditTsbkKey(block.bytes)];
        }
    }
    for (const auto& pdu : result.phase2MacPdus) {
        if (pdu.fecDecoded && pdu.crcValid) {
            ++trustedPhase2Mac;
            ++trustedControlOps[p25ControlAuditPhase2MacKey(pdu)];
        }
    }
    for (const auto& pdu : result.phase1Pdus) {
        if (pdu.headerFecDecoded && pdu.headerCrcValid) {
            ++trustedPhase1Pdu;
            ++trustedControlOps[p25ControlAuditPhase1PduKey(pdu)];
        }
    }
    const QString lockStage = p25LiveLockStageText(result, trustedTsbk);

    std::cout << label;
    if (devIndex >= 0) std::cout << " dev=" << devIndex;
    std::cout << " target=" << (targetHz / 1e6) << " MHz"
              << " center=" << (centerFreqHz / 1e6) << " MHz"
              << " sr=" << (sampleRateHz / 1e6) << " MHz"
              << " stage=" << lockStage.toStdString()
              << " path=" << (result.stats.demodPath.empty() ? "unknown" : result.stats.demodPath)
              << " cqpskLock=" << (result.stats.cqpskLockActive ? "active" : "new")
              << "/" << (result.stats.cqpskLockUsed ? "used" : "search")
              << "/" << (result.stats.cqpskLockUpdated ? "updated" : "held")
              << " cqpskPhase=" << result.stats.cqpskSymbolPhaseFraction
              << " cqpskFine=" << (result.stats.cqpskFineCorrectionApplied ? result.stats.cqpskFineRotationRad : 0.0)
              << " cqpskResidualHz=" << result.stats.cqpskResidualCarrierHz
              << " cqpskErrRms=" << result.stats.cqpskPhaseErrorRmsRad
              << " cqpskTrust=" << result.stats.cqpskLockTrustScore
              << " cqpskMiss=" << result.stats.cqpskLockMisses
              << " cqpskSticky=" << (result.stats.cqpskStickyOverride ? "yes" : "no")
              << " targetOffsetHz=" << result.stats.inputTargetOffsetHz
              << " chanSr=" << result.stats.channelSampleRate
              << " discMeanHz=" << result.stats.discriminatorMeanHz
              << " iq=" << result.stats.inputSamples
              << " symbols=" << result.stats.symbols
              << " softQ=" << result.stats.softDecisionQuality
              << " softLlr=" << result.stats.softBitLlrMean
              << " softLow=" << result.stats.softLowConfidenceSymbols << "/" << result.stats.softDecisionSymbols
              << " syncs=" << result.syncs.size()
              << " bestSyncErr=" << result.stats.bestFrameSyncBitErrors
              << " bestBit=" << result.stats.bestFrameSyncBitOffset
              << " bestAligned=" << (result.stats.bestFrameSyncBitAligned ? "yes" : "no")
              << " bestInv=" << (result.stats.bestFrameSyncInverted ? "yes" : "no")
              << " nidLock=" << (nidLock ? "yes" : "no")
              << " p2bursts=" << result.stats.phase2Bursts
              << " p2vcw=" << result.stats.phase2VoiceCodewords
              << " p2sf=" << result.stats.phase2SuperframeBursts
              << " p2mask=" << result.stats.phase2MaskedBursts
              << " p2phase=" << (result.stats.phase2MaskPhaseKnown ? std::to_string(result.stats.phase2MaskPhase) : std::string("-"))
              << "/" << result.stats.phase2MaskPhaseMacCrcValid
              << " p2phaseScore=" << result.stats.phase2MaskPhaseScore
              << " p1pdu=" << result.stats.phase1PduCrcValid << "/" << result.stats.phase1PduHeaders
              << " p1ambtc=" << result.stats.phase1AmbtcPdus
               << " p2mac=" << result.stats.phase2MacCrcValid << "/" << result.stats.phase2MacPdus
               << " " << p25Phase2AcchStatsText(result.stats).toStdString()
               << " p2ess=" << (result.stats.phase2EssKnown ? (result.stats.phase2EssEncrypted ? "enc" : "clear") : "unknown")
               << " p2isch=" << result.stats.phase2IschDecoded << "/" << result.stats.phase2IschSync
               << " p2syncAdj=" << result.stats.phase2SyncOffsetCorrections
               << "/" << result.stats.phase2SyncOffsetCorrectionDibits;
    if (result.stats.bestPhase2SyncErrors >= 0) {
        std::cout << " p2bestErr=" << result.stats.bestPhase2SyncErrors
                  << " p2bestDibit=" << result.stats.bestPhase2SyncDibitOffset;
    }
    if (result.stats.bestNidBchDistance >= 0) {
        std::cout << " bestNidDist=" << result.stats.bestNidBchDistance
                  << " bestNAC=0x" << std::hex << result.stats.bestNidNac << std::dec
                  << " bestDUID=0x" << std::hex << static_cast<int>(result.stats.bestNidRawDuid) << std::dec
                  << " bestNid=" << (result.stats.bestNidValid ? "valid" : "fail");
    }
    std::cout << " voiceBackend=" << (result.stats.voiceBackendAvailable ? "yes" : "no") << "\n";

    for (const auto& nid : result.nids) {
        std::cout << "  NID bit=" << nid.bitOffset
                  << " NAC=0x" << std::hex << nid.nac << std::dec
                  << " DUID=" << P25LiveDecoder::dataUnitIdToString(nid.duid)
                  << " fec=" << (nid.fecValidated ? "validated" : "fail")
                  << " corrected=" << nid.correctedBitErrors << "\n";
    }

    if (!result.rawTsbkBlocks.empty()) {
        std::cout << "  raw TSDU block candidates=" << result.rawTsbkBlocks.size()
                  << " trusted=" << trustedTsbk
                  << " (trusted means trellis-decoded and CRC-valid)\n";
        if (trustedTsbk == 0) {
            size_t shown = 0;
            for (const auto& block : result.rawTsbkBlocks) {
                if (++shown > 6) break;
                std::cout << "  candidate TSBK bit=" << block.bitOffset
                          << " fec=" << (block.fecDecoded ? "decoded" : "fail")
                          << " crc=" << (block.crcValid ? "ok" : "fail")
                          << " corrected=" << block.correctedDibitErrors;
                if (!block.bytes.empty()) {
                    std::cout << " raw=" << p25BytesToHex(block.bytes).toStdString();
                }
                std::cout << "\n";
            }
            if (result.rawTsbkBlocks.size() > 6) {
                std::cout << "  candidate TSBK list truncated at 6 rows\n";
            }
        }
        auto talkgroups = loadP25Talkgroups();
        bool changed = false;
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        for (const auto& block : result.rawTsbkBlocks) {
            if (!block.fecDecoded || !block.crcValid) continue;
            const auto rawHex = p25BytesToHex(block.bytes).toStdString();
            std::cout << "  trusted TSBK bit=" << block.bitOffset
                      << " corrected=" << block.correctedDibitErrors
                      << " raw=" << rawHex << "\n";
            const bool registryEligible = block.correctedDibitErrors <= kP25RegistryMaxCorrectedDibits;
            bool acceptedHighCorrectionGrant = false;
            const auto events = analyzer.ingestTsbk(block.bytes);
            for (const auto& ev : events) {
                std::cout << "    " << p25EventLogText(ev).toStdString() << "\n";
                if (p25ControlEventIsVoiceGrant(ev)) {
                    ++controlVoiceGrantEvents;
                    if (p25ControlEventIsResolvedVoiceGrant(ev)) ++controlResolvedVoiceGrantEvents;
                    else ++controlUnresolvedVoiceGrantEvents;
                    std::cout << "    " << p25GrantDetailLogText(ev).toStdString() << "\n";
                } else if (ev.type == P25ControlEventType::IdentifierUpdate && ev.phase2Candidate) {
                    std::cout << "    TDMA identifier table update: " << p25EventLogText(ev).toStdString() << "\n";
                }
                const bool eventRegistryEligible = p25TsbkEventRegistryEligible(block.correctedDibitErrors, ev);
                if (eventRegistryEligible) {
                    if (p25ControlEventIsVoiceGrant(ev) && !p25ControlEventIsResolvedVoiceGrant(ev)) {
                        p25RememberPendingVoiceGrant(pendingVoiceGrants, ev, block.correctedDibitErrors, nowMs);
                    }
                    changed = mergeP25TalkgroupEvent(talkgroups, targetHz, ev, nowMs) || changed;
                    if (!registryEligible && p25ControlEventIsResolvedVoiceGrant(ev)) {
                        acceptedHighCorrectionGrant = true;
                        std::cout << "    note: accepted resolved voice grant despite "
                                  << block.correctedDibitErrors
                                  << " corrected dibits (voice grant threshold "
                                  << kP25VoiceGrantMaxCorrectedDibits << ")\n";
                    }
                    if (ev.type == P25ControlEventType::IdentifierUpdate &&
                        p25ChannelIdentifierUsable(p25IdentifierFromEvent(ev))) {
                        for (const auto& resolved : p25ResolvePendingVoiceGrants(pendingVoiceGrants, analyzer, nowMs)) {
                            ++controlResolvedVoiceGrantEvents;
                            std::cout << "    pending-resolved after identifier ID "
                                      << static_cast<int>(ev.identifier) << ": "
                                      << p25EventLogText(resolved).toStdString() << "\n";
                            std::cout << "    " << p25GrantDetailLogText(resolved).toStdString() << "\n";
                            changed = mergeP25TalkgroupEvent(talkgroups, targetHz, resolved, nowMs) || changed;
                        }
                    }
                }
                if (!eventRegistryEligible &&
                    p25TsbkSessionIdentifierEligible(block.correctedDibitErrors, ev)) {
                    std::cout << "    note: kept high-correction identifier ID "
                              << static_cast<int>(ev.identifier)
                              << " in the current replay session for pending grant resolution; corrected="
                              << block.correctedDibitErrors
                              << " session threshold=" << kP25SessionIdentifierMaxCorrectedDibits << "\n";
                    for (const auto& resolved : p25ResolvePendingVoiceGrants(pendingVoiceGrants, analyzer, nowMs)) {
                        ++controlResolvedVoiceGrantEvents;
                        std::cout << "    pending-resolved after session identifier ID "
                                  << static_cast<int>(ev.identifier) << ": "
                                  << p25EventLogText(resolved).toStdString() << "\n";
                        std::cout << "    " << p25GrantDetailLogText(resolved).toStdString() << "\n";
                        changed = mergeP25TalkgroupEvent(talkgroups, targetHz, resolved, nowMs) || changed;
                    }
                }
                if (!eventRegistryEligible &&
                    p25TsbkPendingVoiceGrantEligible(block.correctedDibitErrors, ev)) {
                    p25RememberPendingVoiceGrant(pendingVoiceGrants, ev, block.correctedDibitErrors, nowMs);
                    std::cout << "    note: queued near-threshold unresolved voice grant pending identifier resolution; corrected="
                              << block.correctedDibitErrors
                              << " pending threshold=" << kP25PendingVoiceGrantMaxCorrectedDibits << "\n";
                }
            }
            if (!registryEligible && !acceptedHighCorrectionGrant) {
                std::cout << "    note: CRC valid but corrected dibits exceed registry threshold; only resolved voice grants up to "
                          << kP25VoiceGrantMaxCorrectedDibits
                          << " corrected dibits are saved/followed; unresolved voice grants are only queued up to "
                          << kP25PendingVoiceGrantMaxCorrectedDibits << " corrected dibits\n";
            }
        }
        if (changed) {
            saveP25Talkgroups(talkgroups);
            std::cout << "  Talkgroup registry updated from trusted TSBK.\n";
        }
    }

    if (!result.phase1Pdus.empty()) {
        auto talkgroups = loadP25Talkgroups();
        bool changed = false;
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        for (const auto& pdu : result.phase1Pdus) {
            std::cout << "  P25P1 PDU bit=" << pdu.bitOffset
                      << " format=" << static_cast<int>(pdu.format)
                      << " vendor=0x" << std::hex << static_cast<int>(pdu.vendor)
                      << " op=0x" << static_cast<int>(pdu.opcode) << std::dec
                      << " btf=" << static_cast<int>(pdu.blocksToFollow)
                      << " blocks=" << pdu.dataBlocks.size()
                      << " crc=" << (pdu.headerCrcValid ? "ok" : "fail")
                      << " corr=" << pdu.headerCorrectedDibitErrors
                      << " hdr=" << p25BytesToHex(pdu.headerBytes).toStdString() << "\n";
            if (!pdu.headerFecDecoded || !pdu.headerCrcValid) continue;
            std::vector<std::vector<uint8_t>> dataBlocks;
            dataBlocks.reserve(pdu.dataBlocks.size());
            for (const auto& block : pdu.dataBlocks) dataBlocks.push_back(block.bytes);
            const auto events = analyzer.ingestPhase1Pdu(pdu.format, pdu.vendor, pdu.opcode, pdu.headerBytes, dataBlocks, pdu.headerCrcValid);
            for (const auto& ev : events) {
                std::cout << "    " << p25EventLogText(ev).toStdString() << "\n";
                if (p25ControlEventIsVoiceGrant(ev)) {
                    ++controlVoiceGrantEvents;
                    if (p25ControlEventIsResolvedVoiceGrant(ev)) ++controlResolvedVoiceGrantEvents;
                    else ++controlUnresolvedVoiceGrantEvents;
                    std::cout << "    " << p25GrantDetailLogText(ev).toStdString() << "\n";
                    if (!p25ControlEventIsResolvedVoiceGrant(ev)) {
                        p25RememberPendingVoiceGrant(pendingVoiceGrants, ev, 0, nowMs);
                    }
                }
                changed = mergeP25TalkgroupEvent(talkgroups, targetHz, ev, nowMs) || changed;
            }
        }
        if (changed) {
            saveP25Talkgroups(talkgroups);
            std::cout << "  Talkgroup registry updated from trusted Phase 1 PDU.\n";
        }
    }

    if (!result.imbeFrames.empty()) {
        size_t valid = 0;
        for (const auto& frame : result.imbeFrames) if (frame.valid) ++valid;
        std::cout << "  IMBE voice frames=" << result.imbeFrames.size()
                  << " valid=" << valid
                  << " (mbelib backend=" << (result.stats.voiceBackendAvailable ? "available" : "missing") << ")\n";
    }

    if (!result.phase2Bursts.empty()) {
        for (const auto& burst : result.phase2Bursts) {
            std::ostringstream isch;
            if (!burst.isch.valid) {
                isch << "-";
            } else if (burst.isch.sync) {
                isch << "sync(err=" << burst.isch.errors << ")";
            } else {
                isch << "ch=" << static_cast<int>(burst.isch.channel)
                     << ",loc=" << static_cast<int>(burst.isch.location)
                     << ",fa=" << (burst.isch.freeAccess ? "yes" : "no")
                     << ",cnt=" << static_cast<int>(burst.isch.ultraframeCounter)
                     << ",err=" << burst.isch.errors;
            }
            std::string macEvent = "-";
            if (burst.macPttSeen) macEvent = "ptt";
            else if (burst.macActiveSeen) macEvent = "active";
            else if (burst.macEndPttSeen) macEvent = "end-ptt";
            else if (burst.macIdleSeen) macEvent = "idle";
            else if (burst.macHangtimeSeen) macEvent = "hangtime";
            std::cout << "  P25P2 burst dibit=" << burst.dibitOffset
                      << " kind=" << P25LiveDecoder::phase2BurstKindToString(burst.kind)
                       << " duid=0x" << std::hex << burst.duid << std::dec
                       << " duidErr=" << burst.duidErrors
                       << " syncErr=" << burst.syncErrors
                       << " syncAdj=" << (burst.syncOffsetAdjusted ? std::to_string(burst.syncOffsetDibits) : std::string("0"))
                       << " vcw=" << burst.voiceCodewords.size()
                       << " tdmaSync=" << (burst.tdmaSyncLock ? "yes" : "no")
                      << " sf=" << (burst.superframeLocked ? "locked" : "no")
                      << " sfScore=" << burst.superframeSyncScore
                      << " legacyAudioLock=" << (burst.phase2AudioLock ? "yes" : "no")
                      << " sessionRelease=" << (burst.sessionAudioRelease ? "yes" : "no")
                      << " sfBurst=" << (burst.superframeBurstIndexKnown ? std::to_string(burst.superframeBurstIndex) : std::string("-"))
                      << " grantSlot=" << (burst.grantSlotKnown ? std::to_string(burst.grantSlot) : std::string("-"))
                      << " xorMask=" << (burst.xorMaskApplied ? "yes" : "not-yet")
                      << " maskPhase=" << (burst.xorMaskPhaseKnown ? std::to_string(burst.xorMaskPhase) : std::string("-"))
                      << " phaseScore=" << burst.xorMaskPhaseScore
                      << " mac=" << (burst.macCrcValid ? "crc-ok" : (burst.macFecDecoded ? "fec-only" : "-"))
                      << " macEvent=" << macEvent
                      << " ess=" << (burst.essKnown ? (burst.encrypted ? "encrypted" : "clear") : "unknown")
                      << " isch=" << isch.str() << "\n";
        }
        const bool lateEntry = std::any_of(result.phase2Bursts.begin(), result.phase2Bursts.end(), [](const P25Phase2Burst& burst) {
            return !burst.sessionAudioRelease &&
                   !burst.voiceCodewords.empty() &&
                   burst.xorMaskApplied &&
                   !burst.macCrcValid;
        });
        if (lateEntry) {
            std::cout << "  note: Phase 2 late entry: voice bursts present, mask applied, waiting for MAC CRC/ESS before audio release.\n";
        }
                std::cout << "  note: Phase 2 AMBE audio follows target-slot security; grants queue until target-slot PTT/ESS or MAC/ESS proves clear. Explicit encrypted state stays muted.\n";
    }

    if (!result.phase2MacPdus.empty()) {
        auto talkgroups = loadP25Talkgroups();
        bool changed = false;
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        for (const auto& pdu : result.phase2MacPdus) {
            std::cout << "  P25P2 MAC type=" << p25Phase2MacPduTypeToString(pdu.opcode)
                      << " offset=" << static_cast<int>(pdu.offset)
                      << " source=" << P25LiveDecoder::phase2BurstKindToString(pdu.source)
                      << " crc=" << (pdu.crcValid ? "ok" : "fail")
                      << " corr=" << pdu.correctedSymbols
                      << " " << p25Phase2MacPduHypothesisText(pdu).toStdString()
                      << " raw=" << p25BytesToHex(pdu.bytes).toStdString() << "\n";
            const auto events = analyzer.ingestPhase2MacPdu(
                pdu.opcode, pdu.offset, pdu.bytes, pdu.crcValid, pdu.macStructureMaxBits);
            for (const auto& ev : events) {
                std::cout << "    " << p25EventLogText(ev).toStdString() << "\n";
                if (p25ControlEventIsVoiceGrant(ev)) {
                    ++controlVoiceGrantEvents;
                    if (p25ControlEventIsResolvedVoiceGrant(ev)) ++controlResolvedVoiceGrantEvents;
                    else ++controlUnresolvedVoiceGrantEvents;
                    std::cout << "    " << p25GrantDetailLogText(ev).toStdString() << "\n";
                    if (!p25ControlEventIsResolvedVoiceGrant(ev)) {
                        p25RememberPendingVoiceGrant(pendingVoiceGrants, ev, 0, nowMs);
                    }
                }
                changed = mergeP25TalkgroupEvent(talkgroups, targetHz, ev, nowMs) || changed;
                if (ev.type == P25ControlEventType::IdentifierUpdate &&
                    p25ChannelIdentifierUsable(p25IdentifierFromEvent(ev))) {
                    for (const auto& resolved : p25ResolvePendingVoiceGrants(pendingVoiceGrants, analyzer, nowMs)) {
                        ++controlResolvedVoiceGrantEvents;
                        std::cout << "    pending-resolved after Phase 2 MAC identifier ID "
                                  << static_cast<int>(ev.identifier) << ": "
                                  << p25EventLogText(resolved).toStdString() << "\n";
                        std::cout << "    " << p25GrantDetailLogText(resolved).toStdString() << "\n";
                        changed = mergeP25TalkgroupEvent(talkgroups, targetHz, resolved, nowMs) || changed;
                    }
                }
            }
        }
        if (changed) {
            saveP25Talkgroups(talkgroups);
            std::cout << "  Talkgroup registry updated from trusted Phase 2 MAC.\n";
        }
    }

    if (trustedTsbk > 0 || trustedPhase1Pdu > 0 || trustedPhase2Mac > 0 || result.stats.phase2Bursts > 0) {
        std::cout << "  control audit: stage=" << lockStage.toStdString()
                  << " trustedTsbk=" << trustedTsbk
                  << " trustedP1Pdu=" << trustedPhase1Pdu
                  << " trustedP2Mac=" << trustedPhase2Mac
                  << " voiceGrants=" << controlVoiceGrantEvents
                  << " resolvedGrants=" << controlResolvedVoiceGrantEvents
                  << " unresolvedGrants=" << controlUnresolvedVoiceGrantEvents
                  << " ops=" << p25ControlAuditOpsText(trustedControlOps).toStdString()
                  << "\n";
        if ((trustedTsbk > 0 || trustedPhase2Mac > 0) && controlVoiceGrantEvents == 0) {
            std::cout << "  note: trusted control decode contained no voice-grant opcode in this window; "
                      << "there was nothing eligible for follow to accept.\n";
        }
        if (result.stats.phase2Bursts > 0 && trustedPhase2Mac == 0) {
            std::cout << "  note: Phase 2 burst telemetry is present without CRC-valid MAC; "
                      << "treat this as RF/symbol/framer evidence, not a followable grant.\n";
        }
    }

    for (const auto& warning : result.warnings) {
        std::cout << "  note: " << warning << "\n";
    }
}

// P25 SigMF/WAV + replay voicetest: see P25VoiceTest.h (ISS-0004 Phase 6)


// populateP25Table + MainWindow: see MainWindow.h (ISS-0004 Phase 8)
// AppBootstrap (logging/theme/instance): see AppBootstrap.h (ISS-0004 Phase 8)

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
