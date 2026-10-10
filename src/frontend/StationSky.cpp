#include "frontend/StationSky.h"

#include <cmath>
#include <filesystem>
#include <fstream>

namespace {
StationPlannerSource& plannerSource() {
    static StationPlannerSource source = [](double minElevationDeg, int tleMaxAgeHours) {
        return skyFeedFromPass(false, false, 0.0, -90.0, 0.0, 0.0, -1, minElevationDeg, tleMaxAgeHours);
    };
    return source;
}
}

SkyFeed skyFeedFromPass(bool passArmed,
                        bool lookValid,
                        double azimuthDeg,
                        double elevationDeg,
                        double nominalHz,
                        double dopplerHz,
                        std::int64_t tleAgeSec,
                        double minElevationDeg,
                        int tleMaxAgeHours) {
    SkyFeed feed;
    feed.azimuthDeg = azimuthDeg;
    feed.elevationDeg = elevationDeg;
    feed.dopplerHz = std::isfinite(dopplerHz) ? dopplerHz : 0.0;
    if (!passArmed) {
        feed.reject = "No armed satellite pass";
        return feed;
    }
    if (!lookValid || !std::isfinite(azimuthDeg) || !std::isfinite(elevationDeg)) {
        feed.reject = "Armed pass has no look angle";
        return feed;
    }
    if (elevationDeg < minElevationDeg) {
        feed.reject = "Armed pass is below the elevation mask";
        return feed;
    }
    if (tleAgeSec < 0) {
        feed.reject = "TLE age is unknown";
        return feed;
    }
    const auto maxAge = static_cast<std::int64_t>(tleMaxAgeHours) * 3600;
    if (tleMaxAgeHours < 1 || tleAgeSec > maxAge) {
        feed.reject = "TLE is older than the station limit";
        return feed;
    }
    feed.tleFresh = true;
    if (!std::isfinite(nominalHz) || nominalHz < 0.0 || !std::isfinite(dopplerHz)) {
        feed.reject = "Armed pass frequency is not finite";
        return feed;
    }
    feed.trueRfHz = nominalHz + dopplerHz;
    if (!(feed.trueRfHz > 0.0) || !std::isfinite(feed.trueRfHz)) {
        feed.reject = "Doppler-corrected frequency is not usable";
        return feed;
    }
    feed.accepted = true;
    return feed;
}

void setStationPlannerSource(StationPlannerSource source) {
    if (source) plannerSource() = std::move(source);
}

SkyFeed readArmedSatellite(double minElevationDeg, int tleMaxAgeHours) {
    return plannerSource()(minElevationDeg, tleMaxAgeHours);
}

bool writePassFolder(const std::string& directory,
                     const StationProfile& profile,
                     const FrontEndMetrics& metrics,
                     std::string* error) {
    std::error_code code;
    std::filesystem::create_directories(directory, code);
    if (code) {
        if (error) *error = code.message();
        return false;
    }
    const auto root = std::filesystem::path(directory);
    std::ofstream profileFile(root / "station-profile.json", std::ios::binary);
    std::ofstream metricFile(root / "metrics.jsonl", std::ios::binary);
    if (!profileFile || !metricFile) {
        if (error) *error = "Could not create the pass log";
        return false;
    }
    profileFile << stationProfileToJson(profile) << '\n';
    metricFile << metrics.toJsonl();
    if (!profileFile || !metricFile) {
        if (error) *error = "Could not finish the pass log";
        return false;
    }
    return true;
}
