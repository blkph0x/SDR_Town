#pragma once

#include <string>
#include <string_view>

enum class P25DebugStage {
    All,
    Iq,
    Demod,
    Framing,
    Vocoder,
    Audio,
    Trunking
};

inline P25DebugStage p25ParseDebugStage(std::string_view text) noexcept
{
    if (text.empty() || text == "all") return P25DebugStage::All;
    if (text == "iq") return P25DebugStage::Iq;
    if (text == "demod") return P25DebugStage::Demod;
    if (text == "framing") return P25DebugStage::Framing;
    if (text == "vocoder") return P25DebugStage::Vocoder;
    if (text == "audio") return P25DebugStage::Audio;
    if (text == "trunking") return P25DebugStage::Trunking;
    return P25DebugStage::All;
}

inline const char* p25DebugStageLabel(P25DebugStage stage) noexcept
{
    switch (stage) {
    case P25DebugStage::Iq: return "iq";
    case P25DebugStage::Demod: return "demod";
    case P25DebugStage::Framing: return "framing";
    case P25DebugStage::Vocoder: return "vocoder";
    case P25DebugStage::Audio: return "audio";
    case P25DebugStage::Trunking: return "trunking";
    case P25DebugStage::All:
    default: return "all";
    }
}

inline bool p25DebugStageAllows(P25DebugStage filter, P25DebugStage stage) noexcept
{
    return filter == P25DebugStage::All || filter == stage;
}
