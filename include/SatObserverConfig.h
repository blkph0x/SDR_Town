#pragma once

#include <nlohmann/json.hpp>
#include <string>

// Ground observer for pass / Doppler predictions (both hemispheres).
struct SatObserverConfig {
    double latDeg = 0.0;   // +N / -S
    double lonDeg = 0.0;   // +E / -W
    double altM = 0.0;
    double minElevationDeg = 10.0;

    nlohmann::json toJson() const;
    static SatObserverConfig fromJson(const nlohmann::json& j);
    void load();
    void save() const;

    // Parse "33.8S", "-33.8", "151.2E", "151.2 W", etc. Returns false on failure.
    static bool parseLatLonToken(const std::string& token, bool isLat, double* outDeg);
};

std::string satcomDataDir();
