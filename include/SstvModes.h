#pragma once

#include <cstddef>
#include <string_view>

// Modes implemented by the pinned unexcellent/sstv Dayton-paper decoder
// (rev 16bf34aac81b0041f5fdce52a1aef64eea0d5f6e). AVT, Robot 8/12/24 B&W,
// and Wraase SC2-30/60/120 are not in that crate.
struct SstvModeSpec {
    const char* id;
    const char* label;
    unsigned vis; // 7-bit VIS
    int width;
    int height;
    int durationSec; // image body, excluding header
};

inline constexpr SstvModeSpec kSstvModes[] = {
    {"scottie1", "Scottie 1", 60, 320, 256, 110},
    {"scottie2", "Scottie 2", 56, 320, 256, 71},
    {"scottiedx", "Scottie DX", 76, 320, 256, 269},
    {"martin1", "Martin M1", 44, 320, 256, 114},
    {"martin2", "Martin M2", 40, 320, 256, 58},
    {"robot36", "Robot 36", 8, 320, 240, 36},
    {"robot72", "Robot 72", 12, 320, 240, 72},
    {"wrasse180", "Wraase SC2-180", 55, 320, 256, 182},
    {"pasokonp3", "Pasokon P3", 113, 640, 496, 203},
    {"pasokonp5", "Pasokon P5", 114, 640, 496, 305},
    {"pasokonp7", "Pasokon P7", 115, 640, 496, 406},
    {"pd50", "PD 50", 93, 320, 256, 50},
    {"pd90", "PD 90", 99, 320, 256, 90},
    {"pd120", "PD 120", 95, 640, 496, 126},
    {"pd160", "PD 160", 98, 512, 400, 161},
    {"pd180", "PD 180", 96, 640, 496, 187},
    {"pd240", "PD 240", 97, 640, 496, 248},
    {"pd290", "PD 290", 94, 800, 616, 289},
};

inline constexpr int kSstvModeCount = int(sizeof(kSstvModes) / sizeof(kSstvModes[0]));
inline constexpr int kSstvMaxDurationSec = 480; // header + PD290/Pasokon P7

inline const SstvModeSpec* sstvModeById(std::string_view id) {
    for (const auto& m : kSstvModes)
        if (id == m.id) return &m;
    return nullptr;
}

inline const SstvModeSpec* sstvModeByVis(unsigned vis) {
    for (const auto& m : kSstvModes)
        if (m.vis == vis) return &m;
    return nullptr;
}

inline bool sstvModeIdOk(std::string_view id) {
    return id == "auto" || sstvModeById(id) != nullptr;
}

inline bool sstvModeDimensionsOk(std::string_view id, int w, int h) {
    if (const auto* m = sstvModeById(id)) return w == m->width && h == m->height;
    return false;
}
