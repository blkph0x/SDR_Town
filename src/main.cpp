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
#include <QTextCursor>
#include <QTextDocument>
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
#include <QStringList>
#include <complex>
#include <array>
#include <cstddef>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <nlohmann/json.hpp>

#include "DeviceManager.h"
#include "SpectrumWidget.h"
#include "AudioEngine.h"
#include "Demod.h"
#include "P25Control.h"
#include "P25LiveDecoder.h"
#include "P25FollowStateMachine.h"
#include "SignalClassifier.h"
#include "ClassifierModelBackend.h"
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
#include <limits>
#include <map>
#include <unordered_map>
#include <mutex>
#include <optional>
#include <iterator>
#include <functional>
#include <memory>

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
static std::atomic<double> gLastRmsDb{-100.0};   // live RF signal level used by squelch calibration/Auto/indicator
static std::atomic<double> gLastNoiseFloorDb{-120.0};
static std::atomic<double> gLastSnrDb{0.0};
static std::atomic<double> gLastAfcOffsetHz{0.0};
static std::atomic<double> gLastAfcPpmDelta{std::numeric_limits<double>::quiet_NaN()};
static std::atomic<double> gLastAfcConfidence{0.0};
static std::atomic<double> gLastAfcBinHz{0.0};


enum class P25VoiceDiagCode : int {
    Idle = 0,
    SkippedEncrypted,
    WaitingForClearGrant,
    NoSync,
    NidUnlocked,
    BackendMissing,
    Phase2Unsupported,
    Phase2AudioLockMissing,
    Phase2MetadataMissing,
    Phase2MaskMissing,
    Phase2MaskAppliedNoMacCrc,
    Phase2EssMissing,
    Phase2WrongSlot,
    Phase2AmbeRejected,
    Phase2LateEntryWaiting,
    NoLduVoice,
    NoDecodedAudio,
    Decoding,
};

static_assert(static_cast<int>(P25VoiceDiagCode::Phase2WrongSlot) == static_cast<int>(P25FollowDiagCode::Phase2WrongSlot),
    "P25 follow state-machine diag codes must match receiver diagnostics.");
static_assert(static_cast<int>(P25VoiceDiagCode::Decoding) == static_cast<int>(P25FollowDiagCode::Decoding),
    "P25 follow state-machine diag codes must match receiver diagnostics.");

static const char* p25VoiceDiagLabel(P25VoiceDiagCode code)
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

struct RfSquelchMetrics {
    double signalLevelDb = -120.0;
    double noiseFloorDb = -120.0;
    double snrDb = 0.0;
    bool valid = false;
};

static std::string trimCopy(const std::string& s)
{
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

static std::string modeToString(DemodMode mode)
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

static QString modeToQString(DemodMode mode)
{
    return QString::fromStdString(modeToString(mode));
}

static DemodMode modeFromString(std::string text)
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

struct BandPlanEntry {
    std::string name;
    double startHz = 0.0;
    double endHz = 0.0;
    DemodMode mode = DemodMode::NFM;
    double bandwidthHz = 12500.0;
    double lpfHz = 3000.0;
    double stepHz = 12500.0;
};

static const std::vector<BandPlanEntry>& builtInBandPlans()
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

static const BandPlanEntry* findBandPlanForFrequency(double freqHz)
{
    if (!std::isfinite(freqHz)) return nullptr;
    for (const auto& p : builtInBandPlans()) {
        if (freqHz >= p.startHz && freqHz <= p.endHz) return &p;
    }
    return nullptr;
}

struct SavedFrequency {
    std::string name;
    double freqHz = 100e6;
    DemodMode mode = DemodMode::AUTO;
    double bandwidthHz = 180000.0;
    double lpfHz = 15000.0;
    bool lpfEnabled = true;
    double squelchDb = -105.0;
    std::string tags;
};

static QString savedFrequenciesPath()
{
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(appData);
    return appData + "/saved_frequencies.json";
}

static std::vector<SavedFrequency> loadSavedFrequencies()
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

static void saveSavedFrequencies(const std::vector<SavedFrequency>& freqs)
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

static void populateSavedFrequencyTable(QTableWidget* table, const std::vector<SavedFrequency>& freqs)
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

struct P25TalkgroupEntry {
    double controlFreqHz = 0.0;
    uint32_t talkgroupId = 0;
    std::string alphaTag;
    uint32_t lastSourceId = 0;
    uint16_t lastChannel = 0;
    double lastVoiceFreqHz = 0.0;
    P25VoiceProtocol voiceProtocol = P25VoiceProtocol::Unknown;
    bool phase2Candidate = false;
    uint8_t tdmaSlot = 0;
    bool tdmaSlotKnown = false;
    bool p25MaskParamsKnown = false;
    uint16_t nac = 0;
    uint32_t wacn = 0;
    uint16_t systemId = 0;
    uint8_t rfssId = 0;
    uint8_t siteId = 0;
    int hitCount = 0;
    bool encryptionKnown = false;
    bool encrypted = false;
    bool verified = false;
    bool scannerEnabled = false;
    qint64 firstSeenMs = 0;
    qint64 lastSeenMs = 0;
};

struct P25KnownControlChannel {
    double freqHz = 0.0;
    std::string label;
    qint64 createdMs = 0;
    qint64 lastUsedMs = 0;
};

struct P25CachedChannelIdentifier {
    double controlFreqHz = 0.0;
    uint16_t nac = 0;
    uint32_t wacn = 0;
    uint16_t systemId = 0;
    uint8_t rfssId = 0;
    uint8_t siteId = 0;
    P25ChannelIdentifier identifier;
    qint64 firstSeenMs = 0;
    qint64 lastSeenMs = 0;
};

struct P25PendingVoiceGrant {
    P25ControlEvent event;
    qint64 firstSeenMs = 0;
    qint64 lastSeenMs = 0;
    int correctedDibitErrors = 0;
};

static bool sameP25ControlFrequency(double a, double b)
{
    return std::isfinite(a) && std::isfinite(b) && std::abs(a - b) <= 50.0;
}

static QString p25TalkgroupsPath()
{
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(appData);
    return appData + "/p25_talkgroups.json";
}

static QString p25KnownControlChannelsPath()
{
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(appData);
    return appData + "/p25_control_channels.json";
}

static QString p25ChannelIdentifiersPath()
{
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(appData);
    return appData + "/p25_channel_identifiers.json";
}

static std::vector<P25KnownControlChannel> loadP25KnownControlChannels()
{
    std::vector<P25KnownControlChannel> out;
    std::ifstream f(p25KnownControlChannelsPath().toStdString());
    if (!f.is_open()) return out;
    try {
        json arr;
        f >> arr;
        if (!arr.is_array()) return out;
        for (const auto& item : arr) {
            P25KnownControlChannel cc;
            cc.freqHz = item.value("freqHz", 0.0);
            cc.label = item.value("label", std::string());
            cc.createdMs = item.value("createdMs", static_cast<qint64>(0));
            cc.lastUsedMs = item.value("lastUsedMs", static_cast<qint64>(0));
            if (std::isfinite(cc.freqHz) && cc.freqHz > 0.0) out.push_back(cc);
        }
    } catch (const std::exception& ex) {
        spdlog::warn("Failed to load p25_control_channels.json: {}", ex.what());
    }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
        return a.freqHz < b.freqHz;
    });
    return out;
}

static void saveP25KnownControlChannels(const std::vector<P25KnownControlChannel>& channels)
{
    json arr = json::array();
    for (const auto& cc : channels) {
        arr.push_back({
            {"freqHz", cc.freqHz},
            {"label", cc.label},
            {"createdMs", cc.createdMs},
            {"lastUsedMs", cc.lastUsedMs},
        });
    }
    try {
        std::ofstream f(p25KnownControlChannelsPath().toStdString());
        if (f.is_open()) f << arr.dump(2);
    } catch (const std::exception& ex) {
        spdlog::warn("Failed to save p25_control_channels.json: {}", ex.what());
    }
}

static bool upsertP25KnownControlChannel(double freqHz, std::string label)
{
    if (!std::isfinite(freqHz) || freqHz <= 0.0) return false;
    auto channels = loadP25KnownControlChannels();
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    auto it = std::find_if(channels.begin(), channels.end(), [&](const P25KnownControlChannel& cc) {
        return std::abs(cc.freqHz - freqHz) <= 50.0;
    });
    if (it == channels.end()) {
        P25KnownControlChannel cc;
        cc.freqHz = freqHz;
        cc.label = trimCopy(label);
        cc.createdMs = nowMs;
        cc.lastUsedMs = nowMs;
        channels.push_back(cc);
    } else {
        it->freqHz = freqHz;
        if (!trimCopy(label).empty()) it->label = trimCopy(label);
        it->lastUsedMs = nowMs;
    }
    std::sort(channels.begin(), channels.end(), [](const auto& a, const auto& b) {
        return a.freqHz < b.freqHz;
    });
    saveP25KnownControlChannels(channels);
    return true;
}

static bool p25ChannelIdentifierUsable(const P25ChannelIdentifier& identifier)
{
    return identifier.valid &&
        identifier.id < 16 &&
        std::isfinite(identifier.baseHz) &&
        std::isfinite(identifier.spacingHz) &&
        identifier.baseHz > 1e6 &&
        identifier.spacingHz > 0.0;
}

static P25ChannelIdentifier p25IdentifierFromEvent(const P25ControlEvent& event)
{
    P25ChannelIdentifier identifier;
    identifier.valid = event.type == P25ControlEventType::IdentifierUpdate &&
        event.identifierKnown &&
        event.baseFrequencyHz > 1e6 &&
        event.channelSpacingHz > 0.0;
    identifier.id = event.identifier;
    identifier.channelType = event.channelType;
    identifier.baseHz = event.baseFrequencyHz;
    identifier.spacingHz = event.channelSpacingHz;
    identifier.txOffsetHz = event.transmitOffsetHz;
    identifier.bandwidthHz = event.bandwidthHz;
    identifier.slotsPerCarrier = std::max(1, event.slotsPerCarrier);
    identifier.phase2Capable = event.phase2Candidate || identifier.slotsPerCarrier > 1;
    return identifier;
}

static void p25ApplyScopeFromEvent(P25CachedChannelIdentifier& rec, const P25ControlEvent& event)
{
    if (event.nacKnown) rec.nac = static_cast<uint16_t>(event.nac & 0x0fffu);
    if (event.networkStatusKnown) {
        if ((event.wacn & 0xfffffu) != 0) rec.wacn = event.wacn & 0xfffffu;
        if ((event.systemId & 0x0fffu) != 0) rec.systemId = static_cast<uint16_t>(event.systemId & 0x0fffu);
    }
    if (event.rfssStatusKnown) {
        if ((event.systemId & 0x0fffu) != 0) rec.systemId = static_cast<uint16_t>(event.systemId & 0x0fffu);
        if (event.rfssId != 0) rec.rfssId = event.rfssId;
        if (event.siteId != 0) rec.siteId = event.siteId;
    }
}

static bool p25CachedIdentifierScopeCompatible(const P25CachedChannelIdentifier& rec,
                                               const P25ControlEvent& event)
{
    if (event.nacKnown && rec.nac != 0 && rec.nac != static_cast<uint16_t>(event.nac & 0x0fffu)) return false;
    if (event.networkStatusKnown) {
        const uint32_t wacn = event.wacn & 0xfffffu;
        const uint16_t systemId = static_cast<uint16_t>(event.systemId & 0x0fffu);
        if (wacn != 0 && rec.wacn != 0 && rec.wacn != wacn) return false;
        if (systemId != 0 && rec.systemId != 0 && rec.systemId != systemId) return false;
    }
    if (event.rfssStatusKnown) {
        const uint16_t systemId = static_cast<uint16_t>(event.systemId & 0x0fffu);
        if (systemId != 0 && rec.systemId != 0 && rec.systemId != systemId) return false;
        if (event.rfssId != 0 && rec.rfssId != 0 && rec.rfssId != event.rfssId) return false;
        if (event.siteId != 0 && rec.siteId != 0 && rec.siteId != event.siteId) return false;
    }
    return true;
}

static bool p25TalkgroupScopeCompatible(const P25TalkgroupEntry& tg,
                                        const P25ControlEvent& event)
{
    if (event.nacKnown && tg.nac != 0 && tg.nac != static_cast<uint16_t>(event.nac & 0x0fffu)) return false;
    if (event.networkStatusKnown) {
        const uint32_t wacn = event.wacn & 0xfffffu;
        const uint16_t systemId = static_cast<uint16_t>(event.systemId & 0x0fffu);
        if (wacn != 0 && tg.wacn != 0 && tg.wacn != wacn) return false;
        if (systemId != 0 && tg.systemId != 0 && tg.systemId != systemId) return false;
    }
    if (event.rfssStatusKnown) {
        const uint16_t systemId = static_cast<uint16_t>(event.systemId & 0x0fffu);
        if (systemId != 0 && tg.systemId != 0 && tg.systemId != systemId) return false;
        if (event.rfssId != 0 && tg.rfssId != 0 && tg.rfssId != event.rfssId) return false;
        if (event.siteId != 0 && tg.siteId != 0 && tg.siteId != event.siteId) return false;
    }
    return true;
}

static std::vector<P25CachedChannelIdentifier> loadP25ChannelIdentifiers()
{
    std::vector<P25CachedChannelIdentifier> out;
    std::ifstream f(p25ChannelIdentifiersPath().toStdString());
    if (!f.is_open()) return out;
    try {
        json arr;
        f >> arr;
        if (!arr.is_array()) return out;
        for (const auto& item : arr) {
            P25CachedChannelIdentifier rec;
            rec.controlFreqHz = item.value("controlFreqHz", 0.0);
            rec.nac = static_cast<uint16_t>(item.value("nac", 0u) & 0x0fffu);
            rec.wacn = item.value("wacn", 0u) & 0xfffffu;
            rec.systemId = static_cast<uint16_t>(item.value("systemId", 0u) & 0x0fffu);
            rec.rfssId = static_cast<uint8_t>(item.value("rfssId", 0u) & 0xffu);
            rec.siteId = static_cast<uint8_t>(item.value("siteId", 0u) & 0xffu);
            rec.firstSeenMs = item.value("firstSeenMs", static_cast<qint64>(0));
            rec.lastSeenMs = item.value("lastSeenMs", static_cast<qint64>(0));
            const auto& planJson = item.contains("identifier") ? item.at("identifier") : item;
            rec.identifier.valid = planJson.value("valid", true);
            rec.identifier.id = static_cast<uint8_t>(planJson.value("id", 0u) & 0x0fu);
            rec.identifier.channelType = static_cast<uint8_t>(planJson.value("channelType", 0u) & 0xffu);
            rec.identifier.baseHz = planJson.value("baseHz", 0.0);
            rec.identifier.spacingHz = planJson.value("spacingHz", 0.0);
            rec.identifier.txOffsetHz = planJson.value("txOffsetHz", 0.0);
            rec.identifier.bandwidthHz = planJson.value("bandwidthHz", 0.0);
            rec.identifier.slotsPerCarrier = std::max(1, planJson.value("slotsPerCarrier", 1));
            rec.identifier.phase2Capable = planJson.value("phase2Capable", false) ||
                rec.identifier.slotsPerCarrier > 1;
            if (std::isfinite(rec.controlFreqHz) && rec.controlFreqHz > 0.0 &&
                p25ChannelIdentifierUsable(rec.identifier)) {
                out.push_back(rec);
            }
        }
    } catch (const std::exception& ex) {
        spdlog::warn("Failed to load p25_channel_identifiers.json: {}", ex.what());
    }
    return out;
}

static void saveP25ChannelIdentifiers(std::vector<P25CachedChannelIdentifier> identifiers)
{
    identifiers.erase(std::remove_if(identifiers.begin(), identifiers.end(), [](const auto& rec) {
        return !std::isfinite(rec.controlFreqHz) || rec.controlFreqHz <= 0.0 ||
            !p25ChannelIdentifierUsable(rec.identifier);
    }), identifiers.end());
    std::sort(identifiers.begin(), identifiers.end(), [](const auto& a, const auto& b) {
        if (std::abs(a.controlFreqHz - b.controlFreqHz) > 50.0) return a.controlFreqHz < b.controlFreqHz;
        if (a.nac != b.nac) return a.nac < b.nac;
        if (a.wacn != b.wacn) return a.wacn < b.wacn;
        if (a.systemId != b.systemId) return a.systemId < b.systemId;
        if (a.rfssId != b.rfssId) return a.rfssId < b.rfssId;
        if (a.siteId != b.siteId) return a.siteId < b.siteId;
        return a.identifier.id < b.identifier.id;
    });
    json arr = json::array();
    for (const auto& rec : identifiers) {
        arr.push_back({
            {"controlFreqHz", rec.controlFreqHz},
            {"nac", rec.nac},
            {"wacn", rec.wacn},
            {"systemId", rec.systemId},
            {"rfssId", rec.rfssId},
            {"siteId", rec.siteId},
            {"firstSeenMs", rec.firstSeenMs},
            {"lastSeenMs", rec.lastSeenMs},
            {"identifier", {
                {"valid", rec.identifier.valid},
                {"id", rec.identifier.id},
                {"channelType", rec.identifier.channelType},
                {"baseHz", rec.identifier.baseHz},
                {"spacingHz", rec.identifier.spacingHz},
                {"txOffsetHz", rec.identifier.txOffsetHz},
                {"bandwidthHz", rec.identifier.bandwidthHz},
                {"slotsPerCarrier", rec.identifier.slotsPerCarrier},
                {"phase2Capable", rec.identifier.phase2Capable},
            }},
        });
    }
    try {
        std::ofstream f(p25ChannelIdentifiersPath().toStdString());
        if (f.is_open()) f << arr.dump(2);
    } catch (const std::exception& ex) {
        spdlog::warn("Failed to save p25_channel_identifiers.json: {}", ex.what());
    }
}

static bool upsertP25ChannelIdentifier(double controlFreqHz,
                                       const P25ControlEvent& event,
                                       qint64 nowMs = QDateTime::currentMSecsSinceEpoch())
{
    if (!std::isfinite(controlFreqHz) || controlFreqHz <= 0.0) return false;
    P25ChannelIdentifier identifier = p25IdentifierFromEvent(event);
    if (!p25ChannelIdentifierUsable(identifier)) return false;

    auto identifiers = loadP25ChannelIdentifiers();
    auto it = std::find_if(identifiers.begin(), identifiers.end(), [&](const auto& rec) {
        return sameP25ControlFrequency(rec.controlFreqHz, controlFreqHz) &&
            rec.identifier.id == identifier.id &&
            p25CachedIdentifierScopeCompatible(rec, event);
    });
    if (it == identifiers.end()) {
        P25CachedChannelIdentifier rec;
        rec.controlFreqHz = controlFreqHz;
        p25ApplyScopeFromEvent(rec, event);
        rec.identifier = identifier;
        rec.firstSeenMs = nowMs;
        rec.lastSeenMs = nowMs;
        identifiers.push_back(rec);
    } else {
        it->controlFreqHz = controlFreqHz;
        p25ApplyScopeFromEvent(*it, event);
        it->identifier = identifier;
        if (it->firstSeenMs <= 0) it->firstSeenMs = nowMs;
        it->lastSeenMs = nowMs;
    }
    saveP25ChannelIdentifiers(std::move(identifiers));
    return true;
}

static size_t seedP25AnalyzerFromCachedChannelIdentifiers(P25ControlChannelAnalyzer& analyzer,
                                                          double controlFreqHz)
{
    size_t count = 0;
    for (const auto& rec : loadP25ChannelIdentifiers()) {
        if (!sameP25ControlFrequency(rec.controlFreqHz, controlFreqHz)) continue;
        analyzer.setChannelIdentifier(rec.identifier);
        ++count;
    }
    return count;
}

static std::string p25VoiceProtocolStorage(P25VoiceProtocol protocol)
{
    switch (protocol) {
        case P25VoiceProtocol::Phase1FDMA: return "phase1_fdma";
        case P25VoiceProtocol::Phase2TDMA: return "phase2_tdma";
        case P25VoiceProtocol::Unknown:
        default: return "unknown";
    }
}

static P25VoiceProtocol p25VoiceProtocolFromStorage(std::string text)
{
    for (char& c : text) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (c == '-' || c == ' ') c = '_';
    }
    if (text == "phase1" || text == "phase1_fdma" || text == "p25_phase_1_fdma") return P25VoiceProtocol::Phase1FDMA;
    if (text == "phase2" || text == "phase2_tdma" || text == "p25_phase_2_tdma") return P25VoiceProtocol::Phase2TDMA;
    return P25VoiceProtocol::Unknown;
}

static QString p25VoiceProtocolShort(P25VoiceProtocol protocol)
{
    switch (protocol) {
        case P25VoiceProtocol::Phase1FDMA: return "P1";
        case P25VoiceProtocol::Phase2TDMA: return "P2";
        case P25VoiceProtocol::Unknown:
        default: return "-";
    }
}

static bool p25TalkgroupIsPhase2(const P25TalkgroupEntry& tg)
{
    return tg.voiceProtocol == P25VoiceProtocol::Phase2TDMA || tg.phase2Candidate || tg.tdmaSlotKnown;
}

static bool p25TalkgroupCanTuneForFollow(const P25TalkgroupEntry& tg)
{
    if (tg.encryptionKnown) return !tg.encrypted;
    // Phase 2 grant-update messages often provide TG/frequency/slot but not
    // service options. Tune so ESS/MAC can prove clear or encrypted on-voice.
    return p25TalkgroupIsPhase2(tg);
}

static bool p25TalkgroupHasUsableMaskMetadata(const P25TalkgroupEntry& tg)
{
    return tg.p25MaskParamsKnown &&
        tg.nac != 0 &&
        tg.wacn != 0 &&
        tg.systemId != 0;
}

static bool p25ApplyControlEventSystemMetadata(P25TalkgroupEntry& tg,
                                               const P25ControlEvent& event)
{
    bool changed = false;
    if (event.nacKnown) {
        const uint16_t nac = static_cast<uint16_t>(event.nac & 0x0fffu);
        if (tg.nac != nac) {
            tg.nac = nac;
            changed = true;
        }
    }
    if (event.networkStatusKnown) {
        const uint32_t wacn = event.wacn & 0xfffffu;
        const uint16_t systemId = static_cast<uint16_t>(event.systemId & 0x0fffu);
        if (wacn != 0 && tg.wacn != wacn) {
            tg.wacn = wacn;
            changed = true;
        }
        if (systemId != 0 && tg.systemId != systemId) {
            tg.systemId = systemId;
            changed = true;
        }
    }
    if (event.rfssStatusKnown) {
        const uint16_t systemId = static_cast<uint16_t>(event.systemId & 0x0fffu);
        if (systemId != 0 && tg.systemId != systemId) {
            tg.systemId = systemId;
            changed = true;
        }
        if (tg.rfssId != event.rfssId) {
            tg.rfssId = event.rfssId;
            changed = true;
        }
        if (tg.siteId != event.siteId) {
            tg.siteId = event.siteId;
            changed = true;
        }
    }
    const bool canMask = tg.nac != 0 && tg.wacn != 0 && tg.systemId != 0;
    if (canMask && !tg.p25MaskParamsKnown) {
        tg.p25MaskParamsKnown = true;
        changed = true;
    }
    return changed;
}

static bool p25CopySiteMetadata(P25TalkgroupEntry& dst,
                                const P25TalkgroupEntry& src,
                                bool requireMissingMask)
{
    if (!p25TalkgroupHasUsableMaskMetadata(src)) return false;
    if (requireMissingMask && p25TalkgroupHasUsableMaskMetadata(dst)) return false;

    bool changed = false;
    if (!p25TalkgroupHasUsableMaskMetadata(dst)) {
        if (dst.nac != src.nac) {
            dst.nac = src.nac;
            changed = true;
        }
        if (dst.wacn != src.wacn) {
            dst.wacn = src.wacn;
            changed = true;
        }
        if (dst.systemId != src.systemId) {
            dst.systemId = src.systemId;
            changed = true;
        }
        if (!dst.p25MaskParamsKnown) {
            dst.p25MaskParamsKnown = true;
            changed = true;
        }
    }
    if (dst.rfssId == 0 && src.rfssId != 0) {
        dst.rfssId = src.rfssId;
        changed = true;
    }
    if (dst.siteId == 0 && src.siteId != 0) {
        dst.siteId = src.siteId;
        changed = true;
    }
    return changed;
}

static bool p25AugmentTalkgroupFromKnownSite(P25TalkgroupEntry& tg,
                                             const std::vector<P25TalkgroupEntry>& talkgroups,
                                             double controlFreqHz)
{
    const double ccHz = std::isfinite(controlFreqHz) && controlFreqHz > 0.0
        ? controlFreqHz
        : tg.controlFreqHz;
    if (!std::isfinite(ccHz) || ccHz <= 0.0) return false;
    if (p25TalkgroupHasUsableMaskMetadata(tg) && tg.rfssId != 0 && tg.siteId != 0) return false;

    const P25TalkgroupEntry* best = nullptr;
    int bestScore = -1;
    qint64 bestLastSeen = std::numeric_limits<qint64>::min();
    for (const auto& candidate : talkgroups) {
        if (!sameP25ControlFrequency(candidate.controlFreqHz, ccHz)) continue;
        if (!p25TalkgroupHasUsableMaskMetadata(candidate)) continue;

        int score = 0;
        if (candidate.talkgroupId == tg.talkgroupId) score += 100;
        if (tg.rfssId != 0 && candidate.rfssId == tg.rfssId) score += 20;
        if (tg.siteId != 0 && candidate.siteId == tg.siteId) score += 20;
        if (candidate.rfssId != 0 && candidate.siteId != 0) score += 5;
        if (!p25TalkgroupHasUsableMaskMetadata(tg)) score += 10;

        if (!best || score > bestScore ||
            (score == bestScore && candidate.lastSeenMs > bestLastSeen)) {
            best = &candidate;
            bestScore = score;
            bestLastSeen = candidate.lastSeenMs;
        }
    }
    return best ? p25CopySiteMetadata(tg, *best, false) : false;
}

static std::vector<P25TalkgroupEntry> loadP25Talkgroups()
{
    std::vector<P25TalkgroupEntry> out;
    std::ifstream f(p25TalkgroupsPath().toStdString());
    if (!f.is_open()) return out;
    try {
        json arr;
        f >> arr;
        if (!arr.is_array()) return out;
        for (const auto& item : arr) {
            P25TalkgroupEntry tg;
            tg.controlFreqHz = item.value("controlFreqHz", 0.0);
            tg.talkgroupId = item.value("talkgroupId", 0u);
            tg.alphaTag = item.value("alphaTag", std::string());
            tg.lastSourceId = item.value("lastSourceId", 0u);
            tg.lastChannel = item.value("lastChannel", 0u);
            tg.lastVoiceFreqHz = item.value("lastVoiceFreqHz", 0.0);
            tg.voiceProtocol = p25VoiceProtocolFromStorage(item.value("voiceProtocol", std::string("unknown")));
            tg.phase2Candidate = item.value("phase2Candidate", false);
            tg.tdmaSlot = static_cast<uint8_t>(item.value("tdmaSlot", 0u) & 0xffu);
            tg.tdmaSlotKnown = item.value("tdmaSlotKnown", false);
            tg.p25MaskParamsKnown = item.value("p25MaskParamsKnown", false);
            tg.nac = static_cast<uint16_t>(item.value("nac", 0u) & 0x0fffu);
            tg.wacn = item.value("wacn", 0u);
            tg.systemId = static_cast<uint16_t>(item.value("systemId", 0u) & 0x0fffu);
            tg.rfssId = static_cast<uint8_t>(item.value("rfssId", 0u) & 0xffu);
            tg.siteId = static_cast<uint8_t>(item.value("siteId", 0u) & 0xffu);
            tg.hitCount = item.value("hitCount", 0);
            tg.encryptionKnown = item.value("encryptionKnown", false);
            tg.encrypted = item.value("encrypted", false);
            tg.verified = item.value("verified", false);
            tg.scannerEnabled = item.value("scannerEnabled", false);
            tg.firstSeenMs = item.value("firstSeenMs", static_cast<qint64>(0));
            tg.lastSeenMs = item.value("lastSeenMs", static_cast<qint64>(0));
            if (tg.talkgroupId > 0 && std::isfinite(tg.controlFreqHz) && tg.controlFreqHz > 0.0) {
                out.push_back(tg);
            }
        }
    } catch (const std::exception& ex) {
        spdlog::warn("Failed to load p25_talkgroups.json: {}", ex.what());
    }
    return out;
}

static void saveP25Talkgroups(const std::vector<P25TalkgroupEntry>& talkgroups)
{
    json arr = json::array();
    for (const auto& tg : talkgroups) {
        arr.push_back({
            {"controlFreqHz", tg.controlFreqHz},
            {"talkgroupId", tg.talkgroupId},
            {"alphaTag", tg.alphaTag},
            {"lastSourceId", tg.lastSourceId},
            {"lastChannel", tg.lastChannel},
            {"lastVoiceFreqHz", tg.lastVoiceFreqHz},
            {"voiceProtocol", p25VoiceProtocolStorage(tg.voiceProtocol)},
            {"phase2Candidate", tg.phase2Candidate},
            {"tdmaSlot", tg.tdmaSlot},
            {"tdmaSlotKnown", tg.tdmaSlotKnown},
            {"p25MaskParamsKnown", tg.p25MaskParamsKnown},
            {"nac", tg.nac},
            {"wacn", tg.wacn},
            {"systemId", tg.systemId},
            {"rfssId", tg.rfssId},
            {"siteId", tg.siteId},
            {"hitCount", tg.hitCount},
            {"encryptionKnown", tg.encryptionKnown},
            {"encrypted", tg.encrypted},
            {"verified", tg.verified},
            {"scannerEnabled", tg.scannerEnabled},
            {"firstSeenMs", tg.firstSeenMs},
            {"lastSeenMs", tg.lastSeenMs},
        });
    }
    try {
        std::ofstream f(p25TalkgroupsPath().toStdString());
        if (f.is_open()) f << arr.dump(2);
    } catch (const std::exception& ex) {
        spdlog::warn("Failed to save p25_talkgroups.json: {}", ex.what());
    }
}

static QString p25TimeText(qint64 ms)
{
    if (ms <= 0) return "-";
    return QDateTime::fromMSecsSinceEpoch(ms).toString("yyyy-MM-dd HH:mm:ss");
}

static QString p25HexId(uint32_t value, int width)
{
    return QString("0x%1").arg(value, width, 16, QLatin1Char('0')).toUpper();
}

static QString p25BytesToHex(const std::vector<uint8_t>& bytes)
{
    QStringList parts;
    for (uint8_t byte : bytes) {
        parts << QString("%1").arg(byte, 2, 16, QLatin1Char('0')).toUpper();
    }
    return parts.join(' ');
}

static QString p25MessageIndicatorText(const std::array<uint8_t, 9>& bytes)
{
    QStringList parts;
    for (uint8_t byte : bytes) {
        parts << QString("%1").arg(byte, 2, 16, QLatin1Char('0')).toUpper();
    }
    return parts.join(QString());
}

static QString p25Phase2AcchStatsText(const P25LiveDecoderStats& stats)
{
    return QString("p2acch=nom:%1 altKind:%2 swap:%3 slip:%4 inv:%5")
        .arg(static_cast<qulonglong>(stats.phase2MacNominalCrcValid))
        .arg(static_cast<qulonglong>(stats.phase2MacAltKindCrcValid))
        .arg(static_cast<qulonglong>(stats.phase2MacBitSwapCrcValid))
        .arg(static_cast<qulonglong>(stats.phase2MacSlipCrcValid))
        .arg(static_cast<qulonglong>(stats.phase2MacInvertCrcValid));
}

static QString p25Phase2AcchStatsText(const P25VoiceDiagSnapshot& diag)
{
    return QString("p2acch=nom:%1 altKind:%2 swap:%3 slip:%4 inv:%5")
        .arg(diag.phase2MacNominalCrcValid)
        .arg(diag.phase2MacAltKindCrcValid)
        .arg(diag.phase2MacBitSwapCrcValid)
        .arg(diag.phase2MacSlipCrcValid)
        .arg(diag.phase2MacInvertCrcValid);
}

static QString p25Phase2MacPduHypothesisText(const P25Phase2MacPdu& pdu)
{
    QStringList parts;
    parts << QString("detected=%1").arg(QString::fromStdString(P25LiveDecoder::phase2BurstKindToString(pdu.detectedKind)));
    parts << QString("attempt=%1").arg(QString::fromStdString(P25LiveDecoder::phase2BurstKindToString(pdu.source)));
    if (pdu.acchHypothesisKnown) {
        parts << QString("swap=%1").arg(pdu.acchBitOrderSwapped ? "yes" : "no");
        parts << QString("invert=%1").arg(pdu.acchDibitInverted ? "yes" : "no");
        parts << QString("slip=%1").arg(pdu.acchSlipDibits);
    } else {
        parts << "hyp=unknown";
    }
    return parts.join(' ');
}

static QString p25ChannelText(uint16_t channel)
{
    return QString("0x%1").arg(channel, 4, 16, QLatin1Char('0')).toUpper();
}

static constexpr int kP25RegistryMaxCorrectedDibits = 10;
static constexpr int kP25VoiceGrantMaxCorrectedDibits = 20;

static QString p25EventLogText(const P25ControlEvent& ev)
{
    QStringList parts;
    const bool identifierUpdate = ev.type == P25ControlEventType::IdentifierUpdate;
    parts << QString::fromStdString(p25ControlEventTypeToString(ev.type));
    parts << QString("op=0x%1").arg(ev.opcode, 2, 16, QLatin1Char('0')).toUpper();
    parts << QString("mfid=0x%1").arg(ev.mfid, 2, 16, QLatin1Char('0')).toUpper();
    if (!ev.label.empty()) parts << QString::fromStdString(ev.label);
    if (ev.phase2Mac) {
        parts << QString("macPdu=%1").arg(QString::fromStdString(p25Phase2MacPduTypeToString(ev.macPduType)));
        parts << QString("macPduType=0x%1").arg(ev.macPduType, 1, 16, QLatin1Char('0')).toUpper();
        parts << QString("macOff=%1").arg(static_cast<int>(ev.macPduOffset));
        parts << QString("macMsg=0x%1").arg(ev.macMessageOpcode, 2, 16, QLatin1Char('0')).toUpper();
        parts << QString("macByte=%1").arg(static_cast<qulonglong>(ev.macMessageOffset));
    }
    if (ev.identifierKnown) parts << QString("id=%1").arg(static_cast<int>(ev.identifier));
    if (ev.channelType) parts << QString("type=%1").arg(static_cast<int>(ev.channelType));
    if (ev.slotsPerCarrier > 1) parts << QString("tdma-slots=%1").arg(ev.slotsPerCarrier);
    if (ev.baseFrequencyHz > 0.0) parts << QString("base=%1MHz").arg(ev.baseFrequencyHz / 1e6, 0, 'f', 5);
    if (ev.channelSpacingHz > 0.0) parts << QString("step=%1kHz").arg(ev.channelSpacingHz / 1000.0, 0, 'f', 3);
    if (ev.talkgroupId) parts << QString("tg=%1").arg(ev.talkgroupId);
    if (ev.sourceId) parts << QString("src=%1").arg(p25HexId(ev.sourceId, 6));
    if (ev.channel || (identifierUpdate && ev.identifierKnown)) {
        parts << QString(identifierUpdate ? "table=%1" : "ch=%1").arg(p25ChannelText(ev.channel));
    }
    if (ev.channelB) parts << QString("chB=%1").arg(p25ChannelText(ev.channelB));
    if (ev.explicitChannelKnown && ev.explicitChannel.valid) {
        parts << QString("protoTx=%1").arg(p25ChannelText(ev.explicitChannel.protocolTransmitChannel));
        parts << QString("protoRx=%1").arg(p25ChannelText(ev.explicitChannel.protocolReceiveChannel));
        parts << QString("scannerDownlink=%1").arg(p25ChannelText(ev.explicitChannel.scannerDownlinkChannel));
        parts << QString("subscriberUplink=%1").arg(p25ChannelText(ev.explicitChannel.subscriberUplinkChannel));
        parts << QString("downlink=%1").arg(p25ChannelText(ev.explicitChannel.downlinkChannel));
        parts << QString("uplink=%1").arg(p25ChannelText(ev.explicitChannel.uplinkChannel));
        if (ev.explicitChannel.downlinkHz > 0.0) {
            parts << QString("downlinkFreq=%1MHz").arg(ev.explicitChannel.downlinkHz / 1e6, 0, 'f', 5);
        }
        if (ev.explicitChannel.uplinkHz > 0.0) {
            parts << QString("uplinkFreq=%1MHz").arg(ev.explicitChannel.uplinkHz / 1e6, 0, 'f', 5);
        }
    }
    if (!identifierUpdate && ev.channel && ev.identifierKnown && ev.channelSpacingHz > 0.0 && ev.baseFrequencyHz > 0.0) {
        const int slotCount = std::max(1, ev.slotsPerCarrier);
        const uint16_t rawNumber = static_cast<uint16_t>(ev.channel & 0x0fffu);
        const uint16_t carrierNumber = static_cast<uint16_t>(rawNumber / static_cast<uint16_t>(slotCount));
        parts << QString("rawNumber=0x%1").arg(rawNumber, 3, 16, QLatin1Char('0')).toUpper();
        parts << QString("carrier=%1").arg(carrierNumber);
        parts << QString("calcSlot=%1").arg(rawNumber % static_cast<uint16_t>(slotCount));
        parts << QString("calcBase=%1MHz").arg(ev.baseFrequencyHz / 1e6, 0, 'f', 5);
        parts << QString("calcStep=%1k").arg(ev.channelSpacingHz / 1000.0, 0, 'f', 3);
    }
    if (ev.nacKnown) parts << QString("nac=%1").arg(p25HexId(ev.nac, 3));
    if (ev.networkStatusKnown) parts << QString("wacn=%1").arg(p25HexId(ev.wacn, 5));
    if (ev.networkStatusKnown || ev.rfssStatusKnown) parts << QString("sys=%1").arg(p25HexId(ev.systemId, 3));
    if (ev.rfssStatusKnown) {
        parts << QString("rfss=%1").arg(static_cast<int>(ev.rfssId));
        parts << QString("site=%1").arg(static_cast<int>(ev.siteId));
    }
    if (ev.networkStatusKnown || ev.rfssStatusKnown) parts << QString("lra=%1").arg(p25HexId(ev.lra, 2));
    if (ev.rfssStatusKnown && ev.rfssNetworkActiveKnown) parts << (ev.rfssNetworkActive ? "rfss-active" : "rfss-failsoft");
    if (ev.controlChannel) parts << QString("cc=%1").arg(p25ChannelText(ev.controlChannel));
    if (ev.controlChannelB) parts << QString("ccB=%1").arg(p25ChannelText(ev.controlChannelB));
    if (ev.voiceFrequencyHz > 0.0) parts << QString("voice=%1MHz").arg(ev.voiceFrequencyHz / 1e6, 0, 'f', 5);
    if (ev.voiceFrequencyHzB > 0.0) parts << QString("voiceB=%1MHz").arg(ev.voiceFrequencyHzB / 1e6, 0, 'f', 5);
    if (ev.channelFrequencyHz > 0.0) parts << QString("freq=%1MHz").arg(ev.channelFrequencyHz / 1e6, 0, 'f', 5);
    if (ev.channelFrequencyHzB > 0.0) parts << QString("freqB=%1MHz").arg(ev.channelFrequencyHzB / 1e6, 0, 'f', 5);
    if (ev.controlChannelFrequencyHz > 0.0) parts << QString("ccFreq=%1MHz").arg(ev.controlChannelFrequencyHz / 1e6, 0, 'f', 5);
    if (ev.controlChannelFrequencyHzB > 0.0) parts << QString("ccFreqB=%1MHz").arg(ev.controlChannelFrequencyHzB / 1e6, 0, 'f', 5);
    if (ev.serviceOptionsKnown) {
        parts << QString("svc=0x%1").arg(ev.serviceOptions, 2, 16, QLatin1Char('0')).toUpper();
        if (ev.serviceEmergency) parts << "emergency";
        if (ev.serviceDuplexFull) parts << "duplex=full";
        if (ev.servicePacketMode) parts << "service=packet";
        parts << QString("priority=%1").arg(static_cast<int>(ev.servicePriority));
    }
    if (ev.pttEncryptionSyncKnown) {
        parts << QString("mi=%1").arg(p25MessageIndicatorText(ev.messageIndicator));
        parts << QString("alg=0x%1").arg(ev.algorithmId, 2, 16, QLatin1Char('0')).toUpper();
        parts << QString("kid=0x%1").arg(ev.keyId, 4, 16, QLatin1Char('0')).toUpper();
    }
    if (ev.endPttNacKnown) parts << QString("endNac=%1").arg(p25HexId(ev.endPttNac, 3));
    if (ev.encryptionKnown) parts << (ev.encrypted ? "encrypted" : "clear");
    if (ev.voiceProtocol != P25VoiceProtocol::Unknown) {
        parts << QString::fromStdString(p25VoiceProtocolToString(ev.voiceProtocol));
    }
    if (ev.tdmaSlotKnown) parts << QString("slot=%1").arg(static_cast<int>(ev.tdmaSlot));
    if (ev.phase2Candidate) parts << "phase2-candidate";
    return parts.join(" | ");
}

static QString p25GrantDetailLogText(const P25ControlEvent& ev)
{
    QStringList parts;
    const uint8_t identifier = static_cast<uint8_t>((ev.channel >> 12) & 0x0f);
    const uint16_t rawNumber = static_cast<uint16_t>(ev.channel & 0x0fffu);
    const int slotCount = std::max(1, ev.slotsPerCarrier);
    const uint16_t carrierNumber = static_cast<uint16_t>(rawNumber / static_cast<uint16_t>(slotCount));
    const uint16_t slot = static_cast<uint16_t>(rawNumber % static_cast<uint16_t>(slotCount));
    parts << QString("Grant: TG=%1").arg(ev.talkgroupId);
    if (ev.sourceId) parts << QString("SRC=%1").arg(p25HexId(ev.sourceId, 6));
    parts << QString("CH=%1").arg(p25ChannelText(ev.channel));
    parts << QString("ID=%1").arg(static_cast<int>(identifier));
    parts << QString("CHAN=0x%1").arg(rawNumber, 3, 16, QLatin1Char('0')).toUpper();
    parts << QString("CARRIER=%1").arg(carrierNumber);
    if (ev.tdmaSlotKnown || slotCount > 1) {
        parts << QString("SLOT=%1").arg(ev.tdmaSlotKnown ? static_cast<int>(ev.tdmaSlot) : static_cast<int>(slot));
    }
    if (ev.explicitChannelKnown && ev.explicitChannel.valid) {
        parts << QString("PROTO_TX=%1").arg(p25ChannelText(ev.explicitChannel.protocolTransmitChannel));
        parts << QString("PROTO_RX=%1").arg(p25ChannelText(ev.explicitChannel.protocolReceiveChannel));
        parts << QString("DOWNLINK=%1").arg(p25ChannelText(ev.explicitChannel.downlinkChannel));
        parts << QString("UPLINK=%1").arg(p25ChannelText(ev.explicitChannel.uplinkChannel));
    }
    if (ev.voiceFrequencyHz > 0.0) parts << QString("FREQ=%1MHz").arg(ev.voiceFrequencyHz / 1e6, 0, 'f', 5);
    else parts << "FREQ=pending";
    if (ev.voiceFrequencyHzB > 0.0) parts << QString("FREQ_B=%1MHz").arg(ev.voiceFrequencyHzB / 1e6, 0, 'f', 5);
    parts << QString("PHASE2=%1").arg(ev.phase2Candidate ? "yes" : "no");
    parts << QString("ENC=%1").arg(ev.encryptionKnown ? (ev.encrypted ? "encrypted" : "clear") : "unknown");
    if (ev.serviceOptionsKnown) {
        parts << QString("SVC=0x%1").arg(ev.serviceOptions, 2, 16, QLatin1Char('0')).toUpper();
        if (ev.serviceEmergency) parts << "EMERGENCY";
        if (ev.serviceDuplexFull) parts << "DUPLEX=full";
        if (ev.servicePacketMode) parts << "SERVICE=packet";
        parts << QString("PRI=%1").arg(static_cast<int>(ev.servicePriority));
    }
    if (ev.pttEncryptionSyncKnown) {
        parts << QString("ALG=0x%1").arg(ev.algorithmId, 2, 16, QLatin1Char('0')).toUpper();
        parts << QString("KID=0x%1").arg(ev.keyId, 4, 16, QLatin1Char('0')).toUpper();
    }
    if (ev.endPttNacKnown) parts << QString("END_NAC=%1").arg(p25HexId(ev.endPttNac, 3));
    parts << QString("OP=0x%1").arg(ev.opcode, 2, 16, QLatin1Char('0')).toUpper();
    if (ev.phase2Mac) {
        parts << QString("MACPDU=%1").arg(QString::fromStdString(p25Phase2MacPduTypeToString(ev.macPduType)));
        parts << QString("MACMSG=0x%1").arg(ev.macMessageOpcode, 2, 16, QLatin1Char('0')).toUpper();
    }
    parts << QString("MFID=0x%1").arg(ev.mfid, 2, 16, QLatin1Char('0')).toUpper();
    return parts.join(' ');
}

static QString p25FollowDetailLogText(const P25TalkgroupEntry& tg)
{
    const bool phase2 = p25TalkgroupIsPhase2(tg);
    QString text = QString("Following %1: freq=%2MHz tg=%3 src=%4 control=%5MHz enc=%6")
        .arg(phase2 ? "Phase 2" : "Phase 1")
        .arg(tg.lastVoiceFreqHz / 1e6, 0, 'f', 5)
        .arg(tg.talkgroupId)
        .arg(tg.lastSourceId ? p25HexId(tg.lastSourceId, 6) : QString("-"))
        .arg(tg.controlFreqHz / 1e6, 0, 'f', 5)
        .arg(tg.encryptionKnown ? (tg.encrypted ? "encrypted" : "clear") : "unknown");
    if (phase2) {
        text += QString(" slot=%1 mask=%2. TDMA sync: searching; MAC/ESS: pending.")
            .arg(tg.tdmaSlotKnown ? QString::number(static_cast<int>(tg.tdmaSlot)) : QString("unknown"))
            .arg(tg.p25MaskParamsKnown ? "known" : "unknown");
    } else {
        text += ". NID/IMBE sync: searching.";
    }
    return text;
}

static void populateP25TalkgroupTable(QTableWidget* table, const std::vector<P25TalkgroupEntry>& talkgroups)
{
    if (!table) return;
    table->setRowCount(static_cast<int>(talkgroups.size()));
    for (int row = 0; row < static_cast<int>(talkgroups.size()); ++row) {
        const auto& tg = talkgroups[static_cast<size_t>(row)];
        QString status = tg.scannerEnabled ? "Scanner"
                       : tg.verified ? "Verified"
                       : "Discovered";
        QString protocol = p25TalkgroupIsPhase2(tg) ? "P2" : p25VoiceProtocolShort(tg.voiceProtocol);
        if (tg.tdmaSlotKnown) protocol += QString(" S%1").arg(static_cast<int>(tg.tdmaSlot));
        if (p25TalkgroupIsPhase2(tg) && tg.p25MaskParamsKnown) protocol += " Meta";
        if (protocol != "-") status = protocol + " / " + status;
        table->setItem(row, 0, new QTableWidgetItem(QString::number(tg.controlFreqHz / 1e6, 'f', 5)));
        table->setItem(row, 1, new QTableWidgetItem(QString::number(tg.talkgroupId)));
        table->setItem(row, 2, new QTableWidgetItem(QString::fromStdString(tg.alphaTag)));
        table->setItem(row, 3, new QTableWidgetItem(tg.lastVoiceFreqHz > 0.0 ? QString::number(tg.lastVoiceFreqHz / 1e6, 'f', 5) : "-"));
        table->setItem(row, 4, new QTableWidgetItem(tg.lastSourceId ? p25HexId(tg.lastSourceId, 6) : "-"));
        table->setItem(row, 5, new QTableWidgetItem(QString::number(tg.hitCount)));
        table->setItem(row, 6, new QTableWidgetItem(tg.encryptionKnown ? (tg.encrypted ? "Yes" : "No") : "Unknown"));
        table->setItem(row, 7, new QTableWidgetItem(status));
        table->setItem(row, 8, new QTableWidgetItem(p25TimeText(tg.lastSeenMs)));
    }
}

static bool sameP25Talkgroup(const P25TalkgroupEntry& tg, double controlFreqHz, uint32_t talkgroupId)
{
    return tg.talkgroupId == talkgroupId && std::abs(tg.controlFreqHz - controlFreqHz) <= 50.0;
}

static bool p25ControlEventHasTalkgroupActivity(const P25ControlEvent& event)
{
    return event.talkgroupId != 0 &&
        (p25ControlEventIsVoiceGrant(event) ||
         event.type == P25ControlEventType::GroupVoiceUser ||
         event.type == P25ControlEventType::GroupVoiceEnd);
}

static bool p25ControlEventHasResolvedVoiceFrequency(const P25ControlEvent& event)
{
    return event.voiceFrequencyHz > 0.0;
}

static bool p25ControlEventIsResolvedVoiceGrant(const P25ControlEvent& event)
{
    return p25ControlEventIsVoiceGrant(event) &&
        event.talkgroupId != 0 &&
        p25ControlEventHasResolvedVoiceFrequency(event) &&
        std::isfinite(event.voiceFrequencyHz) &&
        event.voiceFrequencyHz > 1e6;
}

static bool p25ResolveVoiceGrantFromAnalyzer(P25ControlEvent& event,
                                             const P25ControlChannelAnalyzer& analyzer)
{
    if (!p25ControlEventIsVoiceGrant(event) || event.channel == 0) return false;
    auto freq = analyzer.channelToFrequencyHz(event.channel);
    if (!freq.has_value() || !std::isfinite(*freq) || *freq <= 1e6) return false;

    const uint8_t id = static_cast<uint8_t>((event.channel >> 12) & 0x0f);
    const auto& identifiers = analyzer.channelIdentifiers();
    if (id >= identifiers.size() || !p25ChannelIdentifierUsable(identifiers[id])) return false;
    const auto& plan = identifiers[id];
    event.voiceFrequencyHz = *freq;
    event.identifierKnown = true;
    event.identifier = id;
    event.channelType = plan.channelType;
    event.slotsPerCarrier = std::max(1, plan.slotsPerCarrier);
    event.baseFrequencyHz = plan.baseHz;
    event.channelSpacingHz = plan.spacingHz;
    event.transmitOffsetHz = plan.txOffsetHz;
    event.bandwidthHz = plan.bandwidthHz;
    event.phase2Candidate = event.phase2Candidate || plan.phase2Capable || plan.slotsPerCarrier > 1;
    if (plan.slotsPerCarrier > 1) {
        event.voiceProtocol = P25VoiceProtocol::Phase2TDMA;
        event.tdmaSlotKnown = true;
        event.tdmaSlot = static_cast<uint8_t>((event.channel & 0x0fffu) %
                                              static_cast<uint16_t>(std::max(1, plan.slotsPerCarrier)));
    } else if (event.voiceProtocol == P25VoiceProtocol::Unknown) {
        event.voiceProtocol = P25VoiceProtocol::Phase1FDMA;
    }
    if (event.channelB != 0) {
        if (auto freqB = analyzer.channelToFrequencyHz(event.channelB)) event.voiceFrequencyHzB = *freqB;
    }
    return true;
}

static constexpr qint64 kP25PendingGrantTtlMs = 30000;

static bool p25RememberPendingVoiceGrant(std::vector<P25PendingVoiceGrant>& pendingGrants,
                                         const P25ControlEvent& ev,
                                         int correctedDibitErrors,
                                         qint64 nowMs)
{
    if (!p25ControlEventIsVoiceGrant(ev) || p25ControlEventIsResolvedVoiceGrant(ev) ||
        ev.talkgroupId == 0 || ev.channel == 0) {
        return false;
    }

    pendingGrants.erase(std::remove_if(pendingGrants.begin(), pendingGrants.end(),
        [nowMs](const P25PendingVoiceGrant& pending) {
            return nowMs - pending.lastSeenMs > kP25PendingGrantTtlMs;
        }), pendingGrants.end());

    auto it = std::find_if(pendingGrants.begin(), pendingGrants.end(), [&](const auto& pending) {
        return pending.event.talkgroupId == ev.talkgroupId && pending.event.channel == ev.channel;
    });
    if (it == pendingGrants.end()) {
        P25PendingVoiceGrant pending;
        pending.event = ev;
        pending.firstSeenMs = nowMs;
        pending.lastSeenMs = nowMs;
        pending.correctedDibitErrors = correctedDibitErrors;
        pendingGrants.push_back(pending);
    } else {
        it->event = ev;
        it->lastSeenMs = nowMs;
        it->correctedDibitErrors = correctedDibitErrors;
    }
    return true;
}

static std::vector<P25ControlEvent> p25ResolvePendingVoiceGrants(std::vector<P25PendingVoiceGrant>& pendingGrants,
                                                                 const P25ControlChannelAnalyzer& analyzer,
                                                                 qint64 nowMs)
{
    std::vector<P25ControlEvent> resolvedGrants;
    if (pendingGrants.empty()) return resolvedGrants;

    std::vector<P25PendingVoiceGrant> stillPending;
    stillPending.reserve(pendingGrants.size());
    for (auto pending : pendingGrants) {
        if (nowMs - pending.lastSeenMs > kP25PendingGrantTtlMs) continue;

        P25ControlEvent resolved = pending.event;
        if (!p25ResolveVoiceGrantFromAnalyzer(resolved, analyzer)) {
            stillPending.push_back(std::move(pending));
            continue;
        }
        analyzer.annotateCurrentSystemMetadata(resolved);
        resolvedGrants.push_back(std::move(resolved));
    }

    pendingGrants = std::move(stillPending);
    return resolvedGrants;
}

static bool p25TsbkEventRegistryEligible(int correctedDibitErrors, const P25ControlEvent& event)
{
    if (correctedDibitErrors <= kP25RegistryMaxCorrectedDibits) return true;

    // Resolved voice grants are actionable but short-lived. Permit them at a
    // slightly higher correction count so auto-follow can catch marginal P25
    // control channels without relaxing the trust gate for persistent state.
    return correctedDibitErrors <= kP25VoiceGrantMaxCorrectedDibits &&
        p25ControlEventIsResolvedVoiceGrant(event);
}

static bool mergeP25TalkgroupEvent(std::vector<P25TalkgroupEntry>& talkgroups,
                                   double controlFreqHz,
                                   const P25ControlEvent& event,
                                   qint64 nowMs)
{
    if (!p25ControlEventHasTalkgroupActivity(event) || !std::isfinite(controlFreqHz) || controlFreqHz <= 0.0) return false;
    auto it = std::find_if(talkgroups.begin(), talkgroups.end(), [&](const P25TalkgroupEntry& tg) {
        return sameP25Talkgroup(tg, controlFreqHz, event.talkgroupId) &&
            p25TalkgroupScopeCompatible(tg, event);
    });
    if (it == talkgroups.end()) {
        P25TalkgroupEntry tg;
        tg.controlFreqHz = controlFreqHz;
        tg.talkgroupId = event.talkgroupId;
        tg.firstSeenMs = nowMs;
        tg.lastSeenMs = nowMs;
        talkgroups.push_back(tg);
        it = std::prev(talkgroups.end());
    }

    it->lastSeenMs = nowMs;
    it->hitCount = std::max(0, it->hitCount) + 1;
    if (event.sourceId != 0) it->lastSourceId = event.sourceId;
    if (event.channel != 0) it->lastChannel = event.channel;
    if (event.voiceFrequencyHz > 0.0) it->lastVoiceFreqHz = event.voiceFrequencyHz;
    if (event.voiceProtocol != P25VoiceProtocol::Unknown) {
        it->voiceProtocol = event.voiceProtocol;
        it->phase2Candidate = event.phase2Candidate;
        if (!event.tdmaSlotKnown) {
            it->tdmaSlotKnown = false;
            it->tdmaSlot = 0;
        }
    } else if (event.phase2Candidate) {
        it->phase2Candidate = true;
    }
    if (event.tdmaSlotKnown) {
        it->tdmaSlotKnown = true;
        it->tdmaSlot = event.tdmaSlot;
    }
    p25ApplyControlEventSystemMetadata(*it, event);
    p25AugmentTalkgroupFromKnownSite(*it, talkgroups, controlFreqHz);
    if (event.encryptionKnown) {
        it->encryptionKnown = true;
        it->encrypted = event.encrypted;
    }
    return true;
}

static bool p25DecodeResultHasNidLock(const P25LiveDecodeResult& result)
{
    return result.stats.bestNidValid ||
        std::any_of(result.nids.begin(), result.nids.end(), [](const P25Nid& nid) {
            return nid.fecValidated;
        });
}

static QString p25LiveLockStageText(const P25LiveDecodeResult& result, size_t trustedTsbk)
{
    const bool nidLock = p25DecodeResultHasNidLock(result);
    if (result.stats.phase2EssKnown && !result.stats.phase2EssEncrypted &&
        result.stats.phase2MacCrcValid > 0 && result.stats.phase2MaskedBursts > 0) {
        return "ClearVoiceReady";
    }
    if (result.stats.phase2EssKnown) {
        return result.stats.phase2EssEncrypted ? "Phase2EssEncrypted" : "Phase2EssClear";
    }
    if (result.stats.phase2MacCrcValid > 0) return "Phase2MacCrcValid";
    if (result.stats.phase2MaskedBursts > 0) return "Phase2MaskHypothesis";
    if (result.stats.phase2SuperframeBursts > 0) return "Phase2SuperframeSync";
    if (trustedTsbk > 0) return "TrustedTsbk";
    if (nidLock) return "Phase1NidValid";
    if (result.stats.phase2Bursts > 0) return "Phase2BurstTelemetry";
    if (!result.syncs.empty()) return "FrameSyncCandidate";
    if (result.stats.symbols > 0) return "SymbolStream";
    if (result.stats.inputSamples > 0) return "IqPresent";
    return "NoSignal";
}

static std::string p25ControlAuditTsbkKey(const std::vector<uint8_t>& bytes)
{
    if (bytes.empty()) return "TSBK op=unknown mfid=unknown";
    const uint8_t opcode = static_cast<uint8_t>(bytes[0] & 0x3f);
    const uint8_t mfid = bytes.size() > 1 ? bytes[1] : 0;
    return QString("TSBK op=0x%1 mfid=0x%2")
        .arg(static_cast<int>(opcode), 2, 16, QLatin1Char('0'))
        .arg(static_cast<int>(mfid), 2, 16, QLatin1Char('0'))
        .toUpper()
        .toStdString();
}

static std::string p25ControlAuditPhase2MacKey(const P25Phase2MacPdu& pdu)
{
    return QString("P2MAC type=0x%1 offset=%2")
        .arg(static_cast<int>(pdu.opcode), 2, 16, QLatin1Char('0'))
        .arg(static_cast<int>(pdu.offset))
        .toUpper()
        .toStdString();
}

static QString p25ControlAuditOpsText(const std::map<std::string, size_t>& ops)
{
    if (ops.empty()) return "none";
    QStringList parts;
    int shown = 0;
    for (const auto& [op, count] : ops) {
        if (shown++ >= 10) {
            parts << QString("+%1 more").arg(static_cast<int>(ops.size()) - 10);
            break;
        }
        parts << QString("%1 x%2").arg(QString::fromStdString(op)).arg(static_cast<qulonglong>(count));
    }
    return parts.join(", ");
}

static constexpr qint64 kP25RetunePreArmMuteMs = 250;
static constexpr int kP25RetunePreArmDiscardWindows = 1;
static constexpr qint64 kP25Phase1PostArmSettleMs = 250;
static constexpr qint64 kP25Phase2PostArmSettleMs = 250;
static constexpr int kP25Phase1PostArmDiscardWindows = 1;
static constexpr int kP25Phase2PostArmDiscardWindows = 1;
static constexpr int kP25Phase1ArmDelayMs = 250;
static constexpr int kP25Phase2ArmDelayMs = 300;
static constexpr qint64 kP25Phase2SlotProbeSettleMs = 300;
static constexpr int kP25Phase2SlotProbeDiscardWindows = 1;
static constexpr double kP25ControlDecodeWindowSeconds = 0.512;
static constexpr int kP25ControlDecodeCadenceMs = 360;
static constexpr double kP25Phase2VoiceDecodeWindowSeconds = 0.512;
static constexpr int kP25Phase2VoiceDecodeCadenceMs = 384;

static qint64 p25PostArmSettleMs(bool phase2) noexcept
{
    return phase2 ? kP25Phase2PostArmSettleMs : kP25Phase1PostArmSettleMs;
}

static int p25PostArmDiscardWindows(bool phase2) noexcept
{
    return phase2 ? kP25Phase2PostArmDiscardWindows : kP25Phase1PostArmDiscardWindows;
}

static P25LiveDecoderConfig p25DiagnosticDecoderConfig()
{
    P25LiveDecoderConfig cfg;
    cfg.enableC4fmFixedPhaseSearch = true;
    cfg.maxC4fmFixedPhaseCandidates = 10;
    cfg.maxFrameSyncs = 12;
    cfg.maxRawTsbkBlocksPerFrame = 8;
    return cfg;
}

static P25LiveDecoderConfig p25VoiceDecoderConfig(bool phase2)
{
    P25LiveDecoderConfig cfg = p25DiagnosticDecoderConfig();
    // Phase 1 C4FM/control-channel symbols are 4,800 sps. Phase 2 TDMA
    // H-DQPSK traffic bursts are 6,000 sps; attempting TDMA acquisition with
    // the 4,800 sps clock can show burst-like false positives but will not
    // stabilize superframe/mask/ESS lock.
    cfg.symbolRate = phase2 ? 6000.0 : 4800.0;
    cfg.channelBandwidthHz = 12500.0;
    cfg.workSampleRate = phase2 ? 96000.0 : 48000.0;
    cfg.maxFrameSyncBitErrors = phase2 ? std::max(3, static_cast<int>(cfg.maxFrameSyncBitErrors))
                                       : cfg.maxFrameSyncBitErrors;
    if (phase2) {
        cfg.stopCqpskSearchOnHardLock = true;
        cfg.realtimeVoiceSearch = false;
        cfg.maxC4fmFixedPhaseCandidates = 10;
    }
    return cfg;
}

static int p25CliDecodeScore(const P25LiveDecodeResult& result)
{
    int score = 0;
    for (const auto& block : result.rawTsbkBlocks) {
        if (block.fecDecoded && block.crcValid) score += 400;
        else if (block.fecDecoded) score += 4;
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

static void p25SeedAnalyzerNacFromDecode(P25ControlChannelAnalyzer& analyzer,
                                         const P25LiveDecodeResult& result)
{
    for (const auto& nid : result.nids) {
        if (nid.fecValidated) {
            analyzer.setNac(nid.nac);
            return;
        }
    }
}

static void printP25CliDecodeReport(const std::string& label,
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
            }
            if (!registryEligible && !acceptedHighCorrectionGrant) {
                std::cout << "    note: CRC valid but corrected dibits exceed registry threshold; only resolved voice grants up to "
                          << kP25VoiceGrantMaxCorrectedDibits
                          << " corrected dibits are saved/followed\n";
            }
        }
        if (changed) {
            saveP25Talkgroups(talkgroups);
            std::cout << "  Talkgroup registry updated from trusted TSBK.\n";
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
        const bool lateEntry = std::any_of(result.phase2Bursts.begin(), result.phase2Bursts.end(), [](const P25Phase2Burst& burst) {
            return !burst.sessionAudioRelease &&
                   !burst.voiceCodewords.empty() &&
                   burst.xorMaskApplied &&
                   !burst.macCrcValid;
        });
        if (lateEntry) {
            std::cout << "  note: Phase 2 late entry: voice bursts present, mask applied, waiting for MAC CRC/ESS before audio release.\n";
        }
        std::cout << "  note: clear Phase 2 AMBE audio is gated until TDMA mask, MAC/ESS clear state, and AMBE validation all pass.\n";
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
            const auto events = analyzer.ingestPhase2MacPdu(pdu.opcode, pdu.offset, pdu.bytes, pdu.crcValid);
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

    if (trustedTsbk > 0 || trustedPhase2Mac > 0 || result.stats.phase2Bursts > 0) {
        std::cout << "  control audit: stage=" << lockStage.toStdString()
                  << " trustedTsbk=" << trustedTsbk
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

struct SigmfIqCapture {
    bool ok = false;
    QString metaPath;
    QString dataPath;
    std::string error;
    std::string datatype;
    double sampleRateHz = 0.0;
    double centerFreqHz = 0.0;
    double targetFreqHz = 0.0;
    double startOffsetMs = 0.0;
    uint64_t firstSampleOffset = 0;
    std::vector<std::complex<float>> iq;
};

static QString sigmfSiblingPath(const QFileInfo& info, const QString& extension)
{
    return info.absolutePath() + "/" + info.completeBaseName() + extension;
}

static QString resolveSigmfMetaPath(QString input)
{
    input = input.trimmed();
    if ((input.startsWith('"') && input.endsWith('"')) ||
        (input.startsWith('\'') && input.endsWith('\''))) {
        input = input.mid(1, input.size() - 2);
    }

    QFileInfo info(input);
    if (info.isDir()) {
        QDir dir(input);
        const auto metas = dir.entryInfoList(QStringList() << "*.sigmf-meta", QDir::Files, QDir::Name);
        if (!metas.empty()) return metas.front().absoluteFilePath();
        return {};
    }
    if (info.exists() && info.fileName().endsWith(".sigmf-meta", Qt::CaseInsensitive)) return info.absoluteFilePath();
    if (info.exists() && info.fileName().endsWith(".sigmf-data", Qt::CaseInsensitive)) {
        const QString meta = sigmfSiblingPath(info, ".sigmf-meta");
        if (QFileInfo::exists(meta)) return QFileInfo(meta).absoluteFilePath();
    }
    return {};
}

static double sigmfAnnotationTargetHz(const json& meta)
{
    try {
        if (!meta.contains("annotations") || !meta["annotations"].is_array()) return 0.0;
        for (const auto& ann : meta["annotations"]) {
            const double lo = ann.value("core:freq_lower_edge", 0.0);
            const double hi = ann.value("core:freq_upper_edge", 0.0);
            if (std::isfinite(lo) && std::isfinite(hi) && lo > 0.0 && hi > lo) {
                return (lo + hi) * 0.5;
            }
        }
    } catch (...) {
    }
    return 0.0;
}

static SigmfIqCapture loadSigmfCf32Capture(const QString& requestedPath, double maxMs, double skipMs = 0.0)
{
    SigmfIqCapture out;
    out.metaPath = resolveSigmfMetaPath(requestedPath);
    if (out.metaPath.isEmpty()) {
        out.error = "could not resolve a .sigmf-meta file from the supplied path";
        return out;
    }

    json meta;
    try {
        std::ifstream metaIn(out.metaPath.toStdString());
        if (!metaIn.is_open()) {
            out.error = "could not open SigMF metadata";
            return out;
        }
        metaIn >> meta;
    } catch (const std::exception& ex) {
        out.error = std::string("could not parse SigMF metadata: ") + ex.what();
        return out;
    }

    try {
        out.datatype = meta["global"].value("core:datatype", std::string());
        out.sampleRateHz = meta["global"].value("core:sample_rate", 0.0);
        if (meta.contains("captures") && meta["captures"].is_array() && !meta["captures"].empty()) {
            out.centerFreqHz = meta["captures"][0].value("core:frequency", 0.0);
        }
        out.targetFreqHz = sigmfAnnotationTargetHz(meta);
    } catch (const std::exception& ex) {
        out.error = std::string("SigMF metadata is missing required fields: ") + ex.what();
        return out;
    }

    if (out.datatype != "cf32_le") {
        out.error = "only cf32_le SigMF captures are supported for P25 replay";
        return out;
    }
    if (!std::isfinite(out.sampleRateHz) || out.sampleRateHz <= 0.0 ||
        !std::isfinite(out.centerFreqHz) || out.centerFreqHz <= 0.0) {
        out.error = "SigMF metadata has invalid sample rate or center frequency";
        return out;
    }

    const QFileInfo metaInfo(out.metaPath);
    out.dataPath = sigmfSiblingPath(metaInfo, ".sigmf-data");
    if (!QFileInfo::exists(out.dataPath)) {
        out.error = "matching .sigmf-data file was not found";
        return out;
    }

    std::ifstream data(out.dataPath.toStdString(), std::ios::binary | std::ios::ate);
    if (!data.is_open()) {
        out.error = "could not open SigMF data file";
        return out;
    }
    const auto endPos = data.tellg();
    if (endPos <= 0) {
        out.error = "SigMF data file is empty";
        return out;
    }
    const uint64_t bytesAvailable = static_cast<uint64_t>(endPos);
    if (bytesAvailable % (sizeof(float) * 2u) != 0u) {
        out.error = "SigMF cf32 data length is not an even I/Q float pair count";
        return out;
    }

    const uint64_t totalSamples = bytesAvailable / (sizeof(float) * 2u);
    uint64_t startSample = 0;
    if (std::isfinite(skipMs) && skipMs > 0.0) {
        startSample = static_cast<uint64_t>(std::clamp(
            out.sampleRateHz * (skipMs / 1000.0),
            0.0,
            totalSamples > 0 ? static_cast<double>(totalSamples - 1u) : 0.0));
    }

    uint64_t samplesToRead = totalSamples - startSample;
    if (std::isfinite(maxMs) && maxMs > 0.0) {
        const uint64_t byTime = static_cast<uint64_t>(std::clamp(
            out.sampleRateHz * (maxMs / 1000.0),
            1.0,
            static_cast<double>(samplesToRead)));
        samplesToRead = std::min(samplesToRead, byTime);
    }

    const uint64_t byteOffset = startSample * sizeof(float) * 2u;
    data.seekg(static_cast<std::streamoff>(byteOffset), std::ios::beg);
    if (!data) {
        out.error = "could not seek to requested SigMF replay offset";
        return out;
    }
    out.firstSampleOffset = startSample;
    out.startOffsetMs = out.sampleRateHz > 0.0
        ? static_cast<double>(startSample) * 1000.0 / out.sampleRateHz
        : 0.0;
    out.iq.resize(static_cast<size_t>(samplesToRead));
    constexpr uint64_t kReadBlockSamples = 1u << 16;
    std::vector<float> rawBlock(kReadBlockSamples * 2u);
    uint64_t samplesRead = 0;
    while (samplesRead < samplesToRead) {
        const uint64_t blockSamples = std::min<uint64_t>(kReadBlockSamples, samplesToRead - samplesRead);
        const auto bytesToRead = static_cast<std::streamsize>(blockSamples * 2u * sizeof(float));
        data.read(reinterpret_cast<char*>(rawBlock.data()), bytesToRead);
        if (!data) {
            out.error = "short read while loading SigMF data";
            out.iq.clear();
            return out;
        }
        for (uint64_t i = 0; i < blockSamples; ++i) {
            out.iq[static_cast<size_t>(samplesRead + i)] =
                std::complex<float>(rawBlock[static_cast<size_t>(i * 2u)],
                                    rawBlock[static_cast<size_t>(i * 2u + 1u)]);
        }
        samplesRead += blockSamples;
    }

    out.ok = true;
    return out;
}

struct P25ReplayCliArgs {
    bool ok = false;
    std::string path;
    double targetMhz = 0.0;
    double ms = 0.0;
    double followMs = 0.0;
    double skipMs = 0.0;
    double centerMhz = 0.0;
    uint32_t followTalkgroupId = 0;
    bool phase2Voice = false;
    int nac = -1;
    int64_t wacn = -1;
    int systemId = -1;
    std::string error;
};

static bool parseFiniteDoubleToken(const std::string& text, double& out)
{
    try {
        size_t consumed = 0;
        const double value = std::stod(text, &consumed);
        if (consumed != text.size() || !std::isfinite(value)) return false;
        out = value;
        return true;
    } catch (...) {
        return false;
    }
}

static bool sigmfPathExistsForCli(const std::string& path)
{
    return !resolveSigmfMetaPath(QString::fromStdString(trimCopy(path))).isEmpty();
}

static bool parseUnsignedIntegerToken(const std::string& text, uint64_t& out)
{
    try {
        size_t consumed = 0;
        const uint64_t value = std::stoull(text, &consumed, 0);
        if (consumed != text.size()) return false;
        out = value;
        return true;
    } catch (...) {
        return false;
    }
}

static bool p25ReplayHasMaskParameters(const P25ReplayCliArgs& args)
{
    return args.nac >= 0 && args.nac <= 0x0fff &&
        args.wacn >= 0 && args.wacn <= 0x0fffff &&
        args.systemId >= 0 && args.systemId <= 0x0fff;
}

static bool parseP25ReplayOptionToken(const std::string& token, P25ReplayCliArgs& args)
{
    std::string lower = token;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (lower == "p2" || lower == "phase2" || lower == "phase2voice" || lower == "tdma") {
        args.phase2Voice = true;
        return true;
    }

    const size_t eq = lower.find('=');
    if (eq == std::string::npos) return false;
    const std::string key = lower.substr(0, eq);
    const std::string rawValue = token.substr(eq + 1);

    if (key == "nac" || key == "wacn" || key == "system" || key == "systemid" || key == "sys" || key == "sysid" || key == "sid" ||
        key == "tg" || key == "talkgroup" || key == "talkgroupid") {
        uint64_t u = 0;
        if (!parseUnsignedIntegerToken(rawValue, u)) return false;
        if (key == "nac") {
            if (u > 0x0fff) return false;
            args.nac = static_cast<int>(u);
        } else if (key == "wacn") {
            if (u > 0x0fffff) return false;
            args.wacn = static_cast<int64_t>(u);
        } else if (key == "tg" || key == "talkgroup" || key == "talkgroupid") {
            if (u == 0 || u > 0x00ffffffu) return false;
            args.followTalkgroupId = static_cast<uint32_t>(u);
        } else {
            if (u > 0x0fff) return false;
            args.systemId = static_cast<int>(u);
        }
        return true;
    }

    double value = 0.0;
    if (!parseFiniteDoubleToken(rawValue, value)) return false;

    if (key == "skip" || key == "skipms" || key == "offset" || key == "offsetms" || key == "start" || key == "startms") {
        args.skipMs = std::max(0.0, value);
        return true;
    }
    if (key == "center" || key == "centermhz" || key == "cf" || key == "cfmhz") {
        args.centerMhz = value;
        return true;
    }
    if (key == "target" || key == "targetmhz") {
        args.targetMhz = value;
        return true;
    }
    if (key == "ms" || key == "duration" || key == "durationms") {
        args.ms = std::max(0.0, value);
        return true;
    }
    if (key == "follow" || key == "followms" || key == "voice" || key == "voicems" || key == "followduration") {
        args.followMs = std::max(0.0, value);
        return true;
    }
    return false;
}

static bool parseP25ReplayTailTokens(std::istringstream& tail, P25ReplayCliArgs& args)
{
    std::string token;
    int numericCount = 0;
    while (tail >> token) {
        if (parseP25ReplayOptionToken(token, args)) continue;

        double numeric = 0.0;
        if (parseFiniteDoubleToken(token, numeric)) {
            if (numericCount == 0) {
                args.targetMhz = numeric;
            } else if (numericCount == 1) {
                args.ms = std::max(0.0, numeric);
            } else if (numericCount == 2) {
                args.skipMs = std::max(0.0, numeric);
            } else {
                args.error = "too many numeric replay arguments";
                return false;
            }
            ++numericCount;
            continue;
        }

        args.error = "unknown replay option: " + token;
        return false;
    }
    return true;
}

static P25ReplayCliArgs parseP25ReplayCliArgs(const std::string& rest)
{
    P25ReplayCliArgs args;
    const std::string text = trimCopy(rest);
    if (text.empty()) {
        args.error = "usage: p25 replay <sigmf-meta|sigmf-data|capture_dir> [target_mhz] [ms] [phase2] [skip=<ms>] [center=<mhz>] [nac=<id> wacn=<id> system=<id>]";
        return args;
    }

    if (text.front() == '"' || text.front() == '\'') {
        const char quote = text.front();
        const size_t close = text.find(quote, 1);
        if (close == std::string::npos) {
            args.error = "quoted replay path is missing its closing quote";
            return args;
        }
        args.path = text.substr(1, close - 1);
        std::istringstream tail(text.substr(close + 1));
        if (!parseP25ReplayTailTokens(tail, args)) {
            if (args.error.empty()) args.error = "could not parse replay arguments";
            return args;
        }
        if (!sigmfPathExistsForCli(args.path)) {
            args.error = "replay capture path does not exist or has no .sigmf-meta";
            return args;
        }
        args.ok = true;
        return args;
    }

    struct TokenPos {
        std::string token;
        size_t begin = 0;
        size_t end = 0;
    };
    std::vector<TokenPos> tokens;
    size_t pos = 0;
    while (pos < text.size()) {
        while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) ++pos;
        if (pos >= text.size()) break;
        const size_t begin = pos;
        while (pos < text.size() && !std::isspace(static_cast<unsigned char>(text[pos]))) ++pos;
        tokens.push_back({text.substr(begin, pos - begin), begin, pos});
    }

    for (int trailingNumbers = 0; trailingNumbers <= 2; ++trailingNumbers) {
        if (static_cast<size_t>(trailingNumbers) > tokens.size()) break;
        std::array<double, 2> values{0.0, 0.0};
        bool numeric = true;
        for (int i = 0; i < trailingNumbers; ++i) {
            const size_t idx = tokens.size() - static_cast<size_t>(trailingNumbers) + static_cast<size_t>(i);
            numeric = numeric && parseFiniteDoubleToken(tokens[idx].token, values[static_cast<size_t>(i)]);
        }
        if (!numeric) continue;
        const size_t pathEnd = trailingNumbers == 0
            ? text.size()
            : tokens[tokens.size() - static_cast<size_t>(trailingNumbers)].begin;
        const std::string candidate = trimCopy(text.substr(0, pathEnd));
        if (!candidate.empty() && sigmfPathExistsForCli(candidate)) {
            args.path = candidate;
            if (trailingNumbers >= 1) args.targetMhz = values[0];
            if (trailingNumbers >= 2) args.ms = values[1];
            args.ok = true;
            return args;
        }
    }

    args.error = "replay capture path does not exist or has no .sigmf-meta; quote paths with spaces before adding replay options";
    return args;
}

struct TrainingCaptureRequest {
    std::string label;
    size_t deviceIndex = 0;
    double tunedFreqHz = 100e6;
    DemodMode mode = DemodMode::AUTO;
    double channelBwHz = 12500.0;
    double lpfHz = 3000.0;
    bool audioLpfEnabled = true;
    double squelchDb = -105.0;
    std::vector<std::complex<float>> iq;
    std::vector<float> spectrumDb;
    double centerFreqHz = 100e6;
    double sampleRateHz = 2.048e6;
    SignalRecommendation recommendation;
    ClassifierTile tile;
    DeviceInfo device;
};

struct TrainingCaptureResult {
    bool ok = false;
    QString directory;
    QString message;
};


struct IqTestCaptureRequest {
    std::string label;
    size_t deviceIndex = 0;
    double tunedFreqHz = 100e6;
    DemodMode mode = DemodMode::AUTO;
    double channelBwHz = 12500.0;
    double lpfHz = 3000.0;
    bool audioLpfEnabled = true;
    double squelchDb = -105.0;
    double centerFreqHz = 100e6;
    double sampleRateHz = 2.048e6;
    double requestedSeconds = 5.0;
    uint64_t startAbsolute = 0;
    uint64_t endAbsolute = 0;
    QDateTime captureStartedUtc;
    QDateTime captureEndedUtc;
    std::vector<std::complex<float>> iq;
    std::vector<float> spectrumDb;
    DeviceInfo device;
    QStringList p25LogSnapshot;
    double signalLevelDb = -120.0;
    double noiseFloorDb = -120.0;
    double snrDb = 0.0;
    double afcOffsetHz = 0.0;
};

struct IqTestCaptureResult {
    bool ok = false;
    QString directory;
    QString message;
};

struct LiveIqCaptureSession {
    bool active = false;
    std::string label;
    std::string sessionId;
    size_t deviceIndex = 0;
    double tunedFreqHz = 100e6;
    DemodMode mode = DemodMode::AUTO;
    double channelBwHz = 12500.0;
    double lpfHz = 3000.0;
    bool audioLpfEnabled = true;
    double squelchDb = -105.0;
    double centerFreqHz = 100e6;
    double sampleRateHz = 2.048e6;
    DeviceInfo device;
    QDateTime startedUtc;
    QDateTime stoppedUtc;
    QDateTime lastPollUtc;
    uint64_t startAbsolute = 0;
    uint64_t cursorAbsolute = 0;
    uint64_t endAbsolute = 0;
    uint64_t ringOverrunSamples = 0;
    uint64_t maxSingleGapSamples = 0;
    uint64_t ringEpochResets = 0;
    uint64_t ringEpochResetSkippedSamples = 0;
    uint64_t zeroAppendPolls = 0;
    uint64_t fileWriteErrorPolls = 0;
    uint64_t pollCount = 0;
    size_t samplesWritten = 0;
    uint64_t bytesWritten = 0;
    QString directory;
    QString baseName;
    QString dataPath;
    QString metaPath;
    QString eventsPath;
    QString p25TextPath;
    QString ringCsvPath;
    QString statusPath;
    QString summaryPath;
    QString replayPath;
    std::ofstream data;
    std::ofstream events;
    std::ofstream ringCsv;
    std::ofstream p25LogStream;
    QStringList startP25LogSnapshot; // legacy/small startup context only; no live capture log buffering.
    QStringList p25LogDuringCapture; // unused after streaming-log fix; kept for source compatibility.
    size_t p25CaptureDroppedLines = 0;
    uint64_t p25CaptureLinesWritten = 0;
    uint64_t p25CaptureWriteErrors = 0;
    size_t startP25LogIndex = 0;
    double lastSignalLevelDb = -120.0;
    double lastNoiseFloorDb = -120.0;
    double lastSnrDb = 0.0;
    double lastAfcOffsetHz = 0.0;
};

struct LiveIqCaptureResult {
    bool ok = false;
    QString directory;
    QString message;
};

static QString trainingCapturesRoot()
{
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(appData + "/training_captures");
    return appData + "/training_captures";
}


static QString iqTestCapturesRoot()
{
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(appData + "/iq_test_captures");
    return appData + "/iq_test_captures";
}

static std::string sanitizeFileToken(std::string s)
{
    if (s.empty()) s = "unknown";
    for (char& c : s) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (!std::isalnum(uc) && c != '-' && c != '_') c = '_';
    }
    while (s.find("__") != std::string::npos) {
        s.replace(s.find("__"), 2, "_");
    }
    if (s.size() > 48) s.resize(48);
    return trimCopy(s);
}

static std::string makeCaptureSessionId(const QDateTime& utc, const std::string& label, double freqHz)
{
    const std::string seed = utc.toString(Qt::ISODateWithMs).toStdString() + "|" + label + "|" + std::to_string(freqHz);
    const size_t h = std::hash<std::string>{}(seed);
    std::ostringstream os;
    os << sanitizeFileToken(label) << "_" << std::hex << h;
    std::string out = os.str();
    if (out.size() > 64) out.resize(64);
    return out;
}

static bool writeJsonDocumentFile(const QString& path, const json& doc, QString* error = nullptr)
{
    try {
        std::ofstream out(path.toStdString());
        if (!out.is_open()) {
            if (error) *error = QString("could not open %1 for writing").arg(path);
            return false;
        }
        out << doc.dump(2) << "\n";
        return true;
    } catch (const std::exception& ex) {
        if (error) *error = QString::fromStdString(ex.what());
        return false;
    }
}

static const char* captureHealthVerdict(uint64_t samplesWritten,
                                        uint64_t ringOverrunSamples,
                                        uint64_t fileWriteErrorPolls,
                                        double seconds,
                                        uint64_t ringEpochResetSkippedSamples = 0)
{
    if (fileWriteErrorPolls > 0) return "bad_file_write_errors";
    if (samplesWritten == 0 || seconds <= 0.0) return "bad_empty_capture";
    if (ringOverrunSamples > 0) return "warning_ring_gaps_present";
    if (ringEpochResetSkippedSamples > 0) return "warning_ring_epoch_reset_skipped_samples";
    return "ok_gapless";
}

static bool writeClassifierTilePreview(const ClassifierTile& tile, const std::string& pgmPath, const std::string& f32Path)
{
    if (!tile.valid()) return false;

    std::ofstream pgm(pgmPath, std::ios::binary);
    if (!pgm.is_open()) return false;
    pgm << "P5\n" << tile.width << " " << tile.height << "\n255\n";
    std::vector<unsigned char> bytes(tile.pixels.size());
    for (size_t i = 0; i < tile.pixels.size(); ++i) {
        bytes[i] = static_cast<unsigned char>(std::clamp(tile.pixels[i], 0.0f, 1.0f) * 255.0f);
    }
    pgm.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));

    std::ofstream f32(f32Path, std::ios::binary);
    if (!f32.is_open()) return false;
    f32.write(reinterpret_cast<const char*>(tile.pixels.data()), static_cast<std::streamsize>(tile.pixels.size() * sizeof(float)));
    return true;
}


static IqTestCaptureResult saveIqTestCapture(const IqTestCaptureRequest& req)
{
    IqTestCaptureResult out;
    if (req.iq.empty()) {
        out.message = "No IQ samples available. Start a device and make sure the requested duration fits in the recent-IQ ring.";
        return out;
    }
    if (req.sampleRateHz <= 0.0 || !std::isfinite(req.sampleRateHz)) {
        out.message = "Invalid sample rate for IQ test capture.";
        return out;
    }

    const QString root = iqTestCapturesRoot();
    const QString stamp = req.captureEndedUtc.isValid()
        ? req.captureEndedUtc.toString("yyyyMMdd_HHmmss_zzz")
        : QDateTime::currentDateTimeUtc().toString("yyyyMMdd_HHmmss_zzz");
    const std::string labelToken = sanitizeFileToken(req.label);
    const QString baseName = QString("%1_%2_%3MHz_%4s")
        .arg(stamp)
        .arg(QString::fromStdString(labelToken))
        .arg(req.tunedFreqHz / 1e6, 0, 'f', 5)
        .arg(req.requestedSeconds, 0, 'f', 1);
    const QString dir = root + "/" + baseName;
    if (!QDir().mkpath(dir)) {
        out.message = "Could not create IQ test capture directory.";
        return out;
    }

    const std::string base = (dir + "/" + baseName).toStdString();
    const std::string dataPath = base + ".sigmf-data";
    const std::string metaPath = base + ".sigmf-meta";
    const std::string logPath = base + "_events.jsonl";
    const std::string p25TextPath = base + "_p25_log.txt";

    {
        std::ofstream data(dataPath, std::ios::binary);
        if (!data.is_open()) {
            out.message = "Could not write IQ SigMF data file.";
            return out;
        }
        for (const auto& s : req.iq) {
            const float re = s.real();
            const float im = s.imag();
            data.write(reinterpret_cast<const char*>(&re), sizeof(float));
            data.write(reinterpret_cast<const char*>(&im), sizeof(float));
        }
    }

    const double actualSeconds = static_cast<double>(req.iq.size()) / req.sampleRateHz;
    const QDateTime endUtc = req.captureEndedUtc.isValid() ? req.captureEndedUtc : QDateTime::currentDateTimeUtc();
    const QDateTime startUtc = endUtc.addMSecs(-static_cast<qint64>(std::llround(actualSeconds * 1000.0)));

    json meta;
    meta["global"] = {
        {"core:datatype", "cf32_le"},
        {"core:sample_rate", req.sampleRateHz},
        {"core:version", "1.2.0"},
        {"core:description", "SDR Town timed IQ test capture with synchronized diagnostics"},
        {"core:recorder", "SDR Town"},
        {"sdrtown:label", req.label},
        {"sdrtown:capture_type", "timed_iq_test"},
        {"sdrtown:requested_seconds", req.requestedSeconds},
        {"sdrtown:actual_seconds", actualSeconds},
        {"sdrtown:mode", modeToString(req.mode)},
        {"sdrtown:channel_bandwidth_hz", req.channelBwHz},
        {"sdrtown:audio_lpf_hz", req.lpfHz},
        {"sdrtown:audio_lpf_enabled", req.audioLpfEnabled},
        {"sdrtown:squelch_db", req.squelchDb},
        {"sdrtown:device_index", req.deviceIndex},
        {"sdrtown:device_driver", req.device.driver},
        {"sdrtown:device_label", req.device.label},
        {"sdrtown:device_serial", req.device.serial},
        {"sdrtown:rf_gain_db", req.device.gain},
        {"sdrtown:frequency_correction_ppm", req.device.frequencyCorrectionPpm},
        {"sdrtown:absolute_sample_start", req.startAbsolute},
        {"sdrtown:absolute_sample_end", req.endAbsolute},
        {"sdrtown:capture_started_utc", req.captureStartedUtc.toString(Qt::ISODateWithMs).toStdString()},
        {"sdrtown:capture_saved_utc", endUtc.toString(Qt::ISODateWithMs).toStdString()},
        {"sdrtown:signal_level_db", req.signalLevelDb},
        {"sdrtown:noise_floor_db", req.noiseFloorDb},
        {"sdrtown:snr_db", req.snrDb},
        {"sdrtown:afc_offset_hz", req.afcOffsetHz}
    };
    meta["captures"] = json::array({
        {
            {"core:sample_start", 0},
            {"core:frequency", req.centerFreqHz},
            {"core:datetime", startUtc.toString(Qt::ISODateWithMs).toStdString()},
            {"sdrtown:capture_end_utc", endUtc.toString(Qt::ISODateWithMs).toStdString()},
            {"sdrtown:absolute_sample_start", req.startAbsolute},
            {"sdrtown:absolute_sample_end", req.endAbsolute}
        }
    });
    meta["annotations"] = json::array({
        {
            {"core:sample_start", 0},
            {"core:sample_count", req.iq.size()},
            {"core:freq_lower_edge", req.tunedFreqHz - req.channelBwHz * 0.5},
            {"core:freq_upper_edge", req.tunedFreqHz + req.channelBwHz * 0.5},
            {"core:label", req.label},
            {"sdrtown:mode", modeToString(req.mode)},
            {"sdrtown:snr_db", req.snrDb}
        }
    });
    meta["sdrtown:artifacts"] = {
        {"event_log_jsonl", QFileInfo(QString::fromStdString(logPath)).fileName().toStdString()},
        {"p25_log_text", QFileInfo(QString::fromStdString(p25TextPath)).fileName().toStdString()}
    };

    try {
        std::ofstream metaOut(metaPath);
        if (!metaOut.is_open()) {
            out.message = "Could not write IQ SigMF metadata file.";
            return out;
        }
        metaOut << meta.dump(2);

        std::ofstream p25Text(p25TextPath);
        if (p25Text.is_open()) {
            p25Text << "# SDR Town P25/UI log snapshot for IQ capture\n";
            p25Text << "# capture_start_utc=" << startUtc.toString(Qt::ISODateWithMs).toStdString() << "\n";
            p25Text << "# capture_end_utc=" << endUtc.toString(Qt::ISODateWithMs).toStdString() << "\n";
            p25Text << "# absolute_sample_start=" << req.startAbsolute << "\n";
            p25Text << "# absolute_sample_end=" << req.endAbsolute << "\n";
            for (const QString& line : req.p25LogSnapshot) {
                p25Text << line.toStdString() << "\n";
            }
        }

        std::ofstream log(logPath, std::ios::app);
        if (log.is_open()) {
            json startRow = {
                {"event", "capture_start"},
                {"utc", startUtc.toString(Qt::ISODateWithMs).toStdString()},
                {"label", req.label},
                {"freq_hz", req.tunedFreqHz},
                {"center_freq_hz", req.centerFreqHz},
                {"sample_rate_hz", req.sampleRateHz},
                {"absolute_sample", req.startAbsolute}
            };
            json endRow = {
                {"event", "capture_end"},
                {"utc", endUtc.toString(Qt::ISODateWithMs).toStdString()},
                {"sample_count", req.iq.size()},
                {"actual_seconds", actualSeconds},
                {"absolute_sample", req.endAbsolute},
                {"signal_level_db", req.signalLevelDb},
                {"noise_floor_db", req.noiseFloorDb},
                {"snr_db", req.snrDb},
                {"afc_offset_hz", req.afcOffsetHz}
            };
            log << startRow.dump() << "\n";
            for (const QString& line : req.p25LogSnapshot) {
                json row = {
                    {"event", "p25_log_snapshot"},
                    {"capture_end_utc", endUtc.toString(Qt::ISODateWithMs).toStdString()},
                    {"line", line.toStdString()}
                };
                log << row.dump() << "\n";
            }
            log << endRow.dump() << "\n";
        }

        std::ofstream manifest((root + "/manifest.jsonl").toStdString(), std::ios::app);
        if (manifest.is_open()) {
            json row = {
                {"created_utc", endUtc.toString(Qt::ISODateWithMs).toStdString()},
                {"label", req.label},
                {"freq_hz", req.tunedFreqHz},
                {"center_freq_hz", req.centerFreqHz},
                {"sample_rate_hz", req.sampleRateHz},
                {"sample_count", req.iq.size()},
                {"requested_seconds", req.requestedSeconds},
                {"actual_seconds", actualSeconds},
                {"absolute_sample_start", req.startAbsolute},
                {"absolute_sample_end", req.endAbsolute},
                {"meta", QFileInfo(QString::fromStdString(metaPath)).fileName().toStdString()},
                {"data", QFileInfo(QString::fromStdString(dataPath)).fileName().toStdString()},
                {"event_log", QFileInfo(QString::fromStdString(logPath)).fileName().toStdString()},
                {"p25_log", QFileInfo(QString::fromStdString(p25TextPath)).fileName().toStdString()},
                {"directory", dir.toStdString()}
            };
            manifest << row.dump() << "\n";
        }
    } catch (const std::exception& ex) {
        out.message = QString("IQ test capture write failed: %1").arg(ex.what());
        return out;
    }

    const bool truncated = actualSeconds + 0.050 < req.requestedSeconds;
    out.ok = true;
    out.directory = dir;
    out.message = QString("Saved timed IQ capture: %1 samples, %2 s%3")
        .arg(req.iq.size())
        .arg(actualSeconds, 0, 'f', 3)
        .arg(truncated ? QString(" (shorter than requested; recent-IQ ring limit reached)") : QString());
    return out;
}

static QString p25VoiceDiagCaptureSummary(const P25VoiceDiagSnapshot& diag)
{
    const auto code = static_cast<P25VoiceDiagCode>(diag.diag);
    return QString("stage=%1 tg=%2 sync=%3 nid=%4 nidLock=%5 imbe=%6 decoded=%7 audio=%8 "
                   "p2bursts=%9 p2vcw=%10 p2sf=%11 p2mask=%12 p2mac=%13/%14 %15 p2ess=%16 backend=%17")
        .arg(QString::fromUtf8(p25VoiceDiagLabel(code)))
        .arg(diag.talkgroupId)
        .arg(diag.syncs)
        .arg(diag.nids)
        .arg(diag.nidLock ? "yes" : "no")
        .arg(diag.imbeFrames)
        .arg(diag.decodedFrames)
        .arg(diag.audioSamples)
        .arg(diag.phase2Bursts)
        .arg(diag.phase2VoiceCodewords)
        .arg(diag.phase2SuperframeBursts)
        .arg(diag.phase2MaskedBursts)
        .arg(diag.phase2MacCrcValid)
        .arg(diag.phase2MacPdus)
        .arg(p25Phase2AcchStatsText(diag))
        .arg(diag.phase2EssKnown ? (diag.phase2EssEncrypted ? "enc" : "clear") : "unknown")
        .arg(diag.backendAvailable ? "yes" : "no");
}

static IqTestCaptureResult saveCliP25FollowIqCapture(DeviceManager& mgr,
                                                     size_t devIndex,
                                                     double controlFreqHz,
                                                     const P25TalkgroupEntry& tg,
                                                     double requestedSeconds,
                                                     const P25VoiceDiagSnapshot& finalDiag,
                                                     const QString& reason,
                                                     const std::string& lastVoiceSig)
{
    IqTestCaptureRequest req;
    const QString reasonToken = reason.isEmpty() ? QStringLiteral("diag") : reason;
    req.label = QString("p25_follow_TG_%1_%2MHz_%3")
        .arg(tg.talkgroupId)
        .arg(tg.lastVoiceFreqHz / 1e6, 0, 'f', 5)
        .arg(reasonToken)
        .toStdString();
    req.deviceIndex = devIndex;
    req.tunedFreqHz = tg.lastVoiceFreqHz > 0.0 ? tg.lastVoiceFreqHz : controlFreqHz;
    req.centerFreqHz = req.tunedFreqHz;
    req.mode = DemodMode::NFM;
    req.channelBwHz = 12500.0;
    req.lpfHz = 3000.0;
    req.audioLpfEnabled = false;
    req.squelchDb = -105.0;
    req.requestedSeconds = std::clamp(requestedSeconds, 1.0, 20.0);
    req.captureEndedUtc = QDateTime::currentDateTimeUtc();
    req.captureStartedUtc = req.captureEndedUtc.addMSecs(-static_cast<qint64>(std::llround(req.requestedSeconds * 1000.0)));
    req.signalLevelDb = gLastRmsDb.load(std::memory_order_relaxed);
    req.noiseFloorDb = gLastNoiseFloorDb.load(std::memory_order_relaxed);
    req.snrDb = gLastSnrDb.load(std::memory_order_relaxed);
    req.afcOffsetHz = gLastAfcOffsetHz.load(std::memory_order_relaxed);

    const auto devices = mgr.getDevices();
    if (devIndex < devices.size()) {
        req.device = devices[devIndex];
        if (req.device.sampleRate > 0.0 && std::isfinite(req.device.sampleRate)) {
            req.sampleRateHz = req.device.sampleRate;
        }
    }

    double spectrumCenterHz = 0.0;
    double spectrumSampleRateHz = 0.0;
    std::vector<float> spectrum;
    if (mgr.getLatestSpectrum(devIndex, spectrum, spectrumCenterHz, spectrumSampleRateHz) &&
        spectrumSampleRateHz > 0.0 && std::isfinite(spectrumSampleRateHz)) {
        req.spectrumDb = std::move(spectrum);
        req.sampleRateHz = spectrumSampleRateHz;
    }

    const size_t requestedSamples = static_cast<size_t>(std::clamp(
        req.sampleRateHz * req.requestedSeconds,
        16384.0,
        48000000.0));
    const auto window = mgr.getRecentIQWindowWithCursor(devIndex, requestedSamples);
    req.iq = window.samples;
    req.startAbsolute = window.startAbsolute;
    req.endAbsolute = window.endAbsolute;

    req.p25LogSnapshot
        << "CLI P25 waitgrant follow IQ capture"
        << QString("reason=%1").arg(reasonToken)
        << QString("control=%1 MHz voice=%2 MHz tg=%3 protocol=%4 slot=%5")
              .arg(controlFreqHz / 1e6, 0, 'f', 5)
              .arg(req.tunedFreqHz / 1e6, 0, 'f', 5)
              .arg(tg.talkgroupId)
              .arg(p25TalkgroupIsPhase2(tg) ? "P2 TDMA" : "P1 FDMA")
              .arg(tg.tdmaSlotKnown ? QString::number(static_cast<int>(tg.tdmaSlot & 0x01u)) : QStringLiteral("unknown"))
        << p25FollowDetailLogText(tg)
        << QString("last_voice_signature=%1").arg(QString::fromStdString(lastVoiceSig.empty() ? "none" : lastVoiceSig))
        << p25VoiceDiagCaptureSummary(finalDiag);

    return saveIqTestCapture(req);
}

static TrainingCaptureResult saveTrainingCapture(const TrainingCaptureRequest& req)
{
    TrainingCaptureResult out;
    if (req.iq.empty()) {
        out.message = "No IQ samples available yet. Start/tune a device and wait for spectrum/audio first.";
        return out;
    }
    if (req.sampleRateHz <= 0.0 || !std::isfinite(req.sampleRateHz)) {
        out.message = "Invalid sample rate for training capture.";
        return out;
    }

    const QString root = trainingCapturesRoot();
    const QString stamp = QDateTime::currentDateTimeUtc().toString("yyyyMMdd_HHmmss_zzz");
    const std::string labelToken = sanitizeFileToken(req.label);
    const QString baseName = QString("%1_%2_%3MHz")
        .arg(stamp)
        .arg(QString::fromStdString(labelToken))
        .arg(req.tunedFreqHz / 1e6, 0, 'f', 5);
    const QString dir = root + "/" + baseName;
    if (!QDir().mkpath(dir)) {
        out.message = "Could not create training capture directory.";
        return out;
    }

    const std::string base = (dir + "/" + baseName).toStdString();
    const std::string dataPath = base + ".sigmf-data";
    const std::string metaPath = base + ".sigmf-meta";
    const std::string tilePgmPath = base + "_tile.pgm";
    const std::string tileF32Path = base + "_tile.f32";

    {
        std::ofstream data(dataPath, std::ios::binary);
        if (!data.is_open()) {
            out.message = "Could not write SigMF data file.";
            return out;
        }
        for (const auto& s : req.iq) {
            const float re = s.real();
            const float im = s.imag();
            data.write(reinterpret_cast<const char*>(&re), sizeof(float));
            data.write(reinterpret_cast<const char*>(&im), sizeof(float));
        }
    }

    if (req.tile.valid()) {
        writeClassifierTilePreview(req.tile, tilePgmPath, tileF32Path);
    }

    json meta;
    meta["global"] = {
        {"core:datatype", "cf32_le"},
        {"core:sample_rate", req.sampleRateHz},
        {"core:version", "1.2.0"},
        {"core:description", "SDR Town classifier training capture"},
        {"core:recorder", "SDR Town"},
        {"sdrtown:label", req.label},
        {"sdrtown:mode", modeToString(req.mode)},
        {"sdrtown:channel_bandwidth_hz", req.channelBwHz},
        {"sdrtown:audio_lpf_hz", req.lpfHz},
        {"sdrtown:audio_lpf_enabled", req.audioLpfEnabled},
        {"sdrtown:squelch_db", req.squelchDb},
        {"sdrtown:device_index", req.deviceIndex},
        {"sdrtown:device_driver", req.device.driver},
        {"sdrtown:device_label", req.device.label},
        {"sdrtown:device_serial", req.device.serial},
        {"sdrtown:rf_gain_db", req.device.gain},
        {"sdrtown:frequency_correction_ppm", req.device.frequencyCorrectionPpm},
        {"sdrtown:classifier_label", req.recommendation.label},
        {"sdrtown:classifier_confidence", req.recommendation.confidence},
        {"sdrtown:classifier_reason", req.recommendation.reason},
        {"sdrtown:classifier_filter", classifierFilterKindToString(req.recommendation.filterKind)},
        {"sdrtown:classifier_standard_bandwidth_hz", req.recommendation.standardBandwidthHz}
    };
    meta["captures"] = json::array({
        {
            {"core:sample_start", 0},
            {"core:frequency", req.centerFreqHz},
            {"core:datetime", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toStdString()}
        }
    });
    meta["annotations"] = json::array({
        {
            {"core:sample_start", 0},
            {"core:sample_count", req.iq.size()},
            {"core:freq_lower_edge", req.tunedFreqHz - req.channelBwHz * 0.5},
            {"core:freq_upper_edge", req.tunedFreqHz + req.channelBwHz * 0.5},
            {"core:label", req.label},
            {"sdrtown:classifier_label", req.recommendation.label},
            {"sdrtown:estimated_bandwidth_hz", req.recommendation.estimatedBandwidthHz},
            {"sdrtown:standard_bandwidth_hz", req.recommendation.standardBandwidthHz},
            {"sdrtown:snr_db", req.recommendation.features.snrDb}
        }
    });
    meta["sdrtown:artifacts"] = {
        {"tile_preview_pgm", QFileInfo(QString::fromStdString(tilePgmPath)).fileName().toStdString()},
        {"tile_f32", QFileInfo(QString::fromStdString(tileF32Path)).fileName().toStdString()},
        {"tile_width", req.tile.width},
        {"tile_height", req.tile.height},
        {"tile_min_db", req.tile.minDb},
        {"tile_max_db", req.tile.maxDb}
    };

    try {
        std::ofstream metaOut(metaPath);
        if (!metaOut.is_open()) {
            out.message = "Could not write SigMF metadata file.";
            return out;
        }
        metaOut << meta.dump(2);

        std::ofstream manifest((root + "/manifest.jsonl").toStdString(), std::ios::app);
        if (manifest.is_open()) {
            json row = {
                {"created_utc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toStdString()},
                {"label", req.label},
                {"freq_hz", req.tunedFreqHz},
                {"sample_rate_hz", req.sampleRateHz},
                {"sample_count", req.iq.size()},
                {"meta", QFileInfo(QString::fromStdString(metaPath)).fileName().toStdString()},
                {"data", QFileInfo(QString::fromStdString(dataPath)).fileName().toStdString()},
                {"directory", dir.toStdString()},
                {"classifier_label", req.recommendation.label},
                {"classifier_confidence", req.recommendation.confidence}
            };
            manifest << row.dump() << "\n";
        }
    } catch (const std::exception& ex) {
        out.message = QString("Training capture write failed: %1").arg(ex.what());
        return out;
    }

    out.ok = true;
    out.directory = dir;
    out.message = QString("Saved training capture: %1 samples").arg(req.iq.size());
    return out;
}

static double defaultBandwidthForMode(DemodMode mode)
{
    switch (mode) {
        case DemodMode::WFM:
        case DemodMode::AUTO: return 180000.0;
        case DemodMode::AM: return 20000.0;
        case DemodMode::CW: return 1000.0;
        case DemodMode::USB:
        case DemodMode::LSB: return 6000.0;
        case DemodMode::NFM:
        default: return 12500.0;
    }
}

static double defaultLpfForMode(DemodMode mode)
{
    switch (mode) {
        case DemodMode::WFM:
        case DemodMode::AUTO: return 15000.0;
        case DemodMode::AM: return 9000.0;
        case DemodMode::CW: return 900.0;
        case DemodMode::USB:
        case DemodMode::LSB: return 3000.0;
        case DemodMode::NFM:
        default: return 3000.0;
    }
}

static double lpfForModeAndBandwidth(DemodMode mode, double bandwidthHz)
{
    const double bw = (std::isfinite(bandwidthHz) && bandwidthHz > 0.0)
        ? bandwidthHz
        : defaultBandwidthForMode(mode);

    switch (mode) {
        case DemodMode::WFM:
        case DemodMode::AUTO:
            return 15000.0;
        case DemodMode::AM:
            return std::clamp(std::min(9000.0, bw * 0.45), 2500.0, 10000.0);
        case DemodMode::CW:
            return std::clamp(std::min(900.0, bw * 0.90), 300.0, 1200.0);
        case DemodMode::USB:
        case DemodMode::LSB:
            return std::clamp(std::min(3000.0, bw * 0.95), 1800.0, 3600.0);
        case DemodMode::NFM:
        default:
            return std::clamp(std::min(4500.0, bw * 0.35), 2500.0, 5000.0);
    }
}

static double snapBandwidthForMode(DemodMode mode, double detectedHz, double tunedFreqHz)
{
    double bw = std::isfinite(detectedHz) && detectedHz > 0.0 ? detectedHz : defaultBandwidthForMode(mode);
    switch (mode) {
        case DemodMode::WFM:
        case DemodMode::AUTO:
            return std::clamp(bw, 120000.0, 220000.0);
        case DemodMode::AM:
            if (bw <= 8000.0) return 6000.0;
            if (bw <= 14000.0) return 10000.0;
            if (bw <= 18000.0) return 15000.0;
            if (bw <= 26000.0) return 20000.0;
            return std::clamp(bw, 6000.0, 30000.0);
        case DemodMode::USB:
        case DemodMode::LSB:
            return 6000.0;
        case DemodMode::CW:
            if (bw <= 500.0) return 500.0;
            if (bw <= 1200.0) return 1000.0;
            return std::clamp(bw, 500.0, 2000.0);
        case DemodMode::NFM:
        default:
            // Modern CB/PMR/LMR narrowband spacing is commonly 12.5 kHz; support 25 kHz when the signal is visibly wider.
            (void)tunedFreqHz;
            if (bw <= 18000.0) return 12500.0;
            if (bw <= 36000.0) return 25000.0;
            return std::clamp(bw, 12500.0, 50000.0);
    }
}

struct SmartModeSelection {
    DemodMode mode = DemodMode::NFM;
    double bandwidthHz = 12500.0;
    double lpfHz = 3000.0;
    std::string source;
    SignalRecommendation classifier;
};

static bool isHfDxPlan(const BandPlanEntry& plan)
{
    return plan.endHz <= 30.0e6;
}

static bool hfClassifierCanOverridePlan(const SignalRecommendation& rec,
                                        const BandPlanEntry& plan)
{
    if (rec.confidence < 0.70) return false;
    if (rec.demodMode == plan.mode) return true;
    if (rec.demodMode == DemodMode::CW && rec.confidence >= 0.70) return true;
    if (rec.demodMode == DemodMode::AM && plan.mode == DemodMode::AM) return true;
    if ((rec.demodMode == DemodMode::USB || rec.demodMode == DemodMode::LSB) &&
        (plan.mode == DemodMode::USB || plan.mode == DemodMode::LSB) &&
        rec.confidence >= 0.78) {
        return true;
    }
    return false;
}

static SmartModeSelection chooseSmartModeAndBandwidth(const std::vector<float>& powerDb,
                                                      double sampleRateHz,
                                                      double centerFreqHz,
                                                      double tunedFreqHz,
                                                      DemodMode requestedMode,
                                                      const SignalRecommendation* preferredRecommendation = nullptr)
{
    SmartModeSelection out;
    out.classifier = preferredRecommendation
        ? *preferredRecommendation
        : AdvancedSignalClassifier::instance().classifySpectrum(powerDb, sampleRateHz, centerFreqHz, tunedFreqHz);

    const BandPlanEntry* plan = findBandPlanForFrequency(tunedFreqHz);
    if (requestedMode == DemodMode::AUTO && plan && isHfDxPlan(*plan) &&
        !hfClassifierCanOverridePlan(out.classifier, *plan)) {
        out.mode = plan->mode;
        out.bandwidthHz = plan->bandwidthHz;
        out.lpfHz = plan->lpfHz;
        out.source = plan->name + " plan prior";
        return out;
    }

    if (requestedMode == DemodMode::AUTO && out.classifier.confidence >= 0.70) {
        out.mode = out.classifier.demodMode;
        out.bandwidthHz = out.classifier.standardBandwidthHz;
        out.lpfHz = out.classifier.audioLowPassHz;
        out.source = out.classifier.label + " classifier";
        return out;
    }

    if (plan) {
        if (requestedMode == DemodMode::AUTO || requestedMode == plan->mode) {
            out.mode = plan->mode;
            out.bandwidthHz = plan->bandwidthHz;
            out.lpfHz = plan->lpfHz;
            out.source = plan->name + (out.classifier.confidence > 0.0 ? " plan" : "");
            return out;
        }
    }

    out.mode = requestedMode == DemodMode::AUTO ? out.classifier.demodMode : requestedMode;
    if (out.mode == DemodMode::AUTO) out.mode = DemodMode::NFM;

    if (out.classifier.confidence >= 0.45 && out.classifier.demodMode == out.mode) {
        out.bandwidthHz = out.classifier.standardBandwidthHz;
        out.lpfHz = out.classifier.audioLowPassHz;
        out.source = out.classifier.label + " classifier";
    } else {
        const double searchHz = (out.mode == DemodMode::WFM) ? 300000.0 : 50000.0;
        const double detected = detectChannelBandwidthAround(powerDb, sampleRateHz, centerFreqHz, tunedFreqHz, searchHz);
        out.bandwidthHz = snapBandwidthForMode(out.mode, detected, tunedFreqHz);
        out.lpfHz = lpfForModeAndBandwidth(out.mode, out.bandwidthHz);
        out.source = "signal fallback";
    }
    return out;
}

static double percentileDb(std::vector<double> values, double percentile, double fallback)
{
    values.erase(std::remove_if(values.begin(), values.end(), [](double v) {
        return !std::isfinite(v);
    }), values.end());
    if (values.empty()) return fallback;
    percentile = std::clamp(percentile, 0.0, 1.0);
    size_t idx = static_cast<size_t>(std::llround(percentile * (values.size() - 1)));
    idx = std::min(idx, values.size() - 1);
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(idx), values.end());
    return values[idx];
}

static double linearPowerDb(double db)
{
    if (!std::isfinite(db)) return 0.0;
    db = std::clamp(db, -160.0, 80.0);
    constexpr double kMinDb = -160.0;
    constexpr double kStepDb = 0.25;
    constexpr size_t kLutSize = 961;
    static const std::array<double, kLutSize> lut = [] {
        std::array<double, kLutSize> table{};
        for (size_t i = 0; i < table.size(); ++i) {
            table[i] = std::pow(10.0, (kMinDb + static_cast<double>(i) * kStepDb) / 10.0);
        }
        return table;
    }();
    const double pos = (db - kMinDb) / kStepDb;
    const size_t lo = std::min(static_cast<size_t>(pos), kLutSize - 1);
    const size_t hi = std::min(lo + 1, kLutSize - 1);
    const double frac = pos - static_cast<double>(lo);
    return lut[lo] * (1.0 - frac) + lut[hi] * frac;
}

static double topLinearAverageDb(std::vector<double> values, size_t topCount, double fallback)
{
    values.erase(std::remove_if(values.begin(), values.end(), [](double v) {
        return !std::isfinite(v);
    }), values.end());
    if (values.empty()) return fallback;
    topCount = std::clamp<size_t>(topCount, 1, values.size());
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(topCount - 1), values.end(), std::greater<double>());
    double linear = 0.0;
    for (size_t i = 0; i < topCount; ++i) {
        linear += linearPowerDb(values[i]);
    }
    linear /= static_cast<double>(topCount);
    return 10.0 * std::log10(std::max(linear, 1e-20));
}

static RfSquelchMetrics computeRfSquelchMetrics(const std::vector<float>& powerDb,
                                                double sampleRateHz,
                                                double centerFreqHz,
                                                double targetFreqHz,
                                                double channelBwHz,
                                                DemodMode mode)
{
    RfSquelchMetrics out;
    const int bins = static_cast<int>(powerDb.size());
    if (bins < 8 || sampleRateHz <= 0.0 || !std::isfinite(sampleRateHz)) return out;

    double bw = (channelBwHz > 0.0 && std::isfinite(channelBwHz)) ? channelBwHz : defaultBandwidthForMode(mode);
    bw = std::clamp(bw, sampleRateHz / bins, sampleRateHz);

    const double binHz = sampleRateHz / static_cast<double>(bins);
    const double fullStart = centerFreqHz - sampleRateHz / 2.0;
    const double rel = (targetFreqHz - fullStart) / binHz;
    if (!std::isfinite(rel) || rel < -2.0 || rel > bins + 2.0) return out;

    const int centerBin = std::clamp(static_cast<int>(std::llround(rel - 0.5)), 0, bins - 1);
    const int halfChannelBins = std::max(2, static_cast<int>(std::ceil((bw * 0.5) / binHz)));

    double localHalfHz = std::max(bw * 4.0, 50000.0);
    if (mode == DemodMode::WFM || mode == DemodMode::AUTO) localHalfHz = std::max(bw * 1.6, 260000.0);
    if (mode == DemodMode::USB || mode == DemodMode::LSB) localHalfHz = std::max(bw * 6.0, 20000.0);
    if (mode == DemodMode::CW) localHalfHz = std::max(bw * 8.0, 8000.0);
    localHalfHz = std::min(localHalfHz, sampleRateHz * 0.5);
    const int halfLocalBins = std::max(halfChannelBins + 2, static_cast<int>(std::ceil(localHalfHz / binHz)));

    std::vector<double> local;
    local.reserve(static_cast<size_t>(halfLocalBins * 2 + 1));
    for (int i = std::max(0, centerBin - halfLocalBins); i <= std::min(bins - 1, centerBin + halfLocalBins); ++i) {
        local.push_back(powerDb[static_cast<size_t>(i)]);
    }
    if (local.size() < 8) {
        local.clear();
        for (float v : powerDb) local.push_back(v);
    }

    std::vector<double> channel;
    channel.reserve(static_cast<size_t>(halfChannelBins * 2 + 1));
    for (int i = std::max(0, centerBin - halfChannelBins); i <= std::min(bins - 1, centerBin + halfChannelBins); ++i) {
        channel.push_back(powerDb[static_cast<size_t>(i)]);
    }
    if (channel.empty()) return out;

    // Lower-percentile floor is taken from the receiver channel window itself, so squelch
    // follows the selected/auto BW around the tuned frequency rather than the whole display.
    const auto& floorBins = channel.size() >= 8 ? channel : local;
    out.noiseFloorDb = percentileDb(floorBins, 0.20, -120.0);

    // Top-bin linear average catches AM/NFM carriers without letting one unstable bin dominate.
    const size_t topCount = std::max<size_t>(1, std::min<size_t>(6, channel.size() / 20 + 1));
    out.signalLevelDb = topLinearAverageDb(channel, topCount, percentileDb(channel, 0.90, out.noiseFloorDb));
    out.snrDb = out.signalLevelDb - out.noiseFloorDb;
    out.valid = std::isfinite(out.signalLevelDb) && std::isfinite(out.noiseFloorDb);
    return out;
}

static double applyNfmAfcFromSpectrum(Receiver& rx,
                                      const std::vector<float>& powerDb,
                                      double sampleRateHz,
                                      double centerFreqHz,
                                      double nominalFreqHz,
                                      double channelBwHz,
                                      DemodMode mode)
{
    const bool narrowFm = (mode == DemodMode::NFM) || (mode == DemodMode::AUTO && channelBwHz <= 50000.0);
    if (rx.p25AfcFrozen) {
        const double frozen = rx.p25FrozenAfcOffsetHz;
        gLastAfcPpmDelta.store(estimatePpmCorrectionDelta(frozen, nominalFreqHz));
        gLastAfcConfidence.store(1.0);
        return nominalFreqHz + frozen;
    }
    if (!rx.afcEnabled || !narrowFm || channelBwHz <= 0.0 || channelBwHz > 60000.0 || powerDb.empty()) {
        rx.afcLocked = false;
        rx.afcOffsetHz = 0.0;
        gLastAfcPpmDelta.store(std::numeric_limits<double>::quiet_NaN());
        gLastAfcConfidence.store(0.0);
        gLastAfcBinHz.store(0.0);
        return nominalFreqHz;
    }

    const double searchHz = std::clamp(std::max(18000.0, channelBwHz * 1.6), 8000.0, 45000.0);
    const double maxSignalBw = std::clamp(channelBwHz * 1.35, 8000.0, 60000.0);
    const auto estimate = estimateSignalOffsetFromSpectrum(powerDb, sampleRateHz, centerFreqHz,
                                                           nominalFreqHz, searchHz, maxSignalBw);
    if (estimate.valid) {
        const double alpha = rx.afcLocked ? 0.18 : 1.0;
        const double proposedOffsetHz = rx.afcOffsetHz * (1.0 - alpha) + estimate.offsetHz * alpha;
        // Keep live AFC from chasing bursty adjacent-channel energy. P25 control/voice
        // decoding wants a stable channel center; large block-to-block jumps should be
        // learned over several windows, not applied instantly.
        const double maxStepHz = rx.afcLocked ? std::max(250.0, std::min(1200.0, estimate.binHz * 2.0)) : searchHz;
        const double deltaHz = std::clamp(proposedOffsetHz - rx.afcOffsetHz, -maxStepHz, maxStepHz);
        rx.afcOffsetHz = std::clamp(rx.afcOffsetHz + deltaHz, -searchHz, searchHz);
        rx.afcLocked = true;
        gLastAfcPpmDelta.store(estimatePpmCorrectionDelta(rx.afcOffsetHz, nominalFreqHz));
        gLastAfcConfidence.store(estimate.confidence);
        gLastAfcBinHz.store(estimate.binHz);
    } else if (rx.afcLocked) {
        rx.afcOffsetHz *= 0.92;
        if (std::abs(rx.afcOffsetHz) < 25.0) {
            rx.afcOffsetHz = 0.0;
            rx.afcLocked = false;
            gLastAfcPpmDelta.store(std::numeric_limits<double>::quiet_NaN());
            gLastAfcConfidence.store(0.0);
            gLastAfcBinHz.store(0.0);
        } else {
            gLastAfcPpmDelta.store(estimatePpmCorrectionDelta(rx.afcOffsetHz, nominalFreqHz));
            gLastAfcConfidence.store(0.0);
        }
    } else {
        gLastAfcPpmDelta.store(std::numeric_limits<double>::quiet_NaN());
        gLastAfcConfidence.store(0.0);
        gLastAfcBinHz.store(estimate.binHz);
    }

    return nominalFreqHz + rx.afcOffsetHz;
}

static double p25VoiceAfcTargetHz(const Receiver& rx, double nominalFreqHz, double channelBwHz)
{
    if (!rx.p25AfcFrozen || !std::isfinite(rx.p25FrozenAfcOffsetHz)) return nominalFreqHz;
    const double halfChannelHz = std::isfinite(channelBwHz) && channelBwHz > 0.0
        ? channelBwHz * 0.5
        : 6250.0;
    const double limitHz = std::clamp(halfChannelHz * 0.72, 1200.0, 5000.0);
    const double frozenHz = std::clamp(rx.p25FrozenAfcOffsetHz, -limitHz, limitHz);
    gLastAfcPpmDelta.store(estimatePpmCorrectionDelta(frozenHz, nominalFreqHz));
    gLastAfcConfidence.store(1.0);
    gLastAfcBinHz.store(frozenHz);
    return nominalFreqHz + frozenHz;
}

struct P25VoiceAudioBlock {
    std::vector<float> audio;
    uint32_t talkgroupId = 0;
    P25VoiceDiagCode diag = P25VoiceDiagCode::Idle;
    size_t imbeFrames = 0;
    size_t decodedFrames = 0;
    size_t syncs = 0;
    size_t nids = 0;
    size_t phase2Bursts = 0;
    size_t phase2VoiceCodewords = 0;
    size_t phase2SuperframeBursts = 0;
    size_t phase2MaskedBursts = 0;
    size_t phase2MacPdus = 0;
    size_t phase2MacCrcValid = 0;
    size_t phase2MacNominalCrcValid = 0;
    size_t phase2MacAltKindCrcValid = 0;
    size_t phase2MacBitSwapCrcValid = 0;
    size_t phase2MacSlipCrcValid = 0;
    size_t phase2MacInvertCrcValid = 0;
    bool phase2EssKnown = false;
    bool phase2EssEncrypted = false;
    bool nidLock = false;
    bool skippedEncrypted = false;
    bool waitingForClearGrant = false;
    bool backendAvailable = false;
    bool decoderRan = false;
    bool phase2VoiceUnsupported = false;
    bool phase2AudioLockMissing = false;
    bool phase2MetadataMissing = false;
    bool phase2MaskMissing = false;
    bool phase2MaskAppliedNoMacCrc = false;
    bool phase2EssMissing = false;
    bool phase2WrongSlot = false;
    bool phase2AmbeRejected = false;
    bool phase2LateEntryWaiting = false;
    size_t phase2RejectedVoiceCodewords = 0;
    size_t phase2WrongSlotVoiceCodewords = 0;
    std::string demodPath;
};


static bool p25AudioSamplesLookSafe(const std::vector<float>& audio) noexcept
{
    if (audio.empty()) return false;
    double peak = 0.0;
    double sum2 = 0.0;
    size_t count = 0;
    for (float sample : audio) {
        if (!std::isfinite(sample)) return false;
        const double v = static_cast<double>(sample);
        peak = std::max(peak, std::abs(v));
        sum2 += v * v;
        ++count;
    }
    if (count == 0) return false;
    const double rms = std::sqrt(sum2 / static_cast<double>(count));
    // mbelib normal speech is bounded around +/-1.0. Reject runaway or near-silent
    // transition junk so retunes cannot briefly open the audio gate as white noise.
    return peak <= 1.25 && rms >= 1.0e-5 && rms <= 0.95;
}

static bool p25VoiceBlockMayEmitAudio(const P25VoiceAudioBlock& out) noexcept
{
    if (out.audio.empty() || out.decodedFrames == 0) return false;
    if (out.diag != P25VoiceDiagCode::Decoding) return false;
    if (out.skippedEncrypted || out.waitingForClearGrant) return false;
    if (out.phase2AudioLockMissing || out.phase2MetadataMissing || out.phase2MaskMissing ||
        out.phase2MaskAppliedNoMacCrc || out.phase2EssMissing || out.phase2WrongSlot ||
        out.phase2AmbeRejected || out.phase2LateEntryWaiting || out.phase2VoiceUnsupported) {
        return false;
    }
    return p25AudioSamplesLookSafe(out.audio);
}

struct P25Phase2AmbeValidationFrame {
    size_t burstDibitOffset = 0;
    bool superframeBurstIndexKnown = false;
    uint8_t superframeBurstIndex = 0;
    bool grantSlotKnown = false;
    uint8_t grantSlot = 0;
    uint8_t voiceIndex = 0;
    std::string ambeBits;
    int status = static_cast<int>(P25VoiceDecodeStatus::InvalidFrame);
    int errors = 0;
    int totalErrors = 0;
    double pcmPeak = 0.0;
    double pcmRms = 0.0;
    bool accepted = false;
};

static bool p25HasValidatedNid(const P25LiveDecodeResult& live)
{
    if (live.stats.bestNidValid) return true;
    return std::any_of(live.nids.begin(), live.nids.end(), [](const P25Nid& nid) {
        return nid.fecValidated;
    });
}

static P25VoiceDiagCode chooseP25VoiceDiag(const P25VoiceAudioBlock& out)
{
    if (out.skippedEncrypted) return P25VoiceDiagCode::SkippedEncrypted;
    if (out.waitingForClearGrant) return P25VoiceDiagCode::WaitingForClearGrant;
    if (out.syncs == 0 && out.phase2Bursts == 0) return P25VoiceDiagCode::NoSync;
    if (out.nids > 0 && !out.nidLock) return P25VoiceDiagCode::NidUnlocked;
    if (!out.backendAvailable && (out.imbeFrames > 0 || out.phase2VoiceCodewords > 0)) return P25VoiceDiagCode::BackendMissing;
    if (out.phase2WrongSlot) return P25VoiceDiagCode::Phase2WrongSlot;
    if (out.phase2MaskAppliedNoMacCrc) return P25VoiceDiagCode::Phase2MaskAppliedNoMacCrc;
    if (out.phase2AudioLockMissing) return P25VoiceDiagCode::Phase2AudioLockMissing;
    if (out.phase2LateEntryWaiting) return P25VoiceDiagCode::Phase2LateEntryWaiting;
    if (out.phase2EssMissing) return P25VoiceDiagCode::Phase2EssMissing;
    if (out.phase2AmbeRejected) return P25VoiceDiagCode::Phase2AmbeRejected;
    if (out.phase2MetadataMissing) return P25VoiceDiagCode::Phase2MetadataMissing;
    if (out.phase2MaskMissing) return P25VoiceDiagCode::Phase2MaskMissing;
    if (out.phase2VoiceUnsupported) return P25VoiceDiagCode::Phase2Unsupported;
    if (out.imbeFrames == 0 && out.phase2VoiceCodewords == 0) return P25VoiceDiagCode::NoLduVoice;
    if (out.decodedFrames == 0) return P25VoiceDiagCode::NoDecodedAudio;
    return P25VoiceDiagCode::Decoding;
}

static P25VoiceDiagSnapshot makeP25VoiceDiagnostics(const P25VoiceAudioBlock& out)
{
    P25VoiceDiagSnapshot diag;
    diag.diag = static_cast<int>(out.diag);
    diag.talkgroupId = out.talkgroupId;
    diag.syncs = static_cast<long long>(out.syncs);
    diag.nids = static_cast<long long>(out.nids);
    diag.imbeFrames = static_cast<long long>(out.imbeFrames);
    diag.decodedFrames = static_cast<long long>(out.decodedFrames);
    diag.audioSamples = static_cast<long long>(out.audio.size());
    diag.phase2Bursts = static_cast<long long>(out.phase2Bursts);
    diag.phase2VoiceCodewords = static_cast<long long>(out.phase2VoiceCodewords);
    diag.phase2SuperframeBursts = static_cast<long long>(out.phase2SuperframeBursts);
    diag.phase2MaskedBursts = static_cast<long long>(out.phase2MaskedBursts);
    diag.phase2MacPdus = static_cast<long long>(out.phase2MacPdus);
    diag.phase2MacCrcValid = static_cast<long long>(out.phase2MacCrcValid);
    diag.phase2MacNominalCrcValid = static_cast<long long>(out.phase2MacNominalCrcValid);
    diag.phase2MacAltKindCrcValid = static_cast<long long>(out.phase2MacAltKindCrcValid);
    diag.phase2MacBitSwapCrcValid = static_cast<long long>(out.phase2MacBitSwapCrcValid);
    diag.phase2MacSlipCrcValid = static_cast<long long>(out.phase2MacSlipCrcValid);
    diag.phase2MacInvertCrcValid = static_cast<long long>(out.phase2MacInvertCrcValid);
    diag.phase2EssKnown = out.phase2EssKnown;
    diag.phase2EssEncrypted = out.phase2EssEncrypted;
    diag.backendAvailable = out.backendAvailable;
    diag.nidLock = out.nidLock;
    return diag;
}

static void publishP25VoiceDiagnostics(Receiver& rx, const P25VoiceAudioBlock& out, bool publishReceiver = true)
{
    if (!publishReceiver) return;
    std::lock_guard<std::mutex> lk(rx.stateMutex);
    rx.p25VoiceDiagnostics = makeP25VoiceDiagnostics(out);
}

static void clearP25VoiceDiagnostics(Receiver& rx)
{
    rx.p25VoiceDiagnostics = P25VoiceDiagSnapshot{};
}

static void clearP25VoiceFollowFieldsLocked(Receiver& rx, bool controlMute)
{
    rx.p25VoiceDecodeEnabled = false;
    rx.p25VoiceClearKnown = false;
    rx.p25VoiceEncrypted = false;
    rx.p25VoiceTalkgroupId = 0;
    rx.p25VoicePhase2 = false;
    rx.p25VoiceTdmaSlotKnown = false;
    rx.p25VoiceTdmaSlot = 0;
    rx.p25VoiceSlotProbePending = false;
    rx.p25VoiceSlotProbeRequested = 0;
    rx.p25VoiceMaskParamsKnown = false;
    rx.p25VoiceNac = 0;
    rx.p25VoiceWacn = 0;
    rx.p25VoiceSystemId = 0;
    rx.p25VoiceSettleUntilMs = 0;
    rx.p25VoiceDiscardWindows = 0;
    rx.p25ControlChannelMute = controlMute;
    rx.p25AfcFrozen = false;
    rx.p25FrozenAfcOffsetHz = 0.0;
    rx.p25VoiceResetPending = true;
    clearP25VoiceDiagnostics(rx);
}

static bool tryApplyP25VoiceResetLocked(Receiver& rx)
{
    std::unique_lock<std::recursive_mutex> dspLock(rx.dspMutex, std::try_to_lock);
    if (!dspLock.owns_lock()) return false;
    rx.resetP25VoiceState();
    rx.p25VoiceResetPending = false;
    clearP25VoiceDiagnostics(rx);
    return true;
}

static void applyP25Phase2SlotProbeLocked(Receiver& rx, uint8_t newSlot, qint64 nowMs)
{
    const bool clearKnown = rx.p25VoiceClearKnown;
    const bool encrypted = rx.p25VoiceEncrypted;
    const uint32_t talkgroupId = rx.p25VoiceTalkgroupId;
    const bool maskKnown = rx.p25VoiceMaskParamsKnown;
    const uint16_t nac = rx.p25VoiceNac;
    const uint32_t wacn = rx.p25VoiceWacn;
    const uint16_t systemId = rx.p25VoiceSystemId;

    rx.resetP25VoiceState();
    rx.p25VoiceResetPending = false;
    rx.p25VoiceDecodeEnabled = true;
    rx.p25VoiceClearKnown = clearKnown;
    rx.p25VoiceEncrypted = encrypted;
    rx.p25VoiceTalkgroupId = talkgroupId;
    rx.p25VoicePhase2 = true;
    rx.p25VoiceTdmaSlotKnown = true;
    rx.p25VoiceTdmaSlot = static_cast<uint8_t>(newSlot & 0x01u);
    rx.p25VoiceSlotProbePending = false;
    rx.p25VoiceSlotProbeRequested = 0;
    rx.p25VoiceMaskParamsKnown = maskKnown;
    rx.p25VoiceNac = nac;
    rx.p25VoiceWacn = wacn;
    rx.p25VoiceSystemId = systemId;
    rx.p25VoiceSettleUntilMs = nowMs + kP25Phase2SlotProbeSettleMs;
    rx.p25VoiceDiscardWindows = std::max<int>(rx.p25VoiceDiscardWindows, kP25Phase2SlotProbeDiscardWindows);
    rx.p25ControlChannelMute = false;
    rx.p25VoiceLiveDecoder = P25LiveDecoder(p25VoiceDecoderConfig(true));
    if (maskKnown) {
        rx.p25VoiceLiveDecoder.setPhase2MaskParameters(nac, wacn, systemId);
    } else {
        rx.p25VoiceLiveDecoder.clearPhase2MaskParameters();
    }
    clearP25VoiceDiagnostics(rx);
}

static void pushAudioFrames(AudioEngine* engine,
                            std::vector<float>& pending,
                            const std::vector<float>& audio,
                            const std::vector<size_t>& activeOutputIndices = {},
                            size_t frameSize = 240)
{
    if (!engine || audio.empty() || frameSize == 0) return;

    pending.insert(pending.end(), audio.begin(), audio.end());

    size_t consumed = 0;
    while (pending.size() - consumed >= frameSize) {
        engine->pushAudioToActiveOutputs(pending.data() + consumed, frameSize, activeOutputIndices);
        consumed += frameSize;
    }
    if (consumed > 0) {
        pending.erase(pending.begin(), pending.begin() + static_cast<std::ptrdiff_t>(consumed));
    }

    constexpr size_t kMaxPendingFrames = 240 * 20;
    constexpr size_t kKeepPendingFrames = 240 * 4;
    if (pending.size() > kMaxPendingFrames) {
        pending.erase(pending.begin(), pending.end() - static_cast<std::ptrdiff_t>(kKeepPendingFrames));
    }
}

struct RollingIqWindow {
    std::vector<std::complex<float>> samples;
    uint64_t startAbsolute = 0;
    uint64_t endAbsolute = 0;
    bool absoluteKnown = false;

    void clear()
    {
        samples.clear();
        startAbsolute = 0;
        endAbsolute = 0;
        absoluteKnown = false;
    }

    bool append(const DeviceManager::RecentIQWindow& win, size_t maxSamples)
    {
        if (win.samples.empty()) return false;

        const bool validAbsolute =
            win.endAbsolute >= win.startAbsolute &&
            (win.endAbsolute - win.startAbsolute) == static_cast<uint64_t>(win.samples.size());

        if (!validAbsolute || !absoluteKnown || samples.empty() ||
            win.startAbsolute > endAbsolute || win.endAbsolute <= startAbsolute) {
            samples = win.samples;
            startAbsolute = validAbsolute ? win.startAbsolute : 0;
            endAbsolute = validAbsolute ? win.endAbsolute : static_cast<uint64_t>(samples.size());
            absoluteKnown = validAbsolute;
        } else if (win.endAbsolute > endAbsolute) {
            const uint64_t overlap = endAbsolute > win.startAbsolute ? endAbsolute - win.startAbsolute : 0;
            const size_t firstNew = static_cast<size_t>(std::min<uint64_t>(overlap, win.samples.size()));
            samples.insert(samples.end(), win.samples.begin() + static_cast<std::ptrdiff_t>(firstNew), win.samples.end());
            endAbsolute = win.endAbsolute;
        } else {
            return false;
        }

        if (maxSamples > 0 && samples.size() > maxSamples) {
            const size_t drop = samples.size() - maxSamples;
            samples.erase(samples.begin(), samples.begin() + static_cast<std::ptrdiff_t>(drop));
            if (absoluteKnown) startAbsolute += static_cast<uint64_t>(drop);
        }
        return true;
    }
};

struct P25AudioResamplerState {
    double phase = 0.0;
    double lastInputRate = 0.0;
    double lastOutputRate = 0.0;
    float previousSample = 0.0f;
    bool havePreviousSample = false;
};

static std::mutex gP25AudioResamplerMutex;
static std::unordered_map<const Receiver*, P25AudioResamplerState> gP25AudioResamplers;

static std::vector<float> resampleDecodedP25Pcm(Receiver& rx,
                                                const std::vector<float>& pcm,
                                                double inputRate,
                                                double outputRate)
{
    if (pcm.empty() || !std::isfinite(inputRate) || !std::isfinite(outputRate) ||
        inputRate <= 0.0 || outputRate <= 0.0) {
        return {};
    }

    double peak = 0.0;
    for (float sample : pcm) {
        if (std::isfinite(sample)) peak = std::max(peak, std::abs(static_cast<double>(sample)));
    }
    const double gain = peak > 1.0 ? (0.90 / peak) : 0.90;

    std::lock_guard<std::mutex> lk(gP25AudioResamplerMutex);
    auto& st = gP25AudioResamplers[&rx];
    if (std::abs(st.lastInputRate - inputRate) > 1.0 ||
        std::abs(st.lastOutputRate - outputRate) > 1.0) {
        st.phase = 0.0;
        st.previousSample = 0.0f;
        st.havePreviousSample = false;
        st.lastInputRate = inputRate;
        st.lastOutputRate = outputRate;
    }

    // mbelib emits 160 mono samples per 20 ms at exactly 8 kHz.  This stateful
    // interpolator keeps fractional phase and previous edge sample across AMBE
    // frames so 44.1/48 kHz output does not jitter or reset at every 20 ms block.
    const double step = inputRate / outputRate;
    const double frameEnd = static_cast<double>(pcm.size());
    const size_t expected = std::max<size_t>(1, static_cast<size_t>(
        std::ceil((frameEnd - st.phase) / std::max(step, 1e-12))));
    std::vector<float> out;
    out.reserve(expected);

    while (st.phase < frameEnd) {
        const double src = st.phase;
        const long base = static_cast<long>(std::floor(src));
        const double frac = src - static_cast<double>(base);
        double a = 0.0;
        double b = 0.0;
        if (base < 0) {
            a = st.havePreviousSample ? static_cast<double>(st.previousSample) : 0.0;
            b = std::isfinite(pcm.front()) ? static_cast<double>(pcm.front()) : 0.0;
        } else {
            const size_t lo = std::min(static_cast<size_t>(base), pcm.size() - 1);
            const size_t hi = std::min(lo + 1, pcm.size() - 1);
            a = std::isfinite(pcm[lo]) ? static_cast<double>(pcm[lo]) : 0.0;
            b = std::isfinite(pcm[hi]) ? static_cast<double>(pcm[hi]) : 0.0;
        }
        const double v = (a * (1.0 - frac) + b * frac) * gain;
        out.push_back(static_cast<float>(std::clamp(v, -0.98, 0.98)));
        st.phase += step;
    }

    st.phase -= frameEnd;
    if (std::isfinite(pcm.back())) {
        st.previousSample = pcm.back();
        st.havePreviousSample = true;
    }
    return out;
}

static bool p25AmbeDecodeFrameLooksUsable(const P25VoiceDecodeResult& decoded)
{
    if (decoded.status != P25VoiceDecodeStatus::Decoded || decoded.pcm.empty()) return false;

    double peak = 0.0;
    double sumSquares = 0.0;
    for (float sample : decoded.pcm) {
        if (!std::isfinite(sample)) return false;
        const double v = static_cast<double>(sample);
        peak = std::max(peak, std::abs(v));
        sumSquares += v * v;
    }
    if (peak <= 1e-7 || sumSquares <= 1e-10) return false;

    // mbelib's AMBE path exposes hard-decision FEC correction counts. Phase 2
    // audio still requires the upstream TDMA superframe/mask gate before this
    // plausibility check is allowed to release samples.
    return decoded.totalErrors <= 18;
}

static QString p25Phase2ValidationPath()
{
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(appData + "/logs");
    return appData + "/logs/p25_phase2_validation.jsonl";
}

static bool p25Phase2ValidationLoggingEnabled()
{
    static const bool enabled = [] {
        const QByteArray value = qgetenv("SDR_TOWN_P25_VALIDATION_LOG").trimmed().toLower();
        const bool on = value == "1" || value == "true" || value == "yes" || value == "on";
        if (on) {
            spdlog::warn("P25 validation logging enabled: raw diagnostic symbols and metadata may be sensitive.");
        }
        return on;
    }();
    return enabled;
}

static bool p25Phase2ValidationRedactionEnabled()
{
    static const bool enabled = [] {
        const QByteArray value = qgetenv("SDR_TOWN_P25_VALIDATION_REDACT").trimmed().toLower();
        return value == "1" || value == "true" || value == "yes" || value == "on";
    }();
    return enabled;
}

static void rotateP25Phase2ValidationLogIfNeeded(const QString& path)
{
    static qint64 lastRotateCheckMs = 0;
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (nowMs - lastRotateCheckMs < 5000) return;
    lastRotateCheckMs = nowMs;

    constexpr qint64 kMaxValidationLogBytes = 8LL * 1024LL * 1024LL;
    QFileInfo info(path);
    if (!info.exists() || info.size() <= kMaxValidationLogBytes) return;

    const QString rotated = path + ".1";
    QFile::remove(rotated);
    if (!QFile::rename(path, rotated)) {
        spdlog::warn("Failed to rotate P25 Phase 2 validation log at {}", path.toStdString());
    }
}

static std::string p25CompactDibits(const std::vector<int>& dibits)
{
    std::string out;
    out.reserve(dibits.size());
    for (int d : dibits) out.push_back(static_cast<char>('0' + (d & 0x03)));
    return out;
}

static std::string p25CompactBits(const std::array<uint8_t, 96>& bits)
{
    std::string out;
    out.reserve(bits.size());
    for (uint8_t bit : bits) out.push_back(bit ? '1' : '0');
    return out;
}

static json p25Phase2EssJson(const P25Phase2EssState& ess, bool redactSensitive = false)
{
    json out;
    out["known"] = ess.known;
    out["encrypted"] = ess.encrypted;
    out["algId"] = ess.algId;
    out["keyId"] = redactSensitive ? json(nullptr) : json(ess.keyId);
    out["fecValidated"] = ess.fecValidated;
    out["correctedSymbols"] = ess.correctedSymbols;
    std::vector<uint8_t> mi(ess.messageIndicator.begin(), ess.messageIndicator.end());
    out["messageIndicatorHex"] = redactSensitive ? std::string("<redacted>") : p25BytesToHex(mi).toStdString();
    out["redacted"] = redactSensitive;
    return out;
}

static void writeP25Phase2ValidationRecord(const Receiver& rx,
                                           const P25LiveDecodeResult& live,
                                           const P25VoiceAudioBlock& audio,
                                           const std::vector<P25Phase2AmbeValidationFrame>& ambeFrames,
                                           double sampleRateHz,
                                           double centerFreqHz,
                                           double targetFreqHz,
                                           double outputRateHz)
{
    if (!rx.p25VoicePhase2 || live.stats.phase2Bursts == 0) return;
    if (!p25Phase2ValidationLoggingEnabled()) return;
    const bool redactRaw = p25Phase2ValidationRedactionEnabled();

    static std::mutex validationMutex;
    static qint64 lastWriteMs = 0;
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

    std::lock_guard<std::mutex> lk(validationMutex);
    if (audio.decodedFrames == 0 && nowMs - lastWriteMs < 1000) return;
    lastWriteMs = nowMs;

    json record;
    record["schema"] = "sdr-town-p25-phase2-validation-v1";
    record["timeUtc"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toStdString();
    record["talkgroupId"] = rx.p25VoiceTalkgroupId;
    record["centerFreqHz"] = centerFreqHz;
    record["targetFreqHz"] = targetFreqHz;
    record["sampleRateHz"] = sampleRateHz;
    record["outputRateHz"] = outputRateHz;
    record["demodPath"] = live.stats.demodPath;
    record["inputTargetOffsetHz"] = live.stats.inputTargetOffsetHz;
    record["channelSampleRateHz"] = live.stats.channelSampleRate;
    record["discriminatorMeanHz"] = live.stats.discriminatorMeanHz;
    record["diag"] = p25VoiceDiagLabel(audio.diag);

    record["mask"] = {
        {"known", rx.p25VoiceMaskParamsKnown},
        {"nac", rx.p25VoiceNac},
        {"wacn", rx.p25VoiceWacn},
        {"systemId", rx.p25VoiceSystemId},
        {"followedGrantSlotKnown", rx.p25VoiceTdmaSlotKnown},
        {"followedGrantSlot", rx.p25VoiceTdmaSlotKnown ? static_cast<int>(rx.p25VoiceTdmaSlot & 0x01u) : -1},
        {"phaseKnown", live.stats.phase2MaskPhaseKnown},
        {"phase", live.stats.phase2MaskPhase},
        {"phaseScore", live.stats.phase2MaskPhaseScore},
        {"phaseMacCrcValid", live.stats.phase2MaskPhaseMacCrcValid},
    };
    record["ess"] = p25Phase2EssJson(live.phase2Ess, redactRaw);
    record["stats"] = {
        {"syncs", audio.syncs},
        {"nids", audio.nids},
        {"nidLock", audio.nidLock},
        {"phase2Bursts", live.stats.phase2Bursts},
        {"phase2VoiceCodewords", live.stats.phase2VoiceCodewords},
        {"phase2SuperframeBursts", live.stats.phase2SuperframeBursts},
        {"phase2MaskedBursts", live.stats.phase2MaskedBursts},
        {"phase2MacPdus", live.stats.phase2MacPdus},
        {"phase2MacCrcValid", live.stats.phase2MacCrcValid},
        {"phase2MacNominalCrcValid", live.stats.phase2MacNominalCrcValid},
        {"phase2MacAltKindCrcValid", live.stats.phase2MacAltKindCrcValid},
        {"phase2MacBitSwapCrcValid", live.stats.phase2MacBitSwapCrcValid},
        {"phase2MacSlipCrcValid", live.stats.phase2MacSlipCrcValid},
        {"phase2MacInvertCrcValid", live.stats.phase2MacInvertCrcValid},
        {"phase2MaskPhaseKnown", live.stats.phase2MaskPhaseKnown},
        {"phase2MaskPhase", live.stats.phase2MaskPhase},
        {"phase2MaskPhaseScore", live.stats.phase2MaskPhaseScore},
        {"phase2MaskPhaseMacCrcValid", live.stats.phase2MaskPhaseMacCrcValid},
        {"cqpskLockActive", live.stats.cqpskLockActive},
        {"cqpskLockUsed", live.stats.cqpskLockUsed},
        {"cqpskLockUpdated", live.stats.cqpskLockUpdated},
        {"cqpskSymbolPhaseFraction", live.stats.cqpskSymbolPhaseFraction},
        {"cqpskFineCorrectionApplied", live.stats.cqpskFineCorrectionApplied},
        {"cqpskFineRotationRad", live.stats.cqpskFineRotationRad},
        {"cqpskResidualCarrierHz", live.stats.cqpskResidualCarrierHz},
        {"cqpskPhaseErrorRmsRad", live.stats.cqpskPhaseErrorRmsRad},
        {"cqpskFineCorrectionSymbols", live.stats.cqpskFineCorrectionSymbols},
        {"softDecisionSymbols", live.stats.softDecisionSymbols},
        {"softDecisionQuality", live.stats.softDecisionQuality},
        {"softBitLlrMean", live.stats.softBitLlrMean},
        {"softBitLlrMinimum", live.stats.softBitLlrMinimum},
        {"softLowConfidenceSymbols", live.stats.softLowConfidenceSymbols},
        {"phase2IschDecoded", live.stats.phase2IschDecoded},
        {"phase2IschSync", live.stats.phase2IschSync},
        {"bestPhase2SyncErrors", live.stats.bestPhase2SyncErrors},
        {"bestPhase2SyncDibitOffset", live.stats.bestPhase2SyncDibitOffset},
        {"rejectedVoiceCodewords", audio.phase2RejectedVoiceCodewords},
        {"wrongSlotVoiceCodewords", audio.phase2WrongSlotVoiceCodewords},
        {"audioLockMissing", audio.phase2AudioLockMissing},
        {"maskAppliedNoMacCrc", audio.phase2MaskAppliedNoMacCrc},
        {"essMissing", audio.phase2EssMissing},
        {"wrongSlot", audio.phase2WrongSlot},
        {"ambeRejected", audio.phase2AmbeRejected},
        {"lateEntryWaiting", audio.phase2LateEntryWaiting},
        {"decodedFrames", audio.decodedFrames},
        {"audioSamples", audio.audio.size()},
        {"backendAvailable", audio.backendAvailable},
    };

    record["macPdus"] = json::array();
    P25ControlChannelAnalyzer validationMacAnalyzer;
    for (const auto& pdu : live.phase2MacPdus) {
        json parsedEvents = json::array();
        const auto parsed = validationMacAnalyzer.ingestPhase2MacPdu(pdu.opcode, pdu.offset, pdu.bytes, pdu.crcValid);
        for (const auto& ev : parsed) {
            parsedEvents.push_back({
                {"type", p25ControlEventTypeToString(ev.type)},
                {"label", ev.label},
                {"macMessageOpcode", ev.macMessageOpcode},
                {"macMessageOffset", ev.macMessageOffset},
                {"talkgroupId", ev.talkgroupId},
                {"sourceId", ev.sourceId},
                {"channel", ev.channel},
                {"channelB", ev.channelB},
                {"phase2Candidate", ev.phase2Candidate},
                {"tdmaSlotKnown", ev.tdmaSlotKnown},
                {"tdmaSlot", ev.tdmaSlotKnown ? static_cast<int>(ev.tdmaSlot) : -1},
                {"serviceOptionsKnown", ev.serviceOptionsKnown},
                {"serviceOptions", ev.serviceOptionsKnown ? static_cast<int>(ev.serviceOptions) : -1},
                {"encryptionKnown", ev.encryptionKnown},
                {"encrypted", ev.encrypted},
            });
        }
        record["macPdus"].push_back({
            {"dibitOffset", pdu.dibitOffset},
            {"detectedKind", P25LiveDecoder::phase2BurstKindToString(pdu.detectedKind)},
            {"source", P25LiveDecoder::phase2BurstKindToString(pdu.source)},
            {"opcode", pdu.opcode},
            {"pduTypeName", p25Phase2MacPduTypeToString(pdu.opcode)},
            {"offset", pdu.offset},
            {"fecDecoded", pdu.fecDecoded},
            {"crcValid", pdu.crcValid},
            {"correctedSymbols", pdu.correctedSymbols},
            {"acchHypothesisKnown", pdu.acchHypothesisKnown},
            {"acchBitOrderSwapped", pdu.acchBitOrderSwapped},
            {"acchDibitInverted", pdu.acchDibitInverted},
            {"acchSlipDibits", pdu.acchSlipDibits},
            {"bytesHex", p25BytesToHex(pdu.bytes).toStdString()},
            {"essPresent", pdu.essPresent},
            {"ess", p25Phase2EssJson(pdu.ess, redactRaw)},
            {"parsedEvents", parsedEvents},
        });
    }

    record["bursts"] = json::array();
    for (const auto& burst : live.phase2Bursts) {
        json codewords = json::array();
        for (const auto& cw : burst.voiceCodewords) {
            std::array<uint8_t, 96> ambe = p25Phase2VoiceCodewordToAmbe3600x2450Frame(cw);
            codewords.push_back({
                {"voiceIndex", cw.voiceIndex},
                {"dibitOffset", cw.dibitOffset},
                {"ambeBits", redactRaw ? std::string("<redacted>") : p25CompactBits(ambe)},
                {"sessionCodewordIdKnown", cw.sessionCodewordIdKnown},
                {"sessionCodewordId", cw.sessionCodewordIdKnown ? static_cast<long long>(cw.sessionCodewordId) : -1},
                {"duplicateInSession", cw.duplicateInSession},
            });
        }
        record["bursts"].push_back({
            {"dibitOffset", burst.dibitOffset},
            {"syncErrors", burst.syncErrors},
            {"superframeLocked", burst.superframeLocked},
            {"superframeDibitOffset", burst.superframeDibitOffset},
            {"superframeSyncScore", burst.superframeSyncScore},
            {"superframeSyncErrors", burst.superframeSyncErrors},
            {"phase2AudioLock", burst.phase2AudioLock},
            {"tdmaSyncLock", burst.tdmaSyncLock},
            {"superframeLock", burst.superframeLock},
            {"maskPhaseLock", burst.maskPhaseLock},
            {"macCrcLock", burst.macCrcLock},
            {"sessionAudioRelease", burst.sessionAudioRelease},
            {"superframeBurstIndexKnown", burst.superframeBurstIndexKnown},
            {"superframeBurstIndex", burst.superframeBurstIndex},
            {"grantSlotKnown", burst.grantSlotKnown},
            {"grantSlot", burst.grantSlot},
            {"kind", P25LiveDecoder::phase2BurstKindToString(burst.kind)},
            {"duid", burst.duid},
            {"duidErrors", burst.duidErrors},
            {"xorMaskApplied", burst.xorMaskApplied},
            {"xorMaskPhaseKnown", burst.xorMaskPhaseKnown},
            {"xorMaskPhase", burst.xorMaskPhase},
            {"xorMaskPhaseScore", burst.xorMaskPhaseScore},
            {"macFecDecoded", burst.macFecDecoded},
            {"macCrcValid", burst.macCrcValid},
            {"essKnown", burst.essKnown},
            {"encrypted", burst.encrypted},
            {"ischValid", burst.isch.valid},
            {"ischSync", burst.isch.sync},
            {"ischErrors", burst.isch.errors},
            {"ischChannel", burst.isch.channel},
            {"ischLocation", burst.isch.location},
            {"ischFreeAccess", burst.isch.freeAccess},
            {"ischUltraframeCounter", burst.isch.ultraframeCounter},
            {"rawPayloadDibits", redactRaw ? std::string("<redacted>") : p25CompactDibits(burst.rawPayloadDibits)},
            {"postMaskPayloadDibits", redactRaw ? std::string("<redacted>") : p25CompactDibits(burst.maskedPayloadDibits)},
            {"voiceCodewords", std::move(codewords)},
        });
    }

    record["ambeFrames"] = json::array();
    for (const auto& frame : ambeFrames) {
        record["ambeFrames"].push_back({
            {"burstDibitOffset", frame.burstDibitOffset},
            {"superframeBurstIndexKnown", frame.superframeBurstIndexKnown},
            {"superframeBurstIndex", frame.superframeBurstIndex},
            {"grantSlotKnown", frame.grantSlotKnown},
            {"grantSlot", frame.grantSlot},
            {"voiceIndex", frame.voiceIndex},
            {"ambeBits", redactRaw ? std::string("<redacted>") : frame.ambeBits},
            {"status", frame.status},
            {"errors", frame.errors},
            {"totalErrors", frame.totalErrors},
            {"pcmPeak", frame.pcmPeak},
            {"pcmRms", frame.pcmRms},
            {"accepted", frame.accepted},
        });
    }

    try {
        const QString path = p25Phase2ValidationPath();
        rotateP25Phase2ValidationLogIfNeeded(path);
        std::ofstream f(path.toStdString(), std::ios::app);
        if (f.is_open()) f << record.dump() << "\n";
    } catch (const std::exception& ex) {
        spdlog::warn("Failed to write P25 Phase 2 validation record: {}", ex.what());
    }
}

static void populateP25VoiceAudioBlockFromLive(P25VoiceAudioBlock& out, const P25LiveDecodeResult& live)
{
    out.decoderRan = true;
    out.syncs = live.syncs.size();
    out.nids = live.nids.size();
    out.nidLock = p25HasValidatedNid(live);
    out.phase2Bursts = live.stats.phase2Bursts;
    out.phase2VoiceCodewords = live.stats.phase2VoiceCodewords;
    out.phase2SuperframeBursts = live.stats.phase2SuperframeBursts;
    out.phase2MaskedBursts = live.stats.phase2MaskedBursts;
    out.phase2MacPdus = live.stats.phase2MacPdus;
    out.phase2MacCrcValid = live.stats.phase2MacCrcValid;
    out.phase2MacNominalCrcValid = live.stats.phase2MacNominalCrcValid;
    out.phase2MacAltKindCrcValid = live.stats.phase2MacAltKindCrcValid;
    out.phase2MacBitSwapCrcValid = live.stats.phase2MacBitSwapCrcValid;
    out.phase2MacSlipCrcValid = live.stats.phase2MacSlipCrcValid;
    out.phase2MacInvertCrcValid = live.stats.phase2MacInvertCrcValid;
    out.phase2EssKnown = live.stats.phase2EssKnown;
    out.phase2EssEncrypted = live.stats.phase2EssEncrypted;
    out.demodPath = live.stats.demodPath;
    out.imbeFrames = live.imbeFrames.size();
}

static P25VoiceAudioBlock decodeP25Phase2VoiceBlock(Receiver& rx,
                                                    const P25LiveDecodeResult& live,
                                                    P25VoiceAudioBlock out,
                                                    double sampleRateHz,
                                                    double centerFreqHz,
                                                    double targetFreqHz,
                                                    double outputRateHz,
                                                    uint64_t windowStartAbsDibit,
                                                    bool haveAbsoluteDibits)
{
    std::vector<P25Phase2AmbeValidationFrame> ambeFrames;
    bool sawVoice = false;
    bool acceptedVoice = false;
    bool skippedDuplicateVoice = false;
    bool attemptedNewVoice = false;

    for (const auto& burst : live.phase2Bursts) {
        if (burst.voiceCodewords.empty()) continue;
        sawVoice = true;
        if (!rx.p25VoiceTdmaSlotKnown) {
            out.phase2RejectedVoiceCodewords += burst.voiceCodewords.size();
            out.phase2MetadataMissing = true;
            continue;
        }
        const bool epochTrusted = burst.superframeLock || burst.macCrcLock || burst.sessionAudioRelease;
        if (!epochTrusted) {
            out.phase2RejectedVoiceCodewords += burst.voiceCodewords.size();
            out.phase2AudioLockMissing = true;
            out.phase2MetadataMissing = true;
            continue;
        }
        if (!burst.grantSlotKnown) {
            // Do not label pre-superframe/late-entry VCWs as "wrong slot".
            // Without a superframe epoch there is no reliable slot decision yet,
            // and using this as wrong-slot evidence causes useless slot thrash.
            out.phase2RejectedVoiceCodewords += burst.voiceCodewords.size();
            out.phase2AudioLockMissing = true;
            continue;
        }
        const uint8_t followedGrantSlot = static_cast<uint8_t>(rx.p25VoiceTdmaSlot & 0x01u);
        if (burst.grantSlot != followedGrantSlot) {
            out.phase2RejectedVoiceCodewords += burst.voiceCodewords.size();
            out.phase2WrongSlotVoiceCodewords += burst.voiceCodewords.size();
            out.phase2WrongSlot = true;
            continue;
        }
        if (!burst.xorMaskApplied) {
            out.phase2RejectedVoiceCodewords += burst.voiceCodewords.size();
            out.phase2MaskMissing = true;
            continue;
        }
        const bool maskPhaseTrusted = burst.maskPhaseLock || burst.macCrcLock || burst.sessionAudioRelease;
        if (!maskPhaseTrusted) {
            out.phase2RejectedVoiceCodewords += burst.voiceCodewords.size();
            out.phase2AudioLockMissing = true;
            out.phase2MetadataMissing = true;
            continue;
        }
        const bool metadataTrusted = burst.macCrcLock || (burst.essKnown && !burst.encrypted && burst.sessionAudioRelease);
        if (!metadataTrusted) {
            out.phase2RejectedVoiceCodewords += burst.voiceCodewords.size();
            out.phase2AudioLockMissing = true;
            out.phase2MaskAppliedNoMacCrc = true;
            continue;
        }
        if (!burst.essKnown) {
            out.phase2RejectedVoiceCodewords += burst.voiceCodewords.size();
            out.phase2EssMissing = true;
            out.phase2LateEntryWaiting = true;
            out.phase2MetadataMissing = true;
            continue;
        }
        if (burst.encrypted) {
            out.phase2RejectedVoiceCodewords += burst.voiceCodewords.size();
            out.skippedEncrypted = true;
            continue;
        }
        if (!burst.sessionAudioRelease) {
            out.phase2RejectedVoiceCodewords += burst.voiceCodewords.size();
            out.phase2AudioLockMissing = true;
            continue;
        }

        for (const auto& codeword : burst.voiceCodewords) {
            const uint64_t codewordAbsDibit = haveAbsoluteDibits
                ? windowStartAbsDibit + static_cast<uint64_t>(codeword.dibitOffset)
                : 0;
            const uint64_t codewordEndAbsDibit = codewordAbsDibit + 36u;
            constexpr uint64_t kPhase2DuplicateDibitTolerance = 8u;
            if (codeword.duplicateInSession) {
                skippedDuplicateVoice = true;
                continue;
            }
            if (!codeword.sessionCodewordIdKnown &&
                haveAbsoluteDibits &&
                rx.p25Phase2LastEmittedAbsDibit != 0 &&
                codewordEndAbsDibit <= rx.p25Phase2LastEmittedAbsDibit + kPhase2DuplicateDibitTolerance) {
                skippedDuplicateVoice = true;
                continue;
            }
            attemptedNewVoice = true;
            const auto ambeFrame = p25Phase2VoiceCodewordToAmbe3600x2450Frame(codeword);
            const auto decoded = rx.p25AmbeVoiceDecoder.decodeAmbe3600x2450Frame(ambeFrame);
            P25Phase2AmbeValidationFrame frame;
            frame.burstDibitOffset = burst.dibitOffset;
            frame.superframeBurstIndexKnown = burst.superframeBurstIndexKnown;
            frame.superframeBurstIndex = burst.superframeBurstIndex;
            frame.grantSlotKnown = burst.grantSlotKnown;
            frame.grantSlot = burst.grantSlot;
            frame.voiceIndex = codeword.voiceIndex;
            frame.ambeBits = p25CompactBits(ambeFrame);
            frame.status = static_cast<int>(decoded.status);
            frame.errors = decoded.errors;
            frame.totalErrors = decoded.totalErrors;
            for (float sample : decoded.pcm) {
                if (!std::isfinite(sample)) continue;
                const double v = static_cast<double>(sample);
                frame.pcmPeak = std::max(frame.pcmPeak, std::abs(v));
                frame.pcmRms += v * v;
            }
            if (!decoded.pcm.empty()) frame.pcmRms = std::sqrt(frame.pcmRms / static_cast<double>(decoded.pcm.size()));
            frame.accepted = p25AmbeDecodeFrameLooksUsable(decoded);
            ambeFrames.push_back(frame);
            if (!frame.accepted) {
                ++out.phase2RejectedVoiceCodewords;
                out.phase2AmbeRejected = true;
                continue;
            }
            auto block = resampleDecodedP25Pcm(rx, decoded.pcm, decoded.sampleRate, outputRateHz);
            out.audio.insert(out.audio.end(), block.begin(), block.end());
            ++out.decodedFrames;
            if (haveAbsoluteDibits) {
                rx.p25Phase2LastEmittedAbsDibit = std::max(rx.p25Phase2LastEmittedAbsDibit, codewordEndAbsDibit);
            }
            acceptedVoice = true;
        }
    }

    if (sawVoice && skippedDuplicateVoice && !attemptedNewVoice && !acceptedVoice) {
        out.diag = P25VoiceDiagCode::Decoding;
        writeP25Phase2ValidationRecord(rx, live, out, ambeFrames, sampleRateHz, centerFreqHz, targetFreqHz, outputRateHz);
        return out;
    }
    if (sawVoice && !acceptedVoice) {
        if (out.skippedEncrypted) {
            out.diag = P25VoiceDiagCode::SkippedEncrypted;
            writeP25Phase2ValidationRecord(rx, live, out, ambeFrames, sampleRateHz, centerFreqHz, targetFreqHz, outputRateHz);
            return out;
        }
        if (rx.p25VoiceMaskParamsKnown && out.phase2MaskedBursts > 0 && out.phase2MacCrcValid == 0 && !out.phase2EssKnown) {
            out.phase2MaskAppliedNoMacCrc = true;
        }
        if (!out.phase2EssKnown && rx.p25VoiceMaskParamsKnown && out.phase2MaskedBursts > 0) {
            out.phase2EssMissing = true;
            out.phase2LateEntryWaiting = true;
            out.phase2MetadataMissing = true;
        } else if (rx.p25VoiceMaskParamsKnown && out.phase2MaskedBursts == 0) {
            out.phase2MaskMissing = true;
        } else if (!rx.p25VoiceMaskParamsKnown) {
            out.phase2MetadataMissing = true;
        }
        out.phase2VoiceUnsupported = true;
    }
    out.diag = chooseP25VoiceDiag(out);
    writeP25Phase2ValidationRecord(rx, live, out, ambeFrames, sampleRateHz, centerFreqHz, targetFreqHz, outputRateHz);
    return out;
}

static P25VoiceAudioBlock decodeP25Phase1VoiceBlock(Receiver& rx,
                                                    const P25LiveDecodeResult& live,
                                                    P25VoiceAudioBlock out,
                                                    double outputRateHz)
{
    // Production gate: do not let a Phase 1 IMBE false positive open audio while
    // the receiver is actually seeing Phase 2/CQPSK control or TDMA fragments.
    // This is the common source of the one-second white-noise blip during
    // frequency/voice-follow transitions.
    if (!out.nidLock || out.phase2Bursts > 0 || out.phase2VoiceCodewords > 0) {
        if (out.phase2Bursts > 0 || out.phase2VoiceCodewords > 0) out.phase2VoiceUnsupported = true;
        out.diag = chooseP25VoiceDiag(out);
        return out;
    }
    for (const auto& frame : live.imbeFrames) {
        if (!frame.valid) continue;
        const auto decoded = rx.p25ImbeVoiceDecoder.decodeImbe4400Frame(frame.imbe88);
        if (decoded.status != P25VoiceDecodeStatus::Decoded || decoded.pcm.empty()) continue;
        auto block = resampleDecodedP25Pcm(rx, decoded.pcm, decoded.sampleRate, outputRateHz);
        out.audio.insert(out.audio.end(), block.begin(), block.end());
        ++out.decodedFrames;
    }
    out.diag = chooseP25VoiceDiag(out);
    return out;
}

static P25VoiceAudioBlock decodeP25VoiceAudioBlock(Receiver& rx,
                                                   const std::vector<std::complex<float>>& iq,
                                                   double sampleRateHz,
                                                   double centerFreqHz,
                                                   double targetFreqHz,
                                                   double outputRateHz,
                                                   uint64_t iqStartAbsolute = 0,
                                                   bool iqStartAbsoluteKnown = false)
{
    P25VoiceAudioBlock out;
    out.talkgroupId = rx.p25VoiceTalkgroupId;
    out.backendAvailable = rx.p25VoicePhase2
        ? rx.p25AmbeVoiceDecoder.backendAvailable()
        : rx.p25ImbeVoiceDecoder.backendAvailable();
    if (!rx.p25VoiceDecodeEnabled || iq.empty()) {
        out.diag = P25VoiceDiagCode::Idle;
        return out;
    }
    if (rx.p25VoiceEncrypted && !rx.p25VoicePhase2) {
        out.skippedEncrypted = true;
        out.diag = P25VoiceDiagCode::SkippedEncrypted;
        return out;
    }
    if (!rx.p25VoiceClearKnown && !rx.p25VoicePhase2) {
        out.waitingForClearGrant = true;
        out.diag = P25VoiceDiagCode::WaitingForClearGrant;
        return out;
    }

    const auto live = rx.p25VoiceLiveDecoder.processIq(iq, sampleRateHz, centerFreqHz, targetFreqHz);
    populateP25VoiceAudioBlockFromLive(out, live);

    const bool haveAbsoluteDibits = iqStartAbsoluteKnown &&
        std::isfinite(sampleRateHz) && sampleRateHz > 0.0 &&
        std::isfinite(live.stats.symbolRate) && live.stats.symbolRate > 0.0;
    const uint64_t windowStartAbsDibit = haveAbsoluteDibits
        ? static_cast<uint64_t>(std::llround(static_cast<long double>(iqStartAbsolute) *
                                             static_cast<long double>(live.stats.symbolRate) /
                                             static_cast<long double>(sampleRateHz)))
        : 0;

    if (rx.p25VoicePhase2 && out.phase2EssKnown) {
        rx.p25VoiceClearKnown = true;
        rx.p25VoiceEncrypted = out.phase2EssEncrypted;
    }
    if (rx.p25VoicePhase2 && out.phase2EssKnown && out.phase2EssEncrypted) {
        out.skippedEncrypted = true;
        out.diag = P25VoiceDiagCode::SkippedEncrypted;
        writeP25Phase2ValidationRecord(rx, live, out, {}, sampleRateHz, centerFreqHz, targetFreqHz, outputRateHz);
        return out;
    }
    if (!out.backendAvailable) {
        out.diag = chooseP25VoiceDiag(out);
        if (rx.p25VoicePhase2) writeP25Phase2ValidationRecord(rx, live, out, {}, sampleRateHz, centerFreqHz, targetFreqHz, outputRateHz);
        return out;
    }

    if (rx.p25VoicePhase2) {
        return decodeP25Phase2VoiceBlock(rx, live, out, sampleRateHz, centerFreqHz, targetFreqHz,
            outputRateHz, windowStartAbsDibit, haveAbsoluteDibits);
    }

    if (out.phase2VoiceCodewords > 0) {
        out.phase2VoiceUnsupported = true;
        out.diag = chooseP25VoiceDiag(out);
        writeP25Phase2ValidationRecord(rx, live, out, {}, sampleRateHz, centerFreqHz, targetFreqHz, outputRateHz);
        return out;
    }

    return decodeP25Phase1VoiceBlock(rx, live, out, outputRateHz);
}

static void runP25ReplayFollowTest(const P25ReplayCliArgs& args)
{
    const double controlMs = args.ms > 0.0 ? std::clamp(args.ms, 50.0, 60000.0) : 15000.0;
    const double followMs = args.followMs > 0.0 ? std::clamp(args.followMs, 512.0, 60000.0) : 5000.0;
    auto capture = loadSigmfCf32Capture(QString::fromStdString(args.path), controlMs + followMs, args.skipMs);
    if (!capture.ok) {
        std::cout << "P25 followtest load failed: " << capture.error << "\n";
        return;
    }
    if (args.centerMhz > 0.0 && std::isfinite(args.centerMhz)) {
        capture.centerFreqHz = args.centerMhz * 1e6;
    }
    double ccHz = args.targetMhz > 0.0 ? args.targetMhz * 1e6 : capture.targetFreqHz;
    if (!std::isfinite(ccHz) || ccHz <= 0.0) ccHz = capture.centerFreqHz;
    if (!std::isfinite(capture.sampleRateHz) || capture.sampleRateHz <= 0.0 || capture.iq.empty()) {
        std::cout << "P25 followtest result=NO_IQ samples=" << capture.iq.size()
                  << " sampleRate=" << capture.sampleRateHz << "\n";
        return;
    }

    P25LiveDecoder decoder(p25DiagnosticDecoderConfig());
    P25ControlChannelAnalyzer analyzer;
    std::vector<P25PendingVoiceGrant> pendingVoiceGrants;
    std::vector<P25TalkgroupEntry> talkgroups;
    std::optional<P25TalkgroupEntry> selectedGrant;
    size_t selectedStartSample = 0;
    std::string selectedSource;
    size_t windows = 0;
    size_t trustedBlocks = 0;
    size_t trustedMacs = 0;
    size_t grants = 0;
    size_t resolvedGrants = 0;
    size_t pendingResolved = 0;
    size_t encryptedSkipped = 0;
    size_t notReadySkipped = 0;
    size_t outOfBandSkipped = 0;
    bool sawNidLock = false;
    std::unordered_map<uint32_t, size_t> skippedEncryptedTgs;
    std::unordered_map<uint32_t, size_t> skippedNotReadyTgs;
    std::unordered_map<uint32_t, size_t> skippedOutOfBandTgs;

    const size_t windowSamples = std::max<size_t>(1, std::min(capture.iq.size(), static_cast<size_t>(
        std::clamp(capture.sampleRateHz * kP25ControlDecodeWindowSeconds, 24000.0, 4194304.0))));
    // Followtest is a build/regression harness, so prefer deterministic 512 ms
    // strides over replay's 50% overlap. The live worker already overlaps by
    // cadence; here we need bounded runtime for unattended build-test loops.
    const size_t hopSamples = std::max<size_t>(1, windowSamples);
    const size_t controlSamples = std::min(capture.iq.size(), static_cast<size_t>(
        std::max(1.0, std::round(capture.sampleRateHz * controlMs / 1000.0))));
    const double passbandHalfHz = capture.sampleRateHz * 0.48;

    auto considerResolvedGrant = [&](const P25ControlEvent& grant, size_t startSample, const char* source) {
        if (selectedGrant || !p25ControlEventIsResolvedVoiceGrant(grant)) return;
        if (args.followTalkgroupId != 0 && grant.talkgroupId != args.followTalkgroupId) return;
        auto it = std::find_if(talkgroups.begin(), talkgroups.end(), [&](const P25TalkgroupEntry& tg) {
            return sameP25Talkgroup(tg, ccHz, grant.talkgroupId);
        });
        if (it == talkgroups.end() || it->lastVoiceFreqHz <= 0.0) return;

        P25TalkgroupEntry candidate = *it;
        p25AugmentTalkgroupFromKnownSite(candidate, talkgroups, ccHz);
        if (candidate.encryptionKnown && candidate.encrypted) {
            ++encryptedSkipped;
            ++skippedEncryptedTgs[candidate.talkgroupId];
            return;
        }
        if (!p25TalkgroupCanTuneForFollow(candidate) || candidate.lastVoiceFreqHz <= 0.0) {
            ++notReadySkipped;
            ++skippedNotReadyTgs[candidate.talkgroupId];
            return;
        }
        if (!std::isfinite(candidate.lastVoiceFreqHz) ||
            std::abs(candidate.lastVoiceFreqHz - capture.centerFreqHz) > passbandHalfHz) {
            ++outOfBandSkipped;
            ++skippedOutOfBandTgs[candidate.talkgroupId];
            return;
        }

        selectedGrant = candidate;
        selectedStartSample = startSample;
        selectedSource = source ? source : "unknown";
    };

    auto consumeEvent = [&](const P25ControlEvent& ev,
                            size_t startSample,
                            const char* source,
                            std::optional<int> correctedDibitErrors) {
        if (p25ControlEventIsVoiceGrant(ev)) {
            ++grants;
            if (p25ControlEventIsResolvedVoiceGrant(ev)) ++resolvedGrants;
        }
        const bool eventRegistryEligible = !correctedDibitErrors.has_value() ||
            p25TsbkEventRegistryEligible(*correctedDibitErrors, ev);
        if (!eventRegistryEligible) return;

        const qint64 nowMs = static_cast<qint64>(std::llround(capture.startOffsetMs +
            (static_cast<double>(startSample) * 1000.0 / capture.sampleRateHz)));
        if (p25ControlEventIsVoiceGrant(ev) && !p25ControlEventIsResolvedVoiceGrant(ev)) {
            p25RememberPendingVoiceGrant(pendingVoiceGrants, ev, correctedDibitErrors.value_or(0), nowMs);
        }
        mergeP25TalkgroupEvent(talkgroups, ccHz, ev, nowMs);
        considerResolvedGrant(ev, startSample, source);

        if (ev.type == P25ControlEventType::IdentifierUpdate &&
            p25ChannelIdentifierUsable(p25IdentifierFromEvent(ev))) {
            for (const auto& resolved : p25ResolvePendingVoiceGrants(pendingVoiceGrants, analyzer, nowMs)) {
                ++pendingResolved;
                ++resolvedGrants;
                mergeP25TalkgroupEvent(talkgroups, ccHz, resolved, nowMs);
                considerResolvedGrant(resolved, startSample, "pending-resolved");
            }
        }
    };

    for (size_t start = 0; start < controlSamples && !selectedGrant; start += hopSamples) {
        const size_t end = std::min(capture.iq.size(), start + windowSamples);
        if (end <= start) break;
        std::vector<std::complex<float>> window(capture.iq.begin() + static_cast<std::ptrdiff_t>(start),
                                                capture.iq.begin() + static_cast<std::ptrdiff_t>(end));
        auto result = decoder.processIq(window, capture.sampleRateHz, capture.centerFreqHz, ccHz);
        ++windows;
        p25SeedAnalyzerNacFromDecode(analyzer, result);
        sawNidLock = sawNidLock || p25DecodeResultHasNidLock(result);

        for (const auto& block : result.rawTsbkBlocks) {
            if (!block.fecDecoded || !block.crcValid) continue;
            ++trustedBlocks;
            const auto events = analyzer.ingestTsbk(block.bytes);
            for (const auto& ev : events) {
                consumeEvent(ev, start, "TSBK", block.correctedDibitErrors);
                if (selectedGrant) break;
            }
            if (selectedGrant) break;
        }
        if (selectedGrant) break;

        for (const auto& pdu : result.phase2MacPdus) {
            if (!pdu.crcValid) continue;
            ++trustedMacs;
            const auto events = analyzer.ingestPhase2MacPdu(pdu.opcode, pdu.offset, pdu.bytes, true);
            for (const auto& ev : events) {
                consumeEvent(ev, start, "P2MAC", std::nullopt);
                if (selectedGrant) break;
            }
            if (selectedGrant) break;
        }
        if (end == capture.iq.size()) break;
    }

    std::cout << "P25 followtest control center=" << (capture.centerFreqHz / 1e6)
              << "MHz cc=" << (ccHz / 1e6)
              << "MHz samples=" << capture.iq.size()
              << " windowMs=" << (static_cast<double>(windowSamples) * 1000.0 / capture.sampleRateHz)
              << " hopMs=" << (static_cast<double>(hopSamples) * 1000.0 / capture.sampleRateHz)
              << " windows=" << windows
              << " nidLock=" << (sawNidLock ? "yes" : "no")
              << " trustedTsbk=" << trustedBlocks
              << " trustedP2Mac=" << trustedMacs
              << " grants=" << grants
              << " resolved=" << resolvedGrants
              << " pendingResolved=" << pendingResolved
              << " targetTg=" << (args.followTalkgroupId ? std::to_string(args.followTalkgroupId) : std::string("any"))
              << " encryptedSkipped=" << encryptedSkipped
              << " notReadySkipped=" << notReadySkipped
              << " outOfBandSkipped=" << outOfBandSkipped << "\n";

    if (!selectedGrant) {
        const char* result = outOfBandSkipped > 0
            ? "NO_IN_PASSBAND_FOLLOW_CANDIDATE"
            : (encryptedSkipped > 0 ? "NO_CLEAR_FOLLOW_CANDIDATE" : "NO_FOLLOW_CANDIDATE");
        std::cout << "P25 followtest result=" << result
                  << " nidLock=" << (sawNidLock ? "yes" : "no")
                  << " trustedTsbk=" << trustedBlocks
                  << " grants=" << grants
                  << " encryptedSkipped=" << encryptedSkipped
                  << " outOfBandSkipped=" << outOfBandSkipped << "\n";
        return;
    }

    P25TalkgroupEntry tg = *selectedGrant;
    const bool phase2Voice = p25TalkgroupIsPhase2(tg);
    std::cout << "P25 followtest selected source=" << selectedSource
              << " startMs=" << (capture.startOffsetMs + static_cast<double>(selectedStartSample) * 1000.0 / capture.sampleRateHz)
              << " " << p25FollowDetailLogText(tg).toStdString() << "\n";

    Receiver rx;
    rx.freqHz = tg.lastVoiceFreqHz;
    rx.mode = DemodMode::NFM;
    rx.channelBwHz = 12500.0;
    rx.lpfHz = 3000.0;
    rx.audioLpfEnabled = false;
    rx.squelchDb = -105.0;
    rx.resetP25VoiceState();
    rx.p25VoiceDecodeEnabled = true;
    rx.p25VoiceClearKnown = tg.encryptionKnown;
    rx.p25VoiceEncrypted = tg.encrypted;
    rx.p25VoiceTalkgroupId = tg.talkgroupId;
    rx.p25VoicePhase2 = phase2Voice;
    rx.p25VoiceTdmaSlotKnown = tg.tdmaSlotKnown;
    rx.p25VoiceTdmaSlot = tg.tdmaSlot;
    rx.p25VoiceMaskParamsKnown = tg.p25MaskParamsKnown;
    rx.p25VoiceNac = tg.nac;
    rx.p25VoiceWacn = tg.wacn;
    rx.p25VoiceSystemId = tg.systemId;
    rx.p25VoiceLiveDecoder = P25LiveDecoder(p25VoiceDecoderConfig(phase2Voice));
    if (phase2Voice && rx.p25VoiceMaskParamsKnown) {
        rx.p25VoiceLiveDecoder.setPhase2MaskParameters(rx.p25VoiceNac, rx.p25VoiceWacn, rx.p25VoiceSystemId);
    } else {
        rx.p25VoiceLiveDecoder.clearPhase2MaskParameters();
    }

    const size_t voiceWindowSamples = std::max<size_t>(1, std::min(capture.iq.size(), static_cast<size_t>(
        std::clamp(capture.sampleRateHz * kP25Phase2VoiceDecodeWindowSeconds, 48000.0, 4194304.0))));
    const size_t voiceHopSamples = std::max<size_t>(1, voiceWindowSamples);
    const size_t followSamples = static_cast<size_t>(std::max(1.0, std::round(capture.sampleRateHz * followMs / 1000.0)));
    const size_t voiceEndLimit = std::min(capture.iq.size(), selectedStartSample + followSamples);

    size_t voiceWindows = 0;
    long long decodedFrames = 0;
    long long audioSamples = 0;
    long long phase2Bursts = 0;
    long long phase2VoiceCodewords = 0;
    long long phase2MaskedBursts = 0;
    long long phase2MacCrcValid = 0;
    bool voiceEncrypted = false;
    bool essKnown = false;
    bool essEncrypted = false;
    P25VoiceDiagCode lastDiag = P25VoiceDiagCode::Idle;
    std::string lastVoiceSig;

    for (size_t start = selectedStartSample; start < voiceEndLimit; start += voiceHopSamples) {
        const size_t end = std::min(capture.iq.size(), start + voiceWindowSamples);
        if (end <= start) break;
        std::vector<std::complex<float>> window(capture.iq.begin() + static_cast<std::ptrdiff_t>(start),
                                                capture.iq.begin() + static_cast<std::ptrdiff_t>(end));
        const uint64_t absStart = capture.firstSampleOffset + static_cast<uint64_t>(start);
        auto audio = decodeP25VoiceAudioBlock(rx, window, capture.sampleRateHz, capture.centerFreqHz,
                                              tg.lastVoiceFreqHz, 48000.0, absStart, true);
        ++voiceWindows;
        decodedFrames += static_cast<long long>(audio.decodedFrames);
        audioSamples += static_cast<long long>(audio.audio.size());
        phase2Bursts += static_cast<long long>(audio.phase2Bursts);
        phase2VoiceCodewords += static_cast<long long>(audio.phase2VoiceCodewords);
        phase2MaskedBursts += static_cast<long long>(audio.phase2MaskedBursts);
        phase2MacCrcValid += static_cast<long long>(audio.phase2MacCrcValid);
        essKnown = essKnown || audio.phase2EssKnown;
        essEncrypted = essEncrypted || audio.phase2EssEncrypted;
        voiceEncrypted = voiceEncrypted || audio.skippedEncrypted || (audio.phase2EssKnown && audio.phase2EssEncrypted);
        lastDiag = audio.diag;

        std::ostringstream sig;
        sig << static_cast<int>(audio.diag) << ":" << audio.decodedFrames << ":" << audio.audio.size()
            << ":" << audio.phase2Bursts << ":" << audio.phase2VoiceCodewords << ":" << audio.phase2MaskedBursts
            << ":" << audio.phase2MacCrcValid << ":" << audio.phase2EssKnown << ":" << audio.phase2EssEncrypted;
        if (sig.str() != lastVoiceSig) {
            lastVoiceSig = sig.str();
            std::cout << "P25 followtest voice startMs="
                      << (capture.startOffsetMs + static_cast<double>(start) * 1000.0 / capture.sampleRateHz)
                      << " stage=" << p25VoiceDiagLabel(audio.diag)
                      << " decoded=" << audio.decodedFrames
                      << " audio=" << audio.audio.size()
                      << " p2bursts=" << audio.phase2Bursts
                      << " p2vcw=" << audio.phase2VoiceCodewords
                      << " p2sf=" << audio.phase2SuperframeBursts
                      << " p2mask=" << audio.phase2MaskedBursts
                      << " p2mac=" << audio.phase2MacCrcValid << "/" << audio.phase2MacPdus
                      << " " << p25Phase2AcchStatsText(makeP25VoiceDiagnostics(audio)).toStdString()
                      << " p2ess=" << (audio.phase2EssKnown ? (audio.phase2EssEncrypted ? "enc" : "clear") : "unknown")
                      << " backend=" << (audio.backendAvailable ? "yes" : "no") << "\n";
        }

        if (audio.decodedFrames > 0 && !audio.audio.empty()) break;
        if (voiceEncrypted) break;
        if (end == capture.iq.size()) break;
    }

    const char* result = "FAIL_NO_AUDIO";
    if (decodedFrames > 0 && audioSamples > 0) {
        result = "PASS_CLEAR_AUDIO";
    } else if (voiceEncrypted) {
        result = "PASS_ENCRYPTED_GATED";
    }
    std::cout << "P25 followtest result=" << result
              << " voiceWindows=" << voiceWindows
              << " decodedFrames=" << decodedFrames
              << " audioSamples=" << audioSamples
              << " lastStage=" << p25VoiceDiagLabel(lastDiag)
              << " p2bursts=" << phase2Bursts
              << " p2vcw=" << phase2VoiceCodewords
              << " p2mask=" << phase2MaskedBursts
              << " p2macCrc=" << phase2MacCrcValid
              << " essKnown=" << (essKnown ? "yes" : "no")
              << " essEncrypted=" << (essEncrypted ? "yes" : "no") << "\n";
}

static void populateP25Table(QTableWidget* table,
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

#ifdef _WIN32
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
        QLabel* p25Status = new QLabel("Idle");
        p25BtnLay->addWidget(p25ScanBtn);
        p25BtnLay->addWidget(p25MonitorBtn);
        p25BtnLay->addWidget(p25GrantTestBtn);
        p25BtnLay->addWidget(p25RefreshBtn);
        p25BtnLay->addWidget(p25KnownBtn);
        p25BtnLay->addWidget(p25LogBtn);
        p25BtnLay->addWidget(p25AutoFollowCheck);
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
        QPushButton* p25TgFollowBtn = new QPushButton("Follow TG");
        p25TgFollowBtn->setCheckable(true);
        QPushButton* p25TgDeleteBtn = new QPushButton("Delete TG");
        QPushButton* p25TgRefreshBtn = new QPushButton("Refresh TGs");
        p25TgBtnLay->addWidget(new QLabel("Talkgroups:"));
        p25TgBtnLay->addWidget(p25TgManualBtn);
        p25TgBtnLay->addWidget(p25TgVerifyBtn);
        p25TgBtnLay->addWidget(p25TgScannerBtn);
        p25TgBtnLay->addWidget(p25TgFollowBtn);
        p25TgBtnLay->addWidget(p25TgDeleteBtn);
        p25TgBtnLay->addWidget(p25TgRefreshBtn);
        p25TgBtnLay->addStretch();
        p25Lay->addLayout(p25TgBtnLay);

        QTableWidget* p25TgTable = new QTableWidget(0, 9, this);
        p25TgTable->setHorizontalHeaderLabels({"CC MHz", "TGID", "Alpha Tag", "Voice MHz", "Src", "Hits", "Enc", "Status", "Last Seen"});
        p25TgTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        p25TgTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        p25TgTable->verticalHeader()->setVisible(false);
        p25TgTable->setMaximumHeight(150);
        p25TgTable->horizontalHeader()->setStretchLastSection(true);
        p25Lay->addWidget(p25TgTable);
        rxLay->addWidget(p25Box);

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

            std::unique_lock<std::mutex> listLock(receiversMutex, std::try_to_lock);
            if (!listLock.owns_lock()) return false;
            ensureReceiver();
            if (receivers.empty() || !receivers[0]) return true;

            auto& rx = *receivers[0];
            std::unique_lock<std::mutex> rxLock(rx.stateMutex, std::try_to_lock);
            if (!rxLock.owns_lock()) return false;
            double liveMonitorHz = 0.0;
            {
                std::lock_guard<std::mutex> monLock(monitorParamsMutex);
                liveMonitorHz = currentMonitorFreq;
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
                rx.resetP25VoiceState();
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

        auto setP25ControlChannelMute = [this](bool muted) {
            std::lock_guard<std::mutex> lk(receiversMutex);
            ensureReceiver();
            if (receivers.empty() || !receivers[0]) return;
            auto& rx = *receivers[0];
            std::lock_guard<std::mutex> rxLock(rx.stateMutex);
            rx.p25ControlChannelMute = muted;
            if (muted) {
                std::unique_lock<std::recursive_mutex> dspLock(rx.dspMutex, std::try_to_lock);
                if (dspLock.owns_lock()) {
                    rx.resetDemodState();
                }
            }
        };

        auto clearP25VoiceFollowState = [this]() {
            std::lock_guard<std::mutex> lk(receiversMutex);
            ensureReceiver();
            if (receivers.empty() || !receivers[0]) return;
            auto& rx = *receivers[0];
            std::lock_guard<std::mutex> rxLock(rx.stateMutex);
            clearP25VoiceFollowFieldsLocked(rx, false);
            tryApplyP25VoiceResetLocked(rx);
        };

        auto returnP25AutoFollowToControl = [this, p25Status, p25TgFollowBtn, tuneP25Path, clearP25VoiceFollowState, setP25ControlChannelMute]() {
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
            if (p25TgFollowBtn) p25TgFollowBtn->setChecked(false);
            clearP25VoiceFollowState();
            if (ccHz > 0.0 && tuneP25Path(ccHz)) {
                p25MonitoredControlFreqHz = ccHz;
                setP25ControlChannelMute(true);
                p25LiveDecoder.reset();
                { std::lock_guard<std::mutex> lk(p25ControlWorkerDecoderMutex); p25ControlWorkerDecoder.reset(); }
                p25LastDiagSignature.clear();
                appendP25LogLine(QString("P25 follow returned to muted control channel %1MHz.").arg(ccHz / 1e6, 0, 'f', 5));
                appendP25LogLine("AFC unlock: returned to control channel; live AFC adaptation resumed.");
                if (p25Status) p25Status->setText(QString("Monitoring CC %1 MHz").arg(ccHz / 1e6, 0, 'f', 5));
            } else if (p25Status) {
                p25Status->setText("Auto follow idle");
            }
        };

        auto armP25VoiceFollowState = [this](const P25TalkgroupEntry& tg) -> bool {
            bool phase2VoiceLog = false;
            double frozenAfcHzLog = 0.0;
            bool clearAudio = false;
            {
                std::unique_lock<std::mutex> lk(receiversMutex, std::try_to_lock);
                if (!lk.owns_lock()) return false;
                ensureReceiver();
                if (receivers.empty() || !receivers[0]) return true;
                auto& rx = *receivers[0];
                std::unique_lock<std::mutex> rxLock(rx.stateMutex, std::try_to_lock);
                if (!rxLock.owns_lock()) return false;
                std::unique_lock<std::recursive_mutex> dspLock(rx.dspMutex, std::try_to_lock);
                if (!dspLock.owns_lock()) return false;
                const bool phase2Voice = p25TalkgroupIsPhase2(tg);
                const double frozenAfcHz = rx.afcLocked ? rx.afcOffsetHz : 0.0;
                rx.resetDemodState();
                rx.resetP25VoiceState();
                rx.p25VoiceResetPending = false;
                rx.p25AfcFrozen = true;
                rx.p25FrozenAfcOffsetHz = frozenAfcHz;

                // resetP25VoiceState() intentionally clears transient decoder state, so apply
                // the active grant metadata AFTER the reset. Applying it before the reset can
                // erase Phase 2/slot/mask fields and leave TDMA acquisition permanently in
                // wrong-slot/no-mask diagnostics.
                rx.p25VoiceDecodeEnabled = true;
                rx.p25VoiceClearKnown = tg.encryptionKnown;
                rx.p25VoiceEncrypted = tg.encrypted;
                rx.p25VoiceTalkgroupId = tg.talkgroupId;
                rx.p25VoicePhase2 = phase2Voice;
                rx.p25VoiceTdmaSlotKnown = tg.tdmaSlotKnown;
                rx.p25VoiceTdmaSlot = tg.tdmaSlot;
                rx.p25VoiceSlotProbePending = false;
                rx.p25VoiceSlotProbeRequested = 0;
                rx.p25VoiceMaskParamsKnown = tg.p25MaskParamsKnown;
                rx.p25VoiceNac = tg.nac;
                rx.p25VoiceWacn = tg.wacn;
                rx.p25VoiceSystemId = tg.systemId;
                const qint64 armNowMs = QDateTime::currentMSecsSinceEpoch();
                rx.p25VoiceSettleUntilMs = armNowMs + p25PostArmSettleMs(rx.p25VoicePhase2);
                rx.p25VoiceDiscardWindows = p25PostArmDiscardWindows(rx.p25VoicePhase2);
                rx.p25ControlChannelMute = false;
                rx.p25VoiceLiveDecoder = P25LiveDecoder(p25VoiceDecoderConfig(rx.p25VoicePhase2));
                DeviceManager::instance().setReceiverCursorToLiveEdge(rx.deviceIndex, rx);
                clearAudio = true;
                if (rx.p25VoicePhase2 && rx.p25VoiceMaskParamsKnown) {
                    rx.p25VoiceLiveDecoder.setPhase2MaskParameters(rx.p25VoiceNac, rx.p25VoiceWacn, rx.p25VoiceSystemId);
                } else {
                    rx.p25VoiceLiveDecoder.clearPhase2MaskParameters();
                }
                phase2VoiceLog = phase2Voice;
                frozenAfcHzLog = frozenAfcHz;
            }
            if (clearAudio && engineForAudio) {
                QTimer::singleShot(0, this, [this]() {
                    if (engineForAudio) engineForAudio->clearBuffers();
                });
            }
            if (phase2VoiceLog) {
                appendP25LogLine(QString("AFC lock: frozen offset=%1kHz ppmDelta=%2 reason=phase2_voice_acq; applying frozen offset to P25 voice decode target until return to control.")
                    .arg(frozenAfcHzLog / 1000.0, 0, 'f', 3)
                    .arg(estimatePpmCorrectionDelta(frozenAfcHzLog, tg.lastVoiceFreqHz), 0, 'f', 3));
            }
            return true;
        };

        auto scheduleP25VoiceFollowArm = [this, p25Status, armP25VoiceFollowState, returnP25AutoFollowToControl](P25TalkgroupEntry tg, const char* reason) {
            const quint32 expectedTg = tg.talkgroupId;
            const double expectedVoiceHz = tg.lastVoiceFreqHz;
            const QString reasonText = reason ? QString::fromUtf8(reason) : QString("follow");
            // Do not arm the voice decoder synchronously inside the control-channel grant handler.
            // Retune, UI state changes, stale worker-result drops, decoder reset, and audio-buffer clear
            // all used to happen in one stack frame; on some systems that could hang/crash immediately
            // after an auto-follow grant.  Arm voice decode after the RF retune has had a short settle
            // window and only if we are still following the same grant.
            auto armAttempt = std::make_shared<std::function<void(int)>>();
            std::weak_ptr<std::function<void(int)>> weakArmAttempt = armAttempt;
            *armAttempt = [this, p25Status, armP25VoiceFollowState, returnP25AutoFollowToControl, tg, expectedTg, expectedVoiceHz, reasonText, weakArmAttempt](int attempt) mutable {
                try {
                    if (!p25FollowEnabled || p25FollowTalkgroupId != expectedTg) return;
                    if (std::abs(p25AutoFollowVoiceFreqHz - expectedVoiceHz) > 50.0) return;
                    if (armP25VoiceFollowState(tg)) {
                        const bool phase2 = p25TalkgroupIsPhase2(tg);
                        appendP25LogLine(QString("P25 voice decode armed after retune settle: TG=%1 voice=%2MHz reason=%3 postArmSettle=%4ms discardWindows=%5.")
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
                            "P25 voice arm deferred because a DSP decode window is still completing; UI remains responsive.",
                            2500);
                    }
                    if (attempt >= 120) {
                        appendP25LogLine(QString("P25 voice arm timed out for TG %1; returning to control channel to avoid stale voice state.")
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

        auto autoFollowP25Grant = [this, p25Status, p25TgFollowBtn, tuneP25Path, scheduleP25VoiceFollowArm, returnP25AutoFollowToControl]
            (const P25TalkgroupEntry& tg, const P25ControlEvent& event, qint64 nowMs) -> bool {
            if (!p25AutoFollowEnabled || tg.talkgroupId == 0 || tg.lastVoiceFreqHz <= 0.0) return false;
            if (event.talkgroupId != 0 && event.talkgroupId != tg.talkgroupId) return false;

            const double ccHz = p25MonitoredControlFreqHz > 0.0 ? p25MonitoredControlFreqHz : tg.controlFreqHz;
            if (ccHz <= 0.0) return false;

            P25TalkgroupEntry followTg = tg;
            const bool grantLooksPhase2 = p25TalkgroupIsPhase2(followTg);
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

            const bool currentGrantEncryptionKnown = event.encryptionKnown;
            if (currentGrantEncryptionKnown) {
                followTg.encryptionKnown = true;
                followTg.encrypted = event.encrypted;
            }

            if (currentGrantEncryptionKnown && followTg.encrypted) {
                appendP25LogLineKeyed(QString("auto-skip-encrypted:%1:%2").arg(followTg.talkgroupId).arg(static_cast<int>(grantLooksPhase2)),
                    QString("Auto-follow skipped encrypted P25 TG %1 (%2); staying on/returning to control channel.")
                        .arg(followTg.talkgroupId)
                        .arg(grantLooksPhase2 ? "Phase 2 TDMA" : "Phase 1 FDMA"),
                    4000);
                if (p25FollowAutoActive && p25FollowTalkgroupId == followTg.talkgroupId) {
                    appendP25LogLine(QString("Encrypted re-grant/late update for followed TG %1; releasing voice follow immediately.").arg(followTg.talkgroupId));
                    returnP25AutoFollowToControl();
                }
                return false;
            }
            if (!currentGrantEncryptionKnown && grantLooksPhase2 && tg.encryptionKnown && tg.encrypted) {
                appendP25LogLineKeyed(QString("auto-skip-p2-likely-encrypted:%1").arg(followTg.talkgroupId),
                    QString("Auto-follow skipped Phase 2 TG %1 because the grant update has no service options and this TG/call is already known encrypted; waiting for an explicit clear grant.")
                        .arg(followTg.talkgroupId),
                    8000);
                if (p25FollowAutoActive && p25FollowTalkgroupId == followTg.talkgroupId) {
                    appendP25LogLine(QString("Likely encrypted re-grant/update for followed TG %1; releasing voice follow immediately.").arg(followTg.talkgroupId));
                    returnP25AutoFollowToControl();
                }
                return false;
            }
            if (!p25TalkgroupCanTuneForFollow(followTg)) {
                appendP25LogLineKeyed(QString("auto-skip-clear-unknown:%1").arg(followTg.talkgroupId),
                    QString("Auto-follow is waiting for a clear-state grant before following P25 TG %1.").arg(followTg.talkgroupId),
                    5000);
                return false;
            }
            if (!followTg.encryptionKnown && p25TalkgroupIsPhase2(followTg)) {
                appendP25LogLineKeyed(QString("auto-follow-p2-clear-unknown:%1").arg(followTg.talkgroupId),
                    QString("Auto-following Phase 2 TG %1 with unknown grant encryption; audio remains muted until MAC/ESS proves clear.").arg(followTg.talkgroupId),
                    5000);
            }
            if (p25FollowEnabled && !p25FollowAutoActive) return false;

            if (p25FollowAutoActive && p25FollowTalkgroupId == followTg.talkgroupId &&
                std::abs(p25AutoFollowVoiceFreqHz - followTg.lastVoiceFreqHz) <= 50.0) {
                p25AutoFollowLastGrantMs = nowMs;
                p25AutoFollowLastActiveMs = nowMs;
                return false;
            }
            if (p25FollowAutoActive && nowMs - p25AutoFollowTunedAtMs < 1500) return false;

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
                appendP25LogLine(QString("TDMA ACQ armed: TG=%1 voice=%2MHz control=%3MHz slot=%4 mask=%5 cfgSymbolRate=6000Hz armDelay=%6ms postArmSettle=%7ms clearGate=ESS/MAC.")
                    .arg(followTg.talkgroupId)
                    .arg(followTg.lastVoiceFreqHz / 1e6, 0, 'f', 5)
                    .arg(ccHz / 1e6, 0, 'f', 5)
                    .arg(followTg.tdmaSlotKnown ? QString::number(followTg.tdmaSlot & 0x01u) : QString("unknown"))
                    .arg(followTg.p25MaskParamsKnown ? "known" : "pending")
                    .arg(kP25Phase2ArmDelayMs)
                    .arg(kP25Phase2PostArmSettleMs));
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
                    ? "P25 auto-follow enabled; clear grants will tune to voice and return to the control channel after activity drops."
                    : "P25 auto-follow disabled.");
                if (!on && p25FollowAutoActive) {
                    returnP25AutoFollowToControl();
                } else if (p25Status) {
                    p25Status->setText(on ? "Auto follow armed" : "Auto follow off");
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
            p25LiveDecoder.reset();
            { std::lock_guard<std::mutex> lk(p25ControlWorkerDecoderMutex); p25ControlWorkerDecoder.reset(); }
            p25LiveControlAnalyzer.reset();
            p25PendingVoiceGrants.clear();
            const size_t seededIdentifiers = seedP25AnalyzerFromCachedChannelIdentifiers(p25LiveControlAnalyzer, ccHz);
            p25LastDiagSignature.clear();
            clearP25VoiceFollowState();
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
            p25FollowEnabled = false;
            p25FollowAutoActive = false;
            p25FollowTalkgroupId = 0;
            p25LiveDecoder.reset();
            { std::lock_guard<std::mutex> lk(p25ControlWorkerDecoderMutex); p25ControlWorkerDecoder.reset(); }
            p25LiveControlAnalyzer.reset();
            p25PendingVoiceGrants.clear();
            const size_t seededIdentifiers = seedP25AnalyzerFromCachedChannelIdentifiers(p25LiveControlAnalyzer, ccHz);
            p25LastDiagSignature.clear();
            clearP25VoiceFollowState();
            setP25ControlChannelMute(true);
            p25AutoFollowEnabled = true;
            if (p25AutoFollowCheck && !p25AutoFollowCheck->isChecked()) {
                p25AutoFollowCheck->blockSignals(true);
                p25AutoFollowCheck->setChecked(true);
                p25AutoFollowCheck->blockSignals(false);
            }
            showP25LogWindow();
            appendP25LogLine(QString("Grant test armed: monitoring muted P25 control channel %1MHz; auto-follow will tune clear grants and log voice/audio gates.")
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
            if (tg.encryptionKnown && tg.encrypted) {
                p25TgFollowBtn->setChecked(false);
                p25Status->setText(QString("TG %1 encrypted").arg(tg.talkgroupId));
                statusBar()->showMessage(QString("Skipping encrypted P25 TG %1").arg(tg.talkgroupId), 3500);
                return;
            }
            if (!p25TalkgroupCanTuneForFollow(tg)) {
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
                                                      rememberPendingP25VoiceGrant, tryResolvePendingP25VoiceGrants]() {
            try {
                // During P25 voice follow the DSP worker may be doing long Phase 2
                // acquisition windows. Keep the Qt timer as a responsive UI pump
                // instead of repainting 64K spectrum/waterfall rows at 100 Hz.
                static auto lastP25FollowUiTick = std::chrono::steady_clock::now() - std::chrono::seconds(1);
                const auto uiTickNow = std::chrono::steady_clock::now();
                if (p25FollowEnabled && uiTickNow - lastP25FollowUiTick < std::chrono::milliseconds(50)) {
                    return;
                }
                if (p25FollowEnabled) lastP25FollowUiTick = uiTickNow;

                auto& mgr = DeviceManager::instance();
                for (size_t i = 0; i < mgr.getDevices().size(); ++i) {
                    if (mgr.isStreaming(i)) {
                        std::vector<float> pwr;
                        double cf = 100e6, sr = 2.048e6;
                        if (mgr.getLatestSpectrum(i, pwr, cf, sr) && !pwr.empty()) {
                            spectrum->updateSpectrum(pwr, cf, sr);

                            // Stage 2 hardening: keep the 10 ms UI timer light. The classifier and
                            // AUTO bandwidth resolver are useful, but running ROI construction +
                            // deterministic/model classification on every spectrum paint tick makes
                            // the GUI compete with P25 acquisition and IQ capture. Rate-limit this
                            // work and leave the timer as a spectrum/UI pump.
                            static auto lastClassifierUi = std::chrono::steady_clock::now() - std::chrono::seconds(1);
                            const auto classifierNow = std::chrono::steady_clock::now();
                            if (classifierNow - lastClassifierUi > std::chrono::milliseconds(500)) {
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
                            if (p25ScanBtn && p25ScanBtn->isChecked()) {
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
                            if (p25MonitoredControlFreqHz > 0.0 && !p25FollowEnabled && p25CcInPassband) {
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
                                                // At this point busy was false, so the previous worker has completed;
                                                // join it before replacing the std::thread object.
                                                p25ControlWorkerThread.join();
                                            }
                                            p25ControlWorkerThread = std::thread([this, iq = std::move(iq), workerSr, workerCf, workerTarget]() mutable {
                                                P25LiveDecodeResult result;
                                                try {
                                                    std::lock_guard<std::mutex> decoderLock(p25ControlWorkerDecoderMutex);
                                                    result = p25ControlWorkerDecoder.processIq(iq, workerSr, workerCf, workerTarget);
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
                                    if (p25FollowEnabled || p25FollowAutoActive) {
                                        appendP25LogLineKeyed("p25-control-worker-stale-after-follow",
                                            "Dropped stale P25 control worker result after voice-follow retune.",
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
                                                    if (!p25ControlEventIsResolvedVoiceGrant(ev) &&
                                                        block.correctedDibitErrors <= kP25VoiceGrantMaxCorrectedDibits) {
                                                        rememberPendingP25VoiceGrant(ev, block.correctedDibitErrors, nowMs);
                                                    }
                                                } else if (ev.type == P25ControlEventType::IdentifierUpdate && ev.phase2Candidate) {
                                                    appendP25LogLineKeyed(QString("tdma-identifier:%1").arg(key),
                                                        "TDMA identifier table update: " + evText,
                                                        5000);
                                                }
                                                const bool eventRegistryEligible = p25TsbkEventRegistryEligible(block.correctedDibitErrors, ev);
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
                                                        appendP25LogLineKeyed(QString("tsbk-weak-voice-grant:%1").arg(key),
                                                            QString("Accepted resolved voice grant from high-correction TSBK: corrected_dibits=%1 threshold=%2 raw=%3")
                                                                .arg(block.correctedDibitErrors)
                                                                .arg(kP25VoiceGrantMaxCorrectedDibits)
                                                                .arg(rawHex),
                                                            2500);
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
                                            if (!registryEligible && !acceptedHighCorrectionGrant) {
                                                p25LiveControlAnalyzer = analyzerBefore;
                                                appendP25LogLineKeyed(QString("tsbk-weak:%1").arg(rawHex),
                                                    QString("TSBK raw=%1 passed CRC but needed %2 dibit corrections; non-grant state stays read-only. Resolved voice grants are allowed up to %3 corrections.")
                                                        .arg(rawHex)
                                                        .arg(block.correctedDibitErrors)
                                                        .arg(kP25VoiceGrantMaxCorrectedDibits),
                                                    6000);
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
                                        const auto events = p25LiveControlAnalyzer.ingestPhase2MacPdu(pdu.opcode, pdu.offset, pdu.bytes, pdu.crcValid);
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
                                    const bool hasTrustedControl = trustedTsbk > 0 || trustedPhase2Mac > 0;
                                    if (hasTrustedControl || live.stats.phase2Bursts > 0) {
                                        const QString opsText = p25ControlAuditOpsText(trustedControlOps);
                                        const QString auditText = QString("P25 grant audit stage=%1 trustedTsbk=%2 trustedP2Mac=%3 voiceGrants=%4 resolved=%5 unresolved=%6 ops=%7")
                                            .arg(lockStage)
                                            .arg(static_cast<qulonglong>(trustedTsbk))
                                            .arg(static_cast<qulonglong>(trustedPhase2Mac))
                                            .arg(static_cast<qulonglong>(controlVoiceGrantEvents))
                                            .arg(static_cast<qulonglong>(controlResolvedVoiceGrantEvents))
                                            .arg(static_cast<qulonglong>(controlUnresolvedVoiceGrantEvents))
                                            .arg(opsText);
                                        appendP25LogLineThrottled(
                                            QString("p25-grant-audit:%1:%2:%3:%4:%5")
                                                .arg(lockStage)
                                                .arg(static_cast<qulonglong>(trustedTsbk))
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
                                    const QString diag = QString("P25 stage lock=%1 dev=%2 path=%3 cqpskLock=%4/%5/%6 sticky=%7 trust=%8 miss=%9 phase=%10 fine=%11 resid=%12Hz err=%13 cf=%14MHz target=%15MHz offset=%16kHz sr=%17MHz chanSr=%18kHz discMean=%19Hz iq=%20 sym=%21 conf=%22 softQ=%23 softLlr=%24 softLow=%25/%26 sync=%27 bestErr=%28 aligned=%29 bestNidDist=%30 nid=%31 tsbk=%32 trusted=%33")
                                        .arg(lockStage)
                                        .arg(i)
                                        .arg(QString::fromStdString(live.stats.demodPath.empty() ? std::string("unknown") : live.stats.demodPath))
                                        .arg(live.stats.cqpskLockActive ? "active" : "new")
                                        .arg(live.stats.cqpskLockUsed ? "used" : "search")
                                        .arg(live.stats.cqpskLockUpdated ? "updated" : "held")
                                        .arg(live.stats.cqpskStickyOverride ? "yes" : "no")
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
                                        appendP25LogLineKeyed(QString("warn:%1").arg(QString::fromStdString(warning)),
                                            "Decoder warning: " + QString::fromStdString(warning),
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
                                bool haveVoiceDiag = false;
                                {
                                    std::unique_lock<std::mutex> lk(receiversMutex, std::try_to_lock);
                                    if (lk.owns_lock() && !receivers.empty() && receivers[0]) {
                                        std::unique_lock<std::mutex> rxLock(receivers[0]->stateMutex, std::try_to_lock);
                                        if (!rxLock.owns_lock()) {
                                            appendP25LogLineKeyed("p25-follow-status-rx-busy",
                                                "P25 follow status skipped while DSP owned receiver state; GUI timer did not block.",
                                                3000);
                                        } else {
                                        voiceDiag = receivers[0]->p25VoiceDiagnostics;
                                        haveVoiceDiag = true;
                                        }
                                    } else if (!lk.owns_lock()) {
                                        appendP25LogLineKeyed("p25-follow-status-list-busy",
                                            "P25 follow status skipped while receiver list was busy; GUI timer did not block.",
                                            3000);
                                    }
                                }
                                if (!haveVoiceDiag) {
                                    break;
                                }
                                const auto code = static_cast<P25VoiceDiagCode>(voiceDiag.diag);
                                const long long tg = static_cast<long long>(voiceDiag.talkgroupId);
                                const long long syncs = voiceDiag.syncs;
                                const long long nids = voiceDiag.nids;
                                const long long imbe = voiceDiag.imbeFrames;
                                const long long decoded = voiceDiag.decodedFrames;
                                const long long p2bursts = voiceDiag.phase2Bursts;
                                const long long p2vcw = voiceDiag.phase2VoiceCodewords;
                                const long long p2sf = voiceDiag.phase2SuperframeBursts;
                                const long long p2mask = voiceDiag.phase2MaskedBursts;
                                const long long p2mac = voiceDiag.phase2MacPdus;
                                const long long p2crc = voiceDiag.phase2MacCrcValid;
                                const QString p2ess = voiceDiag.phase2EssKnown
                                    ? (voiceDiag.phase2EssEncrypted ? "enc" : "clear")
                                    : "unknown";
                                const long long statusTg = tg > 0 ? tg : static_cast<long long>(p25FollowTalkgroupId);
                                if (p25FollowAutoActive) {
                                    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
                                    P25FollowSnapshot followSnapshot;
                                    followSnapshot.nowMs = nowMs;
                                    followSnapshot.tunedAtMs = p25AutoFollowTunedAtMs;
                                    followSnapshot.lastActiveMs = p25AutoFollowLastActiveMs;
                                    followSnapshot.autoActive = p25FollowAutoActive;
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
                                    followSnapshot.phase2EssKnown = voiceDiag.phase2EssKnown;
                                    followSnapshot.phase2EssEncrypted = voiceDiag.phase2EssEncrypted;
                                    const auto followDecision = evaluateP25Follow(followSnapshot);
                                    if (followDecision.voiceStillLooksActive) p25AutoFollowLastActiveMs = nowMs;
                                    if (followDecision.action == P25FollowAction::ReturnEncrypted) {
                                        appendP25LogLine(QString("P25 auto-follow TG %1 proved encrypted on voice channel; returning to control channel immediately.")
                                            .arg(static_cast<long long>(followDecision.effectiveTalkgroupId)));
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
                                    if (acqNowMs - lastTdmaAcqStatusMs > 2500) {
                                        const double voiceHz = p25AutoFollowVoiceFreqHz > 0.0 ? p25AutoFollowVoiceFreqHz : currentMonitorFreq;
                                        const bool inPassband = sr > 0.0 && std::abs(voiceHz - cf) <= sr * 0.48;
                                        appendP25LogLine(QString("TDMA ACQ check: TG=%1 voice=%2MHz cf=%3MHz offset=%4kHz inPassband=%5 diag=%6 p2bursts=%7 p2vcw=%8 sf=%9 mask=%10 mac=%11/%12 %13 ess=%14 symbolRate=6000Hz audioGate=closed-until-MAC/ESS")
                                            .arg(tg > 0 ? tg : static_cast<long long>(p25FollowTalkgroupId))
                                            .arg(voiceHz / 1e6, 0, 'f', 5)
                                            .arg(cf / 1e6, 0, 'f', 5)
                                            .arg((voiceHz - cf) / 1000.0, 0, 'f', 1)
                                            .arg(inPassband ? "yes" : "NO")
                                            .arg(p25VoiceDiagLabel(code))
                                            .arg(p2bursts)
                                            .arg(p2vcw)
                                            .arg(p2sf)
                                            .arg(p2mask)
                                            .arg(p2crc)
                                            .arg(p2mac)
                                            .arg(p25Phase2AcchStatsText(voiceDiag))
                                            .arg(p2ess));
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
                                        slotProbeSnapshot.maxFlips = 6;
                                        slotProbeSnapshot.inPassband = inPassband;
                                        slotProbeSnapshot.diag = voiceDiag.diag;
                                        slotProbeSnapshot.phase2VoiceCodewords = p2vcw;
                                        slotProbeSnapshot.phase2SuperframeBursts = p2sf;
                                        slotProbeSnapshot.phase2MaskedBursts = p2mask;
                                        slotProbeSnapshot.phase2MacPdus = p2mac;
                                        slotProbeSnapshot.phase2MacCrcValid = p2crc;
                                        slotProbeSnapshot.phase2EssKnown = voiceDiag.phase2EssKnown;
                                        const auto slotProbeDecision = evaluateP25SlotProbe(slotProbeSnapshot);
                                        if (slotProbeDecision.resetTracking) {
                                            slotProbeTg = acqTg;
                                            slotProbeVoiceHz = voiceHz;
                                            slotProbeArmMs = p25AutoFollowTunedAtMs;
                                            lastSlotProbeFlipMs = 0;
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
                                        if (slotProbeDecision.shouldFlip) {
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
                                                } else if (!receivers.empty() && receivers[0]) {
                                                    std::unique_lock<std::mutex> rxLock(receivers[0]->stateMutex, std::try_to_lock);
                                                    if (!rxLock.owns_lock()) {
                                                        appendP25LogLineKeyed("tdma-slot-probe-rx-busy",
                                                            "TDMA slot auto-probe deferred because DSP owned receiver state; GUI timer did not block.",
                                                            3000);
                                                    } else {
                                                    auto& rx = *receivers[0];
                                                    if (rx.p25VoiceDecodeEnabled &&
                                                        (rx.p25VoicePhase2 || p25AutoFollowVoiceFreqHz > 0.0)) {
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
                                            if (flipped || queued) {
                                                ++slotProbeFlipCount;
                                                wrongSlotChecks = 0;
                                                lastSlotProbeFlipMs = acqNowMs;
                                                appendP25LogLine(QString("TDMA ACQ slot auto-probe: repeated wrong-slot diagnostics with VCWs present; %1 slot %2 -> %3 for TG=%4 voice=%5MHz sf=%6 mask=%7 mac=%8/%9 ess=%10. Audio remains gated until lock.")
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
                                const int diagInt = static_cast<int>(code);
                                if (diagInt != lastVoiceDiag || statusTg != lastVoiceTalkgroup) {
                                    appendP25LogLine(QString("P25 voice follow: TG %1 %2 sync=%3 nid=%4 imbe=%5 decoded=%6 p2bursts=%7 p2vcw=%8 p2sf=%9 p2mask=%10 p2mac=%11/%12 p2ess=%13 backend=%14 nidLock=%15.")
                                        .arg(statusTg)
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
                                        .arg(p2ess)
                                        .arg(voiceDiag.backendAvailable ? "yes" : "no")
                                        .arg(voiceDiag.nidLock ? "yes" : "no"));
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
            if (updateTimer && !updateTimer->isActive()) updateTimer->start(10);
        });

        // Dedicated background DSP worker thread for the GUI monitor path.
        // Owned (not detached) so we can join on shutdown. Uses stop flag.
        stopDspWorker.store(false, std::memory_order_release);
        guiDspWorker = std::thread([this]() {
            auto& mgr = DeviceManager::instance();
            std::map<Receiver*, std::chrono::steady_clock::time_point> lastPhase2DecodeByRx;
            std::map<Receiver*, std::vector<float>> pendingAudioByRx;
            std::map<Receiver*, RollingIqWindow> phase2IqByRx;
            while (!stopDspWorker.load(std::memory_order_acquire)) {
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
                auto receiverStillActive = [&rxSnapshot](const Receiver* ptr) {
                    return std::any_of(rxSnapshot.begin(), rxSnapshot.end(),
                        [ptr](const std::shared_ptr<Receiver>& r) { return r.get() == ptr; });
                };
                for (auto it = lastPhase2DecodeByRx.begin(); it != lastPhase2DecodeByRx.end();) {
                    it = receiverStillActive(it->first) ? std::next(it) : lastPhase2DecodeByRx.erase(it);
                }
                for (auto it = pendingAudioByRx.begin(); it != pendingAudioByRx.end();) {
                    it = receiverStillActive(it->first) ? std::next(it) : pendingAudioByRx.erase(it);
                }
                for (auto it = phase2IqByRx.begin(); it != phase2IqByRx.end();) {
                    it = receiverStillActive(it->first) ? std::next(it) : phase2IqByRx.erase(it);
                }
                for (size_t r = 0; r < rxSnapshot.size() && !stopDspWorker.load(std::memory_order_acquire); ++r) {
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
                    bool skipP25VoiceWindow = false;
                    bool appliedQueuedSlotProbe = false;
                    uint8_t appliedQueuedSlot = 0;
                    bool appliedQueuedVoiceReset = false;
                    uint64_t iqStartAbsolute = 0;
                    bool iqStartAbsoluteKnown = false;
                    std::vector<std::complex<float>> iq;

                    {
                        std::lock_guard<std::mutex> rxLock(rx.stateMutex);
                        if (!rx.active || rx.deviceIndex != i) continue;
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
                        if (rx.p25VoiceResetPending) {
                            if (tryApplyP25VoiceResetLocked(rx)) {
                                monP25VoiceDecode = rx.p25VoiceDecodeEnabled;
                                monP25VoicePhase2 = rx.p25VoicePhase2;
                                phase2IqByRx.erase(&rx);
                                pendingAudioByRx[&rx].clear();
                                appliedQueuedVoiceReset = true;
                            }
                        }
                        if (rx.p25VoiceSlotProbePending && rx.p25VoiceDecodeEnabled && rx.p25VoicePhase2) {
                            std::unique_lock<std::recursive_mutex> dspLock(rx.dspMutex, std::try_to_lock);
                            if (dspLock.owns_lock()) {
                                appliedQueuedSlot = static_cast<uint8_t>(rx.p25VoiceSlotProbeRequested & 0x01u);
                                applyP25Phase2SlotProbeLocked(rx, appliedQueuedSlot, QDateTime::currentMSecsSinceEpoch());
                                monP25VoiceDecode = rx.p25VoiceDecodeEnabled;
                                monP25VoicePhase2 = rx.p25VoicePhase2;
                                skipP25VoiceWindow = true;
                                mgr.setReceiverCursorToLiveEdge(i, rx);
                                phase2IqByRx.erase(&rx);
                                pendingAudioByRx[&rx].clear();
                                appliedQueuedSlotProbe = true;
                            }
                        }
                        rxAudioOutputs = rx.audioOutputIndices;
                        if (monP25VoiceDecode) {
                            const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
                            if (rx.p25VoiceSettleUntilMs > nowMs) {
                                skipP25VoiceWindow = true;
                            } else if (rx.p25VoiceDiscardWindows > 0) {
                                --rx.p25VoiceDiscardWindows;
                                skipP25VoiceWindow = true;
                            }
                            if (skipP25VoiceWindow) {
                                mgr.setReceiverCursorToLiveEdge(i, rx);
                            }
                        }
                        if (!monP25VoiceDecode && monMode == DemodMode::AUTO && !pwr.empty()) {
                            auto smart = chooseSmartModeAndBandwidth(pwr, sr, cf, monFreq, DemodMode::AUTO, nullptr);
                            monMode = smart.mode;
                            monBw = smart.bandwidthHz;
                            monLpf = smart.lpfHz;
                        }

                        demodFreq = monP25VoiceDecode
                            ? p25VoiceAfcTargetHz(rx, monFreq, monBw)
                            : applyNfmAfcFromSpectrum(rx, pwr, sr, cf, monFreq, monBw, monMode);
                        // P25 TDMA acquisition must stay centered on the granted channel.
                        // The normal NFM AFC can chase adjacent energy during retune and move
                        // the CQPSK target enough to prevent superframe/mask lock.
                        afcOffsetHz = demodFreq - monFreq;

                        rfMetrics = computeRfSquelchMetrics(pwr, sr, cf, demodFreq, monBw, monMode);
                        rfSquelchLevel = rfMetrics.valid
                            ? rfMetrics.signalLevelDb
                            : std::numeric_limits<double>::quiet_NaN();

                        // audit-followup-2: use per-rx cursor consumption. Each rx pulls only its own *new* chronological samples.
                        // No more "demod the latest 25ms overlapping window" for every rx.
                        const bool phase2BufferedDecode = monP25VoiceDecode && monP25VoicePhase2;
                        if (appliedQueuedVoiceReset) {
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
                            phase2IqByRx.erase(&rx);
                            didWork = true;
                            continue;
                        }
                        if (!phase2BufferedDecode) {
                            phase2IqByRx.erase(&rx);
                        }
                        if (phase2BufferedDecode) {
                            const auto now = std::chrono::steady_clock::now();
                            auto& last = lastPhase2DecodeByRx[&rx];
                            if (last.time_since_epoch().count() != 0 &&
                                now - last < std::chrono::milliseconds(kP25Phase2VoiceDecodeCadenceMs)) {
                                didWork = true;
                                continue;
                            }
                            last = now;
                        }
                        size_t tgt = (sr > 0)
                            ? static_cast<size_t>(sr * (phase2BufferedDecode ? kP25Phase2VoiceDecodeWindowSeconds : 0.025))
                            : 8192;
                        if (phase2BufferedDecode) {
                            const size_t liveWindow = (sr > 0.0)
                                ? static_cast<size_t>(std::clamp(sr * kP25Phase2VoiceDecodeWindowSeconds, 48000.0, 4194304.0))
                                : tgt;
                            auto liveWin = mgr.getRecentIQWindowWithCursor(i, liveWindow);
                            mgr.setReceiverCursorToLiveEdge(i, rx);
                            phase2IqByRx.erase(&rx);
                            if (liveWin.samples.empty()) {
                                didWork = true;
                                continue;
                            }
                            iqStartAbsolute = liveWin.startAbsolute;
                            iqStartAbsoluteKnown = liveWin.endAbsolute >= liveWin.startAbsolute &&
                                (liveWin.endAbsolute - liveWin.startAbsolute) == static_cast<uint64_t>(liveWin.samples.size());
                            iq = std::move(liveWin.samples);
                        } else {
                            iq = mgr.getNewSamplesForReceiver(i, rx, tgt);  // updates the live rx's lastConsumedAbsolute
                        }
                    }

                    const size_t got = iq.size();
                    if (got == 0) continue;

                    const double t = got / sr;
                    const double orate = (engineForAudio ? engineForAudio->getSampleRate() : 48000.0);
                    const size_t need = (size_t)std::round(t * orate);
                    auto t0 = std::chrono::steady_clock::now();
                    bool haveP25Audio = false;
                    bool publishVoiceDiag = false;
                    P25VoiceAudioBlock p25Audio;
                    {
                        std::unique_lock<std::recursive_mutex> dspLock(rx.dspMutex, std::try_to_lock);
                        if (!dspLock.owns_lock()) {
                            didWork = true;
                            continue;
                        }
                        if (monP25ControlMute && !monP25VoiceDecode) {
                            rms = -120.0;
                            (void)need;
                        } else if (monP25VoiceDecode) {
                            p25Audio = decodeP25VoiceAudioBlock(rx, iq, sr, cf, demodFreq, orate,
                                iqStartAbsolute, iqStartAbsoluteKnown);
                            publishVoiceDiag =
                                p25Audio.talkgroupId != 0 ||
                                p25Audio.syncs > 0 ||
                                p25Audio.phase2Bursts > 0;
                            haveP25Audio = true;
                            ch = p25VoiceBlockMayEmitAudio(p25Audio) ? p25Audio.audio : std::vector<float>{};
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
                        if (auto* eng = getOrCreateAudioEngine()) {
                            pushAudioFrames(eng, pendingAudioByRx[&rx], ch, rxAudioOutputs);
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
        auto applyAudioSelection = [&]() {
            std::vector<size_t> active;
            for (size_t i = 0; i < useChecks.size(); ++i) {
                if (useChecks[i]->isChecked()) active.push_back(i);
            }
            engine->setActiveOutputs(active);
            for (size_t i = 0; i < active.size(); ++i) {
                if (active[i] < volSliders.size()) {
                    engine->setOutputVolume(i, volSliders[active[i]]->value() / 100.0f);
                }
            }
            monitorMasterVolume = std::clamp(masterSlider->value() / 100.0, 0.0, 1.0);
            engine->setMasterVolume(static_cast<float>(monitorMasterVolume));
            return active;
        };

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
            applyAudioSelection();

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
    std::thread p25ControlWorkerThread;

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
            if (!engineForAudio) {
                engineForAudio = std::make_unique<AudioEngine>();
                engineForAudio->setMasterVolume(static_cast<float>(monitorMasterVolume));
            }
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
    bool monitorAudioLpfEnabled = true;
    double monitorSquelchDb = -105;
    double monitorRfGainDb = 20.0;
    double monitorMasterVolume = 0.85;
    double monitorGain = 1.0; // audio gain, not RF gain
    double monitorWfmDeTauUs = 75.0;
    double monitorWfmPilotNotchR = 0.96;
    double monitorChannelBwHz = 180000.0; // AUTO/WFM default; NFM snaps to 12.5 kHz for modern CB/PMR
    double p25MonitoredControlFreqHz = 0.0;
    bool p25AutoFollowEnabled = false;
    bool p25FollowEnabled = false;
    bool p25FollowAutoActive = false;
    uint32_t p25FollowTalkgroupId = 0;
    double p25AutoFollowReturnControlFreqHz = 0.0;
    double p25AutoFollowVoiceFreqHz = 0.0;
    qint64 p25AutoFollowTunedAtMs = 0;
    qint64 p25AutoFollowLastGrantMs = 0;
    qint64 p25AutoFollowLastActiveMs = 0;
    P25LiveDecoder p25LiveDecoder;
    // Stage 4: keep heavy P25 control-channel decode off the Qt GUI timer.
    // The worker owns this decoder instance; GUI thread only consumes completed results.
    P25LiveDecoder p25ControlWorkerDecoder;
    std::mutex p25ControlWorkerDecoderMutex;
    std::mutex p25ControlPendingMutex;
    std::optional<P25LiveDecodeResult> p25ControlPendingResult;
    std::atomic<bool> p25ControlWorkerBusy{false};
    std::atomic<long long> p25ControlDroppedResults{0};
    P25ControlChannelAnalyzer p25LiveControlAnalyzer;
    std::vector<P25PendingVoiceGrant> p25PendingVoiceGrants;
    QDialog* p25LogDialog = nullptr;
    QTextEdit* p25LogText = nullptr;
    QStringList p25LogLines;
    QStringList p25VisibleLogPending;
    bool p25VisibleLogFlushQueued = false;
    LiveIqCaptureSession liveIqCapture;
    QTimer* liveIqCaptureTimer = nullptr;
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
        p25LogLines << line;
        while (p25LogLines.size() > 300) p25LogLines.removeFirst();
        if (liveIqCapture.active) {
            // Capture logs stream directly to disk. Do not retain capture-time
            // P25 diagnostics in RAM; long TDMA acquisition sessions can produce
            // thousands of lines, and the capture file is the source of truth.
            if (liveIqCapture.p25LogStream.is_open()) {
                liveIqCapture.p25LogStream << line.toStdString() << "\n";
                if (!liveIqCapture.p25LogStream.good()) {
                    ++liveIqCapture.p25CaptureWriteErrors;
                } else {
                    ++liveIqCapture.p25CaptureLinesWritten;
                    if ((liveIqCapture.p25CaptureLinesWritten & 0x3fu) == 0u) {
                        liveIqCapture.p25LogStream.flush();
                    }
                }
            } else {
                ++liveIqCapture.p25CaptureWriteErrors;
            }
        }
        if (p25LogText && p25LogDialog && p25LogDialog->isVisible()) {
            // Batch visible QTextEdit updates. Appending every decoder/log line
            // directly from hot P25 state changes can dominate the Qt thread.
            p25VisibleLogPending << line;
            while (p25VisibleLogPending.size() > 200) p25VisibleLogPending.removeFirst();
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
            rx.resetP25VoiceState();
            p25FollowEnabled = false;
            p25FollowAutoActive = false;
            p25FollowTalkgroupId = 0;
            p25AutoFollowVoiceFreqHz = 0.0;
            p25AutoFollowTunedAtMs = 0;
            p25AutoFollowLastGrantMs = 0;
            p25AutoFollowLastActiveMs = 0;
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

    LiveIqCaptureResult startLiveIqCapture(const std::string& label) {
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

        if (!liveIqCaptureTimer) {
            liveIqCaptureTimer = new QTimer(this);
            connect(liveIqCaptureTimer, &QTimer::timeout, this, [this]() { pollLiveIqCapture(false); });
        }
        // Use a moderately fast poll interval so the ring cursor is drained before
        // the live IQ ring overwrites unread samples. The pull window below is
        // dynamic, so delayed GUI ticks still ask for a larger safety window.
        liveIqCaptureTimer->start(125);

        out.ok = true;
        out.directory = liveIqCapture.directory;
        out.message = "Started continuous IQ capture.";
        return out;
    }

    void pollLiveIqCapture(bool finalPoll) {
        if (!liveIqCapture.active) return;
        auto& mgr = DeviceManager::instance();
        const QDateTime nowUtc = QDateTime::currentDateTimeUtc();

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
        const double requestSeconds = std::clamp(elapsedSeconds * 2.5 + 0.35, 0.75, 3.0);
        const size_t maxPull = static_cast<size_t>(std::clamp(
            liveIqCapture.sampleRateHz * requestSeconds,
            4096.0,
            std::max(4096.0, liveIqCapture.sampleRateHz * 3.0)));
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
                appendP25LogLine(QString("IQ capture ring epoch reset after retune/restart: old_cursor=%1 new_window=%2-%3 skipped_new_epoch_samples=%4")
                    .arg(cursorBefore)
                    .arg(window.startAbsolute)
                    .arg(window.endAbsolute)
                    .arg(ringEpochResetSkipped));
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
        if (liveIqCaptureTimer) liveIqCaptureTimer->stop();
        pollLiveIqCapture(true);
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
        stopDspWorker.store(true, std::memory_order_release);
        if (guiDspWorker.joinable()) {
            guiDspWorker.join();
        }
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
        std::cout << "  [" << i << "] " << d.driver << " " << d.label
                  << " enabled=" << d.enabled
                  << " state=\"" << mgr.getRuntimeStateLabel(i) << "\""
                  << " gain=" << d.gain
                  << " ppm=" << d.frequencyCorrectionPpm << "\n";
    }

    // Use shared_ptr<Receiver> for CLI too (consistent with GUI, enables stable cursor updates across snapshots, cheap to snapshot pointers).
    std::vector<std::shared_ptr<Receiver>> cliReceivers;
    std::mutex cliRxMutex;  // S0-2 (P0): protect CLI receiver vector mutations (command thread) vs mon thread iteration
    std::unique_ptr<AudioEngine> cliAudio;
    std::atomic<bool> cliStop{false};
    std::atomic<bool> cliAudioEnabled{false};
    std::map<long long, P25ControlChannelAnalyzer> cliP25Analyzers;
    std::map<size_t, P25LiveDecoder> cliP25LiveDecoders;

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

    // CLI DSP monitor thread (mirrors guiDspWorker but standalone, uses Receiver vector + own demod instances)
    std::thread cliMonThread([&]() {
        std::map<Receiver*, std::chrono::steady_clock::time_point> lastPhase2DecodeByRx;
        std::map<Receiver*, std::vector<float>> pendingAudioByRx;
        std::map<Receiver*, RollingIqWindow> phase2IqByRx;
        while (!cliStop) {
            bool did = false;
            // S0-2 (P0): snapshot under lock (shared_ptrs — cheap, stable objects for cursor + demod state).
            std::vector<std::shared_ptr<Receiver>> rxSnap;
            {
                std::lock_guard<std::mutex> lk(cliRxMutex);
                rxSnap.reserve(cliReceivers.size());
                for (auto& r : cliReceivers) if (r && r->active) rxSnap.push_back(r);
            }
            auto receiverStillActive = [&rxSnap](const Receiver* ptr) {
                return std::any_of(rxSnap.begin(), rxSnap.end(),
                    [ptr](const std::shared_ptr<Receiver>& r) { return r.get() == ptr; });
            };
            for (auto it = lastPhase2DecodeByRx.begin(); it != lastPhase2DecodeByRx.end();) {
                it = receiverStillActive(it->first) ? std::next(it) : lastPhase2DecodeByRx.erase(it);
            }
            for (auto it = pendingAudioByRx.begin(); it != pendingAudioByRx.end();) {
                it = receiverStillActive(it->first) ? std::next(it) : pendingAudioByRx.erase(it);
            }
            for (auto it = phase2IqByRx.begin(); it != phase2IqByRx.end();) {
                it = receiverStillActive(it->first) ? std::next(it) : phase2IqByRx.erase(it);
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
                bool skipP25VoiceWindow = false;
                bool appliedQueuedVoiceReset = false;
                uint64_t iqStartAbsolute = 0;
                bool iqStartAbsoluteKnown = false;
                std::vector<std::complex<float>> iq;

                {
                    std::lock_guard<std::mutex> rxLock(rx.stateMutex);
                    if (!rx.active || rx.deviceIndex != di) continue;
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
                    if (rx.p25VoiceResetPending) {
                        if (tryApplyP25VoiceResetLocked(rx)) {
                            rxP25VoiceDecode = rx.p25VoiceDecodeEnabled;
                            rxP25VoicePhase2 = rx.p25VoicePhase2;
                            phase2IqByRx.erase(&rx);
                            pendingAudioByRx[&rx].clear();
                            appliedQueuedVoiceReset = true;
                        }
                    }
                    rxAudioOutputs = rx.audioOutputIndices;
                    if (rxP25VoiceDecode) {
                        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
                        if (rx.p25VoiceSettleUntilMs > nowMs) {
                            skipP25VoiceWindow = true;
                        } else if (rx.p25VoiceDiscardWindows > 0) {
                            --rx.p25VoiceDiscardWindows;
                            skipP25VoiceWindow = true;
                        }
                        if (skipP25VoiceWindow) {
                            mgr.setReceiverCursorToLiveEdge(di, rx);
                        }
                    }

                    // audit-followup-2: cursor-based new samples only for this rx (chronological, no overlap with other rxs on same dev).
                    const bool phase2BufferedDecode = rxP25VoiceDecode && rxP25VoicePhase2;
                    if (appliedQueuedVoiceReset) {
                        did = true;
                        continue;
                    }
                    if (skipP25VoiceWindow) {
                        phase2IqByRx.erase(&rx);
                        did = true;
                        continue;
                    }
                    if (!phase2BufferedDecode) {
                        phase2IqByRx.erase(&rx);
                    }
                    if (phase2BufferedDecode) {
                        const auto now = std::chrono::steady_clock::now();
                        auto& last = lastPhase2DecodeByRx[&rx];
                        if (last.time_since_epoch().count() != 0 &&
                            now - last < std::chrono::milliseconds(kP25Phase2VoiceDecodeCadenceMs)) {
                            did = true;
                            continue;
                        }
                        last = now;
                    }
                    size_t tgt = (sr > 0)
                        ? static_cast<size_t>(sr * (phase2BufferedDecode ? kP25Phase2VoiceDecodeWindowSeconds : 0.025))
                        : 8192;
                    if (phase2BufferedDecode) {
                        const size_t liveWindow = (sr > 0.0)
                            ? static_cast<size_t>(std::clamp(sr * kP25Phase2VoiceDecodeWindowSeconds, 48000.0, 4194304.0))
                            : tgt;
                        auto liveWin = mgr.getRecentIQWindowWithCursor(di, liveWindow);
                        mgr.setReceiverCursorToLiveEdge(di, rx);
                        phase2IqByRx.erase(&rx);
                        if (liveWin.samples.empty()) {
                            did = true;
                            continue;
                        }
                        iqStartAbsolute = liveWin.startAbsolute;
                        iqStartAbsoluteKnown = liveWin.endAbsolute >= liveWin.startAbsolute &&
                            (liveWin.endAbsolute - liveWin.startAbsolute) == static_cast<uint64_t>(liveWin.samples.size());
                        iq = std::move(liveWin.samples);
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
                    if (rxP25ControlMute && !rxP25VoiceDecode) {
                        rms = -120.0;
                        (void)need;
                    } else if (rxP25VoiceDecode) {
                        p25Audio = decodeP25VoiceAudioBlock(rx, iq, sr, cf, demodFreq, orate,
                            iqStartAbsolute, iqStartAbsoluteKnown);
                        haveP25Audio = true;
                        ch = p25VoiceBlockMayEmitAudio(p25Audio) ? p25Audio.audio : std::vector<float>{};
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
                    publishP25VoiceDiagnostics(rx, p25Audio);
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
                    pushAudioFrames(cliAudio.get(), pendingAudioByRx[&rx], ch, rxAudioOutputs);
                }
                did = true;
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

    std::string line;
    try {
        while (true) {
            std::cout << "sdr> " << std::flush;
            if (!std::getline(std::cin, line)) break;
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
                      << "  p25 waitgrant <cc_mhz> [dev] [seconds] [follow] [record[=seconds]] - wait for grant, optionally follow and save follow IQ\n"
                      << "  p25 replay <sigmf-meta|sigmf-data|dir> [target_mhz] [ms] [phase2] [skip=<ms>] [center=<mhz>] [nac=<id> wacn=<id> system=<id>] - replay saved IQ through P25 decoder\n"
                      << "  p25 followtest <sigmf-meta|sigmf-data|dir> <cc_mhz> [ms] [skip=<ms>] [center=<mhz>] [followms=<ms>] [tg=<id>] - replay CC grants and test in-passband voice follow/audio gates\n"
                      << "  p25 voice               - show P25 voice backend status + Phase 2 validation-log path\n"
                      << "  audio list              - list playback devices\n"
                      << "  audio enable <out0> <out1?>\n"
                      << "  audio disable           - stop audio outputs\n"
                      << "  rx add                  - add another receiver entry\n"
                      << "  quit / exit\n";
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
                    rx.active = true;
                }
                if (devIndex < mgr.getDevices().size()) mgr.setCenterFreq(devIndex, ccMhz * 1e6);
                if (!mgr.isStreaming(devIndex) && devIndex < mgr.getDevices().size()) {
                    mgr.setEnabled(devIndex, true);
                    mgr.startStreaming(devIndex, true);
                }
                std::cout << "Monitoring muted P25 control channel " << ccMhz
                          << " MHz on RX" << rxidx << " (use p25 sync to inspect frames, p25 follow for clear TG audio)\n";
            } else if (sub == "waitgrant" || sub == "grantwait" || sub == "watch") {
                double ccMhz = 0.0;
                int devIndex = 0;
                double seconds = 60.0;
                bool followGrant = false;
                bool recordFollowCapture = false;
                double recordFollowSeconds = 8.0;
                if (!(iss >> ccMhz) || ccMhz <= 0.0) {
                    std::cout << "usage: p25 waitgrant <cc_mhz> [dev] [seconds] [follow] [record[=seconds]]\n";
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
                              << "usage: p25 waitgrant <cc_mhz> [dev] [seconds] [follow] [record[=seconds]]\n";
                    continue;
                }
                if (recordFollowCapture) followGrant = true;
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
                mgr.setCenterFreq(static_cast<size_t>(devIndex), ccHz);
                if (!mgr.isStreaming(static_cast<size_t>(devIndex))) {
                    mgr.setEnabled(static_cast<size_t>(devIndex), true);
                    mgr.startStreaming(static_cast<size_t>(devIndex), true);
                }

                cliP25LiveDecoders[static_cast<size_t>(devIndex)] = P25LiveDecoder(p25DiagnosticDecoderConfig());
                auto& decoder = cliP25LiveDecoders[static_cast<size_t>(devIndex)];
                auto& analyzer = cliP25Analyzers[static_cast<long long>(std::llround(ccHz))];
                std::cout << "P25 waitgrant monitoring " << ccMhz << " MHz on dev " << devIndex
                          << " for " << seconds << "s"
                          << (followGrant ? " with follow test" : "")
                          << (recordFollowCapture ? (" record=" + std::to_string(recordFollowSeconds) + "s") : "")
                          << " (raw CC audio muted)" << std::endl;

                const qint64 grantStartMs = QDateTime::currentMSecsSinceEpoch();
                qint64 nextSummaryMs = grantStartMs;
                size_t windows = 0;
                size_t grantCount = 0;
                size_t trustedBlocks = 0;
                size_t encryptedGrantSkips = 0;
                size_t notReadyGrantSkips = 0;
                bool sawNidLock = false;
                std::optional<P25TalkgroupEntry> selectedGrant;
                std::vector<P25PendingVoiceGrant> pendingVoiceGrants;
                std::unordered_map<uint32_t, size_t> skippedEncryptedTgs;
                std::unordered_map<uint32_t, size_t> skippedNotReadyTgs;

                while ((QDateTime::currentMSecsSinceEpoch() - grantStartMs) < static_cast<qint64>(seconds * 1000.0) && !selectedGrant) {
                    std::vector<float> p;
                    double cf = 0.0;
                    double sr = 0.0;
                    if (!mgr.getLatestSpectrum(static_cast<size_t>(devIndex), p, cf, sr) || sr <= 0.0) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(200));
                        continue;
                    }

                    const size_t requestedSamples = static_cast<size_t>(std::clamp(sr * 0.512, 24000.0, 4194304.0));
                    auto iq = mgr.getRecentIQWindow(static_cast<size_t>(devIndex), requestedSamples);
                    if (iq.empty()) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(200));
                        continue;
                    }

                    auto result = decoder.processIq(iq, sr, cf, ccHz);
                    ++windows;
                    p25SeedAnalyzerNacFromDecode(analyzer, result);
                    sawNidLock = sawNidLock || p25DecodeResultHasNidLock(result);

                    auto talkgroups = loadP25Talkgroups();
                    bool changed = false;
                    size_t windowTrustedBlocks = 0;
                    bool windowHadGrant = false;
                    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

                    auto maybeSelectResolvedGrant = [&](const P25ControlEvent& grant) {
                        if (!p25ControlEventIsResolvedVoiceGrant(grant)) return;
                        auto it = std::find_if(talkgroups.begin(), talkgroups.end(), [&](const P25TalkgroupEntry& tg) {
                            return sameP25Talkgroup(tg, ccHz, grant.talkgroupId);
                        });
                        if (it != talkgroups.end() && it->lastVoiceFreqHz > 0.0) {
                            P25TalkgroupEntry candidate = *it;
                            p25AugmentTalkgroupFromKnownSite(candidate, talkgroups, ccHz);
                            if (followGrant) {
                                if (candidate.encryptionKnown && candidate.encrypted) {
                                    ++encryptedGrantSkips;
                                    if (++skippedEncryptedTgs[candidate.talkgroupId] == 1) {
                                        std::cout << "    encrypted grant skipped during follow test: "
                                                  << p25FollowDetailLogText(candidate).toStdString() << "\n";
                                    }
                                    return;
                                }
                                if (!p25TalkgroupCanTuneForFollow(candidate) || candidate.lastVoiceFreqHz <= 0.0) {
                                    ++notReadyGrantSkips;
                                    if (++skippedNotReadyTgs[candidate.talkgroupId] == 1) {
                                        std::cout << "    grant not ready for follow test yet: "
                                                  << p25FollowDetailLogText(candidate).toStdString() << "\n";
                                    }
                                    return;
                                }
                            }
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
                        const bool eventRegistryEligible = !correctedDibitErrors.has_value() ||
                            p25TsbkEventRegistryEligible(*correctedDibitErrors, ev);
                        if (!eventRegistryEligible) {
                            if (correctedDibitErrors.has_value() && p25ControlEventIsVoiceGrant(ev)) {
                                std::cout << "    grant not followable yet: corrected dibits "
                                          << *correctedDibitErrors
                                          << " exceed voice grant threshold "
                                          << kP25VoiceGrantMaxCorrectedDibits << "\n";
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
                        if (correctedDibitErrors.has_value() &&
                            *correctedDibitErrors > kP25RegistryMaxCorrectedDibits &&
                            p25ControlEventIsResolvedVoiceGrant(ev)) {
                            std::cout << "    accepted high-correction resolved grant: corrected="
                                      << *correctedDibitErrors
                                      << " threshold=" << kP25VoiceGrantMaxCorrectedDibits << "\n";
                        }
                        changed = mergeP25TalkgroupEvent(talkgroups, ccHz, ev, nowMs) || changed;
                        maybeSelectResolvedGrant(ev);
                        if (ev.type == P25ControlEventType::IdentifierUpdate &&
                            p25ChannelIdentifierUsable(p25IdentifierFromEvent(ev))) {
                            for (const auto& resolved : p25ResolvePendingVoiceGrants(pendingVoiceGrants, analyzer, nowMs)) {
                                windowHadGrant = true;
                                std::cout << "  pending-resolved after identifier ID "
                                          << static_cast<int>(ev.identifier) << ": "
                                          << p25EventLogText(resolved).toStdString() << "\n";
                                std::cout << "    " << p25GrantDetailLogText(resolved).toStdString() << "\n";
                                changed = mergeP25TalkgroupEvent(talkgroups, ccHz, resolved, nowMs) || changed;
                                maybeSelectResolvedGrant(resolved);
                            }
                        }
                    };

                    for (const auto& block : result.rawTsbkBlocks) {
                        if (!block.fecDecoded || !block.crcValid) continue;
                        ++trustedBlocks;
                        ++windowTrustedBlocks;
                        const auto events = analyzer.ingestTsbk(block.bytes);
                        for (const auto& ev : events) consumeEvent(ev, "TSBK", block.correctedDibitErrors);
                    }
                    for (const auto& pdu : result.phase2MacPdus) {
                        if (!pdu.crcValid) continue;
                        const auto events = analyzer.ingestPhase2MacPdu(pdu.opcode, pdu.offset, pdu.bytes, true);
                        for (const auto& ev : events) consumeEvent(ev, "P2MAC", std::nullopt);
                    }
                    if (changed) {
                        saveP25Talkgroups(talkgroups);
                        std::cout << "  Talkgroup registry updated.\n";
                    }

                    const qint64 loopNowMs = QDateTime::currentMSecsSinceEpoch();
                    if (windowHadGrant || selectedGrant || loopNowMs >= nextSummaryMs) {
                        std::cout << "P25 waitgrant t="
                                  << (static_cast<double>(loopNowMs - grantStartMs) / 1000.0)
                                  << "s path=" << (result.stats.demodPath.empty() ? "unknown" : result.stats.demodPath)
                                  << " syncs=" << result.syncs.size()
                                  << " nidLock=" << (p25DecodeResultHasNidLock(result) ? "yes" : "no")
                                  << " softQ=" << result.stats.softDecisionQuality
                                  << " tsbk=" << result.rawTsbkBlocks.size()
                                  << " p2bursts=" << result.stats.phase2Bursts
                                  << " p2sf=" << result.stats.phase2SuperframeBursts
                                  << " p2mask=" << result.stats.phase2MaskedBursts
                                  << " p2mac=" << result.stats.phase2MacCrcValid << "/" << result.stats.phase2MacPdus
                                  << " " << p25Phase2AcchStatsText(result.stats).toStdString()
                                  << " p2ess=" << (result.stats.phase2EssKnown ? (result.stats.phase2EssEncrypted ? "enc" : "clear") : "unknown")
                                  << std::endl;
                        nextSummaryMs = loopNowMs + 2000;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(selectedGrant ? 0 : 350));
                }

                if (!selectedGrant) {
                    std::cout << "P25 waitgrant finished: no followable voice grant in " << seconds
                              << "s windows=" << windows
                              << " nidLock=" << (sawNidLock ? "yes" : "no")
                              << " trustedBlocks=" << trustedBlocks
                              << " grants=" << grantCount
                              << " encryptedSkipped=" << encryptedGrantSkips
                              << " notReadySkipped=" << notReadyGrantSkips << std::endl;
                    continue;
                }

                auto tg = *selectedGrant;
                {
                    auto registrySnapshot = loadP25Talkgroups();
                    p25AugmentTalkgroupFromKnownSite(tg, registrySnapshot, ccHz);
                }
                std::cout << "P25 waitgrant grant selected: " << p25FollowDetailLogText(tg).toStdString() << std::endl;
                if (!followGrant) continue;
                if (tg.encryptionKnown && tg.encrypted) {
                    std::cout << "P25 waitgrant follow skipped: TG " << tg.talkgroupId << " is known encrypted." << std::endl;
                    continue;
                }
                if (!p25TalkgroupCanTuneForFollow(tg) || tg.lastVoiceFreqHz <= 0.0) {
                    std::cout << "P25 waitgrant follow skipped: TG " << tg.talkgroupId
                              << " does not yet have enough clear/frequency metadata." << std::endl;
                    continue;
                }

                {
                    std::lock_guard<std::mutex> lk(cliRxMutex);
                    ensureCliRxLocked(0);
                    Receiver& rx = *cliReceivers[0];
                    std::lock_guard<std::mutex> rxLock(rx.stateMutex);
                    rx.deviceIndex = static_cast<size_t>(devIndex);
                    rx.resetDemodState();
                    rx.freqHz = tg.lastVoiceFreqHz;
                    rx.mode = DemodMode::NFM;
                    rx.channelBwHz = 12500.0;
                    rx.lpfHz = 3000.0;
                    rx.audioLpfEnabled = false;
                    rx.squelchDb = -105.0;
                    const bool phase2Voice = p25TalkgroupIsPhase2(tg);
                    rx.resetP25VoiceState();
                    rx.p25VoiceResetPending = false;
                    rx.p25VoiceDecodeEnabled = true;
                    rx.p25VoiceClearKnown = tg.encryptionKnown;
                    rx.p25VoiceEncrypted = tg.encrypted;
                    rx.p25VoiceTalkgroupId = tg.talkgroupId;
                    rx.p25VoicePhase2 = phase2Voice;
                    rx.p25VoiceTdmaSlotKnown = tg.tdmaSlotKnown;
                    rx.p25VoiceTdmaSlot = tg.tdmaSlot;
                    rx.p25VoiceSlotProbePending = false;
                    rx.p25VoiceSlotProbeRequested = 0;
                    rx.p25VoiceMaskParamsKnown = tg.p25MaskParamsKnown;
                    rx.p25VoiceNac = tg.nac;
                    rx.p25VoiceWacn = tg.wacn;
                    rx.p25VoiceSystemId = tg.systemId;
                    const qint64 armNowMs = QDateTime::currentMSecsSinceEpoch();
                    rx.p25VoiceSettleUntilMs = armNowMs + p25PostArmSettleMs(rx.p25VoicePhase2);
                    rx.p25VoiceDiscardWindows = p25PostArmDiscardWindows(rx.p25VoicePhase2);
                    rx.p25ControlChannelMute = false;
                    rx.p25VoiceLiveDecoder = P25LiveDecoder(p25VoiceDecoderConfig(rx.p25VoicePhase2));
                    if (rx.p25VoicePhase2 && rx.p25VoiceMaskParamsKnown) {
                        rx.p25VoiceLiveDecoder.setPhase2MaskParameters(rx.p25VoiceNac, rx.p25VoiceWacn, rx.p25VoiceSystemId);
                    } else {
                        rx.p25VoiceLiveDecoder.clearPhase2MaskParameters();
                    }
                    rx.active = true;
                }
                if (cliAudio) cliAudio->clearBuffers();
                if (mgr.isStreaming(static_cast<size_t>(devIndex))) mgr.setCenterFreq(static_cast<size_t>(devIndex), tg.lastVoiceFreqHz);
                {
                    std::lock_guard<std::mutex> lk(cliRxMutex);
                    ensureCliRxLocked(0);
                    Receiver& rx = *cliReceivers[0];
                    std::lock_guard<std::mutex> rxLock(rx.stateMutex);
                    mgr.setReceiverCursorToLiveEdge(static_cast<size_t>(devIndex), rx);
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
                while (QDateTime::currentMSecsSinceEpoch() < voiceDeadlineMs) {
                    P25VoiceDiagSnapshot diag;
                    {
                        std::lock_guard<std::mutex> lk(cliRxMutex);
                        ensureCliRxLocked(0);
                        Receiver& rx = *cliReceivers[0];
                        std::lock_guard<std::mutex> rxLock(rx.stateMutex);
                        diag = rx.p25VoiceDiagnostics;
                    }
                    finalVoiceDiag = diag;
                    const auto code = static_cast<P25VoiceDiagCode>(diag.diag);
                    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
                    std::ostringstream sig;
                    sig << diag.diag << ":" << diag.syncs << ":" << diag.nids << ":" << diag.decodedFrames
                        << ":" << diag.audioSamples << ":" << diag.phase2Bursts << ":" << diag.phase2VoiceCodewords
                        << ":" << diag.phase2SuperframeBursts << ":" << diag.phase2MaskedBursts
                        << ":" << diag.phase2MacCrcValid
                        << ":" << diag.phase2MacNominalCrcValid
                        << ":" << diag.phase2MacAltKindCrcValid
                        << ":" << diag.phase2MacBitSwapCrcValid
                        << ":" << diag.phase2MacSlipCrcValid
                        << ":" << diag.phase2MacInvertCrcValid
                        << ":" << diag.phase2EssKnown << ":" << diag.phase2EssEncrypted;
                    if (sig.str() != lastVoiceSig) {
                        lastVoiceSig = sig.str();
                        std::cout << "  voice stage=" << p25VoiceDiagLabel(code)
                                  << " sync=" << diag.syncs
                                  << " nid=" << diag.nids
                                  << " nidLock=" << (diag.nidLock ? "yes" : "no")
                                  << " imbe=" << diag.imbeFrames
                                  << " decoded=" << diag.decodedFrames
                                  << " audio=" << diag.audioSamples
                                  << " p2bursts=" << diag.phase2Bursts
                                  << " p2vcw=" << diag.phase2VoiceCodewords
                                  << " p2sf=" << diag.phase2SuperframeBursts
                                  << " p2mask=" << diag.phase2MaskedBursts
                                  << " p2mac=" << diag.phase2MacCrcValid << "/" << diag.phase2MacPdus
                                  << " " << p25Phase2AcchStatsText(diag).toStdString()
                                  << " p2ess=" << (diag.phase2EssKnown ? (diag.phase2EssEncrypted ? "enc" : "clear") : "unknown")
                                  << " backend=" << (diag.backendAvailable ? "yes" : "no")
                                  << std::endl;
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
                        slotProbeSnapshot.inPassband = true;
                        slotProbeSnapshot.diag = diag.diag;
                        slotProbeSnapshot.phase2VoiceCodewords = diag.phase2VoiceCodewords;
                        slotProbeSnapshot.phase2SuperframeBursts = diag.phase2SuperframeBursts;
                        slotProbeSnapshot.phase2MaskedBursts = diag.phase2MaskedBursts;
                        slotProbeSnapshot.phase2MacPdus = diag.phase2MacPdus;
                        slotProbeSnapshot.phase2MacCrcValid = diag.phase2MacCrcValid;
                        slotProbeSnapshot.phase2EssKnown = diag.phase2EssKnown;
                        const auto slotProbeDecision = evaluateP25SlotProbe(slotProbeSnapshot);
                        if (slotProbeDecision.resetTracking) {
                            cliSlotProbeTg = tg.talkgroupId;
                            cliSlotProbeVoiceHz = tg.lastVoiceFreqHz;
                            cliSlotProbeArmMs = voiceStartMs;
                            cliLastSlotProbeFlipMs = 0;
                        }
                        cliWrongSlotChecks = slotProbeDecision.wrongSlotChecksAfterObservation;
                        cliSlotProbeFlips = slotProbeDecision.flipCountAfterObservation;
                        if (slotProbeDecision.shouldFlip) {
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
                                    oldSlot = rx.p25VoiceTdmaSlotKnown
                                        ? static_cast<uint8_t>(rx.p25VoiceTdmaSlot & 0x01u)
                                        : static_cast<uint8_t>(cliSlotProbeFlips & 0x01u);
                                    newSlot = static_cast<uint8_t>((oldSlot ^ 0x01u) & 0x01u);
                                    rearmCliP25Phase2Slot(rx, newSlot);
                                    flipped = true;
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
                    followSnapshot.autoActive = true;
                    followSnapshot.talkgroupId = diag.talkgroupId;
                    followSnapshot.fallbackTalkgroupId = tg.talkgroupId;
                    followSnapshot.diag = diag.diag;
                    followSnapshot.syncs = diag.syncs;
                    followSnapshot.nids = diag.nids;
                    followSnapshot.imbeFrames = diag.imbeFrames;
                    followSnapshot.decodedFrames = diag.decodedFrames;
                    followSnapshot.phase2Bursts = diag.phase2Bursts;
                    followSnapshot.phase2VoiceCodewords = diag.phase2VoiceCodewords;
                    followSnapshot.phase2SuperframeBursts = diag.phase2SuperframeBursts;
                    followSnapshot.phase2MaskedBursts = diag.phase2MaskedBursts;
                    followSnapshot.phase2MacPdus = diag.phase2MacPdus;
                    followSnapshot.phase2MacCrcValid = diag.phase2MacCrcValid;
                    followSnapshot.phase2EssKnown = diag.phase2EssKnown;
                    followSnapshot.phase2EssEncrypted = diag.phase2EssEncrypted;
                    const auto followDecision = evaluateP25Follow(followSnapshot);
                    if (followDecision.voiceStillLooksActive) cliFollowLastActiveMs = nowMs;
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
                                          << " sf=" << diag.phase2SuperframeBursts
                                          << " mask=" << diag.phase2MaskedBursts
                                          << " mac=" << diag.phase2MacCrcValid << "/" << diag.phase2MacPdus
                                          << " p2vcw=" << diag.phase2VoiceCodewords << std::endl;
                            } else {
                                std::cout << "  TDMA ACQ watchdog: sf/mask present but MAC/ESS did not progress for TG "
                                          << tg.talkgroupId << "; returning to control channel."
                                          << " sf=" << diag.phase2SuperframeBursts
                                          << " mask=" << diag.phase2MaskedBursts
                                          << " mac=" << diag.phase2MacCrcValid << "/" << diag.phase2MacPdus
                                          << " p2vcw=" << diag.phase2VoiceCodewords << std::endl;
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
                    if (diag.decodedFrames > 0 && diag.audioSamples > 0) {
                        followCaptureReason = QStringLiteral("audio_opened");
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
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
                        lastVoiceSig);
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
                auto result = decoder.processIq(iq, sr, cf, targetHz);
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
                                      << " corrected dibits are saved/followed\n";
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
                    std::cout << "  note: clear Phase 2 AMBE audio is gated until TDMA mask, MAC/ESS clear state, and AMBE validation all pass.\n";
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
                        const auto events = analyzer.ingestPhase2MacPdu(pdu.opcode, pdu.offset, pdu.bytes, pdu.crcValid);
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
                    std::cout << "usage: p25 followtest <sigmf-meta|sigmf-data|capture_dir> <cc_mhz> [ms] [skip=<ms>] [center=<mhz>] [followms=<ms>] [tg=<id>]\n";
                    if (!args.error.empty()) std::cout << args.error << "\n";
                    continue;
                }
                runP25ReplayFollowTest(args);
            } else if (sub == "replay") {
                std::string rest;
                std::getline(iss, rest);
                const auto args = parseP25ReplayCliArgs(rest);
                if (!args.ok) {
                    std::cout << args.error << "\n";
                    continue;
                }

                const double maxMs = args.ms > 0.0 ? std::clamp(args.ms, 50.0, 20000.0) : 0.0;
                auto capture = loadSigmfCf32Capture(QString::fromStdString(args.path), maxMs, args.skipMs);
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

                P25LiveDecoder decoder(args.phase2Voice ? p25VoiceDecoderConfig(true) : p25DiagnosticDecoderConfig());
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
                constexpr size_t kMaxReplayReports = 12;
                for (size_t start = 0; start < capture.iq.size(); start += hopSamples) {
                    const size_t end = std::min(capture.iq.size(), start + windowSamples);
                    if (end <= start) break;
                    std::vector<std::complex<float>> window(capture.iq.begin() + static_cast<std::ptrdiff_t>(start),
                                                            capture.iq.begin() + static_cast<std::ptrdiff_t>(end));
                    auto result = decoder.processIq(window, capture.sampleRateHz, capture.centerFreqHz, targetHz);
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
            } else if (sub == "voice") {
                P25ImbeVoiceDecoder imbe;
                P25AmbeVoiceDecoder ambe;
                std::cout << "P25 IMBE backend: " << (imbe.backendAvailable() ? "available" : "not available")
                          << (imbe.backendAvailable()
                              ? " (clear Phase 1 IMBE frame decode can run after LDU voice extraction)"
                              : " (build with SDR_TOWN_ENABLE_MBELIB=ON and mbelib installed)") << "\n"
                          << "P25 AMBE backend: " << (ambe.backendAvailable() ? "available" : "not available")
                          << (ambe.backendAvailable()
                              ? " (clear Phase 2 AMBE synthesis backend is available after TDMA mask, MAC/ESS clear state, and AMBE validation all pass)"
                              : " (build with SDR_TOWN_ENABLE_MBELIB=ON and mbelib installed)") << "\n"
                          << "P25 Phase 2 status: TDMA sync/DUID/2V/4V burst detection is enabled. Clear AMBE audio remains muted until superframe timing, mask application, and MAC/ESS state are complete.\n"
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
                    std::cout << "TG " << tg.talkgroupId << " has unknown encryption state; wait for a clear grant before following\n";
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
                              << "; following with TDMA burst diagnostics; audio is gated until TDMA mask, MAC/ESS clear state, and AMBE validation all pass\n";
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
                    rx.resetP25VoiceState();
                    rx.p25VoiceResetPending = false;
                    rx.p25VoiceDecodeEnabled = true;
                    rx.p25VoiceClearKnown = tg.encryptionKnown;
                    rx.p25VoiceEncrypted = tg.encrypted;
                    rx.p25VoiceTalkgroupId = tg.talkgroupId;
                    rx.p25VoicePhase2 = phase2Voice;
                    rx.p25VoiceTdmaSlotKnown = tg.tdmaSlotKnown;
                    rx.p25VoiceTdmaSlot = tg.tdmaSlot;
                    rx.p25VoiceSlotProbePending = false;
                    rx.p25VoiceSlotProbeRequested = 0;
                    rx.p25VoiceMaskParamsKnown = tg.p25MaskParamsKnown;
                    rx.p25VoiceNac = tg.nac;
                    rx.p25VoiceWacn = tg.wacn;
                    rx.p25VoiceSystemId = tg.systemId;
                    const qint64 armNowMs = QDateTime::currentMSecsSinceEpoch();
                    rx.p25VoiceSettleUntilMs = armNowMs + p25PostArmSettleMs(rx.p25VoicePhase2);
                    rx.p25VoiceDiscardWindows = p25PostArmDiscardWindows(rx.p25VoicePhase2);
                    rx.p25ControlChannelMute = false;
                    rx.p25VoiceLiveDecoder = P25LiveDecoder(p25VoiceDecoderConfig(rx.p25VoicePhase2));
                    if (rx.p25VoicePhase2 && rx.p25VoiceMaskParamsKnown) {
                        rx.p25VoiceLiveDecoder.setPhase2MaskParameters(rx.p25VoiceNac, rx.p25VoiceWacn, rx.p25VoiceSystemId);
                    } else {
                        rx.p25VoiceLiveDecoder.clearPhase2MaskParameters();
                    }
                    rx.active = true;
                }
                if (devIndex < mgr.getDevices().size()) mgr.setCenterFreq(devIndex, tg.lastVoiceFreqHz);
                if (!mgr.isStreaming(devIndex) && devIndex < mgr.getDevices().size()) {
                    mgr.setEnabled(devIndex, true);
                    mgr.startStreaming(devIndex, true);
                }
                {
                    std::lock_guard<std::mutex> lk(cliRxMutex);
                    ensureCliRxLocked(static_cast<size_t>(std::max(0, rxidx)));
                    Receiver& rx = *cliReceivers[static_cast<size_t>(std::max(0, rxidx))];
                    std::lock_guard<std::mutex> rxLock(rx.stateMutex);
                    mgr.setReceiverCursorToLiveEdge(devIndex, rx);
                }
                std::cout << p25FollowDetailLogText(tg).toStdString() << "\n";
                std::cout << "Following P25 TG " << tg.talkgroupId
                          << " at " << (tg.lastVoiceFreqHz / 1e6) << " MHz on RX" << rxidx
                          << (p25TalkgroupIsPhase2(tg)
                              ? " with TDMA diagnostics; Phase 2 audio is gated until TDMA mask, MAC/ESS clear state, and AMBE validation all pass\n"
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
