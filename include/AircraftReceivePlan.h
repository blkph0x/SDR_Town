#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

struct AircraftReceivePlan {
    double requestedHz = 20e6;
    double sampleRateHz = 0;
    double hardwareBandwidthHz = 0;
};

// DEC-0161: RF capture span is independent of the analogue audio channel filter.
// Only advertised capabilities may satisfy a wideband request. Unknown is not 20 MHz.
inline AircraftReceivePlan aircraftReceivePlan(double requestedHz,
    const std::vector<double>& rates, const std::vector<double>& bandwidths) {
    AircraftReceivePlan plan;
    plan.requestedHz = requestedHz;
    if (!std::isfinite(requestedHz) || requestedHz < 2e6 || requestedHz > 20e6) return plan;
    for (double rate : rates)
        if (std::isfinite(rate) && rate >= 2e6 && rate <= requestedHz)
            plan.sampleRateHz = std::max(plan.sampleRateHz, rate);
    for (double bw : bandwidths)
        if (std::isfinite(bw) && bw > 0 && bw <= plan.sampleRateHz)
            plan.hardwareBandwidthHz = std::max(plan.hardwareBandwidthHz, bw);
    return plan;
}
