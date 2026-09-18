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
