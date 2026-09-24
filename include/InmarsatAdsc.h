#pragma once
#include <optional>
#include <string>
#include <string_view>
#include <cstdint>
struct InmarsatAdscPosition {
    double latitude=0, longitude=0, altitudeFt=0, secondsPastHour=0;
    uint32_t airframeId=0;
    std::string registration, callsign;
};
class InmarsatAdsc {
public:
    // Complete, reassembled ARINC622 ADS application. Never parse printable
    // coordinates or waypoints as position. Nullopt includes bad CRC/truncation.
    static std::optional<InmarsatAdscPosition> parse(std::string_view acars);
};
