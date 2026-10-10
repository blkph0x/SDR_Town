#include "frontend/StationSky.h"
#include "SatPassPlanner.h"

#include <cmath>

namespace {
SkyFeed captureArmedPass(double minElevationDeg, int tleMaxAgeHours) {
    const auto snap = SatPassPlanner::instance().snapshot();
    const SatCurrentPosition* match = nullptr;
    for (const auto& position : snap.positions) {
        if (position.satId == snap.armed.satId) {
            match = &position;
            break;
        }
    }
    const bool look = match && match->tleValid &&
        std::isfinite(match->azimuthDeg) && std::isfinite(match->elevationDeg);
    return skyFeedFromPass(snap.armed.armed,
                           look,
                           look ? match->azimuthDeg : 0.0,
                           look ? match->elevationDeg : -90.0,
                           snap.armed.freqHz,
                           snap.armed.dopplerHz,
                           snap.tleAgeSec,
                           minElevationDeg,
                           tleMaxAgeHours);
}

struct InstallPlannerCapture {
    InstallPlannerCapture() { setStationPlannerSource(captureArmedPass); }
};

InstallPlannerCapture installPlannerCapture;
}
