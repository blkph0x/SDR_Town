#pragma once

#include "frontend/FrontEndMetrics.h"
#include "frontend/StationProfile.h"

#include <cstdint>
#include <functional>
#include <string>

// DEC-0210 sky sample. accepted means the station may point at it.
struct SkyFeed {
    bool accepted = false;
    std::string reject;
    double azimuthDeg = 0;
    double elevationDeg = -90;
    double trueRfHz = 0;
    double dopplerHz = 0;
    bool tleFresh = false;
};

// An armed planner sample is accepted only with a finite look, elevation at
// or above the mask, a known TLE age inside the station limit, and a finite
// non-negative carrier. Doppler is added to the nominal RF.
SkyFeed skyFeedFromPass(bool passArmed,
                        bool lookValid,
                        double azimuthDeg,
                        double elevationDeg,
                        double nominalHz,
                        double dopplerHz,
                        std::int64_t tleAgeSec,
                        double minElevationDeg,
                        int tleMaxAgeHours);

// The application installs the SatPassPlanner reader. Tests keep the default,
// which reports that no pass is armed.
using StationPlannerSource = std::function<SkyFeed(double minElevationDeg, int tleMaxAgeHours)>;
void setStationPlannerSource(StationPlannerSource source);
SkyFeed readArmedSatellite(double minElevationDeg, int tleMaxAgeHours);

bool writePassFolder(const std::string& directory,
                     const StationProfile& profile,
                     const FrontEndMetrics& metrics,
                     std::string* error);
