#include "DemodModeUtils.h"

#include "P25FollowStateMachine.h"

#include <cctype>
#include <cmath>
#include <string>
#include <vector>

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
