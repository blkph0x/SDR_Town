#include "frontend/RotatorTrack.h"

#include <algorithm>
#include <cmath>

double applyBacklashDeg(double commandedAz, double previousAz, double backlashDeg) {
    if (!std::isfinite(commandedAz) || !std::isfinite(backlashDeg) || backlashDeg == 0.0) return commandedAz;
    if (!std::isfinite(previousAz)) return commandedAz;
    const double direction = commandedAz >= previousAz ? 1.0 : -1.0;
    return commandedAz + direction * std::abs(backlashDeg);
}

double feedForwardAz(double azNow, double azNext, double sampleSec, double leadSec) {
    if (!std::isfinite(azNow) || !std::isfinite(azNext) || !std::isfinite(sampleSec) || sampleSec <= 0.0)
        return azNow;
    const double lead = std::clamp(leadSec, 0.0, sampleSec);
    return azNow + (azNext - azNow) * (lead / sampleSec);
}

TrackPoint planTrackTick(bool armed, bool fresh, bool autoEnabled, bool jogPaused,
                         const TrackLimits& limits, double predictedAz, double predictedEl,
                         double backlashDeg, double previousAz) {
    TrackPoint out;
    if (!autoEnabled) { out.reason = "tracking disabled"; return out; }
    if (jogPaused) { out.reason = "manual jog"; return out; }
    if (!armed || !fresh) { out.reason = "rotator not armed"; return out; }
    if (!std::isfinite(predictedAz) || !std::isfinite(predictedEl)) {
        out.reason = "prediction not finite";
        return out;
    }
    out.az = applyBacklashDeg(predictedAz, previousAz, backlashDeg);
    out.el = predictedEl;
    if (out.az < limits.minAz) { out.az = limits.minAz; out.clamped = true; }
    if (out.az > limits.maxAz) { out.az = limits.maxAz; out.clamped = true; }
    if (out.el < limits.minEl) { out.el = limits.minEl; out.clamped = true; }
    if (out.el > limits.maxEl) { out.el = limits.maxEl; out.clamped = true; }
    out.send = true;
    out.reason = out.clamped ? "clamped" : "track";
    return out;
}

BoxScan planBoxScan(double centerAz, double centerEl, double spanAzDeg, double spanElDeg, double stepDeg) {
    BoxScan scan;
    if (!std::isfinite(centerAz) || !std::isfinite(centerEl) ||
        !std::isfinite(spanAzDeg) || !std::isfinite(spanElDeg) || !std::isfinite(stepDeg) ||
        spanAzDeg < 0.0 || spanElDeg < 0.0 || !(stepDeg > 0.0) ||
        spanAzDeg > 8.0 || spanElDeg > 8.0) {
        scan.reject = "Box scan span or step is not usable";
        return scan;
    }
    const auto count = [](double span, double step) {
        if (span == 0.0) return 1;
        return static_cast<int>(std::floor(span / step + 1e-6)) + 1;
    };
    const int azCount = count(spanAzDeg, stepDeg);
    const int elCount = count(spanElDeg, stepDeg);
    if (azCount < 1 || elCount < 1 || azCount > 7 || elCount > 7) {
        scan.reject = "Box scan is larger than 49 dwells";
        return scan;
    }
    for (int el = 0; el < elCount; ++el) {
        const double elOffset = elCount == 1 ? 0.0 : (-spanElDeg / 2.0 + el * stepDeg);
        for (int az = 0; az < azCount; ++az) {
            const double azOffset = azCount == 1 ? 0.0 : (-spanAzDeg / 2.0 + az * stepDeg);
            scan.azimuthDeg.push_back(centerAz + azOffset);
            scan.elevationDeg.push_back(centerEl + elOffset);
        }
    }
    scan.accepted = true;
    return scan;
}
