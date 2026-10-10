#pragma once
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

// Playback names that other SSTV programs listen to. Speakers are everything else.
inline bool sstvNameIsVirtualCable(std::string_view name) {
    std::string lower(name);
    for (char& c : lower)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return lower.find("vb-audio") != std::string::npos
        || lower.find("vb-cable") != std::string::npos
        || lower.find("cable input") != std::string::npos
        || lower.find("cable output") != std::string::npos
        || lower.find("voicemeeter") != std::string::npos
        || lower.find("virtual cable") != std::string::npos;
}

inline std::string sstvListenHint(const std::vector<std::string>& names) {
    if (names.empty())
        return "No playback device is selected, so you will not hear the tones. Open Audio settings and choose your speakers.";
    std::vector<std::string> speakers, cables;
    for (const auto& name : names)
        (sstvNameIsVirtualCable(name) ? cables : speakers).push_back(name);
    auto join = [](const std::vector<std::string>& items) {
        std::string out;
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (i) out += ", ";
            out += items[i];
        }
        return out;
    };
    if (speakers.empty())
        return "Sound is only going to " + join(cables)
            + ". Speakers stay quiet. Other SSTV programs can use that cable. Audio settings can also turn your speakers on.";
    if (cables.empty())
        return "You can hear the tones on " + join(speakers)
            + ". P25 mute does not silence this listen. A picture appears when a VIS header is found.";
    return "You can hear the tones on " + join(speakers) + ". "
        + join(cables) + " still feeds other SSTV programs.";
}
