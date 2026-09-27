#pragma once
#include <string>
#include <optional>
#include "InmarsatAdsc.h"

enum class InmarsatMessageDirection { Unknown, GroundToAir, AirToGround };

struct InmarsatAcarsApplication {
    std::string protocol;
    std::string status;
    std::string text;
    std::optional<InmarsatAdscPosition> aircraft;
    bool hasPosition = false;
};

// Application interpretation only: never changes raw ACARS bytes or RF validity.
InmarsatAcarsApplication decodeInmarsatAcarsApplication(
    const std::string& label, const std::string& raw,
    InmarsatMessageDirection direction = InmarsatMessageDirection::Unknown,
    bool includesDownlinkHeader = false);
