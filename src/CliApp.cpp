#include "CliApp.h"
#include "AppBootstrap.h"
#include "SavedFrequencies.h"
#include "P25DecodeConfig.h"
#include "P25VoiceSession.h"
#include "DemodModeUtils.h"

#include "AudioEngine.h"
#include "AudioCapture.h"
#include "DeviceManager.h"
#include "Demod.h"
#include "IP25AmbeEncoder.h"
#include "P25AppGlobals.h"
#include "P25AudioDropClass.h"
#include "P25Control.h"
#include "P25DebugStage.h"
#include "P25FollowStateMachine.h"
#include "P25LiveDecoder.h"
#include "P25Phase2TxFramer.h"
#include "P25RollingIq.h"
#include "P25SdrtrunkTune.h"
#include "P25TalkgroupRegistry.h"
#include "P25TxConfig.h"
#include "P25TxSession.h"
#include "P25VoiceDecode.h"
#include "P25VoiceTest.h"
#include "P25VoiceTiming.h"
#include "Receiver.h"
#include "RemoteDiagnostics.h"
#include "SignalClassifier.h"
#include "ClassifierModelBackend.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QString>
#include <QStringList>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <complex>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <limits>
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

#ifndef SDR_TOWN_VERSION
#define SDR_TOWN_VERSION "0.0.0"
#endif
#ifndef SDR_TOWN_P25_AUDIO_BASELINE
#define SDR_TOWN_P25_AUDIO_BASELINE "p25-clear-continuous-20260810"
#endif

// Symbols now in DemodModeUtils / P25VoiceSession / P25DecodeConfig /
// SavedFrequencies / AppBootstrap (ISS-0004 Phase A).

static std::deque<std::string> parseCliBatchCommandsFromRawArgs(const std::vector<std::string>& args)
{
    std::deque<std::string> commands;
    for (size_t i = 1; i < args.size(); ++i) {
        std::string arg = args[i];
        if (arg.empty()) continue;
        std::string key = arg;
        const size_t eq = key.find('=');
        std::string value;
        if (eq != std::string::npos) {
            value = key.substr(eq + 1);
            key = key.substr(0, eq);
        }
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (key == "--cmd" || key == "--command" || key == "--exec") {
            if (eq == std::string::npos) {
                std::ostringstream joined;
                for (size_t j = i + 1; j < args.size(); ++j) {
                    if (args[j].empty()) continue;
                    if (joined.tellp() > 0) joined << ' ';
                    joined << args[j];
                }
                value = joined.str();
                i = args.size();
            }
            if (!value.empty()) commands.push_back(value);
        }
    }
    return commands;
}

static std::string cliTrimLowerCommand(std::string command)
{
    if (command.size() >= 3 &&
        static_cast<unsigned char>(command[0]) == 0xef &&
        static_cast<unsigned char>(command[1]) == 0xbb &&
        static_cast<unsigned char>(command[2]) == 0xbf) {
        command.erase(0, 3);
    }
    const auto first = std::find_if_not(command.begin(), command.end(), [](unsigned char c) {
        return std::isspace(c) != 0;
    });
    command.erase(command.begin(), first);
    const auto last = std::find_if_not(command.rbegin(), command.rend(), [](unsigned char c) {
        return std::isspace(c) != 0;
    }).base();
    command.erase(last, command.end());
    std::transform(command.begin(), command.end(), command.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return command;
}

static bool cliCommandNeedsStartupDeviceEnumeration(const std::string& command)
{
    const std::string lower = cliTrimLowerCommand(command);
    if (lower.empty()) return false;
    if (lower == "quit" || lower == "exit" || lower == "q" ||
        lower == "help" || lower == "h" || lower == "?" ||
        lower == "test") {
        return false;
    }
    if (lower.rfind("p25 audit", 0) == 0 ||
        lower.rfind("p25 test", 0) == 0 ||
        lower.rfind("p25 voice", 0) == 0 ||
        lower.rfind("p25 replay", 0) == 0 ||
        lower.rfind("p25 followtest", 0) == 0 ||
        lower.rfind("p25 voicetest", 0) == 0) {
        return false;
    }
    return true;
}

static bool cliBatchCanSkipStartupDeviceEnumeration(const std::deque<std::string>& commands)
{
    if (commands.empty()) return false;
    for (const auto& command : commands) {
        if (cliCommandNeedsStartupDeviceEnumeration(command)) return false;
    }
    return true;
}


// parseGuiRuntimeConfig + helpers (ISS-0004 Phase A; moved from main.cpp)
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

int runCLI(int argc, char* argv[]) {
    std::cout << "SDR Town CLI (Phase 0 complete - per-receiver foundation + full monitor thread)\n";
    std::cout << "Type 'help' for commands. 'quit' to exit.\n";
    std::cout.flush();

    std::vector<std::string> rawCliArgs;
    rawCliArgs.reserve(static_cast<size_t>(std::max(argc, 0)));
    for (int i = 0; i < argc; ++i) {
        rawCliArgs.emplace_back(argv[i] ? argv[i] : "");
    }
    std::deque<std::string> cliBatchCommands = parseCliBatchCommandsFromRawArgs(rawCliArgs);

    const GuiRuntimeConfig cliStartupCfg = parseGuiRuntimeConfig(argc, argv);
    if (!cliStartupCfg.debugStage.empty()) {
        gP25DebugStageFilter = p25ParseDebugStage(cliStartupCfg.debugStage);
        std::cout << "P25 debug stage: " << p25DebugStageLabel(gP25DebugStageFilter) << "\n";
    }

    // S0-1: App identity for CLI
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
    remoteDiagnosticsConfigureFromProcess(argc, argv, &cliApp, "cli");

    setupLogging();
    spdlog::info("CLI mode started");
    spdlog::info("CLI batch commands queued: {}", cliBatchCommands.size());

    auto& mgr = DeviceManager::instance();
    std::vector<DeviceInfo> devs;
    const bool skipStartupDeviceEnumeration =
        cliBatchCanSkipStartupDeviceEnumeration(cliBatchCommands);
    if (skipStartupDeviceEnumeration) {
        spdlog::info("CLI skipping SDR device enumeration for offline batch command(s)");
        std::cout << "Devices enumerated: skipped (offline CLI batch)\n";
    } else {
        mgr.setupSoapyForRTLSDR();
        devs = mgr.enumerateDevices(false);
        std::cout << "Devices enumerated: " << devs.size() << "\n";
        for (size_t i=0; i<devs.size(); ++i) {
            const auto& d = devs[i];
            std::cout << "  [" << i << "] " << d.driver << " " << d.label
                      << " enabled=" << d.enabled
                      << " state=\"" << mgr.getRuntimeStateLabel(i) << "\""
                      << " gain=" << d.gain
                      << " ppm=" << d.frequencyCorrectionPpm << "\n";
        }
    }
    std::cout.flush();

    if (remoteDiagnosticsEnabled()) {
        QJsonObject payload;
        payload["mode"] = "cli";
        payload["version"] = SDR_TOWN_VERSION;
        payload["deviceCount"] = boundedJsonInt(devs.size());
        payload["hasCommand"] = startupHasArg(argc, argv, {"--cmd", "--command", "--exec"});
        remoteDiagnosticsSubmit("app.start", "info", payload);
    }

    // Use shared_ptr<Receiver> for CLI too (consistent with GUI, enables stable cursor updates across snapshots, cheap to snapshot pointers).
    std::vector<std::shared_ptr<Receiver>> cliReceivers;
    std::mutex cliRxMutex;  // S0-2 (P0): protect CLI receiver vector mutations (command thread) vs mon thread iteration
    std::unique_ptr<AudioEngine> cliAudio;
    std::unique_ptr<AudioCapture> cliMic;
    std::atomic<bool> cliStop{false};
    std::atomic<bool> cliAudioEnabled{false};
    std::map<long long, P25ControlChannelAnalyzer> cliP25Analyzers;
    std::map<size_t, P25LiveDecoder> cliP25LiveDecoders;
    P25TxSnapshot cliP25TxSnapshot{};

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
            rptr->channelBwHz = defaultBandwidthForMode(DemodMode::NFM);
            rptr->lpfHz = defaultLpfForMode(DemodMode::NFM);
            rptr->audioLpfEnabled = true;
            rptr->squelchDb = -105.0;
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

    auto clearCliP25VoiceFollow = [](Receiver& rx) {
        clearP25VoiceFollowFieldsLocked(rx, false);
        tryApplyP25VoiceResetLocked(rx);
    };

    auto rearmCliP25Phase2Slot = [](Receiver& rx, uint8_t newSlot) {
        std::lock_guard<std::recursive_mutex> dspLock(rx.dspMutex);
        applyP25Phase2SlotProbeLocked(rx, newSlot, QDateTime::currentMSecsSinceEpoch());
    };

    // CLI has no GUI traffic-source manager/member generation counter.  Keep a
    // local generation value so shared DSP helper code can compile and stale
    // independent traffic-source receivers (not normally created in CLI mode)
    // fail closed if they ever appear.
    std::atomic<uint64_t> cliP25TrafficSourceGeneration{0};

    // CLI DSP monitor thread (mirrors guiDspWorker but standalone, uses Receiver vector + own demod instances)
    std::thread cliMonThread([&]() {
        std::map<ReceiverSessionKey, std::chrono::steady_clock::time_point> lastPhase2DecodeByRx;
        P25SpeakerPendingMap pendingAudioByRx;
        std::map<ReceiverSessionKey, RollingIqWindow> phase2IqByRx;
        while (!cliStop) {
            bool did = false;
            // S0-2 (P0): snapshot under lock (shared_ptrs — cheap, stable objects for cursor + demod state).
            std::vector<std::shared_ptr<Receiver>> rxSnap;
            {
                std::lock_guard<std::mutex> lk(cliRxMutex);
                rxSnap.reserve(cliReceivers.size());
                for (auto& r : cliReceivers) if (r && r->active) rxSnap.push_back(r);
            }
            auto receiverSessionStillActive = [&rxSnap](const ReceiverSessionKey& key) {
                return std::any_of(rxSnap.begin(), rxSnap.end(),
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
                if (!mgr.getLatestSpectrum(di, pwr, cf, sr) || sr <= 0.0) {
                    bool canUseVoiceFallback = false;
                    double fallbackCenterHz = 0.0;
                    {
                        std::lock_guard<std::mutex> rxLock(rx.stateMutex);
                        canUseVoiceFallback = rx.active && rx.deviceIndex == di && rx.p25VoiceDecodeEnabled;
                        fallbackCenterHz = p25Phase2TrafficSourceCenterHz(rx);
                    }
                    if (!canUseVoiceFallback) continue;
                    const auto devices = mgr.getDevices();
                    if (di < devices.size()) sr = devices[di].sampleRate;
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
                double rxFreq = 100e6, rxLpf = 3500.0, rxSquelch = -90.0;
                double rxAudioGain = 1.0, rxWfmDe = 75.0, rxWfmNotch = 0.96, rxBw = 25000.0;
                double demodFreq = 100e6;
                double rfSquelchLevel = std::numeric_limits<double>::quiet_NaN();
                DemodMode rxMode = DemodMode::NFM;
                bool rxAudioLpfEnabled = true;
                bool rxP25ControlMute = false;
                bool rxP25VoiceDecode = false;
                bool rxP25VoicePhase2 = false;
                bool rxP25IndependentTrafficSource = false;
                bool phase2SustainDecodeWindow = false;
                bool phase2SessionHadVoiceLock = false;
                bool phase2SessionHadBurstEye = false;
                bool phase2SessionSpeakerSustain = false;
                bool phase2StableSuperframeLock = false;
                bool phase2WideReacquireWindow = false;
                bool phase2MaskEpochRepairWindow = false;
                bool phase2EstablishedClearStreaming = false;
                bool phase2HardTargetAcquire = false;
                bool phase2StreamingDdc = false;
                bool skipP25VoiceWindow = false;
                bool p25VoiceOutputMutedForSettle = false;
                bool appliedQueuedVoiceReset = false;
                bool phase2UseRecentTrafficWindow = false;
                size_t phase2FreshIqSamples = 0;
                size_t phase2ContextIqSamples = 0;
                uint64_t iqStartAbsolute = 0;
                bool iqStartAbsoluteKnown = false;
                uint64_t iqDecodeEndAbsolute = 0;
                bool iqDecodeEndAbsoluteKnown = false;
                std::vector<std::complex<float>> iq;

                {
                    std::lock_guard<std::mutex> rxLock(rx.stateMutex);
                    if (!rx.active || rx.deviceIndex != di) continue;
                    if (rx.p25IndependentTrafficSource) {
                        const uint64_t liveGen = cliP25TrafficSourceGeneration.load(std::memory_order_acquire);
                        if (rx.p25TrafficGeneration == 0 || rx.p25TrafficGeneration != liveGen) {
                            rx.active = false;
                            rx.p25VoiceDecodeEnabled = false;
                            continue;
                        }
                    }
                    rxFreq = rx.freqHz;
                    rxMode = rx.mode;
                    rxBw = rx.channelBwHz;
                    rxLpf = rx.lpfHz;
                    rxAudioLpfEnabled = rx.audioLpfEnabled;
                    rxSquelch = rx.squelchDb;
                    rxAudioGain = rx.audioGain;
                    rxWfmDe = rx.wfmDeTauUs;
                    rxWfmNotch = rx.wfmPilotNotchR;
                    rxP25ControlMute = rx.p25ControlChannelMute;
                    rxP25VoiceDecode = rx.p25VoiceDecodeEnabled;
                    rxP25VoicePhase2 = rx.p25VoicePhase2;
                    rxP25IndependentTrafficSource = rx.p25IndependentTrafficSource;
                    phase2SustainDecodeWindow = p25Phase2UseSustainDecodeWindowLocked(rx);
                    phase2SessionHadVoiceLock = p25Phase2SessionHadVoiceLock(rx);
                    phase2SessionHadBurstEye = p25Phase2SessionHadBurstEye(rx);
                    phase2SessionSpeakerSustain = p25Phase2SessionSpeakerSustainActive(rx);
                    phase2StableSuperframeLock = p25Phase2HasStableSuperframeLockLocked(rx);
                    phase2WideReacquireWindow = p25Phase2NeedsWideReacquireWindowLocked(rx);
                    phase2MaskEpochRepairWindow = rx.p25Phase2MaskEpochRepairHoldWindows > 0;
                    phase2EstablishedClearStreaming = p25Phase2EstablishedClearVoiceStreamingLocked(rx);
                    phase2HardTargetAcquire = p25Phase2SessionHasHardTargetAcquire(rx);
                    if (rxP25VoiceDecode && rx.p25IndependentTrafficSource && rx.p25TrafficRetunesPrimary) {
                        const double trafficCenterHz = p25Phase2TrafficSourceCenterHz(rx);
                        if (std::isfinite(trafficCenterHz) && trafficCenterHz > 0.0) {
                            cf = trafficCenterHz;
                        }
                    }
                    if (rx.p25VoiceResetPending) {
                        if (tryApplyP25VoiceResetLocked(rx)) {
                            rxP25VoiceDecode = rx.p25VoiceDecodeEnabled;
                            rxP25VoicePhase2 = rx.p25VoicePhase2;
                            phase2IqByRx.erase(p25ReceiverSessionKey(rx));
                            p25Phase2ClearSpeakerPendingQueue(rx,
                                                          p25SpeakerPendingFor(pendingAudioByRx, rx),
                                                          P25PendingClearReason::RetuneOrGeneration);
                            appliedQueuedVoiceReset = true;
                        }
                    }
                    phase2StreamingDdc = rxP25VoiceDecode &&
                        rxP25VoicePhase2 &&
                        rx.p25VoiceLiveDecoder.config().enableStreamingChannelDdc;
                    rxAudioOutputs = rx.audioOutputIndices;
                    if (rxP25VoiceDecode) {
                        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
                        p25VoiceOutputMutedForSettle = rx.p25VoiceSettleUntilMs > nowMs;
                        // Decode through settle; mute/output gating happens later.
                        // Skipping here discards the beginning of the traffic channel
                        // and forces fragile late-entry recovery.
                        if (!p25VoiceOutputMutedForSettle && rx.p25VoiceDiscardWindows > 0) {
                            rx.p25VoiceDiscardWindows = 0;
                        }
                    }

                    // audit-followup-2: cursor-based new samples only for this rx (chronological, no overlap with other rxs on same dev).
                    phase2FreshIqSamples = 0;
                    phase2ContextIqSamples = 0;
                    const bool phase2BufferedDecode = rxP25VoiceDecode && rxP25VoicePhase2;
                    // Match the GUI traffic path: Phase 2 follow uses the receiver's
                    // private cursor even when sourced from the same device ring.
                    phase2UseRecentTrafficWindow = false;
                    if (appliedQueuedVoiceReset) {
                        did = true;
                        continue;
                    }
                    if (skipP25VoiceWindow) {
                        phase2IqByRx.erase(p25ReceiverSessionKey(rx));
                        did = true;
                        continue;
                    }
                    if (!phase2BufferedDecode) {
                        phase2IqByRx.erase(p25ReceiverSessionKey(rx));
                    }
                    size_t tgt = (sr > 0)
                        ? static_cast<size_t>(sr * (phase2BufferedDecode ? kP25Phase2VoiceDecodeWindowSeconds : 0.025))
                        : 8192;
                    if (phase2BufferedDecode) {
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
                        p25Phase2PrepareRollingIqPull(mgr, di, rx, rolling, rollingWindow, pullWindow, sr);
                        auto newWin = phase2UseRecentTrafficWindow
                            ? mgr.getRecentIQWindowWithCursor(di, rollingWindow)
                            : mgr.getNewIQWindowForReceiver(di, rx, pullWindow);
                        if (newWin.streamEpoch != 0 && rolling.streamEpochKnown &&
                            rolling.streamEpoch != newWin.streamEpoch) {
                            if (newWin.samples.empty()) {
                                newWin.cursorDiscontinuity = true;
                            } else {
                                rolling.clear();
                                p25Phase2ClearSpeakerPendingQueue(rx,
                                    pendingAudioByRx[sessionKey],
                                    P25PendingClearReason::RetuneOrGeneration);
                                lastPhase2DecodeByRx.erase(sessionKey);
                                (void)tryResetP25TrafficSessionNonBlocking(rx, "iq-stream-retune-handoff", true);
                                did = true;
                                continue;
                            }
                        }
                        if (newWin.cursorDiscontinuity) {
                            rolling.clear();
                            p25Phase2ClearSpeakerPendingQueue(rx,
                                pendingAudioByRx[sessionKey],
                                P25PendingClearReason::RetuneOrGeneration);
                            lastPhase2DecodeByRx.erase(sessionKey);
                            (void)tryResetP25TrafficSessionNonBlocking(rx, "iq-cursor-discontinuity", true);
                            did = true;
                            continue;
                        }
                        const bool appended = rolling.append(newWin, rollingWindow);
                        const auto now = std::chrono::steady_clock::now();
                        auto& last = lastPhase2DecodeByRx[p25ReceiverSessionKey(rx)];
                        if (rolling.samples.empty()) {
                            did = true;
                            continue;
                        }
                        // Mirror the GUI worker full-eye cold-acquire hold. append() marks
                        // decodeAbsoluteKnown on first fill, so gate on "never decoded past
                        // start" rather than !decodeAbsoluteKnown.
                        {
                            const bool coldAcquireNeverDecoded =
                                !rolling.decodeAbsoluteKnown ||
                                rolling.lastDecodeAbsolute <= rolling.startAbsolute;
                            const bool coldAcquireEye =
                                rx.p25VoicePhase2 &&
                                rx.p25IndependentTrafficSource &&
                                coldAcquireNeverDecoded &&
                                !phase2SessionHadBurstEye;
                            const size_t minColdAcquireIq = (sr > 0.0)
                                ? std::min(rollingWindow, static_cast<size_t>(
                                      std::max(32768.0, sr * kP25Phase2VoiceDecodeFirstColdEyeSeconds)))
                                : std::min<size_t>(rollingWindow, 1474560u);
                            if (coldAcquireEye && rolling.samples.size() < minColdAcquireIq) {
                                did = true;
                                continue;
                            }
                        }
                        if (!appended) {
                            // No new IQ this tick, but keep decoding from the rolling buffer.
                        }
                        if (last.time_since_epoch().count() != 0 &&
                            now - last < std::chrono::milliseconds(p25Phase2AdaptiveVoiceDecodeCadenceMs(rx))) {
                            did = true;
                            continue;
                        }
                        const bool wideReacquireWindow = phase2WideReacquireWindow;
                        const bool maskEpochRepairWindow = phase2MaskEpochRepairWindow;
                        const size_t undecodedBacklog = p25Phase2UndecodedBacklogSamples(rolling);
                        const bool activeSpeakerClearPath =
                            phase2SessionSpeakerSustain ||
                            phase2EstablishedClearStreaming ||
                            p25Phase2SpeakerSustainDecodeActive() ||
                            phase2SessionHadBurstEye;
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
                            rxP25VoiceDecode &&
                            rxP25VoicePhase2 &&
                            rxP25IndependentTrafficSource &&
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
                        // Keep CLI live capture on the same complete-chunk
                        // contract as the GUI path; replay can still consume
                        // bounded file windows independently.
                        iq = rolling.takeUndecoded(maxDecodeChunk, decodeOverlap, iqStartAbsolute, iqStartAbsoluteKnown,
                            &phase2FreshIqSamples, &phase2ContextIqSamples, minDecodeFresh,
                            &iqDecodeEndAbsolute, &iqDecodeEndAbsoluteKnown, false);
                        if (iq.empty()) {
                            did = true;
                            continue;
                        }
                        if (chunkPlan.treatAsContextFreeFresh) {
                            phase2FreshIqSamples = iq.size();
                            phase2ContextIqSamples = 0;
                        }
                        last = now;
                    } else {
                        iq = mgr.getNewSamplesForReceiver(di, rx, tgt);  // updates live rx cursor
                    }
                    if (iq.empty()) continue;

                    // audit-followup-7 (P2): if the rx is in AUTO, let the CLI monitor classify using latest spectrum
                    // and pick a concrete mode (NFM/WFM/AM etc) before demod. GUI timer already does this.
                    if (rxMode == DemodMode::AUTO && !pwr.empty()) {
                        auto smart = chooseSmartModeAndBandwidth(pwr, sr, cf, rxFreq, DemodMode::AUTO);
                        rxMode = smart.mode;
                        rx.mode = smart.mode;
                        rx.channelBwHz = smart.bandwidthHz;
                        rx.lpfHz = smart.lpfHz;
                        rxBw = rx.channelBwHz;
                        rxLpf = rx.lpfHz;
                    }
                    demodFreq = rxP25VoiceDecode
                        ? p25VoiceAfcTargetHz(rx, rxFreq, rxBw)
                        : applyNfmAfcFromSpectrum(rx, pwr, sr, cf, rxFreq, rxBw, rxMode);
                    afcOffsetHz = demodFreq - rxFreq;
                    rfMetrics = computeRfSquelchMetrics(pwr, sr, cf, demodFreq, rxBw, rxMode);
                    rfSquelchLevel = rfMetrics.valid
                        ? rfMetrics.signalLevelDb
                        : std::numeric_limits<double>::quiet_NaN();
                }

                const size_t got = iq.size();
                if (got == 0) continue;
                const double t = got / sr;
                const double orate = (cliAudio ? cliAudio->getSampleRate() : 48000.0);
                const size_t need = (size_t)std::round(t * orate);
                auto t0 = std::chrono::steady_clock::now();
                bool haveP25Audio = false;
                P25VoiceAudioBlock p25Audio;
                {
                    std::unique_lock<std::recursive_mutex> dspLock(rx.dspMutex, std::try_to_lock);
                    if (!dspLock.owns_lock()) {
                        did = true;
                        continue;
                    }
                    if (p25ShouldSuppressAnalogDemod(rxP25VoiceDecode,
                                                     rxP25ControlMute,
                                                     rxP25IndependentTrafficSource,
                                                     rxP25VoicePhase2) &&
                        !rxP25VoiceDecode) {
                        rms = -120.0;
                        (void)need;
                    } else if (rxP25VoiceDecode) {
                        p25Audio = decodeP25VoiceAudioBlock(rx, iq, sr, cf, demodFreq, orate,
                            iqStartAbsolute, iqStartAbsoluteKnown, phase2ContextIqSamples);
                        (void)phase2FreshIqSamples;
                        bool dropStaleTrafficAudio = false;
                        if (rx.p25IndependentTrafficSource) {
                            const uint64_t liveGen = cliP25TrafficSourceGeneration.load(std::memory_order_acquire);
                            std::unique_lock<std::mutex> genLock(rx.stateMutex, std::try_to_lock);
                            if (!genLock.owns_lock() || !rx.active ||
                                rx.p25TrafficGeneration == 0 || rx.p25TrafficGeneration != liveGen) {
                                dropStaleTrafficAudio = true;
                            }
                        }
                        if (dropStaleTrafficAudio) {
                            did = true;
                            continue;
                        }
                        // Phase-2 overlap pre-roll is de-duped by absolute AMBE codeword dibit
                        // positions.  Avoid sample-count trimming here so valid multi-frame
                        // superframe audio is not shortened or made jittery.
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
                        ch = speakerMayEmit ? p25Audio.audio : std::vector<float>{};
                        const bool cliEngineAvailable = cliAudio != nullptr && cliAudioEnabled.load();
                        const size_t cliActiveOutputs = cliEngineAvailable ? cliAudio->activeOutputCount() : 0u;
                        const size_t cliQueuedSamples = cliEngineAvailable ? cliAudio->getRingQueuedSamples() : 0u;
                        const double cliRingFill = cliEngineAvailable ? cliAudio->getRingFillPercent() : 0.0;
                        const int cliUnderruns = cliEngineAvailable ? cliAudio->getUnderrunCount() : 0;
                        writeP25Phase2AudioOutputTrace(rx, p25Audio, "cli-dsp-worker",
                            p25VoiceOutputMutedForSettle, speakerMayEmit, cliEngineAvailable,
                            cliActiveOutputs, cliQueuedSamples, cliRingFill, cliUnderruns, ch.size(), orate);
                        if (!ch.empty()) {
                            double sum = 0.0;
                            for (float sample : ch) sum += static_cast<double>(sample) * sample;
                            rms = 20.0 * std::log10(std::sqrt(sum / static_cast<double>(ch.size())) + 1e-12);
                        }
                        (void)need;
                    } else {
                        ch = rx.demod.demodulateToAudio(iq, sr, cf, demodFreq, rxMode,
                            rms, rxLpf, rxSquelch, rxAudioGain, rxWfmDe,
                            rxWfmNotch, rxBw, need, orate, rfSquelchLevel, rxAudioLpfEnabled);
                    }
                }
                if (haveP25Audio) {
                    if (rxP25VoiceDecode && rxP25VoicePhase2 && iqDecodeEndAbsoluteKnown) {
                        auto& rolling = phase2IqByRx[p25ReceiverSessionKey(rx)];
                        if (p25Phase2RollingDecodeWindowConsumed(p25Audio)) {
                            rolling.commitDecodeAbsolute(iqDecodeEndAbsolute);
                        } else {
                            rolling.rollbackSubmittedDecode();
                        }
                    }
                    publishP25VoiceDiagnostics(rx, p25Audio);
                    if (!ch.empty()) {
                        appendCliP25WavCapture(ch);
                    }
                    if (p25Phase2ShouldFlushStaleVoicePipeline(p25Audio) ||
                        p25Phase2ShouldFlushAudioTail(p25Audio)) {
                        phase2IqByRx.erase(p25ReceiverSessionKey(rx));
                        p25Phase2ClearSpeakerPendingQueue(rx,
                                                          p25SpeakerPendingFor(pendingAudioByRx, rx),
                                                          P25PendingClearReason::RetuneOrGeneration);
                        lastPhase2DecodeByRx.erase(p25ReceiverSessionKey(rx));
                        mgr.setReceiverCursorToLiveEdge(di, rx);
                    }
                }
                auto t1 = std::chrono::steady_clock::now();
                dspMicros = std::chrono::duration_cast<std::chrono::microseconds>(t1-t0).count();

                gLastDspMicros.store(dspMicros);
                gLastAfcOffsetHz.store(afcOffsetHz);
                if (rfMetrics.valid) {
                    gLastRmsDb.store(rfMetrics.signalLevelDb);
                    gLastNoiseFloorDb.store(rfMetrics.noiseFloorDb);
                    gLastSnrDb.store(rfMetrics.snrDb);
                } else {
                    gLastRmsDb.store(rms);
                }
                if (!ch.empty() && cliAudio && cliAudioEnabled) {
                    if (haveP25Audio && rxP25VoiceDecode) {
                        auto& pendingSpeaker = p25SpeakerPendingFor(pendingAudioByRx, rx);
                        p25Phase2BindSpeakerPendingToCall(pendingSpeaker, rx);
                        std::vector<float> pushedRealAudio;
                        std::vector<float> speakerAudioForQueue;
                        const std::vector<float>* speakerAudioToQueue = &ch;
                        const bool hasNewPcm =
                            rxP25VoicePhase2 &&
                            p25VoiceBlockHasSpeakerTimelineAudio(p25Audio) &&
                            p25Audio.phase2EmittedPcmFrames > 0;
                        if (hasNewPcm) {
                            const double outRate = std::max(8000.0,
                                static_cast<double>(cliAudio->getSampleRate()));
                            const size_t phase2FrameSamples = std::max<size_t>(160,
                                static_cast<size_t>(outRate * 0.020 + 0.5));
                            speakerAudioForQueue = p25Phase2SpeakerAudioForQueue(
                                pendingSpeaker, p25Audio, ch, phase2FrameSamples);
                            speakerAudioToQueue = &speakerAudioForQueue;
                        }
                        const bool hasPlayableNewPcm = !speakerAudioToQueue->empty();
                        const size_t pushedSamples = pushP25SpeakerAudio(cliAudio.get(),
                            pendingSpeaker.samples,
                            *speakerAudioToQueue,
                            rxAudioOutputs,
                            cliAudio->getRingFillPercent(),
                            !hasPlayableNewPcm,
                            &pushedRealAudio);
                        if (pushedSamples > 0) {
                            const qint64 speakerNowMs = QDateTime::currentMSecsSinceEpoch();
                            p25Phase2ResetPlayoutBridge(rx);
                            const P25P2CallAudioKey speakerKey =
                                p25CurrentPhase2AudioKey(rx, p25Audio.effectiveTargetFreqHz);
                            const bool bridgeAnchor =
                                p25Phase2CleanPlayoutBridgeAnchorWindow(p25Audio);
                            p25Phase2RememberLastEmittedSample(
                                rx, speakerKey, pushedRealAudio.empty() ? ch : pushedRealAudio,
                                bridgeAnchor);
                            gP25AudioLastSpeakerOutputMs.store(speakerNowMs, std::memory_order_relaxed);
                            p25Phase2UpdateSessionSustainState(rx, p25Audio, speakerNowMs, true);
                        }
                    } else {
                        pushAudioFrames(cliAudio.get(),
                            p25SpeakerPendingFor(pendingAudioByRx, rx).samples,
                            ch, rxAudioOutputs);
                    }
                }
                did = true;
            }
            if (cliAudio && cliAudioEnabled.load(std::memory_order_relaxed)) {
                p25TopUpSpeakerPlaybackRing(cliAudio.get(), pendingAudioByRx, receiverSessionStillActive);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(did ? 4 : 15));
        }
    });

    auto printStats = [&](size_t r = 0) {
        size_t di = 0;
        double freqHz = 100e6;
        double bwHz = 12500.0;
        double lpfHz = 3000.0;
        bool lpfOn = true;
        bool ccMuted = false;
        DemodMode mode = DemodMode::NFM;
        P25VoiceDiagSnapshot voiceDiag;
        {
            std::lock_guard<std::mutex> lk(cliRxMutex);
            ensureCliRxLocked(r);
            Receiver& rx = *cliReceivers[r];
            std::lock_guard<std::mutex> rxLock(rx.stateMutex);
            di = rx.deviceIndex;
            freqHz = rx.freqHz;
            bwHz = rx.channelBwHz;
            lpfHz = rx.lpfHz;
            lpfOn = rx.audioLpfEnabled;
            ccMuted = rx.p25ControlChannelMute;
            mode = rx.mode;
            voiceDiag = rx.p25VoiceDiagnostics;
        }
        double g = (di < mgr.getDevices().size() ? mgr.getCurrentGain(di) : 0.0);
        double ppm = 0.0;
        {
            auto dlist = mgr.getDevices();
            if (di < dlist.size()) ppm = dlist[di].frequencyCorrectionPpm;
        }
        size_t qd = (di < mgr.getDevices().size() ? mgr.getIQQueueDepth(di) : 0);
        const std::string runtimeState = (di < mgr.getDevices().size()) ? mgr.getRuntimeStateLabel(di) : std::string("stopped");
        const size_t fftBins = (di < mgr.getDevices().size()) ? mgr.getSpectrumFftBins(di) : 8192;
        double dsp = gLastDspMicros.load() / 1000.0;
        double level = gLastRmsDb.load();
        double afcHz = gLastAfcOffsetHz.load();
        double afcConf = gLastAfcConfidence.load();
        double afcBinHz = gLastAfcBinHz.load();
        double ppmDelta = estimatePpmCorrectionDelta(afcHz, freqHz);
        double ring = 0, underr = 0;
        if (cliAudio) {
            ring = cliAudio->getRingFillPercent();
            underr = (double)cliAudio->getUnderrunCount();
        }
        std::cout << "RX" << r << " dev=" << di << " f=" << (freqHz/1e6) << "MHz mode=" << (int)mode
                  << " bw=" << (bwHz/1000) << "kHz gain=" << g << "dB IQdepth=" << qd
                  << " ppm=" << ppm
                  << " state=\"" << runtimeState << "\""
                  << " fft=" << fftBins
                  << " lpf=" << (lpfOn ? std::to_string(lpfHz/1000.0) + "kHz" : std::string("off"))
                  << " ccMute=" << (ccMuted ? "yes" : "no")
                  << " level=" << level << "dB AFC=" << (afcHz / 1000.0) << "kHz";
        if (std::isfinite(ppmDelta) && std::abs(afcHz) >= 25.0) {
            std::cout << " ppmDelta=" << ppmDelta << " ppmSuggest=" << (ppm + ppmDelta);
            if (afcConf > 0.0) std::cout << " afcConf=" << afcConf;
            if (afcBinHz > 0.0) std::cout << " afcBin=" << afcBinHz << "Hz";
        }
        std::cout << " DSP=" << dsp << "ms ring=" << ring << "% underruns=" << underr << "\n";
        const auto p25Code = static_cast<P25VoiceDiagCode>(voiceDiag.diag);
        if (p25Code != P25VoiceDiagCode::Idle || voiceDiag.talkgroupId > 0) {
            std::cout << "  P25 voice tg=" << voiceDiag.talkgroupId
                      << " stage=" << p25VoiceDiagLabel(p25Code)
                      << " sync=" << voiceDiag.syncs
                      << " nid=" << voiceDiag.nids
                      << " nidLock=" << (voiceDiag.nidLock ? "yes" : "no")
                      << " imbe=" << voiceDiag.imbeFrames
                      << " decoded=" << voiceDiag.decodedFrames
                      << " audio=" << voiceDiag.audioSamples
                      << " p2bursts=" << voiceDiag.phase2Bursts
                      << " p2vcw=" << voiceDiag.phase2VoiceCodewords
                      << " p2sf=" << voiceDiag.phase2SuperframeBursts
                      << " p2mask=" << voiceDiag.phase2MaskedBursts
                      << " backend=" << (voiceDiag.backendAvailable ? "yes" : "no")
                      << "\n";
        }
    };

    auto printPpmUsage = []() {
        std::cout << "usage:\n"
                  << "  ppm <device> <ppm>\n"
                  << "  ppm cal <device> <known_mhz> [search_khz]\n"
                  << "  ppm apply <device> <known_mhz> [search_khz]\n";
    };

    auto runPpmCalibration = [&](bool applyCorrection, int deviceIndex, double knownFreqInput, double searchKhz) {
        if (deviceIndex < 0 || static_cast<size_t>(deviceIndex) >= mgr.getDevices().size()) {
            std::cout << "bad device index\n";
            return;
        }
        const double knownHz = (knownFreqInput > 100000.0) ? knownFreqInput : (knownFreqInput * 1.0e6);
        if (!std::isfinite(knownHz) || knownHz <= 0.0) {
            std::cout << "bad known frequency\n";
            return;
        }

        std::vector<float> pwr;
        double cf = 0.0;
        double sr = 0.0;
        if (!mgr.getLatestSpectrum(static_cast<size_t>(deviceIndex), pwr, cf, sr) || pwr.empty() || sr <= 0.0) {
            std::cout << "no live spectrum yet for PPM calibration (enable/tune the device and wait for waterfall data)\n";
            return;
        }

        if (!std::isfinite(searchKhz) || searchKhz <= 0.0) searchKhz = 45.0;
        const double searchHz = std::clamp(searchKhz * 1000.0, std::max(1000.0, sr / std::max<size_t>(1, pwr.size()) * 4.0), sr * 0.5);
        const double maxSignalBwHz = std::clamp(searchHz * 0.70, std::max(1000.0, sr / std::max<size_t>(1, pwr.size()) * 4.0), 60000.0);
        const auto estimate = estimateSignalOffsetFromSpectrum(pwr, sr, cf, knownHz, searchHz, maxSignalBwHz);
        if (!estimate.valid) {
            std::cout << "no reliable carrier estimate near " << (knownHz / 1.0e6) << " MHz"
                      << " (center=" << (cf / 1.0e6) << " MHz sr=" << (sr / 1.0e6)
                      << " MHz search=+/-" << (searchHz / 1000.0) << " kHz"
                      << " snr=" << estimate.snrDb << "dB conf=" << estimate.confidence << ")\n";
            return;
        }

        double currentPpm = 0.0;
        {
            auto dlist = mgr.getDevices();
            if (static_cast<size_t>(deviceIndex) < dlist.size()) {
                currentPpm = dlist[static_cast<size_t>(deviceIndex)].frequencyCorrectionPpm;
            }
        }
        const double deltaPpm = estimatePpmCorrectionDelta(estimate.offsetHz, knownHz);
        if (!std::isfinite(deltaPpm)) {
            std::cout << "could not calculate PPM delta from carrier estimate\n";
            return;
        }
        const double suggestedPpm = std::clamp(currentPpm + deltaPpm, -200.0, 200.0);

        std::cout << "PPM calibration dev " << deviceIndex
                  << " known=" << (knownHz / 1.0e6) << "MHz"
                  << " center=" << (cf / 1.0e6) << "MHz"
                  << " sr=" << (sr / 1.0e6) << "MHz\n"
                  << "  offset=" << estimate.offsetHz << "Hz"
                  << " bin=" << estimate.binHz << "Hz"
                  << " measuredBw=" << (estimate.bandwidthHz / 1000.0) << "kHz"
                  << " peak=" << estimate.peakDb << "dB"
                  << " floor=" << estimate.noiseFloorDb << "dB"
                  << " snr=" << estimate.snrDb << "dB"
                  << " confidence=" << estimate.confidence << "\n"
                  << "  current=" << currentPpm << "ppm"
                  << " delta=" << deltaPpm << "ppm"
                  << " suggested=" << suggestedPpm << "ppm\n";

        if (applyCorrection) {
            if (estimate.confidence < 0.55 || estimate.snrDb < 10.0) {
                std::cout << "not applying: measurement confidence/SNR is too low; use ppm <device> <ppm> manually if this carrier is known clean\n";
                return;
            }
            mgr.setFrequencyCorrection(static_cast<size_t>(deviceIndex), suggestedPpm);
            std::cout << "Frequency correction device " << deviceIndex << " -> " << suggestedPpm << " ppm\n";
        } else {
            std::cout << "Run 'ppm apply " << deviceIndex << " " << (knownHz / 1.0e6)
                      << "' to persist this suggestion after confirming the carrier is correct.\n";
        }
    };

    if (!cliBatchCommands.empty()) {
        const std::string last = cliBatchCommands.back();
        std::string lowerLast = last;
        std::transform(lowerLast.begin(), lowerLast.end(), lowerLast.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (lowerLast != "quit" && lowerLast != "exit" && lowerLast != "q") {
            cliBatchCommands.push_back("quit");
        }
    }

    std::string line;
    try {
        while (true) {
            if (!cliBatchCommands.empty()) {
                line = std::move(cliBatchCommands.front());
                cliBatchCommands.pop_front();
                std::cout << "sdr> " << line << "\n";
                std::cout.flush();
                spdlog::info("CLI executing batch command: {}", line);
            } else {
                std::cout << "sdr> " << std::flush;
                if (!std::getline(std::cin, line)) break;
            }
            if (line.size() >= 3 &&
                static_cast<unsigned char>(line[0]) == 0xef &&
                static_cast<unsigned char>(line[1]) == 0xbb &&
                static_cast<unsigned char>(line[2]) == 0xbf) {
                line.erase(0, 3);
            }
            std::istringstream iss(line);
            std::string cmd; iss >> cmd;
            if (cmd.empty()) continue;
            for (auto& c : cmd) c = (char)std::tolower(c);

            if (cmd == "quit" || cmd == "exit" || cmd == "q") {
                mgr.stopAllTx();
                cliStop = true;
                break;
            } else if (cmd == "help" || cmd == "h" || cmd == "?") {
            std::cout << "Commands:\n"
                      << "  list | devices          - show enumerated devices\n"
                      << "  enable <i>              - enable + start real streaming on device i\n"
                      << "  disable <i>             - stop streaming on device i\n"
                      << "  tune <mhz> [rx]         - tune (e.g. tune 98.9 or tune 0 98.9)\n"
                      << "  mode <auto|wfm|nfm|am|usb|lsb|cw> [rx]\n"
                      << "  set bw <khz|auto> [rx]  - set/detect channel BW (e.g. set bw 12.5)\n"
                      << "  set lpf <khz|on|off> [rx] - set or disable audio LPF\n"
                      << "  spectrum fft <4096|8192|16384|65536> [dev] - set waterfall FFT precision\n"
                      << "  gain <i> <db>           - set live device RF gain\n"
                      << "  ppm <i> <ppm> | ppm cal/apply <i> <known_mhz> [search_khz]\n"
                      << "  squelch <db> [rx]       - set squelch\n"
                      << "  stats | status [rx]     - live diagnostics (gain/mode/BW/IQ/DSP/ring/underrun)\n"
                      << "  fav list|add|tune|del   - saved frequencies\n"
                      << "  plans                   - show built-in band auto-mode plans\n"
                      << "  classify [dev] [rx]     - advanced mode/BW/filter classifier\n"
                      << "  capture <label> [rx]    - save SigMF + classifier tile training sample\n"
                      << "  model status|load|unload - experimental ONNX placeholder; deterministic classifier remains active\n"
                      << "  p25 [dev]               - list likely P25 control-channel candidates\n"
                      << "  p25 tgs                 - list discovered/verified P25 talkgroups\n"
                      << "  p25 addtg <cc_mhz> <tgid> [tag] - manually verify a TG\n"
                      << "  p25 deltg <index>       - delete a saved P25 talkgroup row\n"
                      << "  p25 monitor <cc_mhz> [rx] - tune muted P25 control channel\n"
                      << "  p25 follow <index> [rx] - tune to unencrypted active TG voice freq\n"
                      << "  p25 tsbk <cc_mhz> <hex> - ingest decoded P25 TSBK bytes\n"
                      << "  p25 sync [dev] [target_mhz] [ms] - live C4FM/CQPSK frame-sync/NID/TSBK check\n"
                      << "  p25 waitgrant <cc_mhz> [dev] [seconds] [follow] [record[=seconds]] [wav] - wait for grant, optionally follow and save follow IQ/WAV audio\n"
                      << "  p25 clearaudio <cc_mhz> [dev] [seconds] [record=<seconds>] [tg=<id>] - wait/follow/save IQ+WAV for repeatable clear-audio diagnostics\n"
                      << "  p25 replay <sigmf-meta|sigmf-data|dir> [target_mhz] [ms] [phase2] [skip=<ms>] [center=<mhz>] [nac=<id> wacn=<id> system=<id>] - replay saved IQ through P25 decoder\n"
                      << "  p25 followtest <sigmf-meta|sigmf-data|dir> <cc_mhz> [ms] [skip=<ms>] [center=<mhz>] [voicecenter=<mhz>] [followms=<ms>] [tg=<id>] [nac= wacn= system=] - replay CC grants and test retuned voice follow/audio gates\n"
                      << "  p25 audit                 - run static P25 parity verify scripts\n"
                      << "  p25 test                  - build + run P25 unit/verify suite\n"
                      << "  test                      - alias for p25 test\n"
                      << "  p25 voicetest <sigmf|dir> <voice_mhz> [ms] [skip=<ms>] [slot=0|1] [tg=] [nac= wacn= system=] [clear|enc] [stream|legacy] [probe|noprobe] [windowms=720] [hopms=0|auto] [wav=out.wav] [oppwav=companion.wav] [minframes=N] [minaudio=S] - continuous Phase 2 voice replay + automation gates\n"
                      << "  p25 voice               - show P25 voice backend status + Phase 2 validation-log path\n"
                      << "  tx status|arm|disarm|config|ptt on|ptt off - P25 clear TX shell\n"
                      << "  tx tone <dev> <mhz> [hz=1000] [sec=2] [gain=20] [dump=path.cf32] - Sprint 1 tone TX / IQ dump\n"
                      << "  tx stop [dev]           - stop tone/TX on device (or all)\n"
                      << "  tx encode [sec=2] [backend=energy|silence] [mic=i] - Sprint 3 mic→AMBE placeholder + dibit skeleton dump\n"
                      << "  audio list              - list playback devices\n"
                      << "  audio enable <out0> <out1?>\n"
                      << "  audio disable           - stop audio outputs\n"
                      << "  audio mic list|start [i]|stop|level|dump <sec> [path] - Sprint 2 mic capture\n"
                      << "  rx add                  - add another receiver entry\n"
                      << "  quit / exit\n";
            } else if (cmd == "test") {
                std::cout << "Running scripts/build_test_validate.ps1 ...\n";
                const int rc = std::system("powershell -NoProfile -ExecutionPolicy Bypass -File scripts\\build_test_validate.ps1");
                if (rc != 0) std::cout << "test: FAIL (exit " << rc << ")\n";
                else std::cout << "test: PASS\n";
            } else if (cmd == "list" || cmd == "devices") {
            auto dlist = mgr.getDevices();
            for (size_t i=0; i<dlist.size(); ++i) {
                const auto& d = dlist[i];
                std::cout << "[" << i << "] " << d.driver << "/" << d.label
                          << " en=" << d.enabled
                          << " state=\"" << mgr.getRuntimeStateLabel(i) << "\""
                          << " sr=" << d.sampleRate
                          << " gain=" << d.gain
                          << " ppm=" << d.frequencyCorrectionPpm << "\n";
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
                    clearCliP25VoiceFollow(rx);
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
                if (m=="auto") newMode=DemodMode::AUTO;
                else if (m=="wfm") newMode=DemodMode::WFM;
                else if (m=="nfm") newMode=DemodMode::NFM;
                else if (m=="am") newMode=DemodMode::AM;
                else if (m=="usb") newMode=DemodMode::USB;
                else if (m=="lsb") newMode=DemodMode::LSB;
                else if (m=="cw") newMode=DemodMode::CW;
                if (const auto* plan = findBandPlanForFrequency(rx.freqHz); plan && (newMode == DemodMode::AUTO || newMode == plan->mode)) {
                    newBw = plan->bandwidthHz;
                    newLpf = plan->lpfHz;
                } else {
                    newBw = defaultBandwidthForMode(newMode);
                    newLpf = lpfForModeAndBandwidth(newMode, newBw);
                }
                if (newMode != rx.mode || std::abs(newBw - rx.channelBwHz) > 1.0) {
                    rx.resetDemodState();
                    clearCliP25VoiceFollow(rx);
                    rx.mode = newMode;
                    rx.channelBwHz = newBw;
                    rx.lpfHz = newLpf;
                }
            }
            std::cout << "RX" << rxidx << " mode -> " << m << "\n";
        } else if (cmd == "set" ) {
            std::string sub; iss >> sub; for(auto& c : sub) c = (char)std::tolower((unsigned char)c);
            if (sub == "bw") {
                std::string val; int rxidx=0; iss >> val; if(iss>>rxidx){}
                for (auto& c : val) c = (char)std::tolower((unsigned char)c);
                double khz = 0.0;
                if (val == "auto") {
                    size_t devIndex = 0;
                    {
                        std::lock_guard<std::mutex> lk(cliRxMutex);
                        ensureCliRxLocked(rxidx);
                        Receiver& rx = *cliReceivers[rxidx];
                        std::lock_guard<std::mutex> rxLock(rx.stateMutex);
                        devIndex = rx.deviceIndex;
                    }
                    std::vector<float> p; double cf=0, sr=0;
                    if (!mgr.getLatestSpectrum(devIndex, p, cf, sr) || p.empty()) {
                        std::cout << "no spectrum yet for auto BW\n";
                        continue;
                    }
                    {
                        std::lock_guard<std::mutex> lk(cliRxMutex);
                        ensureCliRxLocked(rxidx);
                        Receiver& rx = *cliReceivers[rxidx];
                        std::lock_guard<std::mutex> rxLock(rx.stateMutex);
                        auto smart = chooseSmartModeAndBandwidth(p, sr, cf, rx.freqHz, rx.mode);
                        double newBw = smart.bandwidthHz;
                        rx.resetDemodState();
                        clearCliP25VoiceFollow(rx);
                        if (rx.mode == DemodMode::AUTO) rx.mode = smart.mode;
                        rx.channelBwHz = newBw;
                        rx.lpfHz = smart.lpfHz;
                        khz = newBw / 1000.0;
                    }
                } else {
                    khz = std::stod(val);
                    {
                        std::lock_guard<std::mutex> lk(cliRxMutex);
                        ensureCliRxLocked(rxidx);
                        Receiver& rx = *cliReceivers[rxidx];
                        std::lock_guard<std::mutex> rxLock(rx.stateMutex);
                        const double newBw = khz * 1000.0;
                        if (std::abs(rx.channelBwHz - newBw) > 1.0) {
                            rx.resetDemodState();
                            clearCliP25VoiceFollow(rx);
                            rx.channelBwHz = newBw;
                        }
                    }
                }
                std::cout << "RX" << rxidx << " BW -> " << khz << " kHz\n";
            } else if (sub == "lpf") {
                std::string val; int rxidx=0; iss >> val; if(iss>>rxidx){}
                for (auto& c : val) c = (char)std::tolower((unsigned char)c);
                {
                    std::lock_guard<std::mutex> lk(cliRxMutex);
                    ensureCliRxLocked(rxidx);
                    Receiver& rx = *cliReceivers[rxidx];
                    std::lock_guard<std::mutex> rxLock(rx.stateMutex);
                    if (val == "off" || val == "0") {
                        rx.audioLpfEnabled = false;
                    } else if (val == "on") {
                        rx.audioLpfEnabled = true;
                    } else {
                        rx.lpfHz = std::clamp(std::stod(val) * 1000.0, 100.0, 200000.0);
                        rx.audioLpfEnabled = true;
                    }
                    rx.resetDemodState();
                    std::cout << "RX" << rxidx << " LPF -> " << (rx.audioLpfEnabled ? std::to_string(rx.lpfHz/1000.0) + " kHz" : std::string("off")) << "\n";
                }
            } else {
                std::cout << "set what? bw, lpf\n";
            }
        } else if (cmd == "spectrum" || cmd == "waterfall") {
            std::string sub;
            iss >> sub;
            for (auto& c : sub) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (sub == "fft" || sub == "bins") {
                std::string binsText;
                int devIdx = 0;
                if (!(iss >> binsText)) {
                    std::cout << "usage: spectrum fft <4096|8192|16384|65536> [dev]\n";
                    continue;
                }
                if (iss >> devIdx) {}
                if (devIdx < 0 || static_cast<size_t>(devIdx) >= mgr.getDevices().size()) {
                    std::cout << "bad device index\n";
                    continue;
                }
                size_t bins = 8192;
                try {
                    bins = static_cast<size_t>(std::stoull(binsText));
                } catch (...) {
                    std::cout << "usage: spectrum fft <4096|8192|16384|65536> [dev]\n";
                    continue;
                }
                mgr.setSpectrumFftBins(static_cast<size_t>(devIdx), bins);
                std::cout << "Device " << devIdx << " spectrum FFT -> "
                          << mgr.getSpectrumFftBins(static_cast<size_t>(devIdx)) << " bins\n";
            } else if (sub == "status" || sub.empty()) {
                int devIdx = 0;
                if (iss >> devIdx) {}
                if (devIdx < 0 || static_cast<size_t>(devIdx) >= mgr.getDevices().size()) {
                    std::cout << "bad device index\n";
                    continue;
                }
                std::cout << "Device " << devIdx << " spectrum FFT: "
                          << mgr.getSpectrumFftBins(static_cast<size_t>(devIdx)) << " bins\n";
            } else {
                std::cout << "spectrum fft <4096|8192|16384|65536> [dev] | spectrum status [dev]\n";
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
        } else if (cmd == "ppm") {
            std::string first;
            if (!(iss >> first)) {
                printPpmUsage();
                continue;
            }
            std::string sub = first;
            for (auto& c : sub) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (sub == "cal" || sub == "calibrate" || sub == "apply") {
                int di = 0;
                double knownMhz = 0.0;
                double searchKhz = 45.0;
                if (!(iss >> di >> knownMhz)) {
                    printPpmUsage();
                    continue;
                }
                if (iss >> searchKhz) {}
                runPpmCalibration(sub == "apply", di, knownMhz, searchKhz);
            } else {
                int di = 0;
                double ppm = 0.0;
                std::istringstream firstStream(first);
                if (!(firstStream >> di) || !(iss >> ppm)) {
                    printPpmUsage();
                    continue;
                }
                if (di >= 0 && static_cast<size_t>(di) < mgr.getDevices().size()) {
                    mgr.setFrequencyCorrection(static_cast<size_t>(di), ppm);
                    std::cout << "Frequency correction device " << di << " -> " << ppm << " ppm\n";
                } else {
                    std::cout << "bad device index\n";
                }
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
        } else if (cmd == "fav" || cmd == "favorite" || cmd == "favorites") {
            std::string sub;
            iss >> sub;
            for (auto& c : sub) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (sub == "list" || sub.empty()) {
                auto freqs = loadSavedFrequencies();
                if (freqs.empty()) {
                    std::cout << "No saved frequencies yet.\n";
                } else {
                    for (size_t i = 0; i < freqs.size(); ++i) {
                        const auto& sf = freqs[i];
                        std::cout << "[" << i << "] " << sf.name
                                  << " " << (sf.freqHz / 1e6) << " MHz"
                                  << " " << modeToString(sf.mode)
                                  << " bw=" << (sf.bandwidthHz / 1000.0) << "kHz"
                                  << (sf.tags.empty() ? "" : (" tags=" + sf.tags)) << "\n";
                    }
                }
            } else if (sub == "add") {
                std::string name;
                std::getline(iss, name);
                name = trimCopy(name);
                SavedFrequency sf;
                {
                    std::lock_guard<std::mutex> lk(cliRxMutex);
                    ensureCliRxLocked(0);
                    Receiver& rx = *cliReceivers[0];
                    std::lock_guard<std::mutex> rxLock(rx.stateMutex);
                    sf.freqHz = rx.freqHz;
                    sf.mode = rx.mode;
                    sf.bandwidthHz = rx.channelBwHz;
                    sf.lpfHz = rx.lpfHz;
                    sf.lpfEnabled = rx.audioLpfEnabled;
                    sf.squelchDb = rx.squelchDb;
                }
                if (name.empty()) {
                    std::ostringstream os;
                    os << modeToString(sf.mode) << " " << (sf.freqHz / 1e6) << " MHz";
                    name = os.str();
                }
                sf.name = name;
                if (const auto* plan = findBandPlanForFrequency(sf.freqHz)) sf.tags = plan->name;
                auto freqs = loadSavedFrequencies();
                freqs.push_back(sf);
                saveSavedFrequencies(freqs);
                std::cout << "Saved [" << (freqs.size() - 1) << "] " << sf.name << "\n";
            } else if (sub == "tune") {
                int idx = -1;
                int rxidx = 0;
                if (!(iss >> idx)) {
                    std::cout << "usage: fav tune <index> [rx]\n";
                    continue;
                }
                if (iss >> rxidx) {}
                auto freqs = loadSavedFrequencies();
                if (idx < 0 || static_cast<size_t>(idx) >= freqs.size()) {
                    std::cout << "bad favorite index\n";
                    continue;
                }
                const auto sf = freqs[static_cast<size_t>(idx)];
                size_t devIndex = 0;
                {
                    std::lock_guard<std::mutex> lk(cliRxMutex);
                    ensureCliRxLocked(static_cast<size_t>(std::max(0, rxidx)));
                    Receiver& rx = *cliReceivers[static_cast<size_t>(std::max(0, rxidx))];
                    std::lock_guard<std::mutex> rxLock(rx.stateMutex);
                    devIndex = rx.deviceIndex;
                    rx.resetDemodState();
                    clearCliP25VoiceFollow(rx);
                    rx.freqHz = sf.freqHz;
                    rx.mode = sf.mode;
                    rx.channelBwHz = sf.bandwidthHz;
                    rx.lpfHz = sf.lpfHz;
                    rx.audioLpfEnabled = sf.lpfEnabled;
                    rx.squelchDb = sf.squelchDb;
                    rx.active = true;
                }
                if (devIndex < mgr.getDevices().size()) mgr.setCenterFreq(devIndex, sf.freqHz);
                if (!mgr.isStreaming(devIndex) && devIndex < mgr.getDevices().size()) {
                    mgr.setEnabled(devIndex, true);
                    mgr.startStreaming(devIndex, true);
                }
                std::cout << "Tuned RX" << rxidx << " to favorite [" << idx << "] " << sf.name << "\n";
            } else if (sub == "del" || sub == "delete" || sub == "rm") {
                int idx = -1;
                if (!(iss >> idx)) {
                    std::cout << "usage: fav del <index>\n";
                    continue;
                }
                auto freqs = loadSavedFrequencies();
                if (idx < 0 || static_cast<size_t>(idx) >= freqs.size()) {
                    std::cout << "bad favorite index\n";
                    continue;
                }
                auto removed = freqs[static_cast<size_t>(idx)];
                freqs.erase(freqs.begin() + idx);
                saveSavedFrequencies(freqs);
                std::cout << "Deleted favorite [" << idx << "] " << removed.name << "\n";
            } else {
                std::cout << "fav list | fav add <name> | fav tune <index> [rx] | fav del <index>\n";
            }
        } else if (cmd == "tx") {
            // Sprint 0: clear TX shell — state machine only, no writeStream / RF.
            std::string sub; iss >> sub;
            for (auto& c : sub) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            auto printTxStatus = [&]() {
                std::cout << "P25 TX state=" << p25TxStateLabel(cliP25TxSnapshot.state)
                          << " armed=" << (cliP25TxSnapshot.config.armed ? "yes" : "no")
                          << " rid=0x" << std::hex << cliP25TxSnapshot.config.unitId << std::dec
                          << " tg=" << cliP25TxSnapshot.config.talkgroupId
                          << " nac=0x" << std::hex << cliP25TxSnapshot.config.nac << std::dec
                          << " txDev=" << cliP25TxSnapshot.config.txDeviceIndex
                          << " canTxFlag=" << (cliP25TxSnapshot.deviceCanTx ? "yes" : "no")
                          << " ambeEnc=" << (cliP25TxSnapshot.config.ambeEncoderAvailable ? "yes" : "no")
                          << "\n";
            };
            auto applyCliTx = [&](P25TxEvent ev) {
                cliP25TxSnapshot.nowMs = QDateTime::currentMSecsSinceEpoch();
                if (cliP25TxSnapshot.stateEnteredMs <= 0)
                    cliP25TxSnapshot.stateEnteredMs = cliP25TxSnapshot.nowMs;
                cliP25TxSnapshot.deviceCanTx = cliP25TxSnapshot.config.txDeviceIndex >= 0;
                const auto d = evaluateP25Tx(cliP25TxSnapshot, ev);
                if (d.changed) {
                    cliP25TxSnapshot.state = d.nextState;
                    cliP25TxSnapshot.stateEnteredMs = cliP25TxSnapshot.nowMs;
                }
                std::cout << "TX event=" << p25TxEventLabel(ev)
                          << " -> " << d.statusLine
                          << " request=" << (d.emitChannelRequest ? "yes" : "no")
                          << " startVoice=" << (d.startVoiceTx ? "stub" : "no")
                          << " stopVoice=" << (d.stopVoiceTx ? "yes" : "no")
                          << "\n";
            };
            if (sub.empty() || sub == "status") {
                printTxStatus();
            } else if (sub == "config") {
                std::string key; iss >> key;
                if (key.empty()) {
                    printTxStatus();
                    std::cout << "tx config rid <id> | tg <id> | nac <hex|dec> | device <i>\n";
                } else if (key == "rid") {
                    std::string rs; iss >> rs;
                    const unsigned long v = std::strtoul(rs.c_str(), nullptr, 0);
                    cliP25TxSnapshot.config.unitId = static_cast<uint32_t>(v);
                    std::cout << "tx rid=" << cliP25TxSnapshot.config.unitId << "\n";
                } else if (key == "tg") {
                    unsigned v = 0; iss >> v;
                    cliP25TxSnapshot.config.talkgroupId = v;
                    std::cout << "tx tg=" << cliP25TxSnapshot.config.talkgroupId << "\n";
                } else if (key == "nac") {
                    std::string ns; iss >> ns;
                    const unsigned long v = std::strtoul(ns.c_str(), nullptr, 0);
                    cliP25TxSnapshot.config.nac = static_cast<uint16_t>(v & 0xFFFu);
                    std::cout << "tx nac=0x" << std::hex << cliP25TxSnapshot.config.nac << std::dec << "\n";
                } else if (key == "device") {
                    int i = -1; iss >> i;
                    cliP25TxSnapshot.config.txDeviceIndex = i;
                    std::cout << "tx device=" << i << "\n";
                } else {
                    std::cout << "unknown tx config key\n";
                }
            } else if (sub == "arm") {
                cliP25TxSnapshot.config.armed = true;
                cliP25TxSnapshot.config.clearOnly = true;
                applyCliTx(P25TxEvent::Arm);
                printTxStatus();
            } else if (sub == "disarm") {
                cliP25TxSnapshot.config.armed = false;
                applyCliTx(P25TxEvent::Disarm);
                printTxStatus();
            } else if (sub == "ptt") {
                std::string onoff; iss >> onoff;
                for (auto& c : onoff) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (onoff == "on" || onoff == "press" || onoff == "1") {
                    cliP25TxSnapshot.pttHeld = true;
                    cliP25TxSnapshot.pttPressedMs = QDateTime::currentMSecsSinceEpoch();
                    applyCliTx(P25TxEvent::PttPress);
                    if (cliP25TxSnapshot.state == P25TxState::Requesting)
                        applyCliTx(P25TxEvent::None);
                } else if (onoff == "off" || onoff == "release" || onoff == "0") {
                    cliP25TxSnapshot.pttHeld = false;
                    applyCliTx(P25TxEvent::PttRelease);
                    if (cliP25TxSnapshot.state == P25TxState::Hang) {
                        cliP25TxSnapshot.hangMs = 0;
                        cliP25TxSnapshot.stateEnteredMs = 0;
                        applyCliTx(P25TxEvent::HangComplete);
                    }
                } else {
                    std::cout << "tx ptt on|off\n";
                }
                printTxStatus();
            } else if (sub == "tone") {
                // Sprint 1: continuous complex baseband tone → hardware TX and/or CF32 dump.
                int dev = 0;
                double mhz = 0.0;
                iss >> dev >> mhz;
                double toneHz = 1000.0;
                double seconds = 2.0;
                double gainDb = 20.0;
                std::string dumpPath;
                std::string tok;
                while (iss >> tok) {
                    auto eq = tok.find('=');
                    if (eq == std::string::npos) continue;
                    std::string k = tok.substr(0, eq);
                    std::string v = tok.substr(eq + 1);
                    for (auto& c : k) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    if (k == "hz" || k == "tone") toneHz = std::strtod(v.c_str(), nullptr);
                    else if (k == "sec" || k == "seconds") seconds = std::strtod(v.c_str(), nullptr);
                    else if (k == "gain") gainDb = std::strtod(v.c_str(), nullptr);
                    else if (k == "dump" || k == "file") dumpPath = v;
                }
                if (mhz <= 0.0) {
                    std::cout << "tx tone <dev> <mhz> [hz=1000] [sec=2] [gain=20] [dump=path.cf32]\n";
                } else {
                    if (dumpPath.empty()) {
                        const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
                        QDir().mkpath(appData + "/tx_dumps");
                        dumpPath = (appData + "/tx_dumps/tone_" +
                            QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".cf32").toStdString();
                    }
                    DeviceManager::TxParams tp;
                    tp.centerHz = mhz * 1e6;
                    tp.toneHz = toneHz;
                    tp.gainDb = gainDb;
                    tp.dumpPath = dumpPath;
                    tp.attemptHardware = true;
                    tp.allowFileOnlyFallback = true;
                    {
                        auto* di = mgr.getDevice(static_cast<size_t>(dev));
                        if (di && di->sampleRate > 1e5) tp.sampleRate = di->sampleRate;
                    }
                    const bool ok = mgr.startToneTx(static_cast<size_t>(dev), tp);
                    std::cout << "tx tone start dev=" << dev
                              << " mhz=" << mhz
                              << " hz=" << toneHz
                              << " sec=" << seconds
                              << " state=" << mgr.getTxRuntimeState(static_cast<size_t>(dev))
                              << " hw=" << (mgr.isHardwareTxActive(static_cast<size_t>(dev)) ? "yes" : "no")
                              << " dump=" << dumpPath
                              << " ok=" << (ok ? "yes" : "no") << "\n";
                    if (ok && seconds > 0.0) {
                        const int ms = static_cast<int>(std::max(0.05, seconds) * 1000.0);
                        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
                        mgr.stopTx(static_cast<size_t>(dev));
                        std::cout << "tx tone stopped samples="
                                  << mgr.getTxSamplesWritten(static_cast<size_t>(dev))
                                  << " state=" << mgr.getTxRuntimeState(static_cast<size_t>(dev)) << "\n";
                    }
                }
            } else if (sub == "stop") {
                int dev = -1;
                if (iss >> dev) {
                    mgr.stopTx(static_cast<size_t>(dev));
                    std::cout << "tx stop dev=" << dev << "\n";
                } else {
                    mgr.stopAllTx();
                    std::cout << "tx stop all\n";
                }
            } else if (sub == "encode") {
                // Sprint 3: capture mic → 8 kHz → placeholder AMBE frames → superframe skeleton dump.
                double sec = 2.0;
                std::string backend = "energy";
                int micIdx = -1;
                std::string tok;
                // optional leading seconds
                if (iss >> tok) {
                    if (tok.find('=') == std::string::npos) {
                        sec = std::strtod(tok.c_str(), nullptr);
                    } else {
                        // put back by parsing as key=val
                        auto eq = tok.find('=');
                        std::string k = tok.substr(0, eq);
                        std::string v = tok.substr(eq + 1);
                        if (k == "sec" || k == "seconds") sec = std::strtod(v.c_str(), nullptr);
                        else if (k == "backend") backend = v;
                        else if (k == "mic") micIdx = std::atoi(v.c_str());
                    }
                }
                while (iss >> tok) {
                    auto eq = tok.find('=');
                    if (eq == std::string::npos) continue;
                    std::string k = tok.substr(0, eq);
                    std::string v = tok.substr(eq + 1);
                    if (k == "sec" || k == "seconds") sec = std::strtod(v.c_str(), nullptr);
                    else if (k == "backend") backend = v;
                    else if (k == "mic") micIdx = std::atoi(v.c_str());
                }
                if (!cliMic) cliMic = std::make_unique<AudioCapture>();
                if (!cliMic->isCapturing()) {
                    if (!cliMic->start(micIdx, 48000.0)) {
                        std::cout << "tx encode: mic start failed\n";
                        continue;
                    }
                }
                P25TxVoicePacketizer pkt(p25CreateAmbeEncoder(backend));
                std::vector<P25AmbeEncodedFrame> frames;
                const auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(static_cast<int>(std::max(0.2, sec) * 1000.0));
                float sink[2048];
                while (std::chrono::steady_clock::now() < deadline) {
                    const size_t n = cliMic->pull(sink, 2048);
                    if (n == 0) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                        continue;
                    }
                    float pcm8k[512];
                    size_t outN = 0;
                    for (size_t i = 0; i + 6 <= n && outN < 512; i += 6) {
                        float s = 0.0f;
                        for (size_t k = 0; k < 6; ++k) s += sink[i + k];
                        pcm8k[outN++] = s / 6.0f;
                    }
                    if (outN) pkt.pushPcm8k(pcm8k, outN, frames);
                }
                const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
                QDir().mkpath(appData + "/tx_dumps");
                const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
                const std::string ambePath = (appData + "/tx_dumps/ambe_" + stamp + ".bin").toStdString();
                const std::string dibitPath = (appData + "/tx_dumps/dibits_" + stamp + ".bin").toStdString();
                p25WriteAmbePackedDump(ambePath, frames);
                P25Phase2TxFramer framer;
                P25Phase2TxFramerConfig fcfg;
                fcfg.nac = cliP25TxSnapshot.config.nac;
                fcfg.wacn = cliP25TxSnapshot.config.wacn;
                fcfg.systemId = cliP25TxSnapshot.config.systemId;
                fcfg.talkgroupId = cliP25TxSnapshot.config.talkgroupId;
                fcfg.unitId = cliP25TxSnapshot.config.unitId;
                framer.setConfig(fcfg);
                const auto sf = framer.buildSuperframe(frames);
                if (sf.valid) p25WriteTxDibitDump(dibitPath, sf);
                std::cout << "tx encode frames=" << frames.size()
                          << " backend=" << (pkt.encoder() ? pkt.encoder()->name() : "?")
                          << " realSpeech=" << (pkt.encoder() && pkt.encoder()->producesRealSpeech() ? "yes" : "no")
                          << " ambe=" << ambePath
                          << " dibits=" << dibitPath
                          << " sfVoice=" << sf.voiceFramesUsed << "\n";
            } else {
                std::cout << "tx status | arm | disarm | config ... | ptt on|off | tone ... | stop | encode\n";
            }
        } else if (cmd == "audio") {
            std::string sub; iss >> sub; for(auto& c : sub) c = (char)std::tolower((unsigned char)c);
            if (sub == "list") {
                if (!cliAudio) cliAudio = std::make_unique<AudioEngine>();
                auto outs = cliAudio->enumeratePlaybackDevices();
                for (size_t i=0; i<outs.size(); ++i) {
                    std::cout << "  [" << i << "] " << outs[i].name << (outs[i].isDefault ? " (default)" : "") << "\n";
                }
            } else if (sub == "mic") {
                std::string msub; iss >> msub;
                for (auto& c : msub) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (!cliMic) cliMic = std::make_unique<AudioCapture>();
                if (msub.empty() || msub == "list") {
                    auto mics = cliMic->enumerateCaptureDevices();
                    if (mics.empty()) std::cout << "(no capture devices)\n";
                    for (size_t i = 0; i < mics.size(); ++i) {
                        std::cout << "  [" << i << "] " << mics[i].name
                                  << (mics[i].isDefault ? " (default)" : "") << "\n";
                    }
                } else if (msub == "start") {
                    int idx = -1; iss >> idx;
                    const bool ok = cliMic->start(idx, 48000.0);
                    std::cout << "mic start ok=" << (ok ? "yes" : "no")
                              << " device=" << cliMic->activeDeviceName()
                              << " sr=" << cliMic->sampleRateHz() << "\n";
                } else if (msub == "stop") {
                    cliMic->stop();
                    if (cliAudio) cliAudio->setOutputMuted(false);
                    std::cout << "mic stopped\n";
                } else if (msub == "level") {
                    if (!cliMic->isCapturing()) {
                        std::cout << "mic not capturing (audio mic start first)\n";
                    } else {
                        float sink[2048];
                        while (cliMic->pull(sink, 2048) > 0) {}
                        std::cout << "mic rms=" << cliMic->levelRms()
                                  << " peak=" << cliMic->levelPeak()
                                  << " meter=" << cliMic->levelMeter()
                                  << " frames=" << cliMic->framesCaptured()
                                  << " overruns=" << cliMic->overrunCount() << "\n";
                    }
                } else if (msub == "dump") {
                    double sec = 2.0; iss >> sec;
                    std::string path;
                    iss >> path;
                    if (path.empty()) {
                        const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
                        QDir().mkpath(appData + "/tx_dumps");
                        path = (appData + "/tx_dumps/mic_" +
                            QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".wav").toStdString();
                    }
                    int idx = cliMic->activeDeviceIndex();
                    const bool ok = cliMic->captureToWav(path, sec > 0.0 ? sec : 2.0, idx);
                    std::cout << "mic dump ok=" << (ok ? "yes" : "no") << " path=" << path << "\n";
                } else {
                    std::cout << "audio mic list|start [i]|stop|level|dump <sec> [path]\n";
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
        } else if (cmd == "plans" || cmd == "bandplans") {
            for (const auto& p : builtInBandPlans()) {
                std::cout << p.name
                          << " " << (p.startHz / 1e6) << "-" << (p.endHz / 1e6) << " MHz"
                          << " mode=" << modeToString(p.mode)
                          << " bw=" << (p.bandwidthHz / 1000.0) << "kHz"
                          << " lpf=" << (p.lpfHz / 1000.0) << "kHz"
                          << " step=" << (p.stepHz / 1000.0) << "kHz\n";
            }
        } else if (cmd == "classify" || cmd == "classifier") {
            int devIndex = 0;
            int rxidx = 0;
            if (iss >> devIndex) {
                if (iss >> rxidx) {}
            }
            double rxFreq = 100e6;
            {
                std::lock_guard<std::mutex> lk(cliRxMutex);
                ensureCliRxLocked(static_cast<size_t>(std::max(0, rxidx)));
                Receiver& rx = *cliReceivers[static_cast<size_t>(std::max(0, rxidx))];
                std::lock_guard<std::mutex> rxLock(rx.stateMutex);
                rxFreq = rx.freqHz;
            }
            std::vector<float> p; double cf=0, sr=0;
            if (devIndex < 0 || !mgr.getLatestSpectrum(static_cast<size_t>(devIndex), p, cf, sr) || p.empty()) {
                std::cout << "no spectrum yet for classifier (enable/tune first)\n";
                continue;
            }
            auto rec = AdvancedSignalClassifier::instance().classifySpectrum(p, sr, cf, rxFreq);
            std::cout << "Classifier dev=" << devIndex
                      << " rx=" << rxidx
                      << " freq=" << (rxFreq / 1e6) << " MHz"
                      << " class=" << rec.label
                      << " confidence=" << (rec.confidence * 100.0) << "%"
                      << " demod=" << modeToString(rec.demodMode)
                      << " estBW=" << (rec.estimatedBandwidthHz / 1000.0) << "kHz"
                      << " stdBW=" << (rec.standardBandwidthHz / 1000.0) << "kHz"
                      << " audioLPF=" << (rec.audioLowPassHz / 1000.0) << "kHz"
                      << " filter=" << classifierFilterKindToString(rec.filterKind)
                      << " snr=" << rec.features.snrDb << "dB"
                      << " reason=\"" << rec.reason << "\"\n";
        } else if (cmd == "capture") {
            std::string label;
            std::getline(iss, label);
            label = trimCopy(label);
            int rxidx = 0;
            const auto lastSpace = label.find_last_of(' ');
            if (lastSpace != std::string::npos) {
                std::string tail = label.substr(lastSpace + 1);
                if (!tail.empty() && std::all_of(tail.begin(), tail.end(), [](unsigned char c) { return std::isdigit(c); })) {
                    rxidx = std::stoi(tail);
                    label = trimCopy(label.substr(0, lastSpace));
                }
            }
            if (label.empty()) label = "unknown";

            TrainingCaptureRequest req;
            req.label = label;
            {
                std::lock_guard<std::mutex> lk(cliRxMutex);
                ensureCliRxLocked(static_cast<size_t>(std::max(0, rxidx)));
                Receiver& rx = *cliReceivers[static_cast<size_t>(std::max(0, rxidx))];
                std::lock_guard<std::mutex> rxLock(rx.stateMutex);
                req.deviceIndex = rx.deviceIndex;
                req.tunedFreqHz = rx.freqHz;
                req.mode = rx.mode;
                req.channelBwHz = rx.channelBwHz;
                req.lpfHz = rx.lpfHz;
                req.audioLpfEnabled = rx.audioLpfEnabled;
                req.squelchDb = rx.squelchDb;
            }
            if (req.deviceIndex >= mgr.getDevices().size() || !mgr.isStreaming(req.deviceIndex)) {
                std::cout << "capture needs a streaming device (enable/tune first)\n";
                continue;
            }
            if (!mgr.getLatestSpectrum(req.deviceIndex, req.spectrumDb, req.centerFreqHz, req.sampleRateHz) || req.spectrumDb.empty()) {
                std::cout << "no spectrum yet for capture\n";
                continue;
            }
            const auto dlist = mgr.getDevices();
            if (req.deviceIndex < dlist.size()) req.device = dlist[req.deviceIndex];
            const size_t requestedSamples = static_cast<size_t>(std::clamp(req.sampleRateHz * 1.0, 16384.0, 2400000.0));
            req.iq = mgr.getRecentIQWindow(req.deviceIndex, requestedSamples);

            const double roiHz = std::clamp(
                std::max(req.channelBwHz * 4.0, req.channelBwHz >= 100000.0 ? 350000.0 : 50000.0),
                20000.0,
                req.sampleRateHz);
            WaterfallRoiBuilder cliTileBuilder(1);
            cliTileBuilder.pushSpectrum(req.spectrumDb);
            req.tile = cliTileBuilder.buildTile(req.sampleRateHz, req.centerFreqHz, req.tunedFreqHz, roiHz, 256, 256);
            auto modelRec = req.tile.valid()
                ? ClassifierModelBackend::instance().classifyTile(req.tile, req.sampleRateHz, req.centerFreqHz, req.tunedFreqHz, roiHz)
                : std::optional<SignalRecommendation>{};
            req.recommendation = modelRec.has_value()
                ? *modelRec
                : (req.tile.valid()
                    ? AdvancedSignalClassifier::instance().classifyWaterfallTile(req.tile, req.sampleRateHz, req.centerFreqHz, req.tunedFreqHz, roiHz)
                    : AdvancedSignalClassifier::instance().classifySpectrum(req.spectrumDb, req.sampleRateHz, req.centerFreqHz, req.tunedFreqHz));

            auto result = saveTrainingCapture(req);
            if (result.ok) {
                std::cout << result.message.toStdString() << "\n" << result.directory.toStdString() << "\n";
            } else {
                std::cout << "capture failed: " << result.message.toStdString() << "\n";
            }
        } else if (cmd == "model") {
            std::string sub;
            iss >> sub;
            for (auto& c : sub) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (sub == "load") {
                std::string path;
                std::getline(iss, path);
                path = trimCopy(path);
                if (path.empty()) {
                    std::cout << "usage: model load <path-to-model.onnx>\n";
                    continue;
                }
                const bool ok = ClassifierModelBackend::instance().loadModel(path);
                auto st = ClassifierModelBackend::instance().status();
                std::cout << (ok ? "model loaded: " : "model not loaded: ") << st.message << "\n";
            } else if (sub == "unload") {
                ClassifierModelBackend::instance().unloadModel();
                std::cout << "model unloaded; deterministic classifier active\n";
            } else {
                auto st = ClassifierModelBackend::instance().status();
                std::cout << "Model backend: " << st.backendName
                          << " enabled=" << (st.enabled ? "yes" : "no")
                          << " loaded=" << (st.loaded ? "yes" : "no")
                          << " path=\"" << st.modelPath << "\""
                          << " message=\"" << st.message << "\"\n";
            }
        } else if (cmd == "spectrum" || cmd == "spec") {
            // lightweight: just report latest for primary dev 0 if any
            std::vector<float> p; double cf,sr;
            if (mgr.getLatestSpectrum(0, p, cf, sr) && !p.empty()) {
                std::cout << "Spec dev0 cf=" << (cf/1e6) << " sr=" << (sr/1e6) << " bins=" << p.size() << " peak~ " << *std::max_element(p.begin(),p.end()) << "dB\n";
            } else std::cout << "no spectrum yet (enable + stream first)\n";
        } else if (cmd == "p25") {
            std::string sub;
            iss >> sub;
            for (auto& c : sub) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

            if (sub == "monitor" || sub == "cc") {
                double ccMhz = 0.0;
                int rxidx = 0;
                if (!(iss >> ccMhz) || ccMhz <= 0.0) {
                    std::cout << "usage: p25 monitor <cc_mhz> [rx]\n";
                    continue;
                }
                if (iss >> rxidx) {}
                size_t devIndex = 0;
                {
                    std::lock_guard<std::mutex> lk(cliRxMutex);
                    ensureCliRxLocked(static_cast<size_t>(std::max(0, rxidx)));
                    Receiver& rx = *cliReceivers[static_cast<size_t>(std::max(0, rxidx))];
                    std::lock_guard<std::mutex> rxLock(rx.stateMutex);
                    devIndex = rx.deviceIndex;
                    clearCliP25VoiceFollow(rx);
                    rx.resetDemodState();
                    rx.freqHz = ccMhz * 1e6;
                    rx.mode = DemodMode::NFM;
                    rx.channelBwHz = 12500.0;
                    rx.lpfHz = 3000.0;
                    rx.audioLpfEnabled = false;
                    rx.squelchDb = -105.0;
                    rx.p25ControlChannelMute = true;
                    // waitgrant has its own control-channel decoder and cursor.
                    // Keep the normal CLI DSP receiver passive here so the
                    // monitor thread does not consume the same RF ring or hold
                    // shutdown behind a muted control-channel receiver.
                    rx.active = false;
                }
                if (devIndex < mgr.getDevices().size()) mgr.setCenterFreq(devIndex, ccMhz * 1e6);
                if (!mgr.isStreaming(devIndex) && devIndex < mgr.getDevices().size()) {
                    mgr.setEnabled(devIndex, true);
                    mgr.startStreaming(devIndex, true);
                }
                std::cout << "Monitoring muted P25 control channel " << ccMhz
                          << " MHz on RX" << rxidx << " (use p25 sync to inspect frames, p25 follow for clear TG audio)\n";
            } else if (sub == "waitgrant" || sub == "grantwait" || sub == "watch" ||
                       sub == "clearaudio" || sub == "clear-audio" || sub == "audiotest") {
                double ccMhz = 0.0;
                int devIndex = 0;
                const bool clearAudioDiagnostic =
                    sub == "clearaudio" || sub == "clear-audio" || sub == "audiotest";
                double seconds = clearAudioDiagnostic ? 180.0 : 60.0;
                bool followGrant = clearAudioDiagnostic;
                bool recordFollowCapture = clearAudioDiagnostic;
                bool recordFollowAudio = clearAudioDiagnostic;
                double recordFollowSeconds = 8.0;
                uint32_t targetTalkgroupId = 0;
                if (!(iss >> ccMhz) || ccMhz <= 0.0) {
                    std::cout << "usage: p25 waitgrant <cc_mhz> [dev] [seconds] [follow] [record[=seconds]] [wav] [tg=<id>]\n"
                              << "       p25 clearaudio <cc_mhz> [dev] [seconds] [record=<seconds>] [tg=<id>]\n";
                    continue;
                }
                std::vector<std::string> waitOptions;
                std::string opt;
                while (iss >> opt) waitOptions.push_back(opt);

                int numericArg = 0;
                std::string badOption;
                for (const auto& rawOpt : waitOptions) {
                    std::string lower = rawOpt;
                    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
                        return static_cast<char>(std::tolower(c));
                    });
                    if (lower == "follow" || lower == "--follow" || lower == "audio") {
                        followGrant = true;
                        continue;
                    }
                    if (lower == "record" || lower == "--record" || lower == "capture" ||
                        lower == "--capture" || lower == "captureiq" || lower == "iqcapture") {
                        recordFollowCapture = true;
                        continue;
                    }
                    if (lower == "wav" || lower == "--wav" || lower == "recordaudio" ||
                        lower == "--recordaudio" || lower == "audiofile" || lower == "--audiofile") {
                        recordFollowAudio = true;
                        continue;
                    }

                    const size_t eq = lower.find('=');
                    if (eq != std::string::npos) {
                        const std::string key = lower.substr(0, eq);
                        const std::string valueText = rawOpt.substr(eq + 1);
                        double value = 0.0;
                        if (!parseFiniteDoubleToken(valueText, value)) {
                            badOption = rawOpt;
                            break;
                        }
                        if (key == "seconds" || key == "duration" || key == "duration_s" || key == "secs") {
                            seconds = value;
                            continue;
                        }
                        if (key == "dev" || key == "device" || key == "deviceindex") {
                            devIndex = static_cast<int>(std::llround(value));
                            continue;
                        }
                        if (key == "record" || key == "capture" || key == "captureiq" ||
                            key == "iqcapture" || key == "recordseconds" || key == "captureseconds") {
                            recordFollowCapture = true;
                            recordFollowSeconds = value;
                            continue;
                        }
                        if (key == "wav" || key == "audio" || key == "recordaudio") {
                            recordFollowAudio = (value != 0.0);
                            continue;
                        }
                        if (key == "tg" || key == "talkgroup" || key == "talkgroupid") {
                            targetTalkgroupId = static_cast<uint32_t>(std::max<long long>(0, std::llround(value)));
                            continue;
                        }
                        badOption = rawOpt;
                        break;
                    }

                    double numeric = 0.0;
                    if (parseFiniteDoubleToken(rawOpt, numeric)) {
                        if (numericArg == 0) {
                            devIndex = static_cast<int>(std::llround(numeric));
                        } else if (numericArg == 1) {
                            seconds = numeric;
                        } else {
                            badOption = rawOpt;
                            break;
                        }
                        ++numericArg;
                        continue;
                    }

                    badOption = rawOpt;
                    break;
                }
                if (!badOption.empty()) {
                    std::cout << "unknown waitgrant option: " << badOption << "\n"
                              << "usage: p25 waitgrant <cc_mhz> [dev] [seconds] [follow] [record[=seconds]] [wav] [tg=<id>]\n"
                              << "       p25 clearaudio <cc_mhz> [dev] [seconds] [record=<seconds>] [tg=<id>]\n";
                    continue;
                }
                if (recordFollowCapture) followGrant = true;
                if (recordFollowAudio) followGrant = true;
                if (devIndex < 0 || static_cast<size_t>(devIndex) >= mgr.getDevices().size()) {
                    std::cout << "bad device index\n";
                    continue;
                }
                seconds = std::clamp(seconds, 1.0, 600.0);
                recordFollowSeconds = std::clamp(recordFollowSeconds, 1.0, 20.0);
                const double ccHz = ccMhz * 1e6;
                upsertP25KnownControlChannel(ccHz, "CLI grant test");

                {
                    std::lock_guard<std::mutex> lk(cliRxMutex);
                    ensureCliRxLocked(0);
                    Receiver& rx = *cliReceivers[0];
                    std::lock_guard<std::mutex> rxLock(rx.stateMutex);
                    std::lock_guard<std::recursive_mutex> dspLock(rx.dspMutex);
                    rx.deviceIndex = static_cast<size_t>(devIndex);
                    clearCliP25VoiceFollow(rx);
                    rx.resetDemodState();
                    rx.freqHz = ccHz;
                    rx.mode = DemodMode::NFM;
                    rx.channelBwHz = 12500.0;
                    rx.lpfHz = 3000.0;
                    rx.audioLpfEnabled = false;
                    rx.squelchDb = -105.0;
                    rx.p25ControlChannelMute = true;
                    rx.active = true;
                }
                uint64_t waitGrantTuneSeq = mgr.setCenterFreq(static_cast<size_t>(devIndex), ccHz);
                if (!mgr.isStreaming(static_cast<size_t>(devIndex))) {
                    mgr.setEnabled(static_cast<size_t>(devIndex), true);
                    mgr.startStreaming(static_cast<size_t>(devIndex), true);
                }
                (void)mgr.waitForCenterTuneApplied(static_cast<size_t>(devIndex), waitGrantTuneSeq, 500);

                cliP25LiveDecoders[static_cast<size_t>(devIndex)] = P25LiveDecoder(p25CliControlGrantDecoderConfig());
                auto& decoder = cliP25LiveDecoders[static_cast<size_t>(devIndex)];
                auto& analyzer = cliP25Analyzers[static_cast<long long>(std::llround(ccHz))];
                analyzer.reset();
                const size_t seededIdentifiers = seedP25AnalyzerFromCachedChannelIdentifiers(analyzer, ccHz);
                std::cout << (clearAudioDiagnostic ? "P25 clearaudio monitoring " : "P25 waitgrant monitoring ")
                          << ccMhz << " MHz on dev " << devIndex
                          << " for " << seconds << "s"
                          << (followGrant ? " with follow test" : "")
                          << (recordFollowCapture ? (" record=" + std::to_string(recordFollowSeconds) + "s") : "")
                          << (recordFollowAudio ? " wav" : "")
                          << (targetTalkgroupId != 0 ? (" tg=" + std::to_string(targetTalkgroupId)) : "")
                          << " (raw CC audio muted)" << std::endl;
                if (seededIdentifiers > 0) {
                    std::cout << "P25 waitgrant seeded " << seededIdentifiers
                              << " cached channel identifier table(s) for "
                              << ccMhz << " MHz\n";
                }

                const qint64 grantStartMs = QDateTime::currentMSecsSinceEpoch();
                auto waitGrantStageName = [](int stage) -> const char* {
                    switch (stage) {
                        case 1: return "spectrum";
                        case 2: return "iq-window";
                        case 3: return "decode-wait";
                        case 4: return "decode-result";
                        case 5: return "event-ingest";
                        case 6: return "registry-save";
                        case 7: return "summary-sleep";
                        default: return "idle";
                    }
                };
                std::atomic_bool waitGrantWatchdogStop{false};
                std::atomic<int> waitGrantStage{0};
                std::atomic<qint64> waitGrantStageSinceMs{grantStartMs};
                const bool waitGrantTraceEnabled = []() {
                    const char* v = std::getenv("SDR_TOWN_P25_WAITGRANT_TRACE");
                    return v && *v && std::strcmp(v, "0") != 0;
                }();
                auto traceWaitGrantStage = [&](const char* stageName) {
                    if (!waitGrantTraceEnabled) return;
                    std::ofstream trace("p25_waitgrant_trace.log", std::ios::app);
                    if (!trace) return;
                    trace << QDateTime::currentMSecsSinceEpoch() << ' ' << stageName << '\n';
                };
                auto markWaitGrantStage = [&](int stage) {
                    waitGrantStage.store(stage, std::memory_order_release);
                    waitGrantStageSinceMs.store(QDateTime::currentMSecsSinceEpoch(), std::memory_order_release);
                    traceWaitGrantStage(waitGrantStageName(stage));
                };
                std::thread waitGrantWatchdog([&]() {
                    qint64 lastLogMs = 0;
                    while (!waitGrantWatchdogStop.load(std::memory_order_acquire)) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(500));
                        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
                        const qint64 sinceMs = waitGrantStageSinceMs.load(std::memory_order_acquire);
                        const qint64 ageMs = sinceMs > 0 ? nowMs - sinceMs : 0;
                        if (ageMs > 5000 && nowMs - lastLogMs > 3000) {
                            lastLogMs = nowMs;
                            const int stage = waitGrantStage.load(std::memory_order_acquire);
                            std::cout << "P25 waitgrant watchdog: control scan stalled stage="
                                      << waitGrantStageName(stage)
                                      << " age=" << ageMs << "ms; continuing watchdog diagnostics"
                                      << std::endl;
                        }
                    }
                });
                auto stopWaitGrantWatchdog = [&]() {
                    waitGrantWatchdogStop.store(true, std::memory_order_release);
                    if (waitGrantWatchdog.joinable()) waitGrantWatchdog.join();
                };
                qint64 nextSummaryMs = grantStartMs;
                size_t windows = 0;
                size_t grantCount = 0;
                size_t trustedBlocks = 0;
                size_t trustedPdus = 0;
                size_t tsbkFecTotal = 0;
                size_t tsbkCrcTotal = 0;
                size_t tsbkCrcCorrectedTotal = 0;
                size_t encryptedGrantSkips = 0;
                size_t notReadyGrantSkips = 0;
                size_t decodeTimeouts = 0;
                std::string lastRuntimeState = "unknown";
                double lastCenterHz = 0.0;
                double lastSampleRateHz = 0.0;
                size_t lastIqSamples = 0;
                uint64_t lastIqEndAbsolute = 0;
                size_t lastQueueDepth = 0;
                double lastTrustedControlTargetHz = ccHz;
                bool haveTrustedControlTargetHz = false;
                bool warnedNotLiveHardware = false;
                bool sawNidLock = false;
                bool warnedControlOffBand = false;
                qint64 lastWaitGrantRetuneRequestMs = 0;
                std::optional<P25TalkgroupEntry> selectedGrant;
                int selectedGrantScore = std::numeric_limits<int>::min();
                QString selectedGrantEventText;
                QString selectedGrantDetailText;
                size_t followAttempts = 0;
                size_t followRetrySkips = 0;
                std::vector<P25PendingVoiceGrant> pendingVoiceGrants;
                std::vector<P25RepeatedVoiceGrant> repeatedVoiceGrants;
                std::unordered_map<uint32_t, size_t> skippedEncryptedTgs;
                std::unordered_map<uint32_t, size_t> skippedNotReadyTgs;
                std::map<QString, qint64> followRetryCooldowns;
                std::map<QString, size_t> skippedCooldownKeys;
                std::map<QString, qint64> recentExplicitEncryptedPhase2Grants;
                uint64_t waitGrantLastWindowEnd = 0;
                auto talkgroups = loadP25Talkgroups();
                const qint64 grantDeadlineMs = grantStartMs + static_cast<qint64>(std::llround(seconds * 1000.0));
                auto waitGrantFollowKey = [](const P25TalkgroupEntry& tg) {
                    const qlonglong voiceHz = tg.lastVoiceFreqHz > 0.0
                        ? static_cast<qlonglong>(std::llround(tg.lastVoiceFreqHz))
                        : 0;
                    const int slot = tg.tdmaSlotKnown ? static_cast<int>(tg.tdmaSlot & 0x01u) : -1;
                    return QString("%1:%2:%3:%4")
                        .arg(tg.talkgroupId)
                        .arg(tg.lastChannel)
                        .arg(voiceHz)
                        .arg(slot);
                };

                while (QDateTime::currentMSecsSinceEpoch() < grantDeadlineMs) {
                selectedGrant.reset();
                selectedGrantScore = std::numeric_limits<int>::min();
                selectedGrantEventText.clear();
                selectedGrantDetailText.clear();
                while (!selectedGrant) {
                    traceWaitGrantStage("loop-boundary");
                    if (QDateTime::currentMSecsSinceEpoch() >= grantDeadlineMs) {
                        traceWaitGrantStage("loop-time-expired");
                        break;
                    }
                    markWaitGrantStage(1);
                    std::vector<float> p;
                    double cf = 0.0;
                    double sr = 0.0;
                    if (!mgr.getLatestSpectrum(static_cast<size_t>(devIndex), p, cf, sr) || sr <= 0.0) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(200));
                        continue;
                    }
                    lastCenterHz = cf;
                    lastSampleRateHz = sr;
                    lastRuntimeState = mgr.getRuntimeStateLabel(static_cast<size_t>(devIndex));
                    lastQueueDepth = mgr.getIQQueueDepth(static_cast<size_t>(devIndex));
                    const bool controlInPassband = std::isfinite(cf) && std::isfinite(sr) && sr > 0.0 &&
                        std::abs(ccHz - cf) <= sr * 0.45;
                    if (!controlInPassband) {
                        const qint64 retuneNowMs = QDateTime::currentMSecsSinceEpoch();
                        if (retuneNowMs - lastWaitGrantRetuneRequestMs >= 500) {
                            waitGrantTuneSeq = mgr.setCenterFreq(static_cast<size_t>(devIndex), ccHz);
                            (void)mgr.waitForCenterTuneApplied(static_cast<size_t>(devIndex), waitGrantTuneSeq, 250);
                            lastWaitGrantRetuneRequestMs = retuneNowMs;
                        }
                        if (!warnedControlOffBand || retuneNowMs >= nextSummaryMs) {
                            warnedControlOffBand = true;
                            std::cout << "P25 waitgrant retuning control channel: target="
                                      << (ccHz / 1e6)
                                      << "MHz current_cf=" << (cf / 1e6)
                                      << "MHz sr=" << (sr / 1e6)
                                      << "MHz state=\"" << lastRuntimeState
                                      << "\" tuneSeq=" << waitGrantTuneSeq << "\n";
                            nextSummaryMs = retuneNowMs + 2000;
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(120));
                        continue;
                    }
                    if (!warnedNotLiveHardware &&
                        (QDateTime::currentMSecsSinceEpoch() - grantStartMs) > 3500 &&
                        lastRuntimeState.find("live hardware") == std::string::npos) {
                        warnedNotLiveHardware = true;
                        std::cout << "P25 waitgrant hardware warning: device state=\""
                                  << lastRuntimeState
                                  << "\" after startup; grants will not be reliable until real RTL/Soapy IQ is active."
                                  << std::endl;
                    }

                    const size_t requestedSamples = static_cast<size_t>(
                        std::clamp(sr * kP25ControlDecodeWindowSeconds, 24000.0, 4194304.0));
                    markWaitGrantStage(2);
                    auto iqWindow = mgr.getRecentIQWindowWithCursor(static_cast<size_t>(devIndex), requestedSamples);
                    auto& iq = iqWindow.samples;
                    if (iq.empty() || iqWindow.endAbsolute <= waitGrantLastWindowEnd) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(80));
                        continue;
                    }
                    waitGrantLastWindowEnd = iqWindow.endAbsolute;

                    const size_t iqSamples = iq.size();
                    lastIqSamples = iqSamples;
                    lastIqEndAbsolute = iqWindow.endAbsolute;
                    const auto decodeStart = std::chrono::steady_clock::now();
                    markWaitGrantStage(3);
                    double effectiveControlTargetHz = ccHz;
                    auto result = decodeP25ControlWithOffsetProbe(
                        decoder, iq, sr, cf, ccHz, &effectiveControlTargetHz);
                    const auto decodeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - decodeStart).count();
                    markWaitGrantStage(4);
                    if (decodeMs > 2200) {
                        ++decodeTimeouts;
                        decoder = P25LiveDecoder(p25CliControlGrantDecoderConfig());
                        std::cout << "P25 waitgrant decode timeout: "
                                  << decodeMs << "ms for " << iqSamples
                                  << " IQ samples; reset CC decoder and kept scanning"
                                  << std::endl;
                        std::this_thread::sleep_for(std::chrono::milliseconds(80));
                        continue;
                    }
                    if (decodeMs > 1500) {
                        std::cout << "P25 waitgrant decode warning: "
                                  << decodeMs << "ms for " << iqSamples
                                  << " IQ samples; CLI is using overlapped control windows, but this window is still heavy."
                                  << std::endl;
                    }
                    ++windows;
                    if ((p25ControlDecodeHasTrustedPayload(result) || p25ControlDecodeHasValidatedNid(result)) &&
                        std::isfinite(effectiveControlTargetHz) && effectiveControlTargetHz > 0.0 &&
                        std::abs(effectiveControlTargetHz - ccHz) <= 25000.0) {
                        lastTrustedControlTargetHz = effectiveControlTargetHz;
                        haveTrustedControlTargetHz = true;
                        gP25LastTrustedControlFreqHz.store(ccHz, std::memory_order_release);
                        gP25LastTrustedControlOffsetHz.store(effectiveControlTargetHz - ccHz, std::memory_order_release);
                        gP25LastTrustedControlOffsetMs.store(
                            static_cast<long long>(QDateTime::currentMSecsSinceEpoch()),
                            std::memory_order_release);
                    }
                    p25SeedAnalyzerNacFromDecode(analyzer, result);
                    sawNidLock = sawNidLock || p25DecodeResultHasNidLock(result);

                    markWaitGrantStage(5);
                    bool changed = false;
                    size_t windowTrustedBlocks = 0;
                    size_t windowTsbkFec = 0;
                    size_t windowTsbkCrc = 0;
                    size_t windowTsbkCrcCorrected = 0;
                    int windowBestTsbkCorrections = std::numeric_limits<int>::max();
                    bool windowHadGrant = false;
                    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

                    auto maybeSelectResolvedGrant = [&](const P25ControlEvent& grant,
                                                         const char* source,
                                                         std::optional<int> correctedDibitErrors) {
                        if (!p25ControlEventIsResolvedVoiceGrant(grant)) return;
                        if (targetTalkgroupId != 0 && grant.talkgroupId != targetTalkgroupId) return;
                        P25TalkgroupEntry candidate = p25TalkgroupEntryFromCurrentGrant(ccHz, grant, nowMs);
                        if (candidate.lastVoiceFreqHz > 0.0) {
                            p25AugmentTalkgroupFromKnownSite(candidate, talkgroups, ccHz);
                            p25RefreshFollowGrantFromRegistry(candidate, talkgroups, nowMs);
                            const QString retryKey = waitGrantFollowKey(candidate);
                            auto cooldownIt = followRetryCooldowns.find(retryKey);
                            if (cooldownIt != followRetryCooldowns.end()) {
                                if (cooldownIt->second > nowMs) {
                                    ++followRetrySkips;
                                    if (++skippedCooldownKeys[retryKey] == 1) {
                                        std::cout << "    grant temporarily skipped after failed Phase 2 traffic acquisition; retry in "
                                                  << ((cooldownIt->second - nowMs) / 1000.0)
                                                  << "s: "
                                                  << p25FollowDetailLogText(candidate).toStdString() << "\n";
                                    }
                                    return;
                                }
                                followRetryCooldowns.erase(cooldownIt);
                                skippedCooldownKeys.erase(retryKey);
                            }
                            if (p25TalkgroupIsPhase2(candidate)) {
                                p25PruneRecentExplicitEncryptedPhase2Grants(recentExplicitEncryptedPhase2Grants, nowMs);
                                p25RememberExplicitEncryptedPhase2Grant(recentExplicitEncryptedPhase2Grants, grant, &candidate, nowMs);
                            }
                            bool probingUnknownPhase2EncryptedHistory = false;
                            if (followGrant) {
                                const bool followReady = p25PrepareTalkgroupForFollowGrant(
                                    candidate,
                                    grant,
                                    probingUnknownPhase2EncryptedHistory);
                                if (candidate.encryptionKnown && candidate.encrypted) {
                                    ++encryptedGrantSkips;
                                    if (++skippedEncryptedTgs[candidate.talkgroupId] == 1) {
                                        std::cout << "    encrypted grant skipped during follow test: "
                                                  << p25FollowDetailLogText(candidate).toStdString() << "\n";
                                    }
                                    return;
                                }
                                if (!grant.encryptionKnown && p25TalkgroupIsPhase2(candidate)) {
                                    const qint64 encryptedHoldAgeMs = p25RecentExplicitEncryptedPhase2GrantAgeMs(
                                        recentExplicitEncryptedPhase2Grants, grant, candidate, nowMs);
                                    if (encryptedHoldAgeMs >= 0) {
                                        ++encryptedGrantSkips;
                                        if (++skippedEncryptedTgs[candidate.talkgroupId] == 1) {
                                            std::cout << "    unknown Phase 2 grant skipped during follow test: explicit encrypted grant for same TG/channel/frequency was seen "
                                                      << encryptedHoldAgeMs << "ms earlier: "
                                                      << p25FollowDetailLogText(candidate).toStdString() << "\n";
                                        }
                                        return;
                                    }
                                }
                                if (!followReady || candidate.lastVoiceFreqHz <= 0.0) {
                                    ++notReadyGrantSkips;
                                    if (++skippedNotReadyTgs[candidate.talkgroupId] == 1) {
                                        std::cout << "    grant not ready for follow test yet: "
                                                  << p25FollowDetailLogText(candidate).toStdString() << "\n";
                                    }
                                    return;
                                }
                                if (probingUnknownPhase2EncryptedHistory) {
                                    std::cout << "    probing Phase 2 TG " << candidate.talkgroupId
                                              << " despite previous encrypted history; grant/update has no service options, "
                                                 "so audio remains gated until MAC/ESS proves clear\n";
                                }
                            }
                            const int candidateScore = p25TalkgroupGrantProvesSpeakerClear(candidate)
                                ? 300
                                : (probingUnknownPhase2EncryptedHistory
                                    ? 100
                                    : (!candidate.encryptionKnown ? 200 : 150));
                            if (selectedGrant && candidateScore <= selectedGrantScore) {
                                return;
                            }
                            selectedGrantEventText = QString("%1: %2")
                                .arg(source != nullptr && *source ? source : "grant")
                                .arg(p25EventLogText(grant));
                            selectedGrantDetailText = p25GrantDetailLogText(grant);
                            if (correctedDibitErrors.has_value()) {
                                selectedGrantDetailText += QString(" CORRECTED_DIBITS=%1").arg(*correctedDibitErrors);
                            }
                            selectedGrantScore = candidateScore;
                            selectedGrant = candidate;
                        }
                    };

                    auto consumeEvent = [&](const P25ControlEvent& ev, const char* source, std::optional<int> correctedDibitErrors) {
                        std::cout << "  " << source << ": " << p25EventLogText(ev).toStdString() << "\n";
                        if (p25ControlEventIsVoiceGrant(ev)) {
                            ++grantCount;
                            windowHadGrant = true;
                            std::cout << "    " << p25GrantDetailLogText(ev).toStdString() << "\n";
                            if (!p25ControlEventHasResolvedVoiceFrequency(ev)) {
                                std::cout << "    grant not followable yet: waiting for identifier table/frequency resolution\n";
                            }
                        }
                        const P25RepeatedVoiceGrantDecision repeatDecision = correctedDibitErrors.has_value()
                            ? p25RememberRepeatedHighCorrectionResolvedVoiceGrant(
                                repeatedVoiceGrants, ccHz, ev, *correctedDibitErrors, nowMs)
                            : P25RepeatedVoiceGrantDecision();
                        const bool eventRegistryEligible = !correctedDibitErrors.has_value() ||
                            p25TsbkEventRegistryEligible(*correctedDibitErrors, ev) ||
                            repeatDecision.promoted;
                        if (!eventRegistryEligible) {
                            if (correctedDibitErrors.has_value() && p25ControlEventIsVoiceGrant(ev)) {
                                if (repeatDecision.considered) {
                                    std::cout << "    grant not followable yet: high-correction resolved Phase 2 grant needs repeat confirmation; corrected dibits "
                                              << *correctedDibitErrors
                                              << " hits=" << repeatDecision.hitCount
                                              << "/" << kP25RepeatedVoiceGrantMinHits
                                              << " repeat threshold "
                                              << kP25RepeatedVoiceGrantMaxCorrectedDibits << "\n";
                                } else if (*correctedDibitErrors > kP25VoiceGrantMaxCorrectedDibits) {
                                    std::cout << "    grant not followable yet: corrected dibits "
                                              << *correctedDibitErrors
                                              << " exceed voice grant threshold "
                                              << kP25VoiceGrantMaxCorrectedDibits << "\n";
                                } else {
                                    std::cout << "    grant not followable yet: unresolved voice grant is above the persistent registry threshold "
                                              << kP25RegistryMaxCorrectedDibits
                                              << " corrected dibits; it can only be queued pending identifier resolution\n";
                                }
                                if (p25TsbkPendingVoiceGrantEligible(*correctedDibitErrors, ev) &&
                                    p25RememberPendingVoiceGrant(pendingVoiceGrants, ev, *correctedDibitErrors, nowMs)) {
                                    const uint8_t id = static_cast<uint8_t>((ev.channel >> 12) & 0x0f);
                                    std::cout << "    grant pending: near-threshold unresolved grant queued until identifier table ID "
                                              << static_cast<int>(id)
                                              << " resolves (pending threshold "
                                              << kP25PendingVoiceGrantMaxCorrectedDibits << ")\n";
                                }
                            }
                            if (correctedDibitErrors.has_value() &&
                                p25TsbkSessionIdentifierEligible(*correctedDibitErrors, ev)) {
                                std::cout << "    kept high-correction identifier ID "
                                          << static_cast<int>(ev.identifier)
                                          << " in the current waitgrant session for pending grant resolution; corrected="
                                          << *correctedDibitErrors
                                          << " session threshold=" << kP25SessionIdentifierMaxCorrectedDibits << "\n";
                                for (const auto& resolved : p25ResolvePendingVoiceGrants(pendingVoiceGrants, analyzer, nowMs)) {
                                    windowHadGrant = true;
                                    std::cout << "  pending-resolved after session identifier ID "
                                              << static_cast<int>(ev.identifier) << ": "
                                              << p25EventLogText(resolved).toStdString() << "\n";
                                    std::cout << "    " << p25GrantDetailLogText(resolved).toStdString() << "\n";
                                    changed = mergeP25TalkgroupEvent(talkgroups, ccHz, resolved, nowMs) || changed;
                                    maybeSelectResolvedGrant(resolved, "pending-resolved-session-id", std::nullopt);
                                }
                            }
                            return;
                        }
                        if (p25ControlEventIsVoiceGrant(ev) &&
                            !p25ControlEventIsResolvedVoiceGrant(ev)) {
                            const int corrections = correctedDibitErrors.value_or(0);
                            if (p25RememberPendingVoiceGrant(pendingVoiceGrants, ev, corrections, nowMs)) {
                                const uint8_t id = static_cast<uint8_t>((ev.channel >> 12) & 0x0f);
                                std::cout << "    grant pending: queued until identifier table ID "
                                          << static_cast<int>(id) << " resolves\n";
                            }
                        }
                        if (repeatDecision.promoted) {
                            std::cout << "    accepted repeat-confirmed high-correction Phase 2 grant: corrected="
                                      << *correctedDibitErrors
                                      << " best=" << repeatDecision.bestCorrectedDibitErrors
                                      << " hits=" << repeatDecision.hitCount
                                      << " repeat threshold=" << kP25RepeatedVoiceGrantMaxCorrectedDibits << "\n";
                        } else if (correctedDibitErrors.has_value() &&
                            *correctedDibitErrors > kP25RegistryMaxCorrectedDibits &&
                            p25ControlEventIsResolvedVoiceGrant(ev)) {
                            std::cout << "    accepted high-correction resolved grant: corrected="
                                      << *correctedDibitErrors
                                      << " threshold=" << kP25VoiceGrantMaxCorrectedDibits << "\n";
                        }
                        changed = mergeP25TalkgroupEvent(talkgroups, ccHz, ev, nowMs) || changed;
                        maybeSelectResolvedGrant(ev, source, correctedDibitErrors);
                        if (ev.type == P25ControlEventType::IdentifierUpdate &&
                            p25ChannelIdentifierUsable(p25IdentifierFromEvent(ev))) {
                            if (upsertP25ChannelIdentifier(ccHz, ev, nowMs)) {
                                std::cout << "    cached identifier table ID "
                                          << static_cast<int>(ev.identifier)
                                          << " for future follow attempts\n";
                            }
                            for (const auto& resolved : p25ResolvePendingVoiceGrants(pendingVoiceGrants, analyzer, nowMs)) {
                                windowHadGrant = true;
                                std::cout << "  pending-resolved after identifier ID "
                                          << static_cast<int>(ev.identifier) << ": "
                                          << p25EventLogText(resolved).toStdString() << "\n";
                                std::cout << "    " << p25GrantDetailLogText(resolved).toStdString() << "\n";
                                changed = mergeP25TalkgroupEvent(talkgroups, ccHz, resolved, nowMs) || changed;
                                maybeSelectResolvedGrant(resolved, "pending-resolved-id", std::nullopt);
                            }
                        }
                    };

                    for (const auto& block : result.rawTsbkBlocks) {
                        if (block.fecDecoded) {
                            ++tsbkFecTotal;
                            ++windowTsbkFec;
                        }
                        if (block.crcValid) {
                            ++tsbkCrcTotal;
                            ++windowTsbkCrc;
                        }
                        if (block.crcCorrected) {
                            ++tsbkCrcCorrectedTotal;
                            ++windowTsbkCrcCorrected;
                        }
                        if (block.correctedDibitErrors >= 0) {
                            windowBestTsbkCorrections = std::min(windowBestTsbkCorrections, block.correctedDibitErrors);
                        }
                        if (!block.fecDecoded || !block.crcValid) continue;
                        ++trustedBlocks;
                        ++windowTrustedBlocks;
                        const auto events = analyzer.ingestTsbk(block.bytes);
                        for (const auto& ev : events) consumeEvent(ev, "TSBK", block.correctedDibitErrors);
                    }
                    size_t windowTrustedPdus = 0;
                    for (const auto& pdu : result.phase1Pdus) {
                        if (!pdu.headerFecDecoded || !pdu.headerCrcValid) continue;
                        ++trustedPdus;
                        ++windowTrustedPdus;
                        std::vector<std::vector<uint8_t>> dataBlocks;
                        dataBlocks.reserve(pdu.dataBlocks.size());
                        for (const auto& block : pdu.dataBlocks) dataBlocks.push_back(block.bytes);
                        const auto events = analyzer.ingestPhase1Pdu(pdu.format, pdu.vendor, pdu.opcode, pdu.headerBytes, dataBlocks, true);
                        for (const auto& ev : events) consumeEvent(ev, "P1PDU", std::nullopt);
                    }
                    for (const auto& pdu : result.phase2MacPdus) {
                        if (!pdu.crcValid) continue;
                        const auto events = analyzer.ingestPhase2MacPdu(
                            pdu.opcode, pdu.offset, pdu.bytes, true, pdu.macStructureMaxBits);
                        for (const auto& ev : events) consumeEvent(ev, "P2MAC", std::nullopt);
                    }
                    if (changed) {
                        markWaitGrantStage(6);
                        saveP25Talkgroups(talkgroups);
                        std::cout << "  Talkgroup registry updated.\n";
                    }

                    markWaitGrantStage(7);
                    const qint64 loopNowMs = QDateTime::currentMSecsSinceEpoch();
                    if (windowHadGrant || selectedGrant || loopNowMs >= nextSummaryMs) {
                        std::cout << "P25 waitgrant t="
                                  << (static_cast<double>(loopNowMs - grantStartMs) / 1000.0)
                                  << "s path=" << (result.stats.demodPath.empty() ? "unknown" : result.stats.demodPath)
                                  << " state=\"" << lastRuntimeState << "\""
                                  << " cf=" << (lastCenterHz / 1e6)
                                  << "MHz sr=" << (lastSampleRateHz / 1e6)
                                  << "MHz iq=" << lastIqSamples
                                  << " cqpskCand=" << result.stats.cqpskCandidatesEvaluated
                                  << " c4fmSkipCqpsk=" << (result.stats.c4fmHardLockSkippedCqpsk ? "yes" : "no")
                                  << " eff=" << (effectiveControlTargetHz / 1e6)
                                  << "MHz effOff=" << (effectiveControlTargetHz - ccHz)
                                  << "Hz"
                                  << " absEnd=" << static_cast<unsigned long long>(lastIqEndAbsolute)
                                  << " q=" << lastQueueDepth
                                  << " syncs=" << result.syncs.size()
                                  << " nidLock=" << (p25DecodeResultHasNidLock(result) ? "yes" : "no")
                                  << " softQ=" << result.stats.softDecisionQuality
                                  << " bestSyncErr=" << result.stats.bestFrameSyncBitErrors
                                  << " bestNidDist=" << result.stats.bestNidBchDistance
                                  << " tsbk=" << result.rawTsbkBlocks.size()
                                  << " tsbkFec=" << windowTsbkFec
                                  << "/" << tsbkFecTotal
                                  << " tsbkCrc=" << windowTsbkCrc
                                  << "/" << tsbkCrcTotal
                                  << " tsbkCrcCorr=" << windowTsbkCrcCorrected
                                  << "/" << tsbkCrcCorrectedTotal
                                  << " tsbkBestCorr=" << (windowBestTsbkCorrections == std::numeric_limits<int>::max() ? -1 : windowBestTsbkCorrections)
                                  << " trusted=" << windowTrustedBlocks
                                  << "/" << trustedBlocks
                                  << " p1pdu=" << result.stats.phase1PduCrcValid
                                  << "/" << result.stats.phase1PduHeaders
                                  << " trustedP1Pdu=" << windowTrustedPdus
                                  << "/" << trustedPdus
                                  << " p2bursts=" << result.stats.phase2Bursts
                                  << " p2sf=" << result.stats.phase2SuperframeBursts
                                  << " p2mask=" << result.stats.phase2MaskedBursts
                                  << " p2mac=" << result.stats.phase2MacCrcValid << "/" << result.stats.phase2MacPdus
                                  << " " << p25Phase2AcchStatsText(result.stats).toStdString()
                                  << " p2ess=" << (result.stats.phase2EssKnown ? (result.stats.phase2EssEncrypted ? "enc" : "clear") : "unknown")
                                  << std::endl;
                        for (const auto& warning : result.warnings) {
                            std::cout << "  note: " << warning << "\n";
                        }
                        nextSummaryMs = loopNowMs + 2000;
                    }
                    traceWaitGrantStage("sleep-enter");
                    std::this_thread::sleep_for(std::chrono::milliseconds(selectedGrant ? 0 : kP25ControlDecodeCadenceMs));
                    traceWaitGrantStage("sleep-leave");
                }
                stopWaitGrantWatchdog();

                if (!selectedGrant) {
                    std::cout << "P25 waitgrant finished: no followable voice grant in " << seconds
                              << "s windows=" << windows
                              << " nidLock=" << (sawNidLock ? "yes" : "no")
                              << " state=\"" << lastRuntimeState << "\""
                              << " cf=" << (lastCenterHz / 1e6)
                              << "MHz sr=" << (lastSampleRateHz / 1e6)
                              << "MHz iq=" << lastIqSamples
                              << " absEnd=" << static_cast<unsigned long long>(lastIqEndAbsolute)
                              << " q=" << lastQueueDepth
                              << " tsbkFec=" << tsbkFecTotal
                              << " tsbkCrc=" << tsbkCrcTotal
                              << " tsbkCrcCorr=" << tsbkCrcCorrectedTotal
                              << " trustedBlocks=" << trustedBlocks
                              << " trustedP1Pdu=" << trustedPdus
                              << " grants=" << grantCount
                              << " followAttempts=" << followAttempts
                              << " retrySkipped=" << followRetrySkips
                              << " encryptedSkipped=" << encryptedGrantSkips
                              << " notReadySkipped=" << notReadyGrantSkips
                              << " decodeTimeouts=" << decodeTimeouts << std::endl;
                    break;
                }
                if (!selectedGrant) break;

                auto tg = *selectedGrant;
                {
                    auto registrySnapshot = talkgroups;
                    p25AugmentTalkgroupFromKnownSite(tg, registrySnapshot, ccHz);
                }
                const bool selectedGrantPhase2 = p25TalkgroupIsPhase2(tg);
                std::cout << "P25 waitgrant grant selected: " << p25FollowDetailLogText(tg).toStdString() << std::endl;
                if (!followGrant) break;
                ++followAttempts;
                if (tg.encryptionKnown && tg.encrypted) {
                    std::cout << "P25 waitgrant follow skipped: TG " << tg.talkgroupId << " is known encrypted." << std::endl;
                    continue;
                }
                if (!p25TalkgroupCanTuneForFollow(tg) || tg.lastVoiceFreqHz <= 0.0) {
                    std::cout << "P25 waitgrant follow skipped: TG " << tg.talkgroupId
                              << " does not yet have enough clear/frequency metadata." << std::endl;
                    continue;
                }
                double carriedControlOffsetHz = 0.0;
                bool carryPhase2TrafficOffset =
                    selectedGrantPhase2 &&
                    p25TrustedControlOffsetForPhase2Traffic(
                        ccHz, QDateTime::currentMSecsSinceEpoch(), &carriedControlOffsetHz);
                if (!carryPhase2TrafficOffset && selectedGrantPhase2 && haveTrustedControlTargetHz) {
                    const double localOffsetHz = lastTrustedControlTargetHz - ccHz;
                    const double absLocalOffsetHz = std::abs(localOffsetHz);
                    if (std::isfinite(localOffsetHz) &&
                        absLocalOffsetHz >= kP25Phase2ControlCarryOffsetMinHz &&
                        absLocalOffsetHz <= kP25Phase2ControlCarryOffsetMaxHz) {
                        carriedControlOffsetHz = localOffsetHz;
                        carryPhase2TrafficOffset = true;
                    }
                }

                QString followAudioWavPath;
                QString followOppositeWavPath;
                if (recordFollowAudio) {
                    followAudioWavPath = makeCliP25WavCapturePath(ccHz, tg.talkgroupId, tg.lastVoiceFreqHz);
                    const int companionSlot = tg.tdmaSlotKnown
                        ? static_cast<int>((tg.tdmaSlot ^ 0x01u) & 0x01u)
                        : 0;
                    followOppositeWavPath = makeCliP25OppositeWavCapturePath(
                        ccHz, tg.lastVoiceFreqHz, companionSlot);
                    QString wavError;
                    if (startCliP25WavCapture(followAudioWavPath, 48000.0, &wavError)) {
                        std::cout << "P25 waitgrant WAV capture armed: "
                                  << followAudioWavPath.toStdString() << std::endl;
                        QString oppErr;
                        if (startCliP25OppositeWavCapture(followOppositeWavPath, 48000.0, &oppErr)) {
                            std::cout << "P25 waitgrant companion WAV capture armed: "
                                      << followOppositeWavPath.toStdString() << std::endl;
                        } else {
                            std::cout << "P25 waitgrant companion WAV capture failed to arm: "
                                      << oppErr.toStdString() << std::endl;
                        }
                    } else {
                        std::cout << "P25 waitgrant WAV capture failed to arm: "
                                  << wavError.toStdString() << std::endl;
                        recordFollowAudio = false;
                    }
                }

                {
                    std::lock_guard<std::mutex> lk(cliRxMutex);
                    ensureCliRxLocked(0);
                    Receiver& rx = *cliReceivers[0];
                    std::lock_guard<std::mutex> rxLock(rx.stateMutex);
                    const bool phase2Voice = selectedGrantPhase2;
                    const uint64_t trafficGeneration = phase2Voice
                        ? (cliP25TrafficSourceGeneration.fetch_add(1, std::memory_order_acq_rel) + 1)
                        : 0;
                    rx.deviceIndex = static_cast<size_t>(devIndex);
                    rx.resetDemodState();
                    rx.freqHz = tg.lastVoiceFreqHz;
                    rx.mode = DemodMode::NFM;
                    rx.channelBwHz = 12500.0;
                    rx.lpfHz = 3000.0;
                    rx.audioLpfEnabled = false;
                    rx.squelchDb = -105.0;
                    p25ClearPhase2PendingAudio(rx);
                    rx.resetP25VoiceState();
                    clearP25SessionScopedState(rx);
                    rx.p25VoiceResetPending = false;
                    rx.p25VoiceDecodeEnabled = true;
                    rx.p25VoiceClearKnown = p25TalkgroupGrantProvesSpeakerClear(tg);
                    rx.p25VoiceEncrypted = p25TalkgroupGrantProvesSpeakerEncrypted(tg);
                    rx.p25VoiceTalkgroupId = tg.talkgroupId;
                    rx.p25VoiceSourceId = tg.lastSourceId;
                    const qint64 armNowMs = QDateTime::currentMSecsSinceEpoch();
                    p25Phase2BeginNewPtt(rx, armNowMs);
                    rx.p25VoicePhase2 = phase2Voice;
                    rx.p25VoiceTdmaSlotKnown = tg.tdmaSlotKnown;
                    rx.p25VoiceTdmaSlot = tg.tdmaSlot;
                    rx.p25VoiceSlotProbePending = false;
                    rx.p25VoiceSlotProbeRequested = 0;
                    rx.p25VoiceMaskParamsKnown = tg.p25MaskParamsKnown;
                    rx.p25VoiceNac = tg.nac;
                    rx.p25VoiceWacn = tg.wacn;
                    rx.p25VoiceSystemId = tg.systemId;
                    rx.p25VoiceSettleUntilMs = armNowMs + p25PostArmSettleMs(rx.p25VoicePhase2);
                    rx.p25VoiceDiscardWindows = p25PostArmDiscardWindows(rx.p25VoicePhase2);
                    rx.p25ControlChannelMute = false;
                    rx.p25IndependentTrafficSource = phase2Voice;
                    rx.p25TrafficRetunesPrimary = phase2Voice;
                    rx.p25TrafficGeneration = trafficGeneration;
                    rx.p25TrafficControlFreqHz = ccHz;
                    rx.p25TrafficSourceCenterFreqHz = phase2Voice
                        ? p25Phase2LowIfTrafficCenterHz(tg.lastVoiceFreqHz, lastSampleRateHz)
                        : tg.lastVoiceFreqHz;
                    rx.p25TrafficVoiceFreqHz = tg.lastVoiceFreqHz;
                    rx.p25TrafficSlot = tg.tdmaSlotKnown ? static_cast<uint8_t>(tg.tdmaSlot & 0x01u) : 0;
                    rx.p25TrafficLastGrantMs = armNowMs;
                    rx.p25Phase2TrafficTargetOffsetKnown = carryPhase2TrafficOffset;
                    rx.p25Phase2TrafficTargetOffsetHz = carryPhase2TrafficOffset ? carriedControlOffsetHz : 0.0;
                    rx.p25Phase2TrafficTargetOffsetTrust = carryPhase2TrafficOffset ? 1 : 0;
                    rx.p25Phase2TrafficTargetOffsetMisses = 0;
                    rx.p25AfcFrozen = false;
                    rx.p25FrozenAfcOffsetHz = carryPhase2TrafficOffset ? carriedControlOffsetHz : 0.0;
                    rx.p25Phase2AllowLateEntryAudioProbe =
                        rx.p25VoicePhase2 && kP25Phase2AllowUnknownGrantFieldAudioProbe;
                    rx.p25VoiceLiveDecoder = P25LiveDecoder(p25VoiceDecoderConfigForReceiver(rx));
                    if (rx.p25VoicePhase2 && rx.p25VoiceMaskParamsKnown) {
                        rx.p25VoiceLiveDecoder.setPhase2MaskParameters(rx.p25VoiceNac, rx.p25VoiceWacn, rx.p25VoiceSystemId);
                    } else {
                        rx.p25VoiceLiveDecoder.clearPhase2MaskParameters();
                    }
                    clearP25VoiceDiagnostics(rx);
                    rx.active = true;
                }
                if (carryPhase2TrafficOffset) {
                    std::cout << "P25 waitgrant AFC carry: seeded Phase 2 traffic target offset "
                              << carriedControlOffsetHz
                              << " Hz from trusted control decode eff="
                              << (lastTrustedControlTargetHz / 1e6)
                              << " MHz\n";
                }
                if (cliAudio) cliAudio->clearBuffers();
                double cliVoiceSampleRateHz = 0.0;
                {
                    const auto devices = mgr.getDevices();
                    if (devIndex >= 0 && static_cast<size_t>(devIndex) < devices.size()) {
                        cliVoiceSampleRateHz = devices[static_cast<size_t>(devIndex)].sampleRate;
                    }
                }
                const double cliTrafficCenterHz = selectedGrantPhase2
                    ? p25Phase2LowIfTrafficCenterHz(tg.lastVoiceFreqHz, cliVoiceSampleRateHz)
                    : tg.lastVoiceFreqHz;
                uint64_t voiceTuneSeq = 0;
                if (mgr.isStreaming(static_cast<size_t>(devIndex))) {
                    voiceTuneSeq = mgr.setCenterFreq(static_cast<size_t>(devIndex), cliTrafficCenterHz);
                }
                if (selectedGrantPhase2 && voiceTuneSeq != 0) {
                    const bool tuneApplied = mgr.waitForCenterTuneApplied(static_cast<size_t>(devIndex), voiceTuneSeq, 650);
                    std::cout << "P25 waitgrant voice retune "
                              << (tuneApplied ? "applied" : "pending")
                              << ": seq=" << voiceTuneSeq
                              << " appliedSeq=" << mgr.getCenterTuneAppliedSeq(static_cast<size_t>(devIndex))
                              << " rfCenter=" << (cliTrafficCenterHz / 1e6) << " MHz"
                              << " voice=" << (tg.lastVoiceFreqHz / 1e6) << " MHz"
                              << " offset=" << ((tg.lastVoiceFreqHz - cliTrafficCenterHz) / 1000.0) << " kHz"
                              << std::endl;
                }
                {
                    std::lock_guard<std::mutex> lk(cliRxMutex);
                    ensureCliRxLocked(0);
                    Receiver& rx = *cliReceivers[0];
                    std::lock_guard<std::mutex> rxLock(rx.stateMutex);
                    if (selectedGrantPhase2) {
                        double cursorSampleRateHz = 0.0;
                        const auto devices = mgr.getDevices();
                        if (devIndex >= 0 && static_cast<size_t>(devIndex) < devices.size()) {
                            cursorSampleRateHz = devices[static_cast<size_t>(devIndex)].sampleRate;
                        }
                        mgr.setReceiverCursorBeforeLiveEdge(static_cast<size_t>(devIndex), rx,
                            p25Phase2TrafficPreRollSamples(cursorSampleRateHz, true));
                    } else {
                        mgr.setReceiverCursorToLiveEdge(static_cast<size_t>(devIndex), rx);
                    }
                }
                std::cout << "P25 waitgrant following TG " << tg.talkgroupId
                          << " voice=" << (tg.lastVoiceFreqHz / 1e6) << " MHz"
                          << " proto=" << (p25TalkgroupIsPhase2(tg) ? "P2 TDMA" : "P1 FDMA")
                          << (tg.tdmaSlotKnown ? (" slot=" + std::to_string(tg.tdmaSlot & 0x01u)) : std::string())
                          << "; watching voice/audio gates for 45s" << std::endl;

                std::string lastVoiceSig;
                P25VoiceDiagSnapshot finalVoiceDiag;
                QString followCaptureReason = QStringLiteral("deadline");
                int cliWrongSlotChecks = 0;
                int cliSlotProbeFlips = 0;
                qint64 cliLastSlotProbeFlipMs = 0;
                const qint64 voiceStartMs = QDateTime::currentMSecsSinceEpoch();
                uint32_t cliSlotProbeTg = tg.talkgroupId;
                double cliSlotProbeVoiceHz = tg.lastVoiceFreqHz;
                qint64 cliSlotProbeArmMs = voiceStartMs;
                qint64 cliFollowLastActiveMs = voiceStartMs;
                const qint64 maxVoiceDeadlineMs = voiceStartMs + 90000;
                qint64 voiceDeadlineMs = voiceStartMs + 45000;
                qint64 lastCliFollowBusyLogMs = 0;
                qint64 lastCliFollowHeartbeatMs = 0;
                qint64 cliAudioOpenedMs = 0;
                qint64 cliLastSpeakerOutputSeenMs = gP25AudioLastSpeakerOutputMs.load(std::memory_order_relaxed);
                qint64 cliLastSelectedSlotAudioMs = 0;
                DeviceManager::RecentIQWindow cliAudioOpenIqWindow;
                QDateTime cliAudioOpenIqWindowUtc;
                DeviceManager::RecentIQWindow cliBestEvidenceIqWindow;
                QDateTime cliBestEvidenceIqWindowUtc;
                QString cliBestEvidenceIqReason;
                size_t cliBestEvidenceIqScore = 0;
                auto captureCliFollowIqSnapshot = [&](const QString& reason, size_t score) {
                    if (!recordFollowCapture || score <= cliBestEvidenceIqScore) return;
                    double snapshotSampleRateHz = 0.0;
                    {
                        const auto devices = mgr.getDevices();
                        if (devIndex >= 0 && static_cast<size_t>(devIndex) < devices.size()) {
                            snapshotSampleRateHz = devices[static_cast<size_t>(devIndex)].sampleRate;
                        }
                    }
                    double snapshotCfHz = 0.0;
                    double snapshotSpectrumSrHz = 0.0;
                    std::vector<float> snapshotSpectrum;
                    if (mgr.getLatestSpectrum(static_cast<size_t>(devIndex),
                                              snapshotSpectrum,
                                              snapshotCfHz,
                                              snapshotSpectrumSrHz) &&
                        snapshotSpectrumSrHz > 0.0 &&
                        std::isfinite(snapshotSpectrumSrHz)) {
                        snapshotSampleRateHz = snapshotSpectrumSrHz;
                    }
                    if (snapshotSampleRateHz <= 0.0 || !std::isfinite(snapshotSampleRateHz)) return;

                    const double snapshotSeconds = std::clamp(recordFollowSeconds, 2.0, 8.0);
                    const size_t snapshotSamples = static_cast<size_t>(std::clamp(
                        snapshotSampleRateHz * snapshotSeconds,
                        16384.0,
                        48000000.0));
                    auto window = mgr.getRecentIQWindowWithCursor(static_cast<size_t>(devIndex), snapshotSamples);
                    if (window.samples.empty()) return;

                    cliBestEvidenceIqWindow = std::move(window);
                    cliBestEvidenceIqWindowUtc = QDateTime::currentDateTimeUtc();
                    cliBestEvidenceIqReason = reason;
                    cliBestEvidenceIqScore = score;
                    const double actualSeconds =
                        static_cast<double>(cliBestEvidenceIqWindow.samples.size()) / snapshotSampleRateHz;
                    std::cout << "  P25 follow IQ snapshot captured: reason="
                              << reason.toStdString()
                              << " score=" << cliBestEvidenceIqScore
                              << " samples=" << cliBestEvidenceIqWindow.samples.size()
                              << " seconds=" << actualSeconds
                              << " absStart=" << static_cast<unsigned long long>(cliBestEvidenceIqWindow.startAbsolute)
                              << " absEnd=" << static_cast<unsigned long long>(cliBestEvidenceIqWindow.endAbsolute)
                              << std::endl;
                };
                while (QDateTime::currentMSecsSinceEpoch() < voiceDeadlineMs) {
                    P25VoiceDiagSnapshot diag;
                    P25TrafficProcessorStatusSnapshot trafficStatus;
                    P25CallSecurityLatch cliCallSecurityLatch = P25CallSecurityLatch::Unknown;
                    uint64_t cliCurrentCallSessionId = 0;
                    uint64_t cliEssCallSessionId = 0;
                    bool haveVoiceDiag = false;
                    {
                        std::unique_lock<std::mutex> lk(cliRxMutex, std::try_to_lock);
                        if (lk.owns_lock()) {
                            ensureCliRxLocked(0);
                            Receiver& rx = *cliReceivers[0];
                            std::unique_lock<std::mutex> rxLock(rx.stateMutex, std::try_to_lock);
                            if (rxLock.owns_lock()) {
                                diag = rx.p25VoiceDiagnostics;
                                trafficStatus = snapshotP25TrafficProcessorStatus(rx);
                                cliCallSecurityLatch = rx.p25SessionState.callSecurityLatch;
                                cliCurrentCallSessionId = rx.p25CurrentCallSessionId;
                                if (trafficStatus.present) {
                                    cliEssCallSessionId = trafficStatus.diag.sessionId;
                                }
                                haveVoiceDiag = true;
                            }
                        }
                    }
                    if (!haveVoiceDiag) {
                        const qint64 busyNowMs = QDateTime::currentMSecsSinceEpoch();
                        if (busyNowMs - lastCliFollowBusyLogMs > 2000) {
                            lastCliFollowBusyLogMs = busyNowMs;
                            std::cout << "  voice stage=pending receiver-state-busy; DSP owns P25 voice state, CLI did not block"
                                      << std::endl;
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        continue;
                    }
                    finalVoiceDiag = diag;
                    const auto code = static_cast<P25VoiceDiagCode>(diag.diag);
                    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
                    const long long p2bursts = trafficStatus.present
                        ? std::max<long long>(diag.phase2Bursts, trafficStatus.diag.p2bursts)
                        : diag.phase2Bursts;
                    const long long p2vcw = trafficStatus.present
                        ? trafficStatus.diag.p2vcw
                        : diag.phase2VoiceCodewords;
                    const long long p2sf = trafficStatus.present
                        ? std::max<long long>(diag.phase2SuperframeBursts, trafficStatus.diag.p2sf)
                        : diag.phase2SuperframeBursts;
                    const long long p2mask = trafficStatus.present
                        ? std::max<long long>(diag.phase2MaskedBursts, trafficStatus.diag.p2mask)
                        : diag.phase2MaskedBursts;
                    const long long p2mac = trafficStatus.present
                        ? std::max<long long>(diag.phase2MacPdus, trafficStatus.diag.p2macPdus)
                        : diag.phase2MacPdus;
                    const long long p2crc = trafficStatus.present
                        ? std::max<long long>(diag.phase2MacCrcValid, trafficStatus.diag.p2macCrcValid)
                        : diag.phase2MacCrcValid;
                    const bool p2EssKnown = diag.phase2EssKnown ||
                        (trafficStatus.present && trafficStatus.diag.essTrusted);
                    const bool p2EssEncrypted = diag.phase2EssEncrypted ||
                        (trafficStatus.present && trafficStatus.diag.encrypted);
                    const char* p2ess = p2EssKnown ? (p2EssEncrypted ? "enc" : "clear") : "unknown";
                    const qint64 lastSpeakerOutputMs =
                        gP25AudioLastSpeakerOutputMs.load(std::memory_order_relaxed);
                    const bool speakerAudioOpened =
                        lastSpeakerOutputMs > cliLastSpeakerOutputSeenMs &&
                        lastSpeakerOutputMs >= voiceStartMs &&
                        nowMs >= lastSpeakerOutputMs &&
                        nowMs - lastSpeakerOutputMs <= 2500;
                    std::ostringstream sig;
                    sig << diag.diag << ":" << diag.syncs << ":" << diag.nids << ":" << diag.decodedFrames
                        << ":" << diag.audioSamples << ":" << p2bursts << ":" << p2vcw
                        << ":" << p2sf << ":" << p2mask
                        << ":" << p2crc
                        << ":" << diag.phase2MacNominalCrcValid
                        << ":" << diag.phase2MacAltKindCrcValid
                        << ":" << diag.phase2MacBitSwapCrcValid
                        << ":" << diag.phase2MacSlipCrcValid
                        << ":" << diag.phase2MacInvertCrcValid
                        << ":" << p2EssKnown << ":" << p2EssEncrypted
                        << ":" << trafficStatus.present << ":" << trafficStatus.callActive
                        << ":" << (trafficStatus.present && trafficStatus.diag.audioOpen)
                        << ":" << speakerAudioOpened;
                    const bool heartbeatDue = nowMs - lastCliFollowHeartbeatMs >= 2500;
                    const bool signatureChanged = sig.str() != lastVoiceSig;
                    if (signatureChanged || heartbeatDue) {
                        lastVoiceSig = sig.str();
                        if (heartbeatDue) lastCliFollowHeartbeatMs = nowMs;
                        const long long diagAgeMs = diag.updatedMs > 0
                            ? static_cast<long long>(std::max<qint64>(0, nowMs - static_cast<qint64>(diag.updatedMs)))
                            : -1LL;
                        std::cout << "  voice stage=" << p25VoiceDiagLabel(code)
                                  << " elapsed=" << ((nowMs - voiceStartMs) / 1000.0)
                                  << "s diagAge=" << diagAgeMs << "ms"
                                  << " sync=" << diag.syncs
                                  << " nid=" << diag.nids
                                  << " nidLock=" << (diag.nidLock ? "yes" : "no")
                                  << " imbe=" << diag.imbeFrames
                                  << " decoded=" << diag.decodedFrames
                                  << " audio=" << diag.audioSamples
                                  << " p2bursts=" << p2bursts
                                  << " p2vcw=" << p2vcw
                                  << " p2sf=" << p2sf
                                  << " p2mask=" << p2mask
                                  << " p2mac=" << p2crc << "/" << p2mac
                                  << " " << p25Phase2AcchStatsText(diag).toStdString()
                                  << " p2ess=" << p2ess
                                  << " traffic=" << (trafficStatus.present ? trafficStatus.diag.state : "none")
                                  << " trafficEnd=" << (trafficStatus.present && !trafficStatus.diag.endReason.empty()
                                      ? trafficStatus.diag.endReason
                                      : std::string("-"))
                                  << " trafficCall=" << (trafficStatus.callActive ? "yes" : "no")
                                  << " speakerRecent=" << (speakerAudioOpened ? "yes" : "no")
                                  << " backend=" << (diag.backendAvailable ? "yes" : "no")
                                  << std::endl;
                    }
                    size_t evidenceScore = 0;
                    if (p2bursts > 0) evidenceScore += 10u + static_cast<size_t>(std::min<long long>(p2bursts, 60));
                    if (p2sf > 0) evidenceScore += 100u + static_cast<size_t>(std::min<long long>(p2sf, 60));
                    if (p2mask > 0) evidenceScore += 200u + static_cast<size_t>(std::min<long long>(p2mask, 60));
                    if (p2vcw > 0) evidenceScore += 400u + static_cast<size_t>(std::min<long long>(p2vcw, 120));
                    if (diag.phase2TargetVoiceCodewords > 0) {
                        evidenceScore += 700u +
                            static_cast<size_t>(std::min<long long>(diag.phase2TargetVoiceCodewords, 120));
                    }
                    if (diag.decodedFrames > 0 || diag.audioSamples > 0) {
                        evidenceScore += 2000u +
                            static_cast<size_t>(std::min<long long>(diag.decodedFrames, 40)) * 10u +
                            static_cast<size_t>(std::min<long long>(diag.audioSamples / 480, 100));
                    }
                    if (p2EssKnown && !p2EssEncrypted) evidenceScore += 3000u;
                    if (evidenceScore > 0) {
                        captureCliFollowIqSnapshot(
                            QString("p2-evidence-bursts%1-vcw%2-sf%3-mask%4-dec%5-aud%6")
                                .arg(p2bursts)
                                .arg(p2vcw)
                                .arg(p2sf)
                                .arg(p2mask)
                                .arg(diag.decodedFrames)
                                .arg(diag.audioSamples),
                            evidenceScore);
                    }
                    if (p25TalkgroupIsPhase2(tg)) {
                        P25SlotProbeSnapshot slotProbeSnapshot;
                        slotProbeSnapshot.nowMs = nowMs;
                        slotProbeSnapshot.tunedAtMs = voiceStartMs;
                        slotProbeSnapshot.trackedArmMs = cliSlotProbeArmMs;
                        slotProbeSnapshot.lastFlipMs = cliLastSlotProbeFlipMs;
                        slotProbeSnapshot.talkgroupId = tg.talkgroupId;
                        slotProbeSnapshot.trackedTalkgroupId = cliSlotProbeTg;
                        slotProbeSnapshot.voiceHz = tg.lastVoiceFreqHz;
                        slotProbeSnapshot.trackedVoiceHz = cliSlotProbeVoiceHz;
                        slotProbeSnapshot.wrongSlotChecks = cliWrongSlotChecks;
                        slotProbeSnapshot.flipCount = cliSlotProbeFlips;
                        slotProbeSnapshot.maxFlips = 4;
                        slotProbeSnapshot.wrongSlotThreshold = 3;
                        slotProbeSnapshot.minFlipIntervalMs = 8000;
                        slotProbeSnapshot.earlyNoSyncFlipMs = 15000;
                        slotProbeSnapshot.inPassband = true;
                        slotProbeSnapshot.grantClearStateUnknown =
                            p25TalkgroupIsPhase2(tg) &&
                            !p25TalkgroupGrantProvesSpeakerClear(tg) &&
                            !p25TalkgroupGrantProvesSpeakerEncrypted(tg);
                        slotProbeSnapshot.grantClearKnown = p25TalkgroupGrantProvesSpeakerClear(tg);
                        slotProbeSnapshot.grantMaskParamsKnown = tg.p25MaskParamsKnown;
                        const bool cliCurrentSlotHasUsefulAudio =
                            diag.decodedFrames > 0 && diag.audioSamples > 0;
                        if (cliCurrentSlotHasUsefulAudio) {
                            cliLastSelectedSlotAudioMs = nowMs;
                        }
                        slotProbeSnapshot.diag = cliCurrentSlotHasUsefulAudio
                            ? static_cast<int>(P25VoiceDiagCode::Decoding)
                            : diag.diag;
                        slotProbeSnapshot.phase2VoiceCodewords = p2vcw;
                        slotProbeSnapshot.phase2TargetVoiceCodewords = diag.phase2TargetVoiceCodewords;
                        slotProbeSnapshot.phase2Bursts = p2bursts;
                        slotProbeSnapshot.phase2OppositeVoiceCodewords = diag.phase2OppositeVoiceCodewords;
                        slotProbeSnapshot.phase2SuperframeBursts = p2sf;
                        slotProbeSnapshot.phase2MaskedBursts = p2mask;
                        slotProbeSnapshot.phase2MacPdus = p2mac;
                        slotProbeSnapshot.phase2MacCrcValid = p2crc;
                        slotProbeSnapshot.phase2EssKnown = p2EssKnown;
                        slotProbeSnapshot.recentSelectedSlotAudio = cliCurrentSlotHasUsefulAudio;
                        slotProbeSnapshot.lastSelectedSlotAudioMs = cliLastSelectedSlotAudioMs;
                        const auto slotProbeDecision = evaluateP25SlotProbe(slotProbeSnapshot);
                        if (slotProbeDecision.resetTracking) {
                            cliSlotProbeTg = tg.talkgroupId;
                            cliSlotProbeVoiceHz = tg.lastVoiceFreqHz;
                            cliSlotProbeArmMs = voiceStartMs;
                            cliLastSlotProbeFlipMs = 0;
                            cliLastSelectedSlotAudioMs = cliCurrentSlotHasUsefulAudio ? nowMs : 0;
                        }
                        cliWrongSlotChecks = slotProbeDecision.wrongSlotChecksAfterObservation;
                        cliSlotProbeFlips = slotProbeDecision.flipCountAfterObservation;
                        if (slotProbeDecision.shouldFlip || slotProbeDecision.earlyNoSyncFlip ||
                            slotProbeDecision.maskedOppositeDominantFlip) {
                            bool flipped = false;
                            uint8_t oldSlot = 0;
                            uint8_t newSlot = 0;
                            {
                                std::lock_guard<std::mutex> lk(cliRxMutex);
                                ensureCliRxLocked(0);
                                Receiver& rx = *cliReceivers[0];
                                std::lock_guard<std::mutex> rxLock(rx.stateMutex);
                                if (rx.p25VoiceDecodeEnabled &&
                                    rx.p25VoicePhase2 &&
                                    rx.p25VoiceTalkgroupId == tg.talkgroupId) {
                                    if (p25Phase2GrantedSlotIsImmutable(rx)) {
                                        ++rx.p25DiagSlotProbeBlocked;
                                        rx.p25VoiceSlotProbePending = false;
                                        rx.p25VoiceSlotProbeRequested = 0;
                                    } else {
                                    oldSlot = rx.p25VoiceTdmaSlotKnown
                                        ? static_cast<uint8_t>(rx.p25VoiceTdmaSlot & 0x01u)
                                        : static_cast<uint8_t>(cliSlotProbeFlips & 0x01u);
                                    newSlot = static_cast<uint8_t>((oldSlot ^ 0x01u) & 0x01u);
                                    rearmCliP25Phase2Slot(rx, newSlot);
                                    flipped = true;
                                    }
                                }
                            }
                            if (flipped) {
                                if (cliAudio) cliAudio->clearBuffers();
                                ++cliSlotProbeFlips;
                                cliWrongSlotChecks = 0;
                                cliLastSlotProbeFlipMs = nowMs;
                                voiceDeadlineMs = std::min(maxVoiceDeadlineMs, std::max(voiceDeadlineMs, nowMs + 20000));
                                lastVoiceSig.clear();
                                std::cout << "  TDMA slot auto-probe: repeated wrong-slot VCWs with no MAC/ESS; switching slot "
                                          << static_cast<int>(oldSlot) << " -> " << static_cast<int>(newSlot)
                                          << " for TG " << tg.talkgroupId
                                          << " voice=" << (tg.lastVoiceFreqHz / 1e6)
                                          << " MHz. Audio remains gated until MAC/ESS and AMBE validate." << std::endl;
                            }
                        }
                    }
                    P25FollowSnapshot followSnapshot;
                    followSnapshot.nowMs = nowMs;
                    followSnapshot.tunedAtMs = voiceStartMs;
                    followSnapshot.lastActiveMs = cliFollowLastActiveMs;
                    followSnapshot.recentSpeakerOutputMs = lastSpeakerOutputMs;
                    followSnapshot.diagUpdatedMs = diag.updatedMs;
                    followSnapshot.currentCallSessionId = cliCurrentCallSessionId;
                    if (p2EssKnown && trafficStatus.present) {
                        followSnapshot.essCallSessionId = cliEssCallSessionId;
                    }
                    followSnapshot.autoActive = true;
                    followSnapshot.phase2Voice = p25TalkgroupIsPhase2(tg);
                    followSnapshot.talkgroupId = diag.talkgroupId;
                    followSnapshot.fallbackTalkgroupId = tg.talkgroupId;
                    followSnapshot.diag = diag.diag;
                    followSnapshot.syncs = diag.syncs;
                    followSnapshot.nids = diag.nids;
                    followSnapshot.imbeFrames = diag.imbeFrames;
                    followSnapshot.decodedFrames = diag.decodedFrames;
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
                        cliCallSecurityLatch == P25CallSecurityLatch::Clear &&
                        trafficStatus.callActive;
                    followSnapshot.phase2TrafficEncrypted =
                        cliCallSecurityLatch == P25CallSecurityLatch::Encrypted ||
                        (trafficStatus.present && trafficStatus.diag.encrypted);
                    followSnapshot.grantEncryptionKnown =
                        p25TalkgroupGrantProvesSpeakerClear(tg) || p25TalkgroupGrantProvesSpeakerEncrypted(tg);
                    followSnapshot.grantEncrypted = p25TalkgroupGrantProvesSpeakerEncrypted(tg);
                    followSnapshot.phase2OppositeVoiceCodewords = diag.phase2OppositeVoiceCodewords;

                    followSnapshot.recentSignalLevelDb = gLastRmsDb.load(std::memory_order_relaxed);
                    followSnapshot.recentNoiseFloorDb = gLastNoiseFloorDb.load(std::memory_order_relaxed);
                    followSnapshot.recentSnrDb = gLastSnrDb.load(std::memory_order_relaxed);
                    followSnapshot.rfMetricsPopulated = true;

                    const auto followDecision = evaluateP25Follow(followSnapshot);
                    if (followDecision.voiceStillLooksActive &&
                        (p2vcw > 0 || followSnapshot.decodedFrames > 0)) {
                        cliFollowLastActiveMs = nowMs;
                    }
                    if (followDecision.action == P25FollowAction::ReturnEncrypted) {
                        auto registry = loadP25Talkgroups();
                        bool changed = false;
                        for (auto& row : registry) {
                            if (!sameP25Talkgroup(row, ccHz, tg.talkgroupId)) continue;
                            row.encryptionKnown = true;
                            row.encrypted = true;
                            row.lastSeenMs = QDateTime::currentMSecsSinceEpoch();
                            changed = true;
                        }
                        if (changed) saveP25Talkgroups(registry);
                        p25RememberVoiceProvedEncryptedPhase2Grant(
                            gP25RecentExplicitEncryptedPhase2Grants,
                            tg,
                            nowMs);
                        std::cout << "P25 waitgrant TG " << tg.talkgroupId
                                  << " proved encrypted on voice channel; returning to control channel." << std::endl;
                        followCaptureReason = QStringLiteral("encrypted");
                        break;
                    }
                    if (followDecision.action != P25FollowAction::None) {
                        if (followDecision.action == P25FollowAction::ReturnNoMacEss) {
                            followCaptureReason = QStringLiteral("no_mac_ess");
                            if (followDecision.tdmaVcwNoSuperframeTimeout) {
                                std::cout << "  TDMA ACQ watchdog: VCWs present but no superframe/mask/ESS lock for TG "
                                          << tg.talkgroupId << "; returning to control channel."
                                          << " sf=" << p2sf
                                          << " mask=" << p2mask
                                          << " mac=" << p2crc << "/" << p2mac
                                          << " p2vcw=" << p2vcw << std::endl;
                            } else {
                                std::cout << "  TDMA ACQ watchdog: sf/mask present but MAC/ESS did not progress for TG "
                                          << tg.talkgroupId << "; returning to control channel."
                                          << " sf=" << p2sf
                                          << " mask=" << p2mask
                                          << " mac=" << p2crc << "/" << p2mac
                                          << " p2vcw=" << p2vcw << std::endl;
                            }
                        } else if (followDecision.action == P25FollowAction::ReturnNoVoiceCodewords) {
                            followCaptureReason = QStringLiteral("no_vcw");
                            std::cout << "  TDMA ACQ watchdog: no Phase 2 VCWs after retune for TG "
                                      << tg.talkgroupId << "; returning to control channel." << std::endl;
                        } else if (followDecision.action == P25FollowAction::ReturnHardTimeout) {
                            followCaptureReason = QStringLiteral("hard_timeout");
                            std::cout << "  P25 follow hard timeout for TG " << tg.talkgroupId
                                      << "; returning to control channel." << std::endl;
                        } else {
                            followCaptureReason = QStringLiteral("activity_gone");
                            std::cout << "  P25 follow activity ended for TG " << tg.talkgroupId
                                      << "; returning to control channel." << std::endl;
                        }
                        break;
                    }
                    if (speakerAudioOpened) {
                        followCaptureReason = QStringLiteral("audio_opened");
                        if (!recordFollowAudio) break;
                        if (cliAudioOpenedMs <= 0) {
                            cliAudioOpenedMs = nowMs;
                            cliLastSpeakerOutputSeenMs = lastSpeakerOutputMs;
                            captureCliFollowIqSnapshot(
                                QStringLiteral("audio-open"),
                                std::max<size_t>(cliBestEvidenceIqScore + 1u, 6000u));
                            if (!cliBestEvidenceIqWindow.samples.empty()) {
                                cliAudioOpenIqWindow = cliBestEvidenceIqWindow;
                                cliAudioOpenIqWindowUtc = cliBestEvidenceIqWindowUtc;
                            }
                            const qint64 holdMs = static_cast<qint64>(std::llround(recordFollowSeconds * 1000.0));
                            voiceDeadlineMs = std::min(maxVoiceDeadlineMs, std::max(voiceDeadlineMs, nowMs + holdMs));
                            std::cout << "  P25 speaker-gated audio opened; continuing WAV capture for up to "
                                      << (holdMs / 1000.0)
                                      << "s so the saved file is listenable." << std::endl;
                        } else if (nowMs - cliAudioOpenedMs >= static_cast<qint64>(std::llround(recordFollowSeconds * 1000.0))) {
                            break;
                        }
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(120));
                }

                if (recordFollowAudio) {
                    const auto wav = stopCliP25WavCapture();
                    const double wavSeconds = wav.sampleRate > 0
                        ? static_cast<double>(wav.samples) / static_cast<double>(wav.sampleRate)
                        : 0.0;
                    std::cout << "P25 waitgrant saved decoded WAV audio: "
                              << wav.path.toStdString()
                              << " samples=" << static_cast<unsigned long long>(wav.samples)
                              << " seconds=" << wavSeconds
                              << std::endl;
                    const auto oppWav = stopCliP25OppositeWavCapture();
                    if (oppWav.active || oppWav.samples > 0) {
                        const double oppSeconds = oppWav.sampleRate > 0
                            ? static_cast<double>(oppWav.samples) / static_cast<double>(oppWav.sampleRate)
                            : 0.0;
                        std::cout << "P25 waitgrant saved companion WAV audio: "
                                  << oppWav.path.toStdString()
                                  << " samples=" << static_cast<unsigned long long>(oppWav.samples)
                                  << " seconds=" << oppSeconds
                                  << std::endl;
                    }
                }

                if (recordFollowCapture) {
                    const auto capture = saveCliP25FollowIqCapture(
                        mgr,
                        static_cast<size_t>(devIndex),
                        ccHz,
                        tg,
                        recordFollowSeconds,
                        finalVoiceDiag,
                        followCaptureReason,
                        lastVoiceSig,
                        selectedGrantEventText,
                        selectedGrantDetailText,
                        cliTrafficCenterHz,
                        !cliAudioOpenIqWindow.samples.empty()
                            ? &cliAudioOpenIqWindow
                            : (cliBestEvidenceIqWindow.samples.empty() ? nullptr : &cliBestEvidenceIqWindow),
                        !cliAudioOpenIqWindow.samples.empty()
                            ? cliAudioOpenIqWindowUtc
                            : cliBestEvidenceIqWindowUtc,
                        !cliAudioOpenIqWindow.samples.empty()
                            ? QStringLiteral("audio-open-snapshot")
                            : cliBestEvidenceIqReason);
                    if (capture.ok) {
                        std::cout << "P25 waitgrant saved follow IQ capture: "
                                  << capture.message.toStdString() << "\n"
                                  << capture.directory.toStdString() << std::endl;
                    } else {
                        std::cout << "P25 waitgrant follow IQ capture failed: "
                                  << capture.message.toStdString() << std::endl;
                    }
                }

                {
                    std::lock_guard<std::mutex> lk(cliRxMutex);
                    ensureCliRxLocked(0);
                    Receiver& rx = *cliReceivers[0];
                    std::lock_guard<std::mutex> rxLock(rx.stateMutex);
                    clearCliP25VoiceFollow(rx);
                    rx.resetDemodState();
                    rx.deviceIndex = static_cast<size_t>(devIndex);
                    rx.freqHz = ccHz;
                    rx.mode = DemodMode::NFM;
                    rx.channelBwHz = 12500.0;
                    rx.audioLpfEnabled = false;
                    rx.p25ControlChannelMute = true;
                    rx.active = true;
                }
                if (cliAudio) cliAudio->clearBuffers();
                if (mgr.isStreaming(static_cast<size_t>(devIndex))) mgr.setCenterFreq(static_cast<size_t>(devIndex), ccHz);
                std::cout << "P25 waitgrant returned to muted control channel " << ccMhz << " MHz." << std::endl;

                const qint64 retryNowMs = QDateTime::currentMSecsSinceEpoch();
                const bool followProducedAudio =
                    finalVoiceDiag.decodedFrames > 0 &&
                    finalVoiceDiag.audioSamples > 0;
                const bool retryableFollowMiss =
                    selectedGrantPhase2 &&
                    !followProducedAudio &&
                    followCaptureReason != QStringLiteral("encrypted") &&
                    retryNowMs < grantDeadlineMs;
                if (retryableFollowMiss) {
                    const qint64 cooldownMs =
                        followCaptureReason == QStringLiteral("no_vcw") ? 15000 :
                        followCaptureReason == QStringLiteral("no_mac_ess") ? 10000 :
                        6000;
                    const QString retryKey = waitGrantFollowKey(tg);
                    followRetryCooldowns[retryKey] = retryNowMs + cooldownMs;
                    skippedCooldownKeys.erase(retryKey);
                    decoder = P25LiveDecoder(p25CliControlGrantDecoderConfig());
                    waitGrantLastWindowEnd = 0;
                    std::cout << "P25 waitgrant continuing scan after "
                              << followCaptureReason.toStdString()
                              << " on TG " << tg.talkgroupId
                              << "; same TG/channel/slot cooldown="
                              << (cooldownMs / 1000.0)
                              << "s remaining_time="
                              << ((grantDeadlineMs - retryNowMs) / 1000.0)
                              << "s" << std::endl;
                    continue;
                }

                break;
                }
            } else if (sub == "sync" || sub == "decode") {
                int devIndex = 0;
                double targetMhz = 0.0;
                double ms = 250.0;
                if (iss >> devIndex) {
                    if (iss >> targetMhz) {
                        if (iss >> ms) {}
                    }
                }
                std::vector<float> p;
                double cf = 0.0, sr = 0.0;
                if (devIndex < 0 || !mgr.getLatestSpectrum(static_cast<size_t>(devIndex), p, cf, sr) || sr <= 0.0) {
                    std::cout << "no spectrum/sample-rate yet for live P25 sync (enable/tune first)\n";
                    continue;
                }
                double targetHz = targetMhz > 0.0 ? targetMhz * 1e6 : 0.0;
                if (targetHz <= 0.0) {
                    std::lock_guard<std::mutex> lk(cliRxMutex);
                    for (const auto& rxPtr : cliReceivers) {
                        if (!rxPtr) continue;
                        std::lock_guard<std::mutex> rxLock(rxPtr->stateMutex);
                        if (rxPtr->active && rxPtr->deviceIndex == static_cast<size_t>(devIndex)) {
                            targetHz = rxPtr->freqHz;
                            break;
                        }
                    }
                }
                if (targetHz <= 0.0) targetHz = cf;
                ms = std::clamp(ms, 50.0, 2000.0);
                const size_t requestedSamples = static_cast<size_t>(std::clamp(sr * (ms / 1000.0), 24000.0, 4194304.0));
                auto iq = mgr.getRecentIQWindow(static_cast<size_t>(devIndex), requestedSamples);
                auto decoderIt = cliP25LiveDecoders.find(static_cast<size_t>(devIndex));
                if (decoderIt == cliP25LiveDecoders.end()) {
                    decoderIt = cliP25LiveDecoders.emplace(static_cast<size_t>(devIndex),
                                                           P25LiveDecoder(p25DiagnosticDecoderConfig())).first;
                }
                auto& decoder = decoderIt->second;
                double effectiveTargetHz = targetHz;
                auto result = decodeP25ControlWithOffsetProbe(
                    decoder, iq, sr, cf, targetHz, &effectiveTargetHz);
                auto& analyzer = cliP25Analyzers[static_cast<long long>(std::llround(targetHz))];
                const bool nidLock = result.stats.bestNidValid ||
                    std::any_of(result.nids.begin(), result.nids.end(), [](const P25Nid& nid) {
                        return nid.fecValidated;
                    });
                for (const auto& nid : result.nids) {
                    if (nid.fecValidated) {
                        analyzer.setNac(nid.nac);
                        break;
                    }
                }
                std::cout << "P25 live sync dev=" << devIndex
                          << " target=" << (targetHz / 1e6) << " MHz"
                          << " effective=" << (effectiveTargetHz / 1e6) << " MHz"
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
                          << " cqpskCand=" << result.stats.cqpskCandidatesEvaluated
                          << " c4fmSkipCqpsk=" << (result.stats.c4fmHardLockSkippedCqpsk ? "yes" : "no")
                          << " targetOffsetHz=" << result.stats.inputTargetOffsetHz
                          << " chanSr=" << result.stats.channelSampleRate
                          << " discMeanHz=" << result.stats.discriminatorMeanHz
                          << " iq=" << iq.size()
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
                    size_t trusted = 0;
                    for (const auto& block : result.rawTsbkBlocks) if (block.fecDecoded && block.crcValid) ++trusted;
                    std::cout << "  raw TSDU block candidates=" << result.rawTsbkBlocks.size()
                              << " trusted=" << trusted
                              << " (trusted means trellis-decoded and CRC-valid)\n";
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
                                std::cout << "    " << p25GrantDetailLogText(ev).toStdString() << "\n";
                            } else if (ev.type == P25ControlEventType::IdentifierUpdate && ev.phase2Candidate) {
                                std::cout << "    TDMA identifier table update: " << p25EventLogText(ev).toStdString() << "\n";
                            }
                            const bool eventRegistryEligible = p25TsbkEventRegistryEligible(block.correctedDibitErrors, ev);
                            if (eventRegistryEligible) {
                                changed = mergeP25TalkgroupEvent(talkgroups, targetHz, ev, nowMs) || changed;
                                if (!registryEligible && p25ControlEventIsResolvedVoiceGrant(ev)) {
                                    acceptedHighCorrectionGrant = true;
                                    std::cout << "    note: accepted resolved voice grant despite "
                                              << block.correctedDibitErrors
                                              << " corrected dibits (voice grant threshold "
                                              << kP25VoiceGrantMaxCorrectedDibits << ")\n";
                                }
                            }
                        }
                        if (!registryEligible && !acceptedHighCorrectionGrant) {
                            std::cout << "    note: CRC valid but corrected dibits exceed registry threshold; only resolved voice grants up to "
                                      << kP25VoiceGrantMaxCorrectedDibits
                                      << " corrected dibits are saved/followed; unresolved waitgrant/followtest candidates can be queued up to "
                                      << kP25PendingVoiceGrantMaxCorrectedDibits << " corrected dibits\n";
                        }
                    }
                    if (changed) {
                        saveP25Talkgroups(talkgroups);
                        std::cout << "  Talkgroup registry updated from trusted live TSBK.\n";
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
                                  << " ess=" << (burst.essKnown ? (burst.encrypted ? "encrypted" : "clear") : "unknown")
                                  << " isch=" << isch.str() << "\n";
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
                                std::cout << "    " << p25GrantDetailLogText(ev).toStdString() << "\n";
                            }
                            changed = mergeP25TalkgroupEvent(talkgroups, targetHz, ev, nowMs) || changed;
                        }
                    }
                    if (changed) {
                        saveP25Talkgroups(talkgroups);
                        std::cout << "  Talkgroup registry updated from trusted Phase 2 MAC.\n";
                    }
                }
                for (const auto& warning : result.warnings) {
                    std::cout << "  note: " << warning << "\n";
                }
            } else if (sub == "followtest" || sub == "replayfollow") {
                std::string rest;
                std::getline(iss, rest);
                const auto args = parseP25ReplayCliArgs(rest);
                if (!args.ok || args.targetMhz <= 0.0) {
                    std::cout << "usage: p25 followtest <sigmf-meta|sigmf-data|capture_dir> <cc_mhz> [ms] [skip=<ms>] [center=<mhz>] [voicecenter=<mhz>] [followms=<ms>] [tg=<id>] [nac= wacn= system=]\n";
                    if (!args.error.empty()) std::cout << args.error << "\n";
                    continue;
                }
                runP25ReplayFollowTest(args);
            } else if (sub == "voicetest" || sub == "replayvoice") {
                std::string rest;
                std::getline(iss, rest);
                const auto args = parseP25ReplayCliArgs(rest);
                if (!args.ok || args.targetMhz <= 0.0) {
                    std::cout << "usage: p25 voicetest <sigmf|dir> <voice_mhz> [ms] [skip=<ms>] [center=<mhz>] [slot=0|1] [tg=] [nac= wacn= system=] [clear|enc] [stream|legacy] [probe|noprobe] [windowms=] [hopms=] [wav=out.wav] [oppwav=companion.wav] [minframes=] [minaudio=]\n";
                    if (!args.error.empty()) std::cout << args.error << "\n";
                    continue;
                }
                runP25ReplayVoiceTest(args);
            } else if (sub == "replay") {
                std::string rest;
                std::getline(iss, rest);
                const auto args = parseP25ReplayCliArgs(rest);
                if (!args.ok) {
                    std::cout << args.error << "\n";
                    continue;
                }

                const double maxMs = args.ms > 0.0 ? std::clamp(args.ms, 50.0, 20000.0) : 0.0;
                const auto loadStart = std::chrono::steady_clock::now();
                if (args.traceReplay) {
                    std::cerr << "P25 replay trace=load-start path=\"" << args.path
                              << "\" maxMs=" << maxMs
                              << " skipMs=" << args.skipMs << std::endl;
                }
                auto capture = loadSigmfCf32Capture(QString::fromStdString(args.path), maxMs, args.skipMs);
                if (args.traceReplay) {
                    const auto loadElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - loadStart).count();
                    std::cerr << "P25 replay trace=load-done ok=" << (capture.ok ? "yes" : "no")
                              << " samples=" << capture.iq.size()
                              << " elapsedMs=" << loadElapsedMs << std::endl;
                }
                if (!capture.ok) {
                    std::cout << "P25 replay load failed: " << capture.error << "\n";
                    continue;
                }
                if (args.centerMhz > 0.0 && std::isfinite(args.centerMhz)) {
                    capture.centerFreqHz = args.centerMhz * 1e6;
                }
                double targetHz = args.targetMhz > 0.0 ? args.targetMhz * 1e6 : capture.targetFreqHz;
                if (!std::isfinite(targetHz) || targetHz <= 0.0) targetHz = capture.centerFreqHz;

                auto& analyzer = cliP25Analyzers[static_cast<long long>(std::llround(targetHz))];

                std::cout << "Loaded SigMF replay: " << capture.iq.size() << " samples"
                          << " datatype=" << capture.datatype
                          << " start_ms=" << capture.startOffsetMs
                          << " first_sample=" << static_cast<unsigned long long>(capture.firstSampleOffset)
                          << " center=" << (capture.centerFreqHz / 1e6) << " MHz"
                          << " target=" << (targetHz / 1e6) << " MHz"
                          << " decoder=" << (args.phase2Voice ? "phase2-voice-6000sps" : "diagnostic-4800sps")
                          << " maskParams=" << (p25ReplayHasMaskParameters(args) ? "provided" : "none")
                          << " meta=\"" << capture.metaPath.toStdString() << "\""
                          << " data=\"" << capture.dataPath.toStdString() << "\"\n";
                std::cout.flush();

                P25LiveDecoder decoder(args.phase2Voice
                    ? p25VoiceDecoderConfig(true, P25VoiceDecodeProfile::Forensic)
                    : p25DiagnosticDecoderConfig());
                if (args.phase2Voice && p25ReplayHasMaskParameters(args)) {
                    decoder.setPhase2MaskParameters(static_cast<uint16_t>(args.nac),
                                                    static_cast<uint32_t>(args.wacn),
                                                    static_cast<uint16_t>(args.systemId));
                }
                const size_t liveWindow = static_cast<size_t>(std::clamp(
                    capture.sampleRateHz * 0.512,
                    24000.0,
                    static_cast<double>(std::max<size_t>(1, capture.iq.size()))));
                const size_t windowSamples = std::min(capture.iq.size(), liveWindow);
                const size_t hopSamples = std::max<size_t>(1, windowSamples / 2);
                std::cout << "Replay windows: window_ms=" << (static_cast<double>(windowSamples) * 1000.0 / capture.sampleRateHz)
                          << " hop_ms=" << (static_cast<double>(hopSamples) * 1000.0 / capture.sampleRateHz)
                          << " persistent_decoder=yes\n";

                P25LiveDecodeResult bestResult;
                int bestScore = std::numeric_limits<int>::min();
                size_t bestStart = 0;
                size_t printed = 0;
                size_t replayWindowIndex = 0;
                constexpr size_t kMaxReplayReports = 12;
                for (size_t start = 0; start < capture.iq.size(); start += hopSamples) {
                    ++replayWindowIndex;
                    const size_t end = std::min(capture.iq.size(), start + windowSamples);
                    if (end <= start) break;
                    std::vector<std::complex<float>> window(capture.iq.begin() + static_cast<std::ptrdiff_t>(start),
                                                            capture.iq.begin() + static_cast<std::ptrdiff_t>(end));
                    const auto decodeStart = std::chrono::steady_clock::now();
                    if (args.traceReplay) {
                        std::cerr << "P25 replay trace=decode-start window=" << replayWindowIndex
                                  << " startMs=" << (capture.startOffsetMs + static_cast<double>(start) * 1000.0 / capture.sampleRateHz)
                                  << " samples=" << window.size()
                                  << std::endl;
                    }
                    auto result = decoder.processIq(window, capture.sampleRateHz, capture.centerFreqHz, targetHz);
                    if (args.traceReplay) {
                        const auto decodeElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - decodeStart).count();
                        std::cerr << "P25 replay trace=decode-done window=" << replayWindowIndex
                                  << " elapsedMs=" << decodeElapsedMs
                                  << " sync=" << result.syncs.size()
                                  << " p2bursts=" << result.stats.phase2Bursts
                                  << " p2mac=" << result.stats.phase2MacCrcValid << "/" << result.stats.phase2MacPdus
                                  << std::endl;
                    }
                    const int score = p25CliDecodeScore(result);
                    if (score > bestScore) {
                        bestScore = score;
                        bestStart = start;
                        bestResult = result;
                    }
                    const bool hasTrustedTsbk = std::any_of(result.rawTsbkBlocks.begin(), result.rawTsbkBlocks.end(), [](const P25TsbkBlock& block) {
                        return block.fecDecoded && block.crcValid;
                    });
                    const bool interesting = hasTrustedTsbk ||
                        p25DecodeResultHasNidLock(result) ||
                        result.stats.phase2MacCrcValid > 0 ||
                        result.stats.phase2SuperframeBursts > 0 ||
                        result.stats.phase2VoiceCodewords > 0;
                    if (interesting && printed < kMaxReplayReports) {
                        std::ostringstream label;
                        label << "P25 replay chunk start_ms="
                              << (static_cast<double>(start) * 1000.0 / capture.sampleRateHz);
                        printP25CliDecodeReport(label.str(), -1, capture.centerFreqHz, capture.sampleRateHz, targetHz, result, analyzer);
                        ++printed;
                    }
                    if (end == capture.iq.size()) break;
                }

                if (printed == 0 && bestScore > std::numeric_limits<int>::min()) {
                    std::ostringstream label;
                    label << "P25 replay best start_ms="
                          << (static_cast<double>(bestStart) * 1000.0 / capture.sampleRateHz);
                    printP25CliDecodeReport(label.str(), -1, capture.centerFreqHz, capture.sampleRateHz, targetHz, bestResult, analyzer);
                }
            } else if (sub == "audit") {
                std::cout << "P25 audit: running static verify scripts...\n";
                const char* scripts[] = {
                    "python src\\tools\\verify_p25_phase2_same_call_mhz_hop.py",
                    "python src\\tools\\verify_p25_phase2_clear_grant_op02_preservation.py",
                };
                int failures = 0;
                for (const char* cmdLine : scripts) {
                    std::cout << "  " << cmdLine << "\n";
                    if (std::system(cmdLine) != 0) ++failures;
                }
                std::cout << (failures == 0 ? "P25 audit: PASS\n" : "P25 audit: FAIL\n");
            } else if (sub == "test") {
                std::cout << "Running scripts/build_test_validate.ps1 ...\n";
                const int rc = std::system("powershell -NoProfile -ExecutionPolicy Bypass -File scripts\\build_test_validate.ps1");
                std::cout << (rc == 0 ? "P25 test: PASS\n" : "P25 test: FAIL\n");
            } else if (sub == "voice") {
                P25ImbeVoiceDecoder imbe;
                P25AmbeVoiceDecoder ambe;
                std::cout << "P25 IMBE backend: " << (imbe.backendAvailable() ? "available" : "not available")
                          << (imbe.backendAvailable()
                              ? " (clear Phase 1 IMBE frame decode can run after LDU voice extraction)"
                              : " (build with SDR_TOWN_ENABLE_MBELIB=ON and mbelib installed)") << "\n"
                          << "P25 AMBE backend: " << (ambe.backendAvailable() ? "available" : "not available")
                          << (ambe.backendAvailable()
                              ? " (clear Phase 2 AMBE synthesis backend is available; speaker release follows target-slot security)"
                              : " (build with SDR_TOWN_ENABLE_MBELIB=ON and mbelib installed)") << "\n"
                          << "P25 Phase 2 status: TDMA sync/DUID/2V/4V burst detection is enabled. Clear AMBE audio opens after target-slot PTT/ESS or explicit-clear target-slot AMBE validation. Explicit encrypted state remains muted.\n"
                          << "P25 Phase 2 validation log: "
                          << (p25Phase2ValidationLoggingEnabled()
                              ? p25Phase2ValidationPath().toStdString() + (p25Phase2ValidationRedactionEnabled() ? " (raw symbols redacted)" : " (raw symbols enabled; set SDR_TOWN_P25_VALIDATION_REDACT=1 to redact)")
                              : std::string("disabled; set SDR_TOWN_P25_VALIDATION_LOG=1 before launch to enable JSONL, add SDR_TOWN_P25_VALIDATION_REDACT=1 to redact raw symbols"))
                          << "\n";
            } else if (sub == "tg" || sub == "tgs" || sub == "talkgroups") {
                auto talkgroups = loadP25Talkgroups();
                if (talkgroups.empty()) {
                    std::cout << "No P25 talkgroups saved yet.\n";
                } else {
                    for (size_t i = 0; i < talkgroups.size(); ++i) {
                        const auto& tg = talkgroups[i];
                        std::string proto = p25TalkgroupIsPhase2(tg)
                            ? std::string("P2")
                            : p25VoiceProtocolShort(tg.voiceProtocol).toStdString();
                        if (proto == "-") proto = "unknown";
                        std::cout << "[" << i << "] CC " << (tg.controlFreqHz / 1e6) << " MHz"
                                  << " TG " << tg.talkgroupId
                                  << (tg.alphaTag.empty() ? "" : (" \"" + tg.alphaTag + "\""))
                                  << " voice=" << (tg.lastVoiceFreqHz > 0.0 ? std::to_string(tg.lastVoiceFreqHz / 1e6) + " MHz" : std::string("-"))
                                  << " proto=" << proto
                                  << (tg.tdmaSlotKnown ? (" slot=" + std::to_string(tg.tdmaSlot)) : std::string())
                                  << " hits=" << tg.hitCount
                                  << " verified=" << (tg.verified ? "yes" : "no")
                                  << " scanner=" << (tg.scannerEnabled ? "yes" : "no")
                                  << " enc=" << (tg.encryptionKnown ? (tg.encrypted ? "yes" : "no") : "unknown") << "\n";
                    }
                }
            } else if (sub == "addtg") {
                double ccMhz = 0.0;
                uint32_t tgid = 0;
                if (!(iss >> ccMhz >> tgid) || ccMhz <= 0.0 || tgid == 0) {
                    std::cout << "usage: p25 addtg <cc_mhz> <tgid> [alpha tag]\n";
                    continue;
                }
                std::string tag;
                std::getline(iss, tag);
                tag = trimCopy(tag);
                const double ccHz = ccMhz * 1e6;
                auto talkgroups = loadP25Talkgroups();
                auto it = std::find_if(talkgroups.begin(), talkgroups.end(), [&](const P25TalkgroupEntry& tg) {
                    return sameP25Talkgroup(tg, ccHz, tgid);
                });
                const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
                if (it == talkgroups.end()) {
                    P25TalkgroupEntry tg;
                    tg.controlFreqHz = ccHz;
                    tg.talkgroupId = tgid;
                    tg.alphaTag = tag.empty() ? ("TG " + std::to_string(tgid)) : tag;
                    tg.verified = true;
                    tg.firstSeenMs = nowMs;
                    tg.lastSeenMs = nowMs;
                    talkgroups.push_back(tg);
                } else {
                    if (!tag.empty()) it->alphaTag = tag;
                    it->verified = true;
                    it->lastSeenMs = nowMs;
                }
                saveP25Talkgroups(talkgroups);
                std::cout << "Saved verified P25 TG " << tgid << " for CC " << ccMhz << " MHz\n";
            } else if (sub == "deltg" || sub == "rmtg") {
                int idx = -1;
                if (!(iss >> idx)) {
                    std::cout << "usage: p25 deltg <talkgroup-index>\n";
                    continue;
                }
                auto talkgroups = loadP25Talkgroups();
                if (idx < 0 || static_cast<size_t>(idx) >= talkgroups.size()) {
                    std::cout << "bad talkgroup index\n";
                    continue;
                }
                const auto removed = talkgroups[static_cast<size_t>(idx)];
                talkgroups.erase(talkgroups.begin() + idx);
                saveP25Talkgroups(talkgroups);
                {
                    std::lock_guard<std::mutex> lk(cliRxMutex);
                    for (auto& rxPtr : cliReceivers) {
                        if (!rxPtr) continue;
                        std::lock_guard<std::mutex> rxLock(rxPtr->stateMutex);
                        if (rxPtr->p25VoiceTalkgroupId == removed.talkgroupId) clearCliP25VoiceFollow(*rxPtr);
                    }
                }
                std::cout << "Deleted P25 TG " << removed.talkgroupId
                          << " at CC " << (removed.controlFreqHz / 1e6) << " MHz\n";
            } else if (sub == "follow") {
                int idx = -1;
                int rxidx = 0;
                if (!(iss >> idx)) {
                    std::cout << "usage: p25 follow <talkgroup-index> [rx]\n";
                    continue;
                }
                if (iss >> rxidx) {}
                auto talkgroups = loadP25Talkgroups();
                if (idx < 0 || static_cast<size_t>(idx) >= talkgroups.size()) {
                    std::cout << "bad talkgroup index\n";
                    continue;
                }
                auto tg = talkgroups[static_cast<size_t>(idx)];
                p25AugmentTalkgroupFromKnownSite(tg, talkgroups, tg.controlFreqHz);
                if (tg.encryptionKnown && tg.encrypted) {
                    std::cout << "Refusing encrypted P25 TG " << tg.talkgroupId << "\n";
                    continue;
                }
                if (!p25TalkgroupCanTuneForFollow(tg)) {
                    std::cout << "TG " << tg.talkgroupId << " has unknown encryption state; Phase 2 can still follow and will queue audio until target-slot PTT/ESS proves clear\n";
                    continue;
                }
                if (!tg.encryptionKnown && p25TalkgroupIsPhase2(tg)) {
                    std::cout << "TG " << tg.talkgroupId
                              << " is Phase 2 with unknown grant encryption; tuning so MAC/ESS can prove clear before audio is released\n";
                }
                if (p25TalkgroupIsPhase2(tg)) {
                    std::cout << "TG " << tg.talkgroupId
                              << " is Phase 2 TDMA"
                              << (tg.tdmaSlotKnown ? (" slot " + std::to_string(tg.tdmaSlot)) : std::string())
                              << "; following with TDMA burst diagnostics; audio queues until target-slot PTT/ESS or target MAC/ESS proves clear\n";
                }
                if (tg.lastVoiceFreqHz <= 0.0) {
                    std::cout << "TG " << tg.talkgroupId << " has no active voice grant/frequency yet\n";
                    continue;
                }
                size_t devIndex = 0;
                {
                    std::lock_guard<std::mutex> lk(cliRxMutex);
                    ensureCliRxLocked(static_cast<size_t>(std::max(0, rxidx)));
                    Receiver& rx = *cliReceivers[static_cast<size_t>(std::max(0, rxidx))];
                    std::lock_guard<std::mutex> rxLock(rx.stateMutex);
                    std::lock_guard<std::recursive_mutex> dspLock(rx.dspMutex);
                    devIndex = rx.deviceIndex;
                    rx.resetDemodState();
                    rx.freqHz = tg.lastVoiceFreqHz;
                    rx.mode = DemodMode::NFM;
                    rx.channelBwHz = 12500.0;
                    rx.lpfHz = 3000.0;
                    rx.audioLpfEnabled = false;
                    rx.squelchDb = -105.0;
                    const bool phase2Voice = p25TalkgroupIsPhase2(tg);
                    p25ClearPhase2PendingAudio(rx);
                    rx.resetP25VoiceState();
                    clearP25SessionScopedState(rx);
                    rx.p25VoiceResetPending = false;
                    rx.p25VoiceDecodeEnabled = true;
                    rx.p25VoiceClearKnown = p25TalkgroupGrantProvesSpeakerClear(tg);
                    rx.p25VoiceEncrypted = p25TalkgroupGrantProvesSpeakerEncrypted(tg);
                    rx.p25VoiceTalkgroupId = tg.talkgroupId;
                    rx.p25VoiceSourceId = tg.lastSourceId;
                    const qint64 armNowMs = QDateTime::currentMSecsSinceEpoch();
                    p25Phase2BeginNewPtt(rx, armNowMs);
                    rx.p25VoicePhase2 = phase2Voice;
                    rx.p25VoiceTdmaSlotKnown = tg.tdmaSlotKnown;
                    rx.p25VoiceTdmaSlot = tg.tdmaSlot;
                    rx.p25VoiceSlotProbePending = false;
                    rx.p25VoiceSlotProbeRequested = 0;
                    rx.p25VoiceMaskParamsKnown = tg.p25MaskParamsKnown;
                    rx.p25VoiceNac = tg.nac;
                    rx.p25VoiceWacn = tg.wacn;
                    rx.p25VoiceSystemId = tg.systemId;
                    rx.p25VoiceSettleUntilMs = armNowMs + p25PostArmSettleMs(rx.p25VoicePhase2);
                    rx.p25VoiceDiscardWindows = p25PostArmDiscardWindows(rx.p25VoicePhase2);
                    rx.p25ControlChannelMute = false;
                    rx.p25Phase2AllowLateEntryAudioProbe =
                        rx.p25VoicePhase2 && kP25Phase2AllowUnknownGrantFieldAudioProbe;
                    rx.p25VoiceLiveDecoder = P25LiveDecoder(p25VoiceDecoderConfigForReceiver(rx));
                    if (rx.p25VoicePhase2 && rx.p25VoiceMaskParamsKnown) {
                        rx.p25VoiceLiveDecoder.setPhase2MaskParameters(rx.p25VoiceNac, rx.p25VoiceWacn, rx.p25VoiceSystemId);
                    } else {
                        rx.p25VoiceLiveDecoder.clearPhase2MaskParameters();
                    }
                    rx.active = true;
                }
                uint64_t manualVoiceTuneSeq = 0;
                if (devIndex < mgr.getDevices().size()) {
                    manualVoiceTuneSeq = mgr.setCenterFreq(devIndex, tg.lastVoiceFreqHz);
                }
                if (!mgr.isStreaming(devIndex) && devIndex < mgr.getDevices().size()) {
                    mgr.setEnabled(devIndex, true);
                    mgr.startStreaming(devIndex, true);
                }
                const bool manualPhase2Voice = p25TalkgroupIsPhase2(tg);
                if (manualPhase2Voice && manualVoiceTuneSeq != 0) {
                    const bool tuneApplied = mgr.waitForCenterTuneApplied(devIndex, manualVoiceTuneSeq, 650);
                    std::cout << "P25 follow voice retune "
                              << (tuneApplied ? "applied" : "pending")
                              << ": seq=" << manualVoiceTuneSeq
                              << " appliedSeq=" << mgr.getCenterTuneAppliedSeq(devIndex)
                              << " voice=" << (tg.lastVoiceFreqHz / 1e6) << " MHz\n";
                }
                {
                    std::lock_guard<std::mutex> lk(cliRxMutex);
                    ensureCliRxLocked(static_cast<size_t>(std::max(0, rxidx)));
                    Receiver& rx = *cliReceivers[static_cast<size_t>(std::max(0, rxidx))];
                    std::lock_guard<std::mutex> rxLock(rx.stateMutex);
                    if (manualPhase2Voice) {
                        double cursorSampleRateHz = 0.0;
                        const auto devices = mgr.getDevices();
                        if (devIndex < devices.size()) cursorSampleRateHz = devices[devIndex].sampleRate;
                        mgr.setReceiverCursorBeforeLiveEdge(devIndex, rx,
                            p25Phase2TrafficPreRollSamples(cursorSampleRateHz, true));
                    } else {
                        mgr.setReceiverCursorToLiveEdge(devIndex, rx);
                    }
                }
                std::cout << p25FollowDetailLogText(tg).toStdString() << "\n";
                std::cout << "Following P25 TG " << tg.talkgroupId
                          << " at " << (tg.lastVoiceFreqHz / 1e6) << " MHz on RX" << rxidx
                          << (p25TalkgroupIsPhase2(tg)
                              ? " with TDMA diagnostics; Phase 2 audio follows target-slot PTT/ESS security\n"
                              : " with live clear IMBE Phase 1 voice decode\n");
            } else if (sub == "tsbk") {
                double ccMhz = 0.0;
                if (!(iss >> ccMhz) || ccMhz <= 0.0) {
                    std::cout << "usage: p25 tsbk <cc_mhz> <10-or-12-byte hex block>\n";
                    continue;
                }
                std::string hex;
                std::getline(iss, hex);
                hex = trimCopy(hex);
                auto bytes = p25ParseHexBytes(hex);
                if (bytes.size() != 10 && bytes.size() != 12) {
                    std::cout << "TSBK hex must decode to 10 or 12 bytes; got " << bytes.size() << "\n";
                    continue;
                }
                const double ccHz = ccMhz * 1e6;
                auto& analyzer = cliP25Analyzers[static_cast<long long>(std::llround(ccHz))];
                auto events = analyzer.ingestTsbk(bytes);
                auto talkgroups = loadP25Talkgroups();
                bool changed = false;
                const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
                for (const auto& ev : events) {
                    std::cout << p25EventLogText(ev).toStdString() << "\n";
                    if (p25ControlEventIsVoiceGrant(ev)) {
                        std::cout << p25GrantDetailLogText(ev).toStdString() << "\n";
                    } else if (ev.type == P25ControlEventType::IdentifierUpdate && ev.phase2Candidate) {
                        std::cout << "TDMA identifier table update: " << p25EventLogText(ev).toStdString() << "\n";
                    }
                    changed = mergeP25TalkgroupEvent(talkgroups, ccHz, ev, nowMs) || changed;
                }
                if (changed) {
                    saveP25Talkgroups(talkgroups);
                    std::cout << "Talkgroup registry updated.\n";
                }
            } else {
                int devIndex = 0;
                if (!sub.empty()) {
                    try { devIndex = std::stoi(sub); } catch (...) { devIndex = 0; }
                }
                std::vector<float> p; double cf=0, sr=0;
                if (devIndex < 0 || !mgr.getLatestSpectrum(static_cast<size_t>(devIndex), p, cf, sr) || p.empty()) {
                    std::cout << "no spectrum yet for P25 scan\n";
                    continue;
                }
                auto hits = detectP25ControlCandidates(p, sr, cf);
                if (hits.empty()) {
                    std::cout << "No P25-width spectral candidates in current view.\n";
                } else {
                    std::cout << "P25-width spectral candidates (unverified, run 'p25 sync <dev> <mhz>') dev "
                              << devIndex << ":\n";
                    for (const auto& h : hits) {
                        std::cout << "  " << (h.freqHz / 1e6) << " MHz"
                                  << "  snr=" << h.snrDb << " dB"
                                  << "  bw=" << (h.bandwidthHz / 1000.0) << " kHz"
                                  << "  peak=" << h.peakDb << " dB\n";
                    }
                }
            }
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
        if (remoteDiagnosticsEnabled()) {
            QJsonObject payload;
            payload["mode"] = "cli";
            payload["stage"] = "command-loop";
            payload["exceptionType"] = "std";
            payload["message"] = QString::fromLocal8Bit(ex.what()).left(500);
            remoteDiagnosticsSubmit("app.exception", "error", payload);
        }
        std::cout << "CLI error (see log): " << ex.what() << "\n";
        cliStop = true;
    } catch (...) {
        spdlog::error("Unknown exception in CLI command loop");
        if (remoteDiagnosticsEnabled()) {
            QJsonObject payload;
            payload["mode"] = "cli";
            payload["stage"] = "command-loop";
            payload["exceptionType"] = "unknown";
            remoteDiagnosticsSubmit("app.exception", "error", payload);
        }
        std::cout << "CLI unknown error\n";
        cliStop = true;
    }

    cliStop = true;
    {
        std::lock_guard<std::mutex> lk(cliRxMutex);
        for (auto& rxPtr : cliReceivers) {
            if (!rxPtr) continue;
            std::lock_guard<std::mutex> rxLock(rxPtr->stateMutex);
            rxPtr->active = false;
            rxPtr->p25VoiceDecodeEnabled = false;
        }
    }
    // Stop streams before joining the monitor thread so any in-flight device
    // wait/read path is released promptly during scripted CLI tests.
    for (size_t i=0; i<mgr.getDevices().size(); ++i) {
        if (mgr.isStreaming(i)) { try { mgr.stopStreaming(i); } catch(...) {} }
    }
    if (cliMonThread.joinable()) cliMonThread.join();
    spdlog::info("CLI exiting");
    remoteDiagnosticsShutdown();
    return 0;
}

