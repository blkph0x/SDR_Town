#pragma once

#include <string>
#include <vector>

struct TrackLimits {
    double minAz = 0.0;
    double maxAz = 360.0;
    double minEl = 0.0;
    double maxEl = 90.0;
};

struct TrackPoint {
    bool send = false;
    double az = 0.0;
    double el = 0.0;
    bool clamped = false;
    std::string reason;
};

// Direction-dependent offset. Positive backlash adds when azimuth is increasing.
double applyBacklashDeg(double commandedAz, double previousAz, double backlashDeg);

// Lead the next SGP4 sample. leadSec is clamped to one sample.
double feedForwardAz(double azNow, double azNext, double sampleSec, double leadSec);

TrackPoint planTrackTick(bool armed, bool fresh, bool autoEnabled, bool jogPaused,
                         const TrackLimits& limits, double predictedAz, double predictedEl,
                         double backlashDeg, double previousAz);

// Inclusive raster around a GEO prediction. Each axis is at most 7 points,
// so a scan is at most 49 dwells. A larger request is rejected.
struct BoxScan {
    bool accepted = false;
    std::string reject;
    std::vector<double> azimuthDeg;
    std::vector<double> elevationDeg;
};

BoxScan planBoxScan(double centerAz, double centerEl, double spanAzDeg, double spanElDeg, double stepDeg);
