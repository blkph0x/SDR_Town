#pragma once

#include <cstddef>
#include <string_view>

// Modes implemented by the vendored decoder (Dayton crate + handbook/QSSTV extras).
// vis is the 7-bit VIS code, or 0 when the mode has no 7-bit VIS (FAX480, 16-bit MP/MR/ML).
struct SstvModeSpec {
    const char* id;
    const char* label;
    unsigned vis; // 7-bit VIS; 0 = none / 16-bit only
    int width;
    int height;
    int durationSec; // image body, excluding header
};

inline constexpr SstvModeSpec kSstvModes[] = {
    {"scottie1", "Scottie 1", 60, 320, 256, 110},
    {"scottie2", "Scottie 2", 56, 320, 256, 71},
    {"scottiedx", "Scottie DX", 76, 320, 256, 269},
    {"scottie3", "Scottie S3", 52, 320, 128, 55},
    {"scottie4", "Scottie S4", 48, 160, 128, 36},
    {"martin1", "Martin M1", 44, 320, 256, 114},
    {"martin2", "Martin M2", 40, 320, 256, 58},
    {"martin3", "Martin M3", 36, 320, 128, 57},
    {"martin4", "Martin M4", 32, 160, 128, 29},
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
    {"robotbw8", "Robot B&W 8", 2, 160, 120, 8},
    {"robotbw12", "Robot B&W 12", 6, 160, 120, 12},
    {"robot24", "Robot 24", 4, 160, 120, 24},
    {"sc230", "Wraase SC2-30", 51, 256, 128, 30},
    {"sc260", "Wraase SC2-60", 59, 256, 256, 60},
    {"sc2120", "Wraase SC2-120", 63, 320, 256, 122},
    {"avt24", "AVT 24", 64, 128, 120, 23},
    {"avt90", "AVT 90", 68, 256, 240, 90},
    {"avt94", "AVT 94", 72, 320, 200, 94},
    {"avt188", "AVT 188", 74, 320, 400, 188},
    {"sc124", "Wraase SC-1 24", 16, 128, 128, 23},
    {"sc148", "Wraase SC-1 48", 20, 128, 256, 46},
    {"sc148q", "Wraase SC-1 48Q", 24, 256, 128, 44},
    {"sc196", "Wraase SC-1 96", 28, 256, 256, 88},
    {"fax480", "FAX480", 0, 512, 500, 134},
    {"mp73", "MP73", 0, 320, 256, 73},
    {"mp115", "MP115", 0, 320, 256, 115},
    {"mp140", "MP140", 0, 320, 256, 140},
    {"mp175", "MP175", 0, 320, 256, 175},
    {"mr73", "MR73", 0, 320, 256, 73},
    {"mr90", "MR90", 0, 320, 256, 90},
    {"mr115", "MR115", 0, 320, 256, 115},
    {"mr140", "MR140", 0, 320, 256, 140},
    {"ml180", "ML180", 0, 640, 496, 180},
    {"ml240", "ML240", 0, 640, 496, 240},
    {"ml280", "ML280", 0, 640, 496, 280},
    {"ml320", "ML320", 0, 640, 496, 320},
};

inline constexpr int kSstvModeCount = int(sizeof(kSstvModes) / sizeof(kSstvModes[0]));
inline constexpr int kSstvMaxDurationSec = 540;

inline const SstvModeSpec* sstvModeById(std::string_view id) {
    for (const auto& m : kSstvModes)
        if (id == m.id) return &m;
    return nullptr;
}

inline const SstvModeSpec* sstvModeByVis(unsigned vis) {
    if (vis == 0) return nullptr;
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
